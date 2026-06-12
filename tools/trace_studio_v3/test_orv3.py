#!/usr/bin/env python3
"""test_orv3.py — guard the v3 container parser + slicer + identity join.

Pure-Python (no D3D, no replay, no retail): builds a synthetic orv3 container that
mirrors orv3_format.h — including content-hash resource dedup ACROSS frames (a
resource defined once in frame 0, re-bound in frame 1 with no new RES record) — and
asserts:
  (1) the parser recovers frame count / present payloads / per-frame resource
      defs+refs / draw counts;
  (2) slice_window re-emits a sub-window that re-parses correctly AND pulls forward a
      resource first defined BEFORE the slice (the dedup pull-forward that keeps every
      bound id defined before use);
  (3) the sync-by-identity join pairs by (anchor,occ,offset), surfaces honest gaps,
      and the naive absolute-present contrast collapses to 0 under a load stretch.

Run: nix develop --command python3 tools/trace_studio_v3/test_orv3.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3        # noqa: E402
import orv3_sync   # noqa: E402


def _u(v: int) -> bytes:
    return struct.pack("<I", v)


def build_container() -> bytes:
    """3 frames. id0(tex)+id1(vb) defined in frame 0 and re-bound in frame 1 with NO
    new RES record (dedup); frame 2 defines id2(tex). Byte layout mirrors the proxy /
    orv3_format.h exactly, so the parser is tested against real-shaped data."""
    b = bytearray()
    b += _u(orv3.MAGIC) + _u(2)                                    # header
    b += _u(orv3.DEV_PARAMS) + b"".join(_u(x) for x in            # 12 u32 params
          (1024, 768, 21, 75, 0, 1, 1, 0x40, 0, 0, 1, 1))

    def tex(rid: int) -> bytes:    # RES_TEX: id, levels=1, [w,h,fmt,rowbytes,datalen,data]
        return _u(orv3.RES_TEX) + _u(rid) + _u(1) + _u(2) + _u(2) + _u(21) + _u(8) + _u(16) + b"\xaa" * 16

    def vb(rid: int) -> bytes:     # RES_VB: id, size, fvf, datalen, data
        return _u(orv3.RES_VB) + _u(rid) + _u(12) + _u(0) + _u(12) + b"\xbb" * 12

    def bind_draw(tid: int, vid: int | None) -> bytes:
        out = _u(orv3.SetTexture) + _u(0) + _u(tid)
        if vid is not None:
            out += _u(orv3.SetStreamSource) + _u(0) + _u(vid) + _u(12)
        return out + _u(orv3.DrawPrimitive) + _u(4) + _u(0) + _u(2)

    # frame 0: define id0,id1; a preamble RS; bind+draw; Present(100)
    b += tex(0) + vb(1)
    b += _u(orv3.SetRenderState) + _u(7) + _u(1)                  # ZENABLE (scalar preamble)
    b += bind_draw(0, 1) + _u(orv3.Present) + _u(100)
    # frame 1: NO new RES (dedup) — re-bind id0,id1; Present(101)
    b += bind_draw(0, 1) + _u(orv3.Present) + _u(101)
    # frame 2: define id2; bind id2; Present(102)
    b += tex(2) + bind_draw(2, None) + _u(orv3.Present) + _u(102)
    b += _u(orv3.EOF)
    return bytes(b)


def test_parse() -> None:
    c = orv3.Container(build_container())
    assert c.magic == orv3.MAGIC and c.version == 2
    assert (c.dev["w"], c.dev["h"]) == (1024, 768), c.dev
    assert c.n_frames == 3, c.n_frames
    assert c.presents == [100, 101, 102], c.presents
    assert c.frames[0].res_defined == [0, 1], c.frames[0].res_defined
    assert c.frames[1].res_defined == [], c.frames[1].res_defined    # dedup: no new RES
    assert c.frames[2].res_defined == [2], c.frames[2].res_defined
    assert c.frames[0].res_referenced == {0, 1}
    assert c.frames[1].res_referenced == {0, 1}
    assert c.frames[2].res_referenced == {2}
    assert [f.n_draws for f in c.frames] == [1, 1, 1]
    assert set(c.resources) == {0, 1, 2}, c.resources
    print("  OK parse: 3 frames, presents [100,101,102], dedup'd resources {0,1,2}")


def test_slice() -> None:
    c = orv3.Container(build_container())
    # slice [1,3): frame 1 binds id0,id1 (defined in frame 0, OUTSIDE the slice) ⇒ must
    # pull them forward; frame 2 defines id2 in-slice. Every bind must resolve.
    sl = orv3.Container(c.slice_window(1, 3))
    assert sl.n_frames == 2, sl.n_frames
    assert sl.presents == [101, 102], sl.presents
    assert set(sl.resources) == {0, 1, 2}, sl.resources              # pulled forward {0,1} + carried {2}
    assert sl.frames[0].res_referenced == {0, 1}
    assert sl.frames[1].res_referenced == {2}
    # a full-window slice is a faithful copy
    full = orv3.Container(c.slice_window(0, 3))
    assert full.presents == [100, 101, 102] and set(full.resources) == {0, 1, 2}
    print("  OK slice [1,3): 2 frames, pulled forward id{0,1}, carried id2; [0,3) faithful")


def test_join() -> None:
    def rows(anchor, occ, off0, n, present0):
        return [{"index": i, "key": [anchor, occ, off0 + i], "present": present0 + i}
                for i in range(n)]
    # same anchor+offsets, wildly different absolute presents (a +13400 load stretch)
    port = rows("HOUSE_FREEROAM", 1, 120, 5, 600)
    retail = rows("HOUSE_FREEROAM", 1, 120, 5, 14000)
    pairs, po, ro = orv3_sync.join(port, retail)
    assert len(pairs) == 5 and not po and not ro, (len(pairs), po, ro)
    assert pairs[0]["port"] == 0 and pairs[0]["retail"] == 0
    assert orv3_sync.naive_absolute_pairs(port, retail) == 0          # zero shared presents
    # drop retail's last offset ⇒ exactly one honest port-only gap (no silent mispair)
    pairs2, po2, ro2 = orv3_sync.join(port, retail[:-1])
    assert len(pairs2) == 4 and len(po2) == 1 and not ro2, (len(pairs2), po2, ro2)
    assert po2[0]["key"] == ["HOUSE_FREEROAM", 1, 124], po2[0]["key"]
    # a different anchor occurrence never cross-pairs
    other = rows("HOUSE_FREEROAM", 2, 120, 5, 600)
    p3, po3, ro3 = orv3_sync.join(port, other)
    assert not p3 and len(po3) == 5 and len(ro3) == 5
    print("  OK join: 5/5 by identity (naive 0/5); +1 honest gap on a dropped frame; "
          "occurrences don't cross-pair")


def main() -> int:
    test_parse()
    test_slice()
    test_join()
    print("OK: orv3 container parse + slice pull-forward + sync-by-identity join")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
