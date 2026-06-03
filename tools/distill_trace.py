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

ROOT = Path(__file__).resolve().parent.parent
HOUSE_INTRO_REF = ROOT / "tests/scenarios/house-wall-collide/trace.jsonl"
HOUSE_ANCHOR_OFFSET = 1565   # recording frame 0 → anchor+1565 (idle spam ends +1500)


def load_raw(path):
    masks = {}   # frame -> "0xNNNN"
    caps = []
    escs = []    # relative frames an ESC keypress was recorded (dialogue-skip)
    cts = []     # call-trace windows: [start, len] (F4 toggle pairs)
    anchors = [] # recorded anchor firings: {name, frame(rel), gframe, rng}
    rng_seed = None   # live LCG state snapshotted at record-start (header field)
    for ln in Path(path).read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        o = json.loads(s)
        if "_rec" in o:
            if o.get("rng_seed_at_start") is not None:
                rng_seed = int(o["rng_seed_at_start"]) & 0xffffffff
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
        return [], sorted(caps), sorted(escs), cts, 0, rng_seed, anchors
    n = max(masks) + 1
    # distil: emit a change-point whenever the mask differs from the previous
    series = [masks.get(i, "0x0000") for i in range(n)]
    changes = []
    prev = None
    for i, m in enumerate(series):
        if m != prev:
            changes.append((i, m))
            prev = m
    return changes, sorted(caps), sorted(escs), cts, n, rng_seed, anchors


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


def pick_anchor(anchors, name):
    """Return the chosen recorded anchor firing of `name` (the LAST one — for a
    re-entrant anchor that's the final/settled occurrence), or None."""
    hits = [a for a in anchors if a["name"] == name]
    return hits[-1] if hits else None


def emit_anchor_rebase(changes, caps, escs, cts, total, rng_seed, anchor,
                       pin_gframe=False):
    """Split a recording at a recorded anchor so everything AFTER it replays
    anchor-relative (a `{wait:NAME}` then frames rebased by the anchor's relative
    frame) — a 100%-reproducible offset regardless of boot/load jitter. Inputs
    BEFORE the anchor drive the navigation flat (they only need to *reach* the
    anchor; menus/dialogue wait for input). RNG is re-pinned at the anchor to the
    LCG state recorded THERE (anchor['rng']) so post-anchor RNG (dust/NPC) is
    reproducible and faithful; falls back to the record-start seed if absent.

    `anchor` is one entry from load_raw()'s anchors list. With pin_gframe, also
    emit a {gframe:[0, anchor_gframe]} op (experimental — pins the global frame
    counter so frame-count-derived state like the time-of-day HUD clock matches)."""
    ar = anchor["frame"]                       # anchor's relative frame
    name = anchor["name"]
    pin_rng = anchor["rng"] if anchor["rng"] is not None else rng_seed

    out = [f"# anchor-rebased at {name} (rel frame {ar}, gframe "
           f"{anchor['gframe']}, rng {pin_rng}); post-anchor inputs are "
           f"{name}-relative for a jitter-immune offset."]

    # ── segment 0: pre-anchor navigation, flat (drives to the anchor) ──
    if rng_seed is not None:
        out.append(json.dumps({"rngseed": [0, rng_seed]}))
    for f, m in changes:
        if f < ar:
            out.append(json.dumps({"frame": f, "buttons": m}))
    for e in escs:
        if e < ar:
            out.append(json.dumps({"esc": e}))
    # a trailing release so seg-0 doesn't end mid-hold while we wait for the anchor
    out.append(json.dumps({"frame": max(ar - 1, 0), "buttons": "0x0000"}))

    # ── wait for the anchor, then everything after it, rebased to anchor+0 ──
    out.append(json.dumps({"wait": name}))
    if pin_gframe and anchor["gframe"] is not None:
        out.append(json.dumps({"gframe": [0, anchor["gframe"]]}))
    if pin_rng is not None:
        out.append(json.dumps({"rngseed": [0, pin_rng]}))
    out.append(json.dumps({"frame": 0, "buttons": "0x0000"}))
    for f, m in changes:
        if f >= ar:
            out.append(json.dumps({"frame": f - ar, "buttons": m}))
    for c in caps:
        if c >= ar:
            out.append(json.dumps({"capture": c - ar}))
    for e in escs:
        if e >= ar:
            out.append(json.dumps({"esc": e - ar}))
    for start, length in cts:
        if start >= ar:
            out.append(json.dumps({"calltrace": [start - ar, length]}))
    if changes and changes[-1][1] != "0x0000":
        out.append(json.dumps({"frame": total - ar, "buttons": "0x0000"}))
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("raw", help="raw recording (openrecet-trace-*.raw.jsonl)")
    ap.add_argument("-o", "--out", help="output path (default stdout)")
    ap.add_argument("--house-segtrace", action="store_true",
                    help="wrap as a bootable new-game→HOUSE segtrace")
    ap.add_argument("--anchor-rebase", metavar="NAME",
                    help="split at a recorded anchor (e.g. FREEROAM_START): inputs "
                         "after it become anchor-relative (jitter-immune offset), "
                         "RNG re-pinned at the anchor. Requires the raw recording "
                         "to carry {anchor} rows (recorder ≥ anchor-logging build).")
    ap.add_argument("--pin-gframe", action="store_true",
                    help="with --anchor-rebase, also pin the global frame counter "
                         "at the anchor (experimental — for frame-count-derived "
                         "state like the time-of-day HUD clock).")
    args = ap.parse_args(argv)

    changes, caps, escs, cts, total, rng_seed, anchors = load_raw(args.raw)
    if not changes:
        print("distill_trace: no input frames found in", args.raw, file=sys.stderr)
        return 1
    if args.anchor_rebase:
        a = pick_anchor(anchors, args.anchor_rebase)
        if a is None:
            names = sorted({x["name"] for x in anchors})
            print(f"distill_trace: anchor {args.anchor_rebase!r} not in recording "
                  f"(have: {', '.join(names) or 'none'}). Re-record with the "
                  f"anchor-logging build.", file=sys.stderr)
            return 1
        text = emit_anchor_rebase(changes, caps, escs, cts, total, rng_seed, a,
                                  pin_gframe=args.pin_gframe)
    elif args.house_segtrace:
        text = emit_house_segtrace(changes, caps, escs, cts, rng_seed)
    else:
        text = emit_flat(changes, caps, escs, cts, total, rng_seed)
    if args.out:
        Path(args.out).write_text(text)
        seedmsg = (f", rng_seed {rng_seed}" if rng_seed is not None
                   else ", no rng_seed (pre-rngseed recording)")
        print(f"distill_trace: {len(changes)} change-points, {len(caps)} capture(s), "
              f"{len(escs)} esc(s), {len(cts)} call-trace window(s), "
              f"{len(anchors)} anchor(s), {total} frames{seedmsg} → {args.out}",
              file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
