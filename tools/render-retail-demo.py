#!/usr/bin/env python3
# tools/render-retail-demo.py — drive retail to render a single mesh
# under a fixed camera + light, capture the framebuffer. Companion to
# openrecet's --house-preview: same mesh, same camera math, same
# lighting setup → side-by-side pixel diff isolates render-code
# divergences.
#
# How it works:
#
#   1. Spawn retail via Frida with the agent's mesh-load + present-
#      capture hooks installed (same boot path as
#      tools/dump-retail-meshes.py).
#   2. Wait for the d3d_device_ready event so the device pointer is
#      live and the loader prereqs are met.
#   3. Compute view + projection matrices on the host (math3d.c-
#      compatible RH lookat + perspective fov) and send them via the
#      setup_render_demo RPC. The agent allocates them in retail
#      memory once and reuses every frame.
#   4. Agent calls FUN_00472836 to load the mesh, then
#      Interceptor.replace's FUN_004547ab (engine render-thread top
#      level) with a callback that does:
#        Clear → BeginScene → SetTransform×3 → render-state batch →
#        SetLight + LightEnable → SetMaterial → DrawSubset(0..N-1) →
#        EndScene → Present
#   5. The agent's existing Present hook fires on the self-issued
#      Present, so frame N (the requested capture frame) lands in
#      output_dir/frames/frame_NNNNN.bmp.
#   6. We exit once that capture lands.
#
# Camera math mirrors openrecet's --house-preview helper in main.c:
#
#   r       = mesh bound radius (we don't have it on the host pre-load,
#             so the user passes it via --radius / defaults to 305 for
#             shop_1st.x)
#   eye     = centroid + r*0.8 * (cos(45°), sin(45°), -cos(45°))
#   target  = (cx, cy - r*0.05, cz)
#   up      = (0, 1, 0)
#   fov_y   = 45° (engine scene-1 default)
#   aspect  = 4/3
#   z_near  = 0.5
#   z_far   = r*6 + 100
#
# Centroid is also passed in (default for shop_1st.x is what our parser
# computes after Frame transforms: (19.40, 15.48, -13.57)). If you swap
# in another mesh, override via --centroid + --radius.

import argparse
import json
import math
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import frida


ROOT       = Path(__file__).resolve().parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

DEFAULT_REMOTE       = "cutestation.soy:27042"
DEFAULT_DURATION_MS  = 30_000
DEFAULT_OUTPUT_DIR   = ROOT / "runs" / "retail-demo"

# Default camera params (centered on shop_1st.x post-Frame-transform).
DEFAULT_CENTROID = (19.40, 15.48, -13.57)
DEFAULT_RADIUS   = 304.86
DEFAULT_MESH     = "xfile/shop/shop_1st.x"


def wslpath_w(p: Path) -> str:
    import subprocess
    r = subprocess.run(["wslpath", "-w", str(p)],
                       capture_output=True, text=True, check=True)
    return r.stdout.strip()


# Row-major right-handed lookat — matches math3d.c's mat4_lookat_rh
# byte-for-byte (same operations, same order).
def lookat_rh(eye, target, up):
    def sub(a, b): return [a[0]-b[0], a[1]-b[1], a[2]-b[2]]
    def cross(a, b): return [a[1]*b[2]-a[2]*b[1],
                              a[2]*b[0]-a[0]*b[2],
                              a[0]*b[1]-a[1]*b[0]]
    def dot(a, b): return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]
    def norm(v):
        l = math.sqrt(dot(v, v))
        return [v[0]/l, v[1]/l, v[2]/l]
    z = norm(sub(eye, target))
    x = norm(cross(up, z))
    y = cross(z, x)
    # Row-major, row-vector convention: translation in last row.
    return [
        x[0], y[0], z[0], 0.0,
        x[1], y[1], z[1], 0.0,
        x[2], y[2], z[2], 0.0,
        -dot(x, eye), -dot(y, eye), -dot(z, eye), 1.0,
    ]


def perspective_fov_rh(fov_y, aspect, z_near, z_far):
    y_scale = 1.0 / math.tan(fov_y / 2.0)
    x_scale = y_scale / aspect
    # Match math3d.c's mat4_perspective_fov_rh layout exactly.
    return [
        x_scale, 0.0,     0.0,                         0.0,
        0.0,     y_scale, 0.0,                         0.0,
        0.0,     0.0,     z_far / (z_near - z_far),   -1.0,
        0.0,     0.0,     z_near * z_far / (z_near - z_far), 0.0,
    ]


