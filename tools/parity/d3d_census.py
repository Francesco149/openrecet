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

Pure + offline (reads the committed header + JSON; no toolchain, no drive). The DYNAMIC
census — which methods are actually CALLED in a capture (0 observed ⇒ safe, >0 ⇒ must
record-or-fail) — is the GX-00 follow-up needing a proxy call-counter + a drive.
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
