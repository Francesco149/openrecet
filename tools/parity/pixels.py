#!/usr/bin/env python3
"""tools/parity/pixels.py — EP-04 `pixels` pillar adapter.

Consumes a normalized pixel-metrics doc (one differ-count per identity-joined
frame) and adjudicates the `pixels` pillar: exact mode PASSES iff EVERY required
paired frame is bit-identical (differ == 0).

Normalized pixel-metrics doc (schema_version = OBS_SCHEMA_VERSION):

    {
      "schema_version": 1,
      "pillar": "pixels",
      "mode": "exact",
      "source": {"port_container_sha256": "<64hex>",
                 "retail_container_sha256": "<64hex>"},
      "frames": [
        {"key": ["PAUSE_OPEN", 1, 123], "differ": 0, "total": 786432, "meanabs": 0.0},
        ...
      ]
    }

`differ` is the count of pixels that differ between the two identity-paired frames
(the metric tools/pixel_diff.amplified_diff already computes). A real headless
producer that replays the v3 command stream for each paired frame and emits this
doc is wired in a later package; the adapter is format-only so it is testable now
against fixtures and cannot be fooled by a stale/foreign capture (roadmap rule 11,
"build consumers before platforms").

Verdict map (roadmap §4.1, docs/reference/parity-vocabulary.md):
  * absent file / a required frame unmeasured → NOT_CAPTURED (fail closed)
  * every required frame differ == 0          → PASS
  * first frame with differ > 0               → FAIL (first_divergence)
  * reordered / foreign / stale-source        → INCONCLUSIVE (ObservationError)
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


def adapt_pixels(metrics_path, required, *, mode: str = "exact",
                 expected_containers=None) -> AdapterResult:
    """Adjudicate the `pixels` pillar from a normalized pixel-metrics doc.

    `required` is the ordered in-window paired-frame list (from
    observations.load_required). `expected_containers` (opt-in) is
    {port_container_sha256, retail_container_sha256} from the identity join — when
    given, the metrics' recorded source MUST match or the pillar is INCONCLUSIVE."""
    try:
        if r := guard_empty_required(required, "pixels"):
            return r
        doc = load_json(metrics_path)
        if doc is None:
            return not_captured(f"no pixel metrics at {metrics_path}")
        require_metrics_schema(doc, "pixels")
        if mode != "exact":
            raise ObservationError(f"unsupported pixel mode {mode!r} (only 'exact' in v1)")
        doc_mode = doc.get("mode", "exact")
        if doc_mode != mode:
            raise ObservationError(f"metrics mode {doc_mode!r} != contract mode {mode!r}")
        verify_source_containers(doc, expected_containers)

        art = [artifact_ref(metrics_path, "pixel_metrics")]
        reason, matched = match_frames(doc.get("frames") or [], required, "pixels")
        if reason:
            return AdapterResult(
                observation(captured=False, artifacts=art, note=reason),
                pillar_result(NOT_CAPTURED, detail=reason))

        for lf, m in matched:
            differ = m.get("differ")
            if differ is None:
                note = f"frame {lf.label()} has no 'differ' metric"
                return AdapterResult(
                    observation(captured=False, artifacts=art, note=note),
                    pillar_result(NOT_CAPTURED, detail=note))
            if not isinstance(differ, int) or isinstance(differ, bool) or differ < 0:
                raise ObservationError(f"frame {lf.label()} 'differ' must be a non-negative int: {differ!r}")
            if differ != 0:
                fd = first_divergence(
                    lf, kind="pixels",
                    port_value={"differ": differ, "meanabs": m.get("meanabs"), "total": m.get("total")},
                    retail_value={"differ": 0})
                return AdapterResult(
                    observation(captured=True, artifacts=art,
                                note=f"first pixel diff @ {lf.label()} ({differ} px)"),
                    pillar_result(FAIL, first_div=fd,
                                  detail=f"{differ} px differ at {lf.label()} (mode {mode})"))

        return AdapterResult(
            observation(captured=True, artifacts=art,
                        note=f"{len(matched)} frames pixel-identical"),
            pillar_result(PASS,
                          detail=f"all {len(matched)} required frames pixel-identical (mode {mode})"))
    except (ObservationError, FingerprintError) as exc:
        return inconclusive(str(exc))
