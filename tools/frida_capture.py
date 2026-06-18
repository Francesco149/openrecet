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
import trace_save  # noqa: E402 — TAS save interception (resolve {savefile})


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
    capture_all: bool = False      # capture every (strided) frame — whole-trace view
    capture_stride: int = 1        # with capture_all: every Nth frame
    capture_local: bool = True     # write raw frames straight to the (WSL-accessible)
                                   # frames dir via Win32 instead of shipping each ~3 MB
                                   # blob over the Frida channel. Required for reliability:
                                   # the per-frame readByteArray+send path backpressures
                                   # the remote channel and AVs frida-agent's memcpy/memset
                                   # on DENSE captures (frida-capture-crash.md). Same-machine
                                   # only; flip off (--no-capture-local) for a true remote host.
    suppress_loads: bool = False   # D1: drop captures while loading_active (Trace Studio
                                   # overview; mirrors port --capture-suppress-loads)
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
    # TAS save virtualization. When set, the agent redirects every
    # save.dat/_save.dat open into a per-run sandbox so the replay NEVER reads or
    # writes the user's real save. `save_ref` is the trace's resolved {savefile}:
    #   "@fresh"   → empty sandbox (game boots fresh, no LOAD GAME)
    #   <raw path> → that decompressed save is seeded into the sandbox as save.dat
    #   None       → still sandboxed (write-protection) but unseeded
    # Only used in SPAWN (replay) mode; attach/record leaves the real save alone.
    save_ref: str | None = None
    # ── Retail-side trace RECORDER (attach + observe) ──
    # When attach_match is set, attach to an ALREADY-RUNNING retail process
    # (matched by name substring, case-insensitive) instead of spawning the
    # unpacked dump. This is the only way the user can key the game by hand
    # (a Frida-spawned process can't take keyboard input). Pairs with
    # record_trace_path: the agent emits per-frame input_state + anchors
    # (now carrying gframe + rng), and the driver writes a port-format
    # `.raw.jsonl` so the captured play distils + replays deterministically
    # (distill --anchor-segments re-pins RNG per anchor) exactly like a port
    # F2 recording. Record mode disables all forcing/turbo/hide.
    attach_match: str | None = None
    record_trace_path: Path | None = None
    # DEPRECATED (the --watch CLI flag was removed 2026-06-06). Per-frame port↔
    # retail state comparison now goes through the flow-trace: declare the field
    # in tools/flow/retail_fields.json (read at a hooked VA's onEnter) and diff
    # with tools/flow_diff.py --verdict / --field-timeline. See
    # docs/flow-trace-cheatsheet.md. This field is left (defaulting None, so the
    # `if cfg.watch` emit blocks are inert) only for any in-process caller.
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
    # Show the "Fps NN" debug overlay. Default False = hidden (its value is
    # wall-clock derived → noisy cross-target diff); mirrors the port's
    # capture-default-hide so both targets match in comparisons.
    show_fps:         bool = False
    # Force back-buffer resolution. When set to (w, h), the agent
    # hooks the engine's recet.ini parse exit and overwrites the two
    # screen-size globals (DAT_005cbc04/08), so retail captures at the
    # requested dimensions even when its vendor/unpacked/recet.ini is
    # empty / has a stale `screen=` value. Default None = honor
    # whatever the engine's recet.ini lookup picks. scenario-test.py's
    # retail path defaults this to openrecet's resolution so the
    # side-by-sides line up by construction.
    force_resolution: tuple[int, int] | None = None
    # RNG seed pin. When set, the agent forces DAT_006023a0 to this value
    # right after the engine's one WinMain wall-clock reseed (FUN_005041ec) —
    # the mirror of the port's --rng-seed. Makes RNG-driven positions
    # (foot-dust, ambient motes, particle jitter) directly comparable across
    # targets instead of seed-shifted. None = leave retail's wall-clock seed.
    rng_seed: int | None = None
    # Skip-event probe: directly call FUN_0045337b (WndProc ESC skip entry)
    # once at this manual frame. -1/None = disabled. See esc-skip-event.md.
    arm_skip_at_frame: int | None = None
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
    # When set (with d3d_trace), each immediate-mode draw also carries its
    # FVF-decodable vertex bytes (vb_nverts/vb_bytes, + ib_* for indexed-UP)
    # so tools/render_diff.py --explain can name the first divergent vertex
    # field.  Mirror of the port's --d3d-trace-verts.
    d3d_trace_verts:  bool = False
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
    # Flow-trace declared payloads: {va:int -> [fieldspec]} read from
    # tools/flow/retail_fields.json. Attaches an `f:{}` payload to each matching
    # call so flow_diff.py can match the data moved. None = no payloads.
    call_trace_fields: dict | None = None
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
    # Trace Studio v3 — anchor-relative capture-proxy arm. When set to
    # {"anchor": str, "offset": int, "count": int}, the agent calls the staged
    # proxy d3d8.dll's OrV3ArmWindowAt(anchor_frame + offset, count) the FIRST
    # time the named anchor fires (config.v3_arm), so a post-load present-window
    # lands relative to a semantic event despite the nondeterministic load-stretch
    # (no fixed present-count can target a post-load frame). The retail v3 house
    # driver pairs this with the proxy's `armwait=1` cfg so nothing is kept until
    # the arm fires. Implies anchor_trace. None default ⇒ a silent no-op for every
    # v2 capture (the agent gates the export call on config.v3_arm). See
    # docs/plans/trace-studio-v3.md "HOUSE-drive integration".
    v3_arm:                 dict | None = None
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

    # RNG-consumption probe (tools/phase_probe.py). rng_count: emit a cumulative
    # LCG-call total as vals.rngcalls in each per-frame watch record (diff
    # per-frame RNG consumption port↔retail). rng_callsites: an ABSOLUTE [lo,hi)
    # frame range over which to also record the CALLER VA of every LCG step
    # (incl. periodic consumers) → <run_dir>/rng_callsites.json; the
    # who-consumed-it drill-down that reveals unported RNG consumers (e.g. the
    # missing ambient particles).
    rng_count: bool = False
    rng_callsites: int | None = None   # N frames after the {phasepin} to capture


