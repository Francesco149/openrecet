#!/usr/bin/env python3
"""tools/test_trace_studio_session.py — v2 session schema + v1 migration + timeline.

Pins the Phase-2 acceptance backbone:
  1. load_session() opens a v1 manifest (no schema_version/timeline) and SYNTHESIZES
     one gameplay segment from the global fields — "old v1 sessions still open".
  2. load_session() opens a v2 manifest and returns the stored timeline (seams +
     gameplay), and a v2 manifest is a v1 SUPERSET (keeps the fields the old UI reads).
  3. build_timeline() reconstructs zero-frame load_seams from the anchor streams,
     incl. the cross-target case where one side never fired the LOADING pair.

Run: nix develop --command python3 tools/test_trace_studio_session.py
Exits non-zero on failure; prints OK on success.
"""
from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trace_studio.model import session as SESS    # noqa: E402
from trace_studio.model import timeline as TL     # noqa: E402

fails: list[str] = []


def want(cond: bool, msg: str):
    if not cond:
        fails.append(msg)


def main() -> int:
    # ── 1) v1 manifest migrates in memory ────────────────────────────────────
    v1 = {
        "schema": "trace-studio-v1", "session": "smoke",
        "n_frames": 48, "frame_range": [0, 47],
        "videos": {"port": "port.mp4", "retail": "retail.mp4", "diff": "diff.mp4"},
        "verdict": {"available": True, "exit_code": 0},
        "state": "state.jsonl", "call_trace": True, "caprange": [120, 48],
    }
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        (d / "session.json").write_text(json.dumps(v1))
        s = SESS.load_session(d)
        want(s.schema_version == 1, f"v1 schema_version should be 1, got {s.schema_version}")
        tl = s.timeline
        want(len(tl) == 1 and tl[0]["kind"] == "gameplay",
             f"v1 should synthesize 1 gameplay segment, got {tl}")
        g = tl[0]
        want(g.get("_synthetic") is True, "synthetic gameplay should be marked _synthetic")
        want(g["n_frames"] == 48 and g["frames"] == [0, 47],
             f"synthetic frames wrong: {g}")
        want(g["videos"] == v1["videos"], "synthetic videos mismatch")
        want(g["verdict"] == v1["verdict"], "synthetic verdict mismatch")
        want(s.load_seams == [] and len(s.gameplay_segments) == 1,
             "v1 should have no seams + 1 gameplay")

    # ── 2) build_timeline + v2 round-trip ────────────────────────────────────
    # A clean Continue/Load: BOTH sides fire one LOADING span; window is post-load.
    port = [{"anchor": "BOOT", "frame": 0},
            {"anchor": "LOADING_START", "frame": 100},
            {"anchor": "LOADING_END", "frame": 106},        # port load: 6 ticks
            {"anchor": "HOUSE_FREEROAM", "frame": 106}]
    retail = [{"anchor": "BOOT", "frame": 0},
              {"anchor": "LOADING_START", "frame": 200},
              {"anchor": "LOADING_END", "frame": 2911},     # retail turbo: 2711 ticks
              {"anchor": "HOUSE_FREEROAM", "frame": 2911}]
    tl = TL.build_timeline(port_firings=port, retail_firings=retail,
                           n_frames=48, frame_range=[0, 47],
                           videos={"port": "port.mp4"}, verdict=None,
                           state="state.jsonl", call_trace=True)
    want(len(tl) == 2, f"timeline should be [seam, gameplay], got {len(tl)} entries")
    seam = tl[0]
    want(seam["kind"] == "load_seam" and seam["port_ticks"] == 6
         and seam["retail_ticks"] == 2711,
         f"seam ticks wrong: {seam}")
    want(tl[1]["kind"] == "gameplay" and tl[1]["n_frames"] == 48,
         f"gameplay entry wrong: {tl[1]}")

    v1_fields = {"schema": "trace-studio-v1", "session": "rt", "n_frames": 48,
                 "frame_range": [0, 47], "videos": {"port": "port.mp4"},
                 "verdict": None, "state": "state.jsonl", "call_trace": True}
    man = SESS.make_v2_manifest(v1_fields, tl)
    want(man["schema_version"] == 2, "make_v2_manifest must stamp schema_version 2")
    want(man["n_frames"] == 48 and "videos" in man,
         "v2 manifest must remain a v1 superset (old UI fields present)")
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        SESS.write_session(d, man)
        s = SESS.load_session(d)
        want(s.schema_version == 2, "round-tripped manifest should be v2")
        want(len(s.load_seams) == 1 and len(s.gameplay_segments) == 1,
             f"v2 timeline wrong after round-trip: {s.timeline}")
        want(s.gameplay_segments[0].get("_synthetic") is None,
             "v2 gameplay must be the STORED entry, not synthesized")

    # ── 3) cross-target divergence: one side never fired LOADING ─────────────
    # Mirrors the towntest3 session (retail froze at BOOT in the New-Game prologue).
    port2 = [{"anchor": "LOADING_START", "frame": 152},
             {"anchor": "LOADING_END", "frame": 153},
             {"anchor": "LOADING_START", "frame": 287},
             {"anchor": "LOADING_END", "frame": 355}]
    retail2 = [{"anchor": "BOOT", "frame": 0}]               # only reached BOOT
    seams = TL.find_load_seams(port2, retail2)
    want(len(seams) == 2, f"should reconstruct 2 port seams, got {len(seams)}")
    want([s["port_ticks"] for s in seams] == [1, 68],
         f"port seam ticks wrong: {[s['port_ticks'] for s in seams]}")
    want(all(s["retail_ticks"] is None for s in seams),
         "retail (froze at BOOT) seams should have retail_ticks None")

    if fails:
        print("FAIL: trace_studio session\n  " + "\n  ".join(fails))
        return 1
    print("OK: trace_studio session (v1 migration + v2 round-trip + seam recon)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
