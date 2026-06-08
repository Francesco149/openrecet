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


def resolve_side(segments: list[dict], firings: list[dict]) -> dict:
    """Resolve ONE side: segment bases AND a display PLACEMENT for every firing. Mirrors
    align.mjs:resolveSide — bases identical to resolve_bases; placements give the (seg, rel)
    each firing's chip displays at, the segment it BELONGS to (walking the RESOLVED bases,
    not "last base ≤ frame" which mis-assigns when unresolved bases stack). See
    docs/findings/trace-editor-segment-alignment.md.
    """
    bases: list[dict] = []
    resolver_seg: dict[int, int] = {}     # firing index → the segment it resolved
    cursor = 0
    for k in range(len(segments)):
        if k == 0:
            bases.append({"base": 0, "ok": True, "anchor": None})
            continue
        name = segments[k]["wait_anchor"]
        i = next((idx for idx, f in enumerate(firings)
                  if f["anchor"] == name and f["frame"] > cursor), -1)
        if i >= 0:
            bases.append({"base": firings[i]["frame"], "ok": True, "anchor": name})
            cursor = firings[i]["frame"]
            resolver_seg[i] = k
        else:
            bases.append({"base": cursor, "ok": False, "anchor": name})
    resolved = [{"seg": k, "base": b["base"]} for k, b in enumerate(bases) if b["ok"]]
    placements: list[dict] = []
    for i, f in enumerate(firings):
        if i in resolver_seg:
            placements.append({"seg": resolver_seg[i], "rel": 0})
            continue
        seg = 0
        for r in resolved:
            if r["base"] <= f["frame"]:
                seg = r["seg"]
        placements.append({"seg": seg, "rel": f["frame"] - bases[seg]["base"]})
    return {"bases": bases, "placements": placements}


def abs_to_band(abs_frame: int, bases: list[dict]) -> dict:
    """Absolute frame → the band it falls in (largest RESOLVED base ≤ abs; unresolved
    segments are skipped). Mirrors align.mjs:absToBand."""
    seg = 0
    for k in range(len(bases)):
        if bases[k] and bases[k]["ok"] and bases[k]["base"] <= abs_frame:
            seg = k
    return {"seg": seg, "rel": abs_frame - (bases[seg]["base"] if seg < len(bases) else 0)}


def editor_layout(segments: list[dict], port_firings: list[dict],
                  retail_firings: list[dict], *, emitted: list[dict] | None = None,
                  notes: list[dict] | None = None, window_side: str | None = None,
                  window_start_abs: int | None = None, window_end_abs: int | None = None,
                  gap: int = 16, min_band: int = 8, pad: int = 4) -> dict:
    """Lay segments out as sequential, NON-OVERLAPPING bands (mirrors align.mjs:editorLayout
    — THE alignment workhorse). Band k = [X[k], X[k]+W[k]); width fits the widest content in
    the band across BOTH sides; origins X[k]=X[k-1]+W[k-1]+gap. A per-segment load-stretch
    collapses to the inter-band gap; a divergent/incomplete trace still gets one band per
    segment. The captured window is an ABSOLUTE span on `window_side` (NOT the {caprange} op's
    trace position) — mapped across the bands it covers (each widened to fit) with its band
    endpoints returned. See docs/findings/trace-editor-segment-alignment.md."""
    emitted = emitted or []
    notes = notes or []
    port = resolve_side(segments, port_firings)
    retail = resolve_side(segments, retail_firings)
    n = len(segments)
    ext = [0] * n

    def bump(seg: int, rel: int) -> None:
        if 0 <= seg < n and rel > ext[seg]:
            ext[seg] = rel

    for k, s in enumerate(segments):
        for it in s["items"]:
            bump(k, it["frame"])
    for e in emitted:
        bump(e["seg"], e["frame"])
    for nt in notes:
        bump(nt["seg"], nt["frame"])
    for p in port["placements"]:
        bump(p["seg"], p["rel"])
    for p in retail["placements"]:
        bump(p["seg"], p["rel"])

    window = None
    if window_side and window_start_abs is not None and window_end_abs is not None:
        wb = (port if window_side == "port" else retail)["bases"]
        resolved = [{"seg": k, "base": wb[k]["base"]} for k in range(len(wb)) if wb[k]["ok"]]
        for i, r in enumerate(resolved):
            b = r["base"]
            nb = resolved[i + 1]["base"] if i + 1 < len(resolved) else float("inf")
            lo = max(window_start_abs, b)
            hi = min(window_end_abs, nb - 1)
            if hi >= lo:
                bump(r["seg"], hi - b + 1)
        s = abs_to_band(window_start_abs, wb)
        e = abs_to_band(window_end_abs, wb)
        window = {"startSeg": s["seg"], "startRel": s["rel"],
                  "endSeg": e["seg"], "endRel": e["rel"]}

    w = [max(min_band, e + pad) for e in ext]
    x_origins: list[int] = []
    x = 0
    for k in range(n):
        x_origins.append(x)
        x += w[k] + gap
    return {"port": port, "retail": retail, "X": x_origins, "W": w, "ext": ext,
            "gap": gap, "window": window}


def band_at(x_origins: list[int], pos: int) -> dict:
    """Layout position → {seg, rel}: the band whose origin is the largest ≤ pos (screen→
    segment inverse). Positions left of band 0 clamp to seg 0. Mirrors align.mjs:bandAt."""
    seg = 0
    for k in range(len(x_origins)):
        if x_origins[k] <= pos:
            seg = k
    return {"seg": seg, "rel": pos - (x_origins[seg] if seg < len(x_origins) else 0)}


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
