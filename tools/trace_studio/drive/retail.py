"""drive/retail.py — drive RETAIL through the SAME work trace via frida_capture.

Wraps frida_capture.run_capture (turbo, hidden, silent-audio, 1024×768, anchor +
optional call trace). The {savefile} ref is relative to the ORIGINAL trace's dir
(not the session work copy), so the save is resolved against orig_trace.
Forwards `suppress_loads` (D1) when the caller's EngineCaps says retail supports it.
"""
from __future__ import annotations

from pathlib import Path


def capture_retail(*, trace_work: Path, orig_trace: Path, retail_dir: Path,
                   max_frames: int, rng_seed: int | None, call_trace: bool,
                   remote: str, suppress_loads: bool, result: dict) -> None:
    import types

    import frida_capture
    import trace_save
    retail_dir.mkdir(parents=True, exist_ok=True)
    scen = types.SimpleNamespace(
        name="trace-studio",
        capture_frames=[],
        max_frames=int(max_frames),
        duration_ceiling_ms=600_000,
        rng_seed=rng_seed,
    )
    try:
        meta = frida_capture.run_capture(
            scen, retail_dir,
            remote=remote,
            input_segtrace_path=trace_work,
            hide_window=True, turbo=True, silent_audio=True,
            force_resolution=(1024, 768),
            rng_seed=rng_seed,
            save_ref=trace_save.resolve_save(orig_trace),
            call_trace=call_trace,
            anchor_trace=True,        # → retail anchors.jsonl for the studio timeline
            suppress_loads=suppress_loads,   # D1 load-seam suppression
        )
        result["retail_meta"] = meta
    except Exception as e:                       # noqa: BLE001 — surface, don't crash port
        result["retail_error"] = repr(e)
        print(f"trace_studio: retail capture FAILED: {e!r}")
