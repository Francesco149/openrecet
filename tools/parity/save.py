#!/usr/bin/env python3
"""tools/parity/save.py — EP-04 `save` pillar adapter.

Consumes a normalized `save-metrics.json` (produced by save_producer.py from the
two save.dat files a `--target both` drive writes) and adjudicates the `save`
pillar: PASS iff the two ~18 MB save arenas are byte-identical.

Normalized save-metrics doc (schema_version = OBS_SCHEMA_VERSION):

    {
      "schema_version": 1,
      "pillar": "save",
      "arena_bytes": 18838832,
      "identical": false,
      "ndiff": 6836,
      "first_divergence": {          # null iff identical
        "byte_off": 2840, "scope": "bank", "bank": 0, "dword": 2,
        "region": "occupied_playtime", "class": "persistent",
        "path": "bank0/occupied_playtime",
        "port_byte": 0, "retail_byte": 24, "port_dword_hex": "...", ...
      },
      "region_summary": [ {"path","ndiff","first_byte_off", ...}, ... ],
      "source": {"port_save_sha256": "...", "retail_save_sha256": "..."}
    }

Unlike the per-frame pillars, the save is scenario-scoped — one artifact, not a
per-frame join. So its `first_divergence.logical_frame` is the contract's join
anchor at offset 0 (a NOMINAL frame: "the save this scenario committed"), and the
REAL locus lives in `path` (a state-tree region path, ST-00). `save` never binds
to the v3 view.json container hashes (those are D3D captures); its provenance is
the two save.dat SHA-256s it records + the proof's hashed input save/PEs.

Verdict map (roadmap §4.1, docs/reference/parity-vocabulary.md):
  * absent file                         → NOT_CAPTURED (fail closed)
  * identical == true                   → PASS
  * identical == false + first_divergence → FAIL (localized first_divergence)
  * corrupt / wrong schema / bad shape  → INCONCLUSIVE (ObservationError)
"""
from __future__ import annotations

from typing import Optional

from .fingerprint import FingerprintError
from .observations import (
    FAIL,
    PASS,
    AdapterResult,
    LogicalFrame,
    ObservationError,
    artifact_ref,
    first_divergence,
    inconclusive,
    load_json,
    not_captured,
    observation,
    pillar_result,
    require_metrics_schema,
)


def _verify_source_saves(doc: dict, expected: Optional[dict]) -> None:
    """Opt-in: when the caller knows the two save.dat hashes (e.g. from the drive
    it produced), the metrics' recorded `source` MUST match or the doc is a
    different/stale capture (INCONCLUSIVE). `expected=None` skips (the common
    case — the save has no external join to bind against)."""
    if not expected:
        return
    src = doc.get("source")
    if not isinstance(src, dict):
        raise ObservationError("save metrics omit 'source' hashes; cannot verify provenance")
    for role, exp in expected.items():
        if exp is None:
            continue
        got = src.get(role)
        if got is None:
            raise ObservationError(f"save metrics omit source.{role}; cannot verify provenance")
        if got != exp:
            raise ObservationError(
                f"save metrics source.{role} {got[:12]}… != drive {exp[:12]}… (stale/swapped)")


def adapt_save(metrics_path, *, nominal_frame: Optional[LogicalFrame] = None,
               expected_saves: Optional[dict] = None) -> AdapterResult:
    """Adjudicate the `save` pillar from a normalized save-metrics doc.

    `nominal_frame` is the contract's join anchor (a LogicalFrame) used as the
    scenario-scoped locus for a FAIL's first_divergence; the byte/region locus is
    carried in its `path`. `expected_saves` (opt-in) = {port_save_sha256,
    retail_save_sha256} to bind the doc to a specific drive."""
    try:
        doc = load_json(metrics_path)
        if doc is None:
            return not_captured(f"no save metrics at {metrics_path}")
        require_metrics_schema(doc, "save")
        _verify_source_saves(doc, expected_saves)

        art = [artifact_ref(metrics_path, "save_metrics")]
        arena = doc.get("arena_bytes")
        identical = doc.get("identical")
        ndiff = doc.get("ndiff")
        if not isinstance(identical, bool) or not isinstance(ndiff, int) or isinstance(ndiff, bool):
            raise ObservationError("save metrics lack a boolean 'identical' / int 'ndiff'")

        if identical:
            return AdapterResult(
                observation(captured=True, artifacts=art,
                            note=f"save arenas byte-identical ({arena} bytes)"),
                pillar_result(PASS, detail=f"both save.dat byte-identical ({arena} bytes)"))

        fd_raw = doc.get("first_divergence")
        if not isinstance(fd_raw, dict) or "byte_off" not in fd_raw:
            raise ObservationError(
                "save metrics say not-identical but carry no first_divergence")
        # nominal scenario frame for the schema-required logical_frame; the real
        # locus is the region path.
        nf = nominal_frame or LogicalFrame("SAVE_COMMIT", 1, 0)
        path = fd_raw.get("path") or f"byte{fd_raw['byte_off']}"
        fd = first_divergence(
            nf, kind="save", path=path,
            port_value={"byte": fd_raw.get("port_byte"), "dword": fd_raw.get("port_dword_hex")},
            retail_value={"byte": fd_raw.get("retail_byte"), "dword": fd_raw.get("retail_dword_hex")})
        nregions = len(doc.get("region_summary") or [])
        return AdapterResult(
            observation(captured=True, artifacts=art,
                        note=f"{ndiff} bytes differ; first @ {path}"),
            pillar_result(
                FAIL, first_div=fd,
                detail=f"{ndiff} save bytes differ across {nregions} region(s); "
                       f"first @ {path} (byte {fd_raw['byte_off']})"))
    except (ObservationError, FingerprintError) as exc:
        return inconclusive(str(exc))
