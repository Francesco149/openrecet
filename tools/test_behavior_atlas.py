#!/usr/bin/env python3
"""tools/test_behavior_atlas.py — Test suite for Behavior Atlas (BA-00..BA-08).

Validates:
1. Node & Edge identity computation determinism and sensitivity (BA-00, BA-01).
2. Atlas SQLite and CAS storage operations, graph serialization, and relocation invariance (BA-01).
3. Scenario corpus importing and graph topology extraction from tests/scenarios (BA-02).
4. Atlas traversal runner, BFS path finding, failure localization, and cycle detection (BA-03).
5. Action grammars and semantic input sequence compilation (BA-04).
6. Coverage-guided exploration scheduling, multi-factor scoring, and divergence detection (BA-05).
7. Hierarchical trace minimization, delta-debugging, and flakiness handling (BA-06).
8. RNG callsite registry, LCG backward stepping, jump ahead, and seed solver (BA-07).
9. Multi-dimensional Behavior Atlas health reporting and risk analysis (BA-08).
"""
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

# Add repo root to sys.path
REPO = Path(__file__).resolve().parent.parent
import sys
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from tools.atlas.grammar import GrammarRegistry, compile_action_sequence, BTN_A, BTN_DOWN, BTN_ESC
from tools.atlas.identity import (
    compute_edge_id,
    compute_input_digest,
    compute_node_id,
    compute_path_id,
)
from tools.atlas.importer import ScenarioImporter
from tools.atlas.model import (
    ActionGrammar,
    BehaviorGraph,
    CompletionCondition,
    Edge,
    Node,
    NormalizationPolicy,
)
from tools.atlas.health import AtlasHealthChecker, AtlasHealthReport
from tools.atlas.minimizer import DivergenceSignature, MinimizerConfig, TraceMinimizer
from tools.atlas.rng_solver import (
    RNGCallsiteRegistry,
    RNGSeedSolver,
    rng_compute_seed,
    rng_jump,
    rng_sequence,
    rng_step,
    rng_step_back,
)
from tools.atlas.runner import AtlasRunner, TraversalError
from tools.atlas.scheduler import CoverageGuidedScheduler, SchedulerConfig
from tools.atlas.store import AtlasStore


class TestBehaviorAtlasIdentity(unittest.TestCase):
    """Test BA-00 / BA-01 identity calculations and hashing."""

    def test_node_id_determinism(self):
        id1 = compute_node_id("TITLE_MENU", occurrence=1, rng_state=12345, config_id="default")
        id2 = compute_node_id("TITLE_MENU", occurrence=1, rng_state=12345, config_id="default")
        self.assertEqual(id1, id2)
        self.assertEqual(len(id1), 64)

    def test_node_id_sensitivity(self):
        base_id = compute_node_id("TITLE_MENU", occurrence=1, rng_state=12345, config_id="default")
        
        # Anchor difference
        self.assertNotEqual(base_id, compute_node_id("HOUSE_FREEROAM", occurrence=1, rng_state=12345, config_id="default"))
        # Occurrence difference
        self.assertNotEqual(base_id, compute_node_id("TITLE_MENU", occurrence=2, rng_state=12345, config_id="default"))
        # RNG difference
        self.assertNotEqual(base_id, compute_node_id("TITLE_MENU", occurrence=1, rng_state=12346, config_id="default"))
        # Config difference
        self.assertNotEqual(base_id, compute_node_id("TITLE_MENU", occurrence=1, rng_state=12345, config_id="custom"))
        # Persistent state root difference
        self.assertNotEqual(base_id, compute_node_id("TITLE_MENU", occurrence=1, persistent_state_root="a" * 64, rng_state=12345, config_id="default"))

    def test_input_digest(self):
        seq1 = [{"frame": 0, "buttons": ["A"], "mask": 16}]
        seq2 = [{"frame": 0, "buttons": ["A"], "mask": 16}]
        seq3 = [{"frame": 1, "buttons": ["A"], "mask": 16}]
        self.assertEqual(compute_input_digest(seq1), compute_input_digest(seq2))
        self.assertNotEqual(compute_input_digest(seq1), compute_input_digest(seq3))

    def test_edge_id_determinism_and_sensitivity(self):
        src = "0" * 64
        inp = "1" * 64
        cond1 = {"kind": "anchor_reached", "anchor": "SAVE_PICKER_READY"}
        cond2 = {"kind": "anchor_reached", "anchor": "TITLE_MENU"}
        norm1 = {"phasepin": 80}
        norm2 = {"phasepin": 81}

        e1 = compute_edge_id(src, inp, cond1, norm1)
        e2 = compute_edge_id(src, inp, cond1, norm1)
        self.assertEqual(e1, e2)

        # Condition sensitivity
        self.assertNotEqual(e1, compute_edge_id(src, inp, cond2, norm1))
        # Normalization sensitivity
        self.assertNotEqual(e1, compute_edge_id(src, inp, cond1, norm2))


