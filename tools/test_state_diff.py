#!/usr/bin/env python3
"""tools/test_state_diff.py — gate for ST-04, the first-divergence state report.

Proves the diagnostic's truth-defining core with NO drive dependency, on synthetic
paired per-frame state + a synthetic view.json:

  * MERKLE all_divergent_leaves — every divergent leaf in schema order; the first is
    exactly first_divergent_leaf; empty ⇔ identical roots.
  * LOCALIZATION (ST-04 acceptance: synthetic mutations at every tree level localize)
    — a leaf value change → the exact (subsystem/field) primary; a present/absent
    asymmetry → that leaf with the right presence flags + absent-side bits None; two
    fields diverging at one frame → both co-divergent leaves in schema order.
  * RAW BITS + TYPES — each leaf carries the schema type and its canonical encoder
    bytes (port_bits_hex/retail_bits_hex) == encode_value(type, value).hex().
  * LAST MATCHING FRAME + TRANSITION — the newest equal-root frame; the value
    transition names port-missed / port-spurious / port-wrong; a head-of-window
    divergence has no last match (transition None).
  * VERDICT (fail closed, §4.1) — all identical → PASS; a divergence → FAIL (even
    under a later coverage gap); has_state false / an uncovered required frame →
    NOT_CAPTURED; empty required → INCONCLUSIVE. Matches adapt_state on shared cases.
  * STABLE JSON — the report round-trips deterministically (same input → same bytes).
  * VIEW BRIDGE — report_from_view_json over a synthetic view; --all-frames scans all.
  * CLI — state_diff.main exit codes follow §4.1 (0 PASS / 1 FAIL / 2 absent).
  * WIRING — build_state_diff_report + state_diff_from_view_json exported from parity.

Run: nix develop --command python3 tools/test_state_diff.py
"""
from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import state_diff as cli  # noqa: E402
from parity import (  # noqa: E402
    adapt_state,
    build_state_diff_report,
    state_diff_from_view_json,
    state_metrics_from_view_json,
)
from parity.observations import LogicalFrame  # noqa: E402
from parity.state_codec import StateSchema, build_tree, encode_value  # noqa: E402
from parity.state_diff import build_report, render_text  # noqa: E402
from parity.state_merkle import (  # noqa: E402
    all_divergent_leaves,
    first_divergent_leaf,
)

_checks = 0
_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


SCHEMA = StateSchema.load()


def f32(x: float) -> float:
    return struct.unpack("f", struct.pack("f", x))[0]


def sfields(**over) -> dict:
    d = {
        "rng": 305419896, "rngcalls": 100,
        "db054": 42, "gsim": 3,
        "px": f32(-0.3), "py": f32(1.5), "poct": 6, "panim": 2,
        "cx": f32(0.6), "coct": 2,
        "cursor_pos": 1, "submenu_state": 7,
    }
    d.update(over)
    return d


def _labels(*offs):
    return [f"SAVE_PICKER_READY#1+{o}" for o in offs]


def _req(*offs):
    return [LogicalFrame.from_label(s) for s in _labels(*offs)]


def _paired(pairs):
    """{offset: (port_fields, retail_fields)} → the paired_by_label map."""
    return {f"SAVE_PICKER_READY#1+{o}": {"port": p, "retail": r} for o, (p, r) in pairs.items()}


# ── MERKLE all_divergent_leaves ────────────────────────────────────────────────

def test_all_divergent_leaves():
    base = build_tree(sfields(), SCHEMA)
    # identical → no divergent leaves
    check(all_divergent_leaves(base, build_tree(sfields(), SCHEMA), SCHEMA) == [],
          "all_divergent: identical trees → []")
    # two leaves across two subsystems (phase before player in schema order)
    other = build_tree(sfields(db054=43, px=f32(-0.31)), SCHEMA)
    ds = all_divergent_leaves(base, other, SCHEMA)
    check([d.path for d in ds] == ["phase/db054", "player/px"],
          "all_divergent: both leaves, in schema order")
    check(ds[0] == first_divergent_leaf(base, other, SCHEMA),
          "all_divergent: first element == first_divergent_leaf")


# ── LOCALIZATION + RAW BITS + TRANSITION ────────────────────────────────────────

def test_leaf_localization_and_transition():
    # L0 identical (last match), L1: retail bumps db054 → port MISSED it.
    paired = _paired({0: (sfields(), sfields()),
                      1: (sfields(db054=42), sfields(db054=43))})
    rep = build_report(paired, SCHEMA, _req(0, 1))
    check(rep["verdict"] == "FAIL", "localize: leaf change → FAIL")
    fd = rep["first_divergence"]
    check(fd["logical_frame"]["offset"] == 1, "localize: at the right logical frame")
    check(fd["path"] == "phase/db054" and fd["subsystem"] == "phase" and fd["field"] == "db054",
          "localize: primary leaf path/subsystem/field")
    check(fd["type"] == "i32", "localize: leaf carries schema type")
    check(fd["retail_value"] == 43 and fd["port_value"] == 42, "localize: typed values")
    # RAW BITS == canonical encoder bytes
    check(fd["retail_bits_hex"] == encode_value("i32", 43).hex()
          and fd["port_bits_hex"] == encode_value("i32", 42).hex(), "localize: raw bits")
    check(rep["last_matching_frame"]["offset"] == 0, "transition: last matching frame")
    tr = fd["transition"]
    check(tr and tr["prev_value"] == 42 and tr["prev_present"] is True, "transition: prev value")
    check(tr["retail"]["changed"] and not tr["port"]["changed"], "transition: retail changed, port didn't")
    check("did NOT apply" in tr["interpretation"], "transition: port-missed interpretation")


