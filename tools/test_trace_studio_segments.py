#!/usr/bin/env python3
"""tools/test_trace_studio_segments.py — golden cross-check for the alignment core.

trace_studio.model.segments is a Python port of trace_studio_web/align.mjs (the JS
twin the browser timeline uses). This pins them together: a SHARED fixture
(align.fixture.json) is run through BOTH — the JS via align.dump.mjs (node), the
Python via segments — and the normalized projections must be byte-identical. It
ALSO asserts ground-truth values, so the two drifting *identically* is still caught.

Run: nix develop --command python3 tools/test_trace_studio_segments.py
Exits non-zero on failure; prints OK on success. (node is required for the JS half;
if absent the JS cross-check is skipped with a warning but the ground-truth half
still runs.)
"""
from __future__ import annotations

import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from trace_studio.model import segments as S  # noqa: E402

WEB = ROOT / "tools" / "trace_studio_web"
FIXTURE = WEB / "align.fixture.json"
DUMPER = WEB / "align.dump.mjs"


def py_projection(fx: dict) -> dict:
    """The SAME normalized projection align.dump.mjs emits, built from segments.py."""
    segs = S.parse_segments(fx["trace"])
    rb = S.resolve_bases(segs, fx["retail"])
    pb = S.resolve_bases(segs, fx["port"])
    rep = S.divergence_report(segs, fx["port"], fx["retail"], fx["sync_seg"])
    sf = S.side_layout(segs, fx["retail"], fx["sync_seg"])["sync_frame"]
    rs_r = S.resolve_side(segs, fx["retail"])
    rs_p = S.resolve_side(segs, fx["port"])
    elay = S.editor_layout(segs, fx["port"], fx["retail"],
                           window_side="retail", window_start_abs=85, window_end_abs=140)
    return {
        "segments": [{"wait": s["wait_anchor"],
                      "items": [[i["kind"], i["frame"]] for i in s["items"]]}
                     for s in segs],
        "retail_bases": [[b["base"], b["ok"]] for b in rb],
        "port_bases": [[b["base"], b["ok"]] for b in pb],
        "divergence": [[d["seg"], d["anchor"], d["port_rel"], d["retail_rel"]]
                       for d in rep],
        "item_abs": {"retail": S.item_abs(segs[3]["items"][0], 3, rb),
                     "port": S.item_abs(segs[3]["items"][0], 3, pb)},
        "roundtrip": {"absToX": S.abs_to_x(111, sf, 2),
                      "xToAbs": S.x_to_abs(60, sf, 2)},
        "resolve_side": {
            "retail": {"bases": [[b["base"], b["ok"]] for b in rs_r["bases"]],
                       "placements": [[p["seg"], p["rel"]] for p in rs_r["placements"]]},
            "port": {"bases": [[b["base"], b["ok"]] for b in rs_p["bases"]],
                     "placements": [[p["seg"], p["rel"]] for p in rs_p["placements"]]},
        },
        "editor_layout": {"X": elay["X"], "W": elay["W"], "ext": elay["ext"],
                          "window": elay["window"]},
        "band_at": [[pos, S.band_at(elay["X"], pos)["seg"], S.band_at(elay["X"], pos)["rel"]]
                    for pos in (35, 2515, 3000, -3)],
        "abs_to_band": [[a, S.abs_to_band(a, rs_r["bases"])["seg"],
                         S.abs_to_band(a, rs_r["bases"])["rel"]] for a in (85, 140, 200)],
    }


