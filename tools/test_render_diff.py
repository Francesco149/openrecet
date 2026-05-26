#!/usr/bin/env python3
"""
tools/test_render_diff.py — sanity tests for `render_diff.py`.

Run with `nix develop --command python3 tools/test_render_diff.py`.
Exits non-zero on failure; prints `OK` on success.

Covers:
  1. Self-diff: a synthetic trace versus itself → 0 diff blocks.
  2. Single-arg divergence (different SetRenderState value).
  3. Insertion: port has one extra event mid-frame.
  4. Coalesce collapse: 5 redundant SetRenderState writes collapse to 1.
  5. Coalesce reset across a Draw call.
  6. Scope filter narrows by ret_va.
  7. End-to-end CLI: writes two tmp JSONL files, invokes main(), expects
     exit code 1 + a non-empty divergence print.
"""

from __future__ import annotations

import importlib.util
import io
import json
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def load_render_diff():
    spec = importlib.util.spec_from_file_location(
        "render_diff", ROOT / "tools" / "render_diff.py")
    mod = importlib.util.module_from_spec(spec)
    sys.modules["render_diff"] = mod
    spec.loader.exec_module(mod)
    return mod


# ── fixture helpers ───────────────────────────────────────────────────────


def srs(state: int, value: int, *, ret_va: int = 0x1000,
        frame: int = 0) -> dict:
    return {"op": "SetRenderState",
            "args": {"state": state, "value": value},
            "ret_va": ret_va, "frame": frame}

def tss(stage: int, t: int, value: int, *, ret_va: int = 0x1000,
        frame: int = 0) -> dict:
    return {"op": "SetTextureStageState",
            "args": {"stage": stage, "type": t, "value": value},
            "ret_va": ret_va, "frame": frame}

def stex(stage: int, tex: str, *, ret_va: int = 0x1000,
         frame: int = 0) -> dict:
    return {"op": "SetTexture",
            "args": {"stage": stage, "texture": tex},
            "ret_va": ret_va, "frame": frame}

def draw(*, ret_va: int = 0x1000, frame: int = 0) -> dict:
    return {"op": "DrawPrimitive",
            "args": {"prim_type": 4, "start_vertex": 0, "prim_count": 1},
            "ret_va": ret_va, "frame": frame}

def write_jsonl(path: Path, events: list[dict]) -> None:
    with path.open("w") as f:
        for e in events:
            f.write(json.dumps(e, separators=(",", ":")) + "\n")


# ── tests ─────────────────────────────────────────────────────────────────


def test_self_diff(rd) -> None:
    evts = [srs(7, 0), tss(0, 4, 4), draw()]
    fd = rd.diff_frame(0, evts, evts.copy())
    assert not fd.diverged, f"self-diff produced blocks: {fd.blocks}"


def test_single_value_divergence(rd) -> None:
    r = [srs(7, 0), tss(0, 4, 4), draw()]
    p = [srs(7, 1), tss(0, 4, 4), draw()]
    fd = rd.diff_frame(0, r, p)
    assert fd.diverged
    assert len(fd.blocks) == 1, f"expected 1 block, got {len(fd.blocks)}"
    blk = fd.blocks[0]
    assert blk["tag"] == "replace"
    assert blk["retail"] == [r[0]]
    assert blk["port"] == [p[0]]


def test_insertion(rd) -> None:
    r = [srs(7, 0),                tss(0, 4, 4), draw()]
    p = [srs(7, 0), srs(8, 9, ret_va=0x2000), tss(0, 4, 4), draw()]
    fd = rd.diff_frame(0, r, p)
    assert fd.diverged
    assert len(fd.blocks) == 1
    blk = fd.blocks[0]
    assert blk["tag"] == "insert"
    assert blk["retail"] == []
    assert len(blk["port"]) == 1
    assert blk["port"][0]["args"]["state"] == 8


def test_coalesce_dup_state(rd) -> None:
    evts = [
        srs(7, 0),                              # keep — first
        srs(7, 0, ret_va=0x1004),               # drop — same value
        srs(7, 0, ret_va=0x1008),               # drop
        srs(7, 1, ret_va=0x100c),               # keep — value changed
        srs(7, 1, ret_va=0x1010),               # drop
    ]
    out = rd.collapse_redundant(evts)
    assert len(out) == 2, f"expected 2, got {len(out)}: {out}"
    assert out[0]["args"]["value"] == 0
    assert out[1]["args"]["value"] == 1


def test_coalesce_reset_on_draw(rd) -> None:
    evts = [
        srs(7, 0),                              # keep
        srs(7, 0, ret_va=0x1004),               # drop — same value
        draw(),                                 # keep — resets cache
        srs(7, 0, ret_va=0x1008),               # keep — post-draw "live"
    ]
    out = rd.collapse_redundant(evts)
    assert len(out) == 3, f"expected 3, got {len(out)}"
    assert out[0]["op"] == "SetRenderState"
    assert out[1]["op"] == "DrawPrimitive"
    assert out[2]["op"] == "SetRenderState"


def test_coalesce_tss_keyed_by_stage_and_type(rd) -> None:
    evts = [
        tss(0, 4, 1),                           # keep
        tss(0, 4, 1, ret_va=0x1004),            # drop (same key+val)
        tss(0, 5, 1, ret_va=0x1008),            # keep — different type
        tss(1, 4, 1, ret_va=0x100c),            # keep — different stage
        tss(0, 4, 2, ret_va=0x1010),            # keep — value changed
        tss(0, 4, 2, ret_va=0x1014),            # drop
    ]
    out = rd.collapse_redundant(evts)
    assert len(out) == 4