class TestBehaviorAtlasStore(unittest.TestCase):
    """Test BA-01 SQLite store operations and serialization."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp_dir.name) / "test-atlas.sqlite"
        self.store = AtlasStore(self.db_path)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_node_crud(self):
        nid = compute_node_id("BOOT", occurrence=1)
        node = Node(
            node_id=nid,
            anchor="BOOT",
            occurrence=1,
            config_id="default",
            tags=["boot", "initial"],
            description="Cold boot state",
        )
        self.store.insert_node(node)

        fetched = self.store.get_node(nid)
        self.assertIsNotNone(fetched)
        self.assertEqual(fetched.anchor, "BOOT")
        self.assertEqual(fetched.tags, ["boot", "initial"])
        self.assertEqual(len(self.store.list_nodes()), 1)

    def test_edge_crud(self):
        n1_id = compute_node_id("BOOT", occurrence=1)
        n2_id = compute_node_id("TITLE_MENU", occurrence=1)
        self.store.insert_node(Node(node_id=n1_id, anchor="BOOT"))
        self.store.insert_node(Node(node_id=n2_id, anchor="TITLE_MENU"))

        eid = compute_edge_id(n1_id, "digest1", {"kind": "frame_count", "count": 60}, {})
        edge = Edge(
            edge_id=eid,
            src_node_id=n1_id,
            dst_node_id=n2_id,
            label="boot_to_title",
            duration_frames=60,
            completion_condition=CompletionCondition(kind="frame_count", count=60),
            status="port_verified",
        )
        self.store.insert_edge(edge)

        fetched = self.store.get_edge(eid)
        self.assertIsNotNone(fetched)
        self.assertEqual(fetched.label, "boot_to_title")
        self.assertEqual(len(self.store.get_outgoing_edges(n1_id)), 1)
        self.assertEqual(len(self.store.get_incoming_edges(n2_id)), 1)

    def test_export_import_json(self):
        n1_id = compute_node_id("BOOT", occurrence=1)
        self.store.insert_node(Node(node_id=n1_id, anchor="BOOT"))
        self.store.register_entry_node(n1_id, label="BOOT")

        json_path = Path(self.tmp_dir.name) / "atlas.json"
        self.store.export_json(json_path)
        self.assertTrue(json_path.exists())

        # Fresh store
        db2_path = Path(self.tmp_dir.name) / "test-atlas-2.sqlite"
        store2 = AtlasStore(db2_path)
        res = store2.import_json(json_path)
        self.assertEqual(res["nodes_imported"], 1)
        self.assertIsNotNone(store2.get_node(n1_id))


class TestScenarioImporter(unittest.TestCase):
    """Test BA-02 scenario corpus importer."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp_dir.name) / "test-atlas.sqlite"
        self.store = AtlasStore(self.db_path)
        self.importer = ScenarioImporter(self.store)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_import_all_active_scenarios(self):
        res = self.importer.import_all_scenarios()
        self.assertGreater(res["imported_scenarios"], 20)
        self.assertEqual(res["errors"], [])
        
        summary = self.store.summary()
        self.assertGreater(summary["total_nodes"], 10)
        self.assertGreater(summary["total_edges"], 20)
        self.assertGreater(summary["scenarios_indexed"], 20)


