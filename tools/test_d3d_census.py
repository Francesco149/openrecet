#!/usr/bin/env python3
"""tools/test_d3d_census.py — GX-00 drift guard for the D3D8 method census.

Asserts the committed `proxy_generated.h` matches the R3 census
`d3d8-method-census-v1.json`: every vtable method classified exactly once, every
classified method present, and each actual mode == its class's expected mode. Any drift
(a method flipped recorded↔forwarded, added, removed, or misclassified by a
d3d8.h/gen_forwarders change) fails here — so the census can never silently rot and a
newly-forwarded render-affecting method can't hide.

  * CONSISTENT — the shipped proxy/census pair verifies clean (ok, no drift).
  * COMPLETE — all 113 vtable methods (16 + 97) are covered; the census/mode maps are
    total; the RISK list carries every roadmap-named high-risk method.
  * NEGATIVE — a flipped mode, a dropped classification, and a phantom method each drift.
  * CLI — d3d_census.main exits 0 clean.

Run: nix develop --command python3 tools/test_d3d_census.py
"""
from __future__ import annotations

import copy
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import d3d_census as cli  # noqa: E402
from parity.d3d_census import (  # noqa: E402
    DEFAULT_CENSUS,
    DEFAULT_HEADER,
    RISK_CLASS,
    build_report,
    load_census,
    parse_vtable,
    render_text,
    verify,
)

_checks = 0
_failures: list[str] = []


def check(cond: bool, msg: str) -> None:
    global _checks
    _checks += 1
    if not cond:
        _failures.append(msg)


HEADER = DEFAULT_HEADER.read_text(encoding="utf-8")
CENSUS = load_census(DEFAULT_CENSUS)


def test_consistent():
    ok, issues, methods = verify(HEADER, CENSUS)
    check(ok and not issues, f"consistent: committed proxy matches census (drift: {issues[:2]})")
    check(len(methods) == 113, f"complete: 113 vtable methods covered (got {len(methods)})")


def test_counts():
    rep = build_report(HEADER, CENSUS)
    check(rep["ok"], "counts: report ok")
    # D3D8: 4 my_ (3 wrapper + CreateDevice) / 12 fwd_; Device: 25 my_ (3 wrapper + 22) / 72 fwd_
    d3d8 = parse_vtable(HEADER, "IDirect3D8")
    dev = parse_vtable(HEADER, "IDirect3DDevice8")
    check(sum(1 for _, m in d3d8 if m == "recorded") == 4, "counts: IDirect3D8 4 recorded")
    check(sum(1 for _, m in dev if m == "recorded") == 25, "counts: IDirect3DDevice8 25 recorded")
    check(sum(1 for _, m in dev if m == "forwarded") == 72, "counts: IDirect3DDevice8 72 forwarded")
    # the render-affecting-unsupported RISK set (fail-closed) is the 33 device methods
    check(rep["n_risk"] == 33, f"counts: 33 render-affecting-unsupported (got {rep['n_risk']})")
    check(rep["interfaces"]["IDirect3D8"]["risk"] == [], "counts: IDirect3D8 has no render risk")


def test_mode_map_total():
    classes = set(CENSUS["capture_classes"])
    modes = set(CENSUS["mode_of_class"])
    check(classes == modes, "totality: every capture_class has a mode_of_class entry")
    check(set(CENSUS["mode_of_class"].values()) == {"recorded", "forwarded"},
          "totality: modes are exactly recorded|forwarded")


def test_risk_names():
    rep = build_report(HEADER, CENSUS)
    risk = set(rep["interfaces"]["IDirect3DDevice8"]["risk"])
    # the roadmap §9 GX-00 high-risk seed list must all land in the risk set
    for name in ("Reset", "SetViewport", "SetClipPlane", "MultiplyTransform", "UpdateTexture",
                 "BeginStateBlock", "ApplyStateBlock", "ProcessVertices", "SetVertexShaderConstant",
                 "CreatePixelShader", "SetPixelShader", "SetPixelShaderConstant", "SetGammaRamp",
                 "SetPaletteEntries"):
        check(name in risk, f"risk: {name} is render-affecting-unsupported")
    # the documented asymmetry: SetVertexShader RECORDED, SetPixelShader FORWARDED/risk
    check("SetPixelShader" in risk and "SetVertexShader" not in risk,
          "risk: SetPixelShader forwarded (risk) while SetVertexShader recorded — the asymmetry lead")


def test_negative_mode_flip():
    # simulate GX-02 recording Reset (my_) without reclassifying the census → mode drift
    flipped = HEADER.replace("fwd_IDirect3DDevice8_Reset,", "my_IDirect3DDevice8_Reset,")
    ok, issues, _ = verify(flipped, CENSUS)
    check(not ok and any("Reset" in i and "DRIFT" in i for i in issues),
          "negative: a recorded↔forwarded flip is drift")


def test_negative_dropped_classification():
    # a census that forgot to classify a vtable method → unclassified drift
    c = copy.deepcopy(CENSUS)
    c["interfaces"]["IDirect3DDevice8"]["recorded"].remove("DrawPrimitive")
    ok, issues, _ = verify(HEADER, c)
    check(not ok and any("DrawPrimitive" in i and "UNCLASSIFIED" in i for i in issues),
          "negative: an unclassified vtable method is drift")


def test_negative_phantom_method():
    # a census referencing a method not in the vtable → stale drift
    c = copy.deepcopy(CENSUS)
    c["interfaces"]["IDirect3DDevice8"]["query_only"].append("NoSuchMethod")
    ok, issues, _ = verify(HEADER, c)
    check(not ok and any("NoSuchMethod" in i and "ABSENT" in i for i in issues),
          "negative: a phantom census method is drift")


def test_cli():
    check(cli.main([]) == 0, "cli: clean census → exit 0")


def test_text():
    txt = render_text(build_report(HEADER, CENSUS))
    check("OK" in txt and RISK_CLASS in txt and "state_blocks" in txt,
          "text: summary carries verdict + risk class + a subgroup")


def main() -> int:
    test_consistent()
    test_counts()
    test_mode_map_total()
    test_risk_names()
    test_negative_mode_flip()
    test_negative_dropped_classification()
    test_negative_phantom_method()
    test_cli()
    test_text()

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
