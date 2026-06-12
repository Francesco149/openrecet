#!/usr/bin/env python3
"""Trace Studio v3 — RETAIL full-extent capture + replay verify (present-window).

The retail counterpart of port_capture.py. Retail does NOT read back the backbuffer
per frame (no in-engine screenshot), so the port's GetBackBuffer keep-trigger can't
drive it. Instead the window is addressed by PRESENT-COUNT: the proxy keeps every
present in [START, START+COUNT) (v3proxy.cfg `capframe`/`capcount`), the same
present-window keep mode the port's --window option exercises locally. This is the
"retail captured once, sliced forever" storage model on the real retail side.

Mechanism (Frida orchestration only — the proxy does ALL d3d capture in-process):
  1. stage tools/trace_studio_v3/proxy/d3d8.dll next to the unpacked retail exe;
  2. write v3proxy.cfg (capframe/capcount) next to the dll — env vars don't cross to
     the Frida-spawned exe, so config travels via the file;
  3. spawn vendor/unpacked/recettear.unpacked.exe via Frida (remote frida-server,
     turbo clock, hidden window, silent audio, force_resolution to match the port);
  4. let retail turbo-run past the window; the proxy writes %LOCALAPPDATA%\\openrecet\\
     v3\\{v3cap.bin, v3ref_NNN.raw} + finalizes (EOF) after the last window frame;
  5. replay.exe renders each kept frame index + byte-compares to its reference.

A FIXED present-count window (--window START:COUNT) is reliable for the TITLE (a
deterministic early frame). A post-load gameplay window needs anchor-relative
arming (the load-stretch is nondeterministic) — that lands in a follow-up via the
proxy's OrV3ArmWindowAt export + the agent's anchor resolver.

Usage (host tools need the nix prefix):
  nix develop --command python3 tools/trace_studio_v3/retail_capture.py \
      --window 120:48 [--seconds 12] [--max-frames 8000] [--no-verify]
"""
import argparse
import json
import subprocess
import threading
import time
from pathlib import Path

import frida

ROOT       = Path(__file__).resolve().parent.parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
PROXY_SRC  = ROOT / "tools" / "trace_studio_v3" / "proxy" / "d3d8.dll"
PROXY_DLL  = ROOT / "vendor" / "unpacked" / "d3d8.dll"
REPLAY_EXE = ROOT / "tools" / "trace_studio_v3" / "replay" / "replay.exe"
ASSET_CWD  = ROOT / "vendor" / "original"
DEFAULT_REMOTE = "cutestation.soy:27042"

_SCREEN_SIZES = {0: (640, 480), 1: (800, 600), 2: (1024, 768), 3: (1280, 960)}


def wslpath_w(p: Path) -> str:
    return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True,
                          check=True).stdout.strip()


def localappdata_v3() -> Path:
    """%LOCALAPPDATA%\\openrecet\\v3 as a WSL path (where the proxy writes)."""
    out = subprocess.run(["cmd.exe", "/c", "echo %LOCALAPPDATA%"],
                         capture_output=True, text=True, cwd="/mnt/c").stdout.strip()
    wsl = subprocess.run(["wslpath", "-u", out], capture_output=True, text=True,
                         check=True).stdout.strip()
    return Path(wsl) / "openrecet" / "v3"


def openrecet_screen_dims() -> tuple[int, int]:
    """Resolution the port renders at, from vendor/original/recet.ini `screen=`
    (default 1024×768). Retail's own UNC recet.ini read fails ⇒ would default to
    640×480 ⇒ we pin retail to these dims via the agent's force_resolution hook."""
    ini = ASSET_CWD / "recet.ini"
    try:
        for raw in ini.read_text().splitlines():
            line = raw.strip()
            if line.startswith("screen") and "=" in line:
                return _SCREEN_SIZES.get(int(line.split("=", 1)[1].strip()), (1024, 768))
    except (OSError, ValueError):
        pass
    return (1024, 768)