class TestAtlasRunner(unittest.TestCase):
    """Test BA-03 atlas traversal runner, graph pathfinding, and cycle detection."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp_dir.name) / "test-atlas.sqlite"
        self.store = AtlasStore(self.db_path)
        self.runner = AtlasRunner(self.store)

        # Build a synthetic graph:
        # N0 -> N1 -> N2 -> N3
        #  |          ^
        #  +-> N4 ----+
        self.n0 = compute_node_id("N0", 1)
        self.n1 = compute_node_id("N1", 1)
        self.n2 = compute_node_id("N2", 1)
        self.n3 = compute_node_id("N3", 1)
        self.n4 = compute_node_id("N4", 1)

        for nid in (self.n0, self.n1, self.n2, self.n3, self.n4):
            self.store.insert_node(Node(node_id=nid, anchor=f"Anchor_{nid[:4]}"))

        self.e01 = compute_edge_id(self.n0, "d01", {"kind": "frame_count", "count": 10}, {})
        self.e12 = compute_edge_id(self.n1, "d12", {"kind": "frame_count", "count": 10}, {})
        self.e23 = compute_edge_id(self.n2, "d23", {"kind": "frame_count", "count": 10}, {})
        self.e04 = compute_edge_id(self.n0, "d04", {"kind": "frame_count", "count": 10}, {})
        self.e42 = compute_edge_id(self.n4, "d42", {"kind": "frame_count", "count": 10}, {})

        self.store.insert_edge(Edge(edge_id=self.e01, src_node_id=self.n0, dst_node_id=self.n1, duration_frames=10, label="e01", status="port_verified"))
        self.store.insert_edge(Edge(edge_id=self.e12, src_node_id=self.n1, dst_node_id=self.n2, duration_frames=10, label="e12", status="port_verified"))
        self.store.insert_edge(Edge(edge_id=self.e23, src_node_id=self.n2, dst_node_id=self.n3, duration_frames=10, label="e23", status="port_verified"))
        self.store.insert_edge(Edge(edge_id=self.e04, src_node_id=self.n0, dst_node_id=self.n4, duration_frames=10, label="e04", status="port_verified"))
        self.store.insert_edge(Edge(edge_id=self.e42, src_node_id=self.n4, dst_node_id=self.n2, duration_frames=10, label="e42", status="port_verified"))

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_bfs_shortest_path(self):
        path = self.runner.find_path(self.n0, self.n3)
        self.assertIsNotNone(path)
        self.assertEqual(len(path), 3)  # N0->N1->N2->N3 or N0->N4->N2->N3 (both len 3)
        self.assertEqual(path[0].src_node_id, self.n0)
        self.assertEqual(path[-1].dst_node_id, self.n3)

    def test_run_traversal_success(self):
        res = self.runner.run_traversal([self.e01, self.e12, self.e23])
        self.assertTrue(res.certified)
        self.assertEqual(res.total_frames, 30)
        self.assertEqual(len(res.steps), 3)
        self.assertEqual(res.end_node_id, self.n3)

    def test_run_traversal_discontinuous_fails(self):
        # e01 -> e23 (missing e12)
        res = self.runner.run_traversal([self.e01, self.e23])
        self.assertFalse(res.certified)
        self.assertEqual(res.steps[1].status, "topology_mismatch")

    def test_cycle_detection(self):
        # Add cycle N3 -> N0
        e30 = compute_edge_id(self.n3, "d30", {"kind": "frame_count", "count": 10}, {})
        self.store.insert_edge(Edge(edge_id=e30, src_node_id=self.n3, dst_node_id=self.n0, label="cycle"))
        cycles = self.runner.detect_cycles()
        self.assertGreaterEqual(len(cycles), 1)


class TestActionGrammars(unittest.TestCase):
    """Test BA-04 action grammars and sequence compiler."""

    def test_title_grammar_compilation(self):
        grammar = GrammarRegistry.get_title_menu_grammar()
        inputs = compile_action_sequence(grammar, ["navigate_down", "select_option"])
        self.assertGreaterEqual(len(inputs), 2)
        self.assertEqual(inputs[0]["mask"], BTN_DOWN)
        self.assertEqual(inputs[1]["mask"], BTN_A)
        self.assertGreater(inputs[1]["frame"], inputs[0]["frame"])

    def test_house_pause_grammar_compilation(self):
        grammar = GrammarRegistry.get_house_pause_grammar()
        inputs = compile_action_sequence(grammar, ["open_pause", "navigate_to_save"])
        self.assertGreaterEqual(len(inputs), 4)
        self.assertEqual(inputs[0]["mask"], BTN_ESC)

    def test_unknown_action_raises(self):
        grammar = GrammarRegistry.get_title_menu_grammar()
        with self.assertRaises(ValueError):
            compile_action_sequence(grammar, ["nonexistent_action"])


class TestBehaviorAtlasScheduler(unittest.TestCase):
    """Test BA-05 coverage-guided exploration scheduler and frontier prioritizer."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp_dir.name) / "test-sched-atlas.sqlite"
        self.store = AtlasStore(self.db_path)
        self.runner = AtlasRunner(self.store)

        # Insert root title node
        self.n_root = compute_node_id("TITLE_MENU", occurrence=1)
        self.store.insert_node(Node(
            node_id=self.n_root,
            anchor="TITLE_MENU",
            occurrence=1,
            tags=["root", "title"],
        ))

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_multi_factor_scoring(self):
        config = SchedulerConfig(
            coverage_weight=10.0,
            novelty_weight=5.0,
            rare_branch_weight=8.0,
            debt_unblock_weight=6.0,
            visit_penalty=2.0,
        )
        scheduler = CoverageGuidedScheduler(store=self.store, config=config)
        node = self.store.get_node(self.n_root)
        self.assertIsNotNone(node)

        # Standard action
        spec_standard = {
            "expected_completion": {"anchor": "TITLE_MENU"},
            "action_tags": ["nav"],
        }
        score1, factors1 = scheduler.score_candidate(
            node=node,
            action_name="navigate_down",
            action_spec=spec_standard,
            scene="title_menu",
            depth=0,
            visit_count=0,
            executed_actions=set(),
            rare_anchors={"OPTIONS_MENU_READY"},
            known_debt_tags={"title-options"},
        )
        self.assertGreater(score1, 0)
        self.assertEqual(factors1["novelty"], 5.0)
        self.assertEqual(factors1["rare_branch"], 0.0)

        # Rare branch + debt unblocking action
        spec_rare = {
            "expected_completion": {"anchor": "OPTIONS_MENU_READY"},
            "action_tags": ["title-options"],
        }
        score2, factors2 = scheduler.score_candidate(
            node=node,
            action_name="select_option",
            action_spec=spec_rare,
            scene="title_menu",
            depth=0,
            visit_count=0,
            executed_actions=set(),
            rare_anchors={"OPTIONS_MENU_READY"},
            known_debt_tags={"title-options"},
        )
        # Rare + debt + coverage should have much higher priority score
        self.assertGreater(score2, score1)
        self.assertEqual(factors2["rare_branch"], 8.0)
        self.assertEqual(factors2["debt_unblock"], 6.0)

        # Visit penalty reduces score
        score3, factors3 = scheduler.score_candidate(
            node=node,
            action_name="navigate_down",
            action_spec=spec_standard,
            scene="title_menu",
            depth=0,
            visit_count=3,
            executed_actions={ (node.node_id, "navigate_down") },
            rare_anchors={"OPTIONS_MENU_READY"},
            known_debt_tags={"title-options"},
        )
        self.assertLess(score3, score1)
        self.assertEqual(factors3["novelty"], 0.0)
        self.assertEqual(factors3["visit_penalty"], -6.0)

    def test_scheduler_exploration_reaches_rare_branch(self):
        config = SchedulerConfig(
            max_iterations=30,
            max_depth=5,
            rare_branch_weight=15.0,
            random_seed=123,
        )
        scheduler = CoverageGuidedScheduler(store=self.store, config=config)
        res = scheduler.explore(
            start_node_id=self.n_root,
            rare_anchors={"OPTIONS_MENU_READY"},
        )
        self.assertGreater(res.total_iterations, 0)
        self.assertGreaterEqual(res.rare_branches_reached, 1)
        self.assertGreater(res.nodes_discovered, 1)
        self.assertGreater(res.edges_discovered, 0)

    def test_divergence_detection_and_early_stop(self):
        config = SchedulerConfig(
            max_iterations=50,
            stop_on_divergence=True,
            random_seed=42,
        )
        scheduler = CoverageGuidedScheduler(store=self.store, config=config)

        # Synthetic divergence hook triggered on select_option
        def mock_divergence_hook(curr_node: Node, action_spec: dict):
            if action_spec.get("expected_completion", {}).get("anchor") == "OPTIONS_MENU_READY":
                return {
                    "kind": "state_divergence",
                    "field": "g_cursor_pos",
                    "expected": 0,
                    "actual": 1,
                    "anchor": curr_node.anchor,
                }
            return None

        res = scheduler.explore(
            start_node_id=self.n_root,
            divergence_hook=mock_divergence_hook,
        )
        self.assertEqual(res.divergences_found, 1)
        self.assertGreaterEqual(len(res.divergences), 1)
        self.assertEqual(res.divergences[0]["kind"], "state_divergence")
        self.assertEqual(res.execution_log[-1].outcome, "divergence")

    def test_scheduler_seed_reproducibility(self):
        config1 = SchedulerConfig(max_iterations=10, random_seed=999)
        config2 = SchedulerConfig(max_iterations=10, random_seed=999)
        
        res1 = CoverageGuidedScheduler(store=self.store, config=config1).explore(self.n_root)
        res2 = CoverageGuidedScheduler(store=self.store, config=config2).explore(self.n_root)

        self.assertEqual(res1.total_iterations, res2.total_iterations)
        self.assertEqual(len(res1.execution_log), len(res2.execution_log))
        for s1, s2 in zip(res1.execution_log, res2.execution_log):
            self.assertEqual(s1.action_name, s2.action_name)
            self.assertEqual(s1.score, s2.score)


