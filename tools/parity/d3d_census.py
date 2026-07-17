#!/usr/bin/env python3
"""tools/parity/d3d_census.py — GX-00 D3D8 method census + drift guard.

The v3 capture proxy wraps IDirect3D8 + IDirect3DDevice8; each vtable slot is either
INTERCEPTED (a hand-written `my_<Iface>_<Name>` recording the call into the container)
or a generated PASS-THROUGH forwarder (`fwd_<Iface>_<Name>`). A `pixels`/`render_program`
pillar is only sound if EVERY render-affecting call the game makes was recorded — so a
render-affecting method that is silently FORWARDED (uncaptured) is a capture-completeness
hole this census makes explicit.

This module: (1) PARSES `proxy_generated.h`'s two vtable initializers → each method's
actual mode (recorded | forwarded); (2) loads the R3 census
(`docs/schemas/d3d8-method-census-v1.json`, each method's capture_class); (3) VERIFIES
the code against the census — every vtable method is classified exactly once, every
classified method exists, and each method's actual mode matches its class's expected mode
(`mode_of_class`). A mismatch is DRIFT: a method flipped recorded↔forwarded (e.g. GX-02
started recording a previously-`render_affecting_unsupported` one — reclassify to
`recorded`), or a d3d8.h/gen_forwarders change added/removed a slot. The drift guard
(`test_d3d_census.py`) fails on any of these, so the census can never silently rot.

The static verify/report above is pure + offline (reads the committed header + JSON; no
toolchain, no drive). The DYNAMIC census (below, `load_dynamic`/`build_dynamic_report`) —
which forwarded methods a capture ACTUALLY calls (0 observed ⇒ safe here, >0 ⇒ GX-01
record-or-fail) — consumes the proxy-emitted `v3cap.census.json` sidecar from a real drive.
"""
from __future__ import annotations

import json
import re
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_HEADER = ROOT / "tools/trace_studio_v3/proxy/proxy_generated.h"
DEFAULT_CENSUS = ROOT / "docs/schemas/d3d8-method-census-v1.json"

CENSUS_SCHEMA_VERSION = 1
RISK_CLASS = "render_affecting_unsupported"


class CensusError(Exception):
    """A malformed census JSON or an unparseable proxy header (CLI exit 2)."""


class Method(NamedTuple):
    iface: str
    name: str
    mode: str            # "recorded" (my_) | "forwarded" (fwd_)
    capture_class: str   # from the census, or "(unclassified)"


def parse_vtable(header_text: str, iface: str) -> list[tuple[str, str]]:
    """Parse `g_<iface>_vt = { … }` → ordered [(method, mode)] where mode is
    'recorded' (a my_ slot) or 'forwarded' (a fwd_ slot). Raises if the vtable
    initializer is absent (the header changed shape)."""
    m = re.search(r"g_" + re.escape(iface) + r"_vt\s*=\s*\{(.*?)\};", header_text, re.S)
    if not m:
        raise CensusError(f"vtable initializer g_{iface}_vt not found in the proxy header")
    out: list[tuple[str, str]] = []
    for kind, name in re.findall(r"(my|fwd)_" + re.escape(iface) + r"_(\w+)", m.group(1)):
        out.append((name, "recorded" if kind == "my" else "forwarded"))
    if not out:
        raise CensusError(f"no vtable slots parsed for {iface}")
    return out


def load_census(path=DEFAULT_CENSUS) -> dict:
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise CensusError(f"cannot load census {path}: {exc}") from exc
    if doc.get("schema_version") != CENSUS_SCHEMA_VERSION:
        raise CensusError(
            f"census schema_version {doc.get('schema_version')!r} != {CENSUS_SCHEMA_VERSION}")
    for key in ("capture_classes", "mode_of_class", "interfaces"):
        if key not in doc:
            raise CensusError(f"census missing required key {key!r}")
    return doc


