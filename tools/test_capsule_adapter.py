#!/usr/bin/env python3
"""tools/test_capsule_adapter.py — CC-02 host adapter generator and corpus tests.

Validates:
1. Automated C header/shim generation and runtime dynamic ctypes adapter synthesis.
2. Direct native execution of CallCapsules against libengine_diff.so via NativeHostDiffAdapter.
3. Persistent CorpusStore fixture discovery, disk serialization, and full-corpus replay.
4. Boundary mutation vector generation and oracle-vs-native cross-validation.
5. Deliberate divergence injection (return value, poststate globals, write order).
6. Zero-boilerplate target addition (spec descriptor + port symbol).
"""
from __future__ import annotations

import copy
import ctypes
import json
from pathlib import Path
import sys
import tempfile
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
    CapsuleReplayResult,
    replay_capsule,
    validate_capsule,
)
from tools.parity.capsule_capture import (
    CallCaptureSpec,
    KNOWN_CALL_SPECS,
    RacePolicy,
    get_cc01_canonical_fixtures,
    host_port_audio_fade_compute,
    host_port_boss_id_allowed,
    host_port_floor_is_checkpoint,
    host_port_rng_next15,
)
from tools.parity.host_diff_adapter import (
    EngineAudioOneShotIn,
    EngineAudioOneShotOut,
    EngineBossIdIn,
    EngineBossIdOut,
    EngineBudgetLevelDayIn,
    EngineBudgetLevelDayOut,
    EngineCharaEquipStatsIn,
    EngineCharaEquipStatsOut,
    EngineCheckpointIn,
    EngineCheckpointOut,
    EngineFadeIn,
    EngineFadeOut,
    EngineHaggleBudgetCeilingIn,
    EngineHaggleBudgetCeilingOut,
    EngineHaggleDecideIn,
    EngineHaggleDecideOut,
    EngineItemFindSlotIn,
    EngineItemFindSlotOut,
    EnginePushbackPatienceIn,
    EnginePushbackPatienceOut,
    EngineRngIn,
    EngineRngOut,
    NativeHostDiffAdapter,
    ensure_libengine_diff,
    get_diff_lib,
)
from tools.parity.adapter_gen import (
    AdapterGenerator,
    BoundaryMutator,
    DivergenceInjector,
)
from tools.parity.corpus_store import CorpusStore


class TestAdapterGenerator(unittest.TestCase):
    """Test automated C header, shim, and runtime ctypes adapter generation."""

    def test_generate_c_header_and_shim(self):
        spec = KNOWN_CALL_SPECS["boss_id_allowed"]
        hdr = AdapterGenerator.generate_c_header(spec)
        self.assertIn("typedef struct EngineBossIdAllowedIn", hdr)
        self.assertIn("int32_t arg_0;", hdr)
        self.assertIn("int32_t ret_value;", hdr)
        self.assertIn("void engine_boss_id_allowed(", hdr)

        shim = AdapterGenerator.generate_c_shim(spec)
        self.assertIn("void engine_boss_id_allowed(const EngineBossIdAllowedIn *in", shim)
        self.assertIn("stage_gate_boss_id_allowed(in->arg_0)", shim)

    def test_dynamic_ctypes_adapter_construction(self):
        # Create a dynamic spec for an arithmetic helper
        custom_spec = CallCaptureSpec(
            name="math_clamp",
            target_va="0x00401234",
            target_symbol="custom_clamp",
            abi="cdecl",
            category="pure_leaf",
            args=[10, 0, 5],
            arg_types=["int", "int", "int"],
            return_type="int",
        )

        def mock_c_entry(in_ptr, out_ptr):
            val = in_ptr.contents.arg_0
            lo = in_ptr.contents.arg_1
            hi = in_ptr.contents.arg_2
            out_ptr.contents.ret_value = max(lo, min(hi, val))

        adapter = AdapterGenerator.generate_dynamic_ctypes_adapter(custom_spec, mock_c_entry)
        res = adapter({"arg_0": 10, "arg_1": 0, "arg_2": 5})
        self.assertEqual(res["return_val"], 5)

        res2 = adapter({"arg_0": -3, "arg_1": 0, "arg_2": 5})
        self.assertEqual(res2["return_val"], 0)