class TestBehaviorAtlasMinimizer(unittest.TestCase):
    """Test BA-06 hierarchical trace minimizer and delta-debugging."""

    def test_wait_frame_reduction(self):
        # Trace with unnecessarily large wait frame
        trace = [
            {"frame": 0, "buttons": ["ESC"], "mask": BTN_ESC},
            {"wait": 50},
            {"frame": 51, "buttons": ["A"], "mask": BTN_A},
        ]

        # Evaluator passes as long as wait >= 5
        def evaluator(candidate: list) -> Optional[DivergenceSignature]:
            has_esc = any(item.get("mask") == BTN_ESC for item in candidate)
            has_a = any(item.get("mask") == BTN_A for item in candidate)
            wait_val = sum(item.get("wait", 0) for item in candidate)
            if has_esc and has_a and wait_val >= 5:
                return DivergenceSignature(kind="pause_divergence", field_or_key="menu_state", expected_value=1, actual_value=2)
            return None

        config = MinimizerConfig(enable_action_chunk_removal=False, enable_repeat_coalescing=False, enable_frame_delta_debug=False)
        minimizer = TraceMinimizer(config=config)
        report = minimizer.minimize(trace, evaluator=evaluator)

        self.assertEqual(report.verdict, "MINIMIZED")
        self.assertTrue(report.divergence_signature_preserved)
        self.assertLess(report.minimized_length_frames, report.original_length_frames)
        # The wait should be reduced to 5
        wait_item = [x for x in report.minimized_trace if "wait" in x][0]
        self.assertEqual(wait_item["wait"], 5)

    def test_action_chunk_elimination(self):
        # Trace with 3 distinct action chunks, only chunk 2 triggers divergence
        trace = [
            {"action": "chunk_1", "buttons": ["UP"], "mask": 1},
            {"wait": 5},
            {"action": "chunk_2", "buttons": ["ESC"], "mask": BTN_ESC},
            {"wait": 5},
            {"action": "chunk_3", "buttons": ["DOWN"], "mask": BTN_DOWN},
        ]

        def evaluator(candidate: list) -> Optional[DivergenceSignature]:
            if any(item.get("action") == "chunk_2" for item in candidate):
                return DivergenceSignature(kind="esc_failure", field_or_key="pause", expected_value=0, actual_value=1)
            return None

        config = MinimizerConfig(enable_wait_reduction=False, enable_repeat_coalescing=False, enable_frame_delta_debug=False)
        minimizer = TraceMinimizer(config=config)
        report = minimizer.minimize(trace, evaluator=evaluator)

        self.assertEqual(report.verdict, "MINIMIZED")
        self.assertTrue(report.divergence_signature_preserved)
        actions_remaining = [x.get("action") for x in report.minimized_trace if "action" in x]
        self.assertEqual(actions_remaining, ["chunk_2"])

    def test_repeated_input_coalescing(self):
        # Trace holding button A for 10 frames consecutively
        trace = [
            {"frame": 0, "buttons": ["A"], "mask": BTN_A},
            {"frame": 1, "buttons": ["A"], "mask": BTN_A},
            {"frame": 2, "buttons": ["A"], "mask": BTN_A},
            {"frame": 3, "buttons": ["A"], "mask": BTN_A},
            {"frame": 4, "buttons": ["A"], "mask": BTN_A},
        ]

        # Evaluator only needs button A pressed at least 1 frame
        def evaluator(candidate: list) -> Optional[DivergenceSignature]:
            if any(item.get("mask") == BTN_A for item in candidate):
                return DivergenceSignature(kind="button_a_divergence", field_or_key="dialogue", expected_value=0, actual_value=1)
            return None

        config = MinimizerConfig(enable_action_chunk_removal=False, enable_wait_reduction=False, enable_frame_delta_debug=False)
        minimizer = TraceMinimizer(config=config)
        report = minimizer.minimize(trace, evaluator=evaluator)

        self.assertEqual(report.verdict, "MINIMIZED")
        self.assertEqual(len(report.minimized_trace), 1)
        self.assertEqual(report.minimized_trace[0]["mask"], BTN_A)

    def test_flakiness_detection(self):
        trace = [{"frame": 0, "buttons": ["A"], "mask": BTN_A}]
        eval_count = 0

        # Non-deterministic evaluator returns different signature on alternate evaluations
        def flaky_evaluator(candidate: list) -> Optional[DivergenceSignature]:
            nonlocal eval_count
            eval_count += 1
            if eval_count % 2 == 1:
                return DivergenceSignature(kind="flaky_sig_1", expected_value=1, actual_value=2)
            else:
                return DivergenceSignature(kind="flaky_sig_2", expected_value=3, actual_value=4)

        config = MinimizerConfig(verification_repeats=2)
        minimizer = TraceMinimizer(config=config)
        report = minimizer.minimize(trace, evaluator=flaky_evaluator)

        self.assertEqual(report.verdict, "INCONCLUSIVE")
        self.assertFalse(report.divergence_signature_preserved)

