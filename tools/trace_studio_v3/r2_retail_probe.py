#!/usr/bin/env python3
"""Trace Studio v3 — R2: prove the proxy d3d8.dll loads into RETAIL.

The v3 thesis is "ONE capture implementation for both sides" — the same proxy
d3d8.dll wraps the port AND the retail (SteamStub-unpacked) exe. P0/P1 proved it
for the port; R2 is the remaining de-risk: does the Windows loader pick up the
app-local d3d8.dll for the unpacked exe (does SteamStub/the unpack interfere?),
and can it capture+replay a retail frame.

This driver spawns vendor/unpacked/recettear.unpacked.exe via Frida (the same
path frida_capture/dump-retail-meshes use — remote frida-server, turbo clock,
hidden window, silent audio) with the proxy d3d8.dll already staged next to the
exe. The proxy writes its container + log to the Windows host's %LOCALAPPDATA%\\
openrecet\\v3 (WSL-readable via /mnt/c). We do NOT instrument d3d from the agent
(no vtable hooks) — the proxy does all capture in-process; Frida only spawns,
turbo-clocks and tears down.

R2a (loadability): run with no --capframe → the proxy logs "DllMain attach" +
"Direct3DCreate8 wrapped" + "capture begin"; that alone answers R2.
R2b (capture):    pass --capframe N → the proxy finalizes present-frame N (cap +
reference backbuffer) for replay.exe to re-render and bit-compare.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/r2_retail_probe.py \
      [--capframe N] [--seconds 6] [--max-frames 4000]
"""
import argparse
import json
import threading
import time
from pathlib import Path

import frida

ROOT       = Path(__file__).resolve().parent.parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
PROXY_DLL  = ROOT / "vendor" / "unpacked" / "d3d8.dll"
ASSET_CWD  = ROOT / "vendor" / "original"
DEFAULT_REMOTE = "cutestation.soy:27042"


def wslpath_w(p: Path) -> str:
    import subprocess
    r = subprocess.run(["wslpath", "-w", str(p)],
                       capture_output=True, text=True, check=True)
    return r.stdout.strip()


def main() -> int:
    ap = argparse.ArgumentParser(description="R2: proxy-d3d8 loadability into retail.")
    ap.add_argument("--capframe", type=int, default=None,
                    help="present-count target to finalize (R2b). Omit for a pure "
                         "loadability probe (R2a).")
    ap.add_argument("--out-win", default=None,
                    help="Windows dir for the proxy container/log/reference (cfg out=). "
                         "Default: %%LOCALAPPDATA%%\\openrecet\\v3.")
    ap.add_argument("--seconds", type=float, default=6.0,
                    help="wall-clock to let retail run under turbo before teardown.")
    ap.add_argument("--max-frames", type=int, default=8000,
                    help="engine-side frame budget handed to the agent.")
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE)
    args = ap.parse_args()

    if not PROXY_DLL.exists():
        raise SystemExit(f"proxy not staged: {PROXY_DLL} (build + copy it first)")

    # The proxy reads capframe/out from v3proxy.cfg next to the dll (env vars
    # don't cross to the Frida-spawned Windows exe). Write it fresh each run so a
    # stale target never leaks in; remove it for a pure-loadability probe.
    cfg_path = PROXY_DLL.parent / "v3proxy.cfg"
    if args.capframe is not None:
        lines = [f"capframe={args.capframe}"]
        if args.out_win:
            lines.append(f"out={args.out_win}")
        cfg_path.write_text("\n".join(lines) + "\n")
        print(f"[cfg]   wrote {cfg_path.name}: {'; '.join(lines)}")
    elif cfg_path.exists():
        cfg_path.unlink()
        print(f"[cfg]   removed stale {cfg_path.name} (loadability-only run)")

    device_ready = threading.Event()
    detached = threading.Event()
    log_lines: list[str] = []

    def on_message(message, data):
        if message.get("type") == "error":
            log_lines.append(f"[frida-error] {message.get('description','')}")
            return
        if message.get("type") != "send":
            return
        p = message.get("payload") or {}
        kind = p.get("kind")
        if kind == "d3d_device_ready":
            log_lines.append(f"[agent] d3d_device_ready device={p.get('device')}")
            device_ready.set()

    dm = frida.get_device_manager()
    try:
        dev = dm.add_remote_device(args.frida_remote)
    except frida.InvalidArgumentError:
        dev = dm.get_device(args.frida_remote)

    try:
        dev.enumerate_processes()
    except frida.ServerNotRunningError as e:
        raise SystemExit(f"frida-server not reachable at {args.frida_remote}: {e}")

    win_exe = wslpath_w(RETAIL_EXE)
    win_cwd = wslpath_w(ASSET_CWD)
    print(f"[spawn] {win_exe}\n        cwd {win_cwd}\n        proxy {PROXY_DLL.name} staged "
          f"({PROXY_DLL.stat().st_size} bytes)")

    pid = dev.spawn([win_exe], cwd=win_cwd)
    print(f"[spawn] pid={pid}")
    session = dev.attach(pid)
    session.on("detached", lambda reason, crash: (
        log_lines.append(f"[detached] reason={reason!r} crash={crash!r}"), detached.set()))

    script = session.create_script(AGENT_JS.read_text())
    script.on("message", on_message)
    script.load()

    init_cfg = {
        "max_frames":    args.max_frames,
        "input_trace":   [],
        "force_input":   False,
        "hide_window":   True,
        "turbo":         True,
        "turbo_step_ms": 17,
        "silent_audio":  True,
    }
    print(f"[init]  {json.dumps(init_cfg)}")
    script.exports_sync.init(init_cfg)

    dev.resume(pid)
    t0 = time.monotonic()
    if device_ready.wait(timeout=15.0):
        print(f"[ready] d3d device up after {time.monotonic()-t0:.2f}s")
    else:
        print("[warn]  d3d_device_ready never fired within 15s "
              "(checking proxy log anyway — the proxy logs independently)")

    # Let retail turbo-run so many Presents happen (loadability needs only a
    # device + a present; a capframe target needs to be reached).
    deadline = t0 + args.seconds
    while time.monotonic() < deadline and not detached.is_set():
        time.sleep(0.1)
    print(f"[run]   ran {time.monotonic()-t0:.2f}s, detached={detached.is_set()}")

    try:
        script.unload()
    except Exception as e:
        print(f"[teardown] script.unload: {e}")
    try:
        dev.kill(pid)
    except Exception as e:
        print(f"[teardown] device.kill: {e}")

    for ln in log_lines:
        print(ln)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