def _classify(census: dict, iface: str) -> tuple[dict, list[str]]:
    """{method: capture_class} for an interface + the drift issues from the census
    side alone (a method listed under two classes, or a class the schema doesn't know)."""
    spec = census["interfaces"].get(iface, {})
    classes = census["capture_classes"]
    by_method: dict = {}
    issues: list[str] = []
    for cls, methods in spec.items():
        if cls not in classes:
            issues.append(f"{iface}: census uses unknown capture_class {cls!r}")
        for name in methods:
            if name in by_method:
                issues.append(f"{iface}.{name}: classified twice ({by_method[name]} + {cls})")
            by_method[name] = cls
    return by_method, issues


def verify(header_text: str, census: dict) -> tuple[bool, list[str], list[Method]]:
    """Cross-check the proxy vtables against the census. Returns (ok, drift_issues,
    methods). ok ⇔ no drift: every vtable method classified exactly once, every
    classified method present, and each actual mode == its class's expected mode."""
    mode_of_class = census["mode_of_class"]
    issues: list[str] = []
    methods: list[Method] = []
    for iface in census["interfaces"]:
        parsed = parse_vtable(header_text, iface)
        by_method, cissues = _classify(census, iface)
        issues.extend(cissues)
        parsed_names = {n for n, _ in parsed}
        for name, mode in parsed:
            cls = by_method.get(name)
            if cls is None:
                issues.append(f"{iface}.{name}: in the vtable but UNCLASSIFIED in the census")
                methods.append(Method(iface, name, mode, "(unclassified)"))
                continue
            want = mode_of_class.get(cls)
            if want is not None and want != mode:
                issues.append(
                    f"{iface}.{name}: class {cls!r} expects mode {want!r} but the proxy has "
                    f"{mode!r} — DRIFT (a method flipped recorded↔forwarded; reclassify)")
            methods.append(Method(iface, name, mode, cls))
        for name in by_method:
            if name not in parsed_names:
                issues.append(
                    f"{iface}.{name}: classified in the census but ABSENT from the vtable "
                    f"(stale — a d3d8.h/gen_forwarders change removed it)")
    return (not issues), issues, methods


def build_report(header_text: str, census: dict) -> dict:
    """The census REPORT: per-interface per-class counts, the render-affecting-unsupported
    RISK list (with its subgroups), and the drift issues. `ok` ⇔ census matches the proxy."""
    ok, issues, methods = verify(header_text, census)
    per_iface: dict = {}
    for m in methods:
        d = per_iface.setdefault(m.iface, {"counts": {}, "recorded": [], "forwarded": [], "risk": []})
        d["counts"][m.capture_class] = d["counts"].get(m.capture_class, 0) + 1
        (d["recorded"] if m.mode == "recorded" else d["forwarded"]).append(m.name)
        if m.capture_class == RISK_CLASS:
            d["risk"].append(m.name)
    totals: dict = {}
    for d in per_iface.values():
        for cls, n in d["counts"].items():
            totals[cls] = totals.get(cls, 0) + n
    return {
        "ok": ok,
        "drift": issues,
        "n_methods": len(methods),
        "totals_by_class": totals,
        "n_risk": totals.get(RISK_CLASS, 0),
        "interfaces": per_iface,
        "risk_subgroups": census.get("risk_subgroups", {}),
        "leads": census.get("leads", {}),
    }


def render_text(report: dict) -> str:
    lines = [f"d3d-census: {'OK' if report['ok'] else 'DRIFT'}  "
             f"({report['n_methods']} methods; {report['n_risk']} render-affecting-unsupported)"]
    for cls, n in sorted(report["totals_by_class"].items(), key=lambda kv: -kv[1]):
        lines.append(f"  {n:3d}  {cls}")
    if report["n_risk"]:
        subs = {k: v for k, v in report["risk_subgroups"].items() if not k.startswith("_")}
        lines.append(f"  RISK (forwarded, render-affecting, uncaptured) — {report['n_risk']} methods:")
        for grp, ms in subs.items():
            lines.append(f"    {grp}: {', '.join(ms)}")
    if report["drift"]:
        lines.append("  DRIFT:")
        for d in report["drift"]:
            lines.append(f"    ✗ {d}")
    return "\n".join(lines)


