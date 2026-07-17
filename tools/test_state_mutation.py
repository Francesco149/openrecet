#!/usr/bin/env python3
"""tools/test_state_mutation.py — gate for ST-05, the semantic-mutation CONSUMER.

Proves the consumer's truth-defining logic with NO capture dependency, on synthetic
mutation streams (the Frida post-write / TTD PLATFORM lands behind this gate — roadmap
rule 11):

  * PARSE/VALIDATE — a well-formed event parses; an unknown class/type, a bad path/seq,
    a missing `new`, or a wrong stream major RAISES (fail closed).
  * DEDUP/IDEMPOTENCE — an identical re-observation collapses; a CONFLICTING double-
    observation (same key, different `new`) raises.
  * RECONSTRUCT — replay under a prefix rebuilds the subtree value (last-write-wins);
    `up_to` scopes it; noise is excluded; verify_reconstruction cross-checks captured state.
  * FIRST WRONG WRITE — value / port-missing / port-extra localize; a one-sided write
    with unknown start is NOT a false positive; noise never triggers; identical → None.
  * ORDERING INVARIANT (ST-04/ST-05 link) — wrong write ≤ state divergence → ok; after,
    missing-causal, and over-report → not ok.
  * PROVENANCE SEAM — attach_provenance fills an ST-04 first_divergence.provenance with
    the writer + attaches the ordering check; a PASS report is a no-op on provenance.
  * SCHEMA — docs/schemas/state-mutation-v1.json loads at version 1.

Run: nix develop --command python3 tools/test_state_mutation.py
"""
from __future__ import annotations

import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import state_diff as diff_cli  # noqa: E402
from parity.observations import LogicalFrame, ObservationError  # noqa: E402
from parity.state_codec import StateSchema, encode_value  # noqa: E402
from parity.state_mutation import (  # noqa: E402
    COMPARED,
    MUTATION_SCHEMA_VERSION,
    attach_provenance,
    check_ordering,
    dedup,
    first_wrong_write,
    load_stream,
    parse_mutation,
    reconstruct_subtree,
    verify_reconstruction,
)

_checks = 0
_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


def raises(fn, exc, msg: str) -> None:
    try:
        fn()
    except exc:
        check(True, msg)
    except Exception as e:  # noqa: BLE001
        check(False, f"{msg} (raised {type(e).__name__}, want {exc.__name__})")
    else:
        check(False, f"{msg} (did not raise)")


SCHEMA = StateSchema.load()
REQ = [LogicalFrame.from_label(f"SAVE_PICKER_READY#1+{o}") for o in range(6)]


def ev(off, path, new, *, old=0, cls="semantic", typ="i32", seq=0, owner="0x004905a8"):
    return {"logical_frame": ["SAVE_PICKER_READY", 1, off], "seq": seq, "path": path,
            "class": cls, "type": typ, "old": old, "new": new, "owner_va": owner}


def muts(*evs):
    return [parse_mutation(e) for e in evs]


# ── PARSE / VALIDATE ────────────────────────────────────────────────────────────

def test_parse():
    m = parse_mutation(ev(1, "customer_service/gold", 800, old=500))
    check(m.path == "customer_service/gold" and m.new == 800 and m.old == 500,
          "parse: fields")
    check(m.canon == encode_value("i32", 800) and m.old_canon == encode_value("i32", 500),
          "parse: canon bytes for new + old")
    raises(lambda: parse_mutation(ev(1, "customer_service/gold", 1, cls="weird")),
           ObservationError, "parse: unknown class raises")
    raises(lambda: parse_mutation(ev(1, "customer_service/gold", 1, typ="i64")),
           ObservationError, "parse: unknown type raises")
    raises(lambda: parse_mutation(ev(1, "nogslash", 1)), ObservationError,
           "parse: path without '/' raises")
    raises(lambda: parse_mutation({"logical_frame": ["A", 1, 0], "path": "a/b",
                                   "class": "semantic", "type": "i32"}),
           ObservationError, "parse: missing new raises")
    # stream major
    good = {"schema_version": MUTATION_SCHEMA_VERSION, "side": "port",
            "mutations": [ev(1, "customer_service/gold", 800, old=500)]}
    side, ms = load_stream(good)
    check(side == "port" and len(ms) == 1, "load_stream: valid stream")
    raises(lambda: load_stream({"schema_version": 99, "mutations": []}),
           ObservationError, "load_stream: wrong major raises")


# ── DEDUP / IDEMPOTENCE ─────────────────────────────────────────────────────────

