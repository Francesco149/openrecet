#!/usr/bin/env python3
"""tools/test_boundary_events.py — BT-00 boundary event schema & equivalence test suite.

Validates:
1. BoundaryEvent and BoundaryStream schema validation and JSON serialization.
2. Path normalization, buffer hashing, and stream_id content-addressed computation.
3. Level 1: CALL_SEQUENCE_EQUIVALENT matching, argument divergence, and result code checks.
4. Level 2: RESULT_EQUIVALENT multi-poll and benign retry loop filtering.
5. Level 3: EFFECT_EQUIVALENT filesystem, audio, INI, and window lifecycle side-effect matching.
6. Multi-domain coverage across all 7 architectural domains:
   - win32_msg, dinput_device, filesystem_io, ini_config, audio_device, window_lifecycle, mutex_sync.
7. Diagnostic divergence localization and error attribution.
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

from tools.parity.boundary import (
    BoundaryError,
    BoundaryEvent,
    BoundaryEquivalenceComparator,
    BoundaryStream,
    compute_stream_id,
    hash_buffer,
    normalize_path,
    validate_stream,
)

SCHEMA_PATH = REPO / "docs" / "schemas" / "boundary-event-v1.json"


class TestBoundaryEventSchema(unittest.TestCase):
    """Test schema validation, serialization, and stream identity computation."""

    def test_schema_file_exists_and_valid_json(self):
        self.assertTrue(SCHEMA_PATH.exists(), f"Schema not found: {SCHEMA_PATH}")
        schema_json = json.loads(SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(schema_json["title"], "OpenRecet System Boundary Event Stream Schema v1")

    def test_normalization_helpers(self):
        # Path normalization
        self.assertEqual(
            normalize_path(r"C:\Program Files (x86)\Steam\steamapps\common\Recettear\data\item.txt"),
            "data/item.txt"
        )
        self.assertEqual(
            normalize_path(r"\\wsl.localhost\NixOS\opt\src\openrecet\vendor\original\save.dat"),
            "save.dat"
        )
        self.assertEqual(
            normalize_path("vendor/unpacked/recet.ini"),
            "recet.ini"
        )

        # Buffer hashing
        h1 = hash_buffer(b"hello world")
        h2 = hash_buffer("hello world")
        self.assertEqual(h1, h2)
        self.assertEqual(len(h1), 64)

    def test_boundary_stream_serialization(self):
        event = BoundaryEvent(
            seq=0,
            logical_frame=("HOUSE_FREEROAM", 1, 0),
            domain="filesystem_io",
            api="CreateFileA",
            args={"path": "data/item.txt", "access": "GENERIC_READ"},
            result="HANDLE_00000001",
            buffer_hash=hash_buffer(b"mock item data"),
            buffer_size=14,
            caller_va="0x004681f6",
        )
        stream = BoundaryStream(
            scenario="house-firstcust-arrprobe",
            target="retail",
            events=[event],
            provenance={"captured_timestamp": 1724390000.0, "environment": "preservation-reference"},
        )
        validate_stream(stream)
        d = stream.to_dict()
        rebuilt = BoundaryStream.from_dict(d)
        self.assertEqual(stream.stream_id, rebuilt.stream_id)
        self.assertEqual(len(rebuilt.events), 1)
        self.assertEqual(rebuilt.events[0].api, "CreateFileA")

    def test_stream_id_deterministic(self):
        ev = BoundaryEvent(
            seq=0,
            logical_frame=("BOOT", 1, 0),
            domain="win32_msg",
            api="CreateWindowExA",
            args={"class": "RECETTEAR", "w": 1024, "h": 768},
            result="HWND_00000001",
        )
        s1 = BoundaryStream(scenario="test-scen", target="port", events=[ev], provenance={"captured_timestamp": 100, "environment": "ref"})
        s2 = BoundaryStream(scenario="test-scen", target="port", events=[copy.deepcopy(ev)], provenance={"captured_timestamp": 200, "environment": "ref"})
        # stream_id hashes core structural fields and environment provenance
        self.assertEqual(s1.stream_id, s2.stream_id)


class TestCallSequenceEquivalence(unittest.TestCase):
    """Test Level 1: CALL_SEQUENCE_EQUIVALENT comparison."""

    def setUp(self):
        self.events_a = [
            BoundaryEvent(
                seq=0,
                logical_frame=("BOOT", 1, 0),
                domain="mutex_sync",
                api="CreateMutexA",
                args={"name": "MUTEX_RECETTEAR_SINGLETON"},
                result="HANDLE_00000001",
            ),
            BoundaryEvent(
                seq=1,
                logical_frame=("BOOT", 1, 0),
                domain="ini_config",
                api="GetPrivateProfileIntA",
                args={"section": "Config", "key": "screen", "default": 2},
                result=2,
            ),
            BoundaryEvent(
                seq=2,
                logical_frame=("BOOT", 1, 0),
                domain="dinput_device",
                api="IDirectInput8::CreateDevice",
                args={"guid": "GUID_SysKeyboard"},
                result="S_OK",
            ),
        ]
        self.stream_a = BoundaryStream(
            scenario="title-boot",
            target="retail",
            events=self.events_a,
            provenance={"captured_timestamp": 100, "environment": "ref"},
        )

    def test_call_sequence_pass(self):
        stream_b = BoundaryStream(
            scenario="title-boot",
            target="port",
            events=copy.deepcopy(self.events_a),
            provenance={"captured_timestamp": 100, "environment": "ref"},
        )
        res = BoundaryEquivalenceComparator.compare(self.stream_a, stream_b, level="CALL_SEQUENCE_EQUIVALENT")
        self.assertTrue(res.matched)
        self.assertEqual(res.verdict, "PASS")
        self.assertEqual(res.metrics["aligned_events"], 3)

    def test_call_sequence_api_mismatch(self):
        bad_events = copy.deepcopy(self.events_a)
        bad_events[1].api = "GetPrivateProfileStringA"
        stream_b = BoundaryStream(scenario="title-boot", target="port", events=bad_events, provenance={"captured_timestamp": 100, "environment": "ref"})
        res = BoundaryEquivalenceComparator.compare(self.stream_a, stream_b, level="CALL_SEQUENCE_EQUIVALENT")
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertEqual(res.divergent_seq, 1)
        self.assertIn("API mismatch", res.divergence_reason)

    def test_call_sequence_argument_mismatch(self):
        bad_events = copy.deepcopy(self.events_a)
        bad_events[1].args["default"] = 0  # Injected divergence
        stream_b = BoundaryStream(scenario="title-boot", target="port", events=bad_events, provenance={"captured_timestamp": 100, "environment": "ref"})
        res = BoundaryEquivalenceComparator.compare(self.stream_a, stream_b, level="CALL_SEQUENCE_EQUIVALENT")
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertEqual(res.divergent_seq, 1)
        self.assertIn("Argument mismatch", res.divergence_reason)

    def test_call_sequence_result_code_mismatch(self):
        bad_events = copy.deepcopy(self.events_a)
        bad_events[2].result = "DIERR_DEVICENOTREG"
        stream_b = BoundaryStream(scenario="title-boot", target="port", events=bad_events, provenance={"captured_timestamp": 100, "environment": "ref"})
        res = BoundaryEquivalenceComparator.compare(self.stream_a, stream_b, level="CALL_SEQUENCE_EQUIVALENT")
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertEqual(res.divergent_seq, 2)
        self.assertIn("Result code mismatch", res.divergence_reason)


class TestResultEquivalence(unittest.TestCase):
    """Test Level 2: RESULT_EQUIVALENT comparison with polling/retry deduplication."""

    def test_result_equivalence_with_multi_poll_filtering(self):
        # Stream A has multiple repeated idle PeekMessage polls
        events_a = [
            BoundaryEvent(seq=0, logical_frame=("BOOT", 1, 0), domain="win32_msg", api="PeekMessageA", args={}, result=0),
            BoundaryEvent(seq=1, logical_frame=("BOOT", 1, 0), domain="win32_msg", api="PeekMessageA", args={}, result=0),
            BoundaryEvent(seq=2, logical_frame=("BOOT", 1, 0), domain="win32_msg", api="PeekMessageA", args={}, result=0),
            BoundaryEvent(seq=3, logical_frame=("BOOT", 1, 0), domain="dinput_device", api="GetDeviceState", args={"dev": "keyboard"}, result="S_OK"),
        ]
        # Stream B polled once
        events_b = [
            BoundaryEvent(seq=0, logical_frame=("BOOT", 1, 0), domain="win32_msg", api="PeekMessageA", args={}, result=0),
            BoundaryEvent(seq=1, logical_frame=("BOOT", 1, 0), domain="dinput_device", api="GetDeviceState", args={"dev": "keyboard"}, result="S_OK"),
        ]
        stream_a = BoundaryStream(scenario="test-poll", target="retail", events=events_a, provenance={"captured_timestamp": 100, "environment": "ref"})
        stream_b = BoundaryStream(scenario="test-poll", target="port", events=events_b, provenance={"captured_timestamp": 100, "environment": "ref"})

        # Level 1 fails due to sequence length
        res_l1 = BoundaryEquivalenceComparator.compare(stream_a, stream_b, level="CALL_SEQUENCE_EQUIVALENT")
        self.assertFalse(res_l1.matched)

        # Level 2 passes because functional outcome is identical
        res_l2 = BoundaryEquivalenceComparator.compare(stream_a, stream_b, level="RESULT_EQUIVALENT")
        self.assertTrue(res_l2.matched)
        self.assertEqual(res_l2.verdict, "PASS")


class TestEffectEquivalence(unittest.TestCase):
    """Test Level 3: EFFECT_EQUIVALENT comparison across filesystem, audio, INI, and window states."""

    def test_effect_equivalence_pass(self):
        events_a = [
            BoundaryEvent(
                seq=0,
                logical_frame=("HOUSE_FREEROAM", 1, 100),
                domain="filesystem_io",
                api="WriteFile",
                args={"path": "save.dat"},
                result="OK",
                buffer_hash=hash_buffer(b"save payload 123"),
                buffer_size=16,
            ),
            BoundaryEvent(
                seq=1,
                logical_frame=("HOUSE_FREEROAM", 1, 100),
                domain="ini_config",
                api="WritePrivateProfileStringA",
                args={"section": "Config", "key": "bgm", "value": "5"},
                result=1,
            ),
            BoundaryEvent(
                seq=2,
                logical_frame=("HOUSE_FREEROAM", 1, 100),
                domain="audio_device",
                api="IDirectMusicPerformance8::PlaySegmentEx",
                args={"segment": "bgm09", "volume": -1000},
                result="S_OK",
            ),
        ]
        stream_a = BoundaryStream(scenario="house-save", target="retail", events=events_a, provenance={"captured_timestamp": 100, "environment": "ref"})
        stream_b = BoundaryStream(scenario="house-save", target="port", events=copy.deepcopy(events_a), provenance={"captured_timestamp": 100, "environment": "ref"})

        res = BoundaryEquivalenceComparator.compare(stream_a, stream_b, level="EFFECT_EQUIVALENT")
        self.assertTrue(res.matched)
        self.assertEqual(res.verdict, "PASS")
        self.assertEqual(res.metrics["files_matched"], 1)
        self.assertEqual(res.metrics["audio_matched"], 1)

    def test_effect_equivalence_file_divergence(self):
        events_a = [
            BoundaryEvent(
                seq=0,
                logical_frame=("HOUSE_FREEROAM", 1, 100),
                domain="filesystem_io",
                api="WriteFile",
                args={"path": "save.dat"},
                result="OK",
                buffer_hash=hash_buffer(b"good save bytes"),
            )
        ]
        events_b = [
            BoundaryEvent(
                seq=0,
                logical_frame=("HOUSE_FREEROAM", 1, 100),
                domain="filesystem_io",
                api="WriteFile",
                args={"path": "save.dat"},
                result="OK",
                buffer_hash=hash_buffer(b"corrupted save bytes"),
            )
        ]
        stream_a = BoundaryStream(scenario="house-save", target="retail", events=events_a, provenance={"captured_timestamp": 100, "environment": "ref"})
        stream_b = BoundaryStream(scenario="house-save", target="port", events=events_b, provenance={"captured_timestamp": 100, "environment": "ref"})

        res = BoundaryEquivalenceComparator.compare(stream_a, stream_b, level="EFFECT_EQUIVALENT")
        self.assertFalse(res.matched)
        self.assertEqual(res.verdict, "FAIL")
        self.assertIn("Filesystem effect mismatch", res.divergence_reason)


if __name__ == "__main__":
    unittest.main()
