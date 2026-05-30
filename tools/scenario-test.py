#!/usr/bin/env python3
"""
tools/scenario-test.py — Phase A + B regression harness.

Drives a target binary through a deterministic input trace (Phase A,
--target openrecet) or instruments the retail unpacked exe via Frida
(Phase B, --target retail) and lays the captured frames + audio trace
down next to a per-scenario golden directory.

Layout:

    tests/scenarios/<name>/
        scenario.yaml         # capture_frames, max_frames, rng_seed, etc.
        trace.jsonl           # sparse input trace (input_trace.h format)
        golden/               # --target openrecet golden frames + audio
            frame_00000.bmp
            frame_00030.bmp
            ...
            audio.jsonl       # optional — golden audio-event log
        golden-retail/        # --target retail golden, populated by --bless
            frame_NNNNN.bmp   #   BMPs are NOT bit-comparable across targets
            audio.jsonl
            trace.jsonl       # recorded input mask per engine frame

Usage:
    scenario-test.py                          # run all scenarios, target openrecet
    scenario-test.py boot-idle                # single scenario, target openrecet
    scenario-test.py boot-idle --bless        # regenerate openrecet goldens
    scenario-test.py boot-idle --target retail --bless
                                              # regenerate retail goldens via Frida
    scenario-test.py boot-idle --target both  # run openrecet + retail back-to-back,
                                              # diff each against its own golden, AND
                                              # drop a ours|retail side-by-side PNG

Exit code: 0 on all-pass, 1 on any frame mismatch. --bless always 0.

Pixel diff (within a target): bit-exact (per the Phase A decision
documented in docs/harness-roadmap.md). Any mismatched frame additionally
emits a red-tinted overlay PNG so the change is multimodally inspectable.

Cross-target diff (retail vs openrecet) is NOT bit-exact and is left to
the auto side-by-side contact sheet + audio.jsonl comparisons. --target both
exists to make that comparison one-command instead of two-runs-plus-tooling.
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import yaml


ROOT       = Path(__file__).resolve().parent.parent
SCENARIOS  = ROOT / "tests" / "scenarios"
BUILD_EXE  = ROOT / "build" / "openrecet.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

# Win32 Job-Object supervisor — guarantees the openrecet child dies
# with the harness even if the message pump wedges in WaitMessage with
# g_paused=TRUE. See tools/supervisor/run-supervised.c.
SUPERVISOR_EXE = ROOT / "build" / "openrecet-supervisor.exe"

TARGETS = ("openrecet", "retail", "both")

# Sub-targets that 'both' fans out into, in run order.
BOTH_SUBTARGETS = ("openrecet", "retail")


def golden_subdir(target: str) -> str:
    """Per-target golden directory name."""
    return "golden" if target == "openrecet" else f"golden-{target}"


def load_contact_sheet_module():
    """Import tools/contact-sheet.py by path (hyphen in name blocks plain import)."""
    import importlib.util
    csm_path = ROOT / "tools" / "contact-sheet.py"
    spec = importlib.util.spec_from_file_location("openrecet_contact_sheet", csm_path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# ─── helpers ──────────────────────────────────────────────────────────────


def wslpath_w(p: Path) -> str:
    """Translate a Linux path to a Windows-form path for the WSLInterop exe."""
    r = subprocess.run(
        ["wslpath", "-w", str(p)],
        capture_output=True, text=True, check=True,
    )
    return r.stdout.strip()


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


# ─── scenario spec ────────────────────────────────────────────────────────


@dataclass
class Scenario:
    name: str
    path: Path                         # tests/scenarios/<name>/
    description: str = ""
    rng_seed: int = 1                  # default deterministic seed
    max_frames: int = 60               # default scenario budget
    capture_frames: list[int] = field(default_factory=lambda: [0, 30, 60])
    duration_ceiling_ms: int = 30_000  # wall-clock safety net
    # Optional zoomed-text companion side-by-side. When set, --target
    # both also writes `sidebyside-zoom.png` next to `sidebyside.png`,
    # cropping each frame to the (x,y,w,h) rect and nearest-neighbor-
    # scaling by `factor` (default 4). Picks up tiny font glyph
    # differences that are invisible at the 320×240 thumbnail scale of
    # the main side-by-side. Off by default — only scenarios that
    # render font glyphs are worth zooming. Schema:
    #   zoom_text:
    #     x: 60    # source rect on the 1024×768 captured frame
    #     y: 280
    #     w: 320
    #     h: 200
    #     factor: 4
    zoom_text: dict | None = None

    # Derived (see Scenario.load): True when trace.jsonl is a SEGTRACE — it
    # carries {"wait":...} and/or {"capture":N} ops (see src/input_segtrace.h).
    # Segtrace scenarios drive their own captures from the {capture} ops
    # (anchor-relative), so `capture_frames` is unused and goldens are keyed by
    # capture ORDER (cap_00.bmp, cap_01.bmp, …) rather than absolute frame.
    # Plain {frame,buttons} traces keep the legacy absolute-frame path.
    is_segtrace: bool = False
    # Number of {capture} ops in a segtrace (the expected capture count). 0 for
    # legacy scenarios (which use capture_frames instead).
    n_captures: int = 0

    @classmethod
    def load(cls, scen_path: Path) -> "Scenario":
        if not scen_path.is_dir():
            raise SystemExit(f"scenario dir missing: {scen_path}")
        yaml_path = scen_path / "scenario.yaml"
        if not yaml_path.exists():
            raise SystemExit(f"scenario.yaml missing in {scen_path}")
        data = yaml.safe_load(yaml_path.read_text()) or {}
        zt_raw = data.get("zoom_text")
        zoom_text: dict | None = None
        if zt_raw:
            zoom_text = {
                "x":      int(zt_raw["x"]),
                "y":      int(zt_raw["y"]),
                "w":      int(zt_raw["w"]),
                "h":      int(zt_raw["h"]),
                "factor": int(zt_raw.get("factor", 4)),
            }

        is_segtrace, n_captures = _inspect_trace(scen_path / "trace.jsonl")

        # Segtrace scenarios get their captures from the trace's {capture} ops,
        # not capture_frames — force the latter EMPTY so neither the port nor
        # the retail agent grabs the legacy [0,30,60] default (which the retail
        # Frida path passes straight through to the agent, adding spurious
        # captures that desync the cap-index pairing).
        if is_segtrace:
            capture_frames: list[int] = []
        else:
            capture_frames = [int(x) for x in data.get("capture_frames", [0, 30, 60])]

        return cls(
            name=scen_path.name,
            path=scen_path,
            description=str(data.get("description", "")),
            rng_seed=int(data.get("rng_seed", 1)),
            max_frames=int(data.get("max_frames", 60)),
            capture_frames=capture_frames,
            duration_ceiling_ms=int(data.get("duration_ceiling_ms", 30_000)),
            zoom_text=zoom_text,
            is_segtrace=is_segtrace,
            n_captures=n_captures,
        )


def _inspect_trace(trace_path: Path) -> tuple[bool, int]:
    """Classify a trace file. Returns (is_segtrace, n_captures).

    A trace is a SEGTRACE if any line carries a `wait` or `capture` op (the
    segment grammar in src/input_segtrace.h). `n_captures` counts `capture`
    ops — the expected number of anchor-relative captures. Comments (`#`) and
    blank lines are ignored, matching the C + Frida parsers. A missing file is
    treated as a plain (legacy) trace with no captures."""
    if not trace_path.exists():
        return False, 0
    is_seg = False
    n_cap = 0
    for raw in trace_path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        try:
            rec = json.loads(line)
        except json.JSONDecodeError:
            continue
        if "wait" in rec:
            is_seg = True
        if "capture" in rec:
            is_seg = True
            n_cap += 1
    return is_seg, n_cap


def captured_bmps(run_dir: Path) -> list[Path]:
    """Captured frame BMPs for a run, sorted by absolute engine frame.

    For a segtrace run the absolute frame numbers jitter run-to-run (and
    differ wildly port-vs-retail under turbo), so the Nth element here is the
    Nth `{capture}` op's screenshot — the capture-ORDER key the goldens use."""
    frames_dir = run_dir / "frames"
    if not frames_dir.is_dir():
        return []
    def _fno(p: Path) -> int:
        try:
            return int(p.stem.split("_")[1])
        except (IndexError, ValueError):
            return -1
    return sorted(frames_dir.glob("frame_*.bmp"), key=_fno)


