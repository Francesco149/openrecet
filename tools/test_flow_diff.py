#!/usr/bin/env python3
"""
tools/test_flow_diff.py — sanity tests for flow_diff.py's --field-timeline
(the per-field state-divergence localizer) + its benign-field annotation.

Run with `nix develop --command python3 tools/test_flow_diff.py`.
Exits non-zero on failure; prints `OK` on success.

Covers:
  1. build_field_timeline: all-aligned fields → no first-divergence.
  2. build_field_timeline: a divergent field → first (frame, field) pinned.
  3. benign-marked field that diverges → track.benign set, not a real ✗.
  4. _max_occ_per_frame counts the busiest frame per side.
  5. CLI --field-timeline: real divergence → exit 1, names field + first frame.
  6. CLI --field-timeline: only a benign divergence → exit 0, clean verdict.
  7. CLI --field-timeline: auto mode skips >1×/frame draw VAs (points to render_diff).
  8. float fields compare within --eps (no spurious LSB divergence).
  9. --align-anchor: constant-offset alignment from a shared anchor (cutscene
     verdict where db054 is absent); + --align-field/--align-anchor exclusivity.
 10. verdict draw-VA skip stays correct under an --align-field rekey that
     collapses several raw frames onto one clock key (the _max_occ_any fix).
"""