def load_and_report(header_path=DEFAULT_HEADER, census_path=DEFAULT_CENSUS) -> dict:
    header = Path(header_path).read_text(encoding="utf-8")
    return build_report(header, load_census(census_path))


# ── GX-00 DYNAMIC census (which forwarded methods were actually CALLED) + the GX-01
# record-or-fail gate. The static census above says which methods CAN affect pixels
# but are forwarded-uncaptured (the RISK set); this consumes the proxy-emitted
# v3cap.census.json sidecar (per-forwarded-method call counts) to say which of them a
# scene ACTUALLY hit. 0 observed ⇒ safe to forward for this title/scene; >0 ⇒ an
# uncaptured render-affecting call ran, so a pixels/render_program PASS over the scene
# is unsound (GX-01: record-or-fail). ────────────────────────────────────────────────

DYNAMIC_SCHEMA_VERSION = 1


def dynamic_from_doc(doc: dict, *, where: str = "census sidecar") -> dict:
    """Validate an already-parsed `v3cap.census.json` DICT → {"Iface.Name": count}.
    The shared core of `load_dynamic` (reads a file) and `capture_completeness` (given
    the sidecar orv3_view baked into view.json). Fail-closed: a wrong schema_version,
    a missing `forwarded_calls`, or a bad count raises CensusError — never a silent
    empty pass."""
    if doc.get("schema_version") != DYNAMIC_SCHEMA_VERSION:
        raise CensusError(
            f"{where} schema_version {doc.get('schema_version')!r} != {DYNAMIC_SCHEMA_VERSION}")
    calls = doc.get("forwarded_calls")
    if not isinstance(calls, dict):
        raise CensusError(f"{where} missing a 'forwarded_calls' object")
    out: dict = {}
    for key, val in calls.items():
        if isinstance(val, bool) or not isinstance(val, int) or val < 0:
            raise CensusError(f"{where} count for {key!r} is not a non-negative int: {val!r}")
        out[key] = val
    return out


def load_dynamic(path) -> dict:
    """Load a proxy `v3cap.census.json` sidecar → {"Iface.Name": count}. The proxy
    (gen_forwarders.emit_census_preamble) InterlockedIncrements one slot per FORWARDED
    method on every call, process-lifetime, and rewrites the sidecar at each kept
    frame. Fail-closed: a missing/malformed sidecar or a bad count raises CensusError
    (CLI exit 2), never a silent empty pass."""
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise CensusError(f"cannot load census sidecar {path}: {exc}") from exc
    return dynamic_from_doc(doc, where=f"census sidecar {path}")


def _risk_index(census: dict) -> tuple[dict, dict]:
    """({"Iface.Name": name}, {"Iface.Name": subgroup}) over the render_affecting_
    unsupported set. Subgroup from `risk_subgroups` (device-only), else '(ungrouped)'."""
    subs = {k: v for k, v in census.get("risk_subgroups", {}).items() if not k.startswith("_")}
    name_to_sub = {name: grp for grp, names in subs.items() for name in names}
    risk: dict = {}
    subgroup_of: dict = {}
    for iface, spec in census["interfaces"].items():
        for name in spec.get(RISK_CLASS, []):
            key = f"{iface}.{name}"
            risk[key] = name
            subgroup_of[key] = name_to_sub.get(name, "(ungrouped)")
    return risk, subgroup_of


def _forwarded_keys(census: dict) -> set:
    """Every method the proxy FORWARDS (query_only + forwarded_irrelevant + risk) as
    'Iface.Name' — the exact set the sidecar should carry (recorded/wrapper_lifetime
    are intercepted, not forwarded, so never appear)."""
    mode_of_class = census["mode_of_class"]
    keys: set = set()
    for iface, spec in census["interfaces"].items():
        for cls, names in spec.items():
            if mode_of_class.get(cls) == "forwarded":
                for name in names:
                    keys.add(f"{iface}.{name}")
    return keys