# ─── runner ───────────────────────────────────────────────────────────────


def _ensure_trace_exists(scen: Scenario) -> Path:
    """Return the trace path; create a minimal idle trace if absent."""
    p = scen.path / "trace.jsonl"
    if p.exists():
        return p
    p.write_text('{"frame":0,"buttons":"0x0000"}\n')
    return p


# Map `screen=N` in recet.ini to (width, height). Mirrors the engine's
# branch at FUN_0047a474 (DAT_005cbc04/08 assignment) — keep this
# table in sync with retail's source of truth if the engine changes.
_SCREEN_SIZES: dict[int, tuple[int, int]] = {
    0: (640, 480),
    1: (800, 600),
    2: (1024, 768),
    3: (1280, 960),
}


def _openrecet_screen_dims() -> tuple[int, int]:
    """Return (width, height) openrecet would render at, by reading
    vendor/original/recet.ini's `screen=` value. Falls back to
    1024×768 if the file is missing or the value is invalid — the
    same fallback openrecet's recet_ini_set_defaults uses."""
    ini = ASSET_CWD / "recet.ini"
    if not ini.exists():
        return (1024, 768)
    try:
        for raw in ini.read_text().splitlines():
            line = raw.strip()
            if line.startswith("screen") and "=" in line:
                val = int(line.split("=", 1)[1].strip())
                return _SCREEN_SIZES.get(val, (1024, 768))
    except (OSError, ValueError):
        pass
    return (1024, 768)