def test_dedup():
    same = muts(ev(1, "customer_service/gold", 800, old=500),
                ev(1, "customer_service/gold", 800, old=500))
    check(len(dedup(same)) == 1, "dedup: identical re-observation collapses")
    conflict = muts(ev(1, "customer_service/gold", 800, old=500),
                    ev(1, "customer_service/gold", 999, old=500))
    raises(lambda: dedup(conflict), ObservationError,
           "dedup: conflicting double-observation raises")


# ── RECONSTRUCT ─────────────────────────────────────────────────────────────────

def test_reconstruct():
    ms = muts(ev(1, "customer_service/gold", 800, old=500),
              ev(3, "customer_service/gold", 900, old=800),
              ev(2, "customer_service/b590", 5, old=0),
              ev(2, "rng/rng", 123, old=1, cls="noise"))
    sub = reconstruct_subtree(ms, REQ, "customer_service/")
    check(sub["customer_service/gold"].new == 900, "reconstruct: last-write-wins")
    check("customer_service/b590" in sub, "reconstruct: sibling path included")
    check("rng/rng" not in sub, "reconstruct: other prefix excluded")
    # up_to scopes it
    sub1 = reconstruct_subtree(ms, REQ, "customer_service/",
                               up_to=LogicalFrame.from_label("SAVE_PICKER_READY#1+1"))
    check(sub1["customer_service/gold"].new == 800, "reconstruct: up_to scopes to f1")
    # noise excluded even under its own prefix
    check(reconstruct_subtree(ms, REQ, "rng/") == {}, "reconstruct: noise class excluded")
    # verify against captured state
    ok, mm = verify_reconstruction(sub, {"gold": 900, "b590": 5}, SCHEMA, "customer_service/")
    check(ok and not mm, "verify: reconstruction matches captured state")
    ok, mm = verify_reconstruction(sub, {"gold": 111, "b590": 5}, SCHEMA, "customer_service/")
    check(not ok and mm[0]["path"] == "customer_service/gold", "verify: mismatch localized")


# ── FIRST WRONG WRITE ───────────────────────────────────────────────────────────

def test_first_wrong_value():
    port = muts(ev(2, "customer_service/gold", 800, old=500))
    retail = muts(ev(2, "customer_service/gold", 900, old=500))
    fw = first_wrong_write(port, retail, REQ)
    check(fw and fw["path"] == "customer_service/gold" and fw["kind"] == "value",
          "first-wrong: value divergence localized")
    check(fw["logical_frame"]["offset"] == 2, "first-wrong: at the right frame")
    check(fw["port_value"] == 800 and fw["retail_value"] == 900, "first-wrong: both values")


def test_first_wrong_missing_extra():
    # retail changes f404, port never does → port_missing (port holds the shared start 0)
    port: list = []
    retail = muts(ev(2, "customer_service/f404", 1, old=0))
    fw = first_wrong_write(port, retail, REQ)
    check(fw and fw["kind"] == "port_missing" and fw["port_value"] == 0 and fw["retail_value"] == 1,
          "first-wrong: port_missing (retail moved, port at start)")
    # port changes db054, retail doesn't → port_extra
    fw = first_wrong_write(muts(ev(2, "phase/db054", 7, old=0)), [], REQ)
    check(fw and fw["kind"] == "port_extra" and fw["port_value"] == 7 and fw["retail_value"] == 0,
          "first-wrong: port_extra (port moved, retail at start)")


def test_first_wrong_none_and_noise():
    both = muts(ev(2, "customer_service/gold", 800, old=500))
    check(first_wrong_write(both, list(both), REQ) is None,
          "first-wrong: identical streams → None")
    # a NOISE-class divergence must not trigger
    port = muts(ev(2, "rng/rng", 111, old=0, cls="noise"))
    retail = muts(ev(2, "rng/rng", 222, old=0, cls="noise"))
    check(first_wrong_write(port, retail, REQ) is None, "first-wrong: noise excluded")
    # a one-sided write with UNKNOWN start (old=None) is not a false positive
    port = muts({"logical_frame": ["SAVE_PICKER_READY", 1, 2], "seq": 0,
                 "path": "customer_service/gold", "class": "semantic", "type": "i32", "new": 800})
    check(first_wrong_write(port, [], REQ) is None,
          "first-wrong: one-sided write, unknown start → not flagged")


# ── ORDERING INVARIANT ──────────────────────────────────────────────────────────

def test_ordering():
    div = LogicalFrame.from_label("SAVE_PICKER_READY#1+3")
    before = {"logical_frame": {"anchor": "SAVE_PICKER_READY", "occurrence": 1, "offset": 2}}
    at = {"logical_frame": {"anchor": "SAVE_PICKER_READY", "occurrence": 1, "offset": 3}}
    after = {"logical_frame": {"anchor": "SAVE_PICKER_READY", "occurrence": 1, "offset": 4}}
    check(check_ordering(before, div, REQ)["ok"], "ordering: wrong write before divergence → ok")
    check(check_ordering(at, div, REQ)["ok"], "ordering: wrong write AT divergence → ok")
    check(not check_ordering(after, div, REQ)["ok"], "ordering: wrong write after → not ok")
    check(not check_ordering(None, div, REQ)["ok"], "ordering: divergence but no wrong write → not ok")
    check(check_ordering(None, None, REQ)["ok"], "ordering: no divergence + no wrong → ok")
    check(not check_ordering(before, None, REQ)["ok"], "ordering: wrong write but no divergence → not ok")


