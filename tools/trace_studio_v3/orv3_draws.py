#!/usr/bin/env python3
"""Trace Studio v3 — per-draw enumeration + cross-side draw-list diff.

The semantic layer (P3 N3): a frame is a deterministic render PROGRAM, so a
divergence is "which DRAW / which STATE / which TEXTURE differs". This module
turns a kept frame's flat call section into an ordered list of `Draw` records —
each draw with the device state IN EFFECT when it was issued (bound texture / VB /
IB / FVF + the render states that decide whether and how it paints) — and aligns
the port's draw list against retail's to name the structural divergence (the
HOUSE scene's port-98 vs retail-125 is the canonical first target: identical
pixels from different render programs, invisible to v2's pixel diff).

The cross-side key is CONTENT, not the per-container resource id: two draws match
iff their op + primitive shape + bound-texture content-hash + geometry
content-hash agree. Resource bytes are identical across the two containers for the
same asset (the proxy stores [type][id][body]; we hash type+body exactly like the
proxy's fnv1a dedup), so the hashes line up across sides regardless of id.

Used by orv3_view (bakes the per-frame draw-diff into view.json for the native
viewer) and as a CLI for ad-hoc "explain this frame" probes.
"""
from __future__ import annotations

import difflib
import hashlib
import struct
from dataclasses import dataclass, field
from pathlib import Path

import orv3

# fnv1a-64, byte-identical to the proxy's dedup hash (d3d8_proxy.c) so a resource's
# content hash is the same value the capturer used — cross-side comparable.
_FNV_SEED = 0xCBF29CE484222325
_FNV_PRIME = 0x100000001B3
_MASK = (1 << 64) - 1


def fnv1a(data: bytes, h: int = _FNV_SEED) -> int:
    for b in data:
        h = ((h ^ b) * _FNV_PRIME) & _MASK
    return h


# render states worth snapshotting per draw — the ones that decide IF a draw paints
# (alpha/z/cull) and HOW it blends. D3DRS_* values (d3d8types.h).
_RS = {
    7: "ZENABLE", 14: "ZWRITEENABLE", 15: "ALPHATESTENABLE", 22: "CULLMODE",
    19: "SRCBLEND", 20: "DESTBLEND", 27: "ALPHABLENDENABLE", 24: "ALPHAREF",
    25: "ALPHAFUNC", 9: "FILLMODE", 174: "COLORWRITEENABLE",
}
# texture-stage-0 states that decide the colour pipeline (COLOROP=1, ALPHAOP=4).
_TSS = {1: "COLOROP", 2: "COLORARG1", 3: "COLORARG2", 4: "ALPHAOP", 5: "ALPHAARG1", 6: "ALPHAARG2"}


@dataclass
class Draw:
    """One draw call with the device state in effect when it was issued."""
    index: int                 # 0-based draw index within the frame
    op: int                    # ORV3 op (DrawPrimitive / …IndexedPrimitive / UP variants)
    prim_type: int             # D3DPRIMITIVETYPE
    prim_count: int
    num_verts: int             # vertices referenced (indexed: numv; UP: inline count)
    start: int                 # start vertex / start index (0 for UP)
    min_index: int             # min index (indexed only)
    tex_id: int = -1           # bound stage-0 texture id in this container (-1 = none)
    vb_id: int = -1            # bound stream-0 VB id (-1 = UP / none)
    ib_id: int = -1            # bound IB id (-1 = none / UP)
    fvf: int = 0               # FVF (SetVertexShader handle, fixed-function)
    tex_hash: int = 0          # content hash of the bound stage-0 texture (0 = none)
    geo_hash: int = 0          # content hash of the geometry (VB+IB bytes, or inline UP data)
    rs: dict = field(default_factory=dict)    # snapshot of the _RS render states
    tss: dict = field(default_factory=dict)   # snapshot of the _TSS stage-0 states

    def signature(self) -> tuple:
        """Cross-side identity: equal iff the same draw (same shape + same content),
        independent of per-container resource ids and absolute frame placement."""
        return (self.op, self.prim_type, self.prim_count, self.num_verts,
                self.tex_hash, self.geo_hash)

    def kind(self) -> str:
        return orv3.OPNAME.get(self.op, f"op{self.op}")


