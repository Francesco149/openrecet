#!/usr/bin/env python3
"""Replay a d3d_trace.jsonl op stream and snapshot the LIVE render state at
each Draw call, keyed by ret_va (+0x400000 → real VA).

The raw trace is the call stream (SetRenderState / SetTransform / Draw…), not a
per-draw state snapshot.  This replays the D3D8 state machine so we can read the
actual ZENABLE / ZWRITEENABLE / ZFUNC / ALPHATEST{ENABLE,REF,FUNC} / blend / cull
that was live at every DrawPrimitive — the per-draw RENDER CONTRACT (plan Phase 0).

Usage:
  d3d_state_diff.py dump  <trace.jsonl> [--frame N] [--va 0xXXXXXX]
  d3d_state_diff.py diff  <retail.jsonl> <port.jsonl> [--frame-a N] [--frame-b M]

`dump` prints one row per Draw (frame, real_va, state) for the chosen frame
(default: the first frame that has any Draw).  `diff` aligns the two traces'
draws by (real_va, per-va index) for one frame each and flags state deltas.
"""
import argparse, json, sys, collections

VA_BASE = 0x400000

# D3DRENDERSTATETYPE subset we care about for the depth/alpha/blend contract.
RS = {7: "ZENABLE", 14: "ZWRITE", 23: "ZFUNC", 15: "ATESTEN",
      24: "AREF", 25: "AFUNC", 19: "SRC", 20: "DEST", 22: "CULL", 28: "FOG"}
DRAW_OPS = {"DrawPrimitive", "DrawIndexedPrimitive",
            "DrawPrimitiveUP", "DrawIndexedPrimitiveUP"}

# value enums for readability
ZFUNC = {1: "NEVER", 2: "LESS", 3: "EQUAL", 4: "LE", 5: "GREATER", 6: "NE",
         7: "GE", 8: "ALWAYS"}
BLEND = {1: "ZERO", 2: "ONE", 3: "SRCCOLOR", 4: "INVSRCCOLOR", 5: "SRCALPHA",
         6: "INVSRCALPHA", 7: "DESTALPHA", 8: "INVDESTALPHA", 9: "DESTCOLOR",
         10: "INVDESTCOLOR"}


def replay(path):
    """Yield (frame, real_va, state_dict, per_va_index) for every Draw."""
    state = {}
    va_idx = collections.Counter()   # reset per frame
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
        if op == "SetRenderState":
            a = r["args"]
            state[a["state"]] = a["value"]
        elif op in DRAW_OPS:
            va = (r.get("ret_va") or 0) + VA_BASE
            snap = {name: state.get(code) for code, name in RS.items()}
            snap["_op"] = op
            yield fr, va, snap, va_idx[va]
            va_idx[va] += 1


def fmt(snap):
    def g(k, m=None):
        v = snap.get(k)
        if v is None:
            return f"{k}=?"
        if m:
            return f"{k}={m.get(v, v)}"
        return f"{k}={v}"
    return " ".join([g("ZENABLE"), g("ZWRITE"), g("ZFUNC", ZFUNC), g("ATESTEN"),
                     g("AREF"), g("AFUNC", ZFUNC), g("SRC", BLEND),
                     g("DEST", BLEND), g("CULL")])


def first_draw_frame(path):
    for fr, va, snap, i in replay(path):
        return fr
    return None


def cmd_dump(args):
    frame = args.frame if args.frame is not None else first_draw_frame(args.trace)
    print(f"# {args.trace}  frame={frame}")
    n = 0
    for fr, va, snap, i in replay(args.trace):
        if fr != frame:
            continue
        if args.va is not None and va != args.va:
            continue
        n += 1
        print(f"{n:3d} va=0x{va:06x}#{i} {snap['_op']:18s} {fmt(snap)}")


def cmd_diff(args):
    A = [(va, i, snap) for fr, va, snap, i in replay(args.retail)
         if fr == (args.frame_a if args.frame_a is not None else first_draw_frame(args.retail))]
    B = [(va, i, snap) for fr, va, snap, i in replay(args.port)
         if fr == (args.frame_b if args.frame_b is not None else first_draw_frame(args.port))]
    bmap = {(va, i): snap for va, i, snap in B}
    print(f"# retail={args.retail} port={args.port}")
    keys = ["ZENABLE", "ZWRITE", "ZFUNC", "ATESTEN", "AREF", "AFUNC", "SRC", "DEST", "CULL"]
    for va, i, sa in A:
        sb = bmap.get((va, i))
        if sb is None:
            print(f"va=0x{va:06x}#{i}  PORT-MISSING  retail:[{fmt(sa)}]")
            continue
        deltas = [k for k in keys if sa.get(k) != sb.get(k)]
        flag = "  DELTA:" + ",".join(deltas) if deltas else "  ok"
        print(f"va=0x{va:06x}#{i}{flag}")
        if deltas:
            print(f"    retail: {fmt(sa)}")
            print(f"    port  : {fmt(sb)}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    d = sub.add_parser("dump"); d.add_argument("trace")
    d.add_argument("--frame", type=int); d.add_argument("--va", type=lambda x: int(x, 0))
    d.set_defaults(func=cmd_dump)
    f = sub.add_parser("diff"); f.add_argument("retail"); f.add_argument("port")
    f.add_argument("--frame-a", type=int); f.add_argument("--frame-b", type=int)
    f.set_defaults(func=cmd_diff)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
