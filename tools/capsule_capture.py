#!/usr/bin/env python3
"""tools/capsule_capture.py — CC-01 CLI for known-write call capture, validation, and host diffing.

Usage:
  python3 tools/capsule_capture.py list
  python3 tools/capsule_capture.py capture <spec_name> [--out <file.json>]
  python3 tools/capsule_capture.py replay <capsule.json> [--spec <spec_name>]
  python3 tools/capsule_capture.py verify-corpus
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any, Dict, List, Optional

# Add repo root to sys.path
REPO = Path(__file__).resolve().parent.parent
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from tools.parity.capsule import (
    CallCapsule,
    CapsuleError,
    replay_capsule,
    validate_capsule,
)
from tools.parity.capsule_capture import (
    CallCaptureSpec,
    CapsuleCaptureResult,
    KnownWriteCaptureEngine,
    KNOWN_CALL_SPECS,
    RacePolicy,
    get_cc01_canonical_fixtures,
    host_port_audio_fade_compute,
    host_port_audio_is_one_shot_track,
    host_port_boss_id_allowed,
    host_port_chara_equip_item_stats,
    host_port_customer_service_budget_level_day,
    host_port_customer_service_pushback_patience,
    host_port_floor_is_checkpoint,
    host_port_haggle_budget_ceiling,
    host_port_haggle_decide,
    host_port_records_a_spawn,
    host_port_rng_next15,
    host_port_tables_item_find_slot_by_id,
)

HOST_PORT_HANDLERS = {
    "boss_id_allowed": host_port_boss_id_allowed,
    "floor_is_checkpoint": host_port_floor_is_checkpoint,
    "rng_next15": host_port_rng_next15,
    "records_a_spawn": host_port_records_a_spawn,
    "audio_fade_compute": host_port_audio_fade_compute,
    "haggle_decide": host_port_haggle_decide,
    "haggle_budget_ceiling": host_port_haggle_budget_ceiling,
    "audio_is_one_shot_track": host_port_audio_is_one_shot_track,
    "customer_service_pushback_patience": host_port_customer_service_pushback_patience,
    "customer_service_budget_level_day": host_port_customer_service_budget_level_day,
    "tables_item_find_slot_by_id": host_port_tables_item_find_slot_by_id,
    "chara_equip_item_stats": host_port_chara_equip_item_stats,
}


def cmd_list(args: argparse.Namespace) -> int:
    """Lists registered known-write call specifications."""
    print("Known-Write Call Specifications (CC-01):")
    print("-" * 78)
    print(f"{'SPEC NAME':<22} {'TARGET VA':<12} {'ABI':<8} {'CATEGORY':<16} {'RACE POLICY'}")
    print("-" * 78)
    for name, spec in KNOWN_CALL_SPECS.items():
        print(f"{name:<22} {spec.target_va:<12} {spec.abi:<8} {spec.category:<16} {spec.race_policy}")
    print("-" * 78)
    print(f"Total: {len(KNOWN_CALL_SPECS)} specs registered.")
    return 0


def cmd_capture(args: argparse.Namespace) -> int:
    """Captures a call capsule for the given spec name."""
    spec_name = args.spec_name
    if spec_name not in KNOWN_CALL_SPECS:
        print(f"ERROR: Unknown spec '{spec_name}'. Run 'list' to see available specs.", file=sys.stderr)
        return 1

    spec = KNOWN_CALL_SPECS[spec_name]
    target_fn = HOST_PORT_HANDLERS.get(spec_name)
    if not target_fn:
        print(f"ERROR: No reference implementation mapped for '{spec_name}'.", file=sys.stderr)
        return 1

    print(f"Capturing call capsule for '{spec_name}' ({spec.target_va})...")
    res = KnownWriteCaptureEngine.capture_simulated(spec, target_fn)

    if not res.success or not res.capsule:
        print(f"ERROR: Capture failed: {res.error}", file=sys.stderr)
        return 2

    capsule = res.capsule
    cap_dict = capsule.to_dict()
    cap_json = json.dumps(cap_dict, indent=2)

    if args.out:
        out_path = Path(args.out)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(cap_json)
        print(f"Wrote capsule ({capsule.capsule_id[:16]}...) to {out_path}")
    else:
        print(cap_json)

    return 0


def cmd_replay(args: argparse.Namespace) -> int:
    """Replays a call capsule JSON against host reference."""
    cap_path = Path(args.capsule)
    if not cap_path.is_file():
        print(f"ERROR: File not found: {cap_path}", file=sys.stderr)
        return 1

    try:
        data = json.loads(cap_path.read_text())
        capsule = CallCapsule.from_dict(data)
    except Exception as exc:
        print(f"ERROR: Failed to parse capsule: {exc}", file=sys.stderr)
        return 1

    # Match handler
    spec_name = args.spec
    if not spec_name:
        for name, spec in KNOWN_CALL_SPECS.items():
            if spec.target_va.lower() == capsule.target_va.lower():
                spec_name = name
                break

    if not spec_name or spec_name not in HOST_PORT_HANDLERS:
        print(f"ERROR: Could not find host port handler for VA {capsule.target_va}.", file=sys.stderr)
        return 1

    handler = HOST_PORT_HANDLERS[spec_name]
    result = replay_capsule(capsule, handler)

    print(f"Capsule ID: {capsule.capsule_id}")
    print(f"Target:     {capsule.target_symbol} ({capsule.target_va})")
    print(f"Verdict:    {result.verdict}")
    print(f"Matched:    {result.matched}")
    for n in result.notes:
        print(f"  - {n}")

    return 0 if result.matched else 2


def cmd_verify_corpus(args: argparse.Namespace) -> int:
    """Verifies all 5 canonical CC-01 capsules and checks invariants."""
    print("=" * 78)
    print("CC-01 Observed Call Capsule Acceptance Suite (5 Canonical Categories)")
    print("=" * 78)

    fixtures = get_cc01_canonical_fixtures()
    all_passed = True
    checks_count = 0

    for name, fixture in fixtures.items():
        print(f"\n[Spec: {name}] ({fixture.target_symbol} @ {fixture.target_va})")
        print(f"  Category:    {fixture.category}")
        print(f"  ABI:         {fixture.abi}")
        print(f"  Capsule ID:  {fixture.capsule_id}")

        # 1. Validate capsule schema
        try:
            validate_capsule(fixture)
            print("  [✓] Schema validation PASS")
            checks_count += 1
        except Exception as exc:
            print(f"  [✗] Schema validation FAIL: {exc}")
            all_passed = False

        # 2. Verify race safety metadata
        race_status = fixture.provenance.get("race_status")
        if race_status in {RacePolicy.DIFF_TEST_SUSPENDED, RacePolicy.SINGLE_THREADED_SAFE, RacePolicy.MUTEX_LOCKED}:
            print(f"  [✓] Race safety invariant PASS ({race_status})")
            checks_count += 1
        else:
            print(f"  [✗] Race safety invariant FAIL ({race_status})")
            all_passed = False

        # 3. Verify bit-exact host replay
        handler = HOST_PORT_HANDLERS.get(name)
        if not handler:
            print(f"  [✗] Missing handler for {name}")
            all_passed = False
            continue

        rep = replay_capsule(fixture, handler)
        if rep.matched and rep.verdict == "PASS":
            print(f"  [✓] Bit-exact host replay PASS (ret={rep.return_val_matched}, post={rep.poststate_matched})")
            checks_count += 1
        else:
            print(f"  [✗] Host replay FAIL: {rep.verdict} divergent_field={rep.divergent_field}")
            for n in rep.notes:
                print(f"      {n}")
            all_passed = False

    print("\n" + "=" * 78)
    if all_passed:
        print(f"CC-01 Corpus Verification PASSED ({checks_count} checks across {len(fixtures)} categories).")
        return 0
    else:
        print("CC-01 Corpus Verification FAILED.", file=sys.stderr)
        return 1


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="CC-01 Known-Write Call Capsule Platform")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # list
    p_list = subparsers.add_parser("list", help="List registered known-write specs")
    p_list.set_defaults(func=cmd_list)

    # capture
    p_cap = subparsers.add_parser("capture", help="Capture a call capsule")
    p_cap.add_argument("spec_name", help="Name of registered spec")
    p_cap.add_argument("--out", "-o", help="Output JSON path")
    p_cap.set_defaults(func=cmd_capture)

    # replay
    p_rep = subparsers.add_parser("replay", help="Replay a call capsule JSON")
    p_rep.add_argument("capsule", help="Path to capsule JSON")
    p_rep.add_argument("--spec", help="Explicit spec name override")
    p_rep.set_defaults(func=cmd_replay)

    # verify-corpus
    p_ver = subparsers.add_parser("verify-corpus", help="Run full corpus verification")
    p_ver.set_defaults(func=cmd_verify_corpus)

    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