def build_camera(centroid, radius, fov_deg=45.0, aspect=4.0/3.0):
    """Compute view + proj matching openrecet's --house-preview helper."""
    r = max(radius, 1.0)
    d = r * 0.8
    eye = (centroid[0] + d * 0.866,
           centroid[1] + d * 0.500,
           centroid[2] - d * 0.866)
    target = (centroid[0], centroid[1] - r * 0.05, centroid[2])
    up = (0.0, 1.0, 0.0)
    view = lookat_rh(eye, target, up)
    z_near = 0.5
    z_far  = r * 6.0 + 100.0
    proj = perspective_fov_rh(math.radians(fov_deg), aspect, z_near, z_far)
    return view, proj, dict(eye=eye, target=target, fov=fov_deg,
                            z_near=z_near, z_far=z_far)


def write_bmp_topdown_bgra(path: Path, w: int, h: int, bgra: bytes):
    """Inlined BMP writer — same logic as tools/frida_capture.py so we
    don't have to import a private symbol from there. 24-bit BMP with
    a top-down origin (negative biHeight)."""
    import struct
    row_size = ((w * 3 + 3) // 4) * 4
    pad = b"\x00" * (row_size - w * 3)
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            i = (y * w + x) * 4
            b, g, r = bgra[i], bgra[i+1], bgra[i+2]
            pixels.append(b); pixels.append(g); pixels.append(r)
        pixels.extend(pad)
    fhdr = struct.pack("<2sIHHI", b"BM", 14 + 40 + row_size * h, 0, 0, 14 + 40)
    ihdr = struct.pack("<IiiHHIIIIII", 40, w, -h, 1, 24, 0, row_size * h,
                       2835, 2835, 0, 0)
    with path.open("wb") as f:
        f.write(fhdr); f.write(ihdr); f.write(bytes(pixels))


def run(mesh_path: str, centroid, radius, capture_frame: int,
        max_frames: int, duration_ms: int, remote: str,
        output_dir: Path) -> int:
    output_dir.mkdir(parents=True, exist_ok=True)
    frames_dir = output_dir / "frames"
    frames_dir.mkdir(exist_ok=True)
    log_path = output_dir / "agent.log"
    f_log = log_path.open("w", buffering=1)

    view, proj, cam_info = build_camera(centroid, radius)
    (output_dir / "camera.json").write_text(
        json.dumps({"mesh": mesh_path, "centroid": list(centroid),
                    "radius": radius, **cam_info,
                    "view": view, "proj": proj}, indent=2) + "\n")
    f_log.write(f"[camera] eye={cam_info['eye']} target={cam_info['target']}\n")

    captured = []
    device_ready = threading.Event()
    done = threading.Event()

    def on_message(message: dict[str, Any], data: bytes | None):
        if message.get("type") == "error":
            f_log.write(f"[frida-error] {message.get('description','')} @ "
                        f"{message.get('fileName','')}:{message.get('lineNumber','')}\n")
            f_log.write(f"  stack: {message.get('stack','')}\n")
            return
        if message.get("type") != "send":
            return
        p = message.get("payload") or {}
        kind = p.get("kind")
        if kind == "log":
            f_log.write(f"[agent] {p.get('msg','')}\n")
            return
        if kind == "error":
            f_log.write(f"[agent-error] {p.get('where','?')}: {p.get('msg','')}\n")
            return
        if kind == "ready":
            f_log.write(f"[ready] base={p.get('base')}\n")
            return
        if kind == "d3d_device_ready":
            f_log.write(f"[d3d-ready] {p.get('device')}\n")
            device_ready.set()
            return
        if kind == "frame":
            frame = int(p["frame"])
            w     = int(p["w"])
            h     = int(p["h"])
            if data is None or len(data) != w * h * 4:
                f_log.write(f"[frame-bad] frame={frame} got={len(data) if data else 0}\n")
                return
            bmp_path = frames_dir / f"frame_{frame:05d}.bmp"
            write_bmp_topdown_bgra(bmp_path, w, h, data)
            captured.append(frame)
            f_log.write(f"[frame] {bmp_path.name} {w}x{h}\n")
            if frame >= capture_frame:
                done.set()
            return
        if kind == "max_frames_reached":
            f_log.write(f"[max_frames] engine frame={p.get('frame')}\n")
            done.set()
            return

    # Connect + spawn.
    dm = frida.get_device_manager()
    try:
        device = dm.add_remote_device(remote)
    except frida.InvalidArgumentError:
        device = dm.get_device(remote)
    try:
        _ = device.enumerate_processes()
    except frida.ServerNotRunningError as e:
        f_log.write(f"frida-server not reachable at {remote}: {e}\n")
        raise SystemExit(2) from e

    win_exe = wslpath_w(RETAIL_EXE)
    win_cwd = wslpath_w(ASSET_CWD)
    f_log.write(f"[spawn] {win_exe} cwd={win_cwd}\n")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())
    script.on("message", on_message)
    script.load()

    init_cfg = {
        "capture_frames":   [capture_frame],
        "max_frames":       max_frames,
        "hide_window":      True,
        "turbo":            True,
        "turbo_step_ms":    17,
        "silent_audio":     True,
        "force_resolution": [1024, 768],
    }
    script.exports_sync.init(init_cfg)

    t0 = time.monotonic()
    device.resume(pid)

    if not device_ready.wait(timeout=15.0):
        f_log.write("[timeout] d3d_device_ready never fired within 15s\n")
        try: device.kill(pid)
        except Exception: pass
        return 2

    # Issue the demo setup RPC. After this returns the engine's render
    # thread runs our demo callback every iteration.
    ok = script.exports_sync.setup_render_demo({
        "mesh_path": mesh_path,
        "view":      view,
        "proj":      proj,
        "light_dir": [0.5, -1.0, -0.5],
        "ambient":   [0.25, 0.25, 0.25, 1.0],
    })
    if not ok:
        f_log.write("[setup-render-demo] RPC returned false — see agent log\n")
        try: device.kill(pid)
        except Exception: pass
        return 2

    deadline = t0 + (duration_ms / 1000.0)
    while not done.is_set() and time.monotonic() < deadline:
        time.sleep(0.1)
    elapsed = int((time.monotonic() - t0) * 1000)
    f_log.write(f"[done] elapsed_ms={elapsed} captured={captured}\n")

    try: script.unload()
    except Exception as e: f_log.write(f"[shutdown] unload: {e}\n")
    try: device.kill(pid)
    except Exception as e: f_log.write(f"[shutdown] kill: {e}\n")
    f_log.close()
    return 0 if captured else 2


