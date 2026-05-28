#!/usr/bin/env python3
"""
tools/mem_watch.py — Phase D.7 memory-access watch driver.

Finds the *writer* of an engine memory region we can't locate by reading
the decompile (reached via indirect dispatch, or dropped from Ghidra's
output). Frida's MemoryAccessMonitor write-protects the region in retail;
every write traps with the faulting instruction's address. That address
→ the owning engine function (via the port ledger) → the chip to port.

This is the unblock path for the HOUSE shop_table render gap: the render
walker reads a region whose filler isn't ported, so the table draws
stale/zero. Point mem_watch at that region, drive retail to HOUSE with
--auto-z-spam, and read off the writer VA. See docs/plans/d7-mem-watch.md.

Usage:

    tools/mem_watch.py \\
        --run-dir runs/memwatch-shoptable \\
        --region 0x73d... :64:shop_table_slots \\
        [--access w] [--max-frames 4000] [--duration-ms 120000]

  --region VA:SIZE[:LABEL]   repeatable. VA is a Ghidra VA (ImageBase
                             0x00400000). SIZE in bytes. Page-granular —
                             a tight SIZE still watches the enclosing
                             page(s); keep it small to limit hot-page
                             noise.

The agent arms the monitor pre-resume (before the engine runs), so an
init-time writer during HOUSE bootstrap is trapped on its first write.

Output:
    <run_dir>/mem_watch.jsonl    one row per trapped access
    <run_dir>/agent.log          Frida send(log)/errors
    stdout                       a single JSON object: ranked writer table,
                                 each writer mapped to its owning engine
                                 function + port status (classifier-clean —
                                 see memory feedback_classifier_clean_output)
"""

from __future__ import annotations

import argparse
import bisect
import json
import sys
from pathlib import Path

# Reuse the Phase B capture plumbing wholesale.
sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida_capture as fc  # noqa: E402

ROOT          = fc.ROOT
LEDGER_JSON   = ROOT / "docs" / "port-ledger.json"
FUNCTIONS_CSV = ROOT / "docs" / "decompiled" / "functions.csv"


# ─── ledger / function-table lookup ────────────────────────────────────────


def _load_function_index() -> list[tuple[int, int, dict]]:
    """Build a sorted [(start_va, end_va, info)] interval list of every
    engine function, so a faulting instruction VA can be mapped to its
    containing function by a single binary search.

    Prefers port-ledger.json (carries port status + src paths); falls
    back to functions.csv (name/size only) if the ledger is absent."""
    intervals: list[tuple[int, int, dict]] = []
    if LEDGER_JSON.exists():
        ledger = json.loads(LEDGER_JSON.read_text())
        for fn in ledger.get("functions", []):
            start = int(fn["va"], 16) if isinstance(fn["va"], str) else int(fn["va"])
            size  = int(fn.get("size", 0)) or 1
            intervals.append((start, start + size, {
                "name":   fn.get("name", ""),
                "status": fn.get("status", "unknown"),
                "src":    fn.get("src", []),
            }))
    elif FUNCTIONS_CSV.exists():
        import csv
        with FUNCTIONS_CSV.open() as f:
            for row in csv.DictReader(f):
                start = int(row["entry"], 16)
                size  = int(row["size"]) or 1
                intervals.append((start, start + size, {
                    "name":   row["name"],
                    "status": "unknown",
                    "src":    [],
                }))
    else:
        raise SystemExit(
            f"neither {LEDGER_JSON} nor {FUNCTIONS_CSV} found — "
            f"regenerate the ledger with tools/gen_port_ledger.py")

    intervals.sort(key=lambda iv: iv[0])
    return intervals


def _owner_of(va: int, intervals: list[tuple[int, int, dict]]) -> dict | None:
    """The function whose [start, end) range contains `va`, or None
    (CRT / library / unmapped code)."""
    starts = [iv[0] for iv in intervals]
    i = bisect.bisect_right(starts, va) - 1
    if i < 0:
        return None
    start, end, info = intervals[i]
    if start <= va < end:
        out = dict(info)
        out["func_va"]    = f"0x{start:08x}"
        out["insn_off"]   = va - start   # offset of the faulting insn into fn
        return out
    return None


# ─── region-spec parsing ────────────────────────────────────────────────────


def _parse_region(spec: str) -> dict:
    """`VA:SIZE[:LABEL]` → {va, size, label}."""
    parts = spec.split(":")
    if len(parts) < 2:
        raise argparse.ArgumentTypeError(
            f"--region expects VA:SIZE[:LABEL], got {spec!r}")
    va   = int(parts[0], 0)
    size = int(parts[1], 0)
    label = parts[2] if len(parts) >= 3 and parts[2] else f"0x{va:08x}"
    return {"va": va, "size": size, "label": label}


# ─── post-processing: rank trapped writes by faulting instruction ──────────


