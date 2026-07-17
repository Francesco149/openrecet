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
from parity import (  # noqa: E402
    ObservationError,
    attach_provenance,
    load_mutation_stream,
    load_required,
    ordered_frames_from_view,
)
from parity.observations import FAIL, PASS, load_json  # noqa: E402
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
    ap.add_argument("--mutations", action="store_true",
                    help="attach ST-05 mutation provenance from {port,retail}-state-mutation.json "
                         "in the window dir (the WRITER behind the divergent leaf)")
    ap.add_argument("--port-mutations", type=Path, default=None,
                    help="explicit port mutation stream (overrides --mutations autodetect)")
    ap.add_argument("--retail-mutations", type=Path, default=None,
                    help="explicit retail mutation stream")
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

        # ST-05: attach mutation provenance (the WRITER behind the divergent leaf) +
        # the ordering-invariant check, when mutation streams are present.
        port_mp = args.port_mutations or (
            window_dir / "port-state-mutation.json" if args.mutations else None)
        retail_mp = args.retail_mutations or (
            window_dir / "retail-state-mutation.json" if args.mutations else None)
        if port_mp and retail_mp and port_mp.exists() and retail_mp.exists():
            req = required if required is not None else ordered_frames_from_view(load_json(view))
            _, pms = load_mutation_stream(load_json(port_mp))
            _, rms = load_mutation_stream(load_json(retail_mp))
            attach_provenance(report, pms, rms, req)
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
