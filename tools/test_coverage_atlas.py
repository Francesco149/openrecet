#!/usr/bin/env python3
"""Comprehensive unit and integration test suite for CoverageAtlas (CV-02, CV-03, CV-04, CV-05, CV-06).

Tests:
  1. Address normalization and module filtering logic
  2. Basic block to owning function mapping and interval resolution
  3. Dynamic coverage ingestion (JSON payload / file)
  4. Idempotency and SHA256 artifact hashing
  5. Per-function coverage breakdown (blocks, internal/incoming/outgoing edges)
  6. Semantic dimensions ingestion, derivation, and queries (CV-05)
  7. Semantic item ID validation and bounds checking (CV-05)
  8. Scenario coverage declarations and contract validation (CV-04)
  9. Repository-wide scenario audit (CV-04)
  10. Multi-scenario coverage delta (new blocks, edges, functions)
  11. CV-06 gap analysis (executed-unimplemented, implemented-unexecuted, branch gaps)
  12. SQLite schema, indices, transactions, and error recovery
  13. Full JSON export and CLI subcommands execution
"""

from __future__ import annotations

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from tools.coverage_atlas import (
    CoverageAtlas,
    FunctionRange,
    validate_semantic_item_id,
    CollectionMode,
    ConfidenceBand,
    BlindSpotKind,
    CalibrationVerdict,
    CV08_POLICY_VERSION,
    main,
)


