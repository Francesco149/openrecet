"""model/segments.py — PURE alignment core (Python port of trace_studio_web/align.mjs).

THE single source of the viewer-index ↔ segment-frame ↔ absolute-frame contract
(it used to be duplicated 4× across align.mjs / apply / export renumber / timeline).
`align.mjs` is the JS twin the browser timeline uses; a golden cross-check test
(tools/test_trace_studio_segments.py) runs a shared fixture through BOTH and asserts
identical results, so the two never drift.

A trace is anchor-segmented: ops before the 1st {wait} are segment 0 (base frame 0);
each {wait ANCHOR} opens a new segment whose base is the absolute frame that anchor
RESOLVES to on a given side (next firing strictly after the previous segment's base —
mirroring the replay resolver). A trace op's frame is relative to its segment base.

The timeline x-axis is "frames relative to a chosen SYNC anchor": both sides count
from the sync anchor's firing, so shared anchors + mirrored ops line up and a divergent
anchor (fired at a different offset, or not at all) shows as a horizontal gap.

Keys are snake_case (this module is the Python source of truth); the JS twin uses
camelCase. The golden test compares a normalized projection, not raw key names.
"""
from __future__ import annotations

import math


def parse_segments(ops: list) -> list[dict]:
    """Parse a trace (list of op objects, in file order) into segments.
    Returns [{wait_anchor: str|None, items: [{kind, frame, idx, op, ...}]}].
      kind ∈ "input" | "phasepin" | "rngseed" | "esc"   (frame = segment-relative)
    """
    segs: list[dict] = [{"wait_anchor": None, "items": []}]
    for idx, op in enumerate(ops):
        if op is None or not isinstance(op, dict):
            continue
        if "wait" in op:
            segs.append({"wait_anchor": op["wait"], "items": []})
            continue
        seg = segs[-1]
        if "frame" in op and "buttons" in op:
            seg["items"].append({"kind": "input", "frame": int(op["frame"]),
                                 "buttons": op["buttons"], "idx": idx, "op": op})
        elif "phasepin" in op:
            seg["items"].append({"kind": "phasepin", "frame": int(op["phasepin"]),
                                 "idx": idx, "op": op})
        elif "rngseed" in op and isinstance(op["rngseed"], list):
            seg["items"].append({"kind": "rngseed", "frame": int(op["rngseed"][0]),
                                 "value": op["rngseed"][1], "idx": idx, "op": op})
        elif "esc" in op:
            seg["items"].append({"kind": "esc", "frame": int(op["esc"]),
                                 "idx": idx, "op": op})
        # caprange/calltrace/savefile/wait_until/gframe/poke → not drawn as items
    return segs


def resolve_bases(segments: list[dict], firings: list[dict]) -> list[dict]:
    """Resolve each segment's base absolute frame from one side's anchor firings.
    firings: ordered [{anchor, frame}]. Returns [{base, ok, anchor}].
    Segment 0 → base 0. Segment k>0 → first firing of its wait_anchor with
    frame > cursor. Unresolved (that side never fired the anchor after the cursor)
    → ok False, base = cursor (so following segments stay placed, just flagged)."""
    out: list[dict] = []
    cursor = 0
    for k, seg in enumerate(segments):
        if k == 0:
            out.append({"base": 0, "ok": True, "anchor": None})
            continue
        name = seg["wait_anchor"]
        hit = next((f for f in firings
                    if f["anchor"] == name and f["frame"] > cursor), None)
        if hit:
            out.append({"base": hit["frame"], "ok": True, "anchor": name})
            cursor = hit["frame"]
        else:
            out.append({"base": cursor, "ok": False, "anchor": name})
    return out


def side_layout(segments: list[dict], firings: list[dict],
                sync_seg: int | None = None) -> dict:
    """Per-side layout for a chosen sync anchor. sync_seg = the segment index to
    anchor the view on (default: last segment). Returns {bases, sync_frame}."""
    bases = resolve_bases(segments, firings)
    k = (len(bases) - 1) if sync_seg is None else sync_seg
    sync_frame = bases[k]["base"] if bases[k] else 0
    return {"bases": bases, "sync_frame": sync_frame}


def _jsround(x: float) -> int:
    """JS Math.round (round half toward +inf): floor(x + 0.5). Python's built-in
    round() is banker's rounding, which would diverge from the align.mjs twin."""
    return math.floor(x + 0.5)


def abs_to_x(abs_frame: float, sync_frame: float, px_per_frame: float,
             x_zero: float = 0) -> float:
    """Absolute engine frame → screen x (px). x_zero = where sync_frame sits (px)."""
    return x_zero + (abs_frame - sync_frame) * px_per_frame


def x_to_abs(x: float, sync_frame: float, px_per_frame: float,
             x_zero: float = 0) -> int:
    return _jsround((x - x_zero) / px_per_frame) + sync_frame


def item_abs(item: dict, seg_idx: int, bases: list[dict]) -> int:
    """Item segment-relative frame → absolute on a side (base + frame)."""
    b = bases[seg_idx]
    return (b["base"] if b else 0) + item["frame"]


def ref_frame(abs_frame: int, side_bases: list[dict],
              ref_bases: list[dict]) -> int:
    """Piecewise re-base ONE side's absolute frame onto a REFERENCE side's axis.

    The editor draws every side on ONE axis (the port = the truthful reference). Within
    an anchor segment both sides advance at the same rate, but their segment BASES differ
    — a load the reference skips stretches the other side's frame count (retail's
    LOADING_END can land at frame 11806 where the port's is 363). Mapping
    abs → ref_base[k] + (abs − side_base[k]), where k is the segment abs falls in ON THAT
    SIDE, pins each segment to the reference's base: shared anchors + {wait}-mirrored ops
    COINCIDE and the per-segment load-stretch collapses onto the reference's truthful
    positions. A genuine WITHIN-segment offset survives as a gap. Passing the reference's
    own bases as `side_bases` is the identity. Mirrors align.mjs:refFrame.
    """
    k = 0
    for i in range(len(side_bases)):
        if (side_bases[i]["base"] if side_bases[i] else 0) <= abs_frame:
            k = i
    sb = side_bases[k]["base"] if side_bases[k] else 0
    rb = ref_bases[k]["base"] if ref_bases[k] else 0
    return rb + (abs_frame - sb)


def anchor_names(*firing_lists: list[dict]) -> list[str]:
    """Distinct anchor names present, for the sync-anchor picker (insertion order)."""
    seen: dict[str, None] = {}
    for lst in firing_lists:
        for f in lst:
            seen[f["anchor"]] = None
    return list(seen)


def divergence_report(segments: list[dict], port_firings: list[dict],
                      retail_firings: list[dict],
                      sync_seg: int | None = None) -> list[dict]:
    """Which waits resolved on each side + their anchor-relative offsets. A wait
    whose port_rel ≠ retail_rel is a divergence (fired at a different offset, or
    not at all). Returns one row per segment."""
    p = side_layout(segments, port_firings, sync_seg)
    r = side_layout(segments, retail_firings, sync_seg)
    out: list[dict] = []
    for k, s in enumerate(segments):
        out.append({
            "seg": k, "anchor": s["wait_anchor"],
            "port_base": p["bases"][k]["base"], "retail_base": r["bases"][k]["base"],
            "port_ok": p["bases"][k]["ok"], "retail_ok": r["bases"][k]["ok"],
            # anchor-relative position on each side (frames from sync); differ ⇒ divergence
            "port_rel": p["bases"][k]["base"] - p["sync_frame"],
            "retail_rel": r["bases"][k]["base"] - r["sync_frame"],
        })
    return out
