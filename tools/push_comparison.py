#!/usr/bin/env python3
"""
tools/push_comparison.py — build a scenario's port|retail diff atlases and push
them to the llm-feed as a `comparison` item (click-to-reveal amplified diff,
like runs/comparisons/index.html, but in the live feed).

The diff math (tools/comparison_page.amplified_diff, PIL/numpy) runs here; the
feed only renders the pre-built atlases + geometry. Run from the openrecet nix
shell (needs PIL/numpy):

    nix develop --command python3 tools/push_comparison.py house-movement \
        --note "free-roam: port frozen vs retail walking"
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import comparison_page as cp

ROOT     = Path(__file__).resolve().parent.parent
RUNS     = ROOT / "runs" / "scenarios"
SCEN_DIR = ROOT / "tests" / "scenarios"
FEED_PY  = Path("/opt/src/llm-feed/feed.py")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", help="scenario name under tests/scenarios/")
    ap.add_argument("--title", default=None)
    ap.add_argument("--note", default="")
    ap.add_argument("--amp", type=float, default=cp.AMP_DEFAULT,
                    help=f"diff amplification (default {cp.AMP_DEFAULT})")
    args = ap.parse_args(argv)

    sp = SCEN_DIR / args.scenario
    if not (sp / "trace.jsonl").exists():
        print(f"push_comparison: no such scenario: {sp}", file=sys.stderr)
        return 1
    is_seg, _ = cp.inspect_trace(sp / "trace.jsonl")

    run_dir = cp.latest_both_run(args.scenario, RUNS)
    if run_dir is None:
        print(f"push_comparison: no --target both run for {args.scenario}; "
              f"run scenario-test.py {args.scenario} --target both first.",
              file=sys.stderr)
        return 1

    dest = Path(tempfile.mkdtemp(prefix=f"feedcmp_{args.scenario}_"))
    caps = cp.build_scenario_atlases(run_dir, dest, is_segtrace=is_seg, amp=args.amp)
    panels = [{
        "atlas":     str(dest / c["rel"]),
        "label":     c["label"],
        "row0_pct":  c["row0_pct"],
        "total_pct": c["total_pct"],
        "differ_px": c["differ_px"],
        "meanabs":   c["meanabs"],
    } for c in caps if c.get("rel")]
    if not panels:
        print("push_comparison: no panels (need both openrecet + retail captures)",
              file=sys.stderr)
        return 1

    spec = {
        "title":       args.title or f"{args.scenario} · port | retail",
        "note":        args.note or f"src={run_dir.name}",
        "left_label":  "openrecet",
        "right_label": "retail",
        "panels":      panels,
    }
    spec_path = dest / "spec.json"
    spec_path.write_text(json.dumps(spec))

    # feed.py is stdlib-only, so the same interpreter runs it fine.
    return subprocess.run(
        [sys.executable, str(FEED_PY), "comparison", "--spec", str(spec_path),
         "--title", spec["title"], "--note", spec["note"]],
    ).returncode


if __name__ == "__main__":
    sys.exit(main())
