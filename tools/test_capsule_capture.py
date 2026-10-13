#!/usr/bin/env python3
"""tools/test_capsule_capture.py — CC-01 known-write call capture test suite.

Validates:
1. CallCaptureSpec validation, constraints, and serialization.
2. Simulated call capture lifecycle across all 5 canonical categories.
3. Inviolable `finally` rollback: state restoration on success and exception.
4. Race safety policy enforcement (DIFF_TEST_SUSPENDED, MUTEX_LOCKED, etc.).
5. Bit-exact host replay against ported C routines.
6. Divergence detection and failure localization.
"""
from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import unittest

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


class TestCallCaptureSpec(unittest.TestCase):
    """Test CallCaptureSpec validation and serialization."""

    def test_valid_spec(self):
        spec = KNOWN_CALL_SPECS["boss_id_allowed"]
        KnownWriteCaptureEngine.validate_spec(spec)
        d = spec.to_dict()
        rebuilt = CallCaptureSpec.from_dict(d)
        self.assertEqual(spec.target_va, rebuilt.target_va)
        self.assertEqual(spec.target_symbol, rebuilt.target_symbol)
        self.assertEqual(spec.abi, rebuilt.abi)

    def test_invalid_target_va(self):
        spec = CallCaptureSpec(
            name="bad_va",
            target_va="invalid_va",
            target_symbol="bad_symbol",
            abi="cdecl",
            category="pure_leaf",
        )
        with self.assertRaises(CapsuleError):
            KnownWriteCaptureEngine.validate_spec(spec)

    def test_unsupported_abi(self):
        spec = CallCaptureSpec(
            name="bad_abi",
            target_va="0x00401000",
            target_symbol="bad_symbol",
            abi="pascal",
            category="pure_leaf",
        )
        with self.assertRaises(CapsuleError):
            KnownWriteCaptureEngine.validate_spec(spec)

    def test_unverified_race_policy_rejected(self):
        spec = CallCaptureSpec(
            name="bad_race",
            target_va="0x00401000",
            target_symbol="bad_symbol",
            abi="cdecl",
            category="pure_leaf",
            race_policy=RacePolicy.UNVERIFIED_RACE_PRONE,
        )
        with self.assertRaises(CapsuleError):
            KnownWriteCaptureEngine.validate_spec(spec)


class TestKnownWriteCaptureEngine(unittest.TestCase):
    """Test capture execution across varied functions."""

    def test_capture_pure_leaf_boss_id(self):
        spec = KNOWN_CALL_SPECS["boss_id_allowed"]
        res = KnownWriteCaptureEngine.capture_simulated(spec, host_port_boss_id_allowed)
        self.assertTrue(res.success)
        self.assertTrue(res.restored)
        self.assertIsNotNone(res.capsule)
        self.assertEqual(res.capsule.return_val, 1)
        self.assertEqual(res.capsule.category, "pure_leaf")

    def test_capture_known_globals_checkpoint(self):
        spec = KNOWN_CALL_SPECS["floor_is_checkpoint"]
        initial_globals = {"DAT_0438b4c8": 0, "DAT_0438b4cc": 4}
        res = KnownWriteCaptureEngine.capture_simulated(
            spec, host_port_floor_is_checkpoint, initial_globals=initial_globals
        )
        self.assertTrue(res.success)
        self.assertTrue(res.restored)
        self.assertIsNotNone(res.capsule)
        self.assertEqual(res.capsule.return_val, 1)
        self.assertEqual(res.capsule.prestate["DAT_0438b4cc"], 4)
        self.assertEqual(res.capsule.poststate["DAT_0438b4cc"], 4)

    def test_capture_rng_consumer(self):
        spec = KNOWN_CALL_SPECS["rng_next15"]
        initial_globals = {"DAT_006023a0": 19937}
        res = KnownWriteCaptureEngine.capture_simulated(
            spec, host_port_rng_next15, initial_globals=initial_globals
        )
        self.assertTrue(res.success)
        self.assertTrue(res.restored)
        self.assertIsNotNone(res.capsule)
        self.assertEqual(res.capsule.return_val, 32376)
        self.assertEqual(res.capsule.poststate["DAT_006023a0"], 4269308192)

    def test_capture_records_a_spawn(self):
        spec = KNOWN_CALL_SPECS["records_a_spawn"]
        initial_globals = {"DAT_006023a0": 12345, "DAT_0076b960": 0}
        initial_objects = {"slot_0": bytearray(148)}
        res = KnownWriteCaptureEngine.capture_simulated(
            spec, host_port_records_a_spawn, initial_globals=initial_globals, initial_objects=initial_objects
        )
        self.assertTrue(res.success)
        self.assertTrue(res.restored)
        self.assertIsNotNone(res.capsule)
        self.assertEqual(res.capsule.category, "struct_mutation")
        self.assertEqual(res.capsule.poststate["DAT_0076b960"], 1)
        self.assertIn("slot_0", res.capsule.pointed_objects)

    def test_capture_audio_fade_compute(self):
        spec = KNOWN_CALL_SPECS["audio_fade_compute"]
        res = KnownWriteCaptureEngine.capture_simulated(spec, host_port_audio_fade_compute)
        self.assertTrue(res.success)
        self.assertTrue(res.restored)
        self.assertIsNotNone(res.capsule)
        self.assertEqual(res.capsule.return_val, -10000)


