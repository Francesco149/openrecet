#!/usr/bin/env python3
# tools/distill_trace.py — distil a RAW in-engine TAS recording (from the F2
# recorder in src/main.c) into the sparse change-point trace format the port +
# Frida harness replay (input_trace.h / input_segtrace.h).
#
# The recorder writes one {"frame":k,"buttons":"0xNN"} row PER FRAME (relative to
# the F2 press) plus {"capture":k} rows for the F3 points.  Distilling collapses
# every run of identical buttons into a single change-point ("hold this until the
# next change-point") — i.e. "same input for N frames" → one row.
#
# Usage:
#   # flat distilled trace (relative frames, single segment, base 0):
#   python3 tools/distill_trace.py openrecet-trace-1234-0.raw.jsonl
#   python3 tools/distill_trace.py REC.raw.jsonl -o out.trace.jsonl
#
#   # bootable HOUSE segtrace (prepends the new-game→HOUSE intro + 2×
#   # HOUSE_FREEROAM anchor from house-wall-collide, rebases the recording to
#   # anchor+OFFSET so it replays from boot on both targets):
#   python3 tools/distill_trace.py REC.raw.jsonl --house-segtrace \
#       -o tests/scenarios/house-walk-tables/trace.jsonl
#
# Default output is stdout.
import argparse, json, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import trace_save

ROOT = Path(__file__).resolve().parent.parent
HOUSE_INTRO_REF = ROOT / "tests/scenarios/house-wall-collide/trace.jsonl"
HOUSE_ANCHOR_OFFSET = 1565   # recording frame 0 → anchor+1565 (idle spam ends +1500)

# Cosmetic/FX/collapse-prone anchors whose RELATIVE ORDER DRIFTS between the
# frame-dropped real-time recording and the deterministic turbo replay (see
# findings/cutscene-replay-anchor-drift.md).  In an AUTO-PLAY cutscene region
# (deterministic reveal/anim carried by a held/tapped fast-forward button, no
# timing-sensitive player choices) these are NOT reliable `{wait}` sync points —
# waiting on one that already fired deadlocks the replay.  --drop-fragile-after /
# --drop-fragile-region drop them as syncs in the named region, keeping only the
# reliable scene/state BOUNDARIES (which re-sync + re-pin RNG at each scene change;
# the deterministic auto-play + carried input hold the timeline between them).
FRAGILE_ANCHORS = frozenset({
    "CONV_POSE_BLINK",
    "EXTRA_SPRITE_START", "EXTRA_SPRITE_FADED_IN", "EXTRA_SPRITE_FADEOUT",
    "EXTRA_SPRITE_END",
    "TEXT_ANIM_START", "TEXT_ANIM_END",
    "DLG_LINE_SHOW", "DLG_LINE_CLEAR",
    "LOADING_START", "LOADING_END",
})


def load_raw(path):
    masks = {}   # frame -> "0xNNNN"
    caps = []
    escs = []    # relative frames an ESC keypress was recorded (dialogue-skip)
    cts = []     # call-trace windows: [start, len] (F4 toggle pairs)
    anchors = [] # recorded anchor firings: {name, frame(rel), gframe, rng}
    rng_seed = None   # live LCG state snapshotted at record-start (header field)
    savefile = None   # {path, sha256, size} of the boot save snapshot, if recorded
    save_writes = []  # in-session saves the game wrote during the recording
    for ln in Path(path).read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        o = json.loads(s)
        if "_rec" in o:
            if o.get("rng_seed_at_start") is not None:
                rng_seed = int(o["rng_seed_at_start"]) & 0xffffffff
            continue
        if "savefile" in o:
            savefile = {
                "path": str(o["savefile"]),
                "sha256": (str(o["sha256"]) if o.get("sha256") else None),
                "size": (int(o["size"]) if o.get("size") is not None else None),
            }
            continue
        if "save_write" in o:
            sw = o["save_write"]
            save_writes.append({
                "index": int(sw.get("index", len(save_writes))),
                "frame": int(sw.get("frame", 0)),
                "path": str(sw["file"]),
                "sha256": (str(sw["sha256"]) if sw.get("sha256") else None),
            })
            continue
        if "anchor" in o:
            anchors.append({
                "name": str(o["anchor"]),
                "frame": int(o.get("frame", 0)),
                "gframe": int(o["gframe"]) if o.get("gframe") is not None else None,
                "rng": (int(o["rng"]) & 0xffffffff) if o.get("rng") is not None else None,
            })
        elif "buttons" in o and "frame" in o:
            masks[int(o["frame"])] = o["buttons"]
        elif "capture" in o:
            caps.append(int(o["capture"]))
        elif "esc" in o:
            escs.append(int(o["esc"]))
        elif "calltrace" in o:
            v = o["calltrace"]
            cts.append([int(v[0]), int(v[1])] if isinstance(v, list)
                       else [0, int(v)])
    if not masks:
        return [], sorted(caps), sorted(escs), cts, 0, rng_seed, anchors, savefile, save_writes
    n = max(masks) + 1
    # distil: emit a change-point whenever the mask differs from the previous
    series = [masks.get(i, "0x0000") for i in range(n)]
    changes = []
    prev = None
    for i, m in enumerate(series):
        if m != prev:
            changes.append((i, m))
            prev = m
    return changes, sorted(caps), sorted(escs), cts, n, rng_seed, anchors, savefile, save_writes