class ResHash:
    """Content-hash a container's resources by id, memoized. Create ONCE per container
    and reuse across all its frames — a resource bound in many frames is hashed once
    (a HOUSE container is ~26 MB; re-hashing per frame is the bake's dominant cost).

    The hash is a fast C-speed blake2b over a ZERO-COPY memoryview of the resource
    body (type-keyed, so a tex and a VB with identical bytes don't collide). It need
    only be deterministic + identical for identical CONTENT across the two containers
    (cross-side comparable) — that holds for any content hash, so blake2b replaces the
    pure-Python fnv1a byte loop that made the bake pathologically slow."""

    def __init__(self, c: orv3.Container):
        self.c = c
        self._mv = memoryview(c.data)
        self._cache: dict[int, int] = {}

    def of(self, rid: int) -> int:
        if rid < 0:
            return 0
        h = self._cache.get(rid)
        if h is not None:
            return h
        entry = self.c.resources.get(rid)
        if not entry:
            return 0
        typ, start, end = entry
        d = hashlib.blake2b(digest_size=8)
        d.update(struct.pack("<I", typ))
        d.update(self._mv[start + 8:end])          # body after [type][id], zero-copy
        h = int.from_bytes(d.digest(), "little")
        self._cache[rid] = h
        return h


def enumerate_draws(c: orv3.Container, frame_index: int, reshash: ResHash | None = None) -> list[Draw]:
    """Walk kept frame `frame_index`'s call section and return its draws in order,
    each carrying the bound texture/VB/IB/FVF + the tracked render/stage states. The
    section's preamble re-establishes inherited state, so state is correct from the
    section start (no need to replay earlier frames). Pass a shared `reshash` (one per
    container) when enumerating many frames so resources are hashed once, not per call."""
    if not (0 <= frame_index < c.n_frames):
        raise IndexError(f"frame {frame_index} out of range (0..{c.n_frames})")
    f = c.frames[frame_index]
    d = c.data
    p, end = f.byte_start, f.byte_end
    reshash = reshash or ResHash(c)

    def u(off: int) -> int:
        return struct.unpack_from("<I", d, off)[0]

    def i(off: int) -> int:
        return struct.unpack_from("<i", d, off)[0]

    # tracked device state
    cur_tex, cur_vb, cur_ib, cur_fvf = -1, -1, -1, 0
    vb_stride = 0
    rs: dict[str, int] = {}
    tss: dict[str, int] = {}
    draws: list[Draw] = []

    def geo_hash_indexed(vb_id: int, ib_id: int, start: int, prim_count: int,
                         min_index: int, num_verts: int) -> int:
        h = reshash.of(vb_id)
        h = fnv1a(reshash.of(ib_id).to_bytes(8, "little"), h)
        return fnv1a(struct.pack("<5I", start, prim_count, min_index, num_verts, vb_stride), h)

    while p < end:
        t = u(p)
        p += 4
        if t == orv3.RES_TEX:
            rid = u(p); p += 4
            levels = u(p); p += 4
            for _ in range(levels):
                p += 12
                p += 4                 # rowbytes
                dl = u(p); p += 4
                p += dl
        elif t in (orv3.RES_VB, orv3.RES_IB):
            p += 4 + 8                  # id, size, fvf/fmt
            dl = u(p); p += 4
            p += dl
        elif t == orv3.SetRenderState:
            s = u(p); v = u(p + 4); p += 8
            if s in _RS:
                rs[_RS[s]] = v
        elif t == orv3.SetTextureStageState:
            stage = u(p); ty = u(p + 4); v = u(p + 8); p += 12
            if stage == 0 and ty in _TSS:
                tss[_TSS[ty]] = v
        elif t == orv3.SetTransform:
            p += 4 + 64
        elif t == orv3.SetMaterial:
            p += 68
        elif t == orv3.SetTexture:
            stage = u(p); rid = i(p + 4); p += 8
            if stage == 0:
                cur_tex = rid
        elif t == orv3.SetStreamSource:
            stream = u(p); rid = i(p + 4); stride = u(p + 8); p += 12
            if stream == 0:
                cur_vb, vb_stride = rid, stride
        elif t == orv3.SetIndices:
            rid = i(p); p += 8
            cur_ib = rid
        elif t == orv3.SetVertexShader:
            cur_fvf = u(p); p += 4
        elif t == orv3.DrawPrimitive:
            pt = u(p); sv = u(p + 4); pc = u(p + 8); p += 12
            draws.append(Draw(len(draws), t, pt, pc, _vcount(pt, pc), sv, 0,
                              tex_id=cur_tex, vb_id=cur_vb, ib_id=-1, fvf=cur_fvf,
                              tex_hash=reshash.of(cur_tex),
                              geo_hash=geo_hash_indexed(cur_vb, -1, sv, pc, 0, _vcount(pt, pc)),
                              rs=dict(rs), tss=dict(tss)))
        elif t == orv3.DrawIndexedPrimitive:
            pt = u(p); mi = u(p + 4); nv = u(p + 8); si = u(p + 12); pc = u(p + 16); p += 20
            draws.append(Draw(len(draws), t, pt, pc, nv, si, mi,
                              tex_id=cur_tex, vb_id=cur_vb, ib_id=cur_ib, fvf=cur_fvf,
                              tex_hash=reshash.of(cur_tex),
                              geo_hash=geo_hash_indexed(cur_vb, cur_ib, si, pc, mi, nv),
                              rs=dict(rs), tss=dict(tss)))
        elif t == orv3.DrawPrimitiveUP:
            pt = u(p); pc = u(p + 4); stride = u(p + 8); p += 12
            dl = u(p); p += 4
            data = d[p:p + dl]; p += dl
            draws.append(Draw(len(draws), t, pt, pc, _vcount(pt, pc), 0, 0,
                              tex_id=cur_tex, vb_id=-1, ib_id=-1, fvf=cur_fvf,
                              tex_hash=reshash.of(cur_tex),
                              geo_hash=fnv1a(data, fnv1a(struct.pack("<I", stride))),
                              rs=dict(rs), tss=dict(tss)))
        elif t == orv3.DrawIndexedPrimitiveUP:
            pt = u(p); mvi = u(p + 4); nvi = u(p + 8); pc = u(p + 12); ifmt = u(p + 16); p += 20
            il = u(p); p += 4
            idx = d[p:p + il]; p += il
            stride = u(p); p += 4
            vl = u(p); p += 4
            verts = d[p:p + vl]; p += vl
            draws.append(Draw(len(draws), t, pt, pc, nvi, 0, mvi,
                              tex_id=cur_tex, vb_id=-1, ib_id=-1, fvf=cur_fvf,
                              tex_hash=reshash.of(cur_tex),
                              geo_hash=fnv1a(verts, fnv1a(idx, fnv1a(struct.pack("<I", stride)))),
                              rs=dict(rs), tss=dict(tss)))
        elif t == orv3.Clear:
            cnt = u(p); p += 4
            p += cnt * 16 + 16
        elif t == orv3.SetLight:
            p += 4
            dl = u(p); p += 4
            p += dl
        elif t == orv3.LightEnable:
            p += 8
        elif t in (orv3.BeginScene, orv3.EndScene):
            pass
        elif t == orv3.Present:
            break
        else:
            raise ValueError(f"unexpected op {t} at {p - 4} in frame {frame_index}")
    return draws