@dataclass
class CaptureResult:
    exit_code:        int = 0
    elapsed_ms:       int = 0
    captured_frames:  list[int] = field(default_factory=list)
    last_engine_frame: int = -1


def _run_capture_impl(cfg: CaptureConfig, run_dir: Path) -> CaptureResult:
    frames_dir   = run_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)

    # Auto-enable call-trace when the input segtrace declares a {calltrace} op
    # (the op is the single source of truth — it also arms the agent's window
    # and drives the port).  The CLI main() does this too; run_capture callers
    # (scenario-test --target both) reach the engine through here instead, so
    # without this the agent armed + emitted events but f_call stayed None and
    # the retail call_trace.jsonl was never written.
    # (skip on a v3 drive: a v3 capture is d3d-only by default — the {calltrace} op
    # would auto-load the heavy ~1979-VA call-graph (120k events / ~11 MB on a HOUSE
    # window) that v3 never caches. A v3 --state drive sets call_trace=True + the 4
    # once-per-frame state VAs explicitly (house_capture), so the op is honoured there.)
    if (not cfg.call_trace and cfg.input_segtrace_path is not None
            and not getattr(cfg, "v3_arm", None)):
        try:
            if '"calltrace"' in Path(cfg.input_segtrace_path).read_text():
                cfg.call_trace = True
        except OSError:
            pass
    # ...and load the default bisect-vetted Frida-safe VA list (same default
    # the CLI uses) when call-trace is on but no explicit VAs were passed, so
    # the agent actually hooks something to emit.
    if cfg.call_trace and not cfg.call_trace_vas:
        ct_path = (ROOT / "tools" / "ttd" / "data" /
                   "engine_function_vas_frida_safe.json")
        if ct_path.exists():
            raw = json.loads(ct_path.read_text())
            cfg.call_trace_vas = (raw["vas"] if isinstance(raw, dict)
                                  and "vas" in raw else list(raw))
    # Load the flow-trace declared-payload spec (default; opt out with
    # --no-call-trace-fields). Flatten {va_str -> {fields:[...]}} to
    # {va_int -> [fields]} for the agent, and ensure the spec'd VAs are hooked.
    if cfg.call_trace and cfg.call_trace_fields is None:
        ff_path = ROOT / "tools" / "flow" / "retail_fields.json"
        if ff_path.exists():
            data = json.loads(ff_path.read_text()).get("fields", {})
            flat = {str(int(va, 0) if isinstance(va, str) else int(va)):
                    entry["fields"]
                    for va, entry in data.items() if "fields" in entry}
            cfg.call_trace_fields = flat
            # A field-spec VA must be hooked to read its payload — add any
            # missing ones to the VA list.
            if cfg.call_trace_vas is None:
                cfg.call_trace_vas = []
            have = set(cfg.call_trace_vas)
            for va_s in flat:
                if int(va_s) not in have:
                    cfg.call_trace_vas.append(int(va_s))

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
    # v3_arm implies anchor_trace on the agent — and the v3 cache needs the
    # full anchor stream for per-frame (multi-anchor) identity, so record it.
    f_anchor = (anchors_jsonl.open("w", buffering=1)
                if (cfg.anchor_trace or cfg.capture_at_anchor
                    or getattr(cfg, "v3_arm", None)) else None)
    watch_jsonl = run_dir / "watch.jsonl"
    f_watch = (watch_jsonl.open("w", buffering=1) if cfg.watch else None)
    # frames_meta.jsonl: per-screenshot capture-time sim-state (db054/aframe/…),
    # sampled at Present onEnter atomically with the pixels. Turbo-robust frame↔
    # state alignment (vs the per-tick watch.jsonl). Written only when --watch is on.
    frames_meta_jsonl = run_dir / "frames_meta.jsonl"
    f_frames_meta = (frames_meta_jsonl.open("w", buffering=1) if cfg.watch else None)

    captured: list[int] = []
    last_mask: int | None = None
    last_engine_frame = -1
    done = threading.Event()
    # Recorder buffers (record_trace_path set): DENSE per-frame input masks and
    # the anchor firings (with gframe + rng), assembled into a port-format raw
    # at finalize. Dense because distill_trace.load_raw fills missing frames
    # with 0x0000 (so a held button would be lost if we only logged changes).
    rec_inputs: dict[int, int] = {}
    rec_anchors: list[dict[str, int]] = []
    rec_escs: list[int] = []
    rec_saves: list[dict[str, Any]] = []   # save_capture events (boot + writes)
    recording = cfg.record_trace_path is not None

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
            # Capture-time sim-state label (Present onEnter, post-render) — the
            # screenshot's OWN db054/aframe/etc., atomic with the pixels. Align
            # screenshots by THIS, not the per-tick watch.jsonl (turbo-robust).
            cvals = p.get("vals")
            if cvals is not None and f_frames_meta is not None:
                f_frames_meta.write(json.dumps({"frame": frame, "vals": cvals}) + "\n")
            return

        if kind == "frame_file":
            # Same-machine fast path: the agent already wrote the raw BGRX blob to
            # <frames_dir>/<file>. Just record the frame (no transfer); the .raw is
            # converted to PNG after the run.
            frame = int(p["frame"])
            captured.append(frame)
            last_engine_frame = max(last_engine_frame, frame)
            f_log.write(f"[frame_file] {p.get('file')} {p.get('w')}x{p.get('h')}\n")
            cvals = p.get("vals")
            if cvals is not None and f_frames_meta is not None:
                f_frames_meta.write(json.dumps({"frame": frame, "vals": cvals}) + "\n")
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
            rec = {
                "t_ms":  int(p["t_ms"]),
                "kind":  "se_play",
                "frame": int(p["frame"]),
                "slot":  int(p["slot"]),
            }
            nm = p.get("name")          # se_NNN_idXXXX (resource) or path (file SE)
            if nm is not None:
                rec["name"] = nm
            if p.get("ret_va") is not None:   # immediate caller (module-rel VA)
                rec["ret_va"] = int(p["ret_va"])
            f_audio.write(json.dumps(rec) + "\n")
            return

        if kind == "input_state":
            frame = int(p["frame"])
            mask  = int(p["buttons"])
            last_engine_frame = max(last_engine_frame, frame)
            if recording:
                rec_inputs[frame] = mask            # DENSE — every frame
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

        if kind == "save_capture":
            # The agent shipped an 18 MB save-arena snapshot ('boot' = initial,
            # 'write' = an in-session save). Write it next to the raw recording
            # and remember it; finalize emits the {savefile}/{save_write} rows so
            # distill folds it like a port F2 recording.
            if not recording:
                return
            which = str(p.get("which", "write"))
            idx   = int(p.get("index", len(rec_saves)))
            size  = int(p.get("size", 0))
            if data is None or len(data) != size:
                f_log.write(f"[save_capture] BAD payload which={which} "
                            f"size={size} got={0 if data is None else len(data)}\n")
                return
            import hashlib
            sha = hashlib.sha256(data).hexdigest()
            base = cfg.record_trace_path.stem.replace(".raw", "")
            fname = (f"{base}.save.bin" if which == "boot"
                     else f"{base}-recsave-{idx}.bin")
            # NEVER clobber a different save under the same name (a re-record
            # with the same session name overwrote the previous boot save —
            # bit us twice on item-display-2): identical content reuses the
            # file; different content gets a `-N` suffixed name.
            target = cfg.record_trace_path.parent / fname
            n = 1
            reuse = False
            while target.exists():
                if hashlib.sha256(target.read_bytes()).hexdigest() == sha:
                    reuse = True                # same content — reuse as-is
                    break
                n += 1
                target = target.with_name(
                    f"{base}.save-{n}.bin" if which == "boot"
                    else f"{base}-recsave-{idx}-{n}.bin")
            if not reuse:
                target.write_bytes(data)
            fname = target.name
            rec_saves.append({"which": which, "index": idx,
                              "frame": int(p.get("frame", 0)),
                              "file": fname, "sha256": sha, "size": size})
            f_log.write(f"[save_capture] {which} #{idx} @frame={p.get('frame')} "
                        f"sha={sha[:12]} → {fname}\n")
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
            if recording:
                rec_anchors.append({
                    "anchor": name, "frame": frame,
                    "gframe": int(p.get("gframe", 0)),
                    "rng":    int(p.get("rng", 0)) & 0xffffffff,
                })
            f_log.write(f"[anchor] {name} @ frame={frame}"
                        + (f" gframe={p.get('gframe')} rng={p.get('rng')}"
                           if recording else "") + "\n")
            return

        if kind == "esc_record":
            if recording:
                rec_escs.append(int(p.get("frame", -1)))
                f_log.write(f"[esc_record] frame={p.get('frame')}\n")
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

        if kind == "rng_callsites":
            # Per-frame caller histograms over the armed [lo,hi) range — the
            # who-consumed-RNG drill-down. {frames: {frame: {va: count}}}.
            frames = p.get("frames") or {}
            try:
                (run_dir / "rng_callsites.json").write_text(json.dumps(
                    {"lo": p.get("lo"), "hi": p.get("hi"), "frames": frames},
                    indent=2))
            except Exception:
                pass
            f_log.write(f"[rng_callsites] frames [{p.get('lo')},{p.get('hi')}) "
                        f"captured={len(frames)} frame-buckets\n")
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

    # ── attach to a running retail (recorder) OR spawn the unpacked dump ──
    is_attach = cfg.attach_match is not None
    if is_attach:
        needle = cfg.attach_match.lower()
        procs = device.enumerate_processes()
        matches = [pr for pr in procs if needle in pr.name.lower()]
        if not matches:
            names = ", ".join(sorted({pr.name for pr in procs
                                      if "rec" in pr.name.lower()})) or "(none)"
            msg = (f"\nfrida_capture --attach: no running process matching "
                   f"{cfg.attach_match!r} at {cfg.remote}.\n"
                   f"  Launch Recettear (Steam) first, then re-run.\n"
                   f"  candidate 'rec*' processes seen: {names}\n")
            f_log.write(msg); f_log.close(); f_audio.close(); f_trace.close()
            raise SystemExit(msg)
        proc = matches[0]
        pid = proc.pid
        f_log.write(f"[attach] {proc.name} pid={pid} (match {cfg.attach_match!r})\n")
        session = device.attach(pid)
    else:
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
            if "buttons" not in rec:
                continue   # skip non-input ops (e.g. trace-global {savefile})
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
            elif "rngcs" in rec:
                # {rngcs:N} or {rngcs:[start,len]} — arm the rng-callsites drill
                # anchor-relative WITHOUT a phasepin (clean measurement). Needs
                # --rng-callsites N on the CLI to install the LCG hooks.
                rc = rec["rngcs"]
                segtrace_ops.append({"rngcs": (
                    [int(rc[0]), int(rc[1])] if isinstance(rc, list) else int(rc))})
            elif "rngseed" in rec:
                # Force the engine LCG (DAT_006023a0) to value at base+frame —
                # mirrors the port's rng_seed() so both targets share one RNG
                # stream from the anchor (cross-target parity for the recorded
                # segment). Array form [frame, value] only.
                rs = rec["rngseed"]
                segtrace_ops.append({"rngseed": [int(rs[0]),
                                                 int(rs[1]) & 0xffffffff]})
            elif "caprange" in rec:
                # {caprange:[start,count]} — contiguous capture window
                # [base+start, base+start+count). The agent expands it into its
                # capture set on segment enter (same window export_trace renders
                # on the port), so one exported trace.work.jsonl captures the
                # SAME anchor-relative frames on both targets.
                cr = rec["caprange"]
                segtrace_ops.append({"caprange": [int(cr[0]), int(cr[1])]})
            elif "capstride" in rec:
                # {capstride:N} (D3) — trace-global two-tier capture cadence:
                # forward it so the agent thins each {caprange} to every Nth frame
                # from its start (a coarse OVERVIEW), striding identically to the
                # port → both targets keep the same anchor-relative kept-set.
                segtrace_ops.append({"capstride": int(rec["capstride"])})
            elif "tutloadpin" in rec:
                # {tutloadpin:N} — trace-global: extend every tutorial-dialogue
                # load bracket to N frames (the agent holds the load gate
                # DAT_06a49960 high until N frames past the arm; EXTEND-only).
                # Mirrors the port's IVE_TUT_LOAD_FRAMES override so both sides
                # idle equal-length brackets (engine-quirks §119).
                segtrace_ops.append({"tutloadpin": int(rec["tutloadpin"])})
            elif "esc" in rec:
                # {esc:N} — synthesise an ESC keypress at base+N (dialogue-skip
                # replay), mirroring the port's {esc} op.
                segtrace_ops.append({"esc": int(rec["esc"])})
            elif "poke" in rec:
                # {poke:[frame, va, val]} — STICKY u32 write: hold global `va` at
                # `val` every frame from base+frame on (e.g. enable the debug-
                # overlay gate DAT_06a49938=1). Retail-only; no port mirror.
                pk = rec["poke"]
                segtrace_ops.append({"poke": [int(pk[0]),
                                              int(pk[1]) & 0xffffffff,
                                              int(pk[2]) & 0xffffffff]})
            elif "phasepin" in rec:
                # {phasepin:N} — at base+N, zero the companion's load-dependent
                # free-roam phase (db054 + anim cycle) so a port↔retail trace
                # comparison is phase-clean (engine-quirks §94). Mirrors the
                # port's {phasepin} op.
                segtrace_ops.append({"phasepin": int(rec["phasepin"])})
            elif "memsnap" in rec:
                # {memsnap:N} — at base+N, dump the retail exe's writable
                # sections (.data/.data1, VirtualSize) to the capture dir —
                # the phase-state census input (tools/phase_census.py).
                # Mirrors the port's {memsnap} op; the regions are computed
                # below from the unpacked exe's section table → init_cfg.
                segtrace_ops.append({"memsnap": int(rec["memsnap"])})
            elif "savefile" in rec:
                # {savefile:"<relpath>"} — trace-global embedded-save ref. The save
                # override is harness-driven (the port gets --save-override; a retail
                # redirect is a TODO), so it's not a per-frame segtrace op — skip it
                # here. (Without this it'd fall into the input-entry else and KeyError
                # on the absent "buttons".)
                continue
            else:
                mask_val = rec["buttons"]
                mask = int(mask_val, 16) if isinstance(mask_val, str) else int(mask_val)
                segtrace_ops.append({"frame": int(rec["frame"]), "mask": mask})
        f_log.write(f"[input] loaded {len(segtrace_ops)} segtrace ops from "
                    f"{cfg.input_segtrace_path}\n")
        if cfg.v3_arm:
            # A v3 retail drive's capture window is armed by PRESENT-COUNT
            # (OrV3ArmWindowAt at the anchor), wholly independent of the v2
            # {caprange}. On a v3 drive the caprange's ONLY effect is to make the
            # agent read back + write v2 frames that armwait already drops from the
            # v3 container — pure waste: thousands of GetBackBuffer readbacks + raw
            # writes + the post-run PNG/montage bake (~5 of ~13 min on a guild drive).
            # Strip caprange/capstride so a v3 drive's ONLY artifacts are the v3
            # container + hash refs. Gated on v3_arm ⇒ NO effect on v2 captures, and
            # none on the PORT path (port_capture goes through scenario-test, not
            # run_capture(v3_arm=…), and relies on GetBackBuffer AS its keep-trigger).
            # ALSO strip {calltrace} UNLESS --state (cfg.call_trace): without state the
            # op would only re-load the heavy uncached call-graph; WITH --state we KEEP
            # it, because it window-arms the (state-only) call-trace to the kept frames
            # so the probes don't flood the pre-window load-stretch.
            n_before = len(segtrace_ops)
            segtrace_ops = [o for o in segtrace_ops
                            if not ("caprange" in o or "capstride" in o
                                    or ("calltrace" in o and not cfg.call_trace))]
            f_log.write(f"[v3] dropped {n_before - len(segtrace_ops)} v2 caprange/"
                        f"capstride{'' if cfg.call_trace else '/calltrace'} op(s) — v3_arm "
                        f"set ({'state call-trace kept' if cfg.call_trace else 'd3d-only'}; "
                        f"v2 readbacks + bake are pure waste on a v3 drive)\n")

    # TAS save virtualization (spawn/replay only): create a per-run sandbox and
    # tell the agent to redirect all save.dat/_save.dat I/O into it, so the replay
    # never touches the user's real save. Seed it from the trace's {savefile}:
    #   "@fresh" / None → empty sandbox (game boots fresh; writes land here)
    #   <raw path>      → seed that save as <sandbox>/save.dat (a "continue" trace)
    import shutil
    save_sandbox_win = None
    capture_saves = False
    sandbox = run_dir / "saveout"
    sandbox.mkdir(parents=True, exist_ok=True)
    if cfg.attach_match is None:
        # SPAWN / REPLAY: sandbox seeded from the trace's {savefile}. The replay
        # never touches the real save.
        #   "@fresh" / None → empty sandbox (game boots fresh; writes land here)
        #   <raw path>      → seed that save as <sandbox>/save.dat (a "continue" trace)
        if cfg.save_ref and cfg.save_ref != "@fresh":
            seed = Path(cfg.save_ref)
            if seed.exists():
                shutil.copyfile(seed, sandbox / "save.dat")
                shutil.copyfile(seed, sandbox / "_save.dat")
                f_log.write(f"[save] seeded sandbox from {seed}\n")
            else:
                f_log.write(f"[save] WARNING seed save missing: {seed}\n")
        else:
            f_log.write("[save] @fresh — empty sandbox (game boots fresh)\n")
        save_sandbox_win = wslpath_w(sandbox)
        f_log.write(f"[save] sandbox → {save_sandbox_win} (real save protected)\n")
    elif recording:
        # ATTACH / RECORD: protect the recording too — seed the sandbox with a
        # COPY of the user's real save (so in-game Continue/Load read it) and
        # redirect all writes there, so live play during the recording does NOT
        # alter the real save. Also capture the save state (boot + each write) so
        # the recorded trace carries its saves for the port to replay against.
        capture_saves = True
        for nm in ("save.dat", "_save.dat"):
            real = cfg.cwd / nm
            if real.exists():
                shutil.copyfile(real, sandbox / nm)
        save_sandbox_win = wslpath_w(sandbox)
        f_log.write(f"[save] RECORD sandbox (seeded from real save) → "
                    f"{save_sandbox_win}; real save protected; capture_saves on\n")

    t0 = time.monotonic()
    init_cfg: dict[str, Any] = {
        "capture_frames": list(cfg.capture_frames),
        "capture_all":    bool(cfg.capture_all),
        "capture_stride": int(cfg.capture_stride),
        "suppress_loads": bool(cfg.suppress_loads),
        # Capture-local: the agent writes raw frames straight to the
        # (WSL-accessible) frames dir via Win32 WriteFile instead of shipping each
        # ~3 MB blob over the Frida channel. Applies to ALL capture paths now
        # (capture_all AND windowed caprange/pending/anchor) — the per-frame
        # readByteArray+send path backpressures the remote channel and AVs
        # frida-agent's memcpy/memset on dense captures (see
        # docs/findings/frida-capture-crash.md). Frames + their capture-time
        # watch vals reach Python identically via the 'frame_file' message.
        "capture_dir":    (wslpath_w(frames_dir) if cfg.capture_local else ""),
        "max_frames":     cfg.max_frames,
        "input_trace":    trace_entries,
        "force_input":    bool(cfg.force_input),
        "hide_window":    bool(cfg.hide_window),
        "turbo":          bool(cfg.turbo),
        "turbo_step_ms":  int(cfg.turbo_step_ms),
        "silent_audio":   bool(cfg.silent_audio),
        "show_fps":       bool(cfg.show_fps),
    }
    if save_sandbox_win is not None:
        init_cfg["save_sandbox"] = save_sandbox_win
    if capture_saves:
        init_cfg["capture_saves"] = True
    if cfg.arm_skip_at_frame is not None:
        init_cfg["arm_skip_at_frame"] = int(cfg.arm_skip_at_frame)
    if cfg.force_resolution is not None:
        init_cfg["force_resolution"] = [int(cfg.force_resolution[0]),
                                        int(cfg.force_resolution[1])]
    if cfg.rng_seed is not None:
        init_cfg["rng_seed"] = int(cfg.rng_seed) & 0xffffffff
    if cfg.d3d_trace:
        init_cfg["d3d_trace"] = True
        if cfg.d3d_trace_verts:
            init_cfg["d3d_trace_verts"] = True
        if cfg.d3d_trace_frames is not None:
            init_cfg["d3d_trace_frames"] = [int(f) for f in cfg.d3d_trace_frames]
    if cfg.call_trace:
        init_cfg["call_trace"] = True
        init_cfg["call_trace_vas"] = [int(v) for v in (cfg.call_trace_vas or [])]
        if cfg.call_trace_frames is not None:
            init_cfg["call_trace_frames"] = [int(f) for f in cfg.call_trace_frames]
        if cfg.call_trace_fields:
            init_cfg["call_trace_fields"] = cfg.call_trace_fields
    if cfg.auto_z_spam:
        init_cfg["auto_z_spam"] = True
    if cfg.auto_3d_trace:
        init_cfg["auto_3d_trace"] = True
        init_cfg["auto_3d_trace_frames"] = int(cfg.auto_3d_trace_frames)
    if cfg.pre_3d_trace:
        init_cfg["pre_3d_trace"] = True
    if cfg.anchor_trace:
        init_cfg["anchor_trace"] = True
    if cfg.v3_arm:
        # Studio v3: the agent arms the staged proxy's OrV3ArmWindowAt on this
        # anchor (anchor-relative present-window). Implies the anchor poll.
        init_cfg["v3_arm"] = cfg.v3_arm
        init_cfg["anchor_trace"] = True
    if segtrace_ops:
        # Anchor-segmented forcing owns the input mask and needs the anchor
        # poll for its `wait` ops; the agent forces anchor_trace on too.
        init_cfg["input_segtrace"] = segtrace_ops
        init_cfg["anchor_trace"] = True
        if any("memsnap" in o for o in segtrace_ops):
            # Phase-census {memsnap}: the agent dumps these absolute-VA regions
            # (the exe's WRITABLE sections — .data/.data1 by name; VirtualSize,
            # so the BSS zero-fill arrays where the DAT_ globals live are
            # included) straight to capture_dir via Win32 writes — never over
            # the Frida channel. Requires capture_local.
            sys.path.insert(0, str(ROOT / "tools" / "analyze"))
            from pe import PE                                    # noqa: E402
            secs = [s for s in PE().sections if s.name in (".data", ".data1")]
            if not secs:
                raise SystemExit("frida_capture: {memsnap} op but no writable "
                                 "sections found in the unpacked exe")
            init_cfg["memsnap_regions"] = [[0x400000 + s.vaddr, s.vsize]
                                           for s in secs]
            if not init_cfg.get("capture_dir"):
                raise SystemExit("frida_capture: {memsnap} needs capture_local "
                                 "(the dump writes via Win32, not the channel)")
            f_log.write(f"[memsnap] regions: "
                        f"{[(s.name, hex(0x400000 + s.vaddr), s.vsize) for s in secs]}\n")
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
    if cfg.rng_count:
        init_cfg["rng_count"] = True
    if cfg.rng_callsites:
        init_cfg["rng_callsites"] = int(cfg.rng_callsites)
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
    if recording:
        # Observe-only: the user drives the live game by hand, so disable ALL
        # forcing/turbo/hide and just emit input_state + anchors. anchor_trace
        # is forced on so we capture the {anchor,gframe,rng} rows the raw needs.
        init_cfg["anchor_trace"]  = True
        init_cfg["record_esc"]    = True        # capture WndProc ESC-skip presses
        init_cfg["force_input"]   = False
        init_cfg["turbo"]         = False
        init_cfg["hide_window"]   = False
        init_cfg.pop("input_segtrace", None)
        init_cfg.pop("input_trace", None)
        init_cfg["capture_frames"] = []
        init_cfg["max_frames"]     = 0          # 0 = no auto-shutdown
        f_log.write("[record] observe-only mode — drive the game by hand; "
                    "close Recettear (or Ctrl-C) to finish the recording\n")

    script.exports_sync.init(init_cfg)
    if not is_attach:
        device.resume(pid)        # attach: the process is already running

    # ── wait for max_frames signal or wall-clock ceiling ──
    deadline = t0 + (cfg.duration_ms / 1000.0)
    try:
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
    except KeyboardInterrupt:
        # Ctrl-C: fall through to the shutdown + (recorder) raw write below so a
        # hand-driven recording is still saved instead of lost.
        f_log.write("[interrupt] Ctrl-C — finishing + writing any recording\n")
        print("\nfrida_capture: interrupted — finishing up…", file=sys.stderr)

    elapsed_ms = int((time.monotonic() - t0) * 1000)
    exit_code = 0

    # ── shut the target down ──
    # Every frida teardown call is BOUNDED: session.detach() (and potentially
    # script.unload()) can block forever when the remote session is in a bad
    # state — seen 2026-06-10 on Interceptor/CModule-hooked captures, where the
    # hang wedged the whole studio pipeline in a silent join. By this point all
    # capture data is on disk, so an abandoned teardown step (leaked session /
    # stray process; tools/kill_retail.py reaps those) beats a hung pipeline.
    def _bounded(tag: str, fn, timeout_s: float = 15.0) -> None:
        def _run() -> None:
            try:
                fn()
            except Exception as e:                      # noqa: BLE001
                f_log.write(f"[shutdown] {tag}: {e}\n")
        th = threading.Thread(target=_run, daemon=True, name=f"shutdown-{tag}")
        th.start()
        th.join(timeout_s)
        if th.is_alive():
            f_log.write(f"[shutdown] {tag}: still blocked after {timeout_s}s "
                        f"— abandoned (daemon)\n")

    # Spawn mode: kill the engine FIRST — teardown against a dead process is
    # trivially fast, while unload/detach against a live hooked process is
    # where the hangs live. Attach mode (user's own game) never kills; it
    # relies on the bounded steps alone.
    if not is_attach:
        kill_ok = threading.Event()
        def _kill() -> None:
            device.kill(pid)
            kill_ok.set()
        _bounded("kill", _kill)
        if not kill_ok.is_set():
            # A live stray retail holds the singleton mutex and stalls the
            # NEXT capture — keep the old nonzero-rc signal for that case.
            exit_code = 1
    _bounded("script unload", script.unload)
    _bounded("session detach", session.detach)
    if is_attach:
        f_log.write(f"[shutdown] attach mode — leaving pid={pid} alive\n")

    # ── write the recorded raw trace (port-format .raw.jsonl) ──
    if recording and rec_inputs:
        out = cfg.record_trace_path
        n = max(rec_inputs) + 1
        seed = next((a["rng"] for a in sorted(rec_anchors, key=lambda a: a["frame"])),
                    None)
        lines = [json.dumps({"_rec": "openrecet-tas-raw-v1", "frames": n,
                             "start_abs": 0, "rng_seed_at_start": seed})]
        # DENSE per-frame input rows (sticky-fill gaps so a held button that the
        # agent only re-emitted on change is still dense; defensive — the agent
        # emits every frame, but a dropped wire message would otherwise read 0).
        sticky = 0
        for i in range(n):
            sticky = rec_inputs.get(i, sticky)
            lines.append(json.dumps({"frame": i, "buttons": f"0x{sticky:04x}"}))
        for a in rec_anchors:
            lines.append(json.dumps({"anchor": a["anchor"], "frame": a["frame"],
                                     "gframe": a["gframe"], "rng": a["rng"]}))
        for ef in rec_escs:                       # WndProc ESC-skip presses
            lines.append(json.dumps({"esc": ef}))
        # Save captures: the boot save → {savefile} row (the trace's initial save),
        # each in-session write → {save_write} row. Same raw format the port F2
        # recorder emits, so distill_trace.py folds them unchanged.
        n_saves = 0
        for sv in rec_saves:
            if sv["which"] == "boot":
                lines.append(json.dumps({"savefile": sv["file"],
                                         "sha256": sv["sha256"], "size": sv["size"]}))
            else:
                lines.append(json.dumps({"save_write": {
                    "index": sv["index"], "frame": sv["frame"],
                    "file": sv["file"], "sha256": sv["sha256"], "size": sv["size"]}}))
            n_saves += 1
        out.write_text("\n".join(lines) + "\n")
        f_log.write(f"[record] wrote {n} frames + {len(rec_anchors)} anchors + "
                    f"{len(rec_escs)} esc + {n_saves} save(s) → {out}\n")
        print(f"frida_capture: recorded {n} frames + {len(rec_anchors)} anchors + "
              f"{len(rec_escs)} esc + {n_saves} save(s) → {out}", file=sys.stderr)

    f_audio.close(); f_trace.close()
    # NB: f_log stays open here — the raw->png conversion below still logs to it
    # (it was being closed too early, raising "I/O operation on closed file").
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
    if f_frames_meta is not None:
        f_frames_meta.close()

    # Whole-trace capture: the agent wrote raw BGRX frames straight to disk
    # (frame_NNNNN_WxH.raw). Convert them to PNG here (same BGRX→PNG path the
    # 'frame' message uses), then drop the .raw. Done in frame order.
    raws = sorted(frames_dir.glob("frame_*_*x*.raw"),
                  key=lambda p: int(p.name.split("_")[1]))
    if raws:
        import re as _re
        n_conv = 0
        for rp in raws:
            m = _re.match(r"frame_(\d+)_(\d+)x(\d+)\.raw", rp.name)
            if not m:
                continue
            fr, w, h = int(m.group(1)), int(m.group(2)), int(m.group(3))
            data = rp.read_bytes()
            if len(data) != w * h * 4:
                f_log.write(f"[raw->png] {rp.name} bad size {len(data)} "
                            f"(expect {w*h*4})\n")
                continue
            frame_io.write_frame_png(frames_dir / f"frame_{fr:05d}.png", w, h, data)
            rp.unlink()
            n_conv += 1
        f_log.write(f"[raw->png] converted {n_conv} raw frame(s) → PNG\n")

    f_log.close()

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