def run_scenario_capture_retail(scen: Scenario, run_dir: Path,
                                remote: str, *,
                                turbo: bool = False,
                                silent_audio: bool = False) -> dict:
    """Drive the retail unpacked exe via Frida; capture matching artifacts.

    Delegates to tools/frida_capture.run_capture with input injection
    on: the agent overwrites DAT_073dddd0 each input_poll LEAVE with
    the sticky-trace mask for the current engine frame. The trace is
    the same scenario.yaml-adjacent `trace.jsonl` Phase A consumes, so
    both pipelines walk an identical input sequence.

    `turbo` + `silent_audio` flip the engine into the fast-capture mode
    described in tools/frida/openrecet-agent.js. Useful when running
    many scenarios back-to-back to regenerate side-by-side comparisons —
    each scenario finishes in a fraction of its real-time duration.

    Resolution is pinned to whatever openrecet would render at (from
    vendor/original/recet.ini's `screen=` value, default 1024×768) via
    the agent's force_resolution hook. Without this, retail's empty
    vendor/unpacked/recet.ini sends it to the default 640×480 and the
    side-by-side / zoom-text companions can't line up.
    """
    import frida_capture  # late import: only needed for --target retail
    trace_path = _ensure_trace_exists(scen)

    # Segtrace scenarios drive the agent's anchor-segmented forcing (it owns
    # the input mask AND schedules captures from its {capture} ops), so pass
    # input_segtrace_path instead of input_trace_path; force_input stays off.
    if scen.is_segtrace:
        return frida_capture.run_capture(
            scen, run_dir, remote=remote,
            input_segtrace_path=trace_path,
            hide_window=True,
            turbo=turbo, silent_audio=silent_audio,
            force_resolution=_openrecet_screen_dims(),
        )

    return frida_capture.run_capture(
        scen, run_dir, remote=remote,
        input_trace_path=trace_path, force_input=True,
        # Hide retail's window so the user can't accidentally key into
        # it mid-capture. Agent compensates the missing WM_ACTIVATE by
        # forcing DAT_073dfca0 = 1.
        hide_window=True,
        turbo=turbo, silent_audio=silent_audio,
        force_resolution=_openrecet_screen_dims(),
    )


def run_scenario_capture(scen: Scenario, run_dir: Path, *,
                         turbo: bool = False,
                         silent_audio: bool = False) -> dict:
    """Drive the exe through this scenario; capture frames + audio trace."""
    frames_dir   = run_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    audio_jsonl  = run_dir / "audio.jsonl"
    stdout_log   = run_dir / "stdout.log"
    stderr_log   = run_dir / "stderr.log"

    trace_path = _ensure_trace_exists(scen)

    if not SUPERVISOR_EXE.exists():
        raise SystemExit(
            f"supervisor missing: {SUPERVISOR_EXE}\n"
            f"build it with: nix develop --command "
            f"make -C tools/supervisor"
        )

    if scen.is_segtrace:
        # Anchor-segmented forcing owns the input mask AND schedules the
        # captures from its {capture} ops (anchor-relative, via
        # segtrace_capture_cb → the same g_capture_frames path), so we drop
        # --capture-frames entirely. --anchor-trace-record logs the resolved
        # anchor frames for debugging / alignment.
        anchors_jsonl = run_dir / "anchors.jsonl"
        child_args = [
            "--input-segtrace",      wslpath_w(trace_path),
            "--anchor-trace-record", wslpath_w(anchors_jsonl),
            "--rng-seed",            str(scen.rng_seed),
            "--max-frames",          str(scen.max_frames),
            "--capture-to",          wslpath_w(frames_dir),
            "--audio-trace",         wslpath_w(audio_jsonl),
            "--max-duration-ms",     str(scen.duration_ceiling_ms),
            "--hidden",
        ]
    else:
        capture_frames_csv = ",".join(str(f) for f in scen.capture_frames)
        child_args = [
            "--input-trace-replay", wslpath_w(trace_path),
            "--rng-seed",           str(scen.rng_seed),
            "--max-frames",         str(scen.max_frames),
            "--capture-to",         wslpath_w(frames_dir),
            "--capture-frames",     capture_frames_csv,
            "--audio-trace",        wslpath_w(audio_jsonl),
            "--max-duration-ms",    str(scen.duration_ceiling_ms),
            # Hide the openrecet window so a captured run can't be clobbered
            # by accidental keystrokes / focus steals. D3D renders to a
            # video-memory back buffer regardless of window visibility, so
            # the capture path is unaffected.
            "--hidden",
        ]
    if turbo:
        child_args.append("--turbo")
    if silent_audio:
        child_args.append("--silent-audio")

    # Wrap in the Job-Object supervisor. Supervisor timeout sits 1 s
    # past the in-engine ceiling so openrecet's clean exit path wins
    # the race; if the message pump is wedged, the supervisor reaps
    # via kernel-side job close. Python's subprocess.run timeout is
    # one further second past that — only fires if the supervisor
    # itself misbehaves.
    sup_timeout_ms = scen.duration_ceiling_ms + 1000
    cmd = [
        str(SUPERVISOR_EXE),
        str(int(sup_timeout_ms)),
        wslpath_w(BUILD_EXE),
        *child_args,
    ]

    t0 = dt.datetime.now(dt.timezone.utc)
    with stdout_log.open("wb") as so, stderr_log.open("wb") as se:
        proc = subprocess.run(
            cmd, cwd=str(ASSET_CWD),
            stdout=so, stderr=se,
            timeout=scen.duration_ceiling_ms / 1000 + 2,
        )
    elapsed_ms = int((dt.datetime.now(dt.timezone.utc) - t0).total_seconds() * 1000)

    captured = sorted(frames_dir.glob("frame_*.bmp"))
    meta = {
        "scenario":      scen.name,
        "exit_code":     proc.returncode,
        "elapsed_ms":    elapsed_ms,
        "captured_frames": [int(p.stem.split("_")[1]) for p in captured],
        "trace_path":    str(trace_path.relative_to(ROOT)),
    }
    (run_dir / "run.json").write_text(json.dumps(meta, indent=2))
    return meta


