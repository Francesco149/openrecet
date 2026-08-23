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
import struct
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
    UnknownWriteCaptureEngine,
    UNKNOWN_CALL_SPECS,
    UnknownWriteCaptureSpec,
    get_cc01_canonical_fixtures,
    get_cc04_canonical_fixtures,
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


class TestUnknownWriteCaptureEngine(unittest.TestCase):
    """Test CC-04 unknown write-set dynamic capture and rollback."""

    def setUp(self):
        self.spec = UNKNOWN_CALL_SPECS["chara_equip_item_stats_unknown_write"]

    def test_unknown_write_capture_success(self):
        def runtime_fn(flag, ptr_addr, globals_dict, pages_dict):
            # Mutates 16 bytes in stats struct
            stats_page = pages_dict["0x056db0ac"]
            # Write atk=15, def=10, mag=5, mdef=2
            struct.pack_into("<iiii", stats_page, 0, 15, 10, 5, 2)
            globals_dict["DAT_005c80ac"] = 42
            return 0

        initial_pages = {
            "0x056db0ac": bytearray(64),
            "0x005c80ac": bytearray(16),
        }
        initial_globals = {"DAT_005c80ac": 0}

        res = UnknownWriteCaptureEngine.capture_simulated(
            self.spec,
            runtime_fn,
            memory_pages=initial_pages,
            initial_globals=initial_globals,
        )
        self.assertTrue(res.success)
        self.assertIsNotNone(res.capsule)
        self.assertTrue(res.restored)
        self.assertGreaterEqual(len(res.capsule.ordered_writes), 2)
        self.assertIn("0x056db0ac", res.capsule.pointed_objects)
        self.assertEqual(res.capsule.poststate["DAT_005c80ac"], 42)

    def test_unknown_write_inviolable_rollback(self):
        initial_pages = {
            "0x056db0ac": bytearray(b"\x00" * 64),
            "0x005c80ac": bytearray(b"\x00" * 16),
        }
        initial_globals = {"DAT_005c80ac": 0}

        def crashing_fn(flag, ptr_addr, globals_dict, pages_dict):
            pages_dict["0x056db0ac"][:4] = b"\xff\xff\xff\xff"
            globals_dict["DAT_005c80ac"] = 9999
            raise RuntimeError("simulated engine fault during mutation")

        res = UnknownWriteCaptureEngine.capture_simulated(
            self.spec,
            crashing_fn,
            memory_pages=initial_pages,
            initial_globals=initial_globals,
        )
        self.assertFalse(res.success)
        self.assertTrue(res.restored)
        # Verify prestate was restored bit-for-bit
        self.assertEqual(initial_pages["0x056db0ac"], bytearray(b"\x00" * 64))
        self.assertEqual(initial_globals["DAT_005c80ac"], 0)

    def test_unknown_write_max_bytes_overflow_rejected(self):
        tight_spec = copy.deepcopy(self.spec)
        tight_spec.max_write_bytes = 4  # Cap at 4 bytes

        def overflow_fn(flag, ptr_addr, globals_dict, pages_dict):
            # Mutate 16 bytes (> 4 bytes limit)
            pages_dict["0x056db0ac"][:16] = b"\xaa" * 16
            return 0

        initial_pages = {"0x056db0ac": bytearray(64)}
        res = UnknownWriteCaptureEngine.capture_simulated(
            tight_spec,
            overflow_fn,
            memory_pages=initial_pages,
        )
        self.assertFalse(res.success)
        self.assertIn("max_write_bytes exceeded", res.error)

    def test_unknown_write_max_writes_count_overflow_rejected(self):
        tight_spec = copy.deepcopy(self.spec)
        tight_spec.max_writes_count = 1  # Cap at 1 write

        def multi_write_fn(flag, ptr_addr, globals_dict, pages_dict):
            # Mutate 2 discrete 4-byte chunks
            pages_dict["0x056db0ac"][0:4] = b"\x11" * 4
            pages_dict["0x056db0ac"][8:12] = b"\x22" * 4
            return 0

        initial_pages = {"0x056db0ac": bytearray(64)}
        res = UnknownWriteCaptureEngine.capture_simulated(
            tight_spec,
            multi_write_fn,
            memory_pages=initial_pages,
        )
        self.assertFalse(res.success)
        self.assertIn("max_writes_count exceeded", res.error)

if __name__ == "__main__":
    unittest.main()
