#!/usr/bin/env python3
"""tools/parity_state.py — produce the `state` (volatile) pillar's metrics.

The `state` proof pillar proves the once-per-frame VOLATILE engine state (rng,
player/companion actors, phase, interaction, customer-service, dialogue, title)
is bit-identical port↔retail at every required paired frame. Its evidence is a
Trace Studio v3 `--state` window's `view.json` (which bakes each side's
call_trace.jsonl STATE_VA fields per identity-joined frame). This is the producer
CLI: bridge that view into `state-metrics.json` — the doc parity_prove consumes to
give the `state` pillar a PASS/FAIL verdict instead of NOT_CAPTURED — and print
the per-frame Merkle-compare summary.

parity_prove bridges the SAME view inline (like render_program), so this CLI is
for standalone production/inspection. It scopes to the contract's join window when
the scenario declares one, else compares every both-sided state frame.

Flow:

    # 1. capture a --state window (bakes per-frame state into view.json)
    orv3_window.py <scenario> --window OFF:COUNT --state --view
    # 2. produce the state metrics, deposited in the v3 window dir
    parity_state.py <scenario> --window OFF:COUNT
    # 3. compile the proof (now the state pillar has a real verdict)
    parity_prove.py <scenario> --window OFF:COUNT --env-json <env.json> --json

Exit: 0 wrote metrics, 2 fatal (no view.json / unresolvable).
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
    adapt_state,
    load_required,
    state_metrics_from_view_json,
)
from parity.observations import LogicalFrame  # noqa: E402


def _window_dir(args) -> Path:
    if args.from_window:
        return args.from_window
    if args.window:
        off, _, count = args.window.partition(":")
        return ROOT / "runs/studio-v3-windows" / args.scenario / f"win-{off}-{count}"
    raise parity_prove.ProveError("pass --window OFF:COUNT or --from-window <dir>")


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="produce the state (volatile) pillar metrics")
    ap.add_argument("scenario")
    ap.add_argument("--window", default=None, metavar="OFF:COUNT",
                    help="the v3 window runs/studio-v3-windows/<scen>/win-OFF-COUNT")
    ap.add_argument("--from-window", type=Path, default=None, help="an explicit v3 window dir")
    ap.add_argument("--contract", type=Path, default=None,
                    help="external contract yaml (default: the scenario.yaml proof block)")
    ap.add_argument("--all-frames", action="store_true",
                    help="compare every both-sided state frame, ignoring the contract window")
    ap.add_argument("--json", action="store_true", help="print a JSON summary")
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

        source = parity_prove._view_container_hashes(view) or None
        doc = state_metrics_from_view_json(view, required=required, source=source)
        out = window_dir / "state-metrics.json"
        out.write_text(json.dumps(doc))
        # Adjudicate exactly as parity_prove does (the coverage gate: a required
        # frame with no both-sided state is NOT_CAPTURED, never a silent PASS on the
        # subset). --all-frames has no contract window ⇒ grade over the doc's own frames.
        adj_required = required if required is not None else [
            LogicalFrame.from_key(f["key"]) for f in doc.get("frames") or []]
        verdict = adapt_state(out, adj_required, expected_containers=source).pillar
    except (parity_prove.ProveError, ObservationError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1), file=sys.stderr)
        else:
            print(f"parity_state: {exc}", file=sys.stderr)
        return 2

    frames = doc.get("frames") or []
    n_ident = sum(1 for f in frames if f.get("identical"))
    fd = verdict.get("first_divergence") or {}
    summary = {
        "scenario": args.scenario,
        "wrote": str(out),
        "verdict": verdict["verdict"],
        "detail": verdict.get("detail"),
        "has_state": doc.get("has_state"),
        "n_required": len(adj_required),
        "n_compared": len(frames),
        "n_identical": n_ident,
        "first_divergence": fd or None,
        "source": doc.get("source"),
        "exit_code": 0,
    }
    if args.json:
        print(json.dumps(summary, indent=1))
    else:
        print(f"state: wrote {out}")
        print(f"  {verdict['verdict']}  ({n_ident}/{len(frames)} compared frames identical; "
              f"{len(adj_required)} required)")
        if verdict.get("detail"):
            print(f"  {verdict['detail']}")
        if fd:
            lf = fd.get("logical_frame", {})
            print(f"  first divergence @ {lf.get('anchor')}#{lf.get('occurrence')}+{lf.get('offset')}"
                  f"  {fd.get('path')}  (retail={fd.get('retail_value')} port={fd.get('port_value')})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