from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load_mod():
    spec = importlib.util.spec_from_file_location(
        "flow_diff", ROOT / "tools" / "flow_diff.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules["flow_diff"] = mod
    spec.loader.exec_module(mod)
    return mod


def write_trace(rows: list[dict]) -> Path:
    fd, p = tempfile.mkstemp(suffix=".jsonl")
    with open(fd, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    return Path(p)


def write_spec(spec: dict) -> Path:
    fd, p = tempfile.mkstemp(suffix=".json")
    with open(fd, "w") as f:
        json.dump(spec, f)
    return Path(p)


def stub(va: int, frame: int, seq: int, f: dict) -> dict:
    return {"va": va, "ret_va": 0, "frame": frame, "seq": seq, "stub": True, "f": f}


def run_main(mod, argv: list[str]) -> tuple[int, str, str]:
    buf_out, buf_err = io.StringIO(), io.StringIO()
    with redirect_stdout(buf_out), redirect_stderr(buf_err):
        try:
            rc = mod.main(argv)
        except SystemExit as e:
            # Mirror CPython: None → 0, int → that code, str → message to
            # stderr + exit 1 (the SystemExit("msg") error paths).
            if e.code is None:
                rc = 0
            elif isinstance(e.code, int):
                rc = e.code
            else:
                buf_err.write(str(e.code))
                rc = 1
    return rc, buf_out.getvalue(), buf_err.getvalue()


SIM = 0x49a59e


def base_spec(extra_fields=None) -> dict:
    fields = [
        {"name": "cursor_pos", "src": "global", "va": "0x1", "type": "i32"},
        {"name": "select_phase", "src": "global", "va": "0x2", "type": "i32"},
    ]
    if extra_fields:
        fields += extra_fields
    return {"fields": {hex(SIM): {"name": "scene_title_sim", "fields": fields}}}


def main() -> int:
    mod = load_mod()
    failures: list[str] = []

    def chk(cond: bool, msg: str) -> None:
        if not cond:
            failures.append(msg)

    # ── 1+2. build_field_timeline: aligned vs first-divergence ─────────
    retail = {fr: [stub(SIM, fr, 1, {"cursor_pos": 1,
                                     "select_phase": 15 if fr < 3 else 0})]
              for fr in range(6)}
    # port: cursor_pos tracks; select_phase STUCK at 15 (the soft-lock shape).
    port = {fr: [stub(SIM, fr, 1, {"cursor_pos": 1, "select_phase": 15})]
            for fr in range(6)}
    common = list(range(6))
    tracks, max_occ = mod.build_field_timeline(
        SIM, retail, port, common, eps=1e-4, benign=set(), reasons={},
        field_order=["cursor_pos", "select_phase"])
    by = {t.field: t for t in tracks}
    chk(max_occ == 1, f"max_occ once/frame: {max_occ}")
    chk(by["cursor_pos"].first is None, "cursor_pos should be aligned")
    chk(by["select_phase"].first is not None, "select_phase should diverge")
    chk(by["select_phase"].first[0] == 3,
        f"select_phase first-div frame should be 3: {by['select_phase'].first}")
    chk(by["select_phase"].first[2:] == (0, 15),
        f"select_phase first-div values retail=0 port=15: {by['select_phase'].first}")
    chk(by["select_phase"].n_div == 3,
        f"select_phase diverges on frames 3,4,5: {by['select_phase'].n_div}")

    # ── 3. benign-marked field surfaces benign, not real ──────────────
    tracks_b, _ = mod.build_field_timeline(
        SIM, retail, port, common, eps=1e-4,
        benign={(SIM, "select_phase")}, reasons={(SIM, "select_phase"): "why"},
        field_order=["cursor_pos", "select_phase"])
    sp = {t.field: t for t in tracks_b}["select_phase"]
    chk(sp.benign and sp.first is not None and sp.reason == "why",
        f"benign track should keep first-div + reason: {sp}")

    # ── 4. _max_occ_per_frame ─────────────────────────────────────────
    DRAW = 0x404efc
    r2 = {0: [stub(DRAW, 0, i, {"dx": i}) for i in range(3)]}
    p2 = {0: [stub(DRAW, 0, i, {"dx": i}) for i in range(5)]}
    chk(mod._max_occ_per_frame(DRAW, r2, p2, [0]) == 5,
        "max_occ should pick the busier (port) side = 5")

    # ── 5. CLI: real divergence → exit 1, names field + frame ─────────
    spc = write_spec(base_spec())
    pr = write_trace([row for fr in range(6) for row in retail[fr]])
    pp = write_trace([row for fr in range(6) for row in port[fr]])
    rc, out, err = run_main(mod, ["--retail", str(pr), "--port", str(pp),
                                  "--field-timeline", "--spec", str(spc)])
    chk(rc == 1, f"real-divergence exit should be 1: rc={rc} err={err!r}")
    chk("select_phase" in out and "✗" in out, f"should flag select_phase:\n{out}")
    chk("first @3" in out, f"should pin first frame 3:\n{out}")
    chk("cursor_pos" in out and "✓ aligned" in out, f"cursor_pos aligned:\n{out}")

    # ── 6. CLI: only-benign divergence → exit 0, clean verdict ────────
    spc_b = write_spec(base_spec(extra_fields=None))
    # mark select_phase benign in the spec
    sd = json.loads(spc_b.read_text())
    for fld in sd["fields"][hex(SIM)]["fields"]:
        if fld["name"] == "select_phase":
            fld["benign"] = True
            fld["reason"] = "stale flag, no effect"
    spc_b.write_text(json.dumps(sd))
    rc, out, _ = run_main(mod, ["--retail", str(pr), "--port", str(pp),
                                "--field-timeline", "--spec", str(spc_b)])
    chk(rc == 0, f"benign-only exit should be 0: rc={rc}")
    chk("benign-accepted" in out and "stale flag" in out,
        f"benign field should surface with reason:\n{out}")
    chk("✓ all fields aligned (benign-accepted" in out,
        f"verdict should be clean-benign:\n{out}")

    # ── 7. CLI auto mode skips >1×/frame draw VAs ─────────────────────
    spc2 = write_spec({"fields": {
        hex(SIM): base_spec()["fields"][hex(SIM)],
        hex(DRAW): {"name": "render_quad_add",
                    "fields": [{"name": "dx", "src": "arg", "va": "0", "type": "f32"}]},
    }})
    # both sides identical sim + a 3-deep draw VA → draw VA must be skipped,
    # sim still compared (and here identical → aligned).
    rows_r = [row for fr in range(6) for row in retail[fr]] + \
             [stub(DRAW, 0, i, {"dx": i}) for i in range(3)]
    rows_p = [row for fr in range(6) for row in retail[fr]] + \
             [stub(DRAW, 0, i, {"dx": i}) for i in range(3)]
    pr2, pp2 = write_trace(rows_r), write_trace(rows_p)
    rc, out, _ = run_main(mod, ["--retail", str(pr2), "--port", str(pp2),
                                "--field-timeline", "--spec", str(spc2)])
    chk("skipped >1×/frame draw VAs" in out and "render_quad_add" in out,
        f"draw VA should be skipped with pointer:\n{out}")
    chk("scene_title_sim" in out, f"sim stub should still be shown:\n{out}")
    chk(rc == 0, f"identical-with-skip exit 0: rc={rc}")

    # ── 8. float fields within --eps don't spuriously diverge ─────────
    rf = {0: [stub(SIM, 0, 1, {"cursor_pos": 1, "select_phase": 0,
                               "x": 161.946})]}
    pf = {0: [stub(SIM, 0, 1, {"cursor_pos": 1, "select_phase": 0,
                               "x": 161.94600001})]}
    tr, _ = mod.build_field_timeline(
        SIM, rf, pf, [0], eps=1e-4, benign=set(), reasons={},
        field_order=["cursor_pos", "select_phase", "x"])
    xt = {t.field: t for t in tr}["x"]
    chk(xt.first is None, f"float within eps should not diverge: {xt.first}")

    # ── 9. --align-anchor: constant-offset alignment from a shared anchor ──
    # retail SIM at frames 100..105; port identical SIM at 0..5 (offset -100,
    # the load-stretch shape). A shared anchor SYNC (retail@102, port@2) gives
    # the offset; --frame-from clips a leading frame.
    def sv(fr):
        return {"cursor_pos": fr % 3, "select_phase": 0}
    r9 = write_trace([stub(SIM, 100 + fr, 1, sv(fr)) for fr in range(6)])
    p9 = write_trace([stub(SIM, fr, 1, sv(fr)) for fr in range(6)])
    ra9 = write_trace([{"anchor": "BOOT", "frame": 0}, {"anchor": "SYNC", "frame": 102}])
    pa9 = write_trace([{"anchor": "BOOT", "frame": 0}, {"anchor": "SYNC", "frame": 2}])
    # unaligned: raw frames (100.. vs 0..) don't intersect → no verdict.
    rc, out, err = run_main(mod, ["--retail", str(r9), "--port", str(p9),
                                  "--verdict", "--spec", str(spc)])
    chk(rc != 0 and "no common" in (out + err).lower(),
        f"unaligned offset traces should not share frames:\n{out}{err}")
    # aligned by SYNC: offset 100 → identical SIM lines up → PHASE-CLEAN, exit 0.
    rc, out, err = run_main(mod, ["--retail", str(r9), "--port", str(p9),
                                  "--verdict", "--align-anchor", "SYNC",
                                  "--retail-anchors", str(ra9),
                                  "--port-anchors", str(pa9), "--spec", str(spc)])
    chk(rc == 0 and "PHASE-CLEAN" in out,
        f"--align-anchor SYNC should align identical SIM → exit 0:\n{out}{err}")
    chk("--align-field and --align-anchor are mutually exclusive" in
        run_main(mod, ["--retail", str(r9), "--port", str(p9), "--verdict",
                       "--align-field", "x", "--align-anchor", "SYNC"])[2],
        "align-field + align-anchor should be rejected")

    # ── 10. verdict draw-VA skip is robust under --align-field rekey ───────
    # The collapse regression: a clock that REPEATS every 2 frames makes rekey
    # bucket 2 raw frames per key, so a once/frame state VA shows 2 occurrences
    # PER KEY. The draw-VA test must run on RAW frames (_max_occ_any) so the
    # state VA is NOT misclassified as a >1×/frame draw VA and dropped.
    rows10 = ([stub(SIM, fr, 1, {"cursor_pos": 1, "ck": fr // 2}) for fr in range(4)]
              + [stub(DRAW, fr, 10 + i, {"dx": i}) for fr in range(4) for i in range(3)])
    r10, p10 = write_trace(rows10), write_trace(rows10)
    spc10 = write_spec({"fields": {
        hex(SIM): {"name": "scene_title_sim", "fields": [
            {"name": "cursor_pos", "src": "global", "va": "0x1", "type": "i32"},
            {"name": "ck", "src": "global", "va": "0x2", "type": "i32"}]},
        hex(DRAW): {"name": "render_quad_add", "fields": [
            {"name": "dx", "src": "arg", "va": "0", "type": "f32"}]}}})
    rc, out, err = run_main(mod, ["--retail", str(r10), "--port", str(p10),
                                  "--verdict", "--align-field", "ck",
                                  "--spec", str(spc10)])
    chk("render_quad_add" in out and "skipped >1" in out,
        f"draw VA must still be skipped under rekey:\n{out}")
    chk("scene_title_sim" in out and "cursor_pos" in out,
        f"once/frame state VA must NOT be dropped under rekey collapse:\n{out}{err}")
    chk(mod._max_occ_any(DRAW, {0: rows10[4:7]}, {0: []}) == 3,
        "_max_occ_any counts a side's busiest frame")

    for p in (spc, spc_b, spc2, pr, pp, pr2, pp2,
              r9, p9, ra9, pa9, r10, p10, spc10):
        p.unlink()

    if failures:
        print(f"FAIL ({len(failures)} test(s)):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("OK (10 tests)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
