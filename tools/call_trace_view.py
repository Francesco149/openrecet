#!/usr/bin/env python3
"""
tools/call_trace_view.py — per-frame view of a Frida call_trace.jsonl.

Reads <run_dir>/call_trace.jsonl produced by `frida_capture.py --call-trace`,
filters to one frame, and prints an ordered table cross-referenced against
Ghidra's function list and the current port tree (`src/`).

The "ported?" column is intentionally heuristic: a function is marked
ported if grep finds its VA-as-hex or its `FUN_<offset>` mangled name
anywhere under src/.  False negatives are possible (someone may have
ported a function and renamed it without leaving an address comment),
so treat the column as a hint, not ground truth.

Usage:
    nix develop --command python3 tools/call_trace_view.py \\
        --run-dir /tmp/openrecet-calltrace-full/run \\
        --frame 5
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
from collections import OrderedDict
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def load_functions_csv(path: Path) -> dict[int, str]:
    """va (int) -> ghidra name."""
    out: dict[int, str] = {}
    with path.open() as f:
        for row in csv.DictReader(f):
            try:
                va = int(row["entry"], 16)
            except (KeyError, ValueError):
                continue
            out[va] = row.get("name", "")
    return out


def port_status(va: int, src_dir: Path) -> str | None:
    """Return the first src file that mentions the VA hex or FUN_NNN
    mangled name, or None if no port found.  Uses ripgrep for speed."""
    hex_lower = f"0x{va:x}"
    hex_upper = f"0x{va:X}"
    fun_name  = f"FUN_{va:08x}"
    fun_upper = f"FUN_{va:08X}"
    patterns  = [hex_lower, hex_upper, fun_name, fun_upper]
    # union pattern, ripgrep -lF for fixed-string OR (use -e to add)
    cmd = ["rg", "-l", "--no-messages",
           "-e", hex_lower, "-e", hex_upper, "-e", fun_name, "-e", fun_upper,
           str(src_dir)]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=20)
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None
    if r.returncode != 0 or not r.stdout.strip():
        return None
    # take the shortest path (least specific) and relativize
    paths = [Path(line) for line in r.stdout.splitlines() if line.strip()]
    paths.sort(key=lambda p: (len(p.parts), str(p)))
    return str(paths[0].relative_to(ROOT)) if paths else None


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", type=Path, required=True,
        help="capture run dir containing call_trace.jsonl")
    ap.add_argument("--frame", type=int, default=None,
        help="frame number to inspect; default = first frame with events")
    ap.add_argument("--functions-csv", type=Path,
        default=ROOT / "docs" / "decompiled" / "functions.csv",
        help="ghidra functions.csv for VA → name lookup")
    ap.add_argument("--src-dir", type=Path, default=ROOT / "src",
        help="root of the port to grep for VA references")
    ap.add_argument("--format", choices=["md", "json"], default="md")
    ap.add_argument("--limit", type=int, default=0,
        help="cap displayed rows (0 = no cap)")
    args = ap.parse_args(argv)

    jsonl = args.run_dir / "call_trace.jsonl"
    if not jsonl.exists():
        print(f"error: {jsonl} not found", file=sys.stderr)
        return 1

    rows: list[dict] = []
    with jsonl.open() as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rows.append(json.loads(line))
    if not rows:
        print("error: call_trace.jsonl is empty", file=sys.stderr)
        return 1

    # pick frame
    by_frame: dict[int, list[dict]] = {}
    for r in rows:
        by_frame.setdefault(int(r["frame"]), []).append(r)
    if args.frame is None:
        frame = min(by_frame.keys())
    else:
        frame = args.frame
        if frame not in by_frame:
            print(f"error: no events for frame {frame}. available: "
                  f"{sorted(by_frame.keys())[:20]}{'...' if len(by_frame) > 20 else ''}",
                  file=sys.stderr)
            return 1

    events = by_frame[frame]
    fns = load_functions_csv(args.functions_csv)

    # dedup by va while preserving first-seen order — that's the "what
    # was called this frame" set; per-call counts come from the full list
    counts: dict[int, int] = {}
    first_seen: "OrderedDict[int, dict]" = OrderedDict()
    for ev in events:
        va = int(ev["va"])
        counts[va] = counts.get(va, 0) + 1
        if va not in first_seen:
            first_seen[va] = ev

    if args.format == "json":
        out_rows = []
        for i, (va, ev) in enumerate(first_seen.items()):
            if args.limit and i >= args.limit:
                break
            out_rows.append({
                "order":      i,
                "va":         va,
                "va_hex":     f"0x{va:x}",
                "ghidra":     fns.get(va, ""),
                "n_calls":    counts[va],
                "first_ret":  int(ev.get("ret_va", -1)),
                "first_ts_ms": int(ev.get("ts", 0)),
                "port":       port_status(va, args.src_dir),
            })
        print(json.dumps({
            "run_dir": str(args.run_dir),
            "frame":   frame,
            "n_total_calls":  len(events),
            "n_unique":       len(first_seen),
            "rows":           out_rows,
        }, indent=2))
        return 0

    # markdown
    print(f"# Frame {frame} — call trace")
    print()
    print(f"run_dir: `{args.run_dir}`  ")
    print(f"calls: **{len(events)}** total, **{len(first_seen)}** unique "
          f"function entries")
    print()
    print("| # | calls | va | ghidra | port | first caller |")
    print("|--:|------:|----|--------|------|--------------|")
    for i, (va, ev) in enumerate(first_seen.items()):
        if args.limit and i >= args.limit:
            print(f"| … | … | (truncated to --limit {args.limit}) | | | |")
            break
        ret = int(ev.get("ret_va", -1))
        # ret_va is module-relative; add IMAGE_BASE 0x400000 for display
        ret_abs = (ret + 0x400000) if ret >= 0 else 0
        port = port_status(va, args.src_dir) or "—"
        ghidra = fns.get(va, "")
        print(f"| {i+1} | {counts[va]} | `0x{va:x}` | `{ghidra}` | "
              f"{port} | `0x{ret_abs:x}` |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