# ─── cross-target side-by-side ────────────────────────────────────────────


def render_sidebyside(left_frames: Path, right_frames: Path,
                      out_path: Path,
                      left_label: str = "openrecet",
                      right_label: str = "retail",
                      tile_wh: tuple[int, int] = (320, 240),
                      pair_by_index: bool = False) -> Path | None:
    """Drop a per-frame ours|retail PNG at `out_path`. Returns the path
    on success, None if either side captured nothing.

    Pairs frames by *filename* by default, so a missing capture on one side
    becomes a placeholder tile rather than a silent off-by-one across the
    whole sheet. The first row is a label strip identifying the column.

    `pair_by_index=True` (segtrace mode) pairs the Nth left capture with the
    Nth right capture instead — the absolute frame numbers differ port-vs-
    retail under anchor-relative timing, so filename pairing would never
    line up. Rows are labelled by capture index (cap_NN).
    """
    from PIL import Image

    csm = load_contact_sheet_module()

    tw, th = tile_wh
    placeholder = Image.new("RGB", (tw, th), (40, 0, 0))
    tiles:  list[Image.Image] = []
    labels: list[str] = []

    if pair_by_index:
        lefts  = sorted(csm.list_images(left_frames),
                        key=lambda p: int(p.stem.split("_")[1]))
        rights = sorted(csm.list_images(right_frames),
                        key=lambda p: int(p.stem.split("_")[1]))
        n = max(len(lefts), len(rights))
        if n == 0:
            return None
        for i in range(n):
            lp = lefts[i]  if i < len(lefts)  else None
            rp = rights[i] if i < len(rights) else None
            tiles.append(csm.thumb(lp, tw, th) if lp else placeholder)
            labels.append(f"{left_label} · cap_{i:02d}" if lp
                          else f"{left_label} · cap_{i:02d} (missing)")
            tiles.append(csm.thumb(rp, tw, th) if rp else placeholder)
            labels.append(f"{right_label} · cap_{i:02d}" if rp
                          else f"{right_label} · cap_{i:02d} (missing)")
    else:
        lefts_m  = {p.name: p for p in csm.list_images(left_frames)}
        rights_m = {p.name: p for p in csm.list_images(right_frames)}
        names    = sorted(set(lefts_m) | set(rights_m))
        if not names:
            return None
        for nm in names:
            lp = lefts_m.get(nm)
            rp = rights_m.get(nm)
            tiles.append(csm.thumb(lp, tw, th) if lp else placeholder)
            labels.append(f"{left_label} · {nm}" if lp else f"{left_label} · (missing)")
            tiles.append(csm.thumb(rp, tw, th) if rp else placeholder)
            labels.append(f"{right_label} · {nm}" if rp else f"{right_label} · (missing)")

    sheet = csm.grid(tiles, labels, cols=2)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out_path, optimize=True)
    return out_path


