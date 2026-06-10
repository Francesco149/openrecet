"""analysis/verdict.py — the phase/RNG verdict over the two call traces.

Runs flow_diff --verdict (ALIGNED / CONST-OFFSET / DRIFT + rngcalls). Port and
retail frames are ABSOLUTE (port ~600, retail ~14500 under turbo load-stretch),
so they're paired by --align-field db054 (the shared phase clock), not raw frame
number.

CUTSCENE FALLBACK: db054 is a HOUSE free-roam bob/sparkle counter — it never
advances during a dialogue cutscene (mode 6 / the guild + town event scenes),
so --align-field db054 finds NO shared values there.  When that happens we
re-align by a CONSTANT frame offset taken from the first dialogue anchor
(TEXT_ANIM_START) and clip the comparison to that anchor onward (dropping the
pre-text fade-in, whose origin rides the load-suppression seam).  The cutscene's
dialogue-tick state + rngcalls are richly probed on both sides, so this yields a
real verdict (see findings/merchant-guild-RE.md).
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

from ..paths import ROOT

# First dialogue anchor a cutscene reaches — present on both sides for every
# iv*.ivt scene (prologue, guild, town events). The cutscene verdict aligns +
# clips here.
CUTSCENE_ANCHOR = "TEXT_ANIM_START"


def _first_anchor_frame(anchors_path: Path, name: str) -> int | None:
    if not anchors_path.exists():
        return None
    for line in anchors_path.open():
        line = line.strip()
        if not line:
            continue
        try:
            a = json.loads(line)
        except json.JSONDecodeError:
            continue
        if a.get("anchor") == name:
            return int(a["frame"])
    return None


def _flow(rp: Path, pp: Path, extra: list[str], align: str) -> dict:
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "flow_diff.py"),
         "--retail", str(rp), "--port", str(pp), "--verdict", *extra],
        capture_output=True, text=True, cwd=str(ROOT))
    return {"available": True, "exit_code": r.returncode, "align_field": align,
            "text": r.stdout + (("\n[stderr]\n" + r.stderr) if r.stderr else "")}


def run_verdict(port_dir: Path, retail_dir: Path,
                align_field: str = "db054") -> dict:
    rp = Path(retail_dir) / "call_trace.jsonl"
    pp = Path(port_dir) / "call_trace.jsonl"
    if not (rp.exists() and pp.exists()):
        return {"available": False}

    res = _flow(rp, pp, ["--align-field", align_field], align_field)

    # Cutscene / mode-6: the align-field counter is absent (no shared values).
    # Fall back to constant-offset anchor alignment, clipped to the cutscene.
    if "no shared values" in res["text"]:
        ra = rp.parent / "anchors.jsonl"
        pa = pp.parent / "anchors.jsonl"
        af = _first_anchor_frame(ra, CUTSCENE_ANCHOR)
        if af is not None and _first_anchor_frame(pa, CUTSCENE_ANCHOR) is not None:
            res = _flow(rp, pp,
                        ["--align-anchor", CUTSCENE_ANCHOR,
                         "--frame-from", str(af),
                         "--retail-anchors", str(ra), "--port-anchors", str(pa)],
                        f"anchor:{CUTSCENE_ANCHOR}@{af}")
            res["fallback"] = "cutscene-anchor"
    return res