def build_dynamic_report(census: dict, dynamic: dict) -> dict:
    """GX-00 dynamic census + GX-01 gate: cross-reference observed forwarded-call
    counts against the render-affecting-unsupported RISK set.

      SAFE         — every risk method is present in the sidecar and 0-observed: the
                     capture is COMPLETE for this scene (safe to forward these here).
      VIOLATION    — a risk method fired (count>0): an uncaptured render-affecting call
                     ran, so a pixels/render_program PASS over this scene is UNSOUND
                     (GX-01 record-or-fail). CLI exit 1.
      INCONCLUSIVE — a risk method is ABSENT from the sidecar, or the sidecar carries a
                     forwarded method the census doesn't classify (drift): cannot prove
                     SAFE. Fail-closed (CLI exit 2).

    `observed_forwarded` (all forwarded methods with count>0, incl. query_only) is an
    informational call profile; only the RISK subset drives the verdict."""
    risk, subgroup_of = _risk_index(census)
    fwd_keys = _forwarded_keys(census)

    observed_risk = {k: dynamic[k] for k in risk if dynamic.get(k, 0)}
    missing_risk = sorted(k for k in risk if k not in dynamic)
    unknown_keys = sorted(k for k in dynamic if k not in fwd_keys)
    observed_forwarded = {k: v for k, v in sorted(dynamic.items()) if v}

    if observed_risk:
        verdict = "VIOLATION"
    elif missing_risk or unknown_keys:
        verdict = "INCONCLUSIVE"
    else:
        verdict = "SAFE"

    pixel_shader = "IDirect3DDevice8.SetPixelShader"
    return {
        "verdict": verdict,
        "n_risk": len(risk),
        "n_forwarded_expected": len(fwd_keys),
        "n_forwarded_in_sidecar": len(dynamic),
        "observed_risk": observed_risk,
        "observed_risk_subgroups": {k: subgroup_of[k] for k in observed_risk},
        "n_safe_risk": sum(1 for k in risk if k in dynamic and not dynamic[k]),
        "missing_risk": missing_risk,
        "unknown_keys": unknown_keys,
        "observed_forwarded": observed_forwarded,
        "leads": {"SetPixelShader": dynamic.get(pixel_shader, "absent")},
    }


def render_dynamic_text(report: dict) -> str:
    v = report["verdict"]
    lines = [f"d3d-census DYNAMIC: {v}  "
             f"({report['n_forwarded_in_sidecar']}/{report['n_forwarded_expected']} forwarded methods "
             f"in sidecar; {report['n_risk']} risk, {report['n_safe_risk']} 0-observed)"]
    if report["observed_risk"]:
        lines.append("  ✗ render-affecting-unsupported methods CALLED — capture INCOMPLETE (GX-01):")
        for k, n in sorted(report["observed_risk"].items(), key=lambda kv: -kv[1]):
            lines.append(f"    {n:>10}×  {k}  [{report['observed_risk_subgroups'][k]}]")
    if report["missing_risk"]:
        shown = ", ".join(report["missing_risk"][:8])
        more = " …" if len(report["missing_risk"]) > 8 else ""
        lines.append(f"  ? {len(report['missing_risk'])} risk methods ABSENT from the sidecar — cannot prove SAFE:")
        lines.append(f"    {shown}{more}")
    if report["unknown_keys"]:
        lines.append(f"  ? {len(report['unknown_keys'])} sidecar methods not in the census (drift):")
        lines.append(f"    {', '.join(report['unknown_keys'][:8])}")
    lead = report["leads"]["SetPixelShader"]
    lines.append(f"  lead SetPixelShader (vs RECORDED SetVertexShader): {lead}")
    if v == "SAFE":
        lines.append("  ⇒ every uncaptured render-affecting method was 0-observed: the "
                     "pixels/render_program capture is COMPLETE for this scene.")
    return "\n".join(lines)