def _rank(run_dir: Path, regions: list[dict]) -> dict:
    jsonl = run_dir / "mem_watch.jsonl"
    intervals = _load_function_index()
    region_by_idx = {i: r for i, r in enumerate(regions)}

    # Group by faulting instruction VA.
    by_from: dict[int, dict] = {}
    total = 0
    if jsonl.exists():
        for line in jsonl.read_text().splitlines():
            line = line.strip()
            if not line:
                continue
            ev = json.loads(line)
            total += 1
            frm = int(ev["from"])
            g = by_from.setdefault(frm, {
                "from":         frm,
                "ops":          {},          # op -> count
                "n":            0,
                "first_frame":  ev.get("frame"),
                "regions":      set(),
                "offsets":      set(),       # accessed offset into the region
            })
            g["n"] += 1
            g["ops"][ev["op"]] = g["ops"].get(ev["op"], 0) + 1
            ridx = int(ev.get("region", 0))
            g["regions"].add(ridx)
            base = region_by_idx.get(ridx, {}).get("va")
            if base is not None:
                g["offsets"].add(int(ev["addr"]) - base)

    writers = []
    for frm, g in sorted(by_from.items(), key=lambda kv: -kv[1]["n"]):
        owner = _owner_of(frm, intervals)
        offs = sorted(g["offsets"])
        writers.append({
            "from_va":      f"0x{frm:08x}",
            "n_accesses":   g["n"],
            "ops":          g["ops"],
            "first_frame":  g["first_frame"],
            "regions":      sorted(g["regions"]),
            "sample_offsets": [(f"+0x{o:x}" if o >= 0 else f"-0x{-o:x}")
                               for o in offs[:12]],
            "owner_func":   owner["func_va"]   if owner else None,
            "owner_name":   owner["name"]      if owner else None,
            "owner_status": owner["status"]    if owner else "unmapped",
            "owner_src":    owner["src"]       if owner else [],
            "insn_offset":  (f"+0x{owner['insn_off']:x}"
                             if owner else None),
        })

    # The chip(s) to port = writers whose owning function isn't already
    # ported/verified. Surface them at the top of the summary.
    candidates = [w for w in writers
                  if w["owner_status"] in ("unported", "stubbed", "unmapped")]
    return {
        "total_accesses": total,
        "distinct_writers": len(writers),
        "regions": [{"index": i, **r} for i, r in enumerate(regions)],
        "writers": writers,
        "port_candidates": candidates,
    }


# ─── cli ────────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--run-dir", type=Path, required=True,
                    help="where to write mem_watch.jsonl + agent.log")
    ap.add_argument("--region", type=_parse_region, action="append",
                    required=True, metavar="VA:SIZE[:LABEL]",
                    help="watched region (Ghidra VA). Repeatable.")
    ap.add_argument("--access", choices=("w", "rw"), default="w",
                    help="trap writes only (default) or reads+writes. "
                         "'rw' floods on hot pages — use only when the "
                         "reader is what's unknown.")
    ap.add_argument("--no-precise", action="store_true",
                    help="raw one-shot-per-page mode. By default the agent "
                         "re-arms on page-neighbor traps and records only "
                         "accesses inside the watched field, so an unrelated "
                         "write elsewhere on the 4KiB page can't mask the "
                         "writer. Disable only to see the raw page traffic.")
    ap.add_argument("--remote", default=fc.DEFAULT_REMOTE,
                    help="frida-server host:port (default %(default)s)")
    ap.add_argument("--exe", type=Path, default=fc.RETAIL_EXE)
    ap.add_argument("--cwd", type=Path, default=fc.ASSET_CWD)
    ap.add_argument("--max-frames", type=int, default=4000,
                    help="stop after this many rendered frames "
                         "(default %(default)s — generous, HOUSE entry "
                         "is tens of seconds past the intro video)")
    ap.add_argument("--duration-ms", type=int, default=180_000,
                    help="wall-clock ceiling (default %(default)s)")
    ap.add_argument("--no-auto-z-spam", action="store_true",
                    help="don't auto-press Z to drive past the intro "
                         "cutscene (use when the region is written before "
                         "HOUSE and you don't need to advance scenes)")
    ap.add_argument("--no-turbo", action="store_true",
                    help="don't virtualise the frame clock. The intro "
                         "video is DirectShow and isn't sped by turbo "
                         "anyway, but turbo still spins the post-video "
                         "loop faster.")
    ap.add_argument("--no-auto-start", action="store_true",
                    help="skip auto-launching frida-server.exe")
    ap.add_argument("--analyze-only", action="store_true",
                    help="skip the capture; just re-rank an existing "
                         "<run_dir>/mem_watch.jsonl (for iterating on the "
                         "cross-reference without re-driving retail)")
    args = ap.parse_args(argv)

    regions = [{**r, "access": args.access} for r in args.region]
    args.run_dir.mkdir(parents=True, exist_ok=True)

    if not args.analyze_only:
        cfg = fc.CaptureConfig(
            capture_frames=[],
            max_frames=args.max_frames,
            duration_ms=args.duration_ms,
            remote=args.remote, exe=args.exe, cwd=args.cwd,
            auto_start_server=not args.no_auto_start,
            hide_window=True,
            turbo=not args.no_turbo,
            silent_audio=True,
            auto_z_spam=not args.no_auto_z_spam,
            mem_watch=True,
            mem_watch_regions=regions,
            mem_watch_precise=not args.no_precise,
        )
        result = fc._run_capture_impl(cfg, args.run_dir)
        # Capture diagnostics go to the log, not stdout — stdout stays a
        # single JSON object (classifier-clean).
        (args.run_dir / "mem_watch_run.json").write_text(json.dumps({
            "exit_code":         result.exit_code,
            "elapsed_ms":        result.elapsed_ms,
            "last_engine_frame": result.last_engine_frame,
        }, indent=2))

    summary = _rank(args.run_dir, regions)
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
