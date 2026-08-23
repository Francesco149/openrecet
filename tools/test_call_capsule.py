#!/usr/bin/env python3
"""tools/test_call_capsule.py — Test suite for Call Capsules (CC-00).

Validates:
1. CallCapsule identity hashing determinism and input sensitivity.
2. JSON schema validation against docs/schemas/call-capsule-v1.json.
3. Category invariants across all 5 architectural categories (pure_leaf, known_globals,
   struct_mutation, rng_consumer, unsupported_os_call).
4. Host replay execution and bit-exact differential verification.
5. Divergence detection and failure localization.
"""
from __future__ import annotations

import json
import unittest
from pathlib import Path

# Add repo root to sys.path
REPO = Path(__file__).resolve().parent.parent
import sys
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

try:
    from jsonschema import Draft202012Validator
except ImportError:
    Draft202012Validator = None

from tools.parity.capsule import (
    CallCapsule,
    CapsuleError,
    MemoryWrite,
    ObjectSnapshot,
    compute_capsule_id,
    get_canonical_fixtures,
    replay_capsule,
    validate_capsule,
)


class TestCallCapsuleIdentity(unittest.TestCase):
    """Test CC-00 content-addressed identity computation."""

    def test_id_determinism(self):
        c1 = CallCapsule(
            target_va="0x00431990",
            abi="cdecl",
            category="pure_leaf",
            stack_args=[1, 0],
            prestate={},
            return_val=1,
            poststate={},
            provenance={"scenario": "test", "retail_build_sha256": "0" * 64},
        )
        c2 = CallCapsule(
            target_va="0x00431990",
            abi="cdecl",
            category="pure_leaf",
            stack_args=[1, 0],
            prestate={},
            return_val=1,
            poststate={},
            provenance={"scenario": "test", "retail_build_sha256": "0" * 64},
        )
        self.assertEqual(c1.capsule_id, c2.capsule_id)
        self.assertEqual(len(c1.capsule_id), 64)

    def test_id_sensitivity(self):
        base = CallCapsule(
            target_va="0x00431990",
            abi="cdecl",
            category="pure_leaf",
            stack_args=[1, 0],
            prestate={},
            return_val=1,
            poststate={},
            provenance={"scenario": "test", "retail_build_sha256": "0" * 64},
        )

        # Target VA difference
        c_va = CallCapsule(
            target_va="0x00431994",
            abi="cdecl",
            category="pure_leaf",
            stack_args=[1, 0],
            prestate={},
            return_val=1,
            poststate={},
            provenance={"scenario": "test", "retail_build_sha256": "0" * 64},
        )
        self.assertNotEqual(base.capsule_id, c_va.capsule_id)

        # Stack args difference
        c_args = CallCapsule(
            target_va="0x00431990",
            abi="cdecl",
            category="pure_leaf",
            stack_args=[2, 0],
            prestate={},
            return_val=1,
            poststate={},
            provenance={"scenario": "test", "retail_build_sha256": "0" * 64},
        )
        self.assertNotEqual(base.capsule_id, c_args.capsule_id)


class TestCallCapsuleSchema(unittest.TestCase):
    """Test schema conformance and category rules."""

    def setUp(self):
        schema_path = REPO / "docs" / "schemas" / "call-capsule-v1.json"
        self.schema_doc = json.loads(schema_path.read_text(encoding="utf-8"))
        if Draft202012Validator:
            self.validator = Draft202012Validator(self.schema_doc)
        else:
            self.validator = None

    def test_canonical_fixtures_schema_conformance(self):
        fixtures = get_canonical_fixtures()
        self.assertEqual(len(fixtures), 5)

        for name, cap in fixtures.items():
            cap_dict = cap.to_dict()
            if self.validator:
                errors = list(self.validator.iter_errors(cap_dict))
                self.assertEqual(errors, [], f"Schema validation error for fixture {name}: {errors}")
            validate_capsule(cap)

    def test_pure_leaf_invariant_enforcement(self):
        # pure_leaf with non-empty prestate must fail validation
        bad_leaf = CallCapsule(
            target_va="0x00431990",
            abi="cdecl",
            category="pure_leaf",
            stack_args=[1],
            prestate={"DAT_006023a0": 1},  # Invalid for pure leaf!
            return_val=1,
            poststate={},
            provenance={"scenario": "test", "retail_build_sha256": "0" * 64},
        )
        with self.assertRaises(CapsuleError):
            validate_capsule(bad_leaf)


class TestCallCapsuleReplay(unittest.TestCase):
    """Test host replay and differential execution."""

    def test_replay_pure_leaf_pass(self):
        cap = get_canonical_fixtures()["pure_leaf"]

        # Target function
        def host_boss_id_allowed(boss_id: int, dungeon_mode: int) -> int:
            return 1 if boss_id == 1 and dungeon_mode == 0 else 0

        res = replay_capsule(cap, host_boss_id_allowed)
        self.assertTrue(res.matched)
        self.assertEqual(res.verdict, "PASS")

    def test_replay_pure_leaf_divergence_detected(self):
        cap = get_canonical_fixtures()["pure_leaf"]

        # Faulty implementation returning 0 instead of 1
        def faulty_impl(boss_id: int, dungeon_mode: int) -> int:
            return 0

        res = replay_capsule(cap, faulty_impl)
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertEqual(res.divergent_field, "return_val")
        self.assertEqual(res.expected_val, 1)
        self.assertEqual(res.actual_val, 0)

    def test_replay_rng_consumer(self):
        cap = get_canonical_fixtures()["rng_consumer"]

        # Target implementation that reads prestate["rng"], computes next step, updates poststate
        def host_rng_step(globals_dict: dict) -> int:
            seed = globals_dict.get("rng", 1)
            next_seed = (seed * 0x343fd + 0x269ec3) & 0xffffffff
            globals_dict["rng"] = next_seed
            return (next_seed >> 16) & 0x7fff

        res = replay_capsule(cap, host_rng_step)
        self.assertTrue(res.matched)
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.poststate_matched)

    def test_replay_unsupported_os_call(self):
        cap = get_canonical_fixtures()["unsupported_os_call"]
        res = replay_capsule(cap)
        self.assertEqual(res.verdict, "UNSUPPORTED")
        self.assertFalse(res.matched)


if __name__ == "__main__":
    unittest.main()