def test_transition_interpretations():
    # port SPURIOUS: port changes db054, retail holds
    paired = _paired({0: (sfields(), sfields()), 1: (sfields(db054=43), sfields(db054=42))})
    tr = build_report(paired, SCHEMA, _req(0, 1))["first_divergence"]["transition"]
    check(tr["port"]["changed"] and not tr["retail"]["changed"] and "that retail did not" in tr["interpretation"],
          "transition: port-spurious interpretation")
    # port WRONG: both change db054, differently
    paired = _paired({0: (sfields(), sfields()), 1: (sfields(db054=44), sfields(db054=43))})
    tr = build_report(paired, SCHEMA, _req(0, 1))["first_divergence"]["transition"]
    check(tr["port"]["changed"] and tr["retail"]["changed"] and "both changed" in tr["interpretation"],
          "transition: port-wrong (both changed) interpretation")


def test_presence_asymmetry():
    # port keeps poct, retail drops it → present/absent asymmetry localized
    ret = {k: v for k, v in sfields().items() if k != "poct"}
    paired = _paired({0: (sfields(), sfields()), 1: (sfields(), ret)})
    fd = build_report(paired, SCHEMA, _req(0, 1))["first_divergence"]
    check(fd["path"] == "player/poct", "presence: localized to player/poct")
    check(fd["port_present"] and not fd["retail_present"], "presence: flags (port has, retail lacks)")
    check(fd["retail_bits_hex"] is None and fd["port_bits_hex"] == encode_value("i32", 6).hex(),
          "presence: absent side bits None, present side bits set")


def test_co_divergent_leaves():
    paired = _paired({0: (sfields(), sfields()),
                      1: (sfields(db054=42, px=f32(-0.3)), sfields(db054=43, px=f32(-0.31)))})
    fd = build_report(paired, SCHEMA, _req(0, 1))["first_divergence"]
    check(fd["n_divergent_leaves"] == 2, "co-divergent: count == 2")
    check([d["path"] for d in fd["divergent_leaves"]] == ["phase/db054", "player/px"],
          "co-divergent: both leaves in schema order")
    check(fd["divergent_leaves"][0]["path"] == fd["path"], "co-divergent: primary == first leaf")
    check(fd["divergent_leaves"][1]["type"] == "f32", "co-divergent: second leaf typed (f32)")


def test_head_of_window_divergence():
    # first covered frame already diverges → no last matching frame → transition None
    paired = _paired({0: (sfields(db054=1), sfields(db054=2))})
    rep = build_report(paired, SCHEMA, _req(0))
    check(rep["verdict"] == "FAIL" and rep["last_matching_frame"] is None,
          "head: divergence at head → FAIL, no last match")
    check(rep["first_divergence"]["transition"] is None, "head: transition None at window head")


# ── VERDICT (fail closed) ───────────────────────────────────────────────────────

def test_verdicts():
    ident = _paired({0: (sfields(), sfields()), 1: (sfields(), sfields())})
    check(build_report(ident, SCHEMA, _req(0, 1))["verdict"] == "PASS",
          "verdict: all identical → PASS")
    # empty required → INCONCLUSIVE
    check(build_report(ident, SCHEMA, [])["verdict"] == "INCONCLUSIVE",
          "verdict: empty required → INCONCLUSIVE")
    # has_state false → NOT_CAPTURED
    check(build_report(ident, SCHEMA, _req(0, 1), has_state=False)["verdict"] == "NOT_CAPTURED",
          "verdict: has_state false → NOT_CAPTURED")
    # an uncovered required frame, rest identical → NOT_CAPTURED + first_uncovered
    rep = build_report(ident, SCHEMA, _req(0, 1, 2))
    check(rep["verdict"] == "NOT_CAPTURED" and rep["first_uncovered_frame"]["offset"] == 2,
          "verdict: uncovered required frame → NOT_CAPTURED, localized")
    # a divergence BEFORE an uncovered frame still FAILs (a real disproof outranks a gap)
    paired = _paired({0: (sfields(), sfields()), 1: (sfields(db054=1), sfields(db054=2))})
    rep = build_report(paired, SCHEMA, _req(0, 1, 2))
    check(rep["verdict"] == "FAIL" and rep["first_divergence"] is not None,
          "verdict: divergence before a coverage gap → FAIL")


