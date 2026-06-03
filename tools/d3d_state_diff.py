#!/usr/bin/env python3
"""Replay a d3d_trace.jsonl op stream and analyse the LIVE render state +
projected depth at each Draw call, keyed by ret_va (+0x400000 -> real VA).

The raw trace is the call stream (SetRenderState / SetTransform / Draw...), not a
per-draw snapshot.  This replays the D3D8 state machine so we can read the actual
per-draw RENDER CONTRACT *and* the per-draw projected NDC-z / z_far.

Subcommands
-----------
  dump   <trace> [--frame N] [--va 0xVA]
      One row per Draw: render-state contract (ZENABLE/ZWRITE/ZFUNC/ALPHATEST*/blend).

  diff   <retail> <port> [--frame-a N] [--frame-b M]
      Align the two traces' draws by (real_va, per-va index) and flag state deltas.

  depth  <trace> [--frame N] [--va 0xVA] [--near-pos X,Y,Z[,R]] [--nm EXE]
      Per Draw: world pos, **NDC-z**, **z_far**, blend, ZWRITE, prim_count, ret_va
      (-> symbol if --nm given), sorted near->far.  THIS is the view that finds the
      depth-ordering / z_far bugs the plain `dump`/`diff` hide (see methodology in
      docs/render-depth-debugging.md).  --near-pos filters to draws whose world
      origin is within R (default 0.6) of X,Y,Z (e.g. a character).

  depthdiff <retail> <port> [--frame-a N] [--frame-b M] [--cluster R]
      Cross-binary depth compare: match retail<->port draws by (rounded world pos,
      blend, ZWRITE) -- binary-independent -- and flag z_far mismatches and NDC-z
      ORDER INVERSIONS (the signature of the per-pass projection bug).

  phase  <trace-a> [trace-b] --near-pos X,Y,Z[,R] [--what pc|y|ndcz|count]
      Per-FRAME fingerprint of the matched draw across the whole window:
      prim_count (= sprite anim cell count), world-Y (= hover/bob phase), ndcz,
      and draw count (= RNG-driven spawn count).  With two traces it cross-
      correlates the sequences and reports the integer frame OFFSET that best
      aligns them -- i.e. "the port is N frames ahead/behind retail" for the
      anim/bob/RNG PHASE divergences.

NDC-z / z_far convention: this engine uses D3DXMatrixPerspectiveFovRH with
near=1.0, so z_far is recovered as p10/(1+p10) where p10 = PROJ[10]; larger
z_far maps a given depth NEARER (smaller ndcz).  ZFUNC=LESSEQUAL draws if
ndcz <= stored.
"""
import argparse, json, sys, collections, math, subprocess

VA_BASE = 0x400000

RS = {7: "ZENABLE", 14: "ZWRITE", 23: "ZFUNC", 15: "ATESTEN",
      24: "AREF", 25: "AFUNC", 19: "SRC", 20: "DEST", 22: "CULL", 28: "FOG"}
DRAW_OPS = {"DrawPrimitive", "DrawIndexedPrimitive",
            "DrawPrimitiveUP", "DrawIndexedPrimitiveUP"}

ZFUNC = {1: "NEVER", 2: "LESS", 3: "EQUAL", 4: "LE", 5: "GREATER", 6: "NE",
         7: "GE", 8: "ALWAYS"}
BLEND = {1: "ZERO", 2: "ONE", 3: "SRCCOLOR", 4: "INVSRCCOLOR", 5: "SRCALPHA",
         6: "INVSRCALPHA", 7: "DESTALPHA", 8: "INVDESTALPHA", 9: "DESTCOLOR",
         10: "INVDESTCOLOR"}

# D3DTRANSFORMSTATETYPE
TS_VIEW, TS_PROJ, TS_WORLD = 2, 3, 256


def _matvec(m, v):
    """row-vector * row-major 4x4 (D3D convention): out = v . M."""
    x, y, z, w = v
    return [x*m[0]+y*m[4]+z*m[8]+w*m[12],
            x*m[1]+y*m[5]+z*m[9]+w*m[13],
            x*m[2]+y*m[6]+z*m[10]+w*m[14],
            x*m[3]+y*m[7]+z*m[11]+w*m[15]]


