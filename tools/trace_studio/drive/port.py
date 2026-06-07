"""drive/port.py — drive the PORT through a trace's capture window via export_trace.

Wraps export_trace.main (which owns run-openrecet.sh, TAS save virtualization, the
{caprange}/{calltrace} window arming, and BMP→PNG). Drops anchors.jsonl via
--anchor-record (the port-side anchor stream the timeline aligns on). The caller
gates `suppress_loads` on EngineCaps; we just forward the flag when it's on.
"""
from __future__ import annotations

from pathlib import Path


def capture_port(*, trace: Path, port_dir: Path, cr: tuple[int, int],
                 max_frames: int, call_trace: bool, suppress_loads: bool,
                 result: dict) -> None:
    import export_trace
    argv = [
        str(trace),
        "--caprange", f"{cr[0]},{cr[1]}",
        "--run-dir", str(port_dir),
        "--max-frames", str(max_frames),
        "--name", "trace-studio",
        "--anchor-record",        # port anchor stream → port_dir/anchors.jsonl
    ]
    if call_trace:
        argv.append("--call-trace")
    if suppress_loads:
        argv.append("--capture-suppress-loads")     # D1 load-seam suppression
    # export_trace still drops its final_anchor into global.json; the port anchor
    # ticks on the timeline come from anchors.jsonl, retail carries its own.
    result["port_rc"] = export_trace.main(argv)
