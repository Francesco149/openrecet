#!/usr/bin/env python3
# tools/dump-retail-meshes.py — Frida-driven retail mesh dumper for parser
# bit-diff validation. Spawns vendor/unpacked/recettear.unpacked.exe with
# the mesh-dump agent hook (installMeshDumpHook in tools/frida/openrecet-
# agent.js), drives it through the title-z-press input trace so retail
# dispatches into INGAME (where the scene-1 asset chain runs), and lays
# every dumped mesh's VB+IB bytes + metadata down under
#   runs/retail-meshes/<sanitized-path>/{vb.bin,ib.bin,info.json}
#
# The bytes are the locked output of ID3DXMesh::LockVertexBuffer /
# LockIndexBuffer, i.e. the FVF-0x152 vertex stream as D3DXLoadMeshFromXof
# emitted it post any engine CloneMeshFVF rewrite. Comparing against our
# xfile + mesh_build pipeline gives byte-level proof of where the two
# parsers diverge (vertex N at (x,y,z) mismatches, face M references a
# different vertex index, …) — see tools/diff-mesh.py for the diff side.
#
# Usage:
#   tools/dump-retail-meshes.py shop_1st
#       — dump every .x load whose path contains "shop_1st"
#   tools/dump-retail-meshes.py shop_ floor_ wall_
#       — multiple substring filters (OR)
#   tools/dump-retail-meshes.py --all
#       — dump every .x the engine loads during the run
#   tools/dump-retail-meshes.py shop_1st --expect 1
#       — exit cleanly as soon as 1 distinct path has been dumped
#         (default: run to max_frames so all matching loads fire)
#
# Defaults match what scenario-test.py uses for retail capture: title-
# z-press input trace + --turbo + --silent-audio + --hide-window. That
# combination reaches INGAME at ~frame 90 inside ~3s wall clock, after
# which FUN_00474681 fires its per-stage .x loads. shop_1st.x is loaded
# during that sequence for the HOUSE stage.
#
# Frida remote defaults to cutestation.soy:27042 (the Windows host's
# frida-server reachable from WSL2 NAT — see feedback_frida_remote.md).
# Override with --frida-remote when running on a different host.

import argparse
import json
import re
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
DUMP_ROOT  = ROOT / "runs" / "retail-meshes"
TRACE      = ROOT / "tests" / "scenarios" / "title-z-press" / "trace.jsonl"

DEFAULT_REMOTE = "cutestation.soy:27042"
DEFAULT_MAX_FRAMES   = 600    # ~10s engine time at 60fps; INGAME hits ~frame 90
DEFAULT_DURATION_MS  = 60_000


def wslpath_w(p: Path) -> str:
    """Linux→Windows path translation via wslpath -w."""
    import subprocess
    r = subprocess.run(["wslpath", "-w", str(p)],
                       capture_output=True, text=True, check=True)
    return r.stdout.strip()


def sanitize_path(p: str) -> str:
    """Engine asset paths look like "xfile/shop/shop_1st.x" — turn that
    into a single directory name "xfile__shop__shop_1st.x" so we don't
    spray subdirectories under runs/retail-meshes/."""
    return re.sub(r"[\\/]", "__", p)


@dataclass
class DumpResult:
    paths:       list[str] = field(default_factory=list)
    exit_code:   int = 0
    elapsed_ms:  int = 0