def test_matches_adapter():
    """build_report's diagnostic verdict agrees with adapt_state (authoritative) on
    the cases both see — PASS, FAIL, NOT_CAPTURED."""
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        cases = {
            "PASS": _paired({0: (sfields(), sfields()), 1: (sfields(), sfields())}),
            "FAIL": _paired({0: (sfields(), sfields()), 1: (sfields(db054=1), sfields(db054=2))}),
        }
        for want, paired in cases.items():
            req = _req(0, 1)
            rep = build_report(paired, SCHEMA, req)
            # produce the metrics doc the adapter grades (same paired input)
            from parity.state_producer import compare_states
            doc = compare_states(paired, SCHEMA, req)
            mp = tmp / "state-metrics.json"
            mp.write_text(json.dumps(doc))
            adj = adapt_state(mp, req).pillar["verdict"]
            check(rep["verdict"] == want == adj,
                  f"adapter-agree: {want} (report={rep['verdict']} adapter={adj})")


# ── STABLE JSON + TEXT ──────────────────────────────────────────────────────────

def test_stable_json():
    paired = _paired({0: (sfields(), sfields()), 1: (sfields(db054=42), sfields(db054=43))})
    rep = build_report(paired, SCHEMA, _req(0, 1))
    a = json.dumps(rep, sort_keys=True)
    b = json.dumps(build_report(paired, SCHEMA, _req(0, 1)), sort_keys=True)
    check(a == b, "stable-json: identical input → byte-identical report")
    txt = render_text(rep)
    check("FAIL" in txt and "phase/db054" in txt, "text: summary carries verdict + path")


# ── VIEW BRIDGE ─────────────────────────────────────────────────────────────────

def _write_view(path, frames, has_state=True):
    path.write_text(json.dumps({"has_state": has_state, "frames": frames}))


def test_view_bridge(tmp):
    tmp = Path(tmp)
    labels = _labels(0, 1)
    frames = [{"label": labels[0], "state": {"port": sfields(), "retail": sfields()}},
              {"label": labels[1], "state": {"port": sfields(db054=42),
                                             "retail": sfields(db054=43)}}]
    vp = tmp / "view.json"
    _write_view(vp, frames)
    # scoped to required
    rep = state_diff_from_view_json(vp, required=_req(0, 1))
    check(rep["verdict"] == "FAIL" and rep["first_divergence"]["path"] == "phase/db054",
          "view: scoped bridge localizes")
    # --all-frames (required None → walk every both-sided frame)
    rep = state_diff_from_view_json(vp, required=None)
    check(rep["n_required"] == 2 and rep["verdict"] == "FAIL", "view: all-frames scan")
    # has_state false, a real (non-empty) contract window → NOT_CAPTURED (re-drive)
    _write_view(vp, frames, has_state=False)
    check(state_diff_from_view_json(vp, required=_req(0, 1))["verdict"] == "NOT_CAPTURED",
          "view: has_state false → NOT_CAPTURED")
    # has_state false AND all-frames (no frames captured ⇒ empty required) → the
    # empty-required guard dominates → INCONCLUSIVE, matching adapt_state's precedence
    check(state_diff_from_view_json(vp, required=None)["verdict"] == "INCONCLUSIVE",
          "view: has_state false + all-frames (empty required) → INCONCLUSIVE")


# ── CLI exit codes (§4.1) ───────────────────────────────────────────────────────

def _run_cli(argv) -> tuple[int, str]:
    buf = io.StringIO()
    with redirect_stdout(buf):
        rc = cli.main(argv)
    return rc, buf.getvalue()


def test_cli(tmp):
    tmp = Path(tmp)
    win = tmp / "win-0-2"
    win.mkdir()
    labels = _labels(0, 1)
    # PASS window
    _write_view(win / "view.json",
                [{"label": labels[0], "state": {"port": sfields(), "retail": sfields()}},
                 {"label": labels[1], "state": {"port": sfields(), "retail": sfields()}}])
    rc, out = _run_cli(["scen", "--from-window", str(win), "--all-frames"])
    check(rc == 0 and "PASS" in out, "cli: PASS → exit 0")
    # FAIL window
    _write_view(win / "view.json",
                [{"label": labels[0], "state": {"port": sfields(), "retail": sfields()}},
                 {"label": labels[1], "state": {"port": sfields(db054=1),
                                                "retail": sfields(db054=2)}}])
    rc, out = _run_cli(["scen", "--from-window", str(win), "--all-frames", "--json"])
    check(rc == 1, "cli: FAIL → exit 1")
    check(json.loads(out)["first_divergence"]["path"] == "phase/db054", "cli: --json report")
    # absent view → exit 2
    rc, _ = _run_cli(["scen", "--from-window", str(tmp / "nope"), "--all-frames"])
    check(rc == 2, "cli: absent window → exit 2")


def test_wiring():
    check(callable(build_state_diff_report) and callable(state_diff_from_view_json),
          "wiring: build_state_diff_report + bridge exported from parity")
    check(build_state_diff_report is build_report, "wiring: export is the core fn")


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        test_all_divergent_leaves()
        test_leaf_localization_and_transition()
        test_transition_interpretations()
        test_presence_asymmetry()
        test_co_divergent_leaves()
        test_head_of_window_divergence()
        test_verdicts()
        test_matches_adapter()
        test_stable_json()
        test_view_bridge(tmp)
        test_cli(tmp)
        test_wiring()

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