def main():
    ap = argparse.ArgumentParser(
        description="Render a single .x via retail under a known camera; "
                    "capture for pixel-diff against openrecet's "
                    "--house-preview.")
    ap.add_argument("--mesh", default=DEFAULT_MESH,
                    help=f"Engine asset path (default {DEFAULT_MESH}).")
    ap.add_argument("--centroid", default=",".join(str(c) for c in DEFAULT_CENTROID),
                    help=f"Mesh centroid as 'x,y,z' (default "
                         f"{DEFAULT_CENTROID} — shop_1st.x post-Frame).")
    ap.add_argument("--radius", type=float, default=DEFAULT_RADIUS,
                    help=f"Bound radius (default {DEFAULT_RADIUS}).")
    ap.add_argument("--frame", type=int, default=120,
                    help="Engine frame to capture (default 120 — well past "
                         "the d3d_device_ready event).")
    ap.add_argument("--max-frames", type=int, default=600)
    ap.add_argument("--duration-ms", type=int, default=DEFAULT_DURATION_MS)
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE)
    ap.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR))
    args = ap.parse_args()

    centroid = tuple(float(x) for x in args.centroid.split(","))
    if len(centroid) != 3:
        ap.error("--centroid must be 'x,y,z'")

    rc = run(mesh_path=args.mesh, centroid=centroid, radius=args.radius,
             capture_frame=args.frame, max_frames=args.max_frames,
             duration_ms=args.duration_ms, remote=args.frida_remote,
             output_dir=Path(args.output_dir))
    print(f"output dir: {args.output_dir}")
    if rc == 0:
        print("captured OK")
    else:
        print("no capture — see agent.log", file=sys.stderr)
    sys.exit(rc)


if __name__ == "__main__":
    main()
