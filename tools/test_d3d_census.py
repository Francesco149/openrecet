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
import json
import os
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import d3d_census as cli  # noqa: E402
from parity.d3d_census import (  # noqa: E402
    DEFAULT_CENSUS,
    DEFAULT_HEADER,
    DYNAMIC_SCHEMA_VERSION,
    RISK_CLASS,
    CensusError,
    _forwarded_keys,
    _risk_index,
    build_dynamic_report,
    build_report,
    load_census,
    load_dynamic,
    parse_vtable,
    render_dynamic_text,
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


# ── GX-00 DYNAMIC census (which forwarded methods were CALLED) + GX-01 gate ──
FWD_KEYS = _forwarded_keys(CENSUS)
RISK, _RISK_SUB = _risk_index(CENSUS)
SETVIEWPORT = "IDirect3DDevice8.SetViewport"
SETPIXELSHADER = "IDirect3DDevice8.SetPixelShader"
GETRENDERSTATE = "IDirect3DDevice8.GetRenderState"


def full_calls(overrides=None):
    """A well-formed forwarded_calls map: every forwarded method 0, then overrides
    (derived from the census so the fixture can never drift from the risk set)."""
    calls = {k: 0 for k in FWD_KEYS}
    if overrides:
        calls.update(overrides)
    return calls


def full_doc(overrides=None):
    return {"schema_version": DYNAMIC_SCHEMA_VERSION, "forwarded_calls": full_calls(overrides)}


def write_doc(doc):
    fd, path = tempfile.mkstemp(suffix=".census.json")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(doc, f)
    return path


def _raises(fn) -> bool:
    try:
        fn()
        return False
    except CensusError:
        return True


def test_dynamic_key_coverage():
    check(len(FWD_KEYS) == 84, f"dynamic: 84 forwarded keys derived from census (got {len(FWD_KEYS)})")
    check(len(RISK) == 33, f"dynamic: 33 risk keys derived from census (got {len(RISK)})")
    check(SETVIEWPORT in RISK and SETPIXELSHADER in RISK, "dynamic: known risk keys present")
    check(GETRENDERSTATE in FWD_KEYS and GETRENDERSTATE not in RISK,
          "dynamic: GetRenderState is forwarded query_only, not a risk method")


def test_dynamic_safe():
    rep = build_dynamic_report(CENSUS, full_calls())
    check(rep["verdict"] == "SAFE", "dynamic: all-zero forwarded → SAFE")
    check(rep["n_risk"] == 33 and rep["n_safe_risk"] == 33, "dynamic: all 33 risk methods 0-observed")
    check(not rep["observed_risk"] and not rep["missing_risk"] and not rep["unknown_keys"],
          "dynamic: SAFE report has no observed/missing/unknown")


def test_dynamic_violation():
    # the roadmap §9 negative test: a deliberate SetViewport cannot pass as complete
    rep = build_dynamic_report(CENSUS, full_calls({SETVIEWPORT: 5}))
    check(rep["verdict"] == "VIOLATION", "dynamic: SetViewport fired → VIOLATION")
    check(rep["observed_risk"].get(SETVIEWPORT) == 5, "dynamic: SetViewport count surfaced")
    check(rep["observed_risk_subgroups"].get(SETVIEWPORT) == "fixed_function_state",
          "dynamic: SetViewport tagged fixed_function_state")


def test_dynamic_query_only_ignored():
    # a heavily-called query_only method must NOT trip the gate (safe to forward)
    rep = build_dynamic_report(CENSUS, full_calls({GETRENDERSTATE: 100000}))
    check(rep["verdict"] == "SAFE", "dynamic: a busy query_only method stays SAFE")
    check(rep["observed_forwarded"].get(GETRENDERSTATE) == 100000,
          "dynamic: query_only call still shows in the informational profile")


def test_dynamic_shader_asymmetry():
    rep = build_dynamic_report(CENSUS, full_calls({SETPIXELSHADER: 3}))
    check(rep["verdict"] == "VIOLATION" and rep["leads"]["SetPixelShader"] == 3,
          "dynamic: SetPixelShader bind → VIOLATION + lead surfaces the count")
    check(build_dynamic_report(CENSUS, full_calls())["leads"]["SetPixelShader"] == 0,
          "dynamic: SetPixelShader lead reads 0 when present + unused")


def test_dynamic_missing_risk_inconclusive():
    calls = full_calls()
    del calls[SETVIEWPORT]                      # a risk method absent from the sidecar
    rep = build_dynamic_report(CENSUS, calls)
    check(rep["verdict"] == "INCONCLUSIVE", "dynamic: a missing risk method → INCONCLUSIVE (fail-closed)")
    check(SETVIEWPORT in rep["missing_risk"], "dynamic: the absent risk method is reported")
    calls2 = full_calls()
    del calls2[SETPIXELSHADER]
    check(build_dynamic_report(CENSUS, calls2)["leads"]["SetPixelShader"] == "absent",
          "dynamic: SetPixelShader lead 'absent' when not in the sidecar")


def test_dynamic_unknown_key_inconclusive():
    rep = build_dynamic_report(CENSUS, full_calls({"IDirect3DDevice8.NoSuchMethod": 1}))
    check(rep["verdict"] == "INCONCLUSIVE", "dynamic: an unknown sidecar key → INCONCLUSIVE (drift)")
    check("IDirect3DDevice8.NoSuchMethod" in rep["unknown_keys"], "dynamic: the unknown key is reported")


def test_dynamic_violation_precedence():
    # a fired risk method AND a missing one → VIOLATION outranks INCONCLUSIVE
    calls = full_calls({SETVIEWPORT: 1})
    del calls[SETPIXELSHADER]
    check(build_dynamic_report(CENSUS, calls)["verdict"] == "VIOLATION",
          "dynamic: a real violation outranks an incomplete-sidecar inconclusive")


def test_dynamic_load_and_malformed():
    p = write_doc(full_doc({SETVIEWPORT: 2}))
    check(load_dynamic(p).get(SETVIEWPORT) == 2, "dynamic: load_dynamic round-trips a count")
    os.unlink(p)
    for label, doc in (
        ("wrong schema_version", {"schema_version": 2, "forwarded_calls": {}}),
        ("missing forwarded_calls", {"schema_version": 1}),
        ("forwarded_calls not an object", {"schema_version": 1, "forwarded_calls": []}),
        ("non-int count", {"schema_version": 1, "forwarded_calls": {SETVIEWPORT: "lots"}}),
        ("negative count", {"schema_version": 1, "forwarded_calls": {SETVIEWPORT: -1}}),
    ):
        bad = write_doc(doc)
        check(_raises(lambda bad=bad: load_dynamic(bad)), f"dynamic: {label} raises CensusError")
        os.unlink(bad)
    check(_raises(lambda: load_dynamic("/no/such/sidecar.json")),
          "dynamic: a missing sidecar file raises (fail-closed, never empty pass)")


def test_dynamic_cli():
    safe = write_doc(full_doc())
    check(cli.main(["--dynamic", safe]) == 0, "cli: SAFE sidecar → exit 0")
    os.unlink(safe)
    viol = write_doc(full_doc({SETVIEWPORT: 4}))
    check(cli.main(["--dynamic", viol]) == 1, "cli: VIOLATION sidecar → exit 1")
    os.unlink(viol)
    inc = full_doc()
    del inc["forwarded_calls"][SETVIEWPORT]
    p = write_doc(inc)
    check(cli.main(["--dynamic", p]) == 2, "cli: INCONCLUSIVE sidecar → exit 2")
    os.unlink(p)
    check(cli.main(["--dynamic", "/no/such.json"]) == 2, "cli: unloadable sidecar → exit 2")


def test_dynamic_text():
    txt = render_dynamic_text(build_dynamic_report(CENSUS, full_calls({SETVIEWPORT: 7})))
    check("VIOLATION" in txt and "SetViewport" in txt and "fixed_function_state" in txt,
          "dynamic text: a violation names the method + subgroup")
    safe = render_dynamic_text(build_dynamic_report(CENSUS, full_calls()))
    check("SAFE" in safe and "COMPLETE" in safe, "dynamic text: SAFE says capture complete")


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
    test_dynamic_key_coverage()
    test_dynamic_safe()
    test_dynamic_violation()
    test_dynamic_query_only_ignored()
    test_dynamic_shader_asymmetry()
    test_dynamic_missing_risk_inconclusive()
    test_dynamic_unknown_key_inconclusive()
    test_dynamic_violation_precedence()
    test_dynamic_load_and_malformed()
    test_dynamic_cli()
    test_dynamic_text()

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
