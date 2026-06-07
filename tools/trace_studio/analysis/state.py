"""analysis/state.py — merge the once-per-frame flow-trace fields (both sides) into
anchor-relative rows the viewer's state overlay highlights when port≠retail.

The lightweight once-per-frame probes emit from boot regardless of the {calltrace}
window, so rows are clipped to the captured span [0, nframes).
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


def build_state(port_dir: Path, retail_dir: Path, port_base: int,
                retail_base: int, nframes: int) -> list[dict]:
    """[{frame, port:{...}, retail:{...}}] over [0, nframes), keyed anchor-relative."""
    from flow_diff import load_trace
    vaset = set(STATE_VAS)

    def collect(path: Path, base: int) -> dict[int, dict]:
        path = Path(path)
        if not path.exists():
            return {}
        by_frame = load_trace(path, va_filter=vaset)
        out: dict[int, dict] = {}
        for fr, evts in by_frame.items():
            rel = fr - base
            if rel < 0 or rel >= nframes:
                continue
            merged: dict = {}
            for e in evts:
                f = e.get("f")
                if isinstance(f, dict):
                    merged.update(f)
            if merged:
                out[rel] = merged
        return out

    p = collect(Path(port_dir) / "call_trace.jsonl", port_base)
    r = collect(Path(retail_dir) / "call_trace.jsonl", retail_base)
    frames = sorted(set(p) | set(r))
    return [{"frame": n, "port": p.get(n, {}), "retail": r.get(n, {})}
            for n in frames]