def _vcount(prim_type: int, prim_count: int) -> int:
    """Vertices a non-indexed primitive batch references (D3DPRIMITIVETYPE)."""
    if prim_type == 1:   return prim_count          # POINTLIST
    if prim_type == 2:   return prim_count * 2       # LINELIST
    if prim_type == 3:   return prim_count + 1       # LINESTRIP
    if prim_type == 4:   return prim_count * 3       # TRIANGLELIST
    if prim_type in (5, 6): return prim_count + 2    # TRIANGLESTRIP/FAN
    return prim_count


@dataclass
class DrawDelta:
    """One aligned slot in the port↔retail draw-list diff."""
    tag: str                  # "equal" | "port_only" | "retail_only" | "replace"
    port: list[int]           # port draw indices in this slot
    retail: list[int]         # retail draw indices in this slot


def diff_draw_lists(port: list[Draw], retail: list[Draw]) -> list[DrawDelta]:
    """Align two draw lists by content signature (difflib) → ordered deltas. An
    `insert`/`delete` names retail-only / port-only draws; `replace` is a changed
    run (same slot, different content). Equal runs are the matched draws."""
    psig = [d.signature() for d in port]
    rsig = [d.signature() for d in retail]
    sm = difflib.SequenceMatcher(a=psig, b=rsig, autojunk=False)
    out: list[DrawDelta] = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            out.append(DrawDelta("equal", list(range(i1, i2)), list(range(j1, j2))))
        elif tag == "delete":
            out.append(DrawDelta("port_only", list(range(i1, i2)), []))
        elif tag == "insert":
            out.append(DrawDelta("retail_only", [], list(range(j1, j2))))
        elif tag == "replace":
            out.append(DrawDelta("replace", list(range(i1, i2)), list(range(j1, j2))))
    return out


