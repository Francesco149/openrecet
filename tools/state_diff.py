#!/usr/bin/env python3
"""tools/state_diff.py — ST-04 first-divergence state report (CLI).

Drill into WHY the `state` (volatile) pillar diverged: localize the first logical
frame whose port↔retail volatile-state Merkle root differs and print the full
diagnostic — the divergent leaf path, typed values + raw canonical bits, the last
matching frame, the value transition across that boundary (which side changed the
field, and whether the port missed/spurious/wrong-applied it), and every co-divergent
leaf. The diagnostic sibling of `parity_state.py` (which writes the pillar's
PASS/FAIL metrics for the proof bundle); this reads the SAME `--state` view.json.

Flow:

    # 1. capture a --state window (bakes per-frame state into view.json)
    orv3_window.py <scenario> --window OFF:COUNT --state --view
    # 2. localize + explain the first divergence
    state_diff.py <scenario> --window OFF:COUNT [--json] [--all-frames]

Exit (roadmap §4.1): 0 PASS (no divergence), 1 FAIL (divergence localized),
2 NOT_CAPTURED / INCONCLUSIVE / fatal.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import parity_prove  # noqa: E402  (contract/window helpers, ProveError)
from parity import ObservationError, load_required  # noqa: E402
from parity.observations import FAIL, PASS  # noqa: E402
from parity.state_diff import render_text, report_from_view_json  # noqa: E402

# verdict → §4.1 tool exit code.
_EXIT = {PASS: 0, FAIL: 1}


def _window_dir(args) -> Path:
    if args.from_window:
        return args.from_window
    if args.window:
        off, _, count = args.window.partition(":")
        return ROOT / "runs/studio-v3-windows" / args.scenario / f"win-{off}-{count}"
    raise parity_prove.ProveError("pass --window OFF:COUNT or --from-window <dir>")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="ST-04 first-divergence state report")
    ap.add_argument("scenario")
    ap.add_argument("--window", default=None, metavar="OFF:COUNT",
                    help="the v3 window runs/studio-v3-windows/<scen>/win-OFF-COUNT")
    ap.add_argument("--from-window", type=Path, default=None, help="an explicit v3 window dir")
    ap.add_argument("--contract", type=Path, default=None,
                    help="external contract yaml (default: the scenario.yaml proof block)")
    ap.add_argument("--all-frames", action="store_true",
                    help="scan every both-sided state frame, ignoring the contract window")
    ap.add_argument("--json", action="store_true", help="print the report as JSON")
    args = ap.parse_args(argv)

    try:
        window_dir = _window_dir(args)
        if not window_dir.is_dir():
            raise parity_prove.ProveError(f"window dir not found: {window_dir}")
        view = window_dir / "view.json"
        if not view.exists():
            raise parity_prove.ProveError(
                f"no view.json in {window_dir} — capture it first "
                f"(orv3_window {args.scenario} --window … --state --view)")

        required = None
        if not args.all_frames:
            pairs = window_dir / "pairs.json"
            contract_doc = parity_prove.load_scenario_contract(args.scenario, contract_path=args.contract)
            window = parity_prove.contract_window(contract_doc["proof"])
            if pairs.exists():
                try:
                    required = load_required(pairs, window)
                except ObservationError:
                    required = None

        report = report_from_view_json(view, required=required)
    except (parity_prove.ProveError, ObservationError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1), file=sys.stderr)
        else:
            print(f"state_diff: {exc}", file=sys.stderr)
        return 2

    report["scenario"] = args.scenario
    if args.json:
        print(json.dumps(report, indent=1))
    else:
        print(render_text(report))
    return _EXIT.get(report["verdict"], 2)


if __name__ == "__main__":
    raise SystemExit(main())