def emit_flat(changes, caps, escs, cts, total, rng_seed=None):
    out = []
    # Re-pin the LCG to the live state at record-start so the recording's
    # RNG-driven behaviour reproduces on playback (foot-dust jitter, NPC motion).
    if rng_seed is not None:
        out.append(json.dumps({"rngseed": [0, rng_seed]}))
    for f, m in changes:
        out.append(json.dumps({"frame": f, "buttons": m}))
    for c in caps:
        out.append(json.dumps({"capture": c}))
    for e in escs:
        out.append(json.dumps({"esc": e}))
    for start, length in cts:
        out.append(json.dumps({"calltrace": [start, length]}))
    # a trailing release so the trace doesn't end mid-hold
    if changes and changes[-1][1] != "0x0000":
        out.append(json.dumps({"frame": total, "buttons": "0x0000"}))
    return "\n".join(out) + "\n"


def emit_house_segtrace(changes, caps, escs, cts, rng_seed=None):
    """Prepend the proven new-game→HOUSE intro (segments 0+1 + the segment-2
    spam up to the 2nd HOUSE_FREEROAM + frame 1500), then the recording rebased
    to anchor+HOUSE_ANCHOR_OFFSET, then the recorded captures rebased."""
    ref = HOUSE_INTRO_REF.read_text().splitlines()
    # The reference's seg-2 tail is: {"capture":1540}, the directional hold(s),
    # {"capture":...}, trailing release.  Strip those (any directional 'buttons'
    # row with frame >= HOUSE_ANCHOR_OFFSET, and any 'capture' op) — keep the
    # boot/anchor structure + the spam up to frame 1500.
    prefix = ['# new-game→HOUSE intro + 2× HOUSE_FREEROAM anchor (from '
              'house-wall-collide); recording rebased to anchor+%d.'
              % HOUSE_ANCHOR_OFFSET]
    for ln in ref:
        s = ln.strip()
        if not s or s.startswith("#"):
            continue   # drop the reference scenario's prose comments
        o = json.loads(s)
        if "capture" in o:
            continue
        if "buttons" in o and int(o.get("frame", 0)) >= HOUSE_ANCHOR_OFFSET:
            continue
        prefix.append(ln)
    out = list(prefix)
    off = HOUSE_ANCHOR_OFFSET
    # Re-pin the LCG to the record-start state at the recorded segment's first
    # frame (base+off) — BEFORE the recorded inputs — so the recording's
    # RNG-driven behaviour reproduces regardless of how much RNG the prepended
    # intro consumed.  The recorded segment is anchored at the 2nd HOUSE_FREEROAM
    # wait, so off is relative to that anchor.
    if rng_seed is not None:
        out.append(json.dumps({"rngseed": [off, rng_seed]}))
    out.append(json.dumps({"capture": off - 25}))   # idle cap just before motion
    for f, m in changes:
        out.append(json.dumps({"frame": off + f, "buttons": m}))
    for c in caps:
        out.append(json.dumps({"capture": off + c}))
    # ESC presses (dialogue-skip) rebase by the same offset so they fire at the
    # same anchor-relative instant the recording captured them.
    for e in escs:
        out.append(json.dumps({"esc": off + e}))
    # call-trace windows are anchor-relative within the final segment, so the
    # start rebases by the same offset; the length is unchanged.
    for start, length in cts:
        out.append(json.dumps({"calltrace": [off + start, length]}))
    return "\n".join(out) + "\n"


