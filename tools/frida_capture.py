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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import frame_io   # noqa: E402 — lossless PNG frame writer (vs 3 MB BMPs)


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


def parse_anchor_spec(spec: str) -> dict:
    """Parse a `--capture-at-anchor` token `NAME[+k|-k]` into
    {"name": str, "offset": int}. Anchor names are UPPER_SNAKE (no digits or
    signs), so the first +/- begins the signed offset — same split rule the
    port uses in src/main.c. A bare NAME means offset 0.
    """
    sep = len(spec)
    for i, ch in enumerate(spec):
        if ch in "+-":
            sep = i
            break
    name = spec[:sep]
    offset = int(spec[sep:], 10) if sep < len(spec) else 0
    if not name:
        raise ValueError(f"--capture-at-anchor: empty anchor name in {spec!r}")
    return {"name": name, "offset": offset}


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
    # TAS P3 — anchor-segmented input forcing (the auto_z_spam replacement).
    # A JSONL superset of input_trace: `{"wait":NAME}` ops rebase the segment
    # frame-0 onto the live anchor stream, so the logical trace lands the same
    # on port and retail despite load jitter. Owns the input mask when set
    # (checked before force_input) and implies anchor_trace.
    input_segtrace_path: Path | None = None
    # Per-frame global watch: list of {name, va, type:'f32'|'s32'|'u16'}. The
    # agent reads each address once per frame and emits a `watch` record;
    # the driver appends to <run_dir>/watch.jsonl. A general state probe for
    # locating when a value begins changing under a forced input.
    watch: list[dict[str, Any]] | None = None
    # When true, tile captured frames into 3x3 montages and open them with the
    # default Windows image viewer at the end of the run (quick inspection).
    montage: bool = True
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
    # `call_trace_vas` defaults to the bisect-vetted Frida-safe engine
    # function-entry list at
    # tools/ttd/data/engine_function_vas_frida_safe.json (1979 entries —
    # the wider engine_function_vas.json contains entries that crash
    # the engine when hooked; see tools/bisect_call_trace_vas.py).
    # `call_trace_frames` is a per-frame whitelist — strongly
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
    # Inverse of `auto_3d_trace`.  When true the agent emits call_trace
    # for every frame BEFORE the first DrawIndexedPrimitive call, then
    # sends `pre_3d_trace_done` so the driver shuts down.  Pair with
    # `auto_z_spam` to drive past the title menu unattended.  Output
    # covers title screen + intro cutscene up to (not including) the
    # first HOUSE 3D frame.
    pre_3d_trace:           bool = False
    # TAS anchor emitter (P1 retail side — docs/plans/tas-framework.md).
    # When `anchor_trace` is true the agent samples the engine scene/loading
    # globals each Present and emits {kind:"anchor", anchor:NAME, frame:N}
    # on rising edges (BOOT / NEW_GAME / LOADING_START / LOADING_END /
    # HOUSE_FREEROAM — same names the port writes from src/anchor_trace.c).
    # The driver appends them to `<run_dir>/anchors.jsonl`. Pair with
    # `auto_z_spam` to drive a fresh new-game to HOUSE unattended.
    anchor_trace:           bool = False
    # TAS P2 retail side — anchor-relative capture (`--capture-at-anchor
    # NAME[+k]`). A list of {"name": str, "offset": int}; each resolves to a
    # backbuffer capture at (anchor_frame + offset) when NAME fires, so a
    # capture lands on the SAME semantic instant on both targets despite the
    # load jitter that makes absolute frame numbers meaningless. Mirrors the
    # port's --capture-at-anchor (src/main.c). Implies anchor_trace (forced on
    # below). The agent shuts itself down via `capture_at_anchor_done` once
    # every requested anchor has fired and every resolved target is captured.
    capture_at_anchor:      list[dict] | None = None
    # Memory-access watch (Phase D.7). When `mem_watch` is true the agent
    # arms Frida's MemoryAccessMonitor over `mem_watch_regions` and emits
    # one record per trapped access (faulting instruction VA + accessed
    # data VA, both Ghidra VAs) to `<run_dir>/mem_watch.jsonl`. Used to
    # locate the writer of a region whose filler isn't visible in the
    # decompile — the unblock path for the HOUSE shop_table render gap.
    # Each region is {va: int (Ghidra VA), size: int, label: str,
    # access: "w"|"rw"}. Pair with `auto_z_spam` to drive to HOUSE.
    mem_watch:              bool = False
    mem_watch_regions:      list[dict] | None = None
    # Precise mode (default): re-arm MemoryAccessMonitor on page-neighbor
    # traps and only record accesses that land inside a watched field, so
    # an unrelated write elsewhere on the 4KiB page can't consume the
    # page's one-shot and mask the writer we're hunting. Set False for the
    # raw one-shot-per-page behavior.
    mem_watch_precise:      bool = True
    # Cchr.0 table-B dump. When `dump_records_b` is true the agent shares
    # the auto-3D trigger (anchor on first DrawIndexedPrimitive), then on
    # each frame offset in `dump_records_b_offsets` (relative to that first
    # 3D frame) reads the live scene-1 table-B render records + the three
    # per-pass counts + player pos and emits one JSON object to
    # `<run_dir>/records_b_dump.jsonl`. After the last offset it sends
    # `dump_records_b_done` and the driver shuts down. Pair with
    # `auto_z_spam` to drive a fresh new-game to HOUSE unattended. Answers:
    # does retail's records_b hold a live player record on a fresh HOUSE,
    # and which TYPE / owner-class / scale draws it (= which FUN_004176ff
    # sub-pass renders the player avatar).
    dump_records_b:         bool = False
    dump_records_b_offsets: list[int] | None = None
    # Also grab a backbuffer screenshot at each table-B dump frame (to
    # <run_dir>/frames/<frame>.bmp) for visual confirmation of the scene.
    dump_records_b_capture: bool = False
    # Heartbeat interval (frames) for the records_b_sample progress message
    # (counts + per-frame draw tally). 0 disables.
    dump_records_b_heartbeat: int = 1024
    # Cchr.1 — quad-add caller histogram (rides the dump_records_b drive).
    # Hooks FUN_00404efc + DrawPrimitive(UP)/SetTexture and records every
    # call on each dump-offset frame to <run_dir>/quad_trace.jsonl, naming
    # the 2D caller VA + texture block that emits the player sprite.
    quad_hist: bool = False
    # Cchr.2b — character-sprite leaf capture (rides the dump_records_b
    # drive). Hooks FUN_0045a56f at ENTER (its inputs) + its own
    # DrawPrimitiveUP (the built vertex buffer) and writes one chr_leaf
    # record per dump-offset frame to <run_dir>/chr_leaf.jsonl, so the
    # port's chr_sprite_build_quads can be bit-compared against retail.
    chr_leaf: bool = False
    # RNG caller histogram — hook FUN_005041f6 (the shared global LCG) and
    # tally the immediate caller VA. Writes <run_dir>/rng_callers.json (a
    # cumulative {ret_va: count} map). Finds which subsystems advance the
    # shared RNG stream per frame, the metric for foot-dust / particle RNG
    # parity vs the port.
    rng_callers: bool = False


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
    mem_watch_jsonl = run_dir / "mem_watch.jsonl"
    f_mem = mem_watch_jsonl.open("w") if cfg.mem_watch else None
    records_b_jsonl = run_dir / "records_b_dump.jsonl"
    f_recb = records_b_jsonl.open("w") if cfg.dump_records_b else None
    quad_jsonl = run_dir / "quad_trace.jsonl"
    f_quad = quad_jsonl.open("w") if cfg.quad_hist else None
    chr_leaf_jsonl = run_dir / "chr_leaf.jsonl"
    f_leaf = chr_leaf_jsonl.open("w") if cfg.chr_leaf else None
    anchors_jsonl = run_dir / "anchors.jsonl"
    # capture_at_anchor forces the anchor poll on the agent side, so record
    # the anchor stream here too even when --anchor-trace wasn't passed.
    f_anchor = (anchors_jsonl.open("w", buffering=1)
                if (cfg.anchor_trace or cfg.capture_at_anchor) else None)
    watch_jsonl = run_dir / "watch.jsonl"
    f_watch = (watch_jsonl.open("w", buffering=1) if cfg.watch else None)

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
            png_path = frames_dir / f"frame_{frame:05d}.png"
            frame_io.write_frame_png(png_path, w, h, data)
            captured.append(frame)
            last_engine_frame = max(last_engine_frame, frame)
            f_log.write(f"[frame] {png_path.name} {w}x{h}\n")
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

        if kind == "mem_watch_ready":
            regs = p.get("regions") or []
            f_log.write(f"[mem_watch] armed {len(regs)} region(s): "
                        + ", ".join(f"{r.get('label')}@0x{int(r.get('va',0)):08x}"
                                    f"+{r.get('size')}({r.get('access')})"
                                    for r in regs) + "\n")
            return

        if kind == "mem_access_batch":
            if f_mem is None:
                return
            frame  = int(p["frame"])
            events = p.get("events") or []
            for ev in events:
                ev_out = dict(ev)
                ev_out["frame"] = frame
                f_mem.write(json.dumps(ev_out) + "\n")
            f_log.write(f"[mem_watch] frame={frame} accesses={len(events)}\n")
            return

        if kind == "anchor":
            name  = str(p.get("anchor", "?"))
            frame = int(p.get("frame", -1))
            last_engine_frame = max(last_engine_frame, frame)
            if f_anchor is not None:
                f_anchor.write(json.dumps({
                    "anchor": name,
                    "frame":  frame,
                }) + "\n")
            f_log.write(f"[anchor] {name} @ frame={frame}\n")
            return

        if kind == "watch":
            if f_watch is not None:
                f_watch.write(json.dumps({
                    "frame": int(p.get("frame", -1)),
                    "vals":  p.get("vals", {}),
                }) + "\n")
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

        if kind == "capture_at_anchor_done":
            f_log.write(f"[capture_at_anchor] all requested anchors fired + "
                        f"captures landed @ frame={p.get('frame')}; "
                        f"signaling shutdown\n")
            done.set()
            return

        if kind == "pre_3d_trace_done":
            f_log.write(f"[pre_3d] first 3D draw @ frame={p.get('last_frame')}; "
                        f"signaling shutdown\n")
            done.set()
            return

        if kind == "records_b_sample":
            f_log.write(f"[records_b] sample frame={p.get('frame')} "
                        f"count_b={p.get('count_b')} "
                        f"count_a={p.get('count_a')} "
                        f"count_c={p.get('count_c')} "
                        f"draws={p.get('draws')} "
                        f"draws_max={p.get('draws_max')} "
                        f"anchored={p.get('anchored')}\n")
            return

        if kind == "records_b_populated":
            f_log.write(f"[records_b] table populated @ frame={p.get('frame')} "
                        f"count_a={p.get('count_a')} "
                        f"count_b={p.get('count_b')}\n")
            return

        if kind == "records_b_dump":
            if f_recb is not None:
                f_recb.write(json.dumps(p) + "\n")
                f_recb.flush()
            f_log.write(f"[records_b] dump frame={p.get('frame')} "
                        f"off={p.get('offset_from_3d')} "
                        f"count_a={p.get('count_a')} "
                        f"count_b={p.get('count_b')} "
                        f"liveA={p.get('live_total_a')}/{p.get('emitted_a')} "
                        f"liveB={p.get('live_total')}/{p.get('emitted')} "
                        f"people={p.get('live_total_people')}/"
                        f"{p.get('emitted_people')}\n")
            return

        if kind == "quad_frame":
            if f_quad is not None:
                f_quad.write(json.dumps(p) + "\n")
                f_quad.flush()
            f_log.write(f"[quad] frame={p.get('frame')} "
                        f"off={p.get('offset_from_3d')} "
                        f"events={p.get('event_count')} "
                        f"player_pos={p.get('player_pos')}\n")
            return

        if kind == "quad_hist":
            if f_quad is not None:
                f_quad.write(json.dumps(p) + "\n")
                f_quad.flush()
            f_log.write(f"[quad] histogram: {p.get('bucket_count')} caller "
                        f"buckets [frames {p.get('first_frame')}.."
                        f"{p.get('last_frame')}]\n")
            for b in (p.get("buckets") or [])[:20]:
                va = b.get("va")
                f_log.write(
                    f"  va=0x{va:08x} n={b.get('count')} "
                    f"dx=[{b.get('dx_min'):.0f}..{b.get('dx_max'):.0f}] "
                    f"dy=[{b.get('dy_min'):.0f}..{b.get('dy_max'):.0f}] "
                    f"dims={list((b.get('dims') or {}).keys())}\n")
            return

        if kind == "chr_leaf":
            if f_leaf is not None:
                f_leaf.write(json.dumps(p) + "\n")
                f_leaf.flush()
            n_in = sum(1 for e in (p.get("events") or [])
                       if e.get("ev") == "leaf_in")
            n_out = sum(1 for e in (p.get("events") or [])
                        if e.get("ev") == "leaf_out")
            f_log.write(f"[chr_leaf] frame={p.get('frame')} "
                        f"off={p.get('offset_from_3d')} "
                        f"player_char_id={p.get('player_char_id')} "
                        f"leaf_in={n_in} leaf_out={n_out} "
                        f"player_pos={p.get('player_pos')}\n")
            return

        if kind == "rng_callers":
            # Cumulative {ret_va: count}. Overwrite a single JSON file so the
            # last flush holds the full run total; also log a short top-N.
            hist = p.get("hist") or {}
            try:
                (run_dir / "rng_callers.json").write_text(
                    json.dumps({"frame": p.get("frame"), "hist": hist},
                               indent=2))
                # Also append each cumulative snapshot so windows can be
                # diffed (free-roam vs intro) post-hoc.
                with (run_dir / "rng_callers.jsonl").open("a") as fh:
                    fh.write(json.dumps({"frame": p.get("frame"),
                                         "hist": hist}) + "\n")
            except Exception:
                pass
            top = sorted(hist.items(), key=lambda kv: -kv[1])[:12]
            f_log.write(f"[rng_callers] frame={p.get('frame')} "
                        f"distinct={len(hist)} top="
                        + ", ".join(f"{k}:{v}" for k, v in top) + "\n")
            return

        if kind == "dump_records_b_done":
            f_log.write(f"[records_b] dump window done "
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

    # Detach handler — fires when the target dies (crash, exit, kill from
    # outside).  Without this the driver sits waiting on `done` for the
    # full --duration-ms even though there's nothing alive to trace.
    # Sets `done` so the main loop falls through and reports the early
    # exit in the log.
    def on_detached(reason: str, crash: Any) -> None:
        f_log.write(f"[detached] reason={reason!r} crash={crash!r}\n")
        done.set()
    session.on("detached", on_detached)

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

    # TAS P3 — anchor-segmented input trace. Same JSONL style as above, plus
    # `{"wait":"ANCHOR_NAME"}` segment-break ops. Order is preserved (it is the
    # logical timeline); `{frame,buttons}` entries are segment-relative. Lowers
    # to the agent's segtrace state machine, which rebases on the live anchors.
    segtrace_ops: list[dict[str, Any]] = []
    if cfg.input_segtrace_path and cfg.input_segtrace_path.exists():
        for raw in cfg.input_segtrace_path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            rec = json.loads(line)
            if "wait" in rec:
                segtrace_ops.append({"wait": str(rec["wait"])})
            elif "wait_until" in rec:
                # Threshold segment-break: hold this segment's input until a
                # live global crosses a comparator (e.g. UP until pz<=3), then
                # rebase. `va` accepts a 0x-string or int; `type` defaults f32,
                # `op` defaults "<=".  Removes frame-count guessing for moves.
                w = rec["wait_until"]
                va = w["va"]
                va = int(va, 16) if isinstance(va, str) else int(va)
                segtrace_ops.append({"wait_until": {
                    "va": va,
                    "type": str(w.get("type", "f32")),
                    "op": str(w.get("op", "<=")),
                    "val": float(w["val"]),
                }})
            elif "capture" in rec:
                # Screenshot the deterministic frame base+N (N frames after the
                # current segment's anchor) — for visual state verification.
                segtrace_ops.append({"capture": int(rec["capture"])})
            elif "calltrace" in rec:
                # Arm the call tracer anchor-relative (no absolute frame guess):
                # N -> [base, base+N]; [start,len] -> [base+start, base+start+len].
                ct = rec["calltrace"]
                segtrace_ops.append({"calltrace": (
                    [int(ct[0]), int(ct[1])] if isinstance(ct, list) else int(ct))})
            else:
                mask_val = rec["buttons"]
                mask = int(mask_val, 16) if isinstance(mask_val, str) else int(mask_val)
                segtrace_ops.append({"frame": int(rec["frame"]), "mask": mask})
        f_log.write(f"[input] loaded {len(segtrace_ops)} segtrace ops from "
                    f"{cfg.input_segtrace_path}\n")

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
    if cfg.pre_3d_trace:
        init_cfg["pre_3d_trace"] = True
    if cfg.anchor_trace:
        init_cfg["anchor_trace"] = True
    if segtrace_ops:
        # Anchor-segmented forcing owns the input mask and needs the anchor
        # poll for its `wait` ops; the agent forces anchor_trace on too.
        init_cfg["input_segtrace"] = segtrace_ops
        init_cfg["anchor_trace"] = True
    if cfg.watch:
        init_cfg["watch"] = [
            {"name": str(w["name"]), "va": int(w["va"]),
             "type": str(w.get("type", "s32"))}
            for w in cfg.watch
        ]
    if cfg.capture_at_anchor:
        # Implies the anchor poll; the agent also forces anchor_trace on, but
        # set it here too so the `ready` echo + anchors.jsonl line up.
        init_cfg["anchor_trace"] = True
        init_cfg["capture_at_anchor"] = [
            {"name": str(r["name"]), "offset": int(r.get("offset", 0))}
            for r in cfg.capture_at_anchor
        ]
    if cfg.dump_records_b:
        init_cfg["dump_records_b"] = True
        init_cfg["dump_records_b_capture"] = bool(cfg.dump_records_b_capture)
        init_cfg["dump_records_b_heartbeat"] = int(cfg.dump_records_b_heartbeat)
        if cfg.dump_records_b_offsets is not None:
            init_cfg["dump_records_b_offsets"] = [
                int(o) for o in cfg.dump_records_b_offsets]
        if cfg.quad_hist:
            init_cfg["quad_hist"] = True
        if cfg.chr_leaf:
            init_cfg["chr_leaf"] = True
    if cfg.rng_callers:
        init_cfg["rng_callers"] = True
    if cfg.mem_watch:
        init_cfg["mem_watch"] = True
        init_cfg["mem_watch_precise"] = bool(cfg.mem_watch_precise)
        init_cfg["mem_watch_regions"] = [
            {
                "va":     int(r["va"]),
                "size":   int(r.get("size", 16)),
                "label":  str(r.get("label", f"0x{int(r['va']):08x}")),
                "access": "rw" if r.get("access") == "rw" else "w",
            }
            for r in (cfg.mem_watch_regions or [])
        ]
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
    if f_mem is not None:
        f_mem.close()
    if f_recb is not None:
        f_recb.close()
    if f_quad is not None:
        f_quad.close()
    if f_leaf is not None:
        f_leaf.close()
    if f_anchor is not None:
        f_anchor.close()
    if f_watch is not None:
        f_watch.close()

    # Tile captured frames into 3x3 montage PNG(s) under run_dir. (Auto-open in
    # the Windows viewer was removed — push the montage to the llm-feed to view.)
    if cfg.montage and captured:
        try:
            from montage_frames import build_montages
            build_montages(run_dir)
        except Exception as e:  # never fail a capture over the montage step
            f_log.write(f"[montage] skipped: {e}\n")

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
                input_segtrace_path: Path | None = None,
                force_input: bool = False,
                hide_window: bool = False,
                turbo: bool = False,
                turbo_step_ms: int = 17,
                silent_audio: bool = False,
                force_resolution: tuple[int, int] | None = None) -> dict:
    """Phase A-compatible entry point. `scenario` is a tools/scenario-test.Scenario
    (duck-typed: needs .capture_frames, .max_frames, .duration_ceiling_ms).
    Returns the meta dict that scenario-test.py writes to run.json.

    `input_trace_path` + `force_input` enable absolute-frame input injection:
    the agent overwrites the engine's per-frame input mask with the
    sticky-trace value on every input_poll LEAVE. Default off so legacy
    callers capture an organic trace.

    `input_segtrace_path` enables anchor-segmented forcing instead (TAS P3):
    the agent owns the input mask AND schedules anchor-relative captures from
    the trace's {capture} ops, so the caller should NOT also pass
    capture_frames (the scenario's are empty in segtrace mode). Mutually
    exclusive with input_trace_path / force_input. Implies anchor_trace.

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
        input_trace_path=input_trace_path,
        input_segtrace_path=input_segtrace_path,
        force_input=force_input,
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
    ap.add_argument("--input-segtrace", type=Path, default=None,
                    help="anchor-segmented JSONL trace: same as --input-trace "
                         "plus {\"wait\":\"ANCHOR\"} ops that rebase following "
                         "frames onto the live anchor stream (deterministic "
                         "across load jitter). Supersedes --auto-z-spam; "
                         "implies --anchor-trace.")
    ap.add_argument("--force-input", action="store_true",
                    help="overwrite the engine's input mask each frame "
                         "with the trace value (or 0 if no trace given)")
    ap.add_argument("--watch", action="append", default=None,
                    metavar="NAME=0xVA[:type]",
                    help="per-frame global watch (repeatable). type is "
                         "f32|s32|u16 (default s32). Writes watch.jsonl. "
                         "e.g. --watch px=0x056da1d8:f32 --watch cc08=0x0438cc08")
    ap.add_argument("--no-montage", action="store_true",
                    help="don't tile captured frames into 3x3 montages / "
                         "auto-open them in the Windows image viewer")
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
                         "tools/ttd/data/engine_function_vas_frida_safe.json, "
                         "1979 VAs vetted by tools/bisect_call_trace_vas.py — "
                         "the unvetted superset engine_function_vas.json "
                         "contains entries that destabilize the retail "
                         "engine on boot) and emit one JSONL row per "
                         "invocation to <run_dir>/call_trace.jsonl. Phase "
                         "E.1 — per-frame ordered call list for leaf-first "
                         "porting. Pair with --call-trace-frames or output "
                         "saturates the Frida wire.")
    ap.add_argument("--no-call-trace", action="store_true",
                    help="opt out of the automatic call-trace enable that "
                         "fires when --input-segtrace declares a {calltrace} "
                         "op (the op is normally the single source of truth — "
                         "no --call-trace flag needed).")
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
    ap.add_argument("--pre-3d-trace", action="store_true",
                    help="inverse of --auto-3d-trace: capture call_trace "
                         "for every frame BEFORE the first "
                         "DrawIndexedPrimitive call (= title + intro "
                         "cutscene), then shut down on first 3D draw. "
                         "Pair with --auto-z-spam to drive past the title "
                         "menu unattended.")
    ap.add_argument("--anchor-trace", action="store_true",
                    help="TAS P1: sample the engine scene/loading globals "
                         "each Present and emit rising-edge anchors "
                         "(BOOT / NEW_GAME / LOADING_START / LOADING_END / "
                         "HOUSE_FREEROAM) to <run_dir>/anchors.jsonl. Same "
                         "names src/anchor_trace.c writes on the port side, "
                         "so one spec aligns both targets. Pair with "
                         "--auto-z-spam to drive a fresh new-game to HOUSE.")
    ap.add_argument("--capture-at-anchor", action="append", default=None,
                    metavar="NAME[+k]",
                    help="TAS P2: capture the backbuffer at frame "
                         "(anchor_frame + k) when the named anchor fires, "
                         "instead of a fixed absolute frame. Robust to the "
                         "non-deterministic new-game->HOUSE load (which "
                         "absolute --capture-frames can't hit). NAME is an "
                         "UPPER_SNAKE anchor (BOOT / NEW_GAME / LOADING_START "
                         "/ LOADING_END / HOUSE_FREEROAM); k is an optional "
                         "signed offset (default 0). Repeatable. Mirrors the "
                         "port's same-named flag. Implies --anchor-trace; the "
                         "driver shuts down once every requested anchor has "
                         "fired and its capture landed. Pair with --auto-z-spam "
                         "+ --hide-window + --force-resolution. Example: "
                         "--capture-at-anchor HOUSE_FREEROAM+30")
    ap.add_argument("--dump-records-b", action="store_true",
                    help="Cchr.0: dump scene-1 table-B render records at "
                         "frame offsets from the first 3D draw (default "
                         "0,5,30,60) to <run_dir>/records_b_dump.jsonl, then "
                         "shut down. Pair with --auto-z-spam to drive a fresh "
                         "new-game to HOUSE unattended. Finds the player "
                         "render record + which FUN_004176ff sub-pass draws "
                         "it.")
    ap.add_argument("--dump-records-b-offsets", default="",
                    help="comma-separated frame offsets from the anchor "
                         "(first count_b>0 frame) for --dump-records-b "
                         "(default 0,30,120,300)")
    ap.add_argument("--dump-records-b-capture", action="store_true",
                    help="also grab a backbuffer screenshot at each "
                         "--dump-records-b dump frame (frames/<frame>.bmp)")
    ap.add_argument("--dump-records-b-heartbeat", type=int, default=1024,
                    help="frames between records_b_sample progress messages "
                         "for --dump-records-b (0 disables; default 1024)")
    ap.add_argument("--quad-hist", action="store_true",
                    help="Cchr.1: with --dump-records-b, also hook the 2D "
                         "quad emitter FUN_00404efc + DrawPrimitive(UP)/"
                         "SetTexture and record every call on each dump-offset "
                         "frame to <run_dir>/quad_trace.jsonl. Buckets quad "
                         "callers by return-VA so the player/companion sprite "
                         "emitter (the bucket whose dst rect tracks the player) "
                         "is named. Use dump offsets that land in free-roam "
                         "HOUSE, ideally adjacent pairs so the player moved.")
    ap.add_argument("--chr-leaf", action="store_true",
                    help="Cchr.2b: with --dump-records-b, hook the character-"
                         "sprite leaf renderer FUN_0045a56f at ENTER (its 5 "
                         "inputs + the sheet tex dims + formdata base) and its "
                         "own DrawPrimitiveUP (the built FVF-0x142 vertex "
                         "buffer), writing one chr_leaf record per dump-offset "
                         "frame to <run_dir>/chr_leaf.jsonl. Feed leaf_in into "
                         "the port's chr_sprite_build_quads and bit-compare "
                         "against leaf_out. Use HOUSE free-roam dump offsets.")
    ap.add_argument("--rng-callers", action="store_true",
                    help="Hook the shared LCG FUN_005041f6 and tally the "
                         "immediate caller VA. Writes <run_dir>/rng_callers.json "
                         "(cumulative {ret_va: count}). Finds which subsystems "
                         "advance the RNG stream per frame — the metric for "
                         "foot-dust / particle RNG parity vs the port.")
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

    # Auto-enable call-trace when the input segtrace declares a {calltrace} op.
    # The op is the single source of truth (it also drives the port + the agent's
    # window mode), so a marked trace needs no --call-trace flag.  --no-call-trace
    # opts out.
    if (args.input_segtrace is not None and not args.call_trace
            and not args.no_call_trace):
        try:
            if '"calltrace"' in args.input_segtrace.read_text():
                args.call_trace = True
                print("[capture] auto-enabled call-trace from segtrace "
                      "calltrace op", file=sys.stderr)
        except OSError:
            pass

    call_trace_vas: list[int] | None = None
    if args.call_trace:
        # Default to the bisect-vetted safe subset.  The full
        # engine_function_vas.json contains entries that Frida hooks
        # destabilize (see tools/bisect_call_trace_vas.py).  Callers
        # who need the wider list pass it explicitly via
        # --call-trace-vas-file.
        ct_path = args.call_trace_vas_file or (
            ROOT / "tools" / "ttd" / "data" /
            "engine_function_vas_frida_safe.json")
        if not ct_path.exists():
            ap.error(f"--call-trace: VA list not found at {ct_path}; pass "
                     f"--call-trace-vas-file to override")
        raw = json.loads(ct_path.read_text())
        call_trace_vas = (raw["vas"] if isinstance(raw, dict) and "vas" in raw
                          else list(raw))

    if args.auto_z_spam and args.input_trace is not None:
        ap.error("--auto-z-spam and --input-trace are mutually exclusive")
    if args.input_segtrace is not None and (
            args.input_trace is not None or args.auto_z_spam):
        ap.error("--input-segtrace is mutually exclusive with --input-trace "
                 "and --auto-z-spam (it owns the input mask)")
    if args.auto_3d_trace and args.pre_3d_trace:
        ap.error("--auto-3d-trace and --pre-3d-trace are mutually exclusive")

    dump_records_b_offsets: list[int] | None = None
    if args.dump_records_b_offsets:
        dump_records_b_offsets = [
            int(x) for x in args.dump_records_b_offsets.split(",") if x]

    capture_at_anchor: list[dict] | None = None
    if args.capture_at_anchor:
        try:
            capture_at_anchor = [parse_anchor_spec(s)
                                 for s in args.capture_at_anchor]
        except ValueError as e:
            ap.error(str(e))

    watch: list[dict[str, Any]] | None = None
    if args.watch:
        watch = []
        for spec in args.watch:
            # NAME=0xVA[:type]
            try:
                name, rest = spec.split("=", 1)
                va_str, _, typ = rest.partition(":")
                watch.append({"name": name, "va": int(va_str, 0),
                              "type": typ or "s32"})
            except ValueError:
                ap.error(f"--watch: expected NAME=0xVA[:type], got {spec!r}")

    cfg = CaptureConfig(
        capture_frames=capture_frames,
        max_frames=args.max_frames,
        duration_ms=args.duration_ms,
        remote=args.remote, exe=args.exe, cwd=args.cwd,
        auto_start_server=not args.no_auto_start,
        server_exe=args.server_exe,
        input_trace_path=args.input_trace,
        input_segtrace_path=args.input_segtrace,
        watch=watch,
        montage=not args.no_montage,
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
        pre_3d_trace=args.pre_3d_trace,
        anchor_trace=args.anchor_trace,
        capture_at_anchor=capture_at_anchor,
        dump_records_b=args.dump_records_b,
        dump_records_b_offsets=dump_records_b_offsets,
        dump_records_b_capture=args.dump_records_b_capture,
        dump_records_b_heartbeat=args.dump_records_b_heartbeat,
        quad_hist=args.quad_hist,
        chr_leaf=args.chr_leaf,
        rng_callers=args.rng_callers,
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