def replay_verify(v3: Path, n: int) -> int:
    """Replay every kept frame index and assert bit-exact vs its reference."""
    if not REPLAY_EXE.exists():
        raise SystemExit(f"replayer not built: {REPLAY_EXE} — `make` in replay/")
    cap_w = wslpath_w(v3 / "v3cap.bin")
    chk_w = wslpath_w(v3 / "v3replay_chk.raw")
    npass = nfail = 0
    first_fail = None
    print(f"[verify] replaying all {n} kept frames …")
    for i in range(n):
        ref = v3 / f"v3ref_{i:03d}.raw"
        r = subprocess.run([str(REPLAY_EXE), cap_w, wslpath_w(ref), str(i), chk_w],
                           capture_output=True, text=True)
        db = None
        for ln in (r.stdout + r.stderr).splitlines():
            if "differing bytes" in ln:
                db = ln.split(":", 1)[1].split("(")[0].strip()
        if db == "0":
            npass += 1
        else:
            nfail += 1
            if first_fail is None:
                first_fail = f"frame {i}: differing bytes={db!r} (exit {r.returncode})"
    print("=" * 48)
    print(f"  BIT-EXACT: {npass} / {n}   |   FAILED: {nfail}")
    if first_fail:
        print(f"  first failure: {first_fail}")
    print(f"  VERDICT: {'ALL FRAMES BIT-EXACT *** GO ***' if nfail == 0 else 'DIVERGENT'}")
    print("=" * 48)
    return 0 if nfail == 0 else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="v3 retail present-window capture + replay verify.")
    ap.add_argument("--window", metavar="START:COUNT", required=True,
                    help="present-count window [START, START+COUNT) to keep "
                         "(e.g. 120:48 = a 48-frame title window).")
    ap.add_argument("--seconds", type=float, default=12.0,
                    help="wall-clock to let retail turbo-run (must reach the window END).")
    ap.add_argument("--max-frames", type=int, default=8000,
                    help="engine-side frame budget handed to the agent (> window end).")
    ap.add_argument("--no-verify", action="store_true",
                    help="capture only; skip the per-frame bit-exact replay check.")
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE)
    args = ap.parse_args()

    try:
        s, c = args.window.split(":")
        win_start, win_count = int(s), int(c)
    except ValueError:
        raise SystemExit(f"--window wants START:COUNT (got {args.window!r})")

    if not PROXY_SRC.exists():
        raise SystemExit(f"proxy not built: {PROXY_SRC} — `make` in proxy/")

    # stage proxy + write the present-window cfg next to it (config travels via the
    # file: env vars don't cross to a Frida-spawned exe).
    import shutil
    shutil.copy2(PROXY_SRC, PROXY_DLL)
    cfg_path = PROXY_DLL.parent / "v3proxy.cfg"
    cfg_path.write_text(f"capframe={win_start}\ncapcount={win_count}\n")
    print(f"[stage] {PROXY_DLL.name} staged + v3proxy.cfg → WINDOW [{win_start},{win_start+win_count})")

    v3 = localappdata_v3()
    v3.mkdir(parents=True, exist_ok=True)
    cap = v3 / "v3cap.bin"
    log = v3 / "v3proxy.log"
    for f in [cap, log, *v3.glob("v3ref_*.raw"), v3 / "v3replay_chk.raw"]:
        f.unlink(missing_ok=True)

    detached = threading.Event()
    log_lines: list[str] = []

    def on_message(message, data):
        if message.get("type") == "error":
            log_lines.append(f"[frida-error] {message.get('description','')}")

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
    print(f"[spawn] {win_exe}  cwd {win_cwd}")
    pid = dev.spawn([win_exe], cwd=win_cwd)
    print(f"[spawn] pid={pid}")
    session = dev.attach(pid)
    session.on("detached", lambda reason, crash: (
        log_lines.append(f"[detached] reason={reason!r} crash={crash!r}"), detached.set()))

    script = session.create_script(AGENT_JS.read_text())
    script.on("message", on_message)
    script.load()

    res_w, res_h = openrecet_screen_dims()
    init_cfg = {
        "max_frames":       args.max_frames,
        "input_trace":      [],
        "force_input":      False,
        "hide_window":      True,
        "turbo":            True,
        "turbo_step_ms":    17,
        "silent_audio":     True,
        "force_resolution": [res_w, res_h],
    }
    print(f"[init]  force_resolution={res_w}x{res_h}; "
          f"{json.dumps({k: v for k, v in init_cfg.items() if k != 'force_resolution'})}")
    script.exports_sync.init(init_cfg)

    dev.resume(pid)
    t0 = time.monotonic()
    deadline = t0 + args.seconds
    # finalize == the proxy wrote EOF after the last window frame; poll the log so we
    # can stop early once the window is fully captured (no need to burn the full budget).
    while time.monotonic() < deadline and not detached.is_set():
        time.sleep(0.2)
        if log.exists() and "FINALIZE" in log.read_text(errors="replace"):
            print(f"[run]   window finalized after {time.monotonic()-t0:.2f}s")
            break
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

    if not cap.exists() or not log.exists():
        raise SystemExit(f"[fail] no capture produced at {v3} — check the spawn above")
    keeps = [ln for ln in log.read_text(errors="replace").splitlines() if ln.startswith("KEEP")]
    n = len(keeps)
    cap_mb = cap.stat().st_size / 1048576
    refs = sorted(v3.glob("v3ref_*.raw"))
    print(f"\n[cap]   {n} frame(s) kept · container {cap_mb:.1f} MB · {len(refs)} references")
    if refs and n:
        ref_mb = refs[0].stat().st_size / 1048576
        print(f"[cap]   dedup: {n} frames in {cap_mb:.1f} MB; {n}× raw pixels alone "
              f"would be {n*ref_mb:.0f} MB (resources stored once, frames ≈ free)")
    if n == 0:
        raise SystemExit("[fail] proxy loaded but kept 0 frames — window past the run end? "
                         "raise --seconds / lower --window START.")
    if args.no_verify:
        print("[skip] --no-verify: not replaying")
        return 0
    return replay_verify(v3, n)


if __name__ == "__main__":
    raise SystemExit(main())
