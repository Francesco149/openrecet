#!/usr/bin/env python3
"""
tools/export_trace.py — run a TAS trace and export a contiguous frame window
plus per-frame + whole-trace metadata, ready to push to the llm-feed as a
`trace` card (tools/../../llm-feed/feed.py trace --dir <run-dir>).

This is the producer side of the frame-by-frame trace viewer
(docs/plans/trace-viewer.md, Phase 3). It:

  1. Resolves a runnable segtrace from either a distilled `.trace.jsonl` or a
     RAW recording (openrecet-trace-*.raw.jsonl → distilled via distill_trace,
     --house-segtrace to wrap the new-game→HOUSE intro).
  2. Injects a {caprange:[start,count]} op (anchor-relative, jitter-immune) so
     the port captures the whole window in one shot — bypassing the 32-frame
     CAPTURE_FRAMES_MAX (Phase 1).
  3. Drives the port via run-openrecet.sh with --capture-to (PNG frames) and
     --player-pos-log (the per-frame metadata: rng/buttons/px/py/pz/anim/oct/…).
  4. Writes the export dir:
        <run-dir>/frames/frame_NNNNN.png    every frame in the window
        <run-dir>/meta.jsonl                one {"frame":N,...} per captured frame
        <run-dir>/global.json               {rng_seed_at_start, trace_jsonl, …}

Acceptance: frame count == meta line count == caprange count.

Usage:
  # from a distilled house segtrace, capture 150 frames from anchor+1540:
  nix develop --command python3 tools/export_trace.py \
      tests/scenarios/house-wall-collide/trace.jsonl \
      --caprange 1540,150 --run-dir runs/trace-export/house-dust \
      --name "house free-roam dust walk"

  # from a RAW recording (distil + wrap the intro first):
  nix develop --command python3 tools/export_trace.py REC.raw.jsonl \
      --house-segtrace --caprange 1540,150 --run-dir runs/trace-export/rec
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import distill_trace          # noqa: E402  (sibling tool, reused as a module)
from frame_io import frame_glob   # noqa: E402


def load_ops(path: Path) -> list[dict]:
    """Parse a JSONL trace into a list of op dicts (skipping #comments/blanks)."""
    ops = []
    for ln in path.read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        ops.append(json.loads(s))
    return ops


def is_raw_recording(path: Path) -> bool:
    """A RAW recording starts with a {"_rec":...} header line."""
    for ln in path.read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        return s.startswith('{"_rec"') or '"_rec"' in json.loads(s)
    return False


def resolve_trace(src: Path, house_segtrace: bool) -> tuple[str, list[dict]]:
    """Return (jsonl_text, ops) for the runnable segtrace.

    A RAW recording is distilled first (flat, or --house-segtrace wrapped); an
    already-distilled trace is used verbatim."""
    if is_raw_recording(src):
        changes, caps, escs, cts, total, rng_seed, _anchors = distill_trace.load_raw(str(src))
        if not changes:
            raise SystemExit(f"export_trace: no input frames in {src}")
        text = (distill_trace.emit_house_segtrace(changes, caps, escs, cts, rng_seed)
                if house_segtrace
                else distill_trace.emit_flat(changes, caps, escs, cts, total, rng_seed))
        ops = [json.loads(l) for l in text.splitlines()
               if l.strip() and not l.startswith("#")]
        return text, ops
    # already distilled
    return src.read_text(), load_ops(src)


def extract_rng_seed(ops: list[dict]) -> int | None:
    """The LCG value from the first {rngseed:[frame,value]} op, if any."""
    for o in ops:
        if "rngseed" in o and isinstance(o["rngseed"], list) and len(o["rngseed"]) == 2:
            return int(o["rngseed"][1]) & 0xffffffff
    return None


def has_caprange(ops: list[dict]) -> bool:
    return any("caprange" in o for o in ops)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="distilled .trace.jsonl OR a RAW recording")
    ap.add_argument("--caprange", required=True, metavar="START,COUNT",
                    help="anchor-relative contiguous capture window (START frames "
                         "after the trace's final anchor, COUNT frames long)")
    ap.add_argument("--run-dir", required=True,
                    help="output dir (frames/ + meta.jsonl + global.json)")
    ap.add_argument("--house-segtrace", action="store_true",
                    help="distil a RAW recording as a bootable new-game→HOUSE segtrace")
    ap.add_argument("--name", default="", help="human label for the trace")
    ap.add_argument("--scenario", default="", help="scenario id (free-form, → global)")
    ap.add_argument("--fps", type=int, default=20, help="playback fps hint (→ global)")
    ap.add_argument("--max-frames", type=int, default=4000,
                    help="absolute frame budget (must exceed the window end; "
                         "the window is anchor-relative so allow headroom)")
    ap.add_argument("--timeout-ms", type=int, default=180000,
                    help="supervisor hard timeout for the drive")
    args = ap.parse_args(argv)

    try:
        start_s, count_s = args.caprange.split(",")
        cr_start, cr_count = int(start_s), int(count_s)
    except ValueError:
        ap.error("--caprange must be START,COUNT (e.g. 1540,150)")
    if cr_count <= 0:
        ap.error("--caprange COUNT must be > 0")

    src = Path(args.trace).resolve()
    if not src.exists():
        ap.error(f"trace not found: {src}")

    run_dir = Path(args.run_dir).resolve()
    frames_dir = run_dir / "frames"
    frames_dir.mkdir(parents=True, exist_ok=True)
    meta_path = run_dir / "meta.jsonl"
    work_path = run_dir / "trace.work.jsonl"
    global_path = run_dir / "global.json"

    text, ops = resolve_trace(src, args.house_segtrace)
    rng_seed = extract_rng_seed(ops)

    # Strip pre-existing scalar {capture:N} ops. A recording (and the distilled
    # segtrace it produces) carries its own single-frame F3 capture points; those
    # fire independently of our contiguous window and land at scattered absolute
    # frames (e.g. base+1540, base+1780). The viewer sorts ALL captured PNGs by
    # frame number, so a stray capture 240 frames past the window glues onto the
    # window's tail and reads as a one-frame "jump" (the f=3410→3650 artifact).
    # We capture exactly the caprange window, so these scalar captures are pure
    # pollution — drop them. {esc}/{rngseed}/inputs are kept (they affect the sim).
    def _is_scalar_capture(line: str) -> bool:
        s = line.strip()
        if not s or s.startswith("#"):
            return False
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            return False
        return isinstance(o, dict) and "capture" in o and "caprange" not in o

    n_stripped = sum(1 for ln in text.splitlines() if _is_scalar_capture(ln))
    if n_stripped:
        text = "\n".join(ln for ln in text.splitlines()
                         if not _is_scalar_capture(ln)) + "\n"
        ops = [o for o in ops if not (isinstance(o, dict)
                                      and "capture" in o and "caprange" not in o)]
        print(f"export_trace: stripped {n_stripped} scalar {{capture}} op(s) "
              "(contiguous caprange supersedes them)", file=sys.stderr)

    # Inject the caprange op (in the final segment — it's appended after the last
    # wait) unless the trace already declares one.
    if not has_caprange(ops):
        cap_op = {"caprange": [cr_start, cr_count]}
        ops.append(cap_op)
        text = text.rstrip("\n") + "\n" + json.dumps(cap_op) + "\n"
    work_path.write_text(text)

    # Drive the port. run-openrecet.sh rewrites the unix paths to the Windows
    # paths the exe's fopen needs and auto-converts the BMP frames → PNG.
    cmd = [
        str(ROOT / "tools/run-openrecet.sh"),
        "--timeout-ms", str(args.timeout_ms),
        "--hidden", "--turbo", "--silent-audio",
        "--input-segtrace", str(work_path),
        "--capture-to", str(frames_dir),
        "--player-pos-log", str(meta_path),
        "--max-frames", str(args.max_frames),
    ]
    print(f"export_trace: driving port → {run_dir}", file=sys.stderr)
    print("export_trace:   " + " ".join(cmd), file=sys.stderr)
    rc = subprocess.run(cmd, cwd=str(ROOT)).returncode
    if rc != 0:
        print(f"export_trace: WARNING run-openrecet exited {rc}", file=sys.stderr)

    # Collect the captured frames (PNG, frame-indexed).
    frames = frame_glob(frames_dir)
    if not frames:
        print("export_trace: ERROR no frames captured — was the window reached "
              "before --max-frames? Check the anchor/offset.", file=sys.stderr)
        return 1
    captured_nums = []
    for p in frames:
        digits = "".join(c for c in p.stem if c.isdigit())
        captured_nums.append(int(digits))
    captured_set = set(captured_nums)

    # Trim the player-pos-log to exactly the captured frames so
    # (frame count == meta line count) holds, keyed by frame number.
    meta_by_frame: dict[int, dict] = {}
    if meta_path.exists():
        for ln in meta_path.read_text().splitlines():
            s = ln.strip()
            if not s:
                continue
            try:
                o = json.loads(s)
            except json.JSONDecodeError:
                continue
            if "frame" in o and int(o["frame"]) in captured_set:
                meta_by_frame[int(o["frame"])] = o
    # rewrite meta.jsonl in captured-frame order (one row per frame; rows with no
    # pos-log entry — e.g. a non-INGAME frame — get a minimal stub)
    with meta_path.open("w") as f:
        for n in captured_nums:
            row = meta_by_frame.get(n, {"frame": n})
            f.write(json.dumps(row) + "\n")

    # Whole-trace blob. trace_jsonl is the runnable ops array (the round-trip
    # source); rng_seed_at_start + anchor_offset make the window reconstructable.
    global_blob = {
        "schema": "openrecet-trace-v1",
        "rng_seed_at_start": rng_seed,
        "anchor_offset": cr_start,
        "caprange": [cr_start, cr_count],
        "trace_jsonl": ops,
        "source_raw": src.name,
        "scenario": args.scenario,
        "name": args.name,
        "fps": args.fps,
    }
    global_path.write_text(json.dumps(global_blob, indent=2) + "\n")

    n_frames = len(frames)
    n_meta = sum(1 for _ in meta_path.read_text().splitlines() if _.strip())
    print(f"export_trace: {n_frames} frames, {n_meta} meta rows "
          f"(caprange count {cr_count}), rng_seed={rng_seed} → {run_dir}",
          file=sys.stderr)
    if n_frames != n_meta:
        print("export_trace: NOTE frame/meta count mismatch (some frames had no "
              "INGAME pos-log row)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
