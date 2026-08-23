#!/usr/bin/env python3
"""Unit tests for tools/re_index.py (CV-01 / CV-02).

Tests:
  - Database creation and table schemas
  - Function lookup by VA, name, hex, int
  - Callers and callees resolution
  - Global variable and string cross-references
  - Call tree generation and depth limits
  - Unported callees filter
  - Search functionality
  - Stats aggregation
  - JSON serialization
"""

import json
import sqlite3
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO) not in sys.path:
    sys.path.insert(0, str(REPO))

from tools.re_index import ReIndex, format_fun, format_va, parse_va


class TestReIndex(unittest.TestCase):
    def setUp(self):
        self.tmp_dir = tempfile.TemporaryDirectory()
        self.db_path = Path(self.tmp_dir.name) / "test_re_index.sqlite"
        self.idx = ReIndex(db_path=self.db_path)

    def tearDown(self):
        self.idx.close()
        self.tmp_dir.cleanup()

    def test_parse_and_format_va(self):
        self.assertEqual(parse_va("0x4905a8"), 0x4905A8)
        self.assertEqual(parse_va("4905a8"), 0x4905A8)
        self.assertEqual(parse_va("FUN_004905a8"), 0x4905A8)
        self.assertEqual(parse_va(0x4905A8), 0x4905A8)
        self.assertEqual(format_va(0x4905A8), "0x4905a8")
        self.assertEqual(format_fun(0x4905A8), "FUN_004905a8")

    def test_build_and_stats(self):
        res = self.idx.build(force=True)
        self.assertEqual(res["status"], "built")
        self.assertGreater(res["functions"], 2500)
        self.assertGreater(res["calls"], 5000)
        self.assertGreater(res["global_xrefs"], 8000)
        self.assertGreater(res["string_xrefs"], 500)

        st = self.idx.stats()
        self.assertGreaterEqual(st["total_functions"], 2620)
        self.assertGreaterEqual(st["non_thunk_functions"], 2548)
        self.assertIn("inventory_breakdown", st)

    def test_get_function(self):
        self.idx.build(force=True)
        fn1 = self.idx.get_function(0x4905A8)
        self.assertIsNotNone(fn1)
        self.assertEqual(fn1["va"], 0x4905A8)
        self.assertEqual(fn1["name"], "FUN_004905a8")
        self.assertEqual(fn1["size"], 179)

        fn2 = self.idx.get_function("0x4905a8")
        self.assertEqual(fn2["va"], 0x4905A8)

        fn3 = self.idx.get_function("FUN_004905a8")
        self.assertEqual(fn3["va"], 0x4905A8)

    def test_callers_and_callees(self):
        self.idx.build(force=True)
        callees = self.idx.get_callees(0x4905A8)
        callee_vas = {c["va"] for c in callees}
        self.assertIn(0x5036DE, callee_vas)
        self.assertIn(0x5038B0, callee_vas)
        self.assertIn(0x503B21, callee_vas)

        callers = self.idx.get_callers(0x5038B0)
        caller_vas = {c["va"] for c in callers}
        self.assertIn(0x4905A8, caller_vas)

    def test_global_and_string_xrefs(self):
        self.idx.build(force=True)
        # Check global DAT_056e6280
        xrefs_g = self.idx.get_global_xrefs(0x56E6280)
        func_vas = {x["va"] for x in xrefs_g}
        self.assertIn(0x4905A8, func_vas)

        # Check string s_save_dat_005cfa98
        xrefs_s = self.idx.get_string_xrefs("s_save_dat_005cfa98")
        func_vas_s = {x["va"] for x in xrefs_s}
        self.assertIn(0x4905A8, func_vas_s)

        # Function's own globals and strings
        fn_globals = self.idx.get_function_globals(0x4905A8)
        self.assertIn(0x56E6280, fn_globals)
        fn_strings = self.idx.get_function_strings(0x4905A8)
        self.assertIn("s_save_dat_005cfa98", fn_strings)

    def test_call_tree(self):
        self.idx.build(force=True)
        tree = self.idx.get_call_tree(0x4905A8, max_depth=2)
        self.assertEqual(tree["va"], "0x4905a8")
        self.assertEqual(tree["name"], "FUN_004905a8")
        self.assertGreater(len(tree["children"]), 0)
        for child in tree["children"]:
            self.assertTrue(child["va"].startswith("0x"))
            self.assertIn("name", child)

    def test_search(self):
        self.idx.build(force=True)
        res = self.idx.search("4905a8")
        self.assertTrue(any(f["va"] == 0x4905A8 for f in res["functions"]))

        res_str = self.idx.search("save_dat")
        self.assertTrue(any("save_dat" in s["string_name"] for s in res_str["strings"]))

    def test_disasm(self):
        self.idx.build(force=True)
        text = self.idx.disasm(0x461011)
        self.assertIn("push", text)
        self.assertIn("ret", text)
    def test_get_text(self):
        self.idx.build(force=True)
        text = self.idx.get_text(0x4905a8)
        self.assertIn("void FUN_004905a8(int param_1)", text)
        self.assertIn("DAT_056e6280", text)

        # Test with line numbers
        num_text = self.idx.get_text(0x4905a8, line_numbers=True)
        self.assertIn(" 1: /* =====", num_text)

        # Test invalid function
        with self.assertRaises(ValueError):
            self.idx.get_text(0x12345678)

    def test_search_code(self):
        self.idx.build(force=True)
        res = self.idx.search("DAT_056e6280", search_code=True)
        self.assertIn("code", res)
        self.assertTrue(any(c["va"] == "0x4905a8" for c in res["code"]))
    def test_coverage(self):
        self.idx.build(force=True)
        cov = self.idx.coverage()
        self.assertIn("matrix", cov)
        self.assertGreater(cov["total_functions"], 0)
        self.assertIn("implemented_functions", cov)
        self.assertIn("proven_functions", cov)

        # Test with unimplemented and unexecuted filters
        cov_filters = self.idx.coverage(unimplemented=True, unexecuted=True, limit=10)
        self.assertIsInstance(cov_filters["unimplemented"], list)
        self.assertIsInstance(cov_filters["unexecuted"], list)

        # Test stats runtime breakdown
        st = self.idx.stats()
        self.assertIn("runtime_breakdown", st)

    def test_blocks_and_flows(self):
        self.idx.build(force=True)
        blocks = self.idx.get_blocks(0x4905A8)
        self.assertGreaterEqual(len(blocks), 1)
        first_block = blocks[0]
        self.assertEqual(first_block["block_va"], 0x4905A8)
        self.assertEqual(first_block["func_va"], 0x4905A8)
        self.assertTrue(first_block["is_entry"])
        self.assertGreater(first_block["size"], 0)
        self.assertIn("flow_type", first_block)

        # Test flows
        flows = self.idx.get_flows(0x4905A8)
        self.assertGreaterEqual(len(flows), 1)
        for fl in flows:
            self.assertEqual(fl["func_va"], 0x4905A8)
            self.assertIn(fl["flow_type"], ("FALL_THROUGH", "BRANCH_TAKEN", "CONDITIONAL_JUMP", "UNCONDITIONAL_JUMP", "RETURN", "FLOW"))

    def test_data_xrefs(self):
        self.idx.build(force=True)
        # Query by function VA
        xrefs_fn = self.idx.get_data_xrefs(0x4905A8)
        self.assertGreaterEqual(len(xrefs_fn), 1)
        data_vas = {x["data_va"] for x in xrefs_fn}
        self.assertTrue(0x438B1E0 in data_vas or 0x56E6280 in data_vas)

        # Query by global VA
        xrefs_dat = self.idx.get_data_xrefs(0x438B1E0)
        self.assertGreaterEqual(len(xrefs_dat), 1)
        for x in xrefs_dat:
            self.assertEqual(x["data_va"], 0x438B1E0)
            self.assertIn("access_type", x)

    def test_byte_hash(self):
        self.idx.build(force=True)
        h = self.idx.get_byte_hash(0x4905A8)
        self.assertIsNotNone(h)
        self.assertEqual(len(h), 64)
        # Verify hex format
        int(h, 16)

    def test_json_export_and_import_roundtrip(self):
        self.idx.build(force=True)
        export_dir = Path(self.tmp_dir.name) / "json_export"
        res_exp = self.idx.export_json(export_dir)
        self.assertEqual(res_exp["status"], "exported")
        self.assertTrue((export_dir / "manifest.json").exists())
        self.assertTrue((export_dir / "functions.json").exists())
        self.assertTrue((export_dir / "blocks.json").exists())
        self.assertTrue((export_dir / "flows.json").exists())
        self.assertTrue((export_dir / "calls.json").exists())
        self.assertTrue((export_dir / "data_xrefs.json").exists())
        self.assertTrue((export_dir / "string_xrefs.json").exists())

        # Import into fresh database
        imported_db = Path(self.tmp_dir.name) / "imported.sqlite"
        new_idx = ReIndex(db_path=imported_db)
        res_imp = new_idx.import_json(export_dir)
        self.assertEqual(res_imp["status"], "built")
        self.assertEqual(res_imp["functions"], res_exp["manifest"]["functions_count"])
        self.assertEqual(res_imp["blocks"], res_exp["manifest"]["blocks_count"])

        # Verify queries work identically on imported database
        fn = new_idx.get_function(0x4905A8)
        self.assertIsNotNone(fn)
        self.assertEqual(fn["name"], "FUN_004905a8")
        self.assertEqual(fn["byte_hash"], self.idx.get_byte_hash(0x4905A8))

        blocks = new_idx.get_blocks(0x4905A8)
        self.assertEqual(len(blocks), len(self.idx.get_blocks(0x4905A8)))
        new_idx.close()

    def test_cli_subcommands(self):
        from tools.re_index import main
        self.idx.build(force=True)
        db_arg = ["--db", str(self.db_path)]

        # Test info --json
        ret = main(["info", "0x4905a8", *db_arg, "--json"])
        self.assertEqual(ret, 0)

        # Test blocks --json
        ret = main(["blocks", "0x4905a8", *db_arg, "--json"])
        self.assertEqual(ret, 0)

        # Test flows --json
        ret = main(["flows", "0x4905a8", *db_arg, "--json"])
        self.assertEqual(ret, 0)

        # Test hash --json
        ret = main(["hash", "0x4905a8", *db_arg, "--json"])
        self.assertEqual(ret, 0)

        # Test data-xrefs --json
        ret = main(["data-xrefs", "0x4905a8", *db_arg, "--json"])
        self.assertEqual(ret, 0)

        # Test switches --json
        ret = main(["switches", *db_arg, "--json"])
        self.assertEqual(ret, 0)

        # Test stats --json
        ret = main(["stats", *db_arg, "--json"])
        self.assertEqual(ret, 0)



if __name__ == "__main__":
    unittest.main()
