#!/usr/bin/env python3
"""test_trace_studio_anchoring.py — a recording that carries anchors auto-anchors.

Guards the fix for "retail stops after pressing Z on the save file": a load-bearing
Continue recording distilled FLAT (boot-synced) lands its {caprange} in the pre-load
region (the title/save-picker), so the capture stops at the load instead of reaching
the loaded scene. The recorder logs {anchor} rows precisely so the window can target
the loaded content; `ops.raw_has_anchors` drives `capture.run_capture` to anchor-
segment by default (cfg.anchors None = auto).
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import trace_studio.model.ops as ops  # noqa: E402


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)

        # a shop→town Continue recording: HOUSE_FREEROAM at 200, then a LATER load
        # (the shop-exit → town) at 460. The port reaches HF but not the town.
        rec = d / "with.raw.jsonl"
        rec.write_text("\n".join([
            '{"_rec": "openrecet-tas-raw/1", "ver": 1}',
            '{"frame": 0, "buttons": "0x0"}',
            '{"frame": 50, "buttons": "0x10"}',     # title
            '{"frame": 260, "buttons": "0x01"}',    # walk (after HF)
            '{"frame": 420, "buttons": "0x10"}',    # door-Z
            '{"frame": 500, "buttons": "0x01"}',    # town nav (port never reaches)
            '{"anchor": "BOOT", "frame": 0}',
            '{"anchor": "NEW_GAME", "frame": 50}',
            '{"anchor": "LOADING_END", "frame": 200}',
            '{"anchor": "HOUSE_FREEROAM", "frame": 200}',
            '{"anchor": "LOADING_START", "frame": 425}',
            '{"anchor": "LOADING_END", "frame": 460}',
        ]) + "\n")
        assert ops.raw_has_anchors(rec) is True, "anchors not detected"
        # the auto decision (capture.py): cfg.anchors None → anchored = rec_has_anchors
        cfg_anchors = None                       # AUTO (the new default)
        anchored = cfg_anchors if cfg_anchors is not None else (
            ops.raw_header(rec) is not None and ops.raw_has_anchors(rec))
        assert anchored is True, "auto did not anchor a recording-with-anchors"
        # the anchored window must base on the FIRST FREE-ROAM entry (HOUSE_FREEROAM=200,
        # port-reachable) → span 500-200+90=390 — NOT the LAST anchor (town LOADING_END=
        # 460 → span 130, which the port can't reach → it'd capture 0).
        w_anch = ops.raw_default_window(rec, anchored=True)
        assert w_anch == (0, 390), f"anchored window not based on HOUSE_FREEROAM: {w_anch}"
        assert ops.raw_default_window(rec, anchored=False) == (0, 590), "flat window wrong"
        # the window is placed after the FIRST LOADING_END {wait} (= the HF entry)
        distilled = "\n".join([
            '{"wait": "NEW_GAME"}', '{"wait": "LOADING_END"}',
            '{"frame": 60, "buttons": "0x1"}', '{"wait": "LOADING_START"}',
            '{"wait": "LOADING_END"}'])
        assert ops.first_freeroam_wait(distilled) == "LOADING_END", "free-roam wait not found"

        # a recording with NO anchors → stays FLAT (auto leaves it boot-synced)
        rec2 = d / "without.raw.jsonl"
        rec2.write_text("\n".join([
            '{"_rec": "openrecet-tas-raw/1", "ver": 1}',
            '{"frame": 0, "buttons": "0x0"}',
            '{"frame": 50, "buttons": "0x10"}',
        ]) + "\n")
        assert ops.raw_has_anchors(rec2) is False, "false-positive anchor detection"

        # a non-recording (e.g. a distilled scenario trace) → not a recording, no anchors
        scn = d / "scn.trace.jsonl"
        scn.write_text('{"caprange": [0, 60]}\n{"frame": 0, "mask": 0}\n')
        assert ops.raw_header(scn) is None, "distilled trace misdetected as a recording"
        assert ops.raw_has_anchors(scn) is False

    print("OK: trace_studio anchoring (recording-with-anchors auto-anchors; "
          "no-anchor recording stays FLAT)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
