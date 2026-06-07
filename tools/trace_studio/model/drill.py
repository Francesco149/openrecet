"""model/drill.py — the OVERVIEW-index → dense sub-window mapping (one source of truth).

DRILL recaptures one sub-window of a coarse `{capstride}` overview at cadence 1 (dense).
A viewer index `at` in the overview maps to the anchor-relative frame
`caprange.start + at*stride` (the same math `model/timeline.py` documents as
`frames[0] + v*cadence`); `span` dense frames are taken from there, clamped to the
overview window end. Both the CLI (`cli.cmd_drill`) and the in-browser drill route
(`server/routes.h_drill`) call this so the index math lives in exactly one place.
"""
from __future__ import annotations


def drill_window(manifest: dict, session: str, at: int, span: int
                 ) -> tuple[str, int, int, str]:
    """Map an overview viewer index → (src_trace, start, span_clamped, default_child).

    - src_trace : the overview's WORKING trace (carries any applied pins), reset+
                  re-densified by the capture (--reset-trace strips the old
                  caprange/capstride); falls back to source_trace.
    - start     : caprange.start + at*stride   (anchor-relative dense window start)
    - span_clamped : min(start+span, caprange end) - start, ≥1
    - default_child : "<session>-drill-<start>-<span>"   (the child session name)

    Raises ValueError if the session has no working trace / caprange to drill.
    """
    src = manifest.get("working_trace") or manifest.get("source_trace")
    cr = manifest.get("caprange")
    if not src or not cr:
        raise ValueError("session has no working_trace/caprange to drill")
    stride = int(manifest.get("stride", 1) or 1)
    s0, c0 = int(cr[0]), int(cr[1])
    start = s0 + int(at) * stride
    end = min(start + int(span), s0 + c0)          # clamp to the overview window
    span_clamped = max(1, end - start)
    default_child = f"{session}-drill-{start}-{span_clamped}"
    return src, start, span_clamped, default_child
