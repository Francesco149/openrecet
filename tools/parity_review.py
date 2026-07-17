#!/usr/bin/env python3
"""tools/parity_review.py — EP-07: attach a human confirmation to a proof bundle.

A human review is ADDITIVE, non-hashed attestation. Attaching one:
  * does NOT change the bundle's proof_id (human_review is in canonical.NON_HASHED),
    so the review is written back into the SAME content-addressed bundle — it is an
    amendment of non-hashed metadata (like the envelope), not a new artifact; and
  * does NOT change any machine verdict. A confirming review over a non-PASS gate is
    recorded as `confirmed-despite-<MACHINE>` (never a silent pass). This command's
    exit code is the MACHINE gate's (§4.1), unmoved by the human verdict.

Usage:
    nix develop --command python3 tools/parity_review.py <bundle_dir> \
        --reviewer NAME --date YYYY-MM-DD --scope "what was reviewed" \
        [--verdict confirmed|rejected|noted] [--notes "..."] \
        [--confirmed-pillars a,b,c] [--required-pillars a,b,c] [--json]

`required_pillars` (the gate is computed over them) default to the bundle's own
scenario contract (inputs.scenario_contract.id), verified against the recorded
contract_sha256 so a drifted contract fails closed; override with --required-pillars.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity import prove as _prove  # noqa: E402


class ReviewError(Exception):
    """A fatal, fail-closed condition → exit 2 (invalid input / drifted contract)."""


def resolve_required_pillars(proof: dict) -> list:
    """required_pillars from the bundle's OWN scenario contract, verified against the
    recorded contract_sha256 (fail closed on drift). parity_prove is imported lazily so
    this stays usable in a yaml-less shell whenever --required-pillars is passed."""
    import parity_prove

    sc = proof.get("inputs", {}).get("scenario_contract", {})
    scenario = sc.get("id")
    if not scenario:
        raise ReviewError("bundle has no inputs.scenario_contract.id — pass --required-pillars")
    doc = parity_prove.load_scenario_contract(scenario)
    contract = doc["proof"]
    have = parity_prove.contract_sha256(contract)
    want = sc.get("contract_sha256")
    if want and have != want:
        raise ReviewError(
            f"scenario '{scenario}' contract drifted since this bundle "
            f"(contract_sha256 {have[:12]}… != recorded {want[:12]}…); "
            f"pass --required-pillars to review against an explicit gate")
    return list(contract.get("required_pillars") or [])


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="EP-07 attach a human review to a proof bundle")
    ap.add_argument("bundle_dir", type=Path, help="a proof bundle dir (contains proof.json)")
    ap.add_argument("--reviewer", required=True)
    ap.add_argument("--date", required=True)
    ap.add_argument("--scope", required=True)
    ap.add_argument("--verdict", default="noted", choices=list(_prove.HUMAN_VERDICTS))
    ap.add_argument("--notes", default=None)
    ap.add_argument("--confirmed-pillars", default=None,
                    help="comma-separated pillar names the human confirmed")
    ap.add_argument("--required-pillars", default=None,
                    help="comma-separated gate pillars (default: the bundle's scenario contract)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    try:
        pj = args.bundle_dir / "proof.json"
        if not pj.exists():
            raise ReviewError(f"no proof.json in {args.bundle_dir}")
        proof = json.loads(pj.read_text())

        if args.required_pillars is not None:
            required = [p for p in args.required_pillars.split(",") if p]
        else:
            required = resolve_required_pillars(proof)

        review = {"reviewer": args.reviewer, "date": args.date, "scope": args.scope,
                  "verdict": args.verdict}
        if args.notes:
            review["notes"] = args.notes
        if args.confirmed_pillars is not None:
            review["confirmed_pillars"] = [p for p in args.confirmed_pillars.split(",") if p]

        reviewed = _prove.attach_human_review(proof, review, required_pillars=required)
        # Non-hashed amendment: the review does not move the content-address, so it is
        # written back into the SAME bundle in place (matching write_bundle's on-disk
        # format: indent=1, sorted keys). A bundle whose stored proof_id predates the
        # EP-07 canonicalization is stale under the current rule — the review is still
        # valid attestation, but flag it so the operator knows a re-drive re-addresses it.
        stale = reviewed["proof_id"] != _prove.proof_id_of(reviewed)
        pj.write_text(json.dumps(reviewed, indent=1, sort_keys=True))
        summary = _prove.summarize(reviewed, required)
        if stale:
            summary["caveat"] = ("bundle proof_id predates the EP-07 canonicalization "
                                 "(stale under the current rule); the review is attached "
                                 "here, but a re-drive mints a fresh bundle needing its own review")
    except (ReviewError, ValueError) as exc:
        print(json.dumps({"error": str(exc), "exit_code": 2}, indent=1) if args.json
              else f"parity_review: {exc}", file=sys.stderr)
        return 2
    except Exception as exc:  # pragma: no cover — unexpected
        print(f"parity_review: {type(exc).__name__}: {exc}", file=sys.stderr)
        return 2

    hr = reviewed["human_review"]
    if args.json:
        print(json.dumps(summary, indent=1))
    else:
        print(f"reviewed {reviewed['proof_id'][:16]}…  human={hr['verdict']}  "
              f"machine={summary['verdict']} (exit {summary['exit_code']})")
        print(f"  reviewer: {hr['reviewer']}  date: {hr['date']}")
        print(f"  scope: {hr['scope']}")
        if summary.get("caveat"):
            print(f"  caveat: {summary['caveat']}")
    return summary["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
