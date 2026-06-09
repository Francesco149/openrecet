#!/usr/bin/env python3
"""Tests for trace_studio.edits.lint — the working-trace preflight + canonical
auto-pin (docs/audits/2026-06-09-methodology-audit.md T2: pin policy → mechanism).

Run: nix develop --command python3 tools/test_trace_lint.py
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from trace_studio.edits.lint import (  # noqa: E402
    CANON_SEED, auto_pin_text, caprange_segment, lint)

PINNED = """\
# canonical template (house-loaded-display-pinned)
{"savefile": "@fresh"}
{"frame": 0, "buttons": "0x0000"}
{"wait": "NEW_GAME"}
{"frame": 0, "buttons": "0x0000"}
{"wait": "LOADING_END"}
{"phasepin": 80}
{"rngseed": [80, 19937]}
{"caprange": [120, 48]}
{"calltrace": [120, 48]}
{"frame": 0, "buttons": "0x0000"}
"""

UNPINNED = """\
{"savefile": "@fresh"}
{"frame": 0, "buttons": "0x0000"}
{"wait": "LOADING_END"}
{"caprange": [120, 48]}
{"frame": 0, "buttons": "0x0000"}
"""

MULTISEG = """\
{"savefile": "@fresh"}
{"wait": "LOADING_END"}
{"caprange": [10, 5]}
{"calltrace": [10, 5]}
{"frame": 0, "buttons": "0x0000"}
{"wait": "LOADING_START"}
{"frame": 0, "buttons": "0x0000"}
"""


def codes(findings):
    return {f["code"] for f in findings}


def by_code(findings, code):
    return [f for f in findings if f["code"] == code]


def test_clean_template():
    f = lint(PINNED)
    assert not [x for x in f if x["level"] == "error"], f
    # template pins 40f before the window → no settle-margin info either
    assert "no-settle-margin" not in codes(f), f
    assert "no-phasepin" not in codes(f) and "no-rngseed" not in codes(f), f


def test_unpinned_warns():
    f = lint(UNPINNED)
    assert {"no-phasepin", "no-rngseed", "no-calltrace"} <= codes(f), f
    assert not [x for x in f if x["level"] == "error"], f


def test_no_caprange_errors():
    f = lint('{"frame": 0, "buttons": "0x0000"}\n')
    assert by_code(f, "no-caprange") and f[0]["level"] == "error", f


def test_pin_inside_window_errors():
    t = PINNED.replace('{"phasepin": 80}', '{"phasepin": 130}')
    f = lint(t)
    assert by_code(f, "pin-inside-window"), f
    assert by_code(f, "pin-inside-window")[0]["level"] == "error", f


def test_stacked_rngseed_errors():
    t = PINNED.replace('{"rngseed": [80, 19937]}',
                       '{"rngseed": [80, 4242]}\n{"rngseed": [80, 19937]}')
    f = lint(t)
    assert by_code(f, "stacked-rngseed"), f
    assert by_code(f, "stacked-rngseed")[0]["level"] == "error", f


def test_noncanonical_seed_info():
    t = PINNED.replace('{"rngseed": [80, 19937]}', '{"rngseed": [80, 4242]}')
    f = lint(t)
    assert by_code(f, "noncanonical-seed"), f
    assert by_code(f, "noncanonical-seed")[0]["level"] == "info", f


def test_calltrace_span_warns():
    t = PINNED.replace('{"calltrace": [120, 48]}', '{"calltrace": [130, 10]}')
    f = lint(t)
    assert by_code(f, "calltrace-span"), f


def test_savefile_missing_errors():
    with tempfile.TemporaryDirectory() as td:
        t = PINNED.replace('"@fresh"', '"_saves/nope.sav.gz"')
        f = lint(t, Path(td))
        assert by_code(f, "savefile-missing"), f
        # and resolvable ref passes
        (Path(td) / "_saves").mkdir()
        (Path(td) / "_saves" / "nope.sav.gz").write_bytes(b"x")
        f = lint(t, Path(td))
        assert not by_code(f, "savefile-missing"), f


def test_segment_detection_multiseg():
    seg = caprange_segment(MULTISEG.splitlines())
    assert seg is not None and seg["cr"] == (10, 5)
    # segment closes at the NEXT wait — the LOADING_START line
    assert MULTISEG.splitlines()[seg["end"]].find("LOADING_START") >= 0
    assert seg["calltraces"] and seg["calltraces"][0][1] == (10, 5)


def test_auto_pin_inserts_canonical_block():
    out, actions = auto_pin_text(UNPINNED)
    assert len(actions) == 2, actions
    ops = [json.loads(l) for l in out.splitlines()
           if l.strip() and not l.startswith("#")]
    pi = next(i for i, o in enumerate(ops) if "phasepin" in o)
    ri = next(i for i, o in enumerate(ops) if "rngseed" in o)
    ci = next(i for i, o in enumerate(ops) if "caprange" in o)
    assert pi < ri < ci, (pi, ri, ci)
    assert ops[pi]["phasepin"] == 120                 # default = caprange.start
    assert ops[ri]["rngseed"] == [120, CANON_SEED]
    # idempotent: a second pass adds nothing
    out2, actions2 = auto_pin_text(out)
    assert out2 == out and not actions2, actions2


def test_auto_pin_respects_existing_and_canonicalizes():
    # existing phasepin at 80 + recorded seed at 80 → only the seed is rewritten
    t = PINNED.replace('{"rngseed": [80, 19937]}', '{"rngseed": [80, 31337]}')
    out, actions = auto_pin_text(t)
    assert len(actions) == 1 and "canonicalized" in actions[0], actions
    assert '{"rngseed": [80, 19937]}' in out
    assert out.count("phasepin") == 1
    # pinned template untouched
    out2, actions2 = auto_pin_text(PINNED)
    assert out2 == PINNED and not actions2, actions2


def test_auto_pin_seed_lands_at_existing_pin_frame():
    # phasepin at 80 exists, NO rngseed anywhere → seed inserted at 80, not 120
    t = PINNED.replace('{"rngseed": [80, 19937]}\n', "")
    out, actions = auto_pin_text(t)
    assert len(actions) == 1, actions
    assert '{"rngseed": [80, 19937]}' in out


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
    print(f"OK: trace lint + auto-pin ({len(tests)} tests)")


if __name__ == "__main__":
    main()