def replay(path):
    """Yield a rich dict per Draw: frame, va, idx, op, rs, world/view/proj,
    prim_count, plus derived ndcz / zfar / worldpos when transforms are live."""
    state = {}
    world = view = proj = None
    va_idx = collections.Counter()
    cur_frame = None
    for line in open(path):
        line = line.strip()
        if not line:
            continue
        try:
            r = json.loads(line)
        except Exception:
            continue
        op = r.get("op")
        fr = r.get("frame")
        if fr != cur_frame:
            cur_frame = fr
            va_idx = collections.Counter()
        a = r.get("args", {})
        if op == "SetRenderState":
            state[a["state"]] = a["value"]
        elif op == "SetTransform":
            st = a.get("state"); m = a.get("matrix")
            if st == TS_WORLD: world = m
            elif st == TS_VIEW: view = m
            elif st == TS_PROJ: proj = m
        elif op in DRAW_OPS:
            va = (r.get("ret_va") or 0) + VA_BASE
            snap = {name: state.get(code) for code, name in RS.items()}
            snap["_op"] = op
            rec = {"frame": fr, "va": va, "idx": va_idx[va], "rs": snap,
                   "op": op, "prim_count": a.get("prim_count"),
                   "world": world, "view": view, "proj": proj,
                   "worldpos": None, "ndcz": None, "zfar": None}
            if world:
                rec["worldpos"] = (world[12], world[13], world[14])
            if world and view and proj:
                wp = [world[12], world[13], world[14], 1.0]
                cp = _matvec(proj, _matvec(view, wp))
                if cp[3]:
                    rec["ndcz"] = cp[2] / cp[3]
                p10 = proj[10]
                if (1 + p10):
                    rec["zfar"] = p10 / (1 + p10)
            yield rec
            va_idx[va] += 1


def fmt(snap):
    def g(k, m=None):
        v = snap.get(k)
        if v is None:
            return f"{k}=?"
        return f"{k}={m.get(v, v)}" if m else f"{k}={v}"
    return " ".join([g("ZENABLE"), g("ZWRITE"), g("ZFUNC", ZFUNC), g("ATESTEN"),
                     g("AREF"), g("AFUNC", ZFUNC), g("SRC", BLEND),
                     g("DEST", BLEND), g("CULL")])


def first_draw_frame(path):
    for rec in replay(path):
        return rec["frame"]
    return None


def _load_symbols(exe):
    """ret_va -> nearest preceding T/t/W/w symbol via `nm`."""
    out = subprocess.run(["nm", exe], capture_output=True, text=True).stdout
    syms = []
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 3 and p[1] in "TtWw":
            try:
                syms.append((int(p[0], 16), p[2]))
            except ValueError:
                pass
    syms.sort()
    return syms


def _sym_for(syms, va):
    if not syms:
        return ""
    import bisect
    i = bisect.bisect_right([s[0] for s in syms], va) - 1
    if i < 0:
        return ""
    return f"{syms[i][1]}+0x{va-syms[i][0]:x}"


def _blendsig(rs):
    return f"{BLEND.get(rs.get('SRC'), rs.get('SRC'))}/{BLEND.get(rs.get('DEST'), rs.get('DEST'))}"


def _near(pos, tgt, r):
    return pos and all(abs(pos[k]-tgt[k]) < r for k in range(3))


# ---- subcommands -----------------------------------------------------------

def cmd_dump(args):
    frame = args.frame if args.frame is not None else first_draw_frame(args.trace)
    print(f"# {args.trace}  frame={frame}")
    n = 0
    for rec in replay(args.trace):
        if rec["frame"] != frame:
            continue
        if args.va is not None and rec["va"] != args.va:
            continue
        n += 1
        print(f"{n:3d} va=0x{rec['va']:06x}#{rec['idx']} {rec['rs']['_op']:18s} {fmt(rec['rs'])}")