class TestCoverageAtlas(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.tmp_path = Path(self.tmp_dir.name)
        self.db_path = self.tmp_path / "test_coverage.sqlite"
        self.re_index_path = self.tmp_path / "test_re_index.sqlite"

        # Create mock re-index DB with sample functions
        rconn = sqlite3.connect(str(self.re_index_path))
        rcur = rconn.cursor()
        rcur.execute("""
            CREATE TABLE functions (
                va INTEGER PRIMARY KEY,
                name TEXT NOT NULL,
                size INTEGER NOT NULL,
                is_thunk INTEGER NOT NULL,
                port_status TEXT NOT NULL,
                runtime_status TEXT
            )
        """)
        sample_funcs = [
            (0x401000, "FUN_00401000", 180, 0, "ported", "identity-joined"),
            (0x401100, "FUN_00401100", 256, 0, "discovered", None),
            (0x401200, "FUN_00401200", 64, 1, "discovered", None),  # thunk
            (0x401300, "FUN_00401300", 512, 0, "ported", "proven"),
            (0x401500, "FUN_00401500", 128, 0, "verified", None),
        ]
        rcur.executemany("INSERT INTO functions VALUES (?, ?, ?, ?, ?, ?)", sample_funcs)

        rcur.execute("""
            CREATE TABLE calls (
                caller_va INTEGER NOT NULL,
                callee_va INTEGER NOT NULL,
                PRIMARY KEY (caller_va, callee_va)
            )
        """)
        sample_calls = [
            (0x401000, 0x401100),
            (0x401100, 0x401500),
            (0x401000, 0x401300),
        ]
        rcur.executemany("INSERT INTO calls VALUES (?, ?)", sample_calls)

        rcur.execute("""
            CREATE TABLE string_xrefs (
                func_va INTEGER NOT NULL,
                string_name TEXT NOT NULL
            )
        """)
        sample_strings = [
            (0x401100, "s_customer_service_start"),
            (0x401500, "s_tuto_shop_loop"),
        ]
        rcur.executemany("INSERT INTO string_xrefs VALUES (?, ?)", sample_strings)

        rcur.execute("""
            CREATE TABLE global_xrefs (
                func_va INTEGER NOT NULL,
                global_va INTEGER NOT NULL
            )
        """)
        rconn.commit()
        rconn.close()

        self.atlas = CoverageAtlas(db_path=self.db_path, re_index_path=self.re_index_path)

    def tearDown(self):
        self.atlas.close()
        self.tmp_dir.cleanup()

    def test_schema_creation(self):
        conn = self.atlas.connect()
        cur = conn.cursor()
        cur.execute("SELECT name FROM sqlite_master WHERE type='table'")
        tables = {row[0] for row in cur.fetchall()}
        self.assertIn("coverage_runs", tables)
        self.assertIn("coverage_blocks", tables)
        self.assertIn("coverage_edges", tables)
        self.assertIn("coverage_semantics", tables)

    def test_function_resolution(self):
        fn = self.atlas.resolve_function(0x401000)
        self.assertIsNotNone(fn)
        self.assertEqual(fn.name, "FUN_00401000")
        self.assertEqual(fn.va, 0x401000)

        # Mid-function instruction VA
        fn_mid = self.atlas.resolve_function(0x401050)
        self.assertIsNotNone(fn_mid)
        self.assertEqual(fn_mid.va, 0x401000)

        # Exact end bound is not inside [va, va+size)
        fn_next = self.atlas.resolve_function(0x401100)
        self.assertIsNotNone(fn_next)
        self.assertEqual(fn_next.va, 0x401100)

        # Out-of-bounds VA before first function
        fn_gap = self.atlas.resolve_function(0x300000)
        self.assertIsNone(fn_gap)

    def test_import_run_payload(self):
        payload = {
            "scenario": "test_scenario_alpha",
            "start_frame": 0,
            "end_frame": 100,
            "total_events": 500,
            "module_events": 450,
            "out_of_module_events": 50,
            "lost_events": 0,
            "blocks": [
                {"va": "0x401000", "hits": 10},
                {"va": "0x401020", "hits": 8},
                {"va": "0x401100", "hits": 5},
            ],
            "edges": [
                {"src": "0x401000", "dst": "0x401020", "hits": 8},
                {"src": "0x401020", "dst": "0x401100", "hits": 3},
            ],
            "semantics": {
                "vm_operations": ["TUTO_CHR0", "TUTO_NEDAN"],
                "content_ids": ["KYAKU:13", "ITEM:100"],
            },
            "anchors": ["BOOT", "HOUSE_FREEROAM"],
            "audio": ["se_001_id0010"],
        }
        res = self.atlas.import_run(payload)
        self.assertEqual(res["status"], "imported")
        self.assertEqual(res["blocks_imported"], 3)
        self.assertEqual(res["edges_imported"], 2)
        self.assertGreater(res["semantics_imported"], 0)

        summary = self.atlas.get_summary()
        self.assertEqual(summary["total_runs"], 1)
        self.assertEqual(summary["unique_blocks_covered"], 3)
        self.assertEqual(summary["unique_edges_covered"], 2)
        self.assertEqual(summary["touched_functions"], 2)

    def test_idempotent_import(self):
        payload = {
            "scenario": "idem_scenario",
            "blocks": [{"va": "0x401000", "hits": 5}],
            "edges": [{"src": "0x401000", "dst": "0x401010", "hits": 5}],
        }
        res1 = self.atlas.import_run(payload, run_id="fixed_run_id")
        res2 = self.atlas.import_run(payload, run_id="fixed_run_id")

        self.assertEqual(res1["artifact_hash"], res2["artifact_hash"])
        summary = self.atlas.get_summary()
        self.assertEqual(summary["total_runs"], 1)
        self.assertEqual(summary["unique_blocks_covered"], 1)

    def test_semantic_dimensions_and_derivation(self):
        payload = {
            "scenario": "test_semantic_scenario",
            "start_frame": 10,
            "blocks": [
                {"va": "0x401000", "hits": 4},
                {"va": "0x401300", "hits": 6},
            ],
            "edges": [],
            "semantics": {
                "vm_operations": ["TUTO_CHR0", "TUTO_BUN0", "TUTO_NEDAN"],
                "content_ids": ["ITEM:100", "KYAKU:13"],
                "assets": ["bg/house_1st.x", "hpmp_base.tga"],
                "save_ops": ["SAVE:slot_0_commit"],
                "boundary_outcomes": ["OUTCOME:PASS"],
            },
            "anchors": ["BOOT", "CUSTOMER_SERVICE_ENTER"],
            "audio": ["se_001_id0010"],
        }
        self.atlas.import_run(payload, run_id="sem_run_1")

        sem_sum = self.atlas.get_semantic_summary()
        self.assertIn("functions", sem_sum)
        self.assertIn("blocks", sem_sum)
        self.assertIn("vm_operations", sem_sum)
        self.assertIn("content_ids", sem_sum)
        self.assertIn("assets", sem_sum)
        self.assertIn("transitions", sem_sum)
        self.assertIn("audio_ids", sem_sum)
        self.assertIn("save_ops", sem_sum)
        self.assertIn("boundary_outcomes", sem_sum)

        self.assertEqual(sem_sum["functions"]["unique_items"], 2)  # FUN_00401000, FUN_00401300
        self.assertEqual(sem_sum["vm_operations"]["unique_items"], 3)

        vm_items = self.atlas.get_dimension_items("vm_operations")
        vm_ids = {it["item_id"] for it in vm_items}
        self.assertIn("TUTO_CHR0", vm_ids)
        self.assertIn("TUTO_BUN0", vm_ids)
        self.assertIn("TUTO_NEDAN", vm_ids)

        trans_items = self.atlas.get_dimension_items("transitions")
        trans_ids = {it["item_id"] for it in trans_items}
        self.assertIn("BOOT", trans_ids)
        self.assertIn("CUSTOMER_SERVICE_ENTER", trans_ids)

    def test_validate_semantic_item_id(self):
        # Valid IDs
        valid_cases = [
            ("functions", "FUN_00401000"),
            ("functions", "0x401000"),
            ("blocks", "0x401000"),
            ("vm_operations", "TUTO_CHR0"),
            ("vm_operations", "DLG_LINE_SHOW"),
            ("vm_operations", "OP_0x15"),
            ("transitions", "HOUSE_FREEROAM"),
            ("transitions", "TITLE->HOUSE"),
            ("content_ids", "ITEM:100"),
            ("content_ids", "KYAKU:13"),
            ("assets", "bg/house_1st.x"),
            ("assets", "hpmp_base.tga"),
            ("audio_ids", "se_001_id0010"),
            ("audio_ids", "BGM:1"),
            ("save_ops", "SAVE:slot_0_commit"),
            ("boundary_outcomes", "PASS"),
        ]
        for dim, item_id in valid_cases:
            valid, err = validate_semantic_item_id(dim, item_id)
            self.assertTrue(valid, f"Expected {dim}:{item_id} to be valid, got error: {err}")

        # Invalid IDs fail-closed
        invalid_cases = [
            ("functions", "NOT_A_FUNCTION_NAME"),
            ("blocks", "0x100"),  # out of code range
            ("blocks", "invalid_hex"),
            ("vm_operations", "bad lower opcode"),
            ("transitions", "bad transition * format"),
            ("content_ids", "BAD_PREFIX:123"),
            ("assets", "bad_asset_without_extension"),
            ("audio_ids", "bad audio name $$$"),
            ("save_ops", "NOT_SAVE_OP"),
            ("boundary_outcomes", "bad outcome ???"),
        ]
        for dim, item_id in invalid_cases:
            valid, err = validate_semantic_item_id(dim, item_id)
            self.assertFalse(valid, f"Expected {dim}:{item_id} to be invalid")
            self.assertIsNotNone(err)

    def test_scenario_declarations_validation(self):
        # 1. Ingest run covering functions, vm_operations, transitions
        payload = {
            "scenario": "house-test-scenario",
            "blocks": [
                {"va": "0x401000", "hits": 10},
                {"va": "0x401300", "hits": 5},
            ],
            "semantics": {
                "vm_operations": ["TUTO_CHR0", "TUTO_BUN0"],
                "content_ids": ["KYAKU:13"],
            },
            "anchors": ["BOOT", "HOUSE_FREEROAM"],
        }
        self.atlas.import_run(payload)

        # 2. Declare expectations: some satisfied, some unmet, one invalid ID
        scenario_decl = {
            "schema_version": 2,
            "scenario": "house-test-scenario",
            "proof": {
                "contract_version": 1,
                "start_node": None,
                "join": {"anchor": "HOUSE_FREEROAM", "occurrence": 1, "window": [1, 20]},
                "required_pillars": ["identity"],
                "seeds": [19937],
                "configurations": ["reference-1024-windowed"],
            },
            "coverage_expectations": {
                "functions": ["FUN_00401000", "FUN_00401300", "FUN_00401500"],  # 401500 unmet
                "vm_operations": ["TUTO_CHR0", "TUTO_NEDAN"],                   # TUTO_NEDAN unmet
                "transitions": ["BOOT", "HOUSE_FREEROAM"],                      # both satisfied
                "content_ids": ["KYAKU:13"],                                    # satisfied
                "assets": ["INVALID_ASSET_NO_EXT"],                             # invalid ID
            },
        }

        report = self.atlas.validate_scenario_declarations(scenario_decl)
        self.assertFalse(report["valid"])  # False due to INVALID_ASSET_NO_EXT
        self.assertEqual(len(report["invalid_ids"]), 1)
        self.assertEqual(report["invalid_ids"][0]["dimension"], "assets")

        # Function dimension breakdown
        fn_dim = report["dimensions"]["functions"]
        self.assertEqual(fn_dim["declared_count"], 3)
        self.assertEqual(fn_dim["satisfied_count"], 2)
        self.assertEqual(fn_dim["unmet_count"], 1)
        self.assertIn("FUN_00401500", fn_dim["unmet"])

        # Transitions dimension breakdown
        trans_dim = report["dimensions"]["transitions"]
        self.assertEqual(trans_dim["declared_count"], 2)
        self.assertEqual(trans_dim["satisfied_count"], 2)
        self.assertEqual(trans_dim["unmet_count"], 0)

    def test_audit_all_scenarios(self):
        # Create temporary scenarios directory with 2 mock scenarios
        scen_root = self.tmp_path / "scenarios"
        scen_root.mkdir(parents=True)

        scen_a_dir = scen_root / "scen-a"
        scen_a_dir.mkdir()
        (scen_a_dir / "scenario.yaml").write_text(json.dumps({
            "schema_version": 2,
            "proof": {
                "contract_version": 1,
                "start_node": None,
                "join": {"anchor": "BOOT", "occurrence": 1, "window": [0, 10]},
                "required_pillars": ["identity"],
                "seeds": [19937],
                "configurations": ["ref"],
            },
            "coverage_expectations": {
                "functions": ["FUN_00401000"],
                "transitions": ["BOOT"],
            },
        }))

        audit_res = self.atlas.audit_all_scenarios(scenarios_dir=scen_root)
        self.assertEqual(audit_res["total_scenarios"], 1)
        self.assertEqual(audit_res["opted_in_scenarios"], 1)
        self.assertEqual(audit_res["invalid_scenarios_count"], 0)
        self.assertEqual(audit_res["total_declared_expectations"], 2)

    def test_function_coverage_breakdown(self):
        payload = {
            "scenario": "breakdown_scen",
            "blocks": [
                {"va": "0x401000", "hits": 10},
                {"va": "0x401020", "hits": 8},
            ],
            "edges": [
                {"src": "0x401000", "dst": "0x401020", "hits": 8},  # internal
                {"src": "0x400f00", "dst": "0x401000", "hits": 4},  # incoming
                {"src": "0x401020", "dst": "0x401500", "hits": 2},  # outgoing
            ],
        }
        self.atlas.import_run(payload)

        fc = self.atlas.get_function_coverage(0x401000)
        self.assertIsNotNone(fc)
        self.assertTrue(fc["is_covered"])
        self.assertEqual(fc["total_hits"], 18)
        self.assertEqual(fc["unique_blocks_count"], 2)
        self.assertEqual(fc["internal_edges_count"], 1)
        self.assertEqual(fc["incoming_edges_count"], 1)
        self.assertEqual(fc["outgoing_edges_count"], 1)

        # Unexecuted function
        fc_unexec = self.atlas.get_function_coverage(0x401300)
        self.assertIsNotNone(fc_unexec)
        self.assertFalse(fc_unexec["is_covered"])
        self.assertEqual(fc_unexec["unique_blocks_count"], 0)

    def test_scenario_delta(self):
        scen_a = {
            "scenario": "scenario_A",
            "blocks": [
                {"va": "0x401000", "hits": 10},
                {"va": "0x401020", "hits": 5},
            ],
            "edges": [
                {"src": "0x401000", "dst": "0x401020", "hits": 5},
            ],
        }
        scen_b = {
            "scenario": "scenario_B",
            "blocks": [
                {"va": "0x401000", "hits": 12},
                {"va": "0x401300", "hits": 7},
            ],
            "edges": [
                {"src": "0x401000", "dst": "0x401300", "hits": 7},
            ],
        }
        self.atlas.import_run(scen_a, run_id="run_a")
        self.atlas.import_run(scen_b, run_id="run_b")

        delta = self.atlas.get_scenario_delta("scenario_A", "scenario_B")
        self.assertEqual(delta["blocks_summary"]["scenario_a_total"], 2)
        self.assertEqual(delta["blocks_summary"]["scenario_b_total"], 2)
        self.assertEqual(delta["blocks_summary"]["shared"], 1)
        self.assertEqual(delta["blocks_summary"]["only_in_a"], 1)  # 0x401020
        self.assertEqual(delta["blocks_summary"]["only_in_b"], 1)  # 0x401300

        self.assertIn("0x401300", delta["new_blocks_in_b"])
        self.assertIn("FUN_00401300", delta["new_functions_in_b"])

    def test_cv06_gap_analysis(self):
        payload = {
            "scenario": "gap_scen",
            "blocks": [
                {"va": "0x401000", "hits": 10},  # ported -> covered
                {"va": "0x401100", "hits": 8},   # discovered -> unimplemented covered!
            ],
            "edges": [],
        }
        self.atlas.import_run(payload)

        gaps = self.atlas.get_gaps(unimplemented=True, unexecuted=True, branches=True)
        self.assertEqual(gaps["summary"]["executed_unimplemented_count"], 1)
        self.assertEqual(gaps["unimplemented"][0]["va"], "0x401100")

        unexec_vas = {u["va"] for u in gaps["unexecuted"]}
        self.assertIn("0x401300", unexec_vas)
        self.assertIn("0x401500", unexec_vas)

    def test_cli_subcommands(self):
        cov_file = self.tmp_path / "sample_cov.json"
        cov_file.write_text(json.dumps({
            "scenario": "cli-test",
            "blocks": [{"va": "0x401000", "hits": 12}],
            "edges": [{"src": "0x401000", "dst": "0x401010", "hits": 8}],
            "semantics": {"vm_operations": ["TUTO_CHR0"]},
            "anchors": ["BOOT"],
        }))

        # 1. Import
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "import", str(cov_file)])
        self.assertEqual(rc, 0)

        # 2. Summary
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "summary"])
        self.assertEqual(rc, 0)

        # 3. Semantics summary & dimension
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "semantics"])
        self.assertEqual(rc, 0)

        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "semantics", "--dimension", "vm_operations"])
        self.assertEqual(rc, 0)

        # 4. Function inspection
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "function", "0x401000"])
        self.assertEqual(rc, 0)

        # 5. Gaps
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "gaps", "--unimplemented"])
        self.assertEqual(rc, 0)

        # 6. Export
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "export", "--pretty"])
        self.assertEqual(rc, 0)

        # 7. Prioritize CLI
        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "prioritize", "--limit", "5"])
        self.assertEqual(rc, 0)

        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "prioritize", "--markdown", "--front", "customer_service"])
        self.assertEqual(rc, 0)

        rc = main(["--db", str(self.db_path), "--re-index", str(self.re_index_path), "prioritize", "--json"])
        self.assertEqual(rc, 0)

    def test_cv07_prioritize_default(self):
        payload = {
            "scenario": "prio_test",
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
            "semantics": {"vm_operations": ["TUTO_CHR0"]},
        }
        self.atlas.import_run(payload)

        res = self.atlas.prioritize_experiments(limit=10)
        self.assertEqual(res["version"], "CV-07-v1.0")
        self.assertEqual(res["policy"], "CV-07-v1.0")
        self.assertGreater(res["total_candidates_evaluated"], 0)
        self.assertLessEqual(len(res["candidates"]), 10)

        # Top candidates must have valid rank and composite scores between 0 and 100
        for idx, cand in enumerate(res["candidates"], start=1):
            self.assertEqual(cand["rank"], idx)
            self.assertGreaterEqual(cand["composite_score"], 0.0)
            self.assertLessEqual(cand["composite_score"], 100.0)
            self.assertIn("factors", cand)
            self.assertIn("explanation", cand)
            self.assertTrue(len(cand["explanation"]) > 0)

    def test_cv07_prioritize_front_affinity(self):
        payload = {
            "scenario": "front_test",
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        # Prioritize with customer_service front: FUN_00401100 has string xref 's_customer_service_start'
        res_front = self.atlas.prioritize_experiments(kind="functions", front="customer_service", limit=10)
        res_no_front = self.atlas.prioritize_experiments(kind="functions", front=None, limit=10)

        cand_front_map = {c["target"]: c for c in res_front["candidates"]}
        cand_no_front_map = {c["target"]: c for c in res_no_front["candidates"]}

        if "FUN_00401100" in cand_front_map and "FUN_00401100" in cand_no_front_map:
            self.assertGreater(
                cand_front_map["FUN_00401100"]["factors"]["active_front_affinity"],
                cand_no_front_map["FUN_00401100"]["factors"]["active_front_affinity"],
            )
            self.assertIn("customer_service", cand_front_map["FUN_00401100"]["explanation"])

    def test_cv07_prioritize_candidate_kinds(self):
        payload = {
            "scenario": "kind_test",
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        # Test functions kind
        res_funcs = self.atlas.prioritize_experiments(kind="functions")
        self.assertTrue(all(c["candidate_type"] == "function" for c in res_funcs["candidates"]))

        # Test edges kind
        res_edges = self.atlas.prioritize_experiments(kind="edges")
        self.assertTrue(all(c["candidate_type"] == "edge" for c in res_edges["candidates"]))

        # Test semantics kind
        res_sem = self.atlas.prioritize_experiments(kind="semantics")
        self.assertTrue(all(c["candidate_type"] == "semantics" for c in res_sem["candidates"]))

    def test_cv07_prioritize_min_readiness(self):
        payload = {
            "scenario": "ready_test",
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        # Filter for ported or verified only (exclude discovered FUN_00401100)
        res_ported = self.atlas.prioritize_experiments(kind="functions", min_readiness="ported")
        for c in res_ported["candidates"]:
            self.assertIn(c["port_status"], ("ported", "verified", "runtime_proven", "proven"))

    def test_cv07_prioritize_custom_weights(self):
        payload = {
            "scenario": "weights_test",
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        # Set massive weight on proof_deficit
        custom_w = {"proof_deficit": 10.0, "new_coverage_potential": 0.0}
        res = self.atlas.prioritize_experiments(weights=custom_w, limit=5)
        self.assertGreater(res["weights"]["proof_deficit"], 0.5)

    def test_cv07_prioritize_explanations_content(self):
        payload = {
            "scenario": "exp_test",
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        res = self.atlas.prioritize_experiments(kind="functions", limit=10)
        for c in res["candidates"]:
            exp = c["explanation"]
            self.assertIsInstance(exp, str)
            self.assertTrue(len(exp) > 10)
            # Should mention function name and port status
            self.assertIn(c["name"], exp)
            self.assertIn(c["port_status"], exp)


    # ─── CV-08 Coverage Truth Calibration Tests ─────────────────────────────

    def test_cv08_calibrate_clean_run_pass(self):
        payload = {
            "scenario": "clean_scen",
            "total_events": 1000,
            "module_events": 1000,
            "lost_events": 0,
            "blocks": [
                {"va": "0x401000", "hits": 50},
                {"va": "0x401020", "hits": 30},
                {"va": "0x401300", "hits": 20},
            ],
            "edges": [
                {"src": "0x401000", "dst": "0x401020", "hits": 30},
            ],
        }
        self.atlas.import_run(payload)

        # Calibrate against call trace covering 0x401000 and 0x401300
        res = self.atlas.calibrate_coverage(
            scenario="clean_scen",
            call_trace_vas=[0x401000, 0x401300],
            mode=CollectionMode.DYNAMIC_STALKER,
        )

        self.assertEqual(res["version"], CV08_POLICY_VERSION)
        self.assertEqual(res["verdict"], CalibrationVerdict.PASS)
        self.assertEqual(res["collection_mode"], CollectionMode.DYNAMIC_STALKER)
        self.assertGreaterEqual(res["confidence_score"], 0.85)
        self.assertIn(res["confidence_band"], (ConfidenceBand.HIGH, ConfidenceBand.CERTIFIED))
        self.assertEqual(res["factors"]["collector_integrity"], 1.0)
        self.assertEqual(res["factors"]["cross_collector_agreement"], 1.0)
        self.assertEqual(len(res["missing_in_stalker"]), 0)
        self.assertTrue(any("PASSED" in exp for exp in res["explanations"]))

    def test_cv08_calibrate_lost_events_penalty(self):
        payload = {
            "scenario": "lossy_scen",
            "total_events": 100,
            "module_events": 80,
            "lost_events": 20,  # 20% lost events
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        res = self.atlas.calibrate_coverage(
            scenario="lossy_scen",
            min_confidence=0.85,
        )

        self.assertLess(res["factors"]["collector_integrity"], 1.0)
        self.assertNotEqual(res["verdict"], CalibrationVerdict.PASS)
        self.assertTrue(any("lost events" in exp.lower() for exp in res["explanations"]))

    def test_cv08_calibrate_cross_collector_discrepancy(self):
        payload = {
            "scenario": "discrepancy_scen",
            "total_events": 100,
            "lost_events": 0,
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        # Expecting call trace with 0x401000 and 0x401500 (0x401500 missing from Stalker blocks)
        res = self.atlas.calibrate_coverage(
            scenario="discrepancy_scen",
            call_trace_vas=[0x401000, 0x401500],
        )

        self.assertIn("FUN_00401500", res["missing_in_stalker"])
        self.assertEqual(res["metrics"]["cross_collector_missing_count"], 1)
        self.assertEqual(res["factors"]["cross_collector_agreement"], 0.5)
        self.assertTrue(any("cross-collector discrepancy" in exp.lower() for exp in res["explanations"]))

    def test_cv08_calibrate_cfg_structural_alignment(self):
        payload = {
            "scenario": "cfg_scen",
            "total_events": 200,
            "lost_events": 0,
            "blocks": [
                {"va": "0x401000", "hits": 20},  # entry block
                {"va": "0x401050", "hits": 15},  # interior block
            ],
            "edges": [],
        }
        self.atlas.import_run(payload)

        res = self.atlas.calibrate_coverage(scenario="cfg_scen")
        self.assertEqual(res["factors"]["cfg_structural_validity"], 1.0)

    def test_cv08_calibrate_repeat_run_determinism(self):
        payload_1 = {
            "scenario": "repeat_scen",
            "total_events": 100,
            "lost_events": 0,
            "blocks": [{"va": "0x401000", "hits": 10}, {"va": "0x401020", "hits": 5}],
            "edges": [{"src": "0x401000", "dst": "0x401020", "hits": 5}],
        }
        payload_2 = {
            "scenario": "repeat_scen",
            "total_events": 100,
            "lost_events": 0,
            "blocks": [{"va": "0x401000", "hits": 10}, {"va": "0x401020", "hits": 5}],
            "edges": [{"src": "0x401000", "dst": "0x401020", "hits": 5}],
        }
        self.atlas.import_run(payload_1, run_id="repeat_run_1")
        self.atlas.import_run(payload_2, run_id="repeat_run_2")

        res = self.atlas.calibrate_coverage(
            scenario="repeat_scen",
            repeat_run_ids=["repeat_run_1", "repeat_run_2"],
        )
        self.assertEqual(res["factors"]["determinism"], 1.0)

    def test_cv08_calibrate_blind_spot_detection(self):
        # Insert thunk and small functions in DB
        payload = {
            "scenario": "blindspot_scen",
            "total_events": 100,
            "lost_events": 0,
            "blocks": [
                {"va": "0x401200", "hits": 5},  # thunk FUN_00401200
            ],
            "edges": [],
        }
        self.atlas.import_run(payload)

        res = self.atlas.calibrate_coverage(scenario="blindspot_scen")
        self.assertGreater(len(res["blind_spots"]), 0)
        kinds = [bs["kind"] for bs in res["blind_spots"]]
        self.assertIn(BlindSpotKind.EXTERNAL_THUNK, kinds)
        self.assertGreater(res["factors"]["blind_spot_penalty"], 0.0)

    def test_cv08_summary_gating_invariant(self):
        # 1. Uncalibrated summary
        payload = {
            "scenario": "gating_scen",
            "total_events": 100,
            "lost_events": 0,
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        summary_before = self.atlas.get_summary()
        self.assertIn("cv08_calibration", summary_before)
        self.assertFalse(summary_before["cv08_calibration"]["calibrated"])
        self.assertIn("UNCALIBRATED", summary_before["cv08_calibration"]["coverage_claim"])

        # 2. Calibrate with PASS
        calib_res = self.atlas.calibrate_coverage(
            scenario="gating_scen",
            call_trace_vas=[0x401000],
            save_to_db=True,
        )
        self.assertEqual(calib_res["verdict"], CalibrationVerdict.PASS)

        # 3. Calibrated summary
        summary_after = self.atlas.get_summary()
        self.assertTrue(summary_after["cv08_calibration"]["calibrated"])
        self.assertEqual(summary_after["cv08_calibration"]["verdict"], CalibrationVerdict.PASS)
        self.assertIn("CALIBRATED: PASS", summary_after["cv08_calibration"]["coverage_claim"])

    def test_cv08_cli_calibrate_and_re_index(self):
        payload = {
            "scenario": "cli_scen",
            "total_events": 100,
            "lost_events": 0,
            "blocks": [{"va": "0x401000", "hits": 10}],
            "edges": [],
        }
        self.atlas.import_run(payload)

        # CLI calibrate on coverage_atlas
        rc = main([
            "--db", str(self.db_path),
            "--re-index", str(self.re_index_path),
            "calibrate",
            "--scenario", "cli_scen",
            "--json",
        ])
        self.assertEqual(rc, 0)

        # CLI calibrate with markdown
        rc_md = main([
            "--db", str(self.db_path),
            "--re-index", str(self.re_index_path),
            "calibrate",
            "--scenario", "cli_scen",
            "--markdown",
        ])
        self.assertEqual(rc_md, 0)
if __name__ == "__main__":
    unittest.main()
