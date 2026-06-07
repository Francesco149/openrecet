"""model/timeline.py — build the v2 segmented timeline.

The v2 timeline is an ordered list of entries:
  - load_seam : a zero-frame seam reconstructed from a [LOADING_START, LOADING_END)
                span in the anchor stream. Under D1 capture-suppression a load
                captures ZERO frames, so it collapses to a seam; the entry records
                each side's tick count (port_ticks / retail_ticks) so the viewer can
                show the stretch (e.g. "port 6t / retail 2711t") without any frames.
  - gameplay  : a captured window. Phase 2 emits ONE gameplay entry for the capture
                (per-segment splitting + per-segment media is Phase 3); it carries
                the window frame range + the videos/verdict/state for that window.

Seams come purely from the anchor streams both sides already emit, so no engine
record is needed (Phase-1 "Foundation rule": the suppressed span is reconstructable
from the anchor stream). A side that never fired the LOADING pair (e.g. retail froze
in a New-Game prologue) yields ticks=None for that side — the divergence is visible,
not hidden.
"""
from __future__ import annotations


def _load_spans(firings: list[dict]) -> list[tuple[int | None, int | None]]:
    """Pair LOADING_START→LOADING_END firings into [(start, end), ...]. A dangling
    START (no matching END — a load that never finished on this side) yields
    (start, None); they stay in order so they pair positionally across sides."""
    spans: list[tuple[int | None, int | None]] = []
    start: int | None = None
    for f in firings:
        a = f.get("anchor")
        if a == "LOADING_START":
            if start is not None:                 # START with no END before it
                spans.append((start, None))
            start = f["frame"]
        elif a == "LOADING_END":
            spans.append((start, f["frame"]))
            start = None
    if start is not None:
        spans.append((start, None))
    return spans


def find_load_seams(port_firings: list[dict],
                    retail_firings: list[dict]) -> list[dict]:
    """Reconstruct load seams by positionally pairing each side's LOADING spans.
    Returns [{anchor:"LOADING_END", port_start, port_end, retail_start, retail_end,
              port_ticks, retail_ticks}]. ticks = end-start, or None if that side
    didn't fire a complete pair for this seam index."""
    ps = _load_spans(port_firings)
    rs = _load_spans(retail_firings)
    seams: list[dict] = []
    for i in range(max(len(ps), len(rs))):
        p = ps[i] if i < len(ps) else (None, None)
        r = rs[i] if i < len(rs) else (None, None)

        def ticks(span):
            return (span[1] - span[0]) if (span[0] is not None
                                           and span[1] is not None) else None

        seams.append({
            "kind": "load_seam", "anchor": "LOADING_END",
            "port_start": p[0], "port_end": p[1],
            "retail_start": r[0], "retail_end": r[1],
            "port_ticks": ticks(p), "retail_ticks": ticks(r),
        })
    return seams


def build_timeline(*, port_firings: list[dict], retail_firings: list[dict],
                   n_frames: int, frame_range: list[int] | None,
                   videos: dict | None, verdict: dict | None,
                   state: str | None, call_trace: bool) -> list[dict]:
    """Build the ordered v2 timeline: the load seam(s) reconstructed from the
    anchor streams, then one gameplay entry for the captured window."""
    timeline: list[dict] = list(find_load_seams(port_firings, retail_firings))
    fr = list(frame_range) if frame_range else [0, max(0, int(n_frames) - 1)]
    timeline.append({
        "kind": "gameplay", "idx": 0,
        "frames": fr, "n_frames": int(n_frames),
        "videos": dict(videos or {}),
        "verdict": verdict,
        "state": state,
        "call_trace": bool(call_trace),
    })
    return timeline