def _drop_frame0(frames) -> list[int]:
    """Call-trace frame list minus frame 0 (the retail boot transient), unless
    0 is the only frame (then keep it so the trace isn't empty)."""
    fl = [int(f) for f in frames]
    rest = [f for f in fl if f != 0]
    return rest if rest else fl


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
                show_fps: bool = False,
                force_resolution: tuple[int, int] | None = None,
                rng_seed: int | None = None,
                save_ref: str | None = None,
                d3d_trace: bool = False,
                d3d_trace_verts: bool = False,
                call_trace: bool = False,
                call_trace_vas: list | None = None,
                call_trace_fields: dict | None = None,
                suppress_loads: bool = False,
                capture_local: bool = True,
                anchor_trace: bool = False,
                v3_arm: dict | None = None) -> dict:
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
        show_fps=show_fps,
        force_resolution=force_resolution,
        # Default to the scenario's own seed (the port pins the same value via
        # --rng-seed) so comparisons share one LCG stream unless overridden.
        rng_seed=(rng_seed if rng_seed is not None
                  else getattr(scenario, "rng_seed", None)),
        save_ref=save_ref,
        capture_local=capture_local,     # write frames to disk, not over the channel
        suppress_loads=suppress_loads,   # D1 load-suppression (Trace Studio overview)
        # Trace the captured frames (aligned port↔retail) when enabled; the
        # call_trace_fields spec auto-loads in the core runner.
        d3d_trace=d3d_trace,
        d3d_trace_verts=d3d_trace_verts,
        d3d_trace_frames=(list(scenario.capture_frames)
                          if d3d_trace and scenario.capture_frames else None),
        call_trace=call_trace,
        # Explicit VA/field subset (studio-v3 --state: just the once-per-frame
        # state probes). None ⇒ the core runner auto-loads the full bisect-vetted
        # set + every retail_fields VA (the heavy call-graph). Passing both a VA
        # list AND a non-None field spec skips that auto-load entirely.
        call_trace_vas=([int(v) for v in call_trace_vas] if call_trace_vas else None),
        call_trace_fields=call_trace_fields,
        # Drop frame 0 from the call-trace: on retail every pre-first-Present
        # boot call (CRT/MFC + engine init, 1979 hooks) is buffered and flushed
        # into frame 0 — millions of events that are the boot transient, never a
        # useful comparison frame. d3d frame 0 (the first rendered title frame)
        # is unaffected and kept.
        call_trace_frames=(_drop_frame0(scenario.capture_frames)
                           if call_trace and scenario.capture_frames else None),
        anchor_trace=anchor_trace,   # → run_dir/anchors.jsonl (studio timeline)
        v3_arm=v3_arm,               # studio-v3 anchor-relative proxy arm (None = no-op)
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
    ap.add_argument("--capture-all", action="store_true",
                    help="capture the WHOLE run (every Nth frame, see "
                         "--capture-stride) — for verifying a full trace replay")
    ap.add_argument("--capture-stride", type=int, default=1,
                    help="with --capture-all, capture every Nth frame (keeps the "
                         "~3 MB/frame transfer feasible over remote Frida)")
    ap.add_argument("--no-capture-local", dest="capture_local", action="store_false",
                    help="ship each frame's pixels over the Frida channel instead of "
                         "writing them to the (WSL-accessible) frames dir via Win32. "
                         "The in-band path AVs frida-agent on dense captures "
                         "(frida-capture-crash.md); only use for a true remote host "
                         "where the agent can't see the WSL frames dir.")
    ap.set_defaults(capture_local=True)
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
    ap.add_argument("--attach", nargs="?", const="recettear", default=None,
                    metavar="NAME",
                    help="attach to an ALREADY-RUNNING retail process (match by "
                         "name substring, default 'recettear') instead of spawning "
                         "the unpacked dump. The only way to drive the game by hand "
                         "(Frida-spawned processes can't take keyboard input). "
                         "Pair with --record-trace.")
    ap.add_argument("--record-trace", type=Path, default=None, metavar="OUT.raw.jsonl",
                    help="RECORD mode: observe the live game (no forcing/turbo/hide) "
                         "and write a port-format raw recording (dense per-frame "
                         "inputs + {anchor,gframe,rng} rows) to OUT. Drive the game "
                         "by hand, then close it (or Ctrl-C) to finish. Distil with "
                         "`distill_trace.py OUT --anchor-segments` for deterministic "
                         "replay. Implies --attach if no spawn target makes sense.")
    ap.add_argument("--force-input", action="store_true",
                    help="overwrite the engine's input mask each frame "
                         "with the trace value (or 0 if no trace given)")
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
    ap.add_argument("--show-fps", action="store_true",
                    help="show the bottom-right 'Fps NN' debug overlay. "
                         "Default hidden (NOP FUN_004523e6): its value is "
                         "wall-clock derived so it's a noisy cross-target / "
                         "golden delta. Mirrors the port's --show-fps.")
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
    ap.add_argument("--d3d-trace-verts", action="store_true",
                    help="with --d3d-trace, also capture each immediate-mode "
                         "draw's FVF-decodable vertex bytes (vb_nverts/"
                         "vb_bytes, + ib_* for indexed-UP) so "
                         "tools/render_diff.py --explain can name the first "
                         "divergent vertex field. Mirror of the port's "
                         "--d3d-trace-verts.")
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
    ap.add_argument("--rng-count", action="store_true",
                    help="Emit a cumulative LCG-call total as vals.rngcalls in "
                         "each per-frame watch record (RNG-consumption diff).")
    ap.add_argument("--rng-callsites", type=int, metavar="N",
                    help="record the caller VA of every LCG step for N frames "
                         "AFTER the {phasepin} fire → rng_callsites.json "
                         "(who-consumed-RNG drill-down; catches periodic callers).")
    ap.add_argument("--arm-skip-at-frame", type=int, default=None,
                    help="Directly call FUN_0045337b (the WndProc ESC skip-event "
                         "entry) once at this manual frame. Probes prologue "
                         "skippability + the skip-prompt counter choreography, "
                         "since the skip is keyboard-ESC-only (not DInput).")
    ap.add_argument("--rng-seed", type=lambda s: int(s, 0), default=None,
                    help="Pin DAT_006023a0 to this value right after the "
                         "engine's WinMain reseed (FUN_005041ec) — the mirror "
                         "of openrecet's --rng-seed. Makes RNG-driven positions "
                         "(foot-dust, motes, particles) comparable across "
                         "targets. Omit to keep retail's wall-clock seed.")
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

    # --record-trace implies attach (you can't drive a spawned process by hand).
    attach_match = args.attach
    if args.record_trace is not None and attach_match is None:
        attach_match = "recettear"

    cfg = CaptureConfig(
        capture_frames=capture_frames,
        capture_all=args.capture_all,
        capture_stride=args.capture_stride,
        capture_local=args.capture_local,
        max_frames=args.max_frames,
        duration_ms=(600_000 if (args.record_trace is not None
                                  and args.duration_ms == 30_000)
                     else args.duration_ms),
        remote=args.remote, exe=args.exe, cwd=args.cwd,
        auto_start_server=not args.no_auto_start,
        server_exe=args.server_exe,
        input_trace_path=args.input_trace,
        input_segtrace_path=args.input_segtrace,
        # Seed the replay sandbox from the trace's {savefile} op (so a recorded
        # "continue" trace replays from its embedded save; @fresh boots fresh).
        save_ref=(trace_save.resolve_save(args.input_segtrace)
                  if (args.input_segtrace and args.record_trace is None) else None),
        attach_match=attach_match,
        record_trace_path=args.record_trace,
        montage=not args.no_montage,
        force_input=args.force_input or args.input_trace is not None,
        hide_window=args.hide_window,
        turbo=args.turbo, turbo_step_ms=args.turbo_step_ms,
        silent_audio=args.silent_audio,
        show_fps=args.show_fps,
        force_resolution=fr_tuple,
        rng_seed=args.rng_seed,
        arm_skip_at_frame=args.arm_skip_at_frame,
        d3d_trace=args.d3d_trace,
        d3d_trace_verts=args.d3d_trace_verts,
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
        rng_count=args.rng_count,
        rng_callsites=args.rng_callsites,
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
