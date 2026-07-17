#!/usr/bin/env python3
"""tools/parity/state.py — EP-04 `state` (volatile) pillar adapter.

Consumes a normalized `state-metrics.json` (state_producer.from_view_json over a
Trace Studio v3 `--state` window) and adjudicates the `state` pillar: PASSES iff
EVERY required paired frame's volatile-state Merkle root matches retail's. The
first frame whose root diverges FAILS, localized to the exact (subsystem, field)
leaf the producer drilled (ST-02).

Normalized state-metrics doc (schema_version = OBS_SCHEMA_VERSION):

    {
      "schema_version": 1,
      "pillar": "state",
      "state_schema_version": 1,
      "has_state": true,
      "source": {"port_container_sha256": ..., "retail_container_sha256": ...},
      "frames": [
        {"key": ["SAVE_PICKER_READY", 1, 3], "port_root": "…", "retail_root": "…",
         "identical": true},
        {"key": ["SAVE_PICKER_READY", 1, 4], "port_root": "…", "retail_root": "…",
         "identical": false,
         "divergence": {"path": "rng/rngcalls", "port_value": 119, "retail_value": 117,
                        "port_present": true, "retail_present": true}},
        ...
      ]
    }

Verdict map (roadmap §4.1, docs/reference/parity-vocabulary.md):
  * absent file / has_state false / a required frame uncovered → NOT_CAPTURED
  * every required frame identical                             → PASS
  * first non-identical frame                                  → FAIL (first_divergence = the leaf)
  * corrupt / wrong schema / foreign|reordered / bad shape     → INCONCLUSIVE
"""
from __future__ import annotations

from .fingerprint import FingerprintError
from .observations import (
    FAIL,
    NOT_CAPTURED,
    PASS,
    AdapterResult,
    ObservationError,
    artifact_ref,
    first_divergence,
    guard_empty_required,
    inconclusive,
    load_json,
    match_frames,
    not_captured,
    observation,
    pillar_result,
    require_metrics_schema,
    verify_source_containers,
)


def adapt_state(metrics_path, required, *, expected_containers=None) -> AdapterResult:
    """Adjudicate the `state` pillar from a normalized state-metrics doc. See the
    module docstring for the doc shape + verdict map. `required` is the ordered
    in-window paired-frame list; `expected_containers` opt-in provenance check."""
    try:
        if r := guard_empty_required(required, "state"):
            return r
        doc = load_json(metrics_path)
        if doc is None:
            return not_captured(f"no state metrics at {metrics_path}")
        require_metrics_schema(doc, "state")
        verify_source_containers(doc, expected_containers)

        art = [artifact_ref(metrics_path, "state_metrics")]
        if doc.get("has_state") is False:
            note = ("window captured without --state (no engine-state) — "
                    "re-drive orv3_window … --state")
            return AdapterResult(
                observation(captured=False, artifacts=art, note=note),
                pillar_result(NOT_CAPTURED, detail=note))

        reason, matched = match_frames(doc.get("frames") or [], required, "state")
        if reason:
            return AdapterResult(
                observation(captured=False, artifacts=art, note=reason),
                pillar_result(NOT_CAPTURED, detail=reason))

        for lf, m in matched:
            if "identical" not in m:
                note = f"frame {lf.label()} has no state comparison"
                return AdapterResult(
                    observation(captured=False, artifacts=art, note=note),
                    pillar_result(NOT_CAPTURED, detail=note))
            if not m["identical"]:
                dv = m.get("divergence") or {}
                path = dv.get("path", "?")
                fd = first_divergence(
                    lf, kind="state", path=dv.get("path"),
                    port_value=dv.get("port_value"), retail_value=dv.get("retail_value"))
                return AdapterResult(
                    observation(captured=True, artifacts=art,
                                note=f"first state divergence @ {lf.label()} ({path})"),
                    pillar_result(
                        FAIL, first_div=fd,
                        detail=f"volatile state diverges at {lf.label()} — {path} "
                               f"(retail={dv.get('retail_value')} port={dv.get('port_value')})"))

        detail = f"all {len(matched)} required frames' volatile state Merkle-identical"
        return AdapterResult(
            observation(captured=True, artifacts=art, note=detail),
            pillar_result(PASS, detail=detail))
    except (ObservationError, FingerprintError) as exc:
        return inconclusive(str(exc))