class TestNativeHostDiffAdapter(unittest.TestCase):
    """Test direct native C execution against libengine_diff.so."""

    @classmethod
    def setUpClass(cls):
        # Ensure shared library is compiled
        ensure_libengine_diff()

    def setUp(self):
        self.fixtures = get_cc01_canonical_fixtures()

    def test_native_library_loaded(self):
        lib = get_diff_lib()
        self.assertIsNotNone(lib)
        self.assertTrue(hasattr(lib, "engine_rng_next15"))
        self.assertTrue(hasattr(lib, "engine_audio_fade"))
        self.assertTrue(hasattr(lib, "engine_stage_gate_boss_id_allowed"))
        self.assertTrue(hasattr(lib, "engine_stage_gate_floor_is_checkpoint"))
        self.assertTrue(hasattr(lib, "engine_haggle_decide"))
        self.assertTrue(hasattr(lib, "engine_haggle_budget_ceiling"))
        self.assertTrue(hasattr(lib, "engine_audio_is_one_shot_track"))
        self.assertTrue(hasattr(lib, "engine_customer_service_pushback_patience"))
        self.assertTrue(hasattr(lib, "engine_customer_service_budget_level_day"))
        self.assertTrue(hasattr(lib, "engine_tables_item_find_slot_by_id"))
        self.assertTrue(hasattr(lib, "engine_chara_equip_item_stats"))
    def test_execute_native_boss_id_allowed(self):
        cap = self.fixtures["boss_id_allowed"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"boss_id_allowed failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_floor_is_checkpoint(self):
        cap = self.fixtures["floor_is_checkpoint"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"floor_is_checkpoint failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)
        self.assertTrue(res.poststate_matched)

    def test_execute_native_rng_next15(self):
        cap = self.fixtures["rng_next15"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"rng_next15 failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)
        self.assertTrue(res.poststate_matched)

    def test_execute_native_audio_fade_compute(self):
        cap = self.fixtures["audio_fade_compute"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"audio_fade_compute failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_records_a_spawn(self):
        cap = self.fixtures["records_a_spawn"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"records_a_spawn failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")

    def test_execute_native_haggle_decide(self):
        cap = self.fixtures["haggle_decide"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"haggle_decide failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_haggle_budget_ceiling(self):
        cap = self.fixtures["haggle_budget_ceiling"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"haggle_budget_ceiling failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_audio_is_one_shot_track(self):
        cap = self.fixtures["audio_is_one_shot_track"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"audio_is_one_shot_track failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_customer_service_pushback_patience(self):
        cap = self.fixtures["customer_service_pushback_patience"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"pushback_patience failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_customer_service_budget_level_day(self):
        cap = self.fixtures["customer_service_budget_level_day"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"budget_level_day failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)
        self.assertTrue(res.poststate_matched)

    def test_execute_native_tables_item_find_slot_by_id(self):
        cap = self.fixtures["tables_item_find_slot_by_id"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"item_find_slot failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.return_val_matched)

    def test_execute_native_chara_equip_item_stats(self):
        cap = self.fixtures["chara_equip_item_stats"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched, f"chara_equip_item_stats failed: {res.notes}")
        self.assertEqual(res.verdict, "PASS")


class TestCorpusStore(unittest.TestCase):
    """Test disk serialization, indexing, and whole-corpus native replay."""

    def test_save_load_roundtrip(self):
        fixtures = get_cc01_canonical_fixtures()
        with tempfile.TemporaryDirectory() as td:
            store = CorpusStore(Path(td))
            p = store.save_fixture(fixtures["boss_id_allowed"], name="test_boss")
            self.assertTrue(p.is_file())

            loaded = store.load_fixture("test_boss")
            self.assertEqual(fixtures["boss_id_allowed"].capsule_id, loaded.capsule_id)
            self.assertEqual(loaded.target_symbol, "stage_gate_boss_id_allowed")

    def test_load_all_canonical_fixtures_from_disk(self):
        store = CorpusStore()
        fixtures = store.load_all_fixtures()
        self.assertGreaterEqual(len(fixtures), 5)
        for expected_name in ["boss_id_allowed", "floor_is_checkpoint", "rng_next15", "records_a_spawn", "audio_fade_compute"]:
            self.assertIn(expected_name, fixtures)

    def test_verify_corpus_against_native(self):
        store = CorpusStore()
        results = store.verify_corpus_against_native()
        self.assertGreaterEqual(len(results), 5)
        for name, res in results.items():
            self.assertTrue(res.matched, f"Corpus fixture {name} failed: {res.notes}")
            self.assertEqual(res.verdict, "PASS")


class TestBoundaryMutation(unittest.TestCase):
    """Test boundary mutation generation and cross-validation."""

    def test_generate_boundary_vectors(self):
        spec = KNOWN_CALL_SPECS["boss_id_allowed"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=20)
        self.assertGreaterEqual(len(vecs), 10)
        # Verify edge cases like 0, -1, max int are generated
        values = [v["arg_0"] for v in vecs]
        self.assertIn(0, values)
        self.assertIn(-1, values)
        self.assertIn(0x17, values)

    def test_boss_id_boundary_cross_validation(self):
        spec = KNOWN_CALL_SPECS["boss_id_allowed"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=30)
        lib = get_diff_lib()

        for vec in vecs:
            arg = vec["arg_0"]
            # Oracle
            py_res = host_port_boss_id_allowed(arg)
            # Native C
            in_val = EngineBossIdIn(enemy_id=arg)
            out_val = EngineBossIdOut()
            lib.engine_stage_gate_boss_id_allowed(ctypes.byref(in_val), ctypes.byref(out_val))
            c_res = int(out_val.allowed)

            self.assertEqual(
                py_res, c_res, f"Boss ID boundary mismatch on arg={arg}: py={py_res}, c={c_res}"
            )

    def test_rng_boundary_cross_validation(self):
        spec = KNOWN_CALL_SPECS["rng_next15"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=20)
        lib = get_diff_lib()

        for vec in vecs:
            seed = vec["DAT_006023a0"]
            # Oracle
            py_globals = {"DAT_006023a0": seed}
            py_ret = host_port_rng_next15(py_globals)
            py_post = py_globals["DAT_006023a0"]

            # Native C
            in_val = EngineRngIn(seed=seed)
            out_val = EngineRngOut()
            lib.engine_rng_next15(ctypes.byref(in_val), ctypes.byref(out_val))
            c_ret = int(out_val.ret_value)
            c_post = int(out_val.post_state)

            self.assertEqual(py_ret, c_ret, f"RNG ret mismatch on seed={seed}")
            self.assertEqual(py_post, c_post, f"RNG poststate mismatch on seed={seed}")

    def test_haggle_decide_boundary_cross_validation(self):
        spec = KNOWN_CALL_SPECS["haggle_decide"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=25)
        lib = get_diff_lib()
        from tools.parity.capsule_capture import host_port_haggle_decide

        for vec in vecs:
            ask = vec["arg_0"]
            ref = vec["arg_1"]
            py_res = host_port_haggle_decide(ask, ref)

            in_val = EngineHaggleDecideIn(player_ask=ask, accept_ref=ref)
            out_val = EngineHaggleDecideOut()
            lib.engine_haggle_decide(ctypes.byref(in_val), ctypes.byref(out_val))
            c_res = int(out_val.verdict)

            self.assertEqual(
                py_res, c_res, f"haggle_decide mismatch on ask={ask}, ref={ref}: py={py_res}, c={c_res}"
            )

    def test_haggle_budget_ceiling_boundary_cross_validation(self):
        spec = KNOWN_CALL_SPECS["haggle_budget_ceiling"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=20)
        lib = get_diff_lib()
        from tools.parity.capsule_capture import host_port_haggle_budget_ceiling

        for vec in vecs:
            market = vec["arg_0"]
            low = vec["arg_1"]
            high = vec["arg_2"]
            py_res = host_port_haggle_budget_ceiling(market, low, high)

            in_val = EngineHaggleBudgetCeilingIn(market_price=market, budget_low=low, budget_high=high)
            out_val = EngineHaggleBudgetCeilingOut()
            lib.engine_haggle_budget_ceiling(ctypes.byref(in_val), ctypes.byref(out_val))
            c_res = int(out_val.ceiling)

            self.assertEqual(
                py_res, c_res, f"haggle_budget_ceiling mismatch on market={market}, low={low}, high={high}"
            )

    def test_audio_is_one_shot_boundary_cross_validation(self):
        spec = KNOWN_CALL_SPECS["audio_is_one_shot_track"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=20)
        lib = get_diff_lib()
        from tools.parity.capsule_capture import host_port_audio_is_one_shot_track

        for vec in vecs:
            track = vec["arg_0"]
            py_res = host_port_audio_is_one_shot_track(track)

            in_val = EngineAudioOneShotIn(track=track)
            out_val = EngineAudioOneShotOut()
            lib.engine_audio_is_one_shot_track(ctypes.byref(in_val), ctypes.byref(out_val))
            c_res = int(out_val.is_one_shot)

            self.assertEqual(
                py_res, c_res, f"audio_is_one_shot mismatch on track={track}"
            )

    def test_pushback_patience_boundary_cross_validation(self):
        spec = KNOWN_CALL_SPECS["customer_service_pushback_patience"]
        vecs = BoundaryMutator.generate_boundary_vectors(spec, max_count=20)
        lib = get_diff_lib()
        from tools.parity.capsule_capture import host_port_customer_service_pushback_patience

        for vec in vecs:
            lvl = vec["arg_0"]
            sell_active = vec["arg_1"]
            py_res = host_port_customer_service_pushback_patience(lvl, sell_active)

            in_val = EnginePushbackPatienceIn(loyalty_level=lvl, sell_active=sell_active)
            out_val = EnginePushbackPatienceOut()
            lib.engine_customer_service_pushback_patience(ctypes.byref(in_val), ctypes.byref(out_val))
            c_res = int(out_val.patience_variant)

            self.assertEqual(
                py_res, c_res, f"pushback_patience mismatch on lvl={lvl}, sell_active={sell_active}"
            )


class TestDivergenceDetection(unittest.TestCase):
    """Test deliberate divergence injection and failure localization."""

    def setUp(self):
        self.fixtures = get_cc01_canonical_fixtures()

    def test_detect_return_divergence(self):
        cap = self.fixtures["boss_id_allowed"]
        corrupted = DivergenceInjector.inject_return_mismatch(cap)
        res = NativeHostDiffAdapter.execute_native(corrupted)
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertEqual(res.divergent_field, "return_val")

    def test_detect_poststate_divergence(self):
        cap = self.fixtures["rng_next15"]
        corrupted = DivergenceInjector.inject_poststate_mismatch(cap)
        res = NativeHostDiffAdapter.execute_native(corrupted)
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertIn("poststate", res.divergent_field)

    def test_detect_haggle_decide_divergence(self):
        cap = self.fixtures["haggle_decide"]
        corrupted = DivergenceInjector.inject_return_mismatch(cap)
        res = NativeHostDiffAdapter.execute_native(corrupted)
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertEqual(res.divergent_field, "return_val")

    def test_detect_budget_level_day_poststate_divergence(self):
        cap = self.fixtures["customer_service_budget_level_day"]
        corrupted = DivergenceInjector.inject_poststate_mismatch(cap)
        res = NativeHostDiffAdapter.execute_native(corrupted)
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertIn("poststate", res.divergent_field)

    def test_corpus_provenance_and_caller_distribution(self):
        for name, cap in self.fixtures.items():
            self.assertIn("scenario", cap.provenance, f"Capsule {name} missing scenario")
            self.assertIn("race_status", cap.provenance, f"Capsule {name} missing race_status")
            self.assertIn("retail_build_sha256", cap.provenance, f"Capsule {name} missing retail build hash")

    def test_cc04_unknown_write_native_replay(self):
        from tools.parity.capsule_capture import get_cc04_canonical_fixtures
        cc04 = get_cc04_canonical_fixtures()
        cap = cc04["chara_equip_item_stats_unknown_write"]
        res = NativeHostDiffAdapter.execute_native(cap)
        self.assertTrue(res.matched)
        self.assertEqual(res.verdict, "PASS")
        self.assertTrue(res.poststate_matched)

    def test_cc04_unknown_write_divergence_detection(self):
        from tools.parity.capsule_capture import get_cc04_canonical_fixtures
        cc04 = get_cc04_canonical_fixtures()
        cap = cc04["chara_equip_item_stats_unknown_write"]
        corrupted = DivergenceInjector.inject_poststate_mismatch(cap)
        res = NativeHostDiffAdapter.execute_native(corrupted)
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertIn("poststate", res.divergent_field)


class TestZeroBoilerplateRegistration(unittest.TestCase):
    """Test adding a simple known-write leaf with only a descriptor plus port symbol."""

    def test_add_leaf_descriptor(self):
        # Declare a new leaf descriptor
        new_leaf_spec = CallCaptureSpec(
            name="is_positive",
            target_va="0x00499999",
            target_symbol="port_is_positive",
            abi="cdecl",
            category="pure_leaf",
            args=[42],
            arg_types=["int"],
            return_type="int",
            race_policy=RacePolicy.SINGLE_THREADED_SAFE,
        )

        # Port symbol implementation
        def port_is_positive(val: int) -> int:
            return 1 if val > 0 else 0

        # Capture
        from tools.parity.capsule_capture import KnownWriteCaptureEngine
        res = KnownWriteCaptureEngine.capture_simulated(new_leaf_spec, port_is_positive)
        self.assertTrue(res.success)
        self.assertEqual(res.capsule.return_val, 1)

        # Replay
        rep = replay_capsule(res.capsule, port_is_positive)
        self.assertTrue(rep.matched)
        self.assertEqual(rep.verdict, "PASS")


if __name__ == "__main__":
    unittest.main()
