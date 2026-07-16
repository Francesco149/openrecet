#!/usr/bin/env python3
"""tools/parity/render_program.py — EP-04 `render_program` pillar adapter.

Consumes a normalized draw-metrics doc (one draw verdict per identity-joined
frame) and adjudicates the `render_program` pillar: PASSES iff EVERY required
paired frame draws the same PROGRAM as retail (same textures + per-texture
triangle totals). A BATCHING difference (same materials, only the split-vs-batched
draw granularity differs) still PASSES with a note — the pixels are expected
identical — while a DIVERGENT frame (a one-sided texture, or a shared texture's
triangle totals differ) FAILS (roadmap §4.1; docs/reference/parity-vocabulary.md).

Normalized draw-metrics doc (schema_version = OBS_SCHEMA_VERSION):

    {
      "schema_version": 1,
      "pillar": "render_program",
      "source": {"port_container_sha256": ..., "retail_container_sha256": ...},
      "frames": [
        {"key": ["PAUSE_OPEN", 1, 123], "draw_verdict": "ALIGNED",
         "port_tris": 12, "retail_tris": 12, "divergent": []},
        {"key": ["PAUSE_OPEN", 1, 124], "draw_verdict": "DIVERGENT",
         "divergent": [{"tex": "0000...747d", "port_tris": 7, "retail_tris": 1,
                        "port_draws": 1, "retail_draws": 1}]},
        ...
      ]
    }

`from_view_json` bridges the real producer: a Trace Studio v3 `view.json` already
bakes `draw_verdict` + `divergent` per PAIRED frame (tools/trace_studio_v3/
orv3_draws.frame_draw_report), so a real capture converts to the doc above with no
new tooling.

Verdict map:
  * absent file / a required frame with no draw_verdict → NOT_CAPTURED
  * every required frame ALIGNED (or BATCHING)          → PASS (+note if any BATCHING)
  * first DIVERGENT frame                               → FAIL (first_divergence = the tex)
  * reordered / foreign / stale-source / unknown token  → INCONCLUSIVE
"""
from __future__ import annotations

from .fingerprint import FingerprintError
from .observations import (
    FAIL,
    NOT_CAPTURED,
    OBS_SCHEMA_VERSION,
    PASS,
    AdapterResult,
    LogicalFrame,
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

# A same-materials draw program either PASSES outright (ALIGNED) or PASSES with a
# batching note (BATCHING = same per-texture triangle totals, only granularity
# differs ⇒ pixels expected identical). DIVERGENT is the only real render-program
# disproof.
_PASS_VERDICTS = {"ALIGNED", "BATCHING"}
_ALL_VERDICTS = _PASS_VERDICTS | {"DIVERGENT"}


def adapt_render_program(metrics_path, required, *, expected_containers=None) -> AdapterResult:
    """Adjudicate the `render_program` pillar from a normalized draw-metrics doc.
    See module docstring for the doc shape + verdict map. `required` is the ordered
    in-window paired-frame list; `expected_containers` opt-in provenance check."""
    try:
        if r := guard_empty_required(required, "render_program"):
            return r
        doc = load_json(metrics_path)
        if doc is None:
            return not_captured(f"no draw metrics at {metrics_path}")
        require_metrics_schema(doc, "render_program")
        verify_source_containers(doc, expected_containers)

        art = [artifact_ref(metrics_path, "render_metrics")]
        reason, matched = match_frames(doc.get("frames") or [], required, "render_program")
        if reason:
            return AdapterResult(
                observation(captured=False, artifacts=art, note=reason),
                pillar_result(NOT_CAPTURED, detail=reason))

        n_batching = 0
        for lf, m in matched:
            verdict = m.get("draw_verdict")
            if verdict is None:
                note = f"frame {lf.label()} has no draw_verdict"
                return AdapterResult(
                    observation(captured=False, artifacts=art, note=note),
                    pillar_result(NOT_CAPTURED, detail=note))
            if verdict not in _ALL_VERDICTS:
                raise ObservationError(f"unknown draw_verdict {verdict!r} at {lf.label()}")
            if verdict == "DIVERGENT":
                div = m.get("divergent") or []
                d0 = div[0] if div else {}
                fd = first_divergence(
                    lf, kind="render_program", path=d0.get("tex"),
                    port_value=d0.get("port_tris", m.get("port_tris")),
                    retail_value=d0.get("retail_tris", m.get("retail_tris")))
                tex = d0.get("tex", "?")
                return AdapterResult(
                    observation(captured=True, artifacts=art,
                                note=f"first DIVERGENT draw @ {lf.label()} (tex {tex})"),
                    pillar_result(FAIL, first_div=fd,
                                  detail=f"draw program DIVERGENT at {lf.label()} (tex {tex})"))
            if verdict == "BATCHING":
                n_batching += 1

        detail = f"all {len(matched)} required frames draw-aligned"
        if n_batching:
            detail += (f"; {n_batching} BATCHING (same materials, "
                       f"batched-vs-split granularity — pixels expected equal)")
        return AdapterResult(
            observation(captured=True, artifacts=art, note=detail, n_batching=n_batching),
            pillar_result(PASS, detail=detail))
    except (ObservationError, FingerprintError) as exc:
        return inconclusive(str(exc))


def from_view_json(view_path, *, required=None, source: dict | None = None) -> dict:
    """Bridge a real Trace Studio v3 `view.json` into the normalized draw-metrics
    doc `adapt_render_program` consumes. Emits one frame per PAIRED row (a row with
    a non-null `draw_verdict`); gap rows are skipped — an unpaired frame is not
    render-comparable.

    A v3 window is often MULTI-ANCHOR (a whole guild/shop flow), so a raw bridge
    would carry frames outside any single contract's join window — which the
    adapter would (correctly) reject as foreign. Pass `required` (the contract's
    in-window paired frames, from observations.load_required) to SCOPE the doc to
    exactly those frames, IN required order, by keyed lookup — so the doc covers
    exactly what the adapter must compare (a required frame absent from the view is
    simply omitted ⇒ the adapter reports NOT_CAPTURED, fail closed). Omit `required`
    to emit every paired frame (view order). `source` (opt-in) records the
    container hashes so the doc carries verifiable provenance."""
    v = load_json(view_path)
    if v is None:
        raise ObservationError(f"no view.json at {view_path}")
    paired: dict = {}
    order = []
    for fr in v.get("frames") or []:
        dv = fr.get("draw_verdict")
        if dv is None:
            continue
        lf = LogicalFrame.from_label(fr["label"])
        paired[lf] = {
            "key": list(lf),
            "draw_verdict": dv,
            "port_tris": fr.get("port_tris"),
            "retail_tris": fr.get("retail_tris"),
            "divergent": fr.get("divergent") or [],
        }
        order.append(lf)
    if required is None:
        frames = [paired[lf] for lf in order]
    else:
        frames = [paired[lf] for lf in required if lf in paired]
    return {
        "schema_version": OBS_SCHEMA_VERSION,
        "pillar": "render_program",
        "source": source or {},
        "frames": frames,
    }
