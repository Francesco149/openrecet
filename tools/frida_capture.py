#!/usr/bin/env python3
"""
tools/frida_capture.py — Phase B retail-capture driver.

Connects to a Windows-side frida-server (default cutestation.soy:27042),
spawns
`vendor/unpacked/recettear.unpacked.exe` with the openrecet-agent.js hooks
loaded, and lays artifacts down in a Phase A-compatible run directory:

    <run_dir>/
        frames/frame_NNNNN.bmp        (per scenario.capture_frames)
        audio.jsonl                   (bgm_swap / se_play events)
        trace.jsonl                   (input_state events, sparse)
        run.json                      (metadata)
        agent.log                     (Frida send(log) + errors)

The BMP layout matches src/main.c::capture_backbuffer (32-bit top-down
BGRA, BITMAPFILEHEADER + BITMAPINFOHEADER, no palette). That lets the
existing scenario-test diff path eat retail BMPs without modification.

Module-level entry point — `run_capture(scenario, run_dir, ...)` — is
what tools/scenario-test.py calls when `--target retail` is in effect.
The CLI at the bottom is for ad-hoc / debugging usage.

frida-server setup (Windows side, one-time):
    1. Download frida-server-<ver>-windows-x86_64.exe from the
       Frida releases page that matches the Python frida version
       in nix (currently 17.5.1).
    2. Rename → frida-server.exe, run it as Administrator. Listens on
       127.0.0.1:27042 by default.
    3. Optional: install as a service for unattended runs.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import socket
import struct
import subprocess
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

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")

# Where to look for the Windows-side frida-server.exe when auto-starting.
# Override via $OPENRECET_FRIDA_SERVER_EXE (WSL path to the exe).
DEFAULT_FRIDA_SERVER_EXE = Path(os.environ.get(
    "OPENRECET_FRIDA_SERVER_EXE",
    f"/mnt/c/Users/headpats/Documents/_devtools/"
    f"frida-server-{frida.__version__}-windows-x86_64/"
    f"frida-server-{frida.__version__}-windows-x86_64.exe"))


# ─── helpers ──────────────────────────────────────────────────────────────


def wslpath_w(p: Path) -> str:
    """Translate a Linux path to its Windows form (frida-server is on Windows)."""
    r = subprocess.run(
        ["wslpath", "-w", str(p)],
        capture_output=True, text=True, check=True,
    )
    return r.stdout.strip()


def _tcp_open(host: str, port: int, timeout: float = 1.0) -> bool:
    """True iff a TCP connect to host:port succeeds within `timeout`."""
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def ensure_frida_server(remote: str, exe_wsl_path: Path,
                       startup_timeout_s: float = 15.0) -> bool:
    """Ensure frida-server.exe is reachable at `remote`. If not, spawn it via
    `Start-Process -Verb runAs` on the Windows side (UAC will prompt) and
    poll until the port answers. Returns True if reachable at exit.

    Idempotent: if already up, returns True immediately. Listens on
    0.0.0.0:27042 so WSL can reach it across the NAT boundary.
    """
    host, _, port_s = remote.partition(":")
    port = int(port_s or "27042")
    if _tcp_open(host, port, timeout=1.0):
        return True

    if not exe_wsl_path.exists():
        print(f"[ensure_frida_server] {exe_wsl_path} not found; cannot auto-start. "
              f"Override with $OPENRECET_FRIDA_SERVER_EXE or start manually.",
              file=sys.stderr)
        return False

    win_exe = subprocess.run(
        ["wslpath", "-w", str(exe_wsl_path)],
        capture_output=True, text=True, check=True).stdout.strip()
    print(f"[ensure_frida_server] launching elevated: {win_exe} "
          f"(approve the UAC prompt)", file=sys.stderr)

    # Start-Process -Verb runAs: triggers UAC. -WindowStyle Normal keeps
    # the existing cmd-window UX the user is already familiar with.
    ps_cmd = (
        f"Start-Process -Verb runAs -WindowStyle Normal "
        f"-FilePath '{win_exe}' "
        f"-ArgumentList '-l','0.0.0.0:{port}'")
    subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command", ps_cmd],
        check=False)

    deadline = time.monotonic() + startup_timeout_s
    while time.monotonic() < deadline:
        if _tcp_open(host, port, timeout=0.5):
            print(f"[ensure_frida_server] up on {remote}", file=sys.stderr)
            return True
        time.sleep(0.5)
    print(f"[ensure_frida_server] timed out waiting for {remote}",
          file=sys.stderr)
    return False


def write_bmp_topdown_bgra(path: Path, w: int, h: int, pixels: bytes) -> None:
    """Mirror src/main.c::capture_backbuffer's on-disk layout exactly.

    32-bit top-down BMP: BITMAPFILEHEADER (14) + BITMAPINFOHEADER (40) +
    `pixels` (BGRA, w*4 bytes per row, no padding, h rows). Negative
    biHeight ⇒ top-down. Output is bit-identical to what the openrecet
    exe writes when fed the same back-buffer.
    """
    row_bytes = w * 4
    img_size  = row_bytes * h
    file_size = 14 + 40 + img_size

    fhdr = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 54)
    ihdr = struct.pack(
        "<IiiHHIIiiII",
        40,              # biSize
        w,               # biWidth
        -h,              # biHeight (negative = top-down)
        1,               # biPlanes
        32,              # biBitCount
        0,               # biCompression = BI_RGB
        img_size,        # biSizeImage
        0,               # biXPelsPerMeter
        0,               # biYPelsPerMeter
        0,               # biClrUsed
        0,               # biClrImportant
    )
    with path.open("wb") as f:
        f.write(fhdr); f.write(ihdr); f.write(pixels)


# ─── capture session ──────────────────────────────────────────────────────


@dataclass
class CaptureConfig:
    capture_frames: list[int] = field(default_factory=list)
    max_frames:     int = 60
    duration_ms:    int = 30_000   # wall-clock ceiling
    remote:         str = DEFAULT_REMOTE
    exe:            Path = RETAIL_EXE
    cwd:            Path = ASSET_CWD
    auto_start_server:    bool = True
    server_exe:           Path = DEFAULT_FRIDA_SERVER_EXE
    # Input injection. When `force_input` is true the agent overwrites
    # DAT_073dddd0 (var_input_mask) on every input_poll LEAVE with the
    # sticky-trace mask for the current engine frame. The trace path
    # points to a Phase A-format sparse JSONL ({frame, buttons:"0xNNNN"});
    # an empty / missing file with force_input=True effectively pins
    # input at 0 every frame.
    input_trace_path: Path | None = None
    force_input:      bool = False
    # Window hide. When true the agent rewrites the engine's first
    # ShowWindow call to SW_HIDE and writes 1 to DAT_073dfca0 so the
    # engine's main loop doesn't sit in WaitMessage forever (the flag
    # normally flips via WM_ACTIVATE, which a never-shown window never
    # receives). D3D rendering and the back-buffer capture path are
    # unaffected. Default off here so ad-hoc `frida_capture.py` runs
    # behave like before; scenario-test.py opts in.
    hide_window:      bool = False
    # Turbo. Replaces FUN_0047be2f with a virtual clock that advances by
    # `turbo_step_ms` (default 17) per FUN_0047be92 entry, so the
    # dispatcher takes the tick branch every iteration with no Sleep.
    # Game timing stays consistent (everything runs at the engine's 60
    # FPS budget per loop pass), the wall clock just spins as fast as
    # the host can. Pair with silent_audio — DirectMusic doesn't enjoy
    # being clocked at 200+ fps.
    turbo:            bool = False
    turbo_step_ms:    int  = 17
    # Silent audio. Hooks IDirectMusicAudioPath::SetVolume (vtable[5])
    # on the BGM path (shared vtable across all 3 paths) to clamp
    # lVolume to -10000 every call. Game's audio code still fires
    # normally — PlaySegmentEx, fade animations, segment-state queueing
    # all happen — only the master attenuation is pinned to silence.
    silent_audio:     bool = False
    # Force back-buffer resolution. When set to (w, h), the agent
    # hooks the engine's recet.ini parse exit and overwrites the two
    # screen-size globals (DAT_005cbc04/08), so retail captures at the
    # requested dimensions even when its vendor/unpacked/recet.ini is
    # empty / has a stale `screen=` value. Default None = honor
    # whatever the engine's recet.ini lookup picks. scenario-test.py's
    # retail path defaults this to openrecet's resolution so the
    # side-by-sides line up by construction.
    force_resolution: tuple[int, int] | None = None
    # D3D state-trace emitter (Phase D.4). When `d3d_trace` is true,
    # the agent hooks IDirect3DDevice8 vtable slots and buffers one
    # event per state-change or draw call; the Present hook flushes
    # the buffer as a batched message that the driver writes to
    # `<run_dir>/d3d_trace.jsonl`. `d3d_trace_frames` is an optional
    # filter — when set, only the listed frames have their events
    # captured (INGAME frames can run 1000+ calls each, so a full
    # unfiltered trace generates megabytes per second).
    d3d_trace:        bool = False
    d3d_trace_frames: list[int] | None = None
    # Call tracer (Phase E.1). When `call_trace` is true the agent
    # Interceptor.attach()es onEnter on every VA in `call_trace_vas`
    # and emits one record per invocation to `<run_dir>/call_trace.jsonl`.
    # `call_trace_vas` defaults to the engine function-entry list at
    # tools/ttd/data/engine_function_vas.json (2103 entries from
    # objdump). `call_trace_frames` is a per-frame whitelist — strongly
    # recommended, since unfiltered runs can emit tens of thousands of
    # events per frame and saturate the Frida wire.
    call_trace:        bool = False
    call_trace_vas:    list[int] | None = None
    call_trace_frames: list[int] | None = None
    # Auto-Z spam + auto-3D-trace.  When `auto_z_spam` is true the
    # agent's input_poll onLeave forces button-A every other 2-frame
    # block (~15 presses/sec) — fast enough to clear the intro
    # cutscene unattended.  When `auto_3d_trace` is true the agent
    # hooks DrawIndexedPrimitive; on the first hit it records the
    # frame number and arms call_trace emit ONLY for the window
    # [3D_seen, 3D_seen + auto_3d_trace_frames], then sends
    # `auto_3d_trace_done` which causes the driver to shut down.
    auto_z_spam:            bool = False
    auto_3d_trace:          bool = False
    auto_3d_trace_frames:   int  = 60


@dataclass
class CaptureResult:
    exit_code:        int = 0
    elapsed_ms:       int = 0
    captured_frames:  list[int] = field(default_factory=list)
    last_engine_frame: int = -1


def _run_capture_impl(cfg: CaptureConfig, run_dir: Path) -> CaptureResult:
    frames_dir   = run_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    audio_jsonl  = run_dir / "audio.jsonl"
    trace_jsonl  = run_dir / "trace.jsonl"
    agent_log    = run_dir / "agent.log"
    d3d_jsonl    = run_dir / "d3d_trace.jsonl"

    # File handles. trace.jsonl is sparse — we only emit when the mask
    # changes — so we buffer last value across input_state events.
    f_audio = audio_jsonl.open("w", buffering=1)
    f_trace = trace_jsonl.open("w", buffering=1)
    f_log   = agent_log.open("w",   buffering=1)
    # d3d_trace.jsonl: one line per state-change / draw call. Default
    # buffering (not line-buffered) — bursty render frames would dominate
    # the wall clock if we fsync after every event.
    f_d3d = d3d_jsonl.open("w") if cfg.d3d_trace else None
    call_trace_jsonl = run_dir / "call_trace.jsonl"
    f_call = call_trace_jsonl.open("w") if cfg.call_trace else None

    captured: list[int] = []
    last_mask: int | None = None
    last_engine_frame = -1
    done = threading.Event()

    def on_message(message: dict[str, Any], data: bytes | None):
        nonlocal last_mask, last_engine_frame

        if message.get("type") == "error":
            f_log.write(f"[frida-error] {message.get('description','')} "
                        f"@ {message.get('fileName','')}:{message.get('lineNumber','')}\n")
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
            f_log.write(f"[ready] base={p.get('base')} module={p.get('module')} "
                        f"pending={p.get('capture_pending')} "
                        f"max_frames={p.get('max_frames')}\n")
            return

        if kind == "present_hook_ready":
            f_log.write(f"[present-hook] live @ frame={p.get('frame')}\n")
            return

        if kind == "frame":
            frame = int(p["frame"])
            w     = int(p["w"])
            h     = int(p["h"])
            if data is None or len(data) != w * h * 4:
                f_log.write(f"[frame] frame={frame} BAD payload "
                            f"(w={w} h={h} got={len(data) if data else 0} expect={w*h*4})\n")
                return
            bmp_path = frames_dir / f"frame_{frame:05d}.bmp"
            write_bmp_topdown_bgra(bmp_path, w, h, data)
            captured.append(frame)
            last_engine_frame = max(last_engine_frame, frame)
            f_log.write(f"[frame] {bmp_path.name} {w}x{h}\n")
            return

        if kind == "bgm_swap":
            f_audio.write(json.dumps({
                "t_ms":  int(p["t_ms"]),
                "kind":  "bgm_swap",
                "frame": int(p["frame"]),
                "track": int(p["track"]),
            }) + "\n")
            return

        if kind == "se_play":
            f_audio.write(json.dumps({
                "t_ms":  int(p["t_ms"]),
                "kind":  "se_play",
                "frame": int(p["frame"]),
                "slot":  int(p["slot"]),
            }) + "\n")
            return

        if kind == "input_state":
            frame = int(p["frame"])
            mask  = int(p["buttons"])
            last_engine_frame = max(last_engine_frame, frame)
            # Sparse: only emit when the mask changes.
            if mask != last_mask:
                f_trace.write(json.dumps({
                    "frame":   frame,
                    "buttons": f"0x{mask:04x}",
                }) + "\n")
                last_mask = mask
            return

        if kind == "max_frames_reached":
            f_log.write(f"[max_frames] engine frame={p.get('frame')}\n")
            done.set()
            return

        if kind == "d3d_trace_batch":
            if f_d3d is None:
                return
            frame  = int(p["frame"])
            events = p.get("events") or []
            # One JSONL row per event for tractable diffing; the batch
            # boundary is recorded as `frame` on each row.
            for ev in events:
                ev_out = dict(ev)
                ev_out["frame"] = frame
                f_d3d.write(json.dumps(ev_out) + "\n")
            f_log.write(f"[d3d_trace] frame={frame} events={len(events)}\n")
            return

        if kind == "call_trace_hooked":
            f_log.write(f"[call_trace] hooked ok={p.get('n_ok')} "
                        f"fail={p.get('n_fail')} req={p.get('n_req')}\n")
            return

        if kind == "call_trace_batch":
            if f_call is None:
                return
            frame  = int(p["frame"])
            events = p.get("events") or []
            for ev in events:
                ev_out = dict(ev)
                ev_out["frame"] = frame
                f_call.write(json.dumps(ev_out) + "\n")
            f_log.write(f"[call_trace] frame={frame} events={len(events)}\n")
            return

        if kind == "auto_3d_scene_reached":
            f_log.write(f"[auto_3d] scene reached @ frame={p.get('frame')}\n")
            return

        if kind == "auto_3d_trace_done":
            f_log.write(f"[auto_3d] trace window done "
                        f"[frames {p.get('first_frame')}..{p.get('last_frame')}]; "
                        f"signaling shutdown\n")
            done.set()
            return

        f_log.write(f"[unhandled] {p}\n")

    # ── auto-start frida-server if not already up ──
    if cfg.auto_start_server:
        ensure_frida_server(cfg.remote, cfg.server_exe)

    # ── connect to remote frida-server ──
    dm = frida.get_device_manager()
    try:
        device = dm.add_remote_device(cfg.remote)
    except frida.InvalidArgumentError:
        # Already added — get the existing one.
        device = dm.get_device(cfg.remote)

    # Preflight: a missing frida-server on the Windows side is the most
    # likely failure mode for the first run; surface it with the setup
    # hint inline so the user doesn't have to grep the docstring.
    try:
        _ = device.enumerate_processes()
    except frida.ServerNotRunningError as e:
        msg = (f"\nfrida-server not reachable at {cfg.remote}.\n"
               f"On the Windows host:\n"
               f"  1. Download frida-server-{frida.__version__}-windows-x86_64.exe\n"
               f"     from https://github.com/frida/frida/releases\n"
               f"  2. Rename → frida-server.exe and run as Administrator.\n"
               f"     (default listen 127.0.0.1:27042)\n"
               f"Underlying error: {e}\n")
        f_log.write(msg)
        f_log.close(); f_audio.close(); f_trace.close()
        raise SystemExit(msg) from e

    # ── spawn target on the Windows side ──
    win_exe = wslpath_w(cfg.exe)
    win_cwd = wslpath_w(cfg.cwd)
    f_log.write(f"[spawn] {win_exe} (cwd {win_cwd})\n")

    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())
    script.on("message", on_message)
    script.load()

    # Load the input trace, if any. Parsing is forgiving: blank lines
    # and `#` comments are tolerated (matches Phase A's parser in
    # src/input_trace.c), and missing file just yields an empty list so
    # force_input=True still works as a "pin at 0" mode.
    trace_entries: list[dict[str, int]] = []
    if cfg.input_trace_path and cfg.input_trace_path.exists():
        for raw in cfg.input_trace_path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            rec = json.loads(line)
            mask_val = rec["buttons"]
            mask = int(mask_val, 16) if isinstance(mask_val, str) else int(mask_val)
            trace_entries.append({"frame": int(rec["frame"]), "mask": mask})
        trace_entries.sort(key=lambda r: r["frame"])
        f_log.write(f"[input] loaded {len(trace_entries)} entries from "
                    f"{cfg.input_trace_path} (force_input={cfg.force_input})\n")

    t0 = time.monotonic()
    init_cfg: dict[str, Any] = {
        "capture_frames": list(cfg.capture_frames),
        "max_frames":     cfg.max_frames,
        "input_trace":    trace_entries,
        "force_input":    bool(cfg.force_input),
        "hide_window":    bool(cfg.hide_window),
        "turbo":          bool(cfg.turbo),
        "turbo_step_ms":  int(cfg.turbo_step_ms),
        "silent_audio":   bool(cfg.silent_audio),
    }
    if cfg.force_resolution is not None:
        init_cfg["force_resolution"] = [int(cfg.force_resolution[0]),
                                        int(cfg.force_resolution[1])]
    if cfg.d3d_trace:
        init_cfg["d3d_trace"] = True
        if cfg.d3d_trace_frames is not None:
            init_cfg["d3d_trace_frames"] = [int(f) for f in cfg.d3d_trace_frames]
    if cfg.call_trace:
        init_cfg["call_trace"] = True
        init_cfg["call_trace_vas"] = [int(v) for v in (cfg.call_trace_vas or [])]
        if cfg.call_trace_frames is not None:
            init_cfg["call_trace_frames"] = [int(f) for f in cfg.call_trace_frames]
    if cfg.auto_z_spam:
        init_cfg["auto_z_spam"] = True
    if cfg.auto_3d_trace:
        init_cfg["auto_3d_trace"] = True
        init_cfg["auto_3d_trace_frames"] = int(cfg.auto_3d_trace_frames)
    script.exports_sync.init(init_cfg)
    device.resume(pid)

    # ── wait for max_frames signal or wall-clock ceiling ──
    deadline = t0 + (cfg.duration_ms / 1000.0)
    while not done.is_set() and time.monotonic() < deadline:
        # If we've captured every frame the scenario asked for AND the
        # engine has run past max_frames, we can shut down even without
        # the explicit signal (the agent only fires that on a Present).
        if (cfg.capture_frames
                and set(captured) >= set(cfg.capture_frames)
                and last_engine_frame >= cfg.max_frames):
            f_log.write(f"[done] all frames captured ({len(captured)})\n")
            break
        time.sleep(0.05)

    elapsed_ms = int((time.monotonic() - t0) * 1000)
    exit_code = 0

    # ── shut the target down ──
    try:
        script.unload()
    except Exception as e:
        f_log.write(f"[shutdown] script unload: {e}\n")
    try:
        session.detach()
    except Exception as e:
        f_log.write(f"[shutdown] session detach: {e}\n")
    try:
        device.kill(pid)
    except Exception as e:
        f_log.write(f"[shutdown] kill pid={pid}: {e}\n")
        exit_code = 1

    f_audio.close(); f_trace.close(); f_log.close()
    if f_d3d is not None:
        f_d3d.close()
    if f_call is not None:
        f_call.close()

    return CaptureResult(
        exit_code=exit_code,
        elapsed_ms=elapsed_ms,
        captured_frames=sorted(set(captured)),
        last_engine_frame=last_engine_frame,
    )


def run_capture(scenario: "Any", run_dir: Path, *,
                remote: str = DEFAULT_REMOTE,
                exe: Path = RETAIL_EXE,
                cwd: Path = ASSET_CWD,
                auto_start_server: bool = True,
                server_exe: Path = DEFAULT_FRIDA_SERVER_EXE,
                input_trace_path: Path | None = None,
                force_input: bool = False,
                hide_window: bool = False,
                turbo: bool = False,
                turbo_step_ms: int = 17,
                silent_audio: bool = False,
                force_resolution: tuple[int, int] | None = None) -> dict:
    """Phase A-compatible entry point. `scenario` is a tools/scenario-test.Scenario
    (duck-typed: needs .capture_frames, .max_frames, .duration_ceiling_ms).
    Returns the meta dict that scenario-test.py writes to run.json.

    `input_trace_path` + `force_input` enable input injection: the agent
    overwrites the engine's per-frame input mask with the sticky-trace
    value on every input_poll LEAVE. Default off so legacy callers
    capture an organic trace.

    `hide_window` toggles the agent's ShowWindow → SW_HIDE rewrite plus
    the DAT_073dfca0 pause-flag compensation. scenario-test.py opts in
    so capture runs don't pop a steal-focus window the user might key
    into.
    """
    cfg = CaptureConfig(
        capture_frames=list(scenario.capture_frames),
        max_frames=int(scenario.max_frames),
        duration_ms=int(getattr(scenario, "duration_ceiling_ms", 30_000)),
        remote=remote, exe=exe, cwd=cwd,
        auto_start_server=auto_start_server, server_exe=server_exe,
        input_trace_path=input_trace_path, force_input=force_input,
        hide_window=hide_window,
        turbo=turbo, turbo_step_ms=turbo_step_ms,
        silent_audio=silent_audio,
        force_resolution=force_resolution,
    )
    result = _run_capture_impl(cfg, run_dir)
    meta = {
        "scenario":         getattr(scenario, "name", "(ad-hoc)"),
        "target":           "retail",
        "exit_code":        result.exit_code,
        "elapsed_ms":       result.elapsed_ms,
        "captured_frames":  result.captured_frames,
        "last_engine_frame": result.last_engine_frame,
        "remote":           remote,
        "exe":              str(cfg.exe),
    }
    (run_dir / "run.json").write_text(json.dumps(meta, indent=2))
    return meta


# ─── cli ──────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--remote", default=DEFAULT_REMOTE,
                    help="frida-server host:port (default %(default)s)")
    ap.add_argument("--exe", type=Path, default=RETAIL_EXE,
                    help="target exe path (Linux side; will be wslpath-w'd)")
    ap.add_argument("--cwd", type=Path, default=ASSET_CWD,
                    help="target cwd on Windows side (default vendor/original/)")
    ap.add_argument("--run-dir", type=Path, required=True,
                    help="where to write frames/audio.jsonl/trace.jsonl/agent.log")
    ap.add_argument("--capture-frames", default="",
                    help="comma-separated engine-frame indices to capture")
    ap.add_argument("--max-frames", type=int, default=60)
    ap.add_argument("--duration-ms", type=int, default=30_000)
    ap.add_argument("--no-auto-start", action="store_true",
                    help="skip auto-launching frida-server.exe if it's not "
                         "already running")
    ap.add_argument("--server-exe", type=Path, default=DEFAULT_FRIDA_SERVER_EXE,
                    help="WSL path to frida-server.exe used for auto-start "
                         "(default %(default)s; "
                         "env $OPENRECET_FRIDA_SERVER_EXE)")
    ap.add_argument("--input-trace", type=Path, default=None,
                    help="sparse JSONL trace ({frame, buttons:'0xNNNN'}) to "
                         "replay against retail. Implies --force-input.")
    ap.add_argument("--force-input", action="store_true",
                    help="overwrite the engine's input mask each frame "
                         "with the trace value (or 0 if no trace given)")
    ap.add_argument("--hide-window", action="store_true",
                    help="rewrite the engine's first ShowWindow to SW_HIDE "
                         "and force its pause flag (DAT_073dfca0) to 1, so "
                         "the game runs without a window the user could "
                         "key into. D3D capture path unaffected.")
    ap.add_argument("--turbo", action="store_true",
                    help="bypass the frame limiter: virtualise FUN_0047be2f "
                         "so the dispatcher sees a 16.6 ms (or --turbo-step-ms) "
                         "delta every loop iteration and never Sleeps. "
                         "Pair with --silent-audio.")
    ap.add_argument("--turbo-step-ms", type=int, default=17,
                    help="virtual ms per dispatcher entry under --turbo "
                         "(default %(default)s)")
    ap.add_argument("--silent-audio", action="store_true",
                    help="clamp every SetVolume call on the audio paths to "
                         "-10000 centibel so nothing is audible. Game's "
                         "PlaySegmentEx / fade animations / segment state "
                         "all run normally — only DirectMusic's master "
                         "attenuation is forced to silence.")
    ap.add_argument("--force-resolution", default=None,
                    metavar="WxH",
                    help="hook the engine's recet.ini parse exit and "
                         "overwrite DAT_005cbc04/08 (screen width/height) "
                         "so retail captures at the requested dims even "
                         "when its vendor/unpacked/recet.ini is empty or "
                         "stale. Example: --force-resolution 1024x768")
    ap.add_argument("--d3d-trace", action="store_true",
                    help="hook IDirect3DDevice8 vtable slots "
                         "(SetRenderState / SetTransform / SetTexture / "
                         "DrawIndexedPrimitive et al.) and write one JSONL "
                         "row per call to <run_dir>/d3d_trace.jsonl. "
                         "Phase D.4 — pairs with src/d3d_trace.c on the "
                         "port side + tools/render_diff.py.")
    ap.add_argument("--d3d-trace-frames", default="",
                    help="comma-separated frame numbers to limit the D3D "
                         "trace to. Default empty = every frame (large!). "
                         "Use this for any non-title scenario.")
    ap.add_argument("--call-trace", action="store_true",
                    help="hook every engine function entry (default list: "
                         "tools/ttd/data/engine_function_vas.json, 2103 VAs) "
                         "and emit one JSONL row per invocation to "
                         "<run_dir>/call_trace.jsonl. Phase E.1 — per-frame "
                         "ordered call list for leaf-first porting. Pair "
                         "with --call-trace-frames or output saturates the "
                         "Frida wire.")
    ap.add_argument("--call-trace-vas-file", type=Path, default=None,
                    help="override the default engine VA list. JSON: either "
                         "a bare array of ints, or the metadata-dict form "
                         "{vas: [...], ...}. Useful for trimming to a "
                         "render-side subset.")
    ap.add_argument("--call-trace-frames", default="",
                    help="comma-separated frame numbers to limit call_trace "
                         "to. STRONGLY recommended — unfiltered runs can "
                         "emit tens of thousands of events per frame.")
    ap.add_argument("--auto-z-spam", action="store_true",
                    help="drive the engine past the intro cutscene by "
                         "pressing button A (Z on keyboard) at ~15Hz "
                         "unattended. Mutually exclusive with --input-trace.")
    ap.add_argument("--auto-3d-trace", action="store_true",
                    help="pair with --call-trace + --auto-z-spam: arm "
                         "call_trace emit ONLY for the N-frame window "
                         "starting at the first DrawIndexedPrimitive call "
                         "(= we just entered HOUSE / 3D shop). The driver "
                         "shuts down cleanly once the window closes.")
    ap.add_argument("--auto-3d-trace-frames", type=int, default=60,
                    help="how many frames to capture after the 3D-scene "
                         "trigger fires (default 60 = 1s of game time).")
    args = ap.parse_args(argv)
    fr_tuple: tuple[int, int] | None = None
    if args.force_resolution:
        try:
            w_s, h_s = args.force_resolution.lower().split("x")
            fr_tuple = (int(w_s), int(h_s))
        except (ValueError, AttributeError):
            ap.error(f"--force-resolution: expected WxH, got "
                     f"{args.force_resolution!r}")

    capture_frames = ([int(x) for x in args.capture_frames.split(",") if x]
                      if args.capture_frames else [])

    d3d_trace_frames: list[int] | None = None
    if args.d3d_trace_frames:
        d3d_trace_frames = [int(x) for x in args.d3d_trace_frames.split(",") if x]

    call_trace_frames: list[int] | None = None
    if args.call_trace_frames:
        call_trace_frames = [int(x) for x in args.call_trace_frames.split(",") if x]

    call_trace_vas: list[int] | None = None
    if args.call_trace:
        ct_path = args.call_trace_vas_file or (
            ROOT / "tools" / "ttd" / "data" / "engine_function_vas.json")
        if not ct_path.exists():
            ap.error(f"--call-trace: VA list not found at {ct_path}; pass "
                     f"--call-trace-vas-file to override")
        raw = json.loads(ct_path.read_text())
        call_trace_vas = (raw["vas"] if isinstance(raw, dict) and "vas" in raw
                          else list(raw))

    if args.auto_z_spam and args.input_trace is not None:
        ap.error("--auto-z-spam and --input-trace are mutually exclusive")

    cfg = CaptureConfig(
        capture_frames=capture_frames,
        max_frames=args.max_frames,
        duration_ms=args.duration_ms,
        remote=args.remote, exe=args.exe, cwd=args.cwd,
        auto_start_server=not args.no_auto_start,
        server_exe=args.server_exe,
        input_trace_path=args.input_trace,
        force_input=args.force_input or args.input_trace is not None,
        hide_window=args.hide_window,
        turbo=args.turbo, turbo_step_ms=args.turbo_step_ms,
        silent_audio=args.silent_audio,
        force_resolution=fr_tuple,
        d3d_trace=args.d3d_trace,
        d3d_trace_frames=d3d_trace_frames,
        call_trace=args.call_trace,
        call_trace_vas=call_trace_vas,
        call_trace_frames=call_trace_frames,
        auto_z_spam=args.auto_z_spam,
        auto_3d_trace=args.auto_3d_trace,
        auto_3d_trace_frames=args.auto_3d_trace_frames,
    )
    args.run_dir.mkdir(parents=True, exist_ok=True)
    result = _run_capture_impl(cfg, args.run_dir)
    print(json.dumps({
        "exit_code":        result.exit_code,
        "elapsed_ms":       result.elapsed_ms,
        "captured_frames":  result.captured_frames,
        "last_engine_frame": result.last_engine_frame,
    }, indent=2))
    return result.exit_code


if __name__ == "__main__":
    sys.exit(main())