def load_and_report_dynamic(sidecar_path, census_path=DEFAULT_CENSUS) -> dict:
    return build_dynamic_report(load_census(census_path), load_dynamic(sidecar_path))


# ── GX-01-full: the record-or-fail PRECONDITION for the pixels / render_program
# pillars. Those two replay the captured D3D8 command stream, so they are only SOUND
# if the capture was COMPLETE — every render-affecting forwarded method 0-observed —
# on BOTH sides. This lifts the per-side dynamic gate above into the bilateral policy
# `parity_prove` wires as a hard precondition: not-SAFE (either side) ⇒ the render
# evidence is untrustworthy ⇒ INCONCLUSIVE, never a false PASS/FAIL. (identity/state/
# save do NOT read the D3D stream, so they are not gated.) ──────────────────────────

class CaptureCompleteness(NamedTuple):
    sound: bool     # both sides present AND SAFE ⇒ pixels/render_program evidence trustworthy
    reason: str     # portable one-line reason (method names + counts, never a path)
    sides: dict     # {"port": {...per-side record...}, "retail": {...}} for the bundle


def _side_completeness(census: dict, sidecar) -> dict:
    """Per-side capture-completeness record from a RAW baked sidecar dict (or None =
    the view predates the GX-01 census bake). Verdict ∈ SAFE|VIOLATION|INCONCLUSIVE|
    ABSENT; a malformed sidecar is fail-closed to INCONCLUSIVE (never trusted)."""
    if sidecar is None:
        return {"verdict": "ABSENT"}
    try:
        dyn = dynamic_from_doc(sidecar, where="baked census")
    except CensusError as exc:
        return {"verdict": "INCONCLUSIVE", "malformed": str(exc)}
    rep = build_dynamic_report(census, dyn)
    rec = {"verdict": rep["verdict"], "n_risk": rep["n_risk"], "n_safe_risk": rep["n_safe_risk"]}
    if rep["observed_risk"]:
        rec["observed_risk"] = rep["observed_risk"]
        rec["observed_risk_subgroups"] = rep["observed_risk_subgroups"]
    if rep["missing_risk"]:
        rec["missing_risk"] = rep["missing_risk"]
    if rep["unknown_keys"]:
        rec["unknown_keys"] = rep["unknown_keys"]
    return rec


def capture_completeness(census: dict, port_sidecar, retail_sidecar) -> CaptureCompleteness:
    """GX-01-full bilateral precondition. `port_sidecar`/`retail_sidecar` are each a
    raw `{schema_version, forwarded_calls}` dict (orv3_view bakes them into view.json)
    or None (absent). SOUND iff BOTH sides SAFE; else a portable reason names each
    unsound side (its verdict + the offending risk methods)."""
    sides = {"port": _side_completeness(census, port_sidecar),
             "retail": _side_completeness(census, retail_sidecar)}
    unsound = [(name, rec) for name, rec in sides.items() if rec["verdict"] != "SAFE"]
    if not unsound:
        return CaptureCompleteness(True, "capture complete both sides (census SAFE)", sides)

    parts = []
    for name, rec in unsound:
        v = rec["verdict"]
        if v == "VIOLATION":
            risk = ", ".join(f"{k}×{n}" for k, n in sorted(rec["observed_risk"].items()))
            parts.append(f"{name} VIOLATION — uncaptured render-affecting call(s) {risk}")
        elif v == "ABSENT":
            parts.append(f"{name} census ABSENT (view predates the GX-01 census bake — re-drive/re-bake)")
        elif "malformed" in rec:
            parts.append(f"{name} census malformed ({rec['malformed']})")
        else:  # INCONCLUSIVE: a risk method unobserved in the sidecar, or drift
            parts.append(f"{name} INCONCLUSIVE — census cannot prove SAFE (risk method absent/drift)")
    return CaptureCompleteness(False, "; ".join(parts), sides)
