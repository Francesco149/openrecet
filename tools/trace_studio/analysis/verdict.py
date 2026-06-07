"""analysis/verdict.py — the phase/RNG verdict over the two call traces.

Runs flow_diff --verdict (ALIGNED / CONST-OFFSET / DRIFT + rngcalls). Port and
retail frames are ABSOLUTE (port ~600, retail ~14500 under turbo load-stretch), so
they're paired by --align-field db054 (the shared phase clock), not raw frame number.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

from ..paths import ROOT


def run_verdict(port_dir: Path, retail_dir: Path,
                align_field: str = "db054") -> dict:
    rp = Path(retail_dir) / "call_trace.jsonl"
    pp = Path(port_dir) / "call_trace.jsonl"
    if not (rp.exists() and pp.exists()):
        return {"available": False}
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "flow_diff.py"),
         "--retail", str(rp), "--port", str(pp), "--verdict",
         "--align-field", align_field],
        capture_output=True, text=True, cwd=str(ROOT))
    return {"available": True, "exit_code": r.returncode,
            "align_field": align_field,
            "text": r.stdout + (("\n[stderr]\n" + r.stderr) if r.stderr else "")}
