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
import orv3_view   # noqa: E402
import v3cache     # noqa: E402


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


def _ident(offset0: int, count: int, anchor: str = "HOUSE_FREEROAM") -> v3cache.FrameIdentity:
    return v3cache.FrameIdentity(side="retail", scenario="s", anchor=anchor, anchor_occ=1,
                                 anchor_frame=14000, offset0=offset0, count=count,
                                 present_first=14000 + offset0)


def test_extent_lookup() -> None:
    """The auto-drive loop's cache lookup (orv3_window): containment + widest-pick +
    the anchor / stale-key filters that keep it from ever serving a wrong entry."""
    # containment of the requested sub-window in the cached extent [offset0, +count)
    m = _ident(120, 48)                                   # extent [120,168)
    assert v3cache.extent_contains(m, 120, 48)           # exact
    assert v3cache.extent_contains(m, 130, 20)           # interior
    assert not v3cache.extent_contains(m, 110, 20)       # starts before
    assert not v3cache.extent_contains(m, 160, 20)       # 160+20=180 > 168, runs past
    assert v3cache.extent_contains(m, 167, 1)            # last frame

    # dir_key strips the known scenario prefix (scenario itself has hyphens)
    assert v3cache.dir_key("house-loaded-display-pinned",
                           "house-loaded-display-pinned-26e5aec3") == "26e5aec3"
    assert v3cache.dir_key("house-loaded-display-pinned", "other-deadbeef") is None

    A = {"dir": "A", "meta": _ident(120, 48), "key_ok": True}          # [120,168)
    B = {"dir": "B", "meta": _ident(100, 100), "key_ok": True}         # [100,200) wider
    stale = {"dir": "C", "meta": _ident(120, 48), "key_ok": False}     # trace changed
    wrong = {"dir": "D", "meta": _ident(120, 48, "OTHER"), "key_ok": True}
    pick = v3cache.pick_extent
    assert pick([A], "HOUSE_FREEROAM", 130, 20)["dir"] == "A"          # contains
    assert pick([A, B], "HOUSE_FREEROAM", 130, 20)["dir"] == "B"       # both contain ⇒ widest
    assert pick([A], "HOUSE_FREEROAM", 110, 20) is None               # not contained
    assert pick([stale], "HOUSE_FREEROAM", 130, 20) is None           # stale key excluded
    assert pick([wrong], "HOUSE_FREEROAM", 130, 20) is None           # wrong anchor excluded
    assert pick([stale, wrong, A], "HOUSE_FREEROAM", 130, 20)["dir"] == "A"  # picks the one valid
    print("  OK extent lookup: containment, widest-pick, stale-key + wrong-anchor filtered out")


def test_merge_offsets() -> None:
    """The viewer timeline (orv3_view): merge both sides' identity offsets into one
    ordered column list, classifying single-sided columns as HONEST, named gaps —
    never dropping or silently mispairing one (the v2 sync bug class)."""
    # fully aligned: every column has both sides, no gaps
    rows = orv3_view.merge_offsets({120, 121, 122}, {120, 121, 122})
    assert [r["offset"] for r in rows] == [120, 121, 122]
    assert all(r["gap"] is None for r in rows)
    # port has an extra offset (retail-side gap); retail has an extra (port-side gap)
    rows = orv3_view.merge_offsets({120, 121, 122}, {121, 122, 123})
    assert [r["offset"] for r in rows] == [120, 121, 122, 123]      # union, sorted
    by = {r["offset"]: r for r in rows}
    assert by[120]["gap"] == "retail" and by[120]["has_port"] and not by[120]["has_retail"]
    assert by[123]["gap"] == "port" and by[123]["has_retail"] and not by[123]["has_port"]
    assert by[121]["gap"] is None and by[122]["gap"] is None
    print("  OK merge_offsets: union/sorted columns, honest gap named by the MISSING side")


def test_draws() -> None:
    """The semantic layer (orv3_draws): enumerate a frame's draws with the device
    state in effect, and the material diff that abstracts batching to a verdict."""
    import orv3_draws

    # enumerate_draws on the synthetic container: frame 0 binds tex0 + vb1, draws 2 prims
    c = orv3.Container(build_container())
    d0 = orv3_draws.enumerate_draws(c, 0)
    assert len(d0) == 1 and d0[0].op == orv3.DrawPrimitive, d0
    assert d0[0].tex_id == 0 and d0[0].vb_id == 1, (d0[0].tex_id, d0[0].vb_id)
    assert d0[0].prim_type == 4 and d0[0].prim_count == 2, d0[0]
    assert d0[0].rs.get("ZENABLE") == 1, d0[0].rs            # the scalar preamble is tracked
    assert d0[0].tex_hash != 0 and d0[0].geo_hash != 0       # content hashed (cross-side key)
    d2 = orv3_draws.enumerate_draws(c, 2)
    assert d2[0].tex_id == 2 and d2[0].vb_id == -1, d2[0]    # frame 2: tex2, no stream

    def D(tex, pc, idx=0):
        return orv3_draws.Draw(idx, orv3.DrawIndexedPrimitive, 4, pc, pc * 3, 0, 0, tex_hash=tex)

    # BATCHING: same texture A + same triangle total (108), port 1 draw vs retail 2
    port = [D(0xAAAA, 108)]
    retail = [D(0xAAAA, 50), D(0xAAAA, 58, 1)]
    md = orv3_draws.material_diff(port, retail)
    assert md["verdict"] == "BATCHING", md
    assert md["n_batched"] == 1 and not md["divergent"], md
    assert md["port_tris"] == 108 and md["retail_tris"] == 108

    # DIVERGENT: retail draws an extra one-sided texture B (the ea99 shape)
    md2 = orv3_draws.material_diff(port, retail + [D(0xBBBB, 80, 2)])
    assert md2["verdict"] == "DIVERGENT", md2
    assert len(md2["divergent"]) == 1 and md2["divergent"][0]["tex"].endswith("bbbb"), md2["divergent"]
    assert md2["divergent"][0]["port_tris"] == 0 and md2["divergent"][0]["retail_tris"] == 80

    # ALIGNED: identical draw lists (same tex, tris AND draw counts)
    assert orv3_draws.material_diff(port, port)["verdict"] == "ALIGNED"
    print("  OK draws: enumerate (tex/vb/state tracked), material_diff BATCHING/DIVERGENT/ALIGNED")


def main() -> int:
    test_parse()
    test_slice()
    test_join()
    test_extent_lookup()
    test_merge_offsets()
    test_draws()
    print("OK: orv3 container parse + slice pull-forward + sync-by-identity join + cache lookup "
          "+ view timeline merge + draw semantics")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
