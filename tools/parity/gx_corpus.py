#!/usr/bin/env python3
"""GX-06 graphics-capture regression corpus — coverage gate core (roadmap
parity-evidence-roadmap.md §9 GX-06).

A `pixels` / `render_program` PASS is only trustworthy if the record→replay path for every
render-affecting D3D8 opcode is itself proven. This gate proves it two ways per opcode:

  • a FIXTURE — a synthetic standalone D3D8 exe that exercises the opcode + replays BIT-EXACT
    (gx0{4,5,6_*}, verified live by test_gx0*_fixture.py); proves the plumbing in ISOLATION.
  • a REAL PROOF — a real cached scenario whose captured container CONTAINS the opcode and
    passes v3verify bit-exact (title 2D / HOUSE 3D / pause RT); proves it IN SITU.

Coverage unit = the container OPCODE (orv3.OPNAME), tied to the census recorded method(s) it
captures via the manifest's opcode_methods (drift-guarded against the census). Fail-closed:

  • EVERY recorded opcode needs a fixture (else its replay path is entirely unexercised);
  • every OBSERVED opcode (present in ≥1 bit-exact real proof) needs a real proof too;
  • a supported-but-UNOBSERVED opcode (0 occurrences in every cached scene) is fixture-only,
    recorded honestly — no real proof is required because nothing emits it.

This CORE is the FAST gate: it reasons over the committed manifest attestations + the census,
touching no caches and no replay.exe, so it runs in the host suite on any checkout. The
`--verify` mode (tools/gx_corpus.py, drive-capable) re-parses each real-proof container +
re-runs v3verify + re-runs each fixture and re-STAMPS the manifest, so an attestation cannot
silently rot."""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = ROOT / "docs/parity-graphics-corpus.json"
DEFAULT_CENSUS = ROOT / "docs/schemas/d3d8-method-census-v1.json"
MANIFEST_SCHEMA_VERSION = 1

SURF_KINDS = ("NULL", "BACKBUFFER", "DEPTH", "TEX")


def _opcode_universe() -> set[str]:
    """The recorded render-affecting opcodes = every orv3 opcode name except EOF (the
    container only ever stores recorded calls; EOF is the terminator). Imported from the
    canonical parser so a new opcode (GX-02) auto-enters the universe and the gate flags it
    uncovered until the corpus adds a fixture/proof."""
    import sys
    v3 = str(ROOT / "tools" / "trace_studio_v3")
    if v3 not in sys.path:
        sys.path.insert(0, v3)
    import orv3
    return {name for op, name in orv3.OPNAME.items() if op != orv3.EOF}


def load_manifest(path: str | Path = DEFAULT_MANIFEST) -> dict:
    doc = json.loads(Path(path).read_text())
    if doc.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        raise ValueError(f"corpus manifest schema_version {doc.get('schema_version')!r} "
                         f"!= {MANIFEST_SCHEMA_VERSION}")
    for key in ("opcode_methods", "fixtures", "real_proofs"):
        if key not in doc:
            raise ValueError(f"corpus manifest missing required key {key!r}")
    return doc


def recorded_methods(census: dict) -> set[str]:
    """The census's RECORDED methods as 'Interface.Method' strings — the ground-truth set
    the corpus must fully capture (every one via ≥1 opcode or SURFREF kind)."""
    out: set[str] = set()
    for iface, groups in census.get("interfaces", {}).items():
        for m in groups.get("recorded", []):
            out.add(f"{iface}.{m}")
    return out


