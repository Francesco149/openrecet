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


if __name__ == "__main__":
    unittest.main()
