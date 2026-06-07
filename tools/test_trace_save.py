#!/usr/bin/env python3
"""test_trace_save.py — resolve_save handles BOTH save-ref shapes.

A {savefile} ref can point at a gzip blob (<sha>.sav.gz — the distilled form) OR a
raw uncompressed .save.bin (a fresh recording's ref, before distillation). The retail
drive (trace_studio/drive/retail.py) resolves the save against the ORIGINAL recording,
so it sees the raw ref — which used to crash with BadGzipFile because resolve_save
unconditionally gzip.open'd the ref. This guards that regression.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import trace_save  # noqa: E402

ARENA = trace_save.SAVE_ARENA_BYTES
# real save-arena header magic 0x341944da → first bytes da 44 19 34 (the b'\xdaD' that
# used to surface in the BadGzipFile message).
RAW_HEAD = b"\xda\x44\x19\x34"


def _make_raw(path: Path) -> None:
    with open(path, "wb") as f:
        f.write(RAW_HEAD)
        f.truncate(ARENA)          # sparse-extend to the full arena size


def main() -> int:
    with tempfile.TemporaryDirectory() as td:
        d = Path(td)
        raw = d / "boot.save.bin"
        _make_raw(raw)

        # ── A. RAW ref (the retail-drive / fresh-recording case) ────────────────
        raw_trace = d / "rec.raw.jsonl"
        raw_trace.write_text('{"savefile": "boot.save.bin"}\n{"frame":0,"mask":0}\n')
        got = trace_save.resolve_save(raw_trace)
        assert got is not None, "raw ref resolved to None"
        assert Path(got).stat().st_size == ARENA, f"raw size {Path(got).stat().st_size}"
        assert open(got, "rb").read(4) == RAW_HEAD, "raw passthrough corrupted the bytes"
        # passthrough: returns the raw file itself (no needless temp copy)
        assert Path(got).resolve() == raw.resolve(), f"raw ref not passed through: {got}"

        # ── B. GZIP blob ref (the distilled / port case) ────────────────────────
        store = d / "_saves"
        sha, blob = trace_save.store_save(raw, store)
        assert blob.read_bytes()[:2] == b"\x1f\x8b", "store_save didn't write gzip"
        gz_trace = d / "dist.trace.jsonl"
        gz_trace.write_text(
            f'{{"savefile": "_saves/{sha}{trace_save.SAVE_SUFFIX}"}}\n')
        got2 = trace_save.resolve_save(gz_trace)
        assert got2 is not None and Path(got2).stat().st_size == ARENA, "gzip decode size"
        assert open(got2, "rb").read(4) == RAW_HEAD, "gzip decode mismatch"

        # ── C. neither gzip nor a full-arena raw → a clear error ────────────────
        junk = d / "junk.bin"
        junk.write_bytes(b"\xda\x44 not a save")
        bad_trace = d / "bad.jsonl"
        bad_trace.write_text('{"savefile": "junk.bin"}\n')
        try:
            trace_save.resolve_save(bad_trace)
            raise AssertionError("expected ValueError for a non-gzip non-arena ref")
        except ValueError:
            pass

        # ── D. @fresh / no-ref sentinels still pass through ─────────────────────
        fresh = d / "fresh.jsonl"
        fresh.write_text('{"savefile": "@fresh"}\n')
        assert trace_save.resolve_save(fresh) == trace_save.FRESH_REF
        none_trace = d / "none.jsonl"
        none_trace.write_text('{"frame":0,"mask":0}\n')
        assert trace_save.resolve_save(none_trace) is None

    print("OK: trace_save.resolve_save (raw passthrough + gzip decode + guards)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