class TestBehaviorAtlasRNGSolver(unittest.TestCase):
    """Test BA-07 RNG mathematics, callsite mapping, and seed solver."""

    def test_rng_step_forward_matches_msvc_constants(self):
        # From seed 1:
        # next_seed = (1 * 0x343fd + 0x269ec3) = 0x29e2c0 (2745024)
        # val15 = (0x29e2c0 >> 16) & 0x7fff = 0x29 = 41
        s1, val1 = rng_step(1)
        self.assertEqual(s1, 0x29e2c0)
        self.assertEqual(val1, 41)

        # Step 2:
        s2, val2 = rng_step(s1)
        self.assertEqual(s2, 0xc823f683)
        self.assertEqual(val2, 18467)
    def test_rng_step_backward_inversion(self):
        test_seeds = [0, 1, 42, 12345, 0x12345678, 0x7fffffff, 0xffffffff]
        for s in test_seeds:
            next_s, _ = rng_step(s)
            prev_s = rng_step_back(next_s)
            self.assertEqual(prev_s, s, f"Inversion failed for seed 0x{s:x}")

    def test_rng_arbitrary_step_jump(self):
        start_seed = 0x12345678
        # 100 consecutive single steps
        curr = start_seed
        for _ in range(100):
            curr, _ = rng_step(curr)

        # O(log k) jump
        jumped = rng_jump(start_seed, 100)
        self.assertEqual(jumped, curr)

        # Negative jump (jump back 100 steps)
        jumped_back = rng_jump(jumped, -100)
        self.assertEqual(jumped_back, start_seed)

    def test_datetime_seed_computation(self):
        # 2026-08-22 12:00:00
        seed = rng_compute_seed(year=2026, month=8, day=22, hour=12, minute=0, second=0, dst=0)
        self.assertGreater(seed, 0)
        self.assertLessEqual(seed, 0xffffffff)

        # Invalid month / year out of bounds
        self.assertEqual(rng_compute_seed(2026, 13, 1, 0, 0, 0), -1)
        self.assertEqual(rng_compute_seed(1850, 1, 1, 0, 0, 0), -1)

    def test_callsite_registry(self):
        all_sites = RNGCallsiteRegistry.list_all()
        self.assertGreaterEqual(len(all_sites), 10)

        cs_pick = RNGCallsiteRegistry.get_by_va(0x00460a1a)
        self.assertIsNotNone(cs_pick)
        self.assertEqual(cs_pick.symbol, "cs_pick_line")
        self.assertEqual(cs_pick.consumer_type, "dialogue_variant")

        haggle = RNGCallsiteRegistry.get_by_va(0x00460672)
        self.assertIsNotNone(haggle)
        self.assertEqual(haggle.consumer_type, "haggle_tolerance")

    def test_seed_solver_predicate(self):
        # Target predicate: first draw % 2 == 0 (variant 'Capitalism, ho!')
        # and second draw in [1000, 5000]
        def target_pred(vals: list) -> bool:
            if len(vals) < 2:
                return False
            return (vals[0] % 2 == 0) and (1000 <= vals[1] <= 5000)

        sol = RNGSeedSolver.solve_for_sequence_predicate(
            predicate_fn=target_pred,
            draw_count=2,
            start_seed=1,
            max_search_steps=1000,
        )
        self.assertIsNotNone(sol)
        self.assertTrue(target_pred(sol.matching_values))