class TestInviolableRestoreOnException(unittest.TestCase):
    """Test that target function exceptions strictly restore pre-call state."""

    def test_restore_on_exception(self):
        spec = KNOWN_CALL_SPECS["floor_is_checkpoint"]
        initial_globals = {"DAT_0438b4c8": 999, "DAT_0438b4cc": 888}

        def failing_fn(globals_dict):
            globals_dict["DAT_0438b4c8"] = 123456  # Mutate global before failing
            globals_dict["DAT_0438b4cc"] = 654321
            raise RuntimeError("simulated retail engine crash during call")

        res = KnownWriteCaptureEngine.capture_simulated(
            spec, failing_fn, initial_globals=initial_globals
        )
        self.assertFalse(res.success)
        self.assertTrue(res.restored)
        self.assertIn("simulated retail engine crash", res.error)


class TestCorpusHostReplay(unittest.TestCase):
    """Test that all 5 canonical CC-01 fixtures replay bit-exact against ported code."""

    def setUp(self):
        self.fixtures = get_cc01_canonical_fixtures()
        self.handlers = {
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

    def test_all_five_canonical_fixtures_match(self):
        for name, fixture in self.fixtures.items():
            handler = self.handlers[name]
            rep = replay_capsule(fixture, handler)
            self.assertTrue(
                rep.matched,
                f"Fixture {name} failed replay: {rep.verdict} (divergent: {rep.divergent_field})",
            )
            self.assertEqual(rep.verdict, "PASS")
            self.assertTrue(rep.return_val_matched)
            self.assertTrue(rep.poststate_matched)

    def test_divergence_detection_return_val(self):
        bad_fixture = copy.deepcopy(self.fixtures["boss_id_allowed"])
        bad_fixture.return_val = 999  # Corrupt expected return value
        rep = replay_capsule(bad_fixture, self.handlers["boss_id_allowed"])
        self.assertFalse(rep.matched)
        self.assertEqual(rep.verdict, "FAIL")
        self.assertEqual(rep.divergent_field, "return_val")

    def test_divergence_detection_poststate(self):
        bad_fixture = copy.deepcopy(self.fixtures["rng_next15"])
        bad_fixture.poststate["DAT_006023a0"] = 0xdeadbeef  # Corrupt poststate seed
        rep = replay_capsule(bad_fixture, self.handlers["rng_next15"])
        self.assertFalse(rep.matched)
        self.assertEqual(rep.verdict, "FAIL")
        self.assertIn("poststate", rep.divergent_field)


if __name__ == "__main__":
    unittest.main()
