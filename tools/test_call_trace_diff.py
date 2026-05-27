#!/usr/bin/env python3
"""
tools/test_call_trace_diff.py — sanity tests for call_trace_diff.py.

Run with `nix develop --command python3 tools/test_call_trace_diff.py`.
Exits non-zero on failure; prints `OK` on success.

Covers:
  1. load_trace parses well-formed JSONL into per-frame Counters.
  2. load_trace raises on malformed JSONL.
  3. diff_frames partitions overlap / retail_only / port_only correctly.
  4. diff_frames preserves call counts per side.
  5. End-to-end CLI: identical input → 0 retail-only + 0 port-only.
  6. End-to-end CLI: retail-only divergence surfaces in output.
  7. --retail-frame-offset shifts retail frames before matching.
  8. Auto frame-pair resolution picks the first overlapping pair.
  9. --port-frame referencing a missing frame errors cleanly.
"""

from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
from collections import Counter
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def load_mod():
    spec = importlib.util.spec_from_file_location(
        "call_trace_diff", ROOT / "tools" / "call_trace_diff.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules["call_trace_diff"] = mod
    spec.loader.exec_module(mod)
    return mod


def write_trace(rows: list[dict]) -> Path:
    fd, p = tempfile.mkstemp(suffix=".jsonl")
    with open(fd, "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    return Path(p)


def run_main(mod, argv: list[str]) -> tuple[int, str, str]:
    buf_out, buf_err = io.StringIO(), io.StringIO()
    with redirect_stdout(buf_out), redirect_stderr(buf_err):
        try:
            rc = mod.main(argv)
        except SystemExit as e:
            rc = int(e.code) if e.code is not None else 0
    return rc, buf_out.getvalue(), buf_err.getvalue()


def main() -> int:
    mod = load_mod()
    failures: list[str] = []

    def chk(cond: bool, msg: str) -> None:
        if not cond:
            failures.append(msg)

    # ── 1. load_trace happy path ──────────────────────────────────────
    p1 = write_trace([
        {"va": 0x4536cb, "ret_va": 0x100, "frame": 5},
        {"va": 0x4536cb, "ret_va": 0x100, "frame": 5},
        {"va": 0x457714, "ret_va": 0x200, "frame": 5},
        {"va": 0x457714, "ret_va": 0x200, "frame": 6},
    ])
    by_f = mod.load_trace(p1)
    chk(set(by_f) == {5, 6}, f"load_trace frames: {sorted(by_f)}")
    chk(by_f[5][0x4536cb] == 2, f"frame 5 va 0x4536cb count: {by_f[5][0x4536cb]}")
    chk(by_f[5][0x457714] == 1, f"frame 5 va 0x457714 count: {by_f[5][0x457714]}")
    chk(by_f[6][0x457714] == 1, f"frame 6 va 0x457714 count: {by_f[6][0x457714]}")
    p1.unlink()

    # ── 2. load_trace malformed line ──────────────────────────────────
    p_bad = Path(tempfile.mkstemp(suffix=".jsonl")[1])
    p_bad.write_text('{"va":1,"ret_va":0,"frame":0}\nnot-json\n')
    raised = False
    try:
        mod.load_trace(p_bad)
    except SystemExit:
        raised = True
    chk(raised, "load_trace should SystemExit on malformed JSONL")
    p_bad.unlink()

    # ── 3. diff_frames partitions correctly ───────────────────────────
    r = Counter({0x100: 2, 0x200: 1, 0x300: 5})
    p = Counter({0x100: 2, 0x400: 1, 0x300: 4})
    diff = mod.diff_frames(r, p)
    overlap_vas    = sorted(va for va, _, _ in diff["overlap"])
    retail_only    = sorted(va for va, _, _ in diff["retail_only"])
    port_only      = sorted(va for va, _, _ in diff["port_only"])
    chk(overlap_vas    == [0x100, 0x300], f"overlap: {overlap_vas}")
    chk(retail_only    == [0x200],        f"retail_only: {retail_only}")
    chk(port_only      == [0x400],        f"port_only: {port_only}")

    # ── 4. diff_frames preserves counts ───────────────────────────────
    for va, rc, pc in diff["overlap"]:
        if va == 0x100:
            chk(rc == 2 and pc == 2, f"0x100 counts: {rc},{pc}")
        elif va == 0x300:
            chk(rc == 5 and pc == 4, f"0x300 counts: {rc},{pc}")
    for va, rc, pc in diff["retail_only"]:
        chk(rc == 1 and pc == 0, f"retail-only counts {va}: {rc},{pc}")
    for va, rc, pc in diff["port_only"]:
        chk(rc == 0 and pc == 1, f"port-only counts {va}: {rc},{pc}")

    # ── 5. CLI self-diff → 0 retail-only + 0 port-only ────────────────
    rows = [
        {"va": 0x4536cb, "ret_va": 0x10, "frame": 100},
        {"va": 0x457714, "ret_va": 0x20, "frame": 100},
    ]
    pa = write_trace(rows)
    pb = write_trace(rows)
    rc, out, err = run_main(mod, ["--retail", str(pa), "--port", str(pb)])
    chk(rc == 0, f"self-diff rc: {rc}, err={err!r}")
    chk("retail-only (port missing):  0" in out, f"self-diff retail-only:\n{out}")
    chk("port-only (retail skipped):  0" in out, f"self-diff port-only:\n{out}")
    pa.unlink(); pb.unlink()

    # ── 6. CLI retail-only divergence surfaces ────────────────────────
    pa = write_trace([
        {"va": 0x4536cb, "ret_va": 0x10, "frame": 100},
        {"va": 0x457714, "ret_va": 0x20, "frame": 100},  # only retail
    ])
    pb = write_trace([
        {"va": 0x4536cb, "ret_va": 0x10, "frame": 100},
    ])
    rc, out, _ = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                "--verbose"])
    chk(rc == 0, f"retail-only-divergence rc: {rc}")
    chk("retail-only (port missing):  1" in out,
        f"divergence not surfaced:\n{out}")
    chk("0x457714" in out, f"missing VA hex not surfaced:\n{out}")
    pa.unlink(); pb.unlink()

    # ── 7. --retail-frame-offset shifts retail frames ─────────────────
    pa = write_trace([
        {"va": 0x4536cb, "ret_va": 0x10, "frame": 11890},
    ])
    pb = write_trace([
        {"va": 0x4536cb, "ret_va": 0x10, "frame": 100},
    ])
    rc, out, _ = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                "--retail-frame", "11890",
                                "--retail-frame-offset", "-11790"])
    chk(rc == 0, f"offset rc: {rc}")
    chk("retail frame 11890 vs port frame 100" in out, f"offset header:\n{out}")
    pa.unlink(); pb.unlink()

    # ── 8. auto frame-pair resolution picks first overlap ─────────────
    pa = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 5},
        {"va": 0x100, "ret_va": 0, "frame": 6},
    ])
    pb = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 6},
        {"va": 0x100, "ret_va": 0, "frame": 7},
    ])
    rc, out, _ = run_main(mod, ["--retail", str(pa), "--port", str(pb)])
    chk(rc == 0, f"auto-pair rc: {rc}")
    chk("retail frame 6 vs port frame 6" in out, f"auto-pair header:\n{out}")
    pa.unlink(); pb.unlink()

    # ── 9. --port-frame missing frame errors ──────────────────────────
    pa = write_trace([{"va": 0x100, "ret_va": 0, "frame": 5}])
    pb = write_trace([{"va": 0x100, "ret_va": 0, "frame": 5}])
    rc, out, err = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                  "--retail-frame", "5",
                                  "--port-frame", "99"])
    chk(rc == 2, f"missing-frame rc: {rc}")
    chk("port frame 99 has no events" in err,
        f"missing-frame err:\n{err}")
    pa.unlink(); pb.unlink()

    # ── 10. first_frame_with_va finds earliest frame ──────────────────
    by_f = {
        5: Counter({0x100: 1}),
        10: Counter({0x100: 1, 0x200: 1}),
        15: Counter({0x200: 1}),
    }
    chk(mod.first_frame_with_va(by_f, 0x100) == 5, "first frame 0x100")
    chk(mod.first_frame_with_va(by_f, 0x200) == 10, "first frame 0x200")
    chk(mod.first_frame_with_va(by_f, 0x999) is None, "missing VA → None")

    # ── 11. --align-on-first anchors each side independently ──────────
    pa = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 50},
        {"va": 0x200, "ret_va": 0, "frame": 50},   # anchor on retail = 50
    ])
    pb = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 3},
        {"va": 0x200, "ret_va": 0, "frame": 3},    # anchor on port = 3
    ])
    rc, out, _ = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                "--align-on-first", "0x200"])
    chk(rc == 0, f"align rc: {rc}")
    chk("retail anchor: frame 50" in out, f"retail anchor missing:\n{out}")
    chk("port anchor:   frame 3"  in out, f"port anchor missing:\n{out}")
    chk("retail frame 50 vs port frame 3" in out, f"align header:\n{out}")
    pa.unlink(); pb.unlink()

    # ── 12. --frame-offset advances both anchors ──────────────────────
    pa = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 50},
        {"va": 0x100, "ret_va": 0, "frame": 52},
    ])
    pb = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 3},
        {"va": 0x100, "ret_va": 0, "frame": 5},
    ])
    rc, out, _ = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                "--align-on-first", "0x100",
                                "--frame-offset", "2"])
    chk(rc == 0, f"frame-offset rc: {rc}")
    chk("frame-offset:  +2" in out, f"frame-offset metadata:\n{out}")
    chk("retail frame 52 vs port frame 5" in out, f"offset header:\n{out}")
    pa.unlink(); pb.unlink()

    # ── 13. --align-on-first errors when VA missing on either side ────
    pa = write_trace([{"va": 0x100, "ret_va": 0, "frame": 5}])
    pb = write_trace([{"va": 0x200, "ret_va": 0, "frame": 5}])
    rc, out, err = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                  "--align-on-first", "0x999"])
    chk(rc == 2, f"missing-anchor rc: {rc}")
    chk("never fires on retail side" in err,
        f"missing-anchor retail err:\n{err}")
    pa.unlink(); pb.unlink()

    pa = write_trace([{"va": 0x100, "ret_va": 0, "frame": 5}])
    pb = write_trace([{"va": 0x200, "ret_va": 0, "frame": 5}])
    rc, out, err = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                  "--align-on-first", "0x100"])
    chk(rc == 2, f"missing-port-anchor rc: {rc}")
    chk("never fires on port side" in err,
        f"missing-anchor port err:\n{err}")
    pa.unlink(); pb.unlink()

    # ── 14. --align-on-first + explicit --retail-frame overrides ──────
    pa = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 5},
        {"va": 0x100, "ret_va": 0, "frame": 99},
    ])
    pb = write_trace([
        {"va": 0x100, "ret_va": 0, "frame": 5},
    ])
    rc, out, _ = run_main(mod, ["--retail", str(pa), "--port", str(pb),
                                "--align-on-first", "0x100",
                                "--retail-frame", "99"])
    chk(rc == 0, f"override rc: {rc}")
    # Retail explicit, port stays at its anchor (frame 5)
    chk("retail frame 99 vs port frame 5" in out, f"override header:\n{out}")
    pa.unlink(); pb.unlink()

    if failures:
        print(f"FAIL ({len(failures)} test(s)):")
        for f in failures:
            print(f"  - {f}")
        return 1
    print("OK (14 tests)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