def build_report(manifest: dict, census: dict) -> dict:
    """Pure coverage adjudication: the opcode×{fixture,real-proof} matrix + fail-closed gaps
    + opcode↔census drift. No I/O beyond the two dict inputs."""
    universe = _opcode_universe()
    rec_methods = recorded_methods(census)
    opcode_methods: dict[str, list] = manifest["opcode_methods"]
    surfref_methods: dict[str, list] = manifest.get("surfref_methods", {})

    # ── per-opcode / per-surfref coverage from the fixtures + real proofs ──
    fixture_ops: dict[str, list[str]] = {}
    fixture_surf: dict[str, list[str]] = {}
    for fx in manifest["fixtures"]:
        for op in fx.get("opcodes", []):
            fixture_ops.setdefault(op, []).append(fx["name"])
        for k in fx.get("surfrefs", []):
            fixture_surf.setdefault(k, []).append(fx["name"])

    observed_ops: dict[str, list[str]] = {}   # any real proof (verified or not)
    proof_ops: dict[str, list[str]] = {}      # only bit-exact (REPLAY_EXACT) proofs count
    observed_surf: dict[str, list[str]] = {}
    proof_surf: dict[str, list[str]] = {}
    for rp in manifest["real_proofs"]:
        verified = rp.get("verify") == "REPLAY_EXACT"
        for op in rp.get("opcodes", []):
            observed_ops.setdefault(op, []).append(rp["scenario"])
            if verified:
                proof_ops.setdefault(op, []).append(rp["scenario"])
        for k in rp.get("surfrefs", []):
            observed_surf.setdefault(k, []).append(rp["scenario"])
            if verified:
                proof_surf.setdefault(k, []).append(rp["scenario"])

    observed = set(observed_ops)
    proven = set(proof_ops)
    unobserved = universe - observed

    gaps: list[dict] = []
    drift: list[dict] = []

    # ── drift: the opcode↔census map must stay total in both directions ──
    for op in sorted(universe):
        if op not in opcode_methods:
            drift.append({"kind": "opcode_unmapped", "opcode": op,
                          "detail": "recorded opcode absent from opcode_methods"})
    for op in sorted(opcode_methods):
        if op not in universe:
            drift.append({"kind": "map_stale", "opcode": op,
                          "detail": "opcode_methods names an opcode not in orv3.OPNAME"})
    mapped_methods: set[str] = set()
    for ms in opcode_methods.values():
        mapped_methods |= set(ms)
    for ms in surfref_methods.values():
        if isinstance(ms, list):
            mapped_methods |= set(ms)
    for m in sorted(rec_methods - mapped_methods):
        drift.append({"kind": "method_uncaptured", "method": m,
                      "detail": "census-recorded method captured by no opcode / SURFREF kind"})
    for m in sorted(mapped_methods - rec_methods):
        drift.append({"kind": "method_not_recorded", "method": m,
                      "detail": "opcode_methods/surfref_methods names a method not RECORDED in the census"})

    # ── coverage gaps (fail-closed) ──
    for op in sorted(universe):
        if op not in fixture_ops:
            gaps.append({"kind": "no_fixture", "opcode": op,
                         "detail": "recorded opcode has no fixture (its replay path is unexercised)"})
        if op in observed and op not in proven:
            gaps.append({"kind": "observed_unproven", "opcode": op,
                         "detail": f"observed in {observed_ops[op]} but no bit-exact real proof"})
    for k in SURF_KINDS:
        if k not in fixture_surf:
            gaps.append({"kind": "no_fixture_surfref", "surfref": k,
                         "detail": "SURFREF kind has no fixture"})
        if k in observed_surf and k not in proof_surf:
            gaps.append({"kind": "observed_unproven_surfref", "surfref": k,
                         "detail": f"observed in {observed_surf[k]} but no bit-exact real proof"})

    matrix = {op: {
        "methods": opcode_methods.get(op, []),
        "fixtures": fixture_ops.get(op, []),
        "real_proofs": proof_ops.get(op, []),
        "observed": op in observed,
    } for op in sorted(universe)}
    surf_matrix = {k: {
        "fixtures": fixture_surf.get(k, []),
        "real_proofs": proof_surf.get(k, []),
        "observed": k in observed_surf,
    } for k in SURF_KINDS}

    return {
        "verdict": "COMPLETE" if not gaps and not drift else "GAPS",
        "n_opcodes": len(universe),
        "n_observed": len(observed),
        "n_unobserved": len(unobserved),
        "observed": sorted(observed),
        "unobserved": sorted(unobserved),
        "matrix": matrix,
        "surfref_matrix": surf_matrix,
        "gaps": gaps,
        "drift": drift,
    }


def gate(report: dict) -> int:
    """Exit code: 0 iff the corpus is COMPLETE (every opcode fixture-covered, every observed
    opcode has a bit-exact real proof, no opcode↔census drift). Else 1."""
    return 0 if report["verdict"] == "COMPLETE" else 1


def format_report(report: dict) -> str:
    lines = []
    lines.append(f"GX-06 corpus: {report['verdict']}  "
                 f"({report['n_opcodes']} opcodes: {report['n_observed']} observed, "
                 f"{report['n_unobserved']} supported-unobserved)")
    lines.append("")
    lines.append(f"  {'opcode':24s} {'fix':>3s} {'proof':>5s}  observed  methods")
    for op, cell in report["matrix"].items():
        obs = "yes" if cell["observed"] else " no"
        lines.append(f"  {op:24s} {len(cell['fixtures']):>3d} {len(cell['real_proofs']):>5d}"
                     f"  {obs:>7s}   {', '.join(cell['methods'])}")
    lines.append("")
    lines.append("  SURFREF kinds:")
    for k, cell in report["surfref_matrix"].items():
        obs = "yes" if cell["observed"] else " no"
        lines.append(f"  {k:24s} {len(cell['fixtures']):>3d} {len(cell['real_proofs']):>5d}  {obs:>7s}")
    if report["unobserved"]:
        lines.append("")
        lines.append(f"  supported-but-unobserved (fixture-only): {', '.join(report['unobserved'])}")
    if report["drift"]:
        lines.append("")
        lines.append("  DRIFT:")
        for d in report["drift"]:
            lines.append(f"    ✗ {d['kind']}: {d.get('opcode') or d.get('method')} — {d['detail']}")
    if report["gaps"]:
        lines.append("")
        lines.append("  GAPS:")
        for g in report["gaps"]:
            lines.append(f"    ✗ {g['kind']}: {g.get('opcode') or g.get('surfref')} — {g['detail']}")
    return "\n".join(lines)