def test_coalesce_settexture_per_stage(rd) -> None:
    evts = [
        stex(0, "0xa"),                          # keep
        stex(0, "0xa", ret_va=0x1004),           # drop (same tex)
        stex(1, "0xa", ret_va=0x1008),           # keep — different stage
        stex(0, "0xb", ret_va=0x100c),           # keep — tex changed
    ]
    out = rd.collapse_redundant(evts)
    assert len(out) == 3


def test_scope_filter(rd) -> None:
    evts = [srs(7, 0, ret_va=0x1000),
            srs(8, 0, ret_va=0x2000),
            srs(9, 0, ret_va=0x3000)]
    # window covers only the middle event
    kept = rd.apply_scope(evts, (0x1500, 0x2500))
    assert len(kept) == 1
    assert kept[0]["args"]["state"] == 8


def test_opaque_pointers_basic(rd) -> None:
    evts = [stex(0, "0xaaaa"), stex(0, "0xbbbb"), stex(0, "0xaaaa")]
    out = rd.opaqueify_pointers(evts)
    assert out[0]["args"]["texture"] == "#0"
    assert out[1]["args"]["texture"] == "#1"
    assert out[2]["args"]["texture"] == "#0"   # reused id


def test_opaque_pointers_separate_buckets_per_field(rd) -> None:
    # SetStreamSource uses 'vb' field; SetIndices uses 'ib' field — each
    # gets its own id space so a vb at address X and an ib at address X
    # don't collapse into the same id.
    evts = [
        {"op": "SetStreamSource", "args": {"stream": 0, "vb": "0xaa", "stride": 32},
         "ret_va": 0, "frame": 0},
        {"op": "SetIndices",      "args": {"ib": "0xaa", "base_vertex": 0},
         "ret_va": 0, "frame": 0},
    ]
    out = rd.opaqueify_pointers(evts)
    assert out[0]["args"]["vb"] == "#0"
    assert out[1]["args"]["ib"] == "#0"
    # Different fields → both get #0 from their own bucket.  Critically,
    # the two events do NOT compare equal because op differs.


def test_opaque_pointers_diff_cancels_address_noise(rd) -> None:
    r = [stex(0, "0xaaaa"), stex(0, "0xbbbb"), stex(0, "0xaaaa")]
    p = [stex(0, "0xdead"), stex(0, "0xbeef"), stex(0, "0xdead")]
    # Without opaque: every pair diverges
    fd_raw = rd.diff_frame(0, r, p)
    assert fd_raw.diverged
    # With opaque: identical (1st-seen → #0, 2nd-seen → #1, 1st reused)
    fd_opaque = rd.diff_frame(0,
                              rd.opaqueify_pointers(r),
                              rd.opaqueify_pointers(p))
    assert not fd_opaque.diverged, f"got blocks: {fd_opaque.blocks}"


def test_opaque_pointers_preserves_non_pointer_args(rd) -> None:
    evts = [srs(7, 1), srs(7, 0xdeadbeef)]
    out = rd.opaqueify_pointers(evts)
    # SetRenderState 'value' is an integer, not a "0x…" string — must
    # survive untouched (an opaqueify pass that rewrites integers would
    # destroy semantic content).
    assert out[0]["args"]["value"] == 1
    assert out[1]["args"]["value"] == 0xdeadbeef


def test_parse_range_hex_and_dec(rd) -> None:
    assert rd._parse_range("0x100:0x200") == (0x100, 0x200)
    assert rd._parse_range("0x100-0x200") == (0x100, 0x200)
    assert rd._parse_range("1000:2000")   == (1000, 2000)


def test_cli_end_to_end(rd) -> None:
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        r_path = td / "retail.jsonl"
        p_path = td / "port.jsonl"
        # Retail: 3 events; Port: same shape but one value diverges.
        write_jsonl(r_path, [srs(7, 0), tss(0, 4, 4), draw()])
        write_jsonl(p_path, [srs(7, 1), tss(0, 4, 4), draw()])
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = rd.main([
                "--retail", str(r_path),
                "--port",   str(p_path),
            ])
        out = buf.getvalue()
        assert rc == 1, f"expected exit 1, got {rc}; output:\n{out}"
        assert "FRAME 0" in out
        assert "SetRenderState" in out
        # value mismatch row from each side should appear in the output
        assert '"value":0' in out
        assert '"value":1' in out


def test_cli_self_diff(rd) -> None:
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        r_path = td / "a.jsonl"
        p_path = td / "b.jsonl"
        evts = [srs(7, 0), tss(0, 4, 4), draw()]
        write_jsonl(r_path, evts)
        write_jsonl(p_path, evts)
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = rd.main([
                "--retail", str(r_path),
                "--port",   str(p_path),
            ])
        out = buf.getvalue()
        assert rc == 0, f"expected exit 0, got {rc}; output:\n{out}"
        assert "all 1 compared frame(s) identical" in out


# ── runner ────────────────────────────────────────────────────────────────


def main() -> int:
    rd = load_render_diff()
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    failed = 0
    for fn in tests:
        try:
            fn(rd)
            print(f"  PASS  {fn.__name__}")
        except Exception as e:
            failed += 1
            print(f"  FAIL  {fn.__name__}: {e!r}")
    print()
    if failed:
        print(f"FAILED  {failed}/{len(tests)}")
        return 1
    print(f"OK  {len(tests)}/{len(tests)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
