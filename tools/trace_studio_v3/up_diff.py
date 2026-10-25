#!/usr/bin/env python3
"""up_diff.py — a frame's DrawPrimitiveUP draws, port d3d.jsonl vs a retail v3 capture.

    up_diff.py PORT_D3D_JSONL PORT_FRAME RETAIL_V3CAP [RETAIL_FRAME_INDEX] [--fvf 0x142] [--all]

The UP draws (sprites, billboards, glyphs, 2D quads) carry their vertices inline, so the
two sides compare value for value: the draws are aligned in order by (FVF, stride, vertex
count) and each aligned pair reports its first differing vertex (position / uv / diffuse)
and a WORLD difference.  Answers "the same sprite draws, but which cell / where?"
without dumping two vertex lists by hand.  Port side: `rhost.py portshots X --at F
--port-d3d`; retail side: `rhost.py run X --d3d --cap F-1:1` (the capture's frame 0)."""
import argparse
import difflib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3            # noqa: E402
import orv3_draws as od  # noqa: E402

WORLD = 256


def decode(b: bytes, stride: int, n: int, fvf: int) -> list[tuple]:
    rhw = bool(fvf & 0x4)
    out = []
    for i in range(n):
        off = i * stride
        pos = struct.unpack_from("<4f" if rhw else "<3f", b, off)
        o2 = off + (16 if rhw else 12)
        if fvf & 0x10:           # NORMAL
            o2 += 12
        dif = None
        if fvf & 0x40:
            dif = struct.unpack_from("<I", b, o2)[0]
            o2 += 4
        if fvf & 0x80:
            o2 += 4
        uv = struct.unpack_from("<2f", b, o2) if (fvf >> 8) & 0xF else None
        out.append((pos, uv, dif))
    return out


def port_draws(path: str, frame: int) -> list[dict]:
    fvf, world, tex, out = 0, None, None, []
    for line in open(path):
        o = json.loads(line)
        if o.get("frame") != frame:
            continue
        op, a = o["op"], o["args"]
        if op == "SetVertexShader":
            fvf = a["handle"]
        elif op == "SetTransform" and a["state"] == WORLD:
            world = tuple(a["matrix"])
        elif op == "SetTexture" and a["stage"] == 0:
            tex = a.get("tex_name") or a.get("texture")
        elif op == "DrawPrimitiveUP":
            b = bytes.fromhex(a["vb_bytes"])
            out.append({"fvf": fvf, "stride": a["vb_stride"], "prim": a["prim_type"],
                        "verts": decode(b, a["vb_stride"], a["vb_nverts"], fvf),
                        "world": world, "tex": tex})
    return out


def retail_draws(path: str, fi: int) -> list[dict]:
    c = orv3.Container.load(path)
    out = []
    for di, d in enumerate(od.enumerate_draws(c, fi)):
        if d.op not in (orv3.DrawPrimitiveUP, orv3.DrawIndexedPrimitiveUP):
            continue
        r = od.up_vert_probe(c, fi, di)
        fvf = int(r["fvf"], 16)
        verts = [(tuple(v["pos"]), tuple(v["uv"]) if v["uv"] else None,
                  int(v["diffuse"], 16) if v["diffuse"] else None) for v in r["verts"]]
        ti = c.tex_info(d.tex_id) if d.tex_id >= 0 else None
        tex = f"draw[{di}] tex#{d.tex_id}" + (f" {ti['w']}x{ti['h']}" if ti else "")
        out.append({"fvf": fvf, "stride": r["stride"], "prim": d.prim_type, "verts": verts,
                    "world": tuple(r["world"]) if r.get("world") else None, "tex": tex})
    return out


def first_vert_diff(p: list, r: list, eps: float) -> str | None:
    for i, (pv, rv) in enumerate(zip(p, r)):
        if any(abs(a - b) > eps for a, b in zip(pv[0], rv[0])):
            return f"v{i} pos port {fmt(pv[0])} retail {fmt(rv[0])}"
        if pv[1] and rv[1] and any(abs(a - b) > 1e-4 for a, b in zip(pv[1], rv[1])):
            return f"v{i} uv port {fmt(pv[1])} retail {fmt(rv[1])}"
        if pv[2] != rv[2]:
            return f"v{i} diffuse port {pv[2]:08x} retail {rv[2]:08x}"
    return None


def fmt(t) -> str:
    return "(" + ", ".join(f"{x:.4f}" for x in t) + ")"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("port")
    ap.add_argument("port_frame", type=int)
    ap.add_argument("retail")
    ap.add_argument("retail_frame", type=int, nargs="?", default=0)
    ap.add_argument("--fvf", type=lambda s: int(s, 0), help="only draws of this FVF")
    ap.add_argument("--all", action="store_true", help="print matching pairs too")
    ap.add_argument("--eps", type=float, default=1e-3)
    a = ap.parse_args()
    P = port_draws(a.port, a.port_frame)
    R = retail_draws(a.retail, a.retail_frame)
    if a.fvf is not None:
        P = [d for d in P if d["fvf"] == a.fvf]
        R = [d for d in R if d["fvf"] == a.fvf]
    key = lambda d: (d["fvf"], d["stride"], len(d["verts"]))  # noqa: E731
    sm = difflib.SequenceMatcher(None, [key(d) for d in P], [key(d) for d in R], autojunk=False)
    print(f"UP draws: port {len(P)} / retail {len(R)}")
    ndiff = 0
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag != "equal":
            print(f"  {tag}: port [{i1}:{i2}] {[key(d) for d in P[i1:i2]][:6]}  retail [{j1}:{j2}] {[key(d) for d in R[j1:j2]][:6]}")
            for d in P[i1:i2][:3] + R[j1:j2][:3]:
                print(f"    {d['tex']}: v0 {fmt(d['verts'][0][0])} uv {fmt(d['verts'][0][1] or ())}"
                      f" … v{len(d['verts']) - 1} {fmt(d['verts'][-1][0])}")
            continue
        for i, j in zip(range(i1, i2), range(j1, j2)):
            p, r = P[i], R[j]
            msg = first_vert_diff(p["verts"], r["verts"], a.eps)
            wmsg = None
            if p["world"] and r["world"] and any(abs(x - y) > a.eps for x, y in zip(p["world"], r["world"])):
                wmsg = f"WORLD t port {fmt(p['world'][12:15])} retail {fmt(r['world'][12:15])}"
            if msg or wmsg or a.all:
                ndiff += bool(msg or wmsg)
                print(f"  port #{i} ({p['tex']}) ~ retail #{j} ({r['tex']}) fvf {p['fvf']:#x} n {len(p['verts'])}: "
                      + "; ".join(x for x in (msg, wmsg) if x) if (msg or wmsg) else
                      f"  port #{i} ({p['tex']}) ~ retail #{j} ({r['tex']}): same")
    print(f"{ndiff} aligned pair(s) differ")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