def _held_mask_at(changes, frame):
    """The button mask held at `frame` = the last change-point with f <= frame."""
    held = "0x0000"
    for f, m in changes:
        if f <= frame:
            held = m
        else:
            break
    return held


def _in_drop_regions(frame, regions):
    """True if `frame` falls in any [lo, hi] auto-play drop region (inclusive)."""
    return any(lo <= frame <= hi for lo, hi in regions)


def _suggest_autoplay_boundary(changes, total, min_hold=240):
    """Heuristic HINT (printed, not authoritative): the first frame at which the
    input stops being a dense interactive tap-cluster and becomes sparse holds — a
    likely auto-play cutscene start.  Returns the frame where the first held run of
    >= min_hold frames begins, or None.  The caller still confirms the semantic
    boundary (interactive first-customer vs auto-play) by hand."""
    cp = [f for f, _ in changes]
    for i in range(len(cp)):
        hi = cp[i + 1] if i + 1 < len(cp) else total + 1
        if hi - cp[i] >= min_hold:
            return cp[i]
    return None


def emit_anchor_segments(changes, caps, escs, cts, total, anchors, rng_seed,
                         pin_rng=True, pin_gframe=False, drop_regions=None):
    """Convert a recording that carries recorded anchor firings into an
    ANCHOR-GATED segtrace: every recorded anchor becomes a `{wait:NAME}` sync
    point, and all inputs/escs/captures between two anchors are emitted relative
    to the preceding one. The segtrace's spam-until-anchor + base mechanism then
    re-syncs the timeline at each anchor, so the trace replays correctly under
    turbo/load jitter WITHOUT rebasing anything by hand — an ESC `N frames after a
    dialogue anchor` always fires N frames after that anchor actually lands, and a
    walk after FREEROAM_START always starts at the same sim instant.

    Same-frame anchors are deduped (one sync point per distinct frame, first-named
    wins); BOOT is dropped (it's the implicit start). With pin_rng (default), each
    anchor re-pins the LCG to the value recorded THERE — this erases the extra RNG
    the stretched turbo load consumed, so post-anchor RNG (dust/NPC) is faithful
    and reproducible. pin_gframe additionally pins the global frame counter
    (EXPERIMENTAL). Captures: the producer (export_trace caprange) adds the window
    relative to the final anchor; recorded {capture}s are also carried per-segment.

    drop_regions (list of (lo, hi) frame spans): inside these AUTO-PLAY cutscene
    regions, FRAGILE_ANCHORS are NOT emitted as `{wait}` sync points — only the
    reliable scene/state boundaries survive.  This folds the former hand
    drop_fragile.py into the distiller (findings/cutscene-replay-anchor-drift.md):
    fragile cosmetic/FX anchors drift under turbo and deadlock the replay, so a
    held/tapped auto-play region syncs only on its boundaries and lets the
    deterministic auto-play + carried input hold the timeline between them.  The
    port + retail still EMIT the dropped anchors (the viewer still labels them by
    identity); the trace just no longer WAITS on them."""
    drop_regions = drop_regions or []
    # distinct-frame sync points, in frame order
    seen, syncs = set(), []
    dropped_fragile = 0
    for a in sorted(anchors, key=lambda x: x["frame"]):
        if a["name"] == "BOOT" or a["frame"] in seen:
            continue
        if a["name"] in FRAGILE_ANCHORS and _in_drop_regions(a["frame"], drop_regions):
            dropped_fragile += 1
            continue   # fragile anchor in an auto-play region — not a sync point
        seen.add(a["frame"])
        syncs.append(a)
    if drop_regions:
        print(f"distill_trace: dropped {dropped_fragile} fragile {{wait}}s in "
              f"{len(drop_regions)} auto-play region(s); {len(syncs)} sync(s) kept.",
              file=sys.stderr)

    out = ["# anchor-segmented replay: each recorded anchor is a {wait} sync point;"
           " inputs/escs after it are relative to it (turbo/jitter-immune)."]

    def emit_window(lo, hi, first):
        """Emit inputs/escs/caps/cts in (lo, hi], rebased by -lo. `first` seeds the
        held-mask baseline at frame 0 so a hold spanning `lo` carries.

        The upper bound is INCLUSIVE (`<= hi`): an input-TRIGGERED anchor (e.g.
        PAUSE_OPEN, opened by a Z) fires on the SAME frame as the triggering press,
        so the press sits exactly at `hi`. Excluding it (the old `< hi`) dropped
        the trigger — on replay the player reached the menu but never pressed the
        button, so the anchor never fired and the {wait} stalled. Including it lets
        the replay press the button → cause the anchor → resolve the wait. (For
        state-triggered anchors the frame-`hi` input is just the held state
        continuing; the next segment's baseline carries it, so no double-apply.)"""
        out.append(json.dumps({"frame": 0, "buttons": _held_mask_at(changes, lo)
                               if not first else "0x0000"}))
        for f, m in changes:
            if lo < f <= hi:
                out.append(json.dumps({"frame": f - lo, "buttons": m}))
        for e in escs:
            if lo < e <= hi:
                out.append(json.dumps({"esc": e - lo}))
        for c in caps:
            if lo <= c < hi:
                out.append(json.dumps({"capture": c - lo}))
        for s, l in cts:
            if lo <= s < hi:
                out.append(json.dumps({"calltrace": [s - lo, l]}))

    # segment 0: boot → first anchor (flat from frame 0)
    if rng_seed is not None:
        out.append(json.dumps({"rngseed": [0, rng_seed]}))
    first_hi = syncs[0]["frame"] if syncs else total + 1
    emit_window(0, first_hi, first=True)

    for i, a in enumerate(syncs):
        out.append(json.dumps({"wait": a["name"]}))
        if pin_gframe and a["gframe"] is not None:
            out.append(json.dumps({"gframe": [0, a["gframe"]]}))
        if pin_rng and a["rng"] is not None:
            out.append(json.dumps({"rngseed": [0, a["rng"]]}))
        lo = a["frame"]
        hi = syncs[i + 1]["frame"] if i + 1 < len(syncs) else total + 1
        emit_window(lo, hi, first=False)
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", help="raw recording (openrecet-trace-*.raw.jsonl)")
    ap.add_argument("-o", "--out", help="output path (default stdout)")
    ap.add_argument("--house-segtrace", action="store_true",
                    help="wrap as a bootable new-game→HOUSE segtrace")
    ap.add_argument("--anchor-segments", action="store_true",
                    help="emit an ANCHOR-GATED segtrace: every recorded anchor "
                         "becomes a {wait} sync point and inputs/escs after it are "
                         "relative to it, so the trace replays correctly under "
                         "turbo/load jitter with no hand-rebasing. Requires the raw "
                         "to carry {anchor} rows (recorder ≥ anchor-logging build). "
                         "Default RNG-pins at each anchor to the recorded value.")
    ap.add_argument("--no-pin-rng", action="store_true",
                    help="with --anchor-segments, do NOT re-pin RNG at each anchor.")
    ap.add_argument("--pin-gframe", action="store_true",
                    help="with --anchor-segments, also pin the global frame counter "
                         "at each anchor (experimental — for frame-count-derived "
                         "state like the time-of-day HUD clock).")
    ap.add_argument("--drop-fragile-after", type=int, metavar="FRAME",
                    help="with --anchor-segments, from FRAME onward drop FRAGILE "
                         "(cosmetic/FX) anchors as {wait} sync points — the auto-play "
                         "cutscene region past the interactive part. Keeps only "
                         "reliable scene boundaries (folds the old drop_fragile.py; "
                         "findings/cutscene-replay-anchor-drift.md).")
    ap.add_argument("--drop-fragile-region", action="append", default=[],
                    metavar="LO:HI",
                    help="with --anchor-segments, drop FRAGILE anchors in the frame "
                         "span LO:HI (repeatable). Use for bounded auto-play regions.")
    ap.add_argument("--saves-dir",
                    help="content store for the embedded save blob (default: the "
                         "trace's _saves/ store, shared across scenarios).")
    ap.add_argument("--no-savefile", action="store_true",
                    help="do not carry the recording's boot save into the trace.")
    args = ap.parse_args(argv)

    changes, caps, escs, cts, total, rng_seed, anchors, savefile, save_writes = load_raw(args.raw)
    if not changes:
        print("distill_trace: no input frames found in", args.raw, file=sys.stderr)
        return 1
    if args.anchor_segments:
        if not anchors:
            print("distill_trace: no {anchor} rows in recording — re-record with "
                  "the anchor-logging build (recorder ≥ 2026-06-03).", file=sys.stderr)
            return 1
        drop_regions = []
        for spec in args.drop_fragile_region:
            lo, hi = spec.split(":")
            drop_regions.append((int(lo), int(hi)))
        if args.drop_fragile_after is not None:
            drop_regions.append((args.drop_fragile_after, total + 1))
        if not drop_regions:
            hint = _suggest_autoplay_boundary(changes, total)
            if hint is not None:
                print(f"distill_trace: HINT — input turns sparse (likely auto-play "
                      f"cutscene) at frame {hint}; pass --drop-fragile-after {hint} "
                      f"to drop drift-prone fragile syncs past the interactive part.",
                      file=sys.stderr)
        text = emit_anchor_segments(changes, caps, escs, cts, total, anchors,
                                    rng_seed, pin_rng=not args.no_pin_rng,
                                    pin_gframe=args.pin_gframe,
                                    drop_regions=drop_regions)
    elif args.house_segtrace:
        text = emit_house_segtrace(changes, caps, escs, cts, rng_seed)
    else:
        text = emit_flat(changes, caps, escs, cts, total, rng_seed)
    if args.out:
        Path(args.out).write_text(text)
        # Carry the recorded boot save into the distilled trace: content-address +
        # gzip it into the trace's _saves/ store and embed the {savefile} ref. The
        # raw's savefile path is relative to the RAW file's directory.
        save_msg = ""
        if savefile and not args.no_savefile:
            raw_save = (Path(args.raw).resolve().parent / savefile["path"])
            if raw_save.exists():
                store = (Path(args.saves_dir).resolve() if args.saves_dir
                         else trace_save.default_store_dir(args.out))
                sha, blob = trace_save.store_save(raw_save, store,
                                                  sha=savefile.get("sha256"))
                ref = trace_save._rel_ref(args.out, blob)
                trace_save.embed_in_trace(args.out, ref, sha=sha)
                save_msg = f", save {sha[:12]}…→{ref}"
            else:
                save_msg = (f", SAVE MISSING ({raw_save} not found — re-run "
                            f"distill from the recording's dir)")
        # In-session saves the recording captured (req: multiple saves per trace).
        # Fold each into the content store and write a <out>.saves.json sidecar
        # manifest ({index, frame, ref, sha}). Replay reproduces the saves live
        # (the sandbox virtualization), so these aren't replay ops — they're the
        # recorded ground truth for divergence verification (compare a replay's
        # sandbox writes to these). Kept out of trace.jsonl so no parser needs a
        # new op.
        if save_writes and not args.no_savefile:
            store = (Path(args.saves_dir).resolve() if args.saves_dir
                     else trace_save.default_store_dir(args.out))
            manifest = []
            for sw in save_writes:
                raw_sw = (Path(args.raw).resolve().parent / sw["path"])
                if not raw_sw.exists():
                    save_msg += f", SAVE_WRITE#{sw['index']} MISSING ({raw_sw})"
                    continue
                sha, blob = trace_save.store_save(raw_sw, store, sha=sw.get("sha256"))
                manifest.append({"index": sw["index"], "frame": sw["frame"],
                                 "ref": trace_save._rel_ref(args.out, blob),
                                 "sha256": sha})
            if manifest:
                side = Path(args.out).with_suffix(Path(args.out).suffix + ".saves.json")
                side.write_text(json.dumps(manifest, indent=2) + "\n")
                save_msg += f", {len(manifest)} in-session save(s)→{side.name}"
        seedmsg = (f", rng_seed {rng_seed}" if rng_seed is not None
                   else ", no rng_seed (pre-rngseed recording)")
        print(f"distill_trace: {len(changes)} change-points, {len(caps)} capture(s), "
              f"{len(escs)} esc(s), {len(cts)} call-trace window(s), "
              f"{len(anchors)} anchor(s), {total} frames{seedmsg}{save_msg} → {args.out}",
              file=sys.stderr)
    else:
        sys.stdout.write(text)
        if savefile:
            print("distill_trace: recording carries a savefile but output is stdout "
                  "— pass -o to embed it (content store needs a trace location).",
                  file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