class TestBehaviorAtlasHealth(unittest.TestCase):
    """Test BA-08 Behavior Atlas health and integrity reporting."""

    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp_dir.name) / "test-health-atlas.sqlite"
        self.store = AtlasStore(self.db_path)
        self.checker = AtlasHealthChecker(self.store)

    def tearDown(self):
        self.tmp_dir.cleanup()

    def test_health_report_empty_atlas(self):
        report = self.checker.check_health()
        self.assertEqual(report.total_nodes, 0)
        self.assertEqual(report.total_edges, 0)
        self.assertEqual(report.certification_ratio, 0.0)
        ascii_text = report.format_ascii()
        self.assertIn("Graph Inventory & Edge Certification", ascii_text)

    def test_health_report_with_topology_and_risks(self):
        # Node 0 (Entry) -> Node 1 (Proven) -> Node 2 (Divergent)
        # Node 3 (Unreachable disconnected)
        n0 = compute_node_id("BOOT", occurrence=1)
        n1 = compute_node_id("TITLE_MENU", occurrence=1)
        n2 = compute_node_id("OPTIONS_MENU_READY", occurrence=1)
        n3 = compute_node_id("ORPHAN_NODE", occurrence=1)

        self.store.insert_node(Node(node_id=n0, anchor="BOOT"))
        self.store.insert_node(Node(node_id=n1, anchor="TITLE_MENU"))
        self.store.insert_node(Node(node_id=n2, anchor="OPTIONS_MENU_READY"))
        self.store.insert_node(Node(node_id=n3, anchor="ORPHAN_NODE"))
        self.store.register_entry_node(n0, label="Boot Entry")

        # Edges
        e01 = compute_edge_id(n0, "d01", {"kind": "anchor_reached", "anchor": "TITLE_MENU"}, {})
        e12 = compute_edge_id(n1, "d12", {"kind": "anchor_reached", "anchor": "OPTIONS_MENU_READY"}, {})

        self.store.insert_edge(Edge(edge_id=e01, src_node_id=n0, dst_node_id=n1, status="proven", label="title_menu:boot"))
        self.store.insert_edge(Edge(edge_id=e12, src_node_id=n1, dst_node_id=n2, status="divergent", label="title_menu:options"))

        report = self.checker.check_health()
        self.assertEqual(report.total_nodes, 4)
        self.assertEqual(report.total_edges, 2)
        self.assertEqual(report.proven_edges, 1)
        self.assertEqual(report.divergent_edges, 1)
        self.assertEqual(len(report.unreachable_nodes), 1)
        self.assertEqual(report.unreachable_nodes[0], n3)

        # Risk factors detected
        self.assertGreaterEqual(len(report.risk_factors), 1)
        self.assertTrue(any("divergence" in r.lower() for r in report.risk_factors))
        self.assertTrue(any("unreachable" in r.lower() for r in report.risk_factors))

if __name__ == "__main__":
    unittest.main()