Agg = dict[int, list[int]]   # tex content-hash -> [triangle total, draw count]


def material_agg(c: orv3.Container, frame_index: int, reshash: ResHash) -> Agg:
    """The MATERIAL aggregate of a kept frame — `{tex_hash: [triangles, draws]}` — the
    ONLY thing material_diff reads, computed WITHOUT building Draw objects.

    `enumerate_draws` is the per-draw view/pick layer: it also hashes geometry
    (geo_hash — a pure-Python fnv1a byte-loop over every UP draw's inline vertices)
    and snapshots rs/tss per draw. The material bake throws ALL of that away, so for
    the per-column view.json bake (thousands of columns) this walk tracks only the
    stage-0 texture and sums prim_count per bound texture — an ~18× faster bake with
    byte-identical material verdicts (test_draws_material_agg cross-checks it against
    enumerate_draws + material_diff). It is a deliberate perf-critical SUBSET of the
    record walk in enumerate_draws / orv3.Container._parse: the skip sizes MUST track
    orv3_format.h alongside both (the cross-check test catches drift)."""
    if not (0 <= frame_index < c.n_frames):
        raise IndexError(f"frame {frame_index} out of range (0..{c.n_frames})")
    f = c.frames[frame_index]
    d = c.data
    p, end = f.byte_start, f.byte_end
    upk = struct.Struct("<I").unpack_from
    ipk = struct.Struct("<i").unpack_from
    cur_tex = -1
    out: Agg = {}

    def add(prim_count: int) -> None:
        h = reshash.of(cur_tex)
        t = out.get(h)
        if t is None:
            out[h] = [prim_count, 1]
        else:
            t[0] += prim_count
            t[1] += 1

    while p < end:
        t = upk(d, p)[0]; p += 4
        # draws first (the hot ops; UP dominates the 2D UI), texture binds next
        if t == orv3.DrawPrimitiveUP:
            add(upk(d, p + 4)[0]); p += 12; p += 4 + upk(d, p)[0]
        elif t == orv3.SetTexture:
            if upk(d, p)[0] == 0:           # stage 0
                cur_tex = ipk(d, p + 4)[0]
            p += 8
        elif t == orv3.DrawIndexedPrimitive:
            add(upk(d, p + 16)[0]); p += 20
        elif t == orv3.DrawPrimitive:
            add(upk(d, p + 8)[0]); p += 12
        elif t == orv3.DrawIndexedPrimitiveUP:
            add(upk(d, p + 12)[0]); p += 20
            p += 4 + upk(d, p)[0]           # index data
            p += 4                          # stride
            p += 4 + upk(d, p)[0]           # vertex data
        elif t == orv3.SetRenderState:        p += 8
        elif t == orv3.SetTextureStageState:  p += 12
        elif t == orv3.SetTransform:          p += 68
        elif t == orv3.SetMaterial:           p += 68
        elif t == orv3.SetStreamSource:       p += 12
        elif t == orv3.SetIndices:            p += 8
        elif t == orv3.SetVertexShader:       p += 4
        elif t == orv3.RES_TEX:
            p += 4                            # id
            levels = upk(d, p)[0]; p += 4
            for _ in range(levels):
                p += 16                       # w,h,fmt,rowbytes
                p += 4 + upk(d, p)[0]         # data
        elif t in (orv3.RES_VB, orv3.RES_IB):
            p += 12                           # id, size, fvf/fmt
            p += 4 + upk(d, p)[0]             # data
        elif t == orv3.Clear:
            p += 4 + upk(d, p)[0] * 16 + 16   # count, rects, flags/color/z/stencil
        elif t == orv3.SetLight:
            p += 4                            # index
            p += 4 + upk(d, p)[0]             # light data
        elif t == orv3.LightEnable:           p += 8
        elif t in (orv3.BeginScene, orv3.EndScene):
            pass
        elif t == orv3.Present:
            break
        else:
            raise ValueError(f"unexpected op {t} at {p - 4} in frame {frame_index}")
    return out


