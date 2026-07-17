#!/usr/bin/env python3
"""tools/test_d3d_census.py — GX-00 drift guard for the D3D8 method census.

Asserts the committed `proxy_generated.h` matches the R3 census
`d3d8-method-census-v1.json`: every vtable method classified exactly once, every
classified method present, and each actual mode == its class's expected mode. Any drift
(a method flipped recorded↔forwarded, added, removed, or misclassified by a
d3d8.h/gen_forwarders change) fails here — so the census can never silently rot and a
newly-forwarded render-affecting method can't hide.

  * CONSISTENT — the shipped proxy/census pair verifies clean (ok, no drift).
  * COMPLETE — all 141 vtable methods (D3D8 16 + Device 97 + GX-04 VB 14 + IB 14) are
    covered; the census/mode maps are total; the RISK list carries every roadmap high-risk
    method (minus CreateVertexBuffer/CreateIndexBuffer, now recorded via the GX-04 wrapper).
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
    capture_completeness,
    dynamic_from_doc,
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
    # 141 = IDirect3D8 16 + IDirect3DDevice8 97 + (GX-04) VB 14 + IB 14
    check(len(methods) == 141, f"complete: 141 vtable methods covered (got {len(methods)})")


def test_counts():
    rep = build_report(HEADER, CENSUS)
    check(rep["ok"], "counts: report ok")
    # D3D8: 4 my_ (3 wrapper + CreateDevice) / 12 fwd_; Device: 27 my_ (3 wrapper + 24, incl.
    # GX-04 CreateVertexBuffer/CreateIndexBuffer) / 70 fwd_
    d3d8 = parse_vtable(HEADER, "IDirect3D8")
    dev = parse_vtable(HEADER, "IDirect3DDevice8")
    check(sum(1 for _, m in d3d8 if m == "recorded") == 4, "counts: IDirect3D8 4 recorded")
    check(sum(1 for _, m in dev if m == "recorded") == 27, "counts: IDirect3DDevice8 27 recorded")
    check(sum(1 for _, m in dev if m == "forwarded") == 70, "counts: IDirect3DDevice8 70 forwarded")
    # the render-affecting-unsupported RISK set (fail-closed): 31 device methods (was 33 —
    # GX-04 moved CreateVertexBuffer/CreateIndexBuffer to recorded)
    check(rep["n_risk"] == 31, f"counts: 31 render-affecting-unsupported (got {rep['n_risk']})")
    check(rep["interfaces"]["IDirect3D8"]["risk"] == [], "counts: IDirect3D8 has no render risk")


def test_counts_buffers():
    # GX-04: the two wrapped buffer interfaces. Each is 14 methods = 5 my_ (QI/AddRef/Release
    # + Lock/Unlock) / 9 fwd_; Lock/Unlock are RECORDED (the sole VB/IB content writers), none
    # render_affecting_unsupported (a VB/IB has no uncaptured render-affecting path once wrapped).
    rep = build_report(HEADER, CENSUS)
    for iface in ("IDirect3DVertexBuffer8", "IDirect3DIndexBuffer8"):
        vt = parse_vtable(HEADER, iface)
        check(len(vt) == 14, f"buffers: {iface} 14 methods (got {len(vt)})")
        check(sum(1 for _, m in vt if m == "recorded") == 5, f"buffers: {iface} 5 recorded (QI/AddRef/Release/Lock/Unlock)")
        by = {n: m for n, m in vt}
        check(by.get("Lock") == "recorded" and by.get("Unlock") == "recorded",
              f"buffers: {iface} Lock/Unlock RECORDED (intercepted for content versioning)")
        check(rep["interfaces"][iface]["risk"] == [], f"buffers: {iface} has no render risk")
    # CreateVertexBuffer/CreateIndexBuffer are now RECORDED, out of the risk set
    devrisk = set(rep["interfaces"]["IDirect3DDevice8"]["risk"])
    check("CreateVertexBuffer" not in devrisk and "CreateIndexBuffer" not in devrisk,
          "buffers: CreateVertexBuffer/CreateIndexBuffer reclassified recorded (out of risk)")


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
    # 100 = 84 device/factory forwards + (GX-04) 9 VB + 9 IB buffer forwards; 31 risk (was 33)
    check(len(FWD_KEYS) == 100, f"dynamic: 100 forwarded keys derived from census (got {len(FWD_KEYS)})")
    check(len(RISK) == 31, f"dynamic: 31 risk keys derived from census (got {len(RISK)})")
    check(SETVIEWPORT in RISK and SETPIXELSHADER in RISK, "dynamic: known risk keys present")
    check(GETRENDERSTATE in FWD_KEYS and GETRENDERSTATE not in RISK,
          "dynamic: GetRenderState is forwarded query_only, not a risk method")


def test_dynamic_safe():
    rep = build_dynamic_report(CENSUS, full_calls())
    check(rep["verdict"] == "SAFE", "dynamic: all-zero forwarded → SAFE")
    check(rep["n_risk"] == 31 and rep["n_safe_risk"] == 31, "dynamic: all 31 risk methods 0-observed")
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


# ── GX-01-full: capture_completeness bilateral precondition ──────────────────

def test_completeness_both_safe():
    cc = capture_completeness(CENSUS, full_doc(), full_doc())
    check(cc.sound, "completeness: both sides SAFE → sound")
    check(cc.sides["port"]["verdict"] == "SAFE" and cc.sides["retail"]["verdict"] == "SAFE",
          "completeness: both per-side verdicts SAFE")
    check("SAFE" in cc.reason, "completeness: sound reason mentions SAFE")


def test_completeness_port_violation():
    # The GX-01 acceptance negative: a deliberate SetViewport on ONE side is NOT sound.
    cc = capture_completeness(CENSUS, full_doc({SETVIEWPORT: 3}), full_doc())
    check(not cc.sound, "completeness: a SetViewport VIOLATION on port → NOT sound")
    check(cc.sides["port"]["verdict"] == "VIOLATION", "completeness: port verdict VIOLATION")
    check(SETVIEWPORT in cc.sides["port"]["observed_risk"], "completeness: names the risk method")
    check("port VIOLATION" in cc.reason and "SetViewport" in cc.reason,
          "completeness: reason names the side + method")
    check(cc.sides["retail"]["verdict"] == "SAFE", "completeness: the clean side stays SAFE")


def test_completeness_retail_violation():
    cc = capture_completeness(CENSUS, full_doc(), full_doc({SETVIEWPORT: 1}))
    check(not cc.sound and cc.sides["retail"]["verdict"] == "VIOLATION",
          "completeness: a VIOLATION on retail → NOT sound (bilateral)")


def test_completeness_absent():
    # A view predating the census bake ⇒ that side ABSENT ⇒ NOT sound (fail-closed).
    cc = capture_completeness(CENSUS, None, full_doc())
    check(not cc.sound, "completeness: an ABSENT census → NOT sound")
    check(cc.sides["port"]["verdict"] == "ABSENT", "completeness: absent side → ABSENT verdict")
    check("ABSENT" in cc.reason and "re-drive" in cc.reason,
          "completeness: absent reason points at a re-drive/re-bake")
    both_absent = capture_completeness(CENSUS, None, None)
    check(not both_absent.sound, "completeness: both absent → NOT sound")


def test_completeness_inconclusive():
    # A sidecar missing a risk key (drift/incomplete) can't prove SAFE → not sound.
    doc = full_doc()
    del doc["forwarded_calls"][SETVIEWPORT]
    cc = capture_completeness(CENSUS, doc, full_doc())
    check(not cc.sound and cc.sides["port"]["verdict"] == "INCONCLUSIVE",
          "completeness: a risk method absent from the sidecar → INCONCLUSIVE, NOT sound")


def test_completeness_malformed():
    # A malformed baked sidecar is fail-closed to INCONCLUSIVE, never silently trusted.
    cc = capture_completeness(CENSUS, {"schema_version": 999}, full_doc())
    check(not cc.sound and cc.sides["port"]["verdict"] == "INCONCLUSIVE",
          "completeness: a malformed sidecar → INCONCLUSIVE (fail-closed)")
    check("malformed" in cc.sides["port"], "completeness: records the malformed detail")


def test_dynamic_from_doc_shared():
    # dynamic_from_doc validates a parsed dict the way load_dynamic validates a file.
    check(dynamic_from_doc(full_doc())[SETVIEWPORT] == 0, "dynamic_from_doc: parses a good doc")
    check(_raises(lambda: dynamic_from_doc({"forwarded_calls": {}})),
          "dynamic_from_doc: wrong schema_version raises")
    check(_raises(lambda: dynamic_from_doc({"schema_version": DYNAMIC_SCHEMA_VERSION})),
          "dynamic_from_doc: missing forwarded_calls raises")


def main() -> int:
    test_consistent()
    test_counts()
    test_counts_buffers()
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
    test_completeness_both_safe()
    test_completeness_port_violation()
    test_completeness_retail_violation()
    test_completeness_absent()
    test_completeness_inconclusive()
    test_completeness_malformed()
    test_dynamic_from_doc_shared()

    if _failures:
        print(f"FAIL — {len(_failures)}/{_checks} checks failed:")
        for f in _failures:
            print(f"  ✗ {f}")
        return 1
    print(f"ok — {_checks} checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