def run(paths: list[str], dump_all: bool, expect_count: int,
        max_frames: int, duration_ms: int,
        remote: str, output_dir: Path) -> DumpResult:
    output_dir.mkdir(parents=True, exist_ok=True)
    log_path = output_dir / "agent.log"
    f_log = log_path.open("w", buffering=1)
    f_log.write(f"[config] paths={paths} dump_all={dump_all} "
                f"expect={expect_count} max_frames={max_frames}\n")

    # State for the message handler. Each mesh path emits TWO messages
    # (vb + ib); we collect both before writing info.json.
    pending: dict[str, dict[str, Any]] = {}
    completed: set[str] = set()
    last_engine_frame = -1
    done = threading.Event()
    device_ready = threading.Event()

    def write_part(path: str, payload: dict[str, Any], data: bytes):
        buf = payload["buffer"]
        size_expected = int(payload["size_bytes"])
        if data is None or len(data) != size_expected:
            f_log.write(f"[bad-payload] {path} buffer={buf} "
                        f"size_expected={size_expected} got={len(data) if data else 0}\n")
            return

        slot = pending.setdefault(path, {"meta": None, "have_vb": False, "have_ib": False})
        # Capture meta once (it's identical between vb/ib messages).
        if slot["meta"] is None:
            slot["meta"] = {
                "path":          path,
                "num_vertices":  int(payload["num_vertices"]),
                "num_faces":     int(payload["num_faces"]),
                "fvf":           int(payload["fvf"]),
                "options":       int(payload["options"]),
                "vert_size":     int(payload["vert_size"]),
                "index_size":    int(payload["index_size"]),
            }

        sanitized = sanitize_path(path)
        dest = output_dir / sanitized
        dest.mkdir(parents=True, exist_ok=True)
        out_bin = dest / f"{buf}.bin"
        out_bin.write_bytes(data)
        slot[f"have_{buf}"] = True
        f_log.write(f"[mesh-{buf}] {path} → {out_bin} ({len(data)} bytes)\n")

        if slot["have_vb"] and slot["have_ib"]:
            (dest / "info.json").write_text(json.dumps(slot["meta"], indent=2) + "\n")
            completed.add(path)
            f_log.write(f"[mesh-done] {path}\n")
            if expect_count > 0 and len(completed) >= expect_count:
                f_log.write(f"[done] expected count reached ({expect_count})\n")
                done.set()

    def on_message(message: dict[str, Any], data: bytes | None):
        nonlocal last_engine_frame
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
            f_log.write(f"[ready] base={p.get('base')} module={p.get('module')}\n")
            return
        if kind == "d3d_device_ready":
            f_log.write(f"[d3d-ready] device={p.get('device')}\n")
            device_ready.set()
            return
        if kind == "max_frames_reached":
            f_log.write(f"[max_frames] engine frame={p.get('frame')}\n")
            done.set()
            return
        if kind == "input_state":
            # Track engine progression so we know when to give up.
            f = int(p.get("frame", -1))
            if f > last_engine_frame: last_engine_frame = f
            return
        if kind == "mesh_dump":
            write_part(p["path"], p, data)
            return

        # Other kinds are harmless during a dump run (audio events, etc).

    # ── connect to remote frida-server + spawn retail ──
    dm = frida.get_device_manager()
    try:
        device = dm.add_remote_device(remote)
    except frida.InvalidArgumentError:
        device = dm.get_device(remote)

    # Preflight: surface a missing frida-server clearly.
    try:
        _ = device.enumerate_processes()
    except frida.ServerNotRunningError as e:
        msg = (f"\nfrida-server not reachable at {remote}.\n"
               f"On the Windows host: run frida-server.exe (default 127.0.0.1:27042),\n"
               f"OR pass --frida-remote <host>:<port> if you're on WSL2.\n"
               f"Underlying error: {e}\n")
        f_log.write(msg)
        raise SystemExit(msg) from e

    win_exe = wslpath_w(RETAIL_EXE)
    win_cwd = wslpath_w(ASSET_CWD)
    f_log.write(f"[spawn] {win_exe} (cwd {win_cwd})\n")
    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script  = session.create_script(AGENT_JS.read_text())
    script.on("message", on_message)
    script.load()

    # Load the title-z-press trace so retail dispatches into INGAME and
    # the scene-1 .x loads fire.
    trace_entries = []
    if TRACE.exists():
        for raw in TRACE.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            rec = json.loads(line)
            if "buttons" not in rec:
                continue   # skip non-input ops (e.g. trace-global {savefile})
            mask_val = rec["buttons"]
            mask = int(mask_val, 16) if isinstance(mask_val, str) else int(mask_val)
            trace_entries.append({"frame": int(rec["frame"]), "mask": mask})

    # We don't drive the engine to organic load — instead we invoke
    # FUN_00472836 directly from a Frida RPC for each requested path
    # once the D3D device is up. This sidesteps the cutscene/intro
    # state-machine that would otherwise sit between INGAME entry and
    # the shop scene's asset loads. The input trace is still useful to
    # un-park the title state in case the loader prereqs include
    # something that only runs post-title (none observed so far, but
    # cheap insurance).
    init_cfg: dict[str, Any] = {
        "max_frames":      max_frames,
        "input_trace":     trace_entries,
        "force_input":     bool(trace_entries),
        "hide_window":     True,
        "turbo":           True,
        "turbo_step_ms":   17,
        "silent_audio":    True,
        "dump_meshes":     True if (dump_all or paths) else [],
    }
    f_log.write(f"[init] {json.dumps({k: v for k, v in init_cfg.items() if k != 'input_trace'})}\n")
    f_log.write(f"[init] input_trace entries={len(trace_entries)}\n")

    script.exports_sync.init(init_cfg)

    t0 = time.monotonic()
    device.resume(pid)

    # Wait for the D3D device to come up before invoking the loader.
    # In practice this lands within ~500ms of resume under --turbo.
    if not device_ready.wait(timeout=15.0):
        f_log.write("[timeout] d3d_device_ready never fired within 15s\n")
    else:
        # Direct invoke loop. For each requested path, drive the engine's
        # FUN_00472836 via RPC. The hook then fires onLeave, captures the
        # ID3DXMesh, emits two `mesh_dump` messages (vb + ib), and the
        # `on_message` handler above writes the files.
        for path in paths:
            f_log.write(f"[invoke] {path}\n")
            try:
                ok = script.exports_sync.invoke_mesh_loader(path)
                f_log.write(f"[invoke] {path} → {ok}\n")
            except Exception as e:
                f_log.write(f"[invoke-error] {path}: {e}\n")
            # Small breather so the agent's send() events flush before
            # we either invoke the next path or fall through to wait.
            time.sleep(0.1)

    deadline = t0 + (duration_ms / 1000.0)
    # If we requested specific paths, completion = all of them done.
    # Otherwise we stop on the engine's own asset loads firing (in
    # --all mode without explicit paths).
    target_paths = set(paths) if paths else set()
    while not done.is_set() and time.monotonic() < deadline:
        if target_paths and target_paths <= completed:
            f_log.write(f"[done] all {len(target_paths)} requested paths captured\n")
            break
        time.sleep(0.05)

    elapsed_ms = int((time.monotonic() - t0) * 1000)
    f_log.write(f"[shutdown] elapsed_ms={elapsed_ms} completed={sorted(completed)}\n")

    try:
        script.unload()
    except Exception as e:
        f_log.write(f"[shutdown] script.unload: {e}\n")
    try:
        device.kill(pid)
    except Exception as e:
        f_log.write(f"[shutdown] device.kill: {e}\n")

    f_log.close()
    return DumpResult(paths=sorted(completed), elapsed_ms=elapsed_ms, exit_code=0)