def _material_report(pt: Agg, rt: Agg) -> dict:
    """The material verdict + per-texture rows from two per-texture aggregates — the
    representation `material_diff` (from Draw lists) and the fast bake (`material_agg`)
    share, so both produce byte-identical reports. Verdict:

      ALIGNED   — same textures, same per-texture triangle totals AND draw counts
                  (a true 1:1 draw program).
      BATCHING  — same textures + same per-texture triangle totals, only the draw
                  GRANULARITY differs (split vs batched) ⇒ pixels expected identical.
      DIVERGENT — a texture is one-sided, or a shared texture's triangle totals
                  differ ⇒ a genuine render-program difference (which the viewer's
                  draw-isolation can then confirm visible-or-not).
    """
    textures = []
    for h in sorted(set(pt) | set(rt), key=lambda k: -(pt.get(k, [0])[0] + rt.get(k, [0])[0])):
        ptris, pdraws = pt.get(h, [0, 0])
        rtris, rdraws = rt.get(h, [0, 0])
        textures.append({
            "tex": f"{h:016x}", "port_tris": ptris, "retail_tris": rtris,
            "port_draws": pdraws, "retail_draws": rdraws,
            "tris_match": ptris == rtris, "both_sides": h in pt and h in rt,
        })
    divergent = [t for t in textures if not t["tris_match"]]
    batched = [t for t in textures if t["tris_match"] and t["port_draws"] != t["retail_draws"]]
    verdict = "DIVERGENT" if divergent else ("BATCHING" if batched else "ALIGNED")
    return {
        "verdict": verdict,
        "port_draws": sum(v[1] for v in pt.values()), "retail_draws": sum(v[1] for v in rt.values()),
        "port_tris": sum(v[0] for v in pt.values()), "retail_tris": sum(v[0] for v in rt.values()),
        "n_textures": len(textures), "n_batched": len(batched),
        "divergent": divergent, "batched_textures": [t["tex"] for t in batched],
        "textures": textures,
    }


def material_diff(port: list[Draw], retail: list[Draw]) -> dict:
    """Compare two draw lists at the MATERIAL level — per bound-texture triangle
    totals + draw counts — robust to batching (the port batches geometry the retail
    engine splits, so a draw-by-draw alignment is noisy, but the per-texture triangle
    total is invariant). A thin wrapper over `_material_report`; the per-column bake
    uses `material_agg` to reach the same report without building Draw objects."""
    def agg(draws: list[Draw]) -> Agg:
        out: Agg = {}
        for d in draws:
            t = out.setdefault(d.tex_hash, [0, 0])
            t[0] += d.prim_count   # triangles
            t[1] += 1              # draws
        return out

    return _material_report(agg(port), agg(retail))


def frame_draw_report(pc: orv3.Container, pidx: int, rc: orv3.Container, ridx: int,
                      preshash: ResHash | None = None, rreshash: ResHash | None = None) -> dict:
    """The per-column draw semantics baked into view.json: the material verdict +
    the genuinely-divergent textures (with their port/retail triangle+draw counts),
    so the viewer can flag a frame whose pixels match but whose render program does
    not. Lean by design (no full per-draw list — that's the on-demand draws sidecar).
    Pass shared per-container ResHash instances when baking many columns.

    Uses `material_agg` (per-texture totals, no Draw objects / no geometry hashing)
    — the bake reads only the material level, so this is ~18× faster than enumerating
    full Draw lists across thousands of columns, with byte-identical reports."""
    preshash = preshash or ResHash(pc)
    rreshash = rreshash or ResHash(rc)
    md = _material_report(material_agg(pc, pidx, preshash), material_agg(rc, ridx, rreshash))
    return {
        "draw_verdict": md["verdict"],
        "port_tris": md["port_tris"], "retail_tris": md["retail_tris"],
        "n_textures": md["n_textures"], "n_batched": md["n_batched"],
        # only the divergent rows — usually 0–2, the actionable part
        "divergent": [{"tex": t["tex"], "port_tris": t["port_tris"], "retail_tris": t["retail_tris"],
                       "port_draws": t["port_draws"], "retail_draws": t["retail_draws"]}
                      for t in md["divergent"]],
    }


