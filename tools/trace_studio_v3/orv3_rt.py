#!/usr/bin/env python3
"""Trace Studio v3 — render-target command-sequence dump (the RT blind-spot reader).

`orv3_draws.py` enumerates a frame's DRAWS (for the cross-side draw-program diff);
this tool surfaces a frame's RENDER-TARGET program — the SetRenderTarget excursions,
CopyRects blits, Clear colours, and which draws paint into which surface — IN ORDER,
so an off-screen-RT effect (the pause-menu captured-screen backdrop [0], radial-blur
transitions, post-processing) can be READ off the real captured stream instead of
inferred from the decompile (THE PORTING LOOP: analyze before porting; don't ship
render on RE alone).

It tracks the CURRENT render target (a frame starts on the inherited backbuffer; the
scalar-state preamble doesn't rebind RTs) and collapses runs of draws that share a
target into one line (so a 100-draw house frame is one row, while the handful of
composite draws into an RT each show their bound texture — marking when a draw SAMPLES
a render target, i.e. the captured screen). Read it with the decompile of the fade
system (FUN_00454191) open.

Usage:
  orv3_rt.py <cap.bin> <frame> [<frame2> …]   # dump the RT program of one/more frames
  orv3_rt.py <cap.bin> --scan                 # list frames that USE render targets
  orv3_rt.py <cap.bin> <frame> --full         # expand every draw individually
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import orv3  # noqa: E402

# render states / stage states that tell HOW a draw paints (subset of orv3_draws._RS)
_ALPHABLEND = 27          # D3DRS_ALPHABLENDENABLE
_COLOROP = 1              # D3DTSS_COLOROP (stage 0)


def _fmt_surfref(c: orv3.Container, kind: int, rid: int) -> str:
    if kind == orv3.SURF_NULL:
        return "NULL"
    if kind == orv3.SURF_BACKBUFFER:
        return "BACKBUFFER"
    if kind == orv3.SURF_DEPTH:
        return "DEPTH"
    if kind == orv3.SURF_TEX:
        ti = c.tex_info(rid)
        if ti:
            return f"RT-tex#{rid}({ti['w']}x{ti['h']} fmt{ti['fmt']})"
        return f"tex#{rid}"
    return f"?kind{kind}#{rid}"


def _tex_label(c: orv3.Container, tid: int) -> str:
    if tid < 0:
        return "tex=none"
    ti = c.tex_info(tid)
    if ti and ti["is_rt"]:
        return f"tex#{tid}=RT({ti['w']}x{ti['h']})"   # ← sampling a render target
    return f"tex#{tid}"


def dump_frame(c: orv3.Container, fi: int, full: bool = False) -> dict:
    """Print frame `fi`'s RT command program; return a small summary dict."""
    f = c.frames[fi]
    d = c.data

    def u(off: int) -> int:
        return struct.unpack_from("<I", d, off)[0]

    def i(off: int) -> int:
        return struct.unpack_from("<i", d, off)[0]

    print(f"\n=== frame {fi} (present {f.present}) {c.dev.get('w')}x{c.dev.get('h')} ===")
    rt_defs = [rid for rid in f.res_defined if (c.tex_info(rid) or {}).get("is_rt")]
    for rid in rt_defs:
        ti = c.tex_info(rid)
        print(f"  [RES] RT-tex#{rid} {ti['w']}x{ti['h']} fmt{ti['fmt']} "
              f"levels{ti['levels']} usage=0x{ti.get('usage', 0):x}")

    cur_tex = -1
    alphablend = 0
    colorop = 0
    cur_target = "BACKBUFFER (inherited)"
    run: list[tuple] = []   # (tex_id, primcount, blend, colorop)
    n_srt = n_copy = n_draw = 0
    rt_target_draws = 0     # draws issued while an RT (not the backbuffer) is bound

    def flush() -> None:
        if not run:
            return
        tris = sum(r[1] for r in run)
        texs = []
        seen = set()
        for tid, _pc, _b, _co in run:
            if tid not in seen:
                seen.add(tid)
                texs.append(_tex_label(c, tid))
        blends = {("BLEND" if r[2] else "opaque") for r in run}
        print(f"    {len(run):3d} draw(s) → {cur_target}   {tris} prim   "
              f"[{', '.join(texs)}]   {'/'.join(sorted(blends))}")
        run.clear()

    p, end = f.byte_start, f.byte_end
    while p < end:
        t = u(p); p += 4
        if t == orv3.RES_TEX:
            p += 4; levels = u(p); p += 4
            for _ in range(levels):
                p += 16; dl = u(p); p += 4 + dl
        elif t in (orv3.RES_VB, orv3.RES_IB):
            p += 12; dl = u(p); p += 4 + dl
        elif t == orv3.RES_RT_TEX:
            p += 24
        elif t == orv3.SetRenderState:
            s = u(p); v = u(p + 4); p += 8
            if s == _ALPHABLEND:
                alphablend = v
        elif t == orv3.SetTextureStageState:
            stage = u(p); ty = u(p + 4); v = u(p + 8); p += 12
            if stage == 0 and ty == _COLOROP:
                colorop = v
        elif t == orv3.SetTransform:
            p += 4 + 64
        elif t == orv3.SetMaterial:
            p += 68
        elif t == orv3.SetTexture:
            stage = u(p); rid = i(p + 4); p += 8
            if stage == 0:
                cur_tex = rid
        elif t == orv3.SetStreamSource:
            p += 12
        elif t == orv3.SetIndices:
            p += 8
        elif t == orv3.SetVertexShader:
            p += 4
        elif t in (orv3.DrawPrimitive, orv3.DrawIndexedPrimitive,
                   orv3.DrawPrimitiveUP, orv3.DrawIndexedPrimitiveUP):
            if t == orv3.DrawPrimitive:
                pc = u(p + 8); p += 12
            elif t == orv3.DrawIndexedPrimitive:
                pc = u(p + 16); p += 20
            elif t == orv3.DrawPrimitiveUP:
                pc = u(p + 4); p += 12; dl = u(p); p += 4 + dl
            else:  # DrawIndexedPrimitiveUP
                pc = u(p + 12); p += 20
                il = u(p); p += 4 + il
                p += 4; vl = u(p); p += 4 + vl
            n_draw += 1
            if not cur_target.startswith("BACKBUFFER"):
                rt_target_draws += 1
            if full:
                flush()
                ti = c.tex_info(cur_tex)
                rtmark = " [SAMPLES-RT]" if (ti and ti["is_rt"]) else ""
                print(f"    draw#{n_draw - 1} → {cur_target}   {pc} prim   "
                      f"{_tex_label(c, cur_tex)}{rtmark}   "
                      f"{'BLEND' if alphablend else 'opaque'} colorop={colorop}")
            else:
                run.append((cur_tex, pc, alphablend, colorop))
        elif t == orv3.Clear:
            flush()
            cnt = u(p); p += 4
            p += cnt * 16
            flags = u(p); color = u(p + 4); p += 16
            print(f"  Clear flags=0x{flags:x} color=0x{color:08x} → {cur_target}")
        elif t == orv3.SetLight:
            p += 4; dl = u(p); p += 4 + dl
        elif t == orv3.LightEnable:
            p += 8
        elif t == orv3.SetRenderTarget:
            flush()
            ck = u(p); cr = i(p + 4); dk = u(p + 8); dr = i(p + 12); p += 16
            color = _fmt_surfref(c, ck, cr)
            depth = _fmt_surfref(c, dk, dr)
            print(f"  SetRenderTarget color={color}  depth={depth}")
            cur_target = color
            n_srt += 1
        elif t == orv3.CopyRects:
            flush()
            sk = u(p); sr = i(p + 4); dk = u(p + 8); dr = i(p + 12); p += 16
            cnt = u(p); p += 4
            rects = []
            for _ in range(cnt):
                rects.append(struct.unpack_from("<4i", d, p)); p += 16
            pts = []
            for _ in range(cnt):
                pts.append(struct.unpack_from("<2i", d, p)); p += 8
            src = _fmt_surfref(c, sk, sr)
            dst = _fmt_surfref(c, dk, dr)
            geo = ""
            if cnt:
                geo = f"  rect0={rects[0]} →pt0={pts[0]}"
            print(f"  CopyRects  {src} → {dst}   count={cnt}{geo}   ◀ SCREEN CAPTURE/BLIT")
            n_copy += 1
        elif t in (orv3.BeginScene, orv3.EndScene):
            pass
        elif t == orv3.Present:
            break
        else:
            raise ValueError(f"unexpected op {t} at {p - 4} in frame {fi}")
    flush()
    print(f"  — {n_draw} draws ({rt_target_draws} into an RT), "
          f"{n_srt} SetRenderTarget, {n_copy} CopyRects")
    return {"frame": fi, "present": f.present, "draws": n_draw,
            "rt_target_draws": rt_target_draws, "set_render_target": n_srt,
            "copy_rects": n_copy, "rt_defs": rt_defs}


