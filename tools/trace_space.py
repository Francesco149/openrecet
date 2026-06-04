#!/usr/bin/env python3
"""tools/trace_space.py — stretch a distilled segtrace's inputs for replay headroom.

A TEMPORARY robustness hack (pending real menu anchors): the recorded input
change-points fire too tightly for the replay to keep up — an action (open a
menu, press Z on a display) lands before the player has settled/arrived, so a
menu never opens and the {wait:ANCHOR} after it stalls. Spacing the inputs out
(holding each state ~`--gap` frames longer) gives the player ample time to reach
each menu/display before the next action.

Per anchor segment, each input {frame} change-point is pushed later by an
accumulating `gap` (so every held state — a walk, a stop, a Z hold — lasts
longer); {esc}/{capture}/{caprange} frames are remapped onto the stretched
timeline. {wait}/{rngseed}/{savefile} ops are untouched (anchors re-sync each
segment, so the stretch is local and jitter-immune). Over-walking is safe in the
collision-bounded shop (the player pins against the wall/display).

Usage:
    tools/trace_space.py IN.trace.jsonl -o OUT.trace.jsonl --gap 60
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def space_trace(lines: list[str], gap: int) -> list[str]:
    out: list[str] = []
    shift = 0  # accumulated frame shift within the current segment

    def remap_frame_op(o: dict, key: str) -> dict:
        o = dict(o)
        o[key] = int(o[key]) + shift
        return o

    for ln in lines:
        s = ln.strip()
        if not s or s.startswith("#"):
            out.append(ln)
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            out.append(ln)
            continue

        if "wait" in o:
            shift = 0                      # new segment: reset the stretch
            out.append(json.dumps(o))
        elif "buttons" in o and "frame" in o:
            # An input change-point: emit at the shifted frame, then widen the
            # gap so every SUBSEQUENT op in this segment is pushed further.
            out.append(json.dumps(remap_frame_op(o, "frame")))
            shift += gap
        elif "esc" in o:
            out.append(json.dumps({"esc": int(o["esc"]) + shift}))
        elif "capture" in o:
            out.append(json.dumps({"capture": int(o["capture"]) + shift}))
        elif "caprange" in o and isinstance(o["caprange"], list):
            st, cnt = int(o["caprange"][0]), int(o["caprange"][1])
            out.append(json.dumps({"caprange": [st + shift, cnt]}))
        else:
            # rngseed / savefile / gframe / phasepin / calltrace — untouched.
            out.append(json.dumps(o))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="distilled segtrace (.trace.jsonl)")
    ap.add_argument("-o", "--out", required=True, help="output trace path")
    ap.add_argument("--gap", type=int, default=60,
                    help="frames of headroom added per change-point (default 60 = ~1s)")
    args = ap.parse_args(argv)

    lines = Path(args.trace).read_text().splitlines()
    out = space_trace(lines, args.gap)
    Path(args.out).write_text("\n".join(out) + "\n")
    n_in = sum(1 for l in lines if '"buttons"' in l)
    print(f"trace_space: stretched {n_in} change-points by gap={args.gap} → {args.out}",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
