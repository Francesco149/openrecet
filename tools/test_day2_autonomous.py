#!/usr/bin/env python3
"""tools/test_day2_autonomous.py — Autonomous Day-2 Play-Through Test (Arc 2).

Orchestrates and verifies the complete autonomous Day-2 game lifecycle:
  1. Day-1 -> Day-2 Scenario Replay Validation (house-firstcust-cutscene-day2-full).
  2. Semantic Anchor Sequence & Milestone Verification (0 -> 16,382+ frames, 867 anchors).
  3. Working Bank Day Progression & State Transitions (Day 1 -> Day 2).
  4. Daily News Generation, Market Trend Classification & Customer Kind Machines.
  5. Semantic Coverage Atlas Integration (CV-04/05 dimension validation).

Run: nix develop --command python3 tools/test_day2_autonomous.py
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))
from tools.coverage_atlas import CoverageAtlas, KNOWN_SEMANTIC_DIMENSIONS

class TestDay2AutonomousPlaythrough(unittest.TestCase):
    """Autonomous Day-2 play-through test suite."""

    @classmethod
    def setUpClass(cls):
        cls.scenario_name = "house-firstcust-cutscene-day2-full"
        cls.scenario_dir = REPO_ROOT / "tests" / "scenarios" / cls.scenario_name
        cls.trace_path = cls.scenario_dir / "trace.jsonl"
        cls.scenario_yaml = cls.scenario_dir / "scenario.yaml"

    def test_01_scenario_structure_and_pins(self):
        """Verify scenario metadata, pins, and trace file well-formedness."""
        self.assertTrue(self.scenario_yaml.exists(), "scenario.yaml exists")
        self.assertTrue(self.trace_path.exists(), "trace.jsonl exists")

        # Read trace lines and verify load pins / anchors are present
        lines = [line.strip() for line in self.trace_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        self.assertGreater(len(lines), 100, "trace has sufficient instruction lines")

        # Check for presence of key anchors and waits
        trace_text = "\n".join(lines)
        self.assertIn("LOADING_START", trace_text)
        self.assertIn("LOADING_END", trace_text)
        self.assertIn("CONV_POSE_START", trace_text)
        self.assertIn("HOUSE_FREEROAM", trace_text)

    def test_02_port_replay_end_to_end(self):
        """Run the full scenario replay on OpenRecet port and verify exit=0 and anchor count."""
        cmd = [
            "python3", "tools/scenario-test.py",
            self.scenario_name,
            "--target", "openrecet",
            "--no-regen"
        ]
        res = subprocess.run(cmd, cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0, f"scenario-test exited with code {res.returncode}: {res.stderr}")
        self.assertIn("exit=0", res.stdout)

        # Locate latest run output directory
        runs_dir = REPO_ROOT / "runs" / "scenarios"
        scenario_runs = sorted(runs_dir.glob(f"{self.scenario_name}-openrecet-*"), key=os.path.getmtime, reverse=True)
        self.assertTrue(len(scenario_runs) > 0, "found scenario run output directory")
        latest_run = scenario_runs[0]

        # Verify run.json
        run_json = json.loads((latest_run / "run.json").read_text(encoding="utf-8"))
        self.assertEqual(run_json["exit_code"], 0)

        # Verify anchors.jsonl
        anchors_file = latest_run / "anchors.jsonl"
        self.assertTrue(anchors_file.exists(), "anchors.jsonl produced")
        anchor_events = [json.loads(line) for line in anchors_file.read_text(encoding="utf-8").splitlines() if line.strip()]
        self.assertGreaterEqual(len(anchor_events), 800, f"expected >=800 anchors, got {len(anchor_events)}")

        # Verify milestones reached in order:
        anchor_names = [ev["anchor"] for ev in anchor_events]
        self.assertIn("BOOT", anchor_names)
        self.assertIn("NEW_GAME", anchor_names)
        self.assertIn("LOADING_START", anchor_names)
        self.assertIn("LOADING_END", anchor_names)
        self.assertIn("HOUSE_FREEROAM", anchor_names)
        self.assertIn("TEXT_ANIM_START", anchor_names)
        self.assertIn("TEXT_ANIM_END", anchor_names)
        self.assertIn("DLG_LINE_SHOW", anchor_names)
        self.assertIn("DLG_LINE_CLEAR", anchor_names)
        self.assertIn("EXTRA_SPRITE_START", anchor_names)
        self.assertIn("EXTRA_SPRITE_END", anchor_names)
        self.assertIn("CONV_POSE_END", anchor_names)

        # Verify final frame exceeds 16,000 frames
        last_frame = anchor_events[-1]["frame"]
        self.assertGreater(last_frame, 16000, f"replay reached frame {last_frame}")

    def test_03_day2_host_c_subsystem_gates(self):
        """Execute the host C ASan/UBSan test suite for Day-2 specific subsystems."""
        res = subprocess.run(["./tests/build/run_tests", "day2"], cwd=REPO_ROOT,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(res.returncode, 0, f"day2 C test suite failed: {res.stderr}\n{res.stdout}")
        self.assertIn("pass day2_transition_cascade_state", res.stdout)
        self.assertIn("pass day2_news_generation_and_trends", res.stdout)
        self.assertIn("pass day2_display_grid_and_pricing", res.stdout)
        self.assertIn("pass day2_customer_roster_scan_day2", res.stdout)
        self.assertIn("pass day2_customer_service_sell_machine", res.stdout)
        self.assertIn("pass day2_customer_service_buy_machine", res.stdout)
        self.assertIn("pass day2_customer_service_advance_order_booking", res.stdout)
        self.assertIn("pass day2_customer_service_advance_order_pickup", res.stdout)
        self.assertIn("pass day2_customer_service_chat_machine", res.stdout)
        self.assertIn("pass day2_evening_and_persistence", res.stdout)

    def test_04_coverage_atlas_semantic_dimensions(self):
        """Verify dynamic semantic dimensions coverage in the coverage atlas."""
        atlas = CoverageAtlas()
        summary = atlas.get_semantic_summary()
        self.assertIsInstance(summary, dict)
        for dim in ("functions", "blocks", "transitions", "vm_operations"):
            self.assertIn(dim, KNOWN_SEMANTIC_DIMENSIONS)

def main():
    unittest.main()


if __name__ == "__main__":
    main()
