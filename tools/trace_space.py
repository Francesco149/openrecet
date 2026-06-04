#!/usr/bin/env python3
"""tools/trace_space.py — stretch a distilled segtrace's inputs for replay headroom.

A TEMPORARY robustness hack (pending real menu anchors): the recorded input
change-points fire too tightly for the replay to keep up — a MENU action lands
before the previous animation/menu has settled, so the {wait:ANCHOR} after it
stalls. We add headroom by spacing the IDLE GAPS between presses — but NOT the
held inputs, because stretching a held DIRECTION changes how far the character
walks (it then never reaches the interact spot). Per the rule:

  * a stretch held for MORE than 2 frames (a deliberate hold — movement, a held
    direction) is PRESERVED exactly; and
  * a stretch held for ≤2 frames (a tap, or an idle gap between presses) gets
    `--gap` frames of headroom added AFTER it.

So movement stays frame-exact (reaches the sword), while the rapid menu taps get
breathing room. Per anchor segment; {esc}/{capture}/{caprange} frames are
remapped onto the stretched timeline; {wait}/{rngseed}/{savefile} untouched
(anchors re-sync each segment, so the stretch is local and jitter-immune).

Usage:
    tools/trace_space.py IN.trace.jsonl -o OUT.trace.jsonl --gap 60
    tools/trace_space.py IN -o OUT --gap 60 --hold-thresh 2
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def _mask(b) -> int:
    return int(b, 16) if isinstance(b, str) else int(b)


def space_trace(lines: list[str], gap: int) -> list[str]:
    """Inject idle frames into the gaps where NO inputs are pressed (mask==0),
    preserving every held-input stretch exactly. Per anchor segment: walk the
    input change-points; when a change-point's mask is 0 (an idle gap), push all
    subsequent ops in the segment later by `gap` (= extend that idle wait). A
    non-zero mask (a press / held direction — movement) adds no shift, so its
    duration to the next change-point is untouched. esc/capture/caprange frames
    ride the accumulated shift; wait/rngseed/savefile are untouched."""
    out: list[str] = []
    shift = 0  # accumulated idle injected so far in the current segment

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
            shift = 0                      # new segment: reset
            out.append(json.dumps(o))
        elif "buttons" in o and "frame" in o:
            # Emit at the shifted frame. If this stretch holds NO inputs (mask 0),
            # extend it by `gap` (inject an idle wait); held inputs are preserved.
            out.append(json.dumps({"frame": int(o["frame"]) + shift,
                                   "buttons": o["buttons"]}))
            if _mask(o["buttons"]) == 0:
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
