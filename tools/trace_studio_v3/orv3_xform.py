#!/usr/bin/env python3
"""Trace Studio v3 — per-frame SetTransform (VIEW/PROJ/WORLD) dumper + cross-side diff.

The v3 capture records every `IDirect3DDevice8::SetTransform` (ORV3 op 12 =
[state][16 floats]) — the ACTUAL view/projection/world matrices the exe handed the
GPU, downstream of whatever the engine built them from.  `orv3_draws.py` skips the
matrix bytes (it cares about draws); this tool extracts and DECODES them so a
"camera looks wrong but eye/lookat probe as bit-exact" gap (RE §18.2) can be
settled at the matrix-bytes level:

  - VIEW (D3DTS_VIEW=2):     decode eye / forward / up   (D3DXMatrixLookAtRH layout)
  - PROJECTION (=3):         decode fovY / aspect / near / far (PerspectiveFovRH)
  - WORLD (=256):            per-mesh; report distinct matrices + count
  - TEXTURE0 (=4):           reported raw if present

The matrices are row-major D3DMATRIX (m[row][col]); flat[0..15] = m00,m01,...,m33.

Usage:
  orv3_xform.py <cap.bin> --frame N            # by kept-frame index
  orv3_xform.py <cap.bin> --present EXEFRAME    # by absolute exe present-count
  orv3_xform.py <cap.bin> --present E --diff <other.bin> --present-b E2   # cross-side
  orv3_xform.py <cap.bin> --scan-eye X,Y,Z[,TOL]  # list frames whose VIEW eye ~= X,Y,Z

Single-frame output is structured (classifier-clean) JSON.  Diff prints VIEW+PROJ
side by side with the max abs per-element delta and the decoded-value deltas.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

import orv3

Vec3 = tuple[float, float, float]


def _f16(d: bytes, off: int) -> list[float]:
    return list(struct.unpack_from("<16f", d, off))


def collect_transforms(c: orv3.Container, frame_index: int) -> dict[int, list[list[float]]]:
    """Return {state: [matrix, ...]} for every SetTransform in a kept frame, in
    order.  VIEW/PROJ are normally set once; WORLD is set once per mesh."""
    if not (0 <= frame_index < c.n_frames):
        raise IndexError(f"frame {frame_index} out of range (0..{c.n_frames})")
    f = c.frames[frame_index]
    d = c.data
    p, end = f.byte_start, f.byte_end
    u = lambda off: struct.unpack_from("<I", d, off)[0]
    out: dict[int, list[list[float]]] = {}
    while p < end:
        t = u(p); p += 4
        if t == orv3.SetTransform:
            state = u(p); p += 4
            out.setdefault(state, []).append(_f16(d, p)); p += 64
        elif t == orv3.RES_TEX:
            u(p); levels = u(p + 4); p += 8
            for _ in range(levels):
                p += 12; p += 4; dl = u(p); p += 4 + dl
        elif t in (orv3.RES_VB, orv3.RES_IB):
            p += 12; dl = u(p); p += 4 + dl
        elif t == orv3.RES_RT_TEX:
            p += 24
        elif t == orv3.SetRenderState:        p += 8
        elif t == orv3.SetTextureStageState:  p += 12
        elif t == orv3.SetMaterial:           p += 68
        elif t == orv3.SetTexture:            p += 8
        elif t == orv3.SetStreamSource:       p += 12
        elif t == orv3.SetIndices:            p += 8
        elif t == orv3.SetVertexShader:       p += 4
        elif t == orv3.DrawPrimitive:         p += 12
        elif t == orv3.DrawIndexedPrimitive:  p += 20
        elif t == orv3.DrawPrimitiveUP:
            p += 12; dl = u(p); p += 4 + dl
        elif t == orv3.DrawIndexedPrimitiveUP:
            p += 20; il = u(p); p += 4 + il; p += 4; vl = u(p); p += 4 + vl
        elif t == orv3.Clear:
            cnt = u(p); p += 4 + cnt * 16 + 16
        elif t == orv3.SetLight:
            p += 4; dl = u(p); p += 4 + dl
        elif t == orv3.LightEnable:           p += 8
        elif t == orv3.SetRenderTarget:       p += 16
        elif t == orv3.CopyRects:
            p += 16; cnt = u(p); p += 4 + cnt * 24
        elif t in (orv3.BeginScene, orv3.EndScene):
            pass
        elif t == orv3.Present:
            break
        else:
            raise ValueError(f"unexpected op {t} at {p - 4} in frame {frame_index}")
    return out


# ── matrix decode (row-major D3DMATRIX) ──
def _dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def decode_view(m: list[float]) -> dict:
    """D3DXMatrixLookAtRH: columns of upper-3x3 are xaxis/yaxis/zaxis (right/up/back);
    row 3 (m[12..14]) = -dot(axis, eye).  Recover eye/forward/up."""
    xaxis = (m[0], m[4], m[8])
    yaxis = (m[1], m[5], m[9])
    zaxis = (m[2], m[6], m[10])            # = normalize(eye - at), points BACK
    t = (m[12], m[13], m[14])
    # t_i = -dot(axis_i, eye)  ⇒  eye = -(t0*x + t1*y + t2*z)  (axes orthonormal)
    eye = tuple(-(t[0] * xaxis[k] + t[1] * yaxis[k] + t[2] * zaxis[k]) for k in range(3))
    forward = tuple(-zaxis[k] for k in range(3))    # look direction
    return {"eye": [round(v, 5) for v in eye],
            "forward": [round(v, 5) for v in forward],
            "up": [round(v, 5) for v in yaxis],
            "right": [round(v, 5) for v in xaxis]}


def decode_proj(m: list[float]) -> dict:
    """D3DXMatrixPerspectiveFovRH: m0=xScale, m5=yScale=cot(fovY/2),
    m10=zf/(zn-zf), m14=zn*zf/(zn-zf)."""
    xs, ys = m[0], m[5]
    out: dict = {}
    if ys != 0:
        out["fovY_deg"] = round(math.degrees(2 * math.atan(1.0 / ys)), 4)
    if xs != 0:
        out["aspect"] = round(ys / xs, 5)
    p10, p14 = m[10], m[14]
    if p10 != 0:
        near = p14 / p10
        far = p10 * near / (1.0 + p10)
        out["near"] = round(near, 5)
        out["far"] = round(far, 3)
    out["handed"] = "RH" if m[11] < 0 else "LH"
    return out


def _fmt_mat(m: list[float]) -> list[str]:
    return ["  ".join(f"{m[r * 4 + col]:12.5f}" for col in range(4)) for r in range(4)]


def draws_by_view(c: orv3.Container, frame_index: int) -> list[dict]:
    """Walk a kept frame and group its draws by the active VIEW transform. Each
    segment: the view eye (decoded) + per-texture {tris, draws} under that view +
    the active PROJECTION far. Pinpoints which view a retail-only overlay draws
    under (the cc08==4 z=-550 pass vs the port's identity UI pass)."""
    if not (0 <= frame_index < c.n_frames):
        raise IndexError(f"frame {frame_index} out of range")
    f = c.frames[frame_index]
    d = c.data
    p, end = f.byte_start, f.byte_end
    u = lambda off: struct.unpack_from("<I", d, off)[0]
    ii = lambda off: struct.unpack_from("<i", d, off)[0]
    segs: list[dict] = []
    cur = None
    cur_tex = -1
    cur_proj_far = None

    def newseg(eye):
        nonlocal cur
        cur = {"view_eye": eye, "proj_far": cur_proj_far, "n_draws": 0,
               "tex": {}}   # tex_id -> [tris, draws]
        segs.append(cur)

    def add(prim_count):
        if cur is None:
            newseg(None)
        cur["n_draws"] += 1
        t = cur["tex"].setdefault(cur_tex, [0, 0])
        t[0] += prim_count; t[1] += 1

    while p < end:
        t = u(p); p += 4
        if t == orv3.SetTransform:
            state = u(p); m = _f16(d, p + 4); p += 68
            if state == 2:
                newseg(decode_view(m)["eye"])
            elif state == 3 and abs(m[11] + 1.0) < 1e-6:
                cur_proj_far = decode_proj(m).get("far")
                if cur is not None:
                    cur["proj_far"] = cur_proj_far
        elif t == orv3.SetTexture:
            if u(p) == 0:
                cur_tex = ii(p + 4)
            p += 8
        elif t == orv3.DrawPrimitive:
            add(u(p + 8)); p += 12
        elif t == orv3.DrawIndexedPrimitive:
            add(u(p + 16)); p += 20
        elif t == orv3.DrawPrimitiveUP:
            add(u(p + 4)); p += 12; dl = u(p); p += 4 + dl
        elif t == orv3.DrawIndexedPrimitiveUP:
            add(u(p + 12)); p += 20; il = u(p); p += 4 + il; p += 4; vl = u(p); p += 4 + vl
        elif t == orv3.RES_TEX:
            u(p); levels = u(p + 4); p += 8
            for _ in range(levels):
                p += 12; p += 4; dl = u(p); p += 4 + dl
        elif t in (orv3.RES_VB, orv3.RES_IB):
            p += 12; dl = u(p); p += 4 + dl
        elif t == orv3.RES_RT_TEX:            p += 24
        elif t == orv3.SetRenderState:        p += 8
        elif t == orv3.SetTextureStageState:  p += 12
        elif t == orv3.SetMaterial:           p += 68
        elif t == orv3.SetStreamSource:       p += 12
        elif t == orv3.SetIndices:            p += 8
        elif t == orv3.SetVertexShader:       p += 4
        elif t == orv3.Clear:
            cnt = u(p); p += 4 + cnt * 16 + 16
        elif t == orv3.SetLight:
            p += 4; dl = u(p); p += 4 + dl
        elif t == orv3.LightEnable:           p += 8
        elif t == orv3.SetRenderTarget:       p += 16
        elif t == orv3.CopyRects:
            p += 16; cnt = u(p); p += 4 + cnt * 24
        elif t in (orv3.BeginScene, orv3.EndScene):
            pass
        elif t == orv3.Present:
            break
        else:
            raise ValueError(f"unexpected op {t} at {p - 4}")
    return segs


def _find_present(c: orv3.Container, exe_frame: int) -> int:
    for fr in c.frames:
        if fr.present == exe_frame:
            return fr.index
    raise SystemExit(f"present (exe frame) {exe_frame} not found "
                     f"(range {c.frames[0].present}..{c.frames[-1].present})")


def _distinct_ordered(mats: list[list[float]]) -> list[list[float]]:
    """Distinct matrices in first-seen order (a state is re-set per pass: the 3D
    perspective view + the 2D UI identity view both appear; show each once)."""
    seen: set[tuple] = set()
    out = []
    for m in mats:
        k = tuple(round(v, 6) for v in m)
        if k not in seen:
            seen.add(k); out.append(m)
    return out


def _summary(c: orv3.Container, idx: int) -> dict:
    xf = collect_transforms(c, idx)
    fr = c.frames[idx]
    out: dict = {"frame_index": idx, "exe_frame": fr.present,
                 "states_present": sorted(xf.keys())}
    if 2 in xf:
        out["VIEW"] = {"count": len(xf[2]), "n_distinct": len(_distinct_ordered(xf[2])),
                       "variants": [{"matrix_rows": _fmt_mat(m), "decoded": decode_view(m)}
                                    for m in _distinct_ordered(xf[2])]}
    if 3 in xf:
        out["PROJECTION"] = {"count": len(xf[3]), "n_distinct": len(_distinct_ordered(xf[3])),
                             "variants": [{"matrix_rows": _fmt_mat(m), "decoded": decode_proj(m)}
                                          for m in _distinct_ordered(xf[3])]}
    if 256 in xf:
        worlds = xf[256]
        distinct = {tuple(round(v, 4) for v in w) for w in worlds}
        out["WORLD"] = {"count": len(worlds), "distinct": len(distinct),
                        "first_rows": _fmt_mat(worlds[0])}
    if 4 in xf:
        out["TEXTURE0"] = {"count": len(xf[4]), "first_rows": _fmt_mat(xf[4][0])}
    return out


def _pick_3d_view(mats: list[list[float]]) -> list[float]:
    """The 3D camera VIEW = the distinct VIEW with the largest |eye| (the UI pass
    resets VIEW to identity, eye=0)."""
    best, best_e = mats[0], -1.0
    for m in _distinct_ordered(mats):
        e = decode_view(m)["eye"]
        mag = e[0] * e[0] + e[1] * e[1] + e[2] * e[2]
        if mag > best_e:
            best, best_e = m, mag
    return best


def _pick_perspective_proj(mats: list[list[float]]) -> list[float]:
    """The 3D PROJECTION = the perspective one (m[11] == -1); 2D ortho passes have
    m[11] == 0."""
    for m in _distinct_ordered(mats):
        if abs(m[11] + 1.0) < 1e-6:
            return m
    return mats[0]


def _diff(ca: orv3.Container, ia: int, cb: orv3.Container, ib: int) -> None:
    xa, xb = collect_transforms(ca, ia), collect_transforms(cb, ib)
    print(f"A: {Path(ca._path).name if hasattr(ca,'_path') else 'A'} "
          f"frame_idx {ia} (exe {ca.frames[ia].present})   "
          f"B: frame_idx {ib} (exe {cb.frames[ib].present})")
    for state, name, pick, dec in (
            (2, "VIEW (3D camera)", _pick_3d_view, decode_view),
            (3, "PROJECTION (perspective)", _pick_perspective_proj, decode_proj)):
        print(f"\n=== {name} (state {state}) ===")
        if state not in xa or state not in xb:
            print(f"  PRESENT? A={state in xa} B={state in xb}"); continue
        ma, mb = pick(xa[state]), pick(xb[state])
        maxd = max(abs(ma[k] - mb[k]) for k in range(16))
        print(f"  set/frame: A={len(xa[state])} ({len(_distinct_ordered(xa[state]))} distinct)  "
              f"B={len(xb[state])} ({len(_distinct_ordered(xb[state]))} distinct)")
        print(f"  max abs element delta: {maxd:.6g}   "
              f"{'BIT-IDENTICAL' if maxd == 0 else 'DIFFERS'}")
        ra, rb = _fmt_mat(ma), _fmt_mat(mb)
        print("   A (side-A)                                        | B (side-B)")
        for r in range(4):
            print(f"   {ra[r]}  |  {rb[r]}")
        print(f"   decode A: {json.dumps(dec(ma))}")
        print(f"   decode B: {json.dumps(dec(mb))}")
    # WORLD: compare the set of distinct matrices
    print("\n=== WORLD (state 256) ===")
    wa = {tuple(round(v, 4) for v in w) for w in xa.get(256, [])}
    wb = {tuple(round(v, 4) for v in w) for w in xb.get(256, [])}
    print(f"  A: {len(xa.get(256, []))} sets / {len(wa)} distinct   "
          f"B: {len(xb.get(256, []))} sets / {len(wb)} distinct   "
          f"shared {len(wa & wb)}  A-only {len(wa - wb)}  B-only {len(wb - wa)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cap", type=Path)
    ap.add_argument("--frame", type=int, help="kept-frame index")
    ap.add_argument("--present", type=int, help="absolute exe present-count")
    ap.add_argument("--diff", type=Path, help="second container to diff against")
    ap.add_argument("--frame-b", type=int)
    ap.add_argument("--present-b", type=int)
    ap.add_argument("--draws-by-view", action="store_true",
                    help="group the frame's draws by active VIEW (segment → per-tex tris/draws)")
    ap.add_argument("--scan-eye", help="X,Y,Z[,TOL]: list frames whose VIEW eye ~= X,Y,Z")
    ap.add_argument("--scan-range", help="A,B kept-frame index range for --scan-eye (default all)")
    args = ap.parse_args()

    c = orv3.Container.load(args.cap)
    c._path = str(args.cap)

    if args.scan_eye:
        parts = [float(x) for x in args.scan_eye.split(",")]
        tx, ty, tz = parts[:3]
        tol = parts[3] if len(parts) > 3 else 0.05
        a, b = (0, c.n_frames)
        if args.scan_range:
            a, b = (int(x) for x in args.scan_range.split(","))
        hits = []
        for i in range(max(0, a), min(c.n_frames, b)):
            xf = collect_transforms(c, i)
            if 2 not in xf:
                continue
            for m in _distinct_ordered(xf[2]):
                e = decode_view(m)["eye"]
                if abs(e[0] - tx) <= tol and abs(e[1] - ty) <= tol and abs(e[2] - tz) <= tol:
                    hits.append((i, c.frames[i].present, e)); break
        print(json.dumps({"scan_eye": [tx, ty, tz], "tol": tol,
                          "n_hits": len(hits),
                          "first": hits[0] if hits else None,
                          "last": hits[-1] if hits else None,
                          "hits": hits[:40]}, indent=1))
        return 0

    def resolve(cont, fr, pr):
        if pr is not None:
            return _find_present(cont, pr)
        if fr is not None:
            return fr
        raise SystemExit("need --frame or --present")

    ia = resolve(c, args.frame, args.present)

    if args.draws_by_view:
        segs = draws_by_view(c, ia)
        print(f"frame_idx {ia} (exe {c.frames[ia].present}): {len(segs)} view-segments")
        for si, s in enumerate(segs):
            eye = s["view_eye"]
            tag = "identity" if eye and max(abs(v) for v in eye) < 1e-4 else f"eye={eye}"
            print(f"  seg[{si}] view {tag}  proj_far={s['proj_far']}  {s['n_draws']} draws")
            rows = sorted(s["tex"].items(), key=lambda kv: -kv[1][0])
            for tid, (tris, nd) in rows:
                info = ""
                ti = c.tex_info(tid) if tid >= 0 else None
                if ti is not None:
                    info = f" {ti['w']}x{ti['h']}{'/RT' if ti['is_rt'] else ''}"
                print(f"       tex#{tid:<4d}{info}  {tris} tris / {nd} draws")
        return 0

    if args.diff:
        cb = orv3.Container.load(args.diff)
        cb._path = str(args.diff)
        ib = resolve(cb, args.frame_b if args.frame_b is not None else args.frame,
                     args.present_b)
        _diff(c, ia, cb, ib)
        return 0

    print(json.dumps(_summary(c, ia), indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
