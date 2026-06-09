"""analysis/state.py — merge the once-per-frame flow-trace fields (both sides) into
VIEWER-ORDINAL rows the state overlay highlights when port≠retail.

Rows are keyed by the viewer ordinal k (the i-th captured frame, the index the SPA
cursor / apply.py's marks use): k = (anchor_relative - window_start) / stride. The
lightweight once-per-frame probes emit from boot regardless of the {calltrace}
window, so rows outside the captured window (and, under a stride, between kept
frames) are dropped. Keying by k — not by the raw anchor-relative frame — keeps the
overlay correct for caprange.start > 0 and capstride > 1 windows (previously those
rows mis-keyed/clipped: the start=0-only assumption family).
"""
from __future__ import annotations

from pathlib import Path

# Once-per-frame flow-trace VAs whose declared fields we surface in the viewer state
# overlay (docs/flow-trace-cheatsheet.md "Standard once-per-frame anchors").
STATE_VAS = {
    0x47be92: "sched",      # tick_scheduler — rng / rngcalls
    0x48670f: "house",      # house_update   — player+companion poct/px/anim/...
    0x49a59e: "title",      # scene_title_sim
    0x46c320: "dlg",        # dialogue_tick
}


def build_state(port_dir: Path, retail_dir: Path, port_anchor: int,
                retail_anchor: int, nframes: int, window_start: int = 0,
                stride: int = 1) -> list[dict]:
    """[{frame, port:{...}, retail:{...}}] keyed by viewer ordinal k ∈ [0, nframes).

    `*_anchor` is each side's ABSOLUTE frame of anchor-relative 0 (port: global.json
    frame_base_abs; retail: first-kept-frame abs − window_start). A trace row at
    absolute frame F maps to k = (F − anchor − window_start) / stride."""
    from flow_diff import load_trace
    vaset = set(STATE_VAS)
    stride = max(1, int(stride))

    def collect(path: Path, anchor: int) -> dict[int, dict]:
        path = Path(path)
        if not path.exists():
            return {}
        by_frame = load_trace(path, va_filter=vaset)
        out: dict[int, dict] = {}
        for fr, evts in by_frame.items():
            off = fr - anchor - window_start
            if off < 0 or off % stride:
                continue
            k = off // stride
            if k >= nframes:
                continue
            merged: dict = {}
            for e in evts:
                f = e.get("f")
                if isinstance(f, dict):
                    merged.update(f)
            if merged:
                out[k] = merged
        return out

    p = collect(Path(port_dir) / "call_trace.jsonl", port_anchor)
    r = collect(Path(retail_dir) / "call_trace.jsonl", retail_anchor)
    frames = sorted(set(p) | set(r))
    return [{"frame": n, "port": p.get(n, {}), "retail": r.get(n, {})}
            for n in frames]
