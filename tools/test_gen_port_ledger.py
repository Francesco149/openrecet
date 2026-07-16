#!/usr/bin/env python3
"""tools/test_gen_port_ledger.py — EP-06 gate for the two-axis port ledger.

Proves the roadmap §EP-06 acceptance on the classifier directly (synthetic
inputs — no filesystem scan needed), plus the compatibility contract:

  * adding CALL_TRACE_ENTER changes only `instrumented` (nothing on the runtime
    axis);
  * a runtime state requires a proof-index artifact (absent → null; present →
    the VA advances; malformed entries fail closed);
  * a bare FUN_ comment reaches `source-referenced`, NEVER `implemented`;
  * PORT-OF(0xVA) reaches `implemented` without a probe;
  * the DEPRECATED legacy `status` alias stays byte-stable (mem_watch.py);
  * the live tree builds and its counts are self-consistent (INVENTORY ≠ PARITY:
    runtime_proven is 0 until the index binds a VA).

Run: nix develop --command python3 tools/test_gen_port_ledger.py
Design: docs/findings/parity-EP06-ledger-lifecycle.md.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_port_ledger as G  # noqa: E402

_checks = 0
_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


# A minimal synthetic engine-function universe.  VAs chosen to exercise each
# inventory rung independently.
FUNCS = {
    0x1000: {"name": "FUN_00001000", "size": 10, "is_thunk": False},  # discovered
    0x2000: {"name": "FUN_00002000", "size": 20, "is_thunk": False},  # source-referenced
    0x3000: {"name": "FUN_00003000", "size": 30, "is_thunk": False},  # implemented (PORT-OF)
    0x4000: {"name": "FUN_00004000", "size": 40, "is_thunk": False},  # instrumented (full)
    0x5000: {"name": "FUN_00005000", "size": 50, "is_thunk": False},  # instrumented (stub)
    0x6000: {"name": "thunk",        "size": 6,  "is_thunk": True},   # excluded from counts
}


def classify_one(va, *, verified=None, stubbed=None, ported=None, port_of=None,
                 proof_index=None):
    """Run G.classify over FUNCS with the given per-VA marker sets."""
    mk = lambda d: {k: sorted(v) for k, v in (d or {}).items()}
    entries, _ = G.classify(
        FUNCS, set(), mk(verified), mk(stubbed), mk(ported), mk(port_of),
        proof_index or {})
    return entries[va]


def test_bare_fun_ref_is_source_referenced_not_implemented():
    """§EP-06 acceptance: a FUN_ comment cannot claim implementation."""
    e = classify_one(0x2000, ported={0x2000: {"src/x.c"}})
    check(e["inventory_state"] == "source-referenced",
          f"bare FUN_ ref → source-referenced, got {e['inventory_state']}")
    check(e["evidence"]["source_referenced"] is True, "source_referenced fact set")
    check(e["evidence"]["implemented"] is False,
          "a bare FUN_ comment MUST NOT set implemented")
    check(e["evidence"]["instrumented"] is False, "no probe → not instrumented")
    check(e["runtime_state"] is None, "no proof → runtime_state null")
    check(e["status"] == "ported", "legacy alias: FUN_ ref → ported")


def test_port_of_reaches_implemented_without_probe():
    e = classify_one(0x3000, port_of={0x3000: {"src/y.c"}})
    check(e["inventory_state"] == "implemented",
          f"PORT-OF → implemented, got {e['inventory_state']}")
    check(e["evidence"]["implemented"] is True, "PORT-OF sets implemented")
    check(e["evidence"]["instrumented"] is False, "PORT-OF alone is not a probe")
    check(e["evidence"]["source_referenced"] is True,
          "PORT-OF is a src reference (source_referenced)")
    check(e["status"] == "ported", "legacy alias: PORT-OF-only → ported")


def test_probe_only_reaches_instrumented():
    """§EP-06 acceptance: adding CALL_TRACE_ENTER changes only `instrumented`."""
    # Start from a bare FUN_ ref (source-referenced), then add a full probe.
    before = classify_one(0x4000, ported={0x4000: {"src/z.c"}})
    check(before["inventory_state"] == "source-referenced", "pre-probe: source-referenced")

    after = classify_one(0x4000, ported={0x4000: {"src/z.c"}},
                         verified={0x4000: {"src/z.c"}})
    check(after["inventory_state"] == "instrumented",
          f"probe → instrumented, got {after['inventory_state']}")
    check(after["evidence"]["instrumented"] is True, "instrumented fact set")
    check(after["evidence"]["implemented"] is True, "a full probe implies implemented")
    check("stub" not in after["quality_flags"], "full probe is not a stub")
    # The runtime axis MUST be untouched by a source probe.
    check(after["runtime_state"] is None, "probe does NOT advance runtime_state")
    for rung in G.RUNTIME_LADDER:
        key = rung.replace("-", "_").replace("I/O", "io").lower()
        check(after["evidence"][key] is False,
              f"probe must not set runtime evidence {key}")
    check(after["status"] == "verified", "legacy alias: full probe → verified")


def test_stub_is_instrumented_with_flag():
    e = classify_one(0x5000, stubbed={0x5000: {"src/s.c"}})
    check(e["inventory_state"] == "instrumented", "stub reaches instrumented rung")
    check(e["quality_flags"] == ["stub"], f"stub flag set, got {e['quality_flags']}")
    check(e["evidence"]["instrumented_stub"] is True, "instrumented_stub fact set")
    check(e["evidence"]["implemented"] is False,
          "a stub body is not a full implementation")
    check(e["status"] == "stubbed", "legacy alias: stub → stubbed")


def test_discovered_floor():
    e = classify_one(0x1000)
    check(e["inventory_state"] == "discovered", "no marker → discovered")
    check(e["evidence"]["source_referenced"] is False, "no ref")
    check(e["status"] == "unported", "legacy alias: no marker → unported")


def test_runtime_state_needs_proof_index():
    """§EP-06 acceptance: executed/aligned states require a proof artifact."""
    # No index → null even for a fully instrumented function.
    e0 = classify_one(0x4000, verified={0x4000: {"src/z.c"}})
    check(e0["runtime_state"] is None,
          "instrumented but no proof-index entry → runtime_state null")

    # A synthetic index entry advances the VA to scenario-pillar-proven.
    # The DURABLE key is contract_sha256; proof_id is optional + advisory.
    idx = {0x4000: [{"state": "scenario-pillar-proven",
                     "contract_sha256": "a" * 64, "scenario": "syn",
                     "scope": "HF#1[1,80]", "pillars": ["render_program"],
                     "proof_id": "b" * 64}]}
    e1 = classify_one(0x4000, verified={0x4000: {"src/z.c"}}, proof_index=idx)
    check(e1["runtime_state"] == "scenario-pillar-proven",
          f"proof entry → scenario-pillar-proven, got {e1['runtime_state']}")
    check(e1["evidence"]["scenario_pillar_proven"] is True, "runtime fact set")
    check(e1["evidence"]["retail_executed"] is True,
          "a higher runtime rung implies the lower ones reached")
    check(e1["evidence"]["matrix_proven"] is False, "matrix rung not reached")
    check(len(e1["proofs"]) == 1
          and e1["proofs"][0]["contract_sha256"] == "a" * 64
          and e1["proofs"][0]["proof_id"] == "b" * 64,
          "proof ref recorded (durable contract key + advisory proof_id)")
    # The inventory axis is unchanged by the runtime binding.
    check(e1["inventory_state"] == "instrumented",
          "runtime proof does not alter inventory_state")


def test_proof_index_fails_closed():
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        SHA = "a" * 64
        # Absent file → {}.
        check(G.load_proof_index(tmp / "nope.json") == {}, "absent index → {}")

        # Invalid runtime state → SystemExit.
        bad_state = tmp / "bad_state.json"
        bad_state.write_text(json.dumps({"entries": [
            {"va": "0x4000", "state": "instrumented", "contract_sha256": SHA}]}))
        try:
            G.load_proof_index(bad_state)
            check(False, "invalid runtime state must fail closed")
        except SystemExit:
            check(True, "invalid runtime state raised")

        # Missing contract_sha256 (the durable key) → SystemExit.
        no_contract = tmp / "no_contract.json"
        no_contract.write_text(json.dumps({"entries": [
            {"va": "0x4000", "state": "retail-executed"}]}))
        try:
            G.load_proof_index(no_contract)
            check(False, "missing contract_sha256 must fail closed")
        except SystemExit:
            check(True, "missing contract_sha256 raised")

        # A malformed contract_sha256 (not 64-hex) → SystemExit.
        bad_sha = tmp / "bad_sha.json"
        bad_sha.write_text(json.dumps({"entries": [
            {"va": "0x4000", "state": "retail-executed", "contract_sha256": "abc"}]}))
        try:
            G.load_proof_index(bad_sha)
            check(False, "malformed contract_sha256 must fail closed")
        except SystemExit:
            check(True, "malformed contract_sha256 raised")

        # A present-but-malformed proof_id → SystemExit (advisory, but must be a sha256).
        bad_pid = tmp / "bad_pid.json"
        bad_pid.write_text(json.dumps({"entries": [
            {"va": "0x4000", "state": "retail-executed",
             "contract_sha256": SHA, "proof_id": "nope"}]}))
        try:
            G.load_proof_index(bad_pid)
            check(False, "malformed proof_id must fail closed")
        except SystemExit:
            check(True, "malformed proof_id raised")

        # A valid entry parses — proof_id is OPTIONAL (omitted here).
        ok = tmp / "ok.json"
        ok.write_text(json.dumps({"entries": [
            {"va": "0x4000", "state": "port-executed", "contract_sha256": SHA}]}))
        parsed = G.load_proof_index(ok)
        check(0x4000 in parsed and parsed[0x4000][0]["state"] == "port-executed",
              "valid entry parsed by VA (proof_id optional)")


def test_live_tree_builds_and_is_consistent():
    """The real repo builds, counts are self-consistent, and the shipped index's
    single binding (FUN_004905a8 → scenario-pillar-proven) is the ONLY runtime
    proof (INVENTORY ≠ PARITY)."""
    entries, orphans, counts = G.build()
    real = [e for e in entries.values() if not e["is_thunk"]]
    check(len(real) == counts["non_thunk_functions"], "non_thunk count matches")

    # Inventory rungs partition the non-thunk universe.
    rung_sum = (counts["inv_instrumented"] + counts["inv_implemented"]
                + counts["inv_source_referenced"] + counts["inv_discovered"])
    check(rung_sum == counts["non_thunk_functions"],
          f"inventory rungs partition the universe ({rung_sum} != {counts['non_thunk_functions']})")
    check(counts["referenced_or_better"]
          == counts["inv_instrumented"] + counts["inv_implemented"] + counts["inv_source_referenced"],
          "referenced_or_better = sum of the three marked rungs")
    check(counts["inv_instrumented"]
          == counts["inv_instrumented_full"] + counts["inv_instrumented_stub"],
          "instrumented = full + stub")

    # Legacy aliases stay consistent with the new axes (mem_watch contract).
    check(counts["verified"] == counts["inv_instrumented_full"], "alias verified")
    check(counts["stubbed"] == counts["inv_instrumented_stub"], "alias stubbed")
    check(counts["unported"] == counts["inv_discovered"], "alias unported")
    check(counts["touched"] == counts["referenced_or_better"], "alias touched")

    # The shipped index binds exactly one VA (the save-commit path, ★NEXT-b′);
    # every other function stays runtime-null. INVENTORY must not masquerade as
    # parity — one proven binding, not a source-marker inventory.
    check(counts["runtime_proven"] == 1,
          f"runtime_proven must be 1 with the shipped index, got {counts['runtime_proven']}")
    proven = [e for e in real if e["runtime_state"] is not None]
    check(len(proven) == 1 and proven[0]["va"] == "0x4905a8"
          and proven[0]["runtime_state"] == "scenario-pillar-proven",
          f"the one runtime-proven VA is FUN_004905a8 @ scenario-pillar-proven, "
          f"got {[(e['va'], e['runtime_state']) for e in proven]}")
    check(bool(proven[0]["proofs"])
          and proven[0]["proofs"][0]["contract_sha256"]
              == "9c2d27556b6f0d4b36ba867ca1de87dda605d0d18ed7b1c5e072ad8a72eb76bc",
          "the binding is keyed on the stable house-pause-save-commit contract_sha256")

    # Every function carries the full evidence fact-set + a legacy status.
    for e in real[:50]:
        check(set(e["evidence"]) >= {"discovered", "source_referenced", "implemented",
                                     "instrumented", "scenario_pillar_proven"},
              "evidence fact-set present")
        check(e["status"] in ("verified", "stubbed", "ported", "unported"),
              f"legacy status in enum, got {e['status']}")


def main() -> int:
    test_bare_fun_ref_is_source_referenced_not_implemented()
    test_port_of_reaches_implemented_without_probe()
    test_probe_only_reaches_instrumented()
    test_stub_is_instrumented_with_flag()
    test_discovered_floor()
    test_runtime_state_needs_proof_index()
    test_proof_index_fails_closed()
    test_live_tree_builds_and_is_consistent()

    if _failures:
        print("FAIL:")
        for f in _failures:
            print("  -", f)
        return 1
    print(f"OK ({_checks} checks)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