def scan(c: orv3.Container) -> None:
    """List the frames that touch render targets (SetRenderTarget / CopyRects /
    define an RT texture) — the fast way to find WHERE an RT effect lives in a window."""
    print(f"{c.n_frames} frames; scanning for render-target use…")
    hits = 0
    for fi, f in enumerate(c.frames):
        d = c.data
        p, end = f.byte_start, f.byte_end
        n_srt = n_copy = 0
        rt_defs = [rid for rid in f.res_defined if (c.tex_info(rid) or {}).get("is_rt")]

        def u(off: int) -> int:
            return struct.unpack_from("<I", d, off)[0]

        # cheap targeted walk reusing the orv3 record sizing
        while p < end:
            t = u(p); p += 4
            if t == orv3.RES_TEX:
                p += 4; levels = u(p); p += 4
                for _ in range(levels):
                    p += 16; p += 4 + u(p)
            elif t in (orv3.RES_VB, orv3.RES_IB):
                p += 12; p += 4 + u(p)
            elif t == orv3.RES_RT_TEX:
                p += 24
            elif t == orv3.SetRenderState:
                p += 8
            elif t == orv3.SetTextureStageState:
                p += 12
            elif t == orv3.SetTransform:
                p += 68
            elif t == orv3.SetMaterial:
                p += 68
            elif t == orv3.SetTexture:
                p += 8
            elif t == orv3.SetStreamSource:
                p += 12
            elif t == orv3.SetIndices:
                p += 8
            elif t == orv3.SetVertexShader:
                p += 4
            elif t == orv3.DrawPrimitive:
                p += 12
            elif t == orv3.DrawIndexedPrimitive:
                p += 20
            elif t == orv3.DrawPrimitiveUP:
                p += 12; p += 4 + u(p)
            elif t == orv3.DrawIndexedPrimitiveUP:
                p += 20; p += 4 + u(p); p += 4; p += 4 + u(p)
            elif t == orv3.Clear:
                p += 4 + u(p) * 16 + 16
            elif t == orv3.SetLight:
                p += 4; p += 4 + u(p)
            elif t == orv3.LightEnable:
                p += 8
            elif t == orv3.SetRenderTarget:
                n_srt += 1; p += 16
            elif t == orv3.CopyRects:
                p += 16; cnt = u(p); p += 4 + cnt * 24; n_copy += 1
            elif t in (orv3.BeginScene, orv3.EndScene):
                pass
            elif t == orv3.Present:
                break
            else:
                raise ValueError(f"unexpected op {t} at {p - 4} in frame {fi}")
        if n_srt or n_copy or rt_defs:
            hits += 1
            tag = []
            if rt_defs:
                tag.append(f"defines RT {rt_defs}")
            if n_srt:
                tag.append(f"{n_srt} SetRenderTarget")
            if n_copy:
                tag.append(f"{n_copy} CopyRects")
            print(f"  frame {fi:4d} (present {f.present}): {', '.join(tag)}")
    print(f"{hits}/{c.n_frames} frames use render targets")


def main() -> int:
    ap = argparse.ArgumentParser(description="dump a frame's render-target command program")
    ap.add_argument("cap", type=Path)
    ap.add_argument("frames", type=int, nargs="*", help="kept-frame indices to dump")
    ap.add_argument("--scan", action="store_true", help="list frames that use render targets")
    ap.add_argument("--full", action="store_true", help="expand every draw individually")
    args = ap.parse_args()

    c = orv3.Container.load(args.cap)
    if args.scan:
        scan(c)
        return 0
    if not args.frames:
        ap.error("give one or more frame indices, or --scan")
    for fi in args.frames:
        if not (0 <= fi < c.n_frames):
            print(f"frame {fi} out of range (0..{c.n_frames})", file=sys.stderr)
            continue
        dump_frame(c, fi, full=args.full)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