# ── PROVENANCE SEAM ─────────────────────────────────────────────────────────────

def _st04(path, off, verdict="FAIL"):
    fd = None
    if verdict == "FAIL":
        fd = {"logical_frame": {"anchor": "SAVE_PICKER_READY", "occurrence": 1, "offset": off},
              "path": path, "provenance": None}
    return {"verdict": verdict, "first_divergence": fd}


def test_provenance():
    port = muts(ev(2, "customer_service/gold", 800, old=500, owner="0x00460d52"))
    retail = muts(ev(2, "customer_service/gold", 900, old=500, owner="0x00460d52"))
    rep = attach_provenance(_st04("customer_service/gold", 2), port, retail, REQ)
    prov = rep["first_divergence"]["provenance"]
    check(prov and prov["owner_va"] == "0x00460d52" and prov["same_leaf"] is True,
          "provenance: writer VA filled, same_leaf")
    check(rep["mutation_ordering"]["ok"], "provenance: ordering check attached + ok")
    # a PASS report → provenance no-op, ordering still attached (no divergence, no wrong)
    rep = attach_provenance(_st04("", 0, verdict="PASS"),
                            muts(ev(2, "customer_service/gold", 800, old=500)),
                            muts(ev(2, "customer_service/gold", 800, old=500)), REQ)
    check(rep["first_divergence"] is None and rep["mutation_ordering"]["ok"],
          "provenance: PASS report → no-op provenance, ordering ok")


# ── SCHEMA ──────────────────────────────────────────────────────────────────────

def test_cli_end_to_end():
    """The full ST-04 + ST-05 path through the state_diff CLI: a --state window whose
    gold diverges + matching mutation streams → the report's first_divergence carries
    the WRITER provenance and passes the ordering invariant."""
    with tempfile.TemporaryDirectory() as td:
        win = Path(td) / "win-0-2"
        win.mkdir()
        labels = ["SAVE_PICKER_READY#1+0", "SAVE_PICKER_READY#1+1"]
        view = {"has_state": True, "frames": [
            {"label": labels[0], "state": {"port": {"gold": 500}, "retail": {"gold": 500}}},
            {"label": labels[1], "state": {"port": {"gold": 800}, "retail": {"gold": 900}}}]}
        (win / "view.json").write_text(json.dumps(view))
        stream = lambda new: {"schema_version": MUTATION_SCHEMA_VERSION, "mutations": [
            ev(1, "customer_service/gold", new, old=500, owner="0x00460d52")]}
        (win / "port-state-mutation.json").write_text(json.dumps(stream(800)))
        (win / "retail-state-mutation.json").write_text(json.dumps(stream(900)))

        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = diff_cli.main(["scen", "--from-window", str(win), "--all-frames",
                                "--mutations", "--json"])
        rep = json.loads(buf.getvalue())
        check(rc == 1, "cli-e2e: FAIL exit 1")
        fd = rep["first_divergence"]
        check(fd["path"] == "customer_service/gold", "cli-e2e: divergence localized")
        prov = fd.get("provenance")
        check(prov and prov["owner_va"] == "0x00460d52" and prov["same_leaf"],
              "cli-e2e: provenance owner_va from the mutation stream")
        check(rep["mutation_ordering"]["ok"], "cli-e2e: ordering invariant holds")


def test_schema_doc():
    doc = json.loads((ROOT / "docs/schemas/state-mutation-v1.json").read_text())
    check(doc["schema_version"] == MUTATION_SCHEMA_VERSION == 1, "schema: version 1")
    for cls in ("semantic", "derived", "noise"):
        check(cls in doc["classes"], f"schema: class {cls} defined")
    check(doc["semantic_events"]["save_slot_commit"]["owner"] == "0x004905a8",
          "schema: save_slot_commit grounded to FUN_004905a8")
    check(COMPARED == frozenset({"semantic", "derived"}), "schema: noise is the only excluded class")


def main() -> int:
    test_parse()
    test_dedup()
    test_reconstruct()
    test_first_wrong_value()
    test_first_wrong_missing_extra()
    test_first_wrong_none_and_noise()
    test_ordering()
    test_provenance()
    test_cli_end_to_end()
    test_schema_doc()

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