def main() -> int:
    fails: list[str] = []
    fx = json.loads(FIXTURE.read_text())
    proj = py_projection(fx)

    # ── 1) ground-truth pins (mirrors align.test.mjs) ────────────────────────
    def want(cond: bool, msg: str):
        if not cond:
            fails.append(msg)

    want(proj["segments"][3]["items"] == [["phasepin", 20], ["rngseed", 20],
                                          ["input", 0]],
         f"seg3 items wrong: {proj['segments'][3]['items']}")
    want([s["wait"] for s in proj["segments"]]
         == [None, "NEW_GAME", "CONV_POSE_START", "LOADING_END"],
         "segment wait anchors wrong")
    want(proj["retail_bases"] == [[0, True], [81, True], [108, True], [111, True]],
         f"retail bases wrong: {proj['retail_bases']}")
    # port: NEW_GAME@261, CONV_POSE_START resolves @2750 (after HF), LOADING_END
    # is UNRESOLVED (fired @2726, before CONV_POSE_START@2750) ⇒ the divergence.
    want(proj["port_bases"][1] == [261, True], "port NEW_GAME != 261")
    want(proj["port_bases"][2] == [2750, True], "port CONV_POSE_START != 2750")
    want(proj["port_bases"][3][1] is False, "port LOADING_END should be UNRESOLVED")
    conv = proj["divergence"][2]
    want(conv[2] == 2489 and conv[3] == 27,
         f"CONV_POSE_START divergence wrong: portRel={conv[2]} retailRel={conv[3]}")
    want(proj["item_abs"] == {"retail": 131, "port": 2770},
         f"item_abs wrong: {proj['item_abs']}")
    want(proj["roundtrip"] == {"absToX": 60, "xToAbs": 111},
         f"roundtrip wrong: {proj['roundtrip']}")
    # band model: retail places every anchor on a band edge [0,1,2,3,3]; the port fired
    # LOADING_END@2726 BEFORE CONV_POSE_START@2750, so its chips land WITHIN seg 1 far to the
    # right (rel 2465) — the divergence shown as a wide band, not frozen onto one frame.
    want(proj["resolve_side"]["retail"]["placements"] == [[0, 0], [1, 0], [2, 0], [3, 0], [3, 0]],
         f"retail placements wrong: {proj['resolve_side']['retail']['placements']}")
    want(proj["resolve_side"]["port"]["placements"] == [[0, 0], [1, 0], [1, 2465], [1, 2465], [2, 0]],
         f"port placements wrong: {proj['resolve_side']['port']['placements']}")
    # bands never overlap: X[k] ≥ X[k-1]+W[k-1]; the captured window (abs 85→140 on retail)
    # spans seg1→seg3 and widens seg3 from its content (20) to cover its slice (29 → ext 30).
    xs, ws = proj["editor_layout"]["X"], proj["editor_layout"]["W"]
    want(all(xs[k] >= xs[k - 1] + ws[k - 1] for k in range(1, len(xs))),
         f"bands overlap: X={xs} W={ws}")
    want(proj["editor_layout"] == {
        "X": [0, 30, 2515, 2540], "W": [14, 2469, 9, 34], "ext": [10, 2465, 5, 30],
        "window": {"startSeg": 1, "startRel": 4, "endSeg": 3, "endRel": 29}},
         f"editor_layout wrong: {proj['editor_layout']}")
    want(proj["band_at"] == [[35, 1, 5], [2515, 2, 0], [3000, 3, 460], [-3, 0, -3]],
         f"band_at wrong: {proj['band_at']}")
    # abs_to_band: 85→seg1+4, 140→seg3+29; 200 is past the last base (111) so it stays in
    # seg3 (skips nothing here — all resolved) at rel 89.
    want(proj["abs_to_band"] == [[85, 1, 4], [140, 3, 29], [200, 3, 89]],
         f"abs_to_band wrong: {proj['abs_to_band']}")

    # ── 2) JS twin cross-check (node) ────────────────────────────────────────
    node = shutil.which("node")
    if not node:
        print("WARN: node not found — skipping the align.mjs JS cross-check "
              "(ground-truth half still ran)", file=sys.stderr)
    else:
        r = subprocess.run([node, str(DUMPER)], capture_output=True, text=True)
        if r.returncode != 0:
            fails.append(f"align.dump.mjs failed: {r.stderr.strip()[:400]}")
        else:
            try:
                js = json.loads(r.stdout)
            except json.JSONDecodeError as e:
                fails.append(f"align.dump.mjs emitted non-JSON: {e}")
                js = None
            if js is not None and js != proj:
                fails.append("JS twin (align.mjs) and Python (segments.py) DIVERGE:\n"
                             f"    js  = {json.dumps(js, sort_keys=True)}\n"
                             f"    py  = {json.dumps(proj, sort_keys=True)}")

    if fails:
        print("FAIL: trace_studio segments\n  " + "\n  ".join(fails))
        return 1
    print(f"OK: trace_studio segments ({'with' if node else 'without'} JS cross-check)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
