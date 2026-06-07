"""model/ops.py — pure trace-op + anchor-stream parsing.

Lifted from the trace_studio monolith. These read the small JSONL artifacts a
trace/capture produces (trace ops, anchors.jsonl) and return plain Python — no
engine or driver deps. Behaviour is byte-for-byte identical to the originals.
"""
from __future__ import annotations

import json
from pathlib import Path


def load_ops(path: Path) -> list[dict]:
    """Parse a trace .jsonl into a list of op dicts (skipping blanks/comments)."""
    ops: list[dict] = []
    for ln in Path(path).read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            ops.append(json.loads(s))
        except json.JSONDecodeError:
            pass
    return ops


def is_json(line: str) -> bool:
    try:
        json.loads(line.strip())
        return True
    except json.JSONDecodeError:
        return False


def extract_caprange(ops: list[dict]) -> tuple[int, int] | None:
    for o in ops:
        if isinstance(o, dict) and "caprange" in o:
            cr = o["caprange"]
            return int(cr[0]), int(cr[1])
    return None


def extract_capstride(ops: list[dict]) -> int:
    """The trace-global {capstride:N} cadence (D3), or 1 (dense) if absent. >1 means
    the {caprange} window captures every Nth frame — a coarse OVERVIEW."""
    for o in ops:
        if isinstance(o, dict) and "capstride" in o:
            try:
                n = int(o["capstride"])
            except (TypeError, ValueError):
                return 1
            return n if n > 1 else 1
    return 1


def extract_calltrace(ops: list[dict]) -> tuple[int, int] | None:
    for o in ops:
        if isinstance(o, dict) and "calltrace" in o:
            ct = o["calltrace"]
            if isinstance(ct, list):
                return int(ct[0]), int(ct[1])
            return int(ct), 0
    return None


def raw_header(path: Path) -> dict | None:
    """If `path` is a frida/F2 raw recording, return its header dict, else None."""
    try:
        first = next(l for l in Path(path).read_text().splitlines() if l.strip())
        o = json.loads(first)
    except (StopIteration, OSError, json.JSONDecodeError):
        return None
    return o if isinstance(o, dict) and str(o.get("_rec", "")).startswith(
        "openrecet-tas-raw") else None


def raw_has_anchors(path: Path) -> bool:
    """True if a raw recording carries {anchor} rows (the recorder logged the
    BOOT/LOADING/HOUSE_FREEROAM/… stream). When present, the distill should
    anchor-segment (the FLAT boot-relative window otherwise lands a load-bearing
    trace's {caprange} in the pre-load region — e.g. a Continue trace stops at the
    save-picker instead of reaching the loaded scene)."""
    for ln in Path(path).read_text().splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and "anchor" in o and "frame" in o:
            return True
    return False


FREEROAM_ANCHORS = ("HOUSE_FREEROAM", "TOWN_FREEROAM", "FREEROAM_START")


def raw_default_window(path: Path, anchored: bool = False) -> tuple[int, int] | None:
    """Default capture window for a raw recording. FLAT (default): the WHOLE
    recording from boot, [0, last_frame+margin]. ANCHORED: from the FIRST FREE-ROAM
    entry (HOUSE_FREEROAM etc.) through the end — that's where comparable gameplay
    begins AND it's reachable on both targets. (Anchoring at the LAST anchor would
    pick a later scene the PORT may not reach yet — e.g. a town the shop-exit leads
    to — so the port captures 0 / 'window never reached'.) Falls back to the last
    anchor if no free-roam anchor was recorded."""
    anchor_rows: list[tuple[str, int]] = []
    frames: list[int] = []
    for ln in Path(path).read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if "anchor" in o and "frame" in o:
            anchor_rows.append((str(o.get("anchor", "")), int(o["frame"])))
        elif "frame" in o and "buttons" in o:
            frames.append(int(o["frame"]))
    if not frames:
        return None
    base = 0
    if anchored and anchor_rows:
        freeroam = [f for n, f in anchor_rows if n in FREEROAM_ANCHORS]
        base = freeroam[0] if freeroam else max(f for _, f in anchor_rows)
    return (0, max(1, max(frames) - base + 90))


def first_freeroam_wait(text: str) -> str | None:
    """The {wait} op name marking the FIRST free-roam entry in a distilled trace —
    where the auto-window should anchor (see raw_default_window). The distil emits a
    {wait LOADING_END} coincident with HOUSE_FREEROAM (they fire the same frame), so
    the free-roam entry is the FIRST LOADING_END wait. Returns the wait name to place
    the window after, or None (→ caller falls back to the last wait)."""
    for ln in text.splitlines():
        s = ln.strip()
        if not s or s.startswith("#"):
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if isinstance(o, dict) and o.get("wait") in ("LOADING_END", *FREEROAM_ANCHORS):
            return o["wait"]
    return None


def window_at_freeroam(ops_list: list[dict]) -> bool:
    """True if the {caprange} is anchored at the FIRST free-roam entry — i.e. it sits
    in that segment: after the first LOADING_END / FREEROAM {wait}, with NO later
    {wait} before it. A window after a LATER scene's {wait} (the town a shop-exit
    leads to — port-unreachable) or with no {wait} at all (FLAT, pre-load) is STALE:
    the port captures 0. Used by re-capture to decide whether to rebuild the working
    trace (a session built before the free-roam-anchored auto-window has a stale one)."""
    seen_freeroam = False
    for o in ops_list:
        if not isinstance(o, dict):
            continue
        w = o.get("wait")
        if w in ("LOADING_END", *FREEROAM_ANCHORS):
            seen_freeroam = True
        elif w and seen_freeroam:        # a later scene's {wait} before the window
            return False
        elif "caprange" in o:
            return seen_freeroam
    return False


def read_anchor_stream(path: Path) -> list[dict]:
    """Absolute anchor firings [{anchor, frame, ...}] in file order. This is the
    raw anchors.jsonl both sides emit (frames are ABSOLUTE engine frames). Used by
    segments (resolve_bases) + timeline (load-seam reconstruction)."""
    out: list[dict] = []
    p = Path(path)
    if not p.exists():
        return out
    for ln in p.read_text().splitlines():
        s = ln.strip()
        if not s:
            continue
        try:
            o = json.loads(s)
        except json.JSONDecodeError:
            continue
        if "anchor" in o and "frame" in o:
            out.append({**o, "frame": int(o["frame"])})
    return out


def read_anchors(path: Path, base: int) -> list[dict]:
    """anchors.jsonl rebased to anchor-relative index ({anchor, frame-base})."""
    return [{"anchor": a["anchor"], "frame": a["frame"] - base}
            for a in read_anchor_stream(path)]


def resolve_trace(arg: str, root: Path) -> Path:
    """A trace file path, or a scenario name → tests/scenarios/<name>/trace.jsonl."""
    p = Path(arg)
    if p.exists():
        return p.resolve()
    scn = Path(root) / "tests" / "scenarios" / arg / "trace.jsonl"
    if scn.exists():
        return scn.resolve()
    raise SystemExit(f"trace_studio: no trace file or scenario named {arg!r}")