def render_sidebyside_zoom(left_frames: Path, right_frames: Path,
                           out_path: Path,
                           zoom: dict,
                           left_label: str = "openrecet",
                           right_label: str = "retail") -> Path | None:
    """Same layout as `render_sidebyside`, but each tile is a
    nearest-neighbor-scaled crop of the captured frame instead of a
    LANCZOS-fit thumbnail. Useful for inspecting font glyph differences
    that are below the visual threshold of the 320×240 main
    side-by-side.

    `zoom` is the dict produced by `Scenario.load`: keys x, y, w, h
    (source rect in the captured 1024×768 frame) + `factor` (integer
    scale, default 4). Output tile dimensions are `w*factor × h*factor`.

    Crops outside the captured frame are clipped to the frame bounds
    (so a too-large rect still produces a tile, just smaller). Mode is
    always nearest-neighbor — every source pixel maps to a `factor`×
    `factor` block of output pixels, preserving pixel boundaries the
    user needs to see when diffing antialiased glyphs.
    """
    from PIL import Image

    csm = load_contact_sheet_module()

    crop_x = int(zoom["x"])
    crop_y = int(zoom["y"])
    crop_w = int(zoom["w"])
    crop_h = int(zoom["h"])
    factor = int(zoom.get("factor", 4))
    out_w  = crop_w * factor
    out_h  = crop_h * factor

    lefts  = {p.name: p for p in csm.list_images(left_frames)}
    rights = {p.name: p for p in csm.list_images(right_frames)}
    names  = sorted(set(lefts) | set(rights))
    if not names:
        return None

    placeholder = Image.new("RGB", (out_w, out_h), (40, 0, 0))

    def zoom_tile(path: Path) -> Image.Image:
        img = Image.open(path).convert("RGB")
        # Clip the source rect to the image bounds so a malformed YAML
        # config produces a (smaller) tile instead of a stack trace.
        x0 = max(0, min(crop_x, img.width))
        y0 = max(0, min(crop_y, img.height))
        x1 = max(x0, min(crop_x + crop_w, img.width))
        y1 = max(y0, min(crop_y + crop_h, img.height))
        crop = img.crop((x0, y0, x1, y1))
        # NEAREST = preserve pixel boundaries; we want to *see* where
        # individual glyph pixels landed, not a smoothed interpolation.
        scaled = crop.resize(
            (max(1, crop.width * factor), max(1, crop.height * factor)),
            Image.Resampling.NEAREST,
        )
        # Pad to the canonical tile size so the grid lays out
        # consistently across rows even when a frame had a clipped crop.
        canvas = Image.new("RGB", (out_w, out_h), (0, 0, 0))
        canvas.paste(scaled, (0, 0))
        return canvas

    tiles:  list[Image.Image] = []
    labels: list[str] = []
    suffix = f" · zoom ×{factor} @ ({crop_x},{crop_y}) {crop_w}×{crop_h}"
    for n in names:
        lp = lefts.get(n)
        rp = rights.get(n)
        tiles.append(zoom_tile(lp) if lp else placeholder)
        labels.append(f"{left_label} · {n}{suffix}" if lp
                      else f"{left_label} · (missing){suffix}")
        tiles.append(zoom_tile(rp) if rp else placeholder)
        labels.append(f"{right_label} · {n}{suffix}" if rp
                      else f"{right_label} · (missing){suffix}")

    # The zoom tiles are large (1000+ px per side typical); a default
    # 18-px label strip with ~10-px bitmap font is unreadable against
    # them. Scale both proportionally to the tile width so the filename
    # / coord suffix stays legible at any zoom level.
    label_font = max(14, out_w // 60)   # ~14 px floor; 1440-wide tile → 24 px
    label_h    = label_font + 12        # padding above + descender slack

    sheet = csm.grid(tiles, labels, cols=2,
                     label_h=label_h, font_size=label_font)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out_path, optimize=True)
    return out_path


# ─── diff ──────────────────────────────────────────────────────────────────


