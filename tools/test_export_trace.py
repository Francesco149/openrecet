#!/usr/bin/env python3
"""
tools/test_export_trace.py — regression guard for the export_trace RAW path.

The TAS-save commits grew distill_trace.load_raw()'s return tuple from 7 to 9
values (added savefile, save_writes), but export_trace.resolve_trace kept
unpacking 7 — so every `export_trace.py <REC>.raw.jsonl` crashed with
`ValueError: too many values to unpack (expected 7)`. Trace Studio dodged it by
only ever feeding already-distilled traces, so it went unnoticed. This pins:

  1. distill_trace.load_raw() returns exactly 9 values.
  2. export_trace.resolve_trace() handles a RAW (anchored + FLAT) without
     ValueError and returns the (text, ops, savefile) 3-tuple.
  3. a RAW's boot {savefile} dict threads through as the 3rd element.

Run: nix develop --command python3 tools/test_export_trace.py
Exits non-zero on failure; prints OK on success.
"""
from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name: str):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


def write_raw(d: Path, *, with_anchors: bool = True, with_save: bool = False) -> Path:
    """A minimal in-engine TAS raw recording (the shape distill_trace.load_raw reads)."""
    rows: list[dict] = [{"_rec": "openrecet-tas-raw-v1", "rng_seed_at_start": 4259672399}]
    if with_save:
        (d / "boot.save.bin").write_bytes(b"\x00" * 4096)   # tiny stand-in
        rows.append({"savefile": "boot.save.bin", "sha256": None, "size": 4096})
    rows += [{"frame": 0, "buttons": "0x0000"},
             {"frame": 10, "buttons": "0x0010"},     # Z press
             {"frame": 14, "buttons": "0x0000"},
             {"frame": 60, "buttons": "0x0008"},     # DOWN
             {"frame": 66, "buttons": "0x0000"}]
    if with_anchors:
        rows += [{"anchor": "BOOT", "frame": 0, "gframe": 0, "rng": 1},
                 {"anchor": "FREEROAM_START", "frame": 50, "gframe": 4200, "rng": 99}]
    p = d / "rec.raw.jsonl"
    p.write_text("".join(json.dumps(r) + "\n" for r in rows))
    return p


def main() -> int:
    distill = load("distill_trace")
    export = load("export_trace")
    fails: list[str] = []

    with tempfile.TemporaryDirectory() as td:
        d = Path(td)

        # 1) load_raw arity is exactly 9 (the value that drifted under the bug).
        ret = distill.load_raw(str(write_raw(d)))
        if len(ret) != 9:
            fails.append(f"load_raw returned {len(ret)} values, expected 9")

        # 2) resolve_trace on an anchored raw → 3-tuple, no ValueError, anchor-gated.
        raw = write_raw(d)
        try:
            text, ops, savefile = export.resolve_trace(raw, house_segtrace=False)
        except ValueError as e:                       # the exact regression
            fails.append(f"resolve_trace raised ValueError (the regression!): {e}")
            text, ops, savefile = "", [], None
        if not any(isinstance(o, dict) and "wait" in o for o in ops):
            fails.append("anchored raw did not produce a {wait} sync point")
        if savefile is not None:
            fails.append(f"no-save raw returned savefile={savefile!r}, expected None")

        # 3) FLAT fallback (no anchors) still resolves.
        fd = d / "flat"; fd.mkdir()
        try:
            _t2, ops2, _sf2 = export.resolve_trace(
                write_raw(fd, with_anchors=False), house_segtrace=False)
        except Exception as e:                         # noqa: BLE001
            fails.append(f"FLAT raw resolve_trace raised {e!r}")
            ops2 = []
        if not any(isinstance(o, dict) and "frame" in o for o in ops2):
            fails.append("FLAT raw produced no input ops")

        # 4) a raw's boot savefile dict threads through as the 3rd element.
        sd = d / "withsave"; sd.mkdir()
        _t, _o, sf = export.resolve_trace(
            write_raw(sd, with_save=True), house_segtrace=False)
        if not (isinstance(sf, dict) and sf.get("path") == "boot.save.bin"):
            fails.append(f"raw savefile not threaded through: {sf!r}")

    if fails:
        for f in fails:
            print("FAIL:", f, file=sys.stderr)
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