def cmd_diff(args):
    fa = args.frame_a if args.frame_a is not None else first_draw_frame(args.retail)
    fb = args.frame_b if args.frame_b is not None else first_draw_frame(args.port)
    A = [(r["va"], r["idx"], r["rs"]) for r in replay(args.retail) if r["frame"] == fa]
    bmap = {(r["va"], r["idx"]): r["rs"] for r in replay(args.port) if r["frame"] == fb}
    print(f"# retail={args.retail} port={args.port}")
    keys = ["ZENABLE", "ZWRITE", "ZFUNC", "ATESTEN", "AREF", "AFUNC", "SRC", "DEST", "CULL"]
    for va, i, sa in A:
        sb = bmap.get((va, i))
        if sb is None:
            print(f"va=0x{va:06x}#{i}  PORT-MISSING  retail:[{fmt(sa)}]")
            continue
        deltas = [k for k in keys if sa.get(k) != sb.get(k)]
        print(f"va=0x{va:06x}#{i}" + ("  DELTA:" + ",".join(deltas) if deltas else "  ok"))
        if deltas:
            print(f"    retail: {fmt(sa)}")
            print(f"    port  : {fmt(sb)}")


def _parse_pos(s):
    parts = [float(x) for x in s.split(",")]
    tgt = parts[:3]
    r = parts[3] if len(parts) > 3 else 0.6
    return tgt, r


def cmd_depth(args):
    frame = args.frame if args.frame is not None else first_draw_frame(args.trace)
    syms = _load_symbols(args.nm) if args.nm else []
    tgt = r = None
    if args.near_pos:
        tgt, r = _parse_pos(args.near_pos)
    rows = []
    for rec in replay(args.trace):
        if rec["frame"] != frame:
            continue
        if args.va is not None and rec["va"] != args.va:
            continue
        if tgt and not _near(rec["worldpos"], tgt, r):
            continue
        rows.append(rec)
    rows.sort(key=lambda r: (r["ndcz"] if r["ndcz"] is not None else 9))
    print(f"# {args.trace} frame={frame}  (NDCz smaller=NEARER; ZFUNC=LE draws if ndcz<=stored)")
    for rec in rows:
        wp = rec["worldpos"]
        wps = f"({wp[0]:.2f},{wp[1]:.2f},{wp[2]:.2f})" if wp else "?"
        ndcz = f"{rec['ndcz']:.5f}" if rec["ndcz"] is not None else "?"
        zf = f"{rec['zfar']:.0f}" if rec["zfar"] is not None else "?"
        sym = (" " + _sym_for(syms, rec["va"])) if syms else ""
        print(f"  0x{rec['va']:06x}#{rec['idx']} pc{rec['prim_count']:<3} "
              f"ndcz={ndcz} zfar={zf} {_blendsig(rec['rs'])} "
              f"ZW{rec['rs'].get('ZWRITE')} pos={wps}{sym}")


def cmd_depthdiff(args):
    fa = args.frame_a if args.frame_a is not None else first_draw_frame(args.retail)
    fb = args.frame_b if args.frame_b is not None else first_draw_frame(args.port)
    R = args.cluster

    def key(rec):
        wp = rec["worldpos"]
        if not wp:
            return None
        return (round(wp[0]/R)*R, round(wp[1]/R)*R, round(wp[2]/R)*R,
                rec["rs"].get("SRC"), rec["rs"].get("DEST"), rec["rs"].get("ZWRITE"),
                rec["prim_count"])

    A = [r for r in replay(args.retail) if r["frame"] == fa and r["worldpos"]]
    B = {key(r): r for r in replay(args.port) if r["frame"] == fb and r["worldpos"]}
    print(f"# depthdiff retail={args.retail}#{fa}  port={args.port}#{fb}  cluster={R}")
    print(f"# match by (world-pos/{R}, blend, ZWRITE, pc) -- flags z_far + NDC-z order")
    for ra in A:
        rb = B.get(key(ra))
        if not rb:
            continue
        za, zb = ra["zfar"], rb["zfar"]
        na, nb = ra["ndcz"], rb["ndcz"]
        flags = []
        if za is not None and zb is not None and abs(za - zb) > 1:
            flags.append(f"ZFAR retail={za:.0f} port={zb:.0f}")
        if na is not None and nb is not None and abs(na - nb) > 1e-4:
            flags.append(f"NDCz retail={na:.5f} port={nb:.5f} (Δ{(nb-na):+.5f})")
        if flags:
            wp = ra["worldpos"]
            print(f"  pos=({wp[0]:.2f},{wp[1]:.2f},{wp[2]:.2f}) {_blendsig(ra['rs'])} "
                  f"pc{ra['prim_count']}: " + " | ".join(flags))


