#!/usr/bin/env python3
"""
tools/call_trace_diff.py — Phase E.2 port-side ↔ retail call-trace diff.

Reads two call_trace.jsonl files (one from each side):

  retail JSONL — produced by `tools/frida_capture.py --call-trace`.
                 Schema: {"va": int, "ret_va": int, "frame": int, "ts": int}
                 — `va` is the Ghidra VA (already 0x004xxxxx), `ret_va` is
                 module-relative (add 0x00400000 for the Ghidra VA), `frame`
                 is the retail sim-frame counter.

  port JSONL   — produced by `openrecet.exe --call-trace <path>`.
                 Same schema modulo `ts`.  `va` is the engine Ghidra VA the
                 port function corresponds to (declared by the
                 CALL_TRACE_ENTER(0xVA) probe macro at the top of each
                 instrumented port function — see src/call_trace.h).

For each (retail_frame, port_frame) pair the script computes:

  • overlap     — set of VAs both sides called
  • retail-only — VAs retail called that port did not.  These are either
                  unported engine functions (= work to do) or ported
                  functions we forgot to annotate with CALL_TRACE_ENTER.
  • port-only   — VAs port called that retail did not.  Structural
                  divergence: either the port enters a code path retail
                  doesn't, or a CALL_TRACE_ENTER probe got an incorrect
                  Ghidra-VA argument.

Frame alignment is one-to-one by default (retail_frame == port_frame).
For non-matching scenarios (most HOUSE captures), pass `--retail-frame N
--port-frame M` to align a single frame on each side, OR pass
`--retail-frame-offset DELTA` to shift retail's frame numbers by DELTA
before matching (e.g. retail HOUSE-entry at frame 11775 vs port
HOUSE-entry at frame 240 → --retail-frame-offset -11535).

Default output is a compact summary; pass `--verbose` to dump the
VA-by-VA breakdown with Ghidra names from docs/decompiled/functions.csv.

CLI:
    nix develop --command python3 tools/call_trace_diff.py \\
        --retail runs/calltrace-house/call_trace.jsonl \\
        --port   runs/openrecet-calltrace/call_trace.jsonl

    # specific frame on each side:
    tools/call_trace_diff.py … --retail-frame 11890 --port-frame 320

    # verbose VA breakdown:
    tools/call_trace_diff.py … --verbose

Exit code: 0 always.  This is a diagnostic, not a gate; use it to
prioritise the next port.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


# ── loaders ───────────────────────────────────────────────────────────────


def load_trace(path: Path) -> dict[int, Counter[int]]:
    """frame -> Counter[va] (per-frame call counts).  Both emitter schemas
    are accepted (Frida agent emits `ts` too; we ignore it)."""
    by_frame: dict[int, Counter[int]] = {}
    with path.open() as f:
        for line_no, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                row = json.loads(line)
            except json.JSONDecodeError as e:
                raise SystemExit(
                    f"{path}:{line_no}: malformed JSONL — {e}") from None
            frame = int(row["frame"])
            va    = int(row["va"])
            by_frame.setdefault(frame, Counter())[va] += 1
    return by_frame


def load_ghidra_names(path: Path) -> dict[int, str]:
    """va -> Ghidra symbol name.  Returns {} if the CSV is missing."""
    if not path.exists():
        return {}
    out: dict[int, str] = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            try:
                va = int(row["entry"], 16)
            except (KeyError, ValueError):
                continue
            out[va] = row.get("name", "")
    return out


# ── diff core ─────────────────────────────────────────────────────────────


def diff_frames(retail: Counter[int],
                port:   Counter[int]) -> dict[str, list[tuple[int, int, int]]]:
    """Returns {overlap, retail_only, port_only} each as
    [(va, retail_count, port_count)] sorted by va asc."""
    all_vas = sorted(set(retail) | set(port))
    overlap, ronly, ponly = [], [], []
    for va in all_vas:
        rc, pc = retail.get(va, 0), port.get(va, 0)
        if rc and pc:
            overlap.append((va, rc, pc))
        elif rc:
            ronly.append((va, rc, 0))
        elif pc:
            ponly.append((va, 0, pc))
    return {"overlap": overlap, "retail_only": ronly, "port_only": ponly}


# ── output ────────────────────────────────────────────────────────────────


def fmt_va(va: int, names: dict[int, str]) -> str:
    name = names.get(va, "")
    base = f"0x{va:06x}"
    return f"{base} {name}" if name else base


def print_summary(retail_frame: int, port_frame: int,
                  retail: Counter[int], port: Counter[int],
                  diff: dict, names: dict[int, str],
                  verbose: bool) -> None:
    nr, np_ = sum(retail.values()), sum(port.values())
    ur, up = len(retail), len(port)
    n_overlap = len(diff["overlap"])
    n_ronly   = len(diff["retail_only"])
    n_ponly   = len(diff["port_only"])
    print(f"# Call-trace diff — retail frame {retail_frame} vs "
          f"port frame {port_frame}")
    print()
    print(f"retail: {nr:5d} calls, {ur:3d} unique VAs")
    print(f"port:   {np_:5d} calls, {up:3d} unique VAs")
    print()
    print(f"  overlap (called on both):    {n_overlap}")
    print(f"  retail-only (port missing):  {n_ronly}")
    print(f"  port-only (retail skipped):  {n_ponly}")
    if not verbose:
        print()
        print("(--verbose for VA-by-VA breakdown)")
        return
    print()
    if diff["overlap"]:
        print("## Overlap (va | retail × | port ×)")
        for va, rc, pc in diff["overlap"]:
            mark = " " if rc == pc else "≠"
            print(f"  {mark} {fmt_va(va, names):42s}  retail={rc:4d}  port={pc:4d}")
        print()
    if diff["retail_only"]:
        print("## Retail-only (= port-side gap)")
        for va, rc, _ in diff["retail_only"]:
            print(f"    {fmt_va(va, names):42s}  retail={rc:4d}")
        print()
    if diff["port_only"]:
        print("## Port-only (= structural divergence or wrong VA tag)")
        for va, _, pc in diff["port_only"]:
            print(f"    {fmt_va(va, names):42s}  port={pc:4d}")
        print()


# ── cli ───────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--retail", type=Path, required=True,
                    help="retail-side call_trace.jsonl (from frida_capture.py)")
    ap.add_argument("--port",   type=Path, required=True,
                    help="port-side call_trace.jsonl (from openrecet.exe --call-trace)")
    ap.add_argument("--retail-frame", type=int, default=None,
                    help="pin retail to this frame; default = first frame "
                         "with events on both sides after offset shift")
    ap.add_argument("--port-frame",   type=int, default=None,
                    help="pin port to this frame; default = same as resolved "
                         "retail frame minus --retail-frame-offset")
    ap.add_argument("--retail-frame-offset", type=int, default=0,
                    help="DELTA added to retail frame numbers before "
                         "matching (so port_frame == retail_frame + DELTA)")
    ap.add_argument("--functions-csv", type=Path,
                    default=ROOT / "docs" / "decompiled" / "functions.csv",
                    help="Ghidra functions.csv for VA→name decoration")
    ap.add_argument("--verbose", action="store_true",
                    help="dump every VA in overlap/retail-only/port-only")
    args = ap.parse_args(argv)

    retail_by_f = load_trace(args.retail)
    port_by_f   = load_trace(args.port)
    if not retail_by_f:
        print(f"error: retail trace {args.retail} has no events", file=sys.stderr)
        return 2
    if not port_by_f:
        print(f"error: port trace {args.port} has no events", file=sys.stderr)
        return 2

    # Frame resolution.  Allow either pin to be unset and derive it from
    # the other via the offset; if both are unset, pick the first retail
    # frame whose shifted-counterpart exists on the port side.
    if args.retail_frame is not None and args.port_frame is not None:
        rf, pf = args.retail_frame, args.port_frame
    elif args.retail_frame is not None:
        rf = args.retail_frame
        pf = rf + args.retail_frame_offset
    elif args.port_frame is not None:
        pf = args.port_frame
        rf = pf - args.retail_frame_offset
    else:
        rf = None
        for cand in sorted(retail_by_f.keys()):
            if (cand + args.retail_frame_offset) in port_by_f:
                rf = cand
                break
        if rf is None:
            print("error: no overlapping (retail, port) frame pair found; "
                  "try --retail-frame / --port-frame to pin manually",
                  file=sys.stderr)
            return 2
        pf = rf + args.retail_frame_offset

    if rf not in retail_by_f:
        print(f"error: retail frame {rf} has no events. "
              f"first available: {min(retail_by_f)}, last: {max(retail_by_f)}",
              file=sys.stderr)
        return 2
    if pf not in port_by_f:
        print(f"error: port frame {pf} has no events. "
              f"first available: {min(port_by_f)}, last: {max(port_by_f)}",
              file=sys.stderr)
        return 2

    names = load_ghidra_names(args.functions_csv)
    diff  = diff_frames(retail_by_f[rf], port_by_f[pf])
    print_summary(rf, pf, retail_by_f[rf], port_by_f[pf],
                  diff, names, args.verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
