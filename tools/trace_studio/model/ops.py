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


def raw_default_window(path: Path, anchored: bool = False) -> tuple[int, int] | None:
    """Default capture window for a raw recording. FLAT (default): the WHOLE
    recording from boot, [0, last_frame+margin]. ANCHORED: from the LAST recorded
    anchor (the post-intro span) through the end."""
    anchors: list[int] = []
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
            anchors.append(int(o["frame"]))
        elif "frame" in o and "buttons" in o:
            frames.append(int(o["frame"]))
    if not frames:
        return None
    base = (max(anchors) if anchors else 0) if anchored else 0
    return (0, max(1, max(frames) - base + 90))


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
