"""drive/runner.py — concurrent port+retail drive (the threading core of capture).

Drives the selected side(s) at once. Retail replays the work trace export_trace
writes for the port, so it waits for that file before starting. Returns only the
drive result (port_rc / retail_meta / retail_error); post-processing + the manifest
live in the capture orchestrator.
"""
from __future__ import annotations

import threading
import time
from pathlib import Path

from . import port as port_drive
from . import retail as retail_drive


def drive_both(*, working_trace: Path, orig_trace: Path, ops: list[dict],
               port_dir: Path, retail_dir: Path, cr: tuple[int, int],
               call_trace: bool, run_port: bool, run_retail: bool,
               port_max_frames: int, retail_max_frames: int, remote: str,
               port_suppress: bool, retail_suppress: bool) -> dict:
    result: dict = {}
    threads: list[threading.Thread] = []

    if run_port:
        tp = threading.Thread(target=port_drive.capture_port, kwargs=dict(
            trace=working_trace, port_dir=port_dir, cr=cr,
            max_frames=port_max_frames, call_trace=call_trace,
            suppress_loads=port_suppress, result=result))
        tp.start()
        threads.append(tp)

    if run_retail:
        # Retail replays the work trace export_trace writes; wait for it to appear.
        work = port_dir / "trace.work.jsonl"
        for _ in range(600):                     # ≤60s for the port to write it
            if work.exists():
                break
            time.sleep(0.1)
        seed_from_trace = next((o.get("rngseed", [None, None])[1]
                                for o in ops if "rngseed" in o), None)
        if work.exists():
            tr = threading.Thread(target=retail_drive.capture_retail, kwargs=dict(
                trace_work=work, orig_trace=orig_trace, retail_dir=retail_dir,
                max_frames=retail_max_frames, rng_seed=seed_from_trace,
                call_trace=call_trace, remote=remote,
                suppress_loads=retail_suppress, result=result))
            tr.start()
            threads.append(tr)
        else:
            print("trace_studio: port never wrote trace.work.jsonl — skipping retail")
            result["retail_skipped"] = True

    for t in threads:
        t.join()
    return result
