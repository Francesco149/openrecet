#!/usr/bin/env python3
"""
tools/export_trace.py — run a TAS trace and export a contiguous frame window
plus per-frame + whole-trace metadata, ready to push to the llm-feed as a
`trace` card (tools/../../llm-feed/feed.py trace --dir <run-dir>).

This is the producer side of the frame-by-frame trace viewer
(see docs/trace-workflow.md). It:

  1. Resolves a runnable segtrace from either a distilled `.trace.jsonl` or a
     RAW recording (openrecet-trace-*.raw.jsonl → distilled via distill_trace,
     --house-segtrace to wrap the new-game→HOUSE intro).
  2. Injects a {caprange:[start,count]} op (anchor-relative, jitter-immune) so
     the port captures the whole window in one shot — bypassing the 32-frame
     CAPTURE_FRAMES_MAX (Phase 1).
  3. Drives the port via run-openrecet.sh with --capture-to (PNG frames).
  4. Writes the export dir:
        <run-dir>/frames/frame_NNNNN.png    every frame in the window
        <run-dir>/meta.jsonl                one {"frame":N,"frame_abs":M} per frame
                                            (anchor-relative renumber index; the
                                            old per-frame px/oct/rng columns now
                                            live in the flow-trace — see
                                            docs/flow-trace-cheatsheet.md)
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
import trace_save             # noqa: E402  (TAS save virtualization, shared with scenario-test)
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


def resolve_trace(src: Path, house_segtrace: bool,
                  force_flat: bool = False) -> tuple[str, list[dict], dict | None]:
    """Return (jsonl_text, ops, raw_savefile) for the runnable segtrace.

    raw_savefile is the {path,sha256,size} dict of a RAW recording's boot save
    (None for an already-distilled trace, whose {savefile} op is resolved from its
    own dir). main() gzips+embeds it into the work trace so save resolution is
    uniform across both inputs — mirroring distill_trace.main.

    A RAW recording carrying {anchor} rows is ANCHOR-GATED by default
    (emit_anchor_segments): every recorded anchor becomes a {wait} sync point, so
    the {caprange} we append below lands in the FINAL segment and is resolved as
    `last_anchor_frame + start` — i.e. capture-index 0 == the last recorded
    anchor's frame, jitter-immune. Without this the caprange anchors to boot
    (frame 0) and turbo load-stretch drifts the captured window run-to-run (the
    frame_0186↔frame_00500 ambiguity). --house-segtrace forces the legacy boot→HOUSE
    wrap; force_flat forces the old flat emit (caprange anchored to boot). An
    already-distilled trace is used verbatim."""
    if is_raw_recording(src):
        changes, caps, escs, cts, total, rng_seed, anchors, savefile, _save_writes = \
            distill_trace.load_raw(str(src))
        if not changes:
            raise SystemExit(f"export_trace: no input frames in {src}")
        if house_segtrace:
            text = distill_trace.emit_house_segtrace(changes, caps, escs, cts, rng_seed)
        elif anchors and not force_flat:
            text = distill_trace.emit_anchor_segments(
                changes, caps, escs, cts, total, anchors, rng_seed)
        else:
            if not anchors:
                print("export_trace: WARNING raw carries no {anchor} rows — FLAT "
                      "fallback (caprange anchored to boot; capture-index drifts "
                      "with turbo load jitter). Re-record with the anchor-logging "
                      "build for stable frame refs.", file=sys.stderr)
            text = distill_trace.emit_flat(changes, caps, escs, cts, total, rng_seed)
        ops = [json.loads(l) for l in text.splitlines()
               if l.strip() and not l.startswith("#")]
        if not house_segtrace and anchors and not force_flat:
            last_anchor = next((o["wait"] for o in reversed(ops) if "wait" in o), None)
            print(f"export_trace: anchor-gated ({len(anchors)} anchor rows) — "
                  f"caprange relative to the last anchor "
                  f"({last_anchor or '?'}); frame refs are jitter-immune",
                  file=sys.stderr)
        return text, ops, savefile
    # already distilled (its {savefile} op, if any, is resolved from src's dir)
    return src.read_text(), load_ops(src), None


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
    ap.add_argument("--flat", action="store_true",
                    help="force the legacy FLAT distil (caprange anchored to boot); "
                         "by default a RAW recording with {anchor} rows is "
                         "anchor-gated so frame refs are jitter-immune")
    ap.add_argument("--name", default="", help="human label for the trace")
    ap.add_argument("--scenario", default="", help="scenario id (free-form, → global)")
    ap.add_argument("--fps", type=int, default=20, help="playback fps hint (→ global)")
    ap.add_argument("--d3d-trace", action="store_true",
                    help="also capture a per-draw d3d_trace.jsonl over the "
                         "caprange window (→ tools/d3d_state_diff.py)")
    ap.add_argument("--d3d-trace-verts", action="store_true",
                    help="with --d3d-trace, also capture each immediate-mode "
                         "draw's FVF-decodable vertex bytes (→ "
                         "tools/render_diff.py --explain). Mirror on the "
                         "retail side with frida_capture.py --d3d-trace-verts.")
    ap.add_argument("--call-trace", action="store_true",
                    help="also capture the flow-trace call_trace.jsonl over the "
                         "{calltrace} window (→ tools/flow_diff.py). The trace's "
                         "{calltrace} op arms the window; this wires the output.")
    ap.add_argument("--anchor-record", action="store_true",
                    help="also log EVERY anchor firing (absolute engine frames) to "
                         "<run-dir>/anchors.jsonl — the port-side anchor stream for "
                         "the trace-studio timeline.")
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

    text, ops, raw_savefile = resolve_trace(src, args.house_segtrace, force_flat=args.flat)
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

    # A RAW source carries its boot save as a {savefile:{path,sha256,size}} row
    # (an uncompressed .bin beside the recording), NOT a gzipped content-addressed
    # blob — so trace_save.resolve_save can't read it directly. Mirror
    # distill_trace.main: gzip it into the work trace's _saves/ store and embed a
    # proper {savefile} ref into the work trace, so save resolution is uniform with
    # the already-distilled path. (Without this, raw→export crashed on the load_raw
    # unpack, then would have errored trying to gunzip the raw .bin.)
    save_trace = src                       # where resolve_save reads the ref from
    if raw_savefile:
        raw_save = (src.resolve().parent / raw_savefile["path"])
        if raw_save.exists():
            store = trace_save.default_store_dir(work_path)
            sha, blob = trace_save.store_save(raw_save, store,
                                              sha=raw_savefile.get("sha256"))
            trace_save.embed_in_trace(
                work_path, trace_save._rel_ref(work_path, blob), sha=sha)
            save_trace = work_path
            print(f"export_trace: embedded raw boot save {sha[:12]}… → "
                  f"{work_path.name}", file=sys.stderr)
        else:
            print(f"export_trace: WARNING raw save {raw_save} missing — running "
                  "without --save-override (distil from the recording's dir to "
                  "embed it)", file=sys.stderr)

    # Drive the port. run-openrecet.sh rewrites the unix paths to the Windows
    # paths the exe's fopen needs and auto-converts the BMP frames → PNG.
    cmd = [
        str(ROOT / "tools/run-openrecet.sh"),
        "--timeout-ms", str(args.timeout_ms),
        "--hidden", "--turbo", "--silent-audio",
        "--input-segtrace", str(work_path),
        "--capture-to", str(frames_dir),
        "--max-frames", str(args.max_frames),
    ]
    # TAS save virtualization: resolve the trace's {savefile} ref exactly like
    # scenario-test, else a trace that wants a fresh boot (@fresh) silently uses
    # the on-disk save.dat and the replay diverges (e.g. a HOUSE intro scenario
    # skips the dialogues straight to freeroam, never reaching the capture
    # window).  Writes always go to a per-run sandbox so a replay can't clobber
    # the real save.dat.
    #   @fresh  → --save-fresh   (boot with no save; fresh menu)
    #   <blob>  → --save-override (decompressed embedded save)
    #   (none)  → on-disk save.dat (legacy)
    # NB run-openrecet.sh does NOT path-rewrite the --save-* flags (unlike
    # --capture-to / --input-segtrace), so hand it the Windows path the exe's
    # fopen needs directly (wslpath -w is not idempotent — can't let the wrapper
    # re-convert).
    def _winpath(p: Path) -> str:
        return subprocess.run(["wslpath", "-w", str(p)],
                              capture_output=True, text=True, check=True).stdout.strip()
    save_ref = trace_save.resolve_save(save_trace)
    if save_ref == trace_save.FRESH_REF:
        cmd += ["--save-fresh"]
    elif save_ref:
        cmd += ["--save-override", _winpath(Path(save_ref))]
    save_out_dir = run_dir / "saveout"
    save_out_dir.mkdir(parents=True, exist_ok=True)
    cmd += ["--save-write-dir", _winpath(save_out_dir)]
    if args.d3d_trace:
        # The {caprange} op also arms the d3d-trace window (main.c
        # segtrace_caprange_cb → d3d_trace_set_window), so the trace emits
        # exactly the captured frames, anchor-relative.
        cmd += ["--d3d-trace", str(run_dir / "d3d_trace.jsonl")]
        if args.d3d_trace_verts:
            cmd += ["--d3d-trace-verts"]
    if args.call_trace:
        # The {calltrace} op (already in the trace) arms call_trace_arm_window as
        # the segtrace replays; this flag tells the engine where to write it.
        cmd += ["--call-trace", str(run_dir / "call_trace.jsonl")]
    if args.anchor_record:
        # Every anchor firing (absolute g_tick.frame_count) → run_dir/anchors.jsonl.
        cmd += ["--anchor-trace-record", str(run_dir / "anchors.jsonl")]
    print(f"export_trace: driving port → {run_dir}", file=sys.stderr)
    print("export_trace:   " + " ".join(cmd), file=sys.stderr)
    rc = subprocess.run(cmd, cwd=str(ROOT)).returncode
    if rc != 0:
        print(f"export_trace: WARNING run-openrecet exited {rc}", file=sys.stderr)

    # Collect the captured frames (PNG, frame-indexed by ABSOLUTE engine frame).
    frames = frame_glob(frames_dir)
    if not frames:
        print("export_trace: ERROR no frames captured — was the window reached "
              "before --max-frames? Check the anchor/offset.", file=sys.stderr)
        return 1
    abs_by_path: list[tuple[int, "Path"]] = []
    for p in frames:
        digits = "".join(c for c in p.stem if c.isdigit())
        abs_by_path.append((int(digits), p))
    abs_by_path.sort(key=lambda t: t[0])
    captured_abs = [n for n, _ in abs_by_path]
    captured_set = set(captured_abs)

    # ── Jitter-immune renumbering ───────────────────────────────────────────
    # The engine names frames by ABSOLUTE g_tick.frame_count, which drifts
    # run-to-run with turbo boot/load stretch (the anchor that bases the caprange
    # fires at a different absolute frame each run). The SIM is bit-exact by
    # anchor-relative index, so we renormalise: subtract the first captured frame
    # so files+meta become 0-based ANCHOR-RELATIVE indices. frame_00186 is then
    # the same sim instant in every replay of the same trace — a crop reference
    # `frame=f=186` is instantly findable regardless of jitter. (In --flat mode
    # base_abs is the boot offset, so this is a harmless near-no-op renumber.)
    base_abs = captured_abs[0]
    rel_of = {n: n - base_abs for n in captured_abs}

    # Read the player-pos-log (keyed by absolute frame) before we renumber.
    meta_by_abs: dict[int, dict] = {}
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
                meta_by_abs[int(o["frame"])] = o

    # Rename the PNGs to frame_<rel>.<ext>. rel < abs (base_abs >= first frame),
    # so ascending order never collides with a not-yet-renamed source.
    for n, p in abs_by_path:
        rel = rel_of[n]
        target = p.with_name(f"frame_{rel:05d}{p.suffix}")
        if target != p:
            p.rename(target)

    # Rewrite meta.jsonl in anchor-relative order: frame = rel index, and keep the
    # absolute engine frame as frame_abs for traceability. Rows with no INGAME
    # pos-log entry get a minimal stub.
    with meta_path.open("w") as f:
        for n in captured_abs:
            rel = rel_of[n]
            row = dict(meta_by_abs.get(n, {}))
            row["frame"] = rel
            row["frame_abs"] = n
            f.write(json.dumps(row) + "\n")

    # The final {wait} anchor the caprange is based on (informational/debug).
    final_anchor = next((o["wait"] for o in reversed(ops) if "wait" in o), None)

    # Whole-trace blob. trace_jsonl is the runnable ops array (the round-trip
    # source); rng_seed_at_start + the anchor base make the window reconstructable.
    # frame_base = absolute frame of frame_00000 (jittery; for debug only — refs
    # use the anchor-relative index which is what the filenames now encode).
    global_blob = {
        "schema": "openrecet-trace-v1",
        "rng_seed_at_start": rng_seed,
        "anchor_offset": cr_start,
        "caprange": [cr_start, cr_count],
        "frames_anchor_relative": True,
        "final_anchor": final_anchor,
        "frame_base_abs": base_abs,
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
