#!/usr/bin/env python3
"""
d3d_state_at_draw.py — reliable device-state-at-draw inspector.

The d3d trace logs every Set* call but a draw's *effective* state is the
ACCUMULATION of all prior Set* calls across the WHOLE run (device state is
persistent across frames — a state set in frame N and never re-set is still
active in frame N+50).  Per-frame analysis misses inherited state and is the
reason "the trace looked identical" while the pixels differed.

This tool replays the trace sequentially, carrying the full device state
forward (never resetting per frame), and prints the COMPLETE relevant state
(COLOROP/COLORARG1/2, ALPHAOP/ALPHAARG1/2, MIN/MAG/MIPFILTER, key render
states, current texture + diffuse) at each draw matching a texture-name
substring within the requested frames.

Usage:
  d3d_state_at_draw.py <trace.jsonl> --frames 569,570 --tex item_win \
      [--region X0,Y0,X1,Y1]   # filter draws by first-vertex screen pos
"""
import argparse, binascii, json, struct, sys

# DX8 D3DTEXTURESTAGESTATETYPE names we care about
TSS = {1:"COLOROP",2:"COLORARG1",3:"COLORARG2",4:"ALPHAOP",5:"ALPHAARG1",
       6:"ALPHAARG2",16:"MAGFILTER",17:"MINFILTER",18:"MIPFILTER",
       19:"MIPLODBIAS",20:"MAXMIPLEVEL",13:"ADDRESSU",14:"ADDRESSV"}
TOP = {1:"DISABLE",2:"SELECTARG1",3:"SELECTARG2",4:"MODULATE",5:"MODULATE2X",
       6:"MODULATE4X",7:"ADD",8:"ADDSIGNED",9:"ADDSIGNED2X",10:"SUBTRACT",
       13:"BLENDTEXALPHA",24:"LERP"}
TA  = {0:"DIFFUSE",1:"CURRENT",2:"TEXTURE",3:"TFACTOR",4:"ALPHAREPLICATE(+4)"}
RS  = {27:"SRCBLEND",28:"DESTBLEND",27:"SRCBLEND",7:"FOGENABLE",
       28:"DESTBLEND",206:"COLORWRITEENABLE",27:"SRCBLEND"}
# render states worth showing
RS_SHOW = {27:"ALPHABLENDENABLE?",28:"?"}  # we print a curated set below


def topname(v): return TOP.get(v, str(v))
def taname(v):
    base = v & 3
    rep = " |AREP" if (v & 4) else ""
    return TA.get(base, str(base)) + rep


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--frames", required=True, help="comma frame list")
    ap.add_argument("--tex", default="", help="texture-name substring filter")
    ap.add_argument("--region", default="", help="X0,Y0,X1,Y1 first-vertex filter")
    ap.add_argument("--max", type=int, default=3, help="max draws per frame")
    a = ap.parse_args()

    frames = set(int(x) for x in a.frames.split(","))
    region = [float(x) for x in a.region.split(",")] if a.region else None

    tss = {}            # stage0 TSS, accumulated across the WHOLE run
    rs = {}             # render states, accumulated
    cur_tex = None
    per_frame_count = {}

    for line in open(a.trace):
        line = line.strip()
        if not line:
            continue
        try:
            o = json.loads(line)
        except Exception:
            continue
        op = o.get("op"); arg = o.get("args", {})
        if op == "SetTextureStageState" and arg.get("stage") == 0:
            tss[arg.get("type")] = arg.get("value")
        elif op == "SetRenderState":
            rs[arg.get("state")] = arg.get("value")
        elif op == "SetTexture":
            cur_tex = arg.get("tex_name") or arg.get("texture")
        elif op in ("DrawPrimitiveUP", "DrawIndexedPrimitive"):
            fr = o.get("frame")
            if fr not in frames:
                continue
            if a.tex and (cur_tex is None or a.tex not in str(cur_tex)):
                continue
            x = y = diff = None
            vb = arg.get("vb_bytes"); st = arg.get("vb_stride")
            if vb and st:
                try:
                    b = binascii.unhexlify(vb)
                    x, y = struct.unpack_from("<2f", b, 0)
                    if len(b) >= 20:
                        diff = struct.unpack_from("<I", b, 16)[0]
                except Exception:
                    pass
            if region and (x is None or not (region[0] <= x <= region[2] and region[1] <= y <= region[3])):
                continue
            per_frame_count[fr] = per_frame_count.get(fr, 0) + 1
            if per_frame_count[fr] > a.max:
                continue
            print(f"--- frame {fr} draw#{per_frame_count[fr]} tex={cur_tex} "
                  f"pos=({x:.0f},{y:.0f}) diffuse={diff:08x}" if x is not None
                  else f"--- frame {fr} draw#{per_frame_count[fr]} tex={cur_tex}")
            print(f"    COLOR: op={topname(tss.get(1))} "
                  f"arg1={taname(tss.get(2,2))} arg2={taname(tss.get(3,1))}")
            print(f"    ALPHA: op={topname(tss.get(4))} "
                  f"arg1={taname(tss.get(5,2))} arg2={taname(tss.get(6,1))}")
            print(f"    FILT : mag={tss.get(16)} min={tss.get(17)} mip={tss.get(18)} "
                  f"lod={tss.get(19)} maxmip={tss.get(20)}")
            print(f"    BLEND: ABE={rs.get(27)} src={rs.get(19)} dst={rs.get(20)} "
                  f"alphatest={rs.get(15)} alpharef={rs.get(24)} alphafunc={rs.get(25)} "
                  f"fog={rs.get(7)} tfactor={rs.get(28) if 28 in rs else None}")


if __name__ == "__main__":
    sys.exit(main())