def summarize_delta(port: list[Draw], retail: list[Draw], deltas: list[DrawDelta]) -> dict:
    """A compact, JSON-able summary of a frame's draw-list divergence."""
    n_equal = sum(len(d.port) for d in deltas if d.tag == "equal")
    port_only = [i for d in deltas if d.tag in ("port_only", "replace") for i in d.port]
    retail_only = [j for d in deltas if d.tag in ("retail_only", "replace") for j in d.retail]
    return {
        "port_draws": len(port), "retail_draws": len(retail),
        "matched": n_equal, "port_only": port_only, "retail_only": retail_only,
        "aligned": len(port_only) == 0 and len(retail_only) == 0,
    }


def _describe(d: Draw, container: "orv3.Container | None" = None) -> str:
    parts = [f"{d.kind()} pt{d.prim_type} ×{d.prim_count}"]
    if d.tex_id >= 0:
        tag = f"tex#{d.tex_id}={d.tex_hash & 0xffff:04x}"
        if container is not None:
            ti = container.tex_info(d.tex_id)
            if ti is not None:                 # dims + RT-flag (datalen==0 ⇒ a
                tag += f"[{ti['w']}x{ti['h']}"  # captured-screen render target,
                tag += ",RT]" if ti["is_rt"] else ",asset]"  # not a file asset)
        parts.append(tag)
    else:
        parts.append("tex=none")
    if d.vb_id >= 0:
        parts.append(f"vb#{d.vb_id}")
    blend = "BLEND" if d.rs.get("ALPHABLENDENABLE") else "opaque"
    cop = d.tss.get("COLOROP")
    parts.append(f"{blend} colorop={cop}")
    return "  ".join(parts)


def main() -> int:
    import argparse
    import json
    ap = argparse.ArgumentParser(description="enumerate / diff a frame's draws")
    ap.add_argument("port_container", type=Path)
    ap.add_argument("port_frame", type=int)
    ap.add_argument("retail_container", type=Path, nargs="?")
    ap.add_argument("retail_frame", type=int, nargs="?")
    ap.add_argument("--list", action="store_true", help="print every draw, not just the diff")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    pc = orv3.Container.load(args.port_container)
    pdraws = enumerate_draws(pc, args.port_frame)

    if args.retail_container is None:
        if args.json:
            print(json.dumps([d.__dict__ for d in pdraws], indent=1, default=list))
        else:
            print(f"{args.port_container.name} frame {args.port_frame}: {len(pdraws)} draws")
            for d in pdraws:
                print(f"  [{d.index:3d}] {_describe(d, pc)}")
        return 0

    rc = orv3.Container.load(args.retail_container)
    rframe = args.retail_frame if args.retail_frame is not None else args.port_frame
    rdraws = enumerate_draws(rc, rframe)
    deltas = diff_draw_lists(pdraws, rdraws)
    summary = summarize_delta(pdraws, rdraws, deltas)
    if args.json:
        print(json.dumps(summary, indent=1))
        return 0
    print(f"port frame {args.port_frame}: {len(pdraws)} draws   "
          f"retail frame {rframe}: {len(rdraws)} draws")
    print(f"matched {summary['matched']}  port-only {len(summary['port_only'])}  "
          f"retail-only {len(summary['retail_only'])}  "
          f"{'ALIGNED' if summary['aligned'] else 'DIVERGENT'}")
    for dl in deltas:
        if dl.tag == "equal":
            print(f"  = {len(dl.port):3d} matched draws (port {dl.port[0]}..{dl.port[-1]})")
        elif dl.tag == "port_only":
            print(f"  - port-only draws {dl.port}:")
            for i in dl.port:
                print(f"      P[{i:3d}] {_describe(pdraws[i], pc)}")
        elif dl.tag == "retail_only":
            print(f"  + retail-only draws {dl.retail}:")
            for j in dl.retail:
                print(f"      R[{j:3d}] {_describe(rdraws[j], rc)}")
        elif dl.tag == "replace":
            print(f"  ~ replace: port {dl.port} ↔ retail {dl.retail}")
            for i in dl.port:
                print(f"      P[{i:3d}] {_describe(pdraws[i], pc)}")
            for j in dl.retail:
                print(f"      R[{j:3d}] {_describe(rdraws[j], rc)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