def main():
    ap = argparse.ArgumentParser(
        description="Dump retail .x mesh VB+IB via Frida for parser bit-diff.")
    ap.add_argument("paths", nargs="*",
                    help="engine asset paths to load (e.g. xfile/shop/shop_1st.x). "
                         "Each path is dispatched via FUN_00472836 directly.")
    ap.add_argument("--all", action="store_true",
                    help="Dump every organic .x load as well (default: only listed paths).")
    ap.add_argument("--expect", type=int, default=0,
                    help="Stop after N distinct paths captured (default: run to max-frames).")
    ap.add_argument("--max-frames", type=int, default=DEFAULT_MAX_FRAMES,
                    help=f"Engine-side frame budget (default {DEFAULT_MAX_FRAMES}).")
    ap.add_argument("--duration-ms", type=int, default=DEFAULT_DURATION_MS,
                    help=f"Wall-clock ceiling in ms (default {DEFAULT_DURATION_MS}).")
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE,
                    help=f"Remote frida-server (default {DEFAULT_REMOTE}).")
    ap.add_argument("--output-dir", default=str(DUMP_ROOT),
                    help=f"Output root (default {DUMP_ROOT}).")
    args = ap.parse_args()

    if not args.all and not args.paths:
        ap.error("either --all or at least one explicit asset path is required")

    result = run(paths=args.paths, dump_all=args.all,
                 expect_count=args.expect,
                 max_frames=args.max_frames, duration_ms=args.duration_ms,
                 remote=args.frida_remote,
                 output_dir=Path(args.output_dir))

    print(f"dumped {len(result.paths)} mesh(es) in {result.elapsed_ms}ms")
    for p in result.paths:
        print(f"  {p}")
    if not result.paths:
        print("no meshes captured — see runs/retail-meshes/agent.log", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
