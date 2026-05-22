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

    @classmethod
    def load(cls, scen_path: Path) -> "Scenario":
        if not scen_path.is_dir():
            raise SystemExit(f"scenario dir missing: {scen_path}")
        yaml_path = scen_path / "scenario.yaml"
        if not yaml_path.exists():
            raise SystemExit(f"scenario.yaml missing in {scen_path}")
        data = yaml.safe_load(yaml_path.read_text()) or {}
        return cls(
            name=scen_path.name,
            path=scen_path,
            description=str(data.get("description", "")),
            rng_seed=int(data.get("rng_seed", 1)),
            max_frames=int(data.get("max_frames", 60)),
            capture_frames=[int(x) for x in data.get("capture_frames", [0, 30, 60])],
            duration_ceiling_ms=int(data.get("duration_ceiling_ms", 30_000)),
        )


# ─── runner ───────────────────────────────────────────────────────────────


def _ensure_trace_exists(scen: Scenario) -> Path:
    """Return the trace path; create a minimal idle trace if absent."""
    p = scen.path / "trace.jsonl"
    if p.exists():
        return p
    p.write_text('{"frame":0,"buttons":"0x0000"}\n')
    return p


def run_scenario_capture_retail(scen: Scenario, run_dir: Path,
                                remote: str) -> dict:
    """Drive the retail unpacked exe via Frida; capture matching artifacts.

    Delegates to tools/frida_capture.run_capture with input injection
    on: the agent overwrites DAT_073dddd0 each input_poll LEAVE with
    the sticky-trace mask for the current engine frame. The trace is
    the same scenario.yaml-adjacent `trace.jsonl` Phase A consumes, so
    both pipelines walk an identical input sequence.
    """
    import frida_capture  # late import: only needed for --target retail
    trace_path = _ensure_trace_exists(scen)
    return frida_capture.run_capture(
        scen, run_dir, remote=remote,
        input_trace_path=trace_path, force_input=True,
        # Hide retail's window so the user can't accidentally key into
        # it mid-capture. Agent compensates the missing WM_ACTIVATE by
        # forcing DAT_073dfca0 = 1.
        hide_window=True,
    )


def run_scenario_capture(scen: Scenario, run_dir: Path) -> dict:
    """Drive the exe through this scenario; capture frames + audio trace."""
    frames_dir   = run_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    audio_jsonl  = run_dir / "audio.jsonl"
    stdout_log   = run_dir / "stdout.log"
    stderr_log   = run_dir / "stderr.log"

    trace_path = _ensure_trace_exists(scen)
    capture_frames_csv = ",".join(str(f) for f in scen.capture_frames)

    cmd = [
        str(BUILD_EXE),
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

    t0 = dt.datetime.now(dt.timezone.utc)
    with stdout_log.open("wb") as so, stderr_log.open("wb") as se:
        proc = subprocess.run(
            cmd, cwd=str(ASSET_CWD),
            stdout=so, stderr=se,
            timeout=scen.duration_ceiling_ms / 1000 + 5,
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
                      tile_wh: tuple[int, int] = (320, 240)) -> Path | None:
    """Drop a per-frame ours|retail PNG at `out_path`. Returns the path
    on success, None if either side captured nothing.

    Pairs frames by *filename* (not by sorted-index position), so a
    missing capture on one side becomes a placeholder tile rather than a
    silent off-by-one across the whole sheet. The first row is a label
    strip identifying which column is which target.
    """
    from PIL import Image

    csm = load_contact_sheet_module()

    lefts  = {p.name: p for p in csm.list_images(left_frames)}
    rights = {p.name: p for p in csm.list_images(right_frames)}
    names  = sorted(set(lefts) | set(rights))
    if not names:
        return None

    tw, th = tile_wh
    placeholder = Image.new("RGB", (tw, th), (40, 0, 0))

    tiles:  list[Image.Image] = []
    labels: list[str] = []
    for n in names:
        lp = lefts.get(n)
        rp = rights.get(n)
        tiles.append(csm.thumb(lp, tw, th) if lp else placeholder)
        labels.append(f"{left_label} · {n}" if lp else f"{left_label} · (missing)")
        tiles.append(csm.thumb(rp, tw, th) if rp else placeholder)
        labels.append(f"{right_label} · {n}" if rp else f"{right_label} · (missing)")

    sheet = csm.grid(tiles, labels, cols=2)
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
        return 0, len(scen.capture_frames)

    diff_dir = run_dir / "diff"
    diff_dir.mkdir(exist_ok=True)

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
    ap.add_argument("--frida-remote", default="127.0.0.1:27042",
                    help="frida-server host:port used by --target retail "
                         "(default %(default)s)")
    ap.add_argument("--run-dir-root", type=Path, default=ROOT / "runs" / "scenarios",
                    help="where to write per-scenario run artifacts "
                         "(default: runs/scenarios/)")
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
                    m = run_scenario_capture_retail(scen, sub_dir, args.frida_remote)
                else:
                    m = run_scenario_capture(scen, sub_dir)
                print(f"    exit={m['exit_code']} elapsed_ms={m['elapsed_ms']} "
                      f"captured={len(m['captured_frames'])}/{len(scen.capture_frames)}")
                sub_meta[sub] = m

            sbs = render_sidebyside(
                left_frames=run_dir / "openrecet" / "frames",
                right_frames=run_dir / "retail"    / "frames",
                out_path=run_dir / "sidebyside.png",
            )
            if sbs is not None:
                print(f"  side-by-side: {sbs.relative_to(ROOT)}")
            else:
                print(f"  side-by-side: SKIPPED (no frames captured on at least one side)")

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
            meta = run_scenario_capture_retail(scen, run_dir, args.frida_remote)
        else:
            meta = run_scenario_capture(scen, run_dir)
        print(f"  exit={meta['exit_code']} elapsed_ms={meta['elapsed_ms']} "
              f"captured={len(meta['captured_frames'])}/{len(scen.capture_frames)}")

        if args.bless:
            bless(scen, run_dir, args.target)
            continue

        p, f = diff_against_golden(scen, run_dir, args.target)
        total_pass += p
        total_fail += f

    if args.bless:
        return 0

    print(f"\n{total_pass} passed, {total_fail} failed")
    return 0 if total_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