def cmd_phase(args):
    tgt, r = _parse_pos(args.near_pos)

    def fingerprint(path):
        """frame -> (pc, worldY, ndcz, count) of the FIRST matched draw + count."""
        per = {}
        for rec in replay(path):
            if not _near(rec["worldpos"], tgt, r):
                continue
            fr = rec["frame"]
            if fr not in per:
                per[fr] = {"pc": rec["prim_count"],
                           "y": rec["worldpos"][1] if rec["worldpos"] else None,
                           "ndcz": rec["ndcz"], "count": 0}
            per[fr]["count"] += 1
        return per

    pa = fingerprint(args.trace)
    fr_a = sorted(pa)
    sel = args.what
    print(f"# phase {args.trace}  near {tgt} r={r}  (pc=anim cells, y=bob, count=spawns)")
    for fr in fr_a:
        d = pa[fr]
        print(f"  f{fr}: pc={d['pc']} y={d['y']:.3f} ndcz={d['ndcz']:.5f} count={d['count']}"
              if d["y"] is not None else f"  f{fr}: pc={d['pc']} count={d['count']}")

    if not args.trace_b:
        return
    pb = fingerprint(args.trace_b)
    fr_b = sorted(pb)
    print(f"# phase {args.trace_b}  ({len(fr_b)} frames) -- cross-correlating '{sel}'")

    def seq(per, frs):
        out = []
        for fr in frs:
            d = per[fr]
            out.append(d["pc"] if sel == "pc" else d["count"] if sel == "count"
                       else d["y"] if sel == "y" else d["ndcz"])
        return out

    sa, sb = seq(pa, fr_a), seq(pb, fr_b)
    # find integer offset of B relative to A minimizing mismatch over overlap
    best = None
    for off in range(-(len(sb)-1), len(sa)):
        pairs = [(sa[i], sb[i-off]) for i in range(len(sa))
                 if 0 <= i-off < len(sb)]
        if len(pairs) < 3:
            continue
        if sel in ("pc", "count"):
            cost = sum(0 if abs(x-y) < 1e-9 else 1 for x, y in pairs) / len(pairs)
        else:
            cost = sum(abs(x-y) for x, y in pairs) / len(pairs)
        if best is None or cost < best[1]:
            best = (off, cost, len(pairs))
    if best:
        off, cost, n = best
        print(f"# best alignment: port is {off:+d} frame(s) vs retail "
              f"(metric={sel}, mean cost {cost:.4f} over {n} overlapping frames)")
        print(f"#   => to compare like-for-like, pair retail frame i with port frame i-({off})")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    d = sub.add_parser("dump"); d.add_argument("trace")
    d.add_argument("--frame", type=int); d.add_argument("--va", type=lambda x: int(x, 0))
    d.set_defaults(func=cmd_dump)

    f = sub.add_parser("diff"); f.add_argument("retail"); f.add_argument("port")
    f.add_argument("--frame-a", type=int); f.add_argument("--frame-b", type=int)
    f.set_defaults(func=cmd_diff)

    p = sub.add_parser("depth"); p.add_argument("trace")
    p.add_argument("--frame", type=int); p.add_argument("--va", type=lambda x: int(x, 0))
    p.add_argument("--near-pos", help="X,Y,Z[,R] filter to draws near a world point")
    p.add_argument("--nm", help="exe to map ret_va -> symbol via nm")
    p.set_defaults(func=cmd_depth)

    pd = sub.add_parser("depthdiff"); pd.add_argument("retail"); pd.add_argument("port")
    pd.add_argument("--frame-a", type=int); pd.add_argument("--frame-b", type=int)
    pd.add_argument("--cluster", type=float, default=0.25,
                    help="world-pos match granularity (default 0.25)")
    pd.set_defaults(func=cmd_depthdiff)

    ph = sub.add_parser("phase"); ph.add_argument("trace"); ph.add_argument("trace_b", nargs="?")
    ph.add_argument("--near-pos", required=True, help="X,Y,Z[,R] of the actor/effect")
    ph.add_argument("--what", choices=["pc", "y", "ndcz", "count"], default="pc")
    ph.set_defaults(func=cmd_phase)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
