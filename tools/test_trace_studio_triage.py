#!/usr/bin/env python3
"""Tests for trace_studio.analysis.triage — the one-command divergence report
(docs/audits/2026-06-09-methodology-audit.md T1) over a synthetic session.

Run: nix develop --command python3 tools/test_trace_studio_triage.py
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from trace_studio.analysis.triage import run_triage  # noqa: E402


def mk_session(td: Path, per_frame, state_rows=None, extra=None) -> Path:
    sess = td / "synth"
    sess.mkdir(parents=True, exist_ok=True)
    m = {
        "schema": "trace-studio-v2", "session": "synth",
        "caprange": [120, len(per_frame)], "stride": 1,
        "n_frames": len(per_frame), "n_frames_retail": len(per_frame),
        "coords": {"naming": "label", "window_start": 120, "stride": 1},
        "diff": {"n": len(per_frame), "per_frame": per_frame},
        "anchors": {"retail": [{"anchor": "LOADING_END", "frame": 0}]},
        "verdict": {"available": True, "exit_code": 0,
                    "text": "PHASE-CLEAN: all fields ALIGNED"},
    }
    m.update(extra or {})
    (sess / "session.json").write_text(json.dumps(m))
    if state_rows is not None:
        (sess / "state.jsonl").write_text(
            "".join(json.dumps(r) + "\n" for r in state_rows))
    return sess


def pf(label, differ, meanabs=0.0):
    return {"frame": label, "differ": differ, "meanabs": meanabs}


def test_clean_session_exit0():
    with tempfile.TemporaryDirectory() as td:
        sess = mk_session(Path(td), [pf(120 + i, 0) for i in range(10)])
        t, rc = run_triage(sess, field_timeline=False)
        assert rc == 0, (rc, t)
        assert t["diff"]["clean"] and t["diff"]["over_threshold"] == 0


def test_first_and_worst_divergence():
    with tempfile.TemporaryDirectory() as td:
        per = [pf(120 + i, 0) for i in range(10)]
        per[3] = pf(123, 500, 0.4)        # first
        per[7] = pf(127, 9000, 2.0)       # worst
        rows = [{"frame": 2, "port": {"db054": 41}, "retail": {"db054": 41}},
                {"frame": 3, "port": {"db054": 42}, "retail": {"db054": 41}}]
        sess = mk_session(Path(td), per, state_rows=rows)
        t, rc = run_triage(sess, field_timeline=False)
        assert rc == 1
        assert t["diff"]["first"]["ordinal"] == 3
        assert t["diff"]["first"]["frame"] == 123          # the LABEL
        assert t["diff"]["worst"]["ordinal"] == 7
        assert t["state_at_first"]["frame"] == 3
        assert t["anchor_before_first"]["anchor"] == "LOADING_END"


def test_skip_settle_margin():
    with tempfile.TemporaryDirectory() as td:
        per = [pf(120, 5000, 3.0)] + [pf(121 + i, 0) for i in range(9)]
        sess = mk_session(Path(td), per)
        t, rc = run_triage(sess, skip=1, field_timeline=False)
        assert rc == 0, (rc, t["diff"])


def test_threshold_floor_cuts_noise():
    with tempfile.TemporaryDirectory() as td:
        per = [pf(120 + i, 20, 0.001) for i in range(10)]   # sub-floor noise
        sess = mk_session(Path(td), per)
        t, rc = run_triage(sess, field_timeline=False)
        assert rc == 0, (rc, t["diff"])


def test_no_diff_data_unusable():
    with tempfile.TemporaryDirectory() as td:
        sess = mk_session(Path(td), [])
        (sess / "session.json").write_text(json.dumps(
            {"session": "synth", "caprange": [0, 0], "n_frames": 0}))
        t, rc = run_triage(sess, field_timeline=False)
        assert rc == 2
        assert any(p["kind"] == "no_diff_data" for p in t["problems"])


def test_gt8_metric_preferred_over_lsb_noise():
    # whole-frame 1-LSB noise (huge differ, mean .4) but bit-clean by gt8 → CLEAN
    with tempfile.TemporaryDirectory() as td:
        per = [dict(pf(120 + i, 500000, 0.4), gt8=0) for i in range(10)]
        per[5]["gt8"] = 4000                  # one REAL gap
        sess = mk_session(Path(td), per)
        t, rc = run_triage(sess, field_timeline=False)
        assert rc == 1
        assert t["diff"]["metric"] == "gt8"
        assert t["diff"]["over_threshold"] == 1
        assert t["diff"]["first"]["ordinal"] == 5


def test_capture_error_surfaces():
    with tempfile.TemporaryDirectory() as td:
        sess = mk_session(Path(td), [pf(120, 0)],
                          extra={"capture_error": "retail captured 0 frames"})
        t, rc = run_triage(sess, field_timeline=False)
        assert rc == 1
        assert any(p["kind"] == "capture_error" for p in t["problems"])


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for fn in tests:
        fn()
    print(f"OK: trace_studio triage ({len(tests)} tests)")


if __name__ == "__main__":
    main()