def _red_tint_overlay(golden_rgb, new_rgb, threshold: int = 1):
    """Return (overlay_rgb, mask) where pixels with |new-golden| >= threshold
    are tinted 50/50 with red. Inputs are H×W×3 uint8 numpy arrays."""
    import numpy as np
    diff = (new_rgb.astype("int16") - golden_rgb.astype("int16"))
    mask = (np.abs(diff).max(axis=2) >= threshold)
    out  = new_rgb.copy()
    if mask.any():
        red = np.array([255, 0, 0], dtype="int16")
        out[mask] = ((out[mask].astype("int16") + red) // 2).astype("uint8")
    return out, mask


def _diff_segtrace(scen: Scenario, run_dir: Path, golden_dir: Path,
                   diff_dir: Path) -> tuple[int, int]:
    """Capture-INDEX diff for segtrace scenarios. Goldens are cap_NN.bmp
    (Nth {capture} op); the run's Nth captured frame is compared to it,
    irrespective of the (jittering) absolute frame number. Returns
    (pass_count, fail_count)."""
    import numpy as np
    from PIL import Image

    golden_caps = sorted(golden_dir.glob("cap_*.bmp"))
    run_caps    = captured_bmps(run_dir)
    n = max(len(golden_caps), len(run_caps))
    if n == 0:
        print(f"  no captures and no cap_*.bmp goldens — nothing to diff")
        return 0, 0

    passed = failed = 0
    for i in range(n):
        gld_p = golden_dir / f"cap_{i:02d}.bmp"
        new_p = run_caps[i] if i < len(run_caps) else None

        if new_p is None:
            print(f"  FAIL cap_{i:02d}: not captured (golden present)")
            failed += 1
            continue
        if not gld_p.exists():
            print(f"  FAIL cap_{i:02d}: missing golden {gld_p.name} "
                  f"(captured {new_p.name})")
            failed += 1
            continue

        if sha256(new_p) == sha256(gld_p):
            print(f"  pass cap_{i:02d} ({new_p.name})")
            passed += 1
            continue

        new_img = Image.open(new_p).convert("RGB")
        gld_img = Image.open(gld_p).convert("RGB")
        if new_img.size != gld_img.size:
            print(f"  FAIL cap_{i:02d}: size {new_img.size} vs golden {gld_img.size}")
            failed += 1
            continue
        overlay, mask = _red_tint_overlay(np.asarray(gld_img), np.asarray(new_img))
        diff_px = int(mask.sum())
        Image.fromarray(overlay).save(diff_dir / f"cap_{i:02d}.png")
        print(f"  FAIL cap_{i:02d}: {diff_px} px differ "
              f"→ {(diff_dir / f'cap_{i:02d}.png').relative_to(ROOT)}")
        failed += 1
    return passed, failed


def diff_against_golden(scen: Scenario, run_dir: Path, target: str) -> tuple[int, int]:
    """Bit-exact diff per frame. Returns (pass_count, fail_count).

    Mismatches additionally emit a red-tint overlay PNG at
    `<run_dir>/diff/<frame>.png` so visual inspection of the
    regression is one Read away. Missing golden frames count as fail.
    """
    import numpy as np
    from PIL import Image

    golden_dir = scen.path / golden_subdir(target)
    if not golden_dir.is_dir():
        print(f"  scenario '{scen.name}' [{target}]: no golden directory at {golden_dir}")
        print(f"  re-run with --bless to create one from this run.")
        n_expected = scen.n_captures if scen.is_segtrace else len(scen.capture_frames)
        return 0, n_expected

    diff_dir = run_dir / "diff"
    diff_dir.mkdir(exist_ok=True)

    if scen.is_segtrace:
        return _diff_segtrace(scen, run_dir, golden_dir, diff_dir)

    passed = failed = 0
    for fi in scen.capture_frames:
        name = f"frame_{fi:05d}.bmp"
        new_p = run_dir / "frames" / name
        gld_p = golden_dir / name

        if not new_p.exists():
            print(f"  FAIL frame {fi:05d}: not captured")
            failed += 1
            continue
        if not gld_p.exists():
            print(f"  FAIL frame {fi:05d}: missing golden {gld_p}")
            failed += 1
            continue

        # Bit-exact compare via sha256 — fast path.
        if sha256(new_p) == sha256(gld_p):
            print(f"  pass frame {fi:05d}")
            passed += 1
            continue

        # Mismatch — produce the overlay so the human reviewer can see
        # what diverged. PIL converts BMP transparently.
        new_img = Image.open(new_p).convert("RGB")
        gld_img = Image.open(gld_p).convert("RGB")
        if new_img.size != gld_img.size:
            print(f"  FAIL frame {fi:05d}: size {new_img.size} vs golden {gld_img.size}")
            failed += 1
            continue
        n_rgb = np.asarray(new_img)
        g_rgb = np.asarray(gld_img)
        overlay, mask = _red_tint_overlay(g_rgb, n_rgb)
        diff_px = int(mask.sum())
        Image.fromarray(overlay).save(diff_dir / f"frame_{fi:05d}.png")
        print(f"  FAIL frame {fi:05d}: {diff_px} px differ "
              f"→ {(diff_dir / f'frame_{fi:05d}.png').relative_to(ROOT)}")
        failed += 1
    return passed, failed


# ─── bless ────────────────────────────────────────────────────────────────


def bless(scen: Scenario, run_dir: Path, target: str) -> int:
    """Copy a fresh run's captured frames + audio trace into the per-target
    golden dir. Under --target retail we also persist the recorded
    trace.jsonl (the engine's actual per-frame input mask)."""
    import shutil
    golden_dir = scen.path / golden_subdir(target)
    golden_dir.mkdir(parents=True, exist_ok=True)

    if scen.is_segtrace:
        # Capture-INDEX goldens: the Nth captured frame becomes cap_NN.bmp.
        # Wipe stale cap_*.bmp first so a run with fewer captures doesn't
        # leave orphans that the index diff would then flag as missing.
        for stale in golden_dir.glob("cap_*.bmp"):
            stale.unlink()
        run_caps = captured_bmps(run_dir)
        copied = 0
        for i, src in enumerate(run_caps):
            shutil.copyfile(src, golden_dir / f"cap_{i:02d}.bmp")
            copied += 1
        audio_src = run_dir / "audio.jsonl"
        if audio_src.exists():
            shutil.copyfile(audio_src, golden_dir / "audio.jsonl")
        anchors_src = run_dir / "anchors.jsonl"
        if anchors_src.exists():
            shutil.copyfile(anchors_src, golden_dir / "anchors.jsonl")
        print(f"  blessed: {copied} capture(s) → {golden_dir.relative_to(ROOT)} "
              f"(cap_00..cap_{max(copied-1,0):02d})")
        return copied

    copied = 0
    for fi in scen.capture_frames:
        name = f"frame_{fi:05d}.bmp"
        src = run_dir / "frames" / name
        if not src.exists():
            print(f"  bless: WARNING — captured frame missing: {src}")
            continue
        shutil.copyfile(src, golden_dir / name)
        copied += 1

    audio_src = run_dir / "audio.jsonl"
    if audio_src.exists():
        shutil.copyfile(audio_src, golden_dir / "audio.jsonl")

    if target == "retail":
        trace_src = run_dir / "trace.jsonl"
        if trace_src.exists():
            shutil.copyfile(trace_src, golden_dir / "trace.jsonl")

    print(f"  blessed: {copied} frame(s) → {golden_dir.relative_to(ROOT)}")
    return copied


# ─── cli ──────────────────────────────────────────────────────────────────


def discover_all() -> list[Path]:
    if not SCENARIOS.is_dir():
        return []
    return sorted([p for p in SCENARIOS.iterdir()
                   if p.is_dir() and (p / "scenario.yaml").exists()])


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("scenario", nargs="?",
                    help="scenario name under tests/scenarios/; "
                         "omit to run all")
    ap.add_argument("--bless", action="store_true",
                    help="regenerate golden frames from this run instead of diffing")
    ap.add_argument("--target", choices=TARGETS, default="openrecet",
                    help="which binary to drive: our reimplementation "
                         "(openrecet, default) or the retail unpacked exe "
                         "instrumented via Frida (retail)")
    ap.add_argument("--frida-remote",
                    default=os.environ.get("OPENRECET_FRIDA_REMOTE",
                                           "cutestation.soy:27042"),
                    help="frida-server host:port used by --target retail "
                         "(default %(default)s; env $OPENRECET_FRIDA_REMOTE)")
    ap.add_argument("--run-dir-root", type=Path, default=ROOT / "runs" / "scenarios",
                    help="where to write per-scenario run artifacts "
                         "(default: runs/scenarios/)")
    ap.add_argument("--turbo", action="store_true",
                    help="bypass the engine's 60 FPS frame limiter and feed "
                         "it a virtual 16.6 ms timestep — the game runs as "
                         "fast as the host can chew through it. Affects both "
                         "targets. Pair with --silent-audio.")
    ap.add_argument("--silent-audio", action="store_true",
                    help="force audio paths silent (centibel -10000) while "
                         "leaving the engine's audio code running normally. "
                         "Recommended alongside --turbo since DirectMusic "
                         "complains about being clocked at 200+ fps.")
    ap.add_argument("--no-regen", action="store_true",
                    help="after a --target both run, do NOT rebuild the "
                         "interactive comparison gallery "
                         "(runs/comparisons/index.html). Default rebuilds it. "
                         "Push it to the llm-feed to view. No effect for "
                         "single-target runs.")
    args = ap.parse_args(argv)

    if args.target in ("openrecet", "both"):
        if not BUILD_EXE.exists():
            raise SystemExit(f"exe missing: {BUILD_EXE}. Build: `make -C src`.")

    if args.scenario:
        scen_path = SCENARIOS / args.scenario
        if not scen_path.is_dir():
            raise SystemExit(f"unknown scenario: {args.scenario}")
        scenarios = [scen_path]
    else:
        scenarios = discover_all()
        if not scenarios:
            print("no scenarios under tests/scenarios/")
            return 0

    rid = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    total_pass = total_fail = 0
    for sp in scenarios:
        scen = Scenario.load(sp)
        run_dir = args.run_dir_root / f"{scen.name}-{args.target}-{rid}"
        print(f"\n# scenario: {scen.name} [target: {args.target}]")
        if scen.description:
            print(f"  desc: {scen.description}")

        if args.target == "both":
            # Fan out into per-subtarget subdirs so each pipeline gets
            # the same {frames/, audio.jsonl, trace.jsonl, run.json}
            # layout it would have under a single-target run. Avoids
            # filename collisions and keeps the existing diff/bless
            # helpers usable unchanged.
            sub_meta: dict[str, dict] = {}
            for sub in BOTH_SUBTARGETS:
                sub_dir = run_dir / sub
                print(f"  ── {sub} ──")
                if sub == "retail":
                    m = run_scenario_capture_retail(
                        scen, sub_dir, args.frida_remote,
                        turbo=args.turbo, silent_audio=args.silent_audio)
                else:
                    m = run_scenario_capture(
                        scen, sub_dir,
                        turbo=args.turbo, silent_audio=args.silent_audio)
                _exp = scen.n_captures if scen.is_segtrace else len(scen.capture_frames)
                print(f"    exit={m['exit_code']} elapsed_ms={m['elapsed_ms']} "
                      f"captured={len(m['captured_frames'])}/{_exp}")
                sub_meta[sub] = m

            sbs = render_sidebyside(
                left_frames=run_dir / "openrecet" / "frames",
                right_frames=run_dir / "retail"    / "frames",
                out_path=run_dir / "sidebyside.png",
                pair_by_index=scen.is_segtrace,
            )
            if sbs is not None:
                print(f"  side-by-side: {sbs.relative_to(ROOT)}")
            else:
                print(f"  side-by-side: SKIPPED (no frames captured on at least one side)")

            # Optional zoomed-text companion. Only fires for scenarios
            # whose YAML carries `zoom_text:` — see Scenario.zoom_text.
            if scen.zoom_text is not None:
                sbs_zoom = render_sidebyside_zoom(
                    left_frames=run_dir / "openrecet" / "frames",
                    right_frames=run_dir / "retail"    / "frames",
                    out_path=run_dir / "sidebyside-zoom.png",
                    zoom=scen.zoom_text,
                )
                if sbs_zoom is not None:
                    print(f"  side-by-side (zoom): {sbs_zoom.relative_to(ROOT)}")

            if args.bless:
                for sub in BOTH_SUBTARGETS:
                    bless(scen, run_dir / sub, sub)
                continue

            for sub in BOTH_SUBTARGETS:
                p, f = diff_against_golden(scen, run_dir / sub, sub)
                total_pass += p
                total_fail += f
            continue

        if args.target == "retail":
            meta = run_scenario_capture_retail(
                scen, run_dir, args.frida_remote,
                turbo=args.turbo, silent_audio=args.silent_audio)
        else:
            meta = run_scenario_capture(
                scen, run_dir,
                turbo=args.turbo, silent_audio=args.silent_audio)
        _exp = scen.n_captures if scen.is_segtrace else len(scen.capture_frames)
        print(f"  exit={meta['exit_code']} elapsed_ms={meta['elapsed_ms']} "
              f"captured={len(meta['captured_frames'])}/{_exp}")

        if args.bless:
            bless(scen, run_dir, args.target)
            continue

        p, f = diff_against_golden(scen, run_dir, args.target)
        total_pass += p
        total_fail += f

    # After --target both, rebuild the interactive comparison gallery so the
    # freshly-captured atlases are reflected without a second tool invocation.
    # (Skipped under --no-regen, e.g. when regen-comparisons.py is driving the
    # batch and will do its own final regen.)
    if args.target == "both" and not args.no_regen:
        _regen_comparison_gallery(scenarios)

    if args.bless:
        return 0

    print(f"\n{total_pass} passed, {total_fail} failed")
    return 0 if total_fail == 0 else 1


def _regen_comparison_gallery(scen_paths: list[Path]) -> None:
    """Rebuild runs/comparisons/index.html (interactive atlas gallery) from the
    latest --target both runs. Push it to the llm-feed to view."""
    try:
        import comparison_page
    except ImportError as e:  # never fail a run over the gallery step
        print(f"  comparison gallery: skipped ({e})")
        return
    runs_dir = ROOT / "runs" / "scenarios"
    out_dir  = ROOT / "runs" / "comparisons"
    # Regen the full index (all scenarios) so the page stays complete even when
    # a single scenario was run; cheap — it only rebuilds atlases that have a
    # both-run.
    all_scens = discover_all()
    items = comparison_page.collect_artifacts(all_scens, runs_dir, out_dir)
    index = out_dir / "index.html"
    comparison_page.render_html(items, index)
    print(f"  comparison gallery → {index.relative_to(ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
