#!/usr/bin/env python3
"""GX-06 corpus coverage-gate tests (roadmap parity-evidence-roadmap.md §9 GX-06).

Pure, synthetic — reasons over the committed manifest + census (no caches, no replay.exe).
Proves the SHIPPED corpus is COMPLETE and that the gate catches each failure mode
fail-closed: a missing fixture, an observed-but-not-bit-exact opcode, and every direction of
opcode↔census drift. Run:
  nix develop --command python3 tools/test_gx_corpus.py
"""
from __future__ import annotations

import copy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from parity import gx_corpus
from parity.d3d_census import load_census

CENSUS = load_census()
MANIFEST = gx_corpus.load_manifest()


def _kinds(report) -> set[str]:
    return {g["kind"] for g in report["gaps"]} | {d["kind"] for d in report["drift"]}


def test_shipped_complete() -> None:
    """The committed manifest must gate COMPLETE against the committed census — every
    recorded opcode fixture-covered, every observed opcode bit-exact-proven, no drift."""
    r = gx_corpus.build_report(MANIFEST, CENSUS)
    assert r["verdict"] == "COMPLETE", (r["verdict"], r["gaps"], r["drift"])
    assert gx_corpus.gate(r) == 0
    assert r["n_opcodes"] == 25, r["n_opcodes"]
    assert r["n_observed"] == 22 and r["n_unobserved"] == 3, (r["n_observed"], r["n_unobserved"])
    assert r["unobserved"] == ["CopyRects", "DrawIndexedPrimitiveUP", "DrawPrimitive"], r["unobserved"]
    # every opcode is fixture-covered; every observed opcode is proven
    for op, cell in r["matrix"].items():
        assert cell["fixtures"], f"{op} has no fixture"
        if cell["observed"]:
            assert cell["real_proofs"], f"observed {op} has no real proof"
    for k, cell in r["surfref_matrix"].items():
        assert cell["fixtures"], f"SURFREF {k} has no fixture"
    print(f"  OK shipped corpus COMPLETE: 25 opcodes (22 observed proven, 3 unobserved fixture-only), "
          f"4 SURFREF kinds")


def test_missing_fixture_gap() -> None:
    """Drop CopyRects from its only fixture (gx06_rt) ⇒ a no_fixture gap (every recorded
    opcode needs a fixture, even an unobserved one)."""
    m = copy.deepcopy(MANIFEST)
    for fx in m["fixtures"]:
        if fx["name"] == "gx06_rt_fixture":
            fx["opcodes"] = [op for op in fx["opcodes"] if op != "CopyRects"]
    r = gx_corpus.build_report(m, CENSUS)
    assert r["verdict"] == "GAPS" and gx_corpus.gate(r) == 1
    assert "no_fixture" in _kinds(r)
    assert any(g.get("opcode") == "CopyRects" and g["kind"] == "no_fixture" for g in r["gaps"])
    print("  OK missing-fixture: dropping CopyRects's only fixture → no_fixture gap")


def test_observed_unproven_gap() -> None:
    """Flip the pause real proof to DIVERGENT ⇒ RES_RT_TEX/SetRenderTarget (only proven by
    pause) become observed-but-unproven, and the RT SURFREF kinds too."""
    m = copy.deepcopy(MANIFEST)
    for rp in m["real_proofs"]:
        if rp["scenario"] == "house-pause":
            rp["verify"] = "REPLAY_DIVERGENT"
    r = gx_corpus.build_report(m, CENSUS)
    assert r["verdict"] == "GAPS" and gx_corpus.gate(r) == 1
    assert "observed_unproven" in _kinds(r)
    ops = {g.get("opcode") for g in r["gaps"] if g["kind"] == "observed_unproven"}
    assert {"RES_RT_TEX", "SetRenderTarget"} <= ops, ops
    assert "observed_unproven_surfref" in _kinds(r)     # TEX only cited by pause
    print("  OK observed-unproven: a DIVERGENT proof leaves its unique opcodes/SURFREFs unproven")


def test_opcode_map_drift() -> None:
    """Remove an opcode from opcode_methods ⇒ opcode_unmapped drift."""
    m = copy.deepcopy(MANIFEST)
    del m["opcode_methods"]["Clear"]
    r = gx_corpus.build_report(m, CENSUS)
    assert "opcode_unmapped" in _kinds(r) and gx_corpus.gate(r) == 1
    # and a stale mapping (opcode not in orv3.OPNAME) is caught too
    m2 = copy.deepcopy(MANIFEST)
    m2["opcode_methods"]["NotAnOpcode"] = ["IDirect3DDevice8.Clear"]
    assert "map_stale" in _kinds(gx_corpus.build_report(m2, CENSUS))
    print("  OK opcode-map drift: unmapped opcode + stale opcode both caught")


def test_census_method_drift() -> None:
    """A census-recorded method captured by no opcode/SURFREF ⇒ method_uncaptured; a mapped
    method that is NOT recorded in the census ⇒ method_not_recorded."""
    # drop GetBackBuffer's only mapping (surfref BACKBUFFER) — it is a recorded method
    m = copy.deepcopy(MANIFEST)
    m["surfref_methods"]["BACKBUFFER"] = []
    r = gx_corpus.build_report(m, CENSUS)
    assert "method_uncaptured" in _kinds(r) and gx_corpus.gate(r) == 1
    assert any(d.get("method") == "IDirect3DDevice8.GetBackBuffer" for d in r["drift"])
    # map a forwarded (query_only) method as if recorded ⇒ method_not_recorded
    m2 = copy.deepcopy(MANIFEST)
    m2["opcode_methods"]["Clear"].append("IDirect3DDevice8.GetViewport")   # query_only, not recorded
    r2 = gx_corpus.build_report(m2, CENSUS)
    assert "method_not_recorded" in _kinds(r2)
    print("  OK census-method drift: uncaptured recorded method + non-recorded mapped method caught")


def test_schema_version_guard() -> None:
    """load_manifest rejects a wrong schema_version + a manifest missing a required key,
    fail-closed."""
    import json
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        badver = Path(td) / "badver.json"
        badver.write_text(json.dumps({**MANIFEST, "schema_version": 99}))
        try:
            gx_corpus.load_manifest(badver); raise AssertionError("wrong version must raise")
        except ValueError:
            pass
        nokey = Path(td) / "nokey.json"
        m = copy.deepcopy(MANIFEST); del m["real_proofs"]
        nokey.write_text(json.dumps(m))
        try:
            gx_corpus.load_manifest(nokey); raise AssertionError("missing key must raise")
        except ValueError:
            pass
    print("  OK schema-version guard: wrong version + missing required key both rejected")


def main() -> int:
    test_shipped_complete()
    test_missing_fixture_gap()
    test_observed_unproven_gap()
    test_opcode_map_drift()
    test_census_method_drift()
    test_schema_version_guard()
    print("OK: gx_corpus gate — shipped corpus COMPLETE + every gap/drift failure mode caught fail-closed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
