#!/usr/bin/env python3
"""tools/drive/drive.py: the parts that need no game — input specs, expressions → postfix, the movie."""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "drive"))
import drive as D  # noqa: E402

n = 0


def eq(got, want, what):
    global n
    n += 1
    if got != want:
        raise SystemExit(f"FAIL {what}: got {got!r}, want {want!r}")


# input specs
eq(D.parse_spec("A . A*3 .*30"), [(0x10, 1, False), (0, 1, False), (0x10, 3, False), (0, 30, False)], "tokens")
eq(D.parse_spec("(RA .*2)x3"), [(0x11, 1, False), (0, 2, False)] * 3, "group")
eq(D.parse_spec("~ ~A*2 0x41*4 UR"), [(0, 1, True), (0x10, 2, True), (0x41, 4, False), (0x5, 1, False)], "esc / hex / combo")
eq(D.spec_to_run(D.parse_spec(". .*3 A ~ .*2")), ("0:4,10:1,0:3", [6], 8), "run segments merge, ESC tick is 1-based")
eq(D.segs_to_spec("10:2,0:8,5:1"), "A*2 .*8 UR", "path → spec")
eq(D.mask_name(0x13), "LRA", "mask name")

# expressions
V = D.Vars(None)
V.defs = {"scene": {"addr": "0x438b1c0", "type": "s32"}, "px": {"addr": "0x56da1d8", "type": "f32"},
          "bank": {"expr": "0x44e3798 + s32(0x438b1e0) * 0x2dfc8"}, "gold": {"expr": "s32(bank + 4*3)"}}
SC, PX = str(0x438b1c0), str(0x56da1d8)
eq(V.compile("scene == 1"), [SC, "s32", "1", "=="], "name")
eq(V.compile("px // 0.5"), [PX, "f32", "0.5", "//"], "float literal")
eq(V.compile("gold"), [str(0x44e3798), str(0x438b1e0), "s32", str(0x2dfc8), "*", "+", "4", "3", "*", "+", "s32"], "nested definitions")
eq(V.compile("scene in (0, 9)"), [SC, "s32", "0", "==", SC, "s32", "9", "==", "or"], "in")
eq(V.compile("not (1 < scene <= 6) and frame > 10"),
   ["1", SC, "s32", "<", SC, "s32", "6", "<=", "and", "not", "frame", "10", ">", "and"], "chain / not / frame")
eq(V.compile("3 if scene else abs(-2)"), [SC, "s32", "3", "2", "neg", "abs", "sel"], "ifexp")
eq(V.compile("u8(u32(0x10) + 4)"), ["16", "r32", "4", "+", "r8"], "typed reads nest")
eq([len(t) for t in V.compile_tuple("(scene, int(px), 7)")], [2, 4, 1], "tuple")
for bad in ("nosuch + 1", "scene.x", "__import__('os')", "scene ==", "[1, 2]"):
    try:
        V.compile(bad)
    except SystemExit:
        n += 1
    else:
        raise SystemExit(f"FAIL: {bad!r} compiled")

# math, constants, powers
eq(V.compile("hypot(px - 3, 4)"), [PX, "f32", "3", "-", "4", "hypot"], "hypot")
eq(V.compile("atan2(1, scene) * 2 ** 3"), ["1", SC, "s32", "atan2", "2", "3", "pow", "*"], "atan2 / pow")
eq(V.compile("-sqrt(sin(pi))")[-3:], ["sin", "sqrt", "neg"], "sqrt / sin / pi / unary minus")

# task files: a label may hold ": ", the expression a comment
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, "t.md")
    open(p, "w").write("---\nname: t\nstart: fresh\nsteps:\n  - in game: scene == 1\n"
                       "  - day 22: the payment: scene >= 22   # note: late\nforbid:\n  - title: scene == 0\n---\nprose\n")
    t = D.load_task(p)
    eq(t["steps"], [("in game", "scene == 1"), ("day 22: the payment", "scene >= 22")], "step labels")
    eq(t["forbid"], [("title", "scene == 0")], "forbid")

# the movie: server text → file → load → run arguments
mv = D.Movie.from_server("ok frame=20 n=6 poked=0 ops=r0:4de1,m1:0,m5:10,e7,m8:0,m12:1", None)
eq((mv.frame, len(mv.ops)), (20, 6), "from_server")
with tempfile.TemporaryDirectory() as d:
    p = os.path.join(d, "movie.jsonl")
    mv.write(p)
    text = open(p).read()
    eq(text.splitlines()[1], '{"savefile": "@fresh"}', "fresh savefile line")
    eq(text.splitlines()[-1], '{"frame": 20, "buttons": "0x0001"}', "end marker = movie length, last mask")
    back = D.Movie.load(p)
    eq(back.ops[:6], mv.ops, "round trip")
    eq(back.frame, 20, "length from the end marker")
eq(mv.run_args(0, 20), "0:4,10:3,0:4,1:9 esc=7 rng=0:4de1".replace(" rng=0:4de1", ""), "whole movie as a run (the f0 seed is the launch's)")
eq(mv.run_args(6, 12), "10:1,0:4,1:1 esc=1", "a window: mask carried in, ESC tick relative")
eq(mv.truncated(7).frame, 7, "truncate")
eq(mv.mask_at(6), 0x10, "mask_at")

# --watch / --where lists: split on top-level commas only
eq(D.split_top("hp, hypot(a0x - px, a0z - pz), (a, b)[0],, x in (1, 2)"),
   ["hp", "hypot(a0x - px, a0z - pz)", "(a, b)[0]", "x in (1, 2)"], "split_top")
eq(D.split_top(None), [], "split_top: nothing")

# sum(..): additions, a left fold
eq(V.compile("sum(scene, 2, frame)"), [SC, "s32", "2", "+", "frame", "+"], "sum(a, b, c)")
eq(V.compile("sum((1, scene))"), ["1", SC, "s32", "+"], "sum(tuple)")
eq(V.compile("sum([scene == 1, scene == 2])"), [SC, "s32", "1", "==", SC, "s32", "2", "==", "+"], "sum(list)")

V.defs["pz"] = {"addr": "0x56da1e0", "type": "f32"}
# actors(COND): per slot, in use (U != 0) and COND; T U HP X Z .. bound to the slot's fields, I = the slot index
A0, SZ = D.ACTOR_BASE, D.ACTOR_SIZE
t = V.compile("actors(T == 10, 0, 1)")
eq(t, [str(A0 + 0x428), "s32", str(A0 + 0x424), "s32", "10", "==", "and",
       str(A0 + SZ + 0x428), "s32", str(A0 + SZ + 0x424), "s32", "10", "==", "and", "+"], "actors: two slots")
eq(len(V.compile("actors(T == 10)")), 64 * 8 - 1, "actors: 64 slots by default")
eq(V.compile("actors(U == 2, 3)")[:4], [str(A0 + 0x428), "s32", "2", "=="], "actors: U named = no implicit in-use test")
eq(V.compile("actors(HP, 5, 5)"), [str(A0 + 5 * SZ + 0x428), "s32", str(A0 + 5 * SZ + 0x42c), "f32", "and"], "actors: one slot")
eq(V.compile("actors(I == 2 and U, 2, 2)"), ["2", "2", "==", str(A0 + 2 * SZ + 0x428), "s32", "and"], "actors: I")
for bad in ("actors(actors(T))", "actors(T, 0, 200)", "actors(T, px)", "sum(x for x in (1, 2))"):
    try:
        V.compile(bad)
    except SystemExit:
        n += 1
    else:
        raise SystemExit(f"FAIL: {bad!r} compiled")

# the server's limits: ≤ 598 tokens per program (more are cut silently), ≤ 64 stack values
eq(D.stack_depth(["1", "2", "3", "+", "+"]), 3, "stack depth")
eq(D.stack_depth(V.compile("actors(T == 10)")), 4, "a fold stays shallow")
for toks in (["1"] * 300 + ["+"] * 299, ["1"] * 65 + ["+"] * 64):
    try:
        D.prog_check(toks)
    except SystemExit:
        n += 1
    else:
        raise SystemExit("FAIL: prog_check let an over-long / over-deep program through")
long_ = "actors(U == 1 and hypot(X - px, Z - pz) < 12)"
parts = V.compile_parts(long_)
eq((len(parts) > 1, all(len(p) <= D.MAX_PROG_TOKENS for p in parts)), (True, True), "an over-long sum is split")
eq(V.compile_parts("scene + 1"), [[SC, "s32", "1", "+"]], "a short expression stays one program")
try:
    V.program(long_)
except SystemExit:
    n += 1
else:
    raise SystemExit("FAIL: a stop condition over the limit compiled")


# a fake live server: a postfix VM over a fake memory, the real limits (eval ≤ 64 ids, prog ≤ 598 tokens)
class Fake(D.Live):
    def __init__(self, mem):
        self.mem, self.progs, self.nprog, self.evals = mem, {}, 0, []

    def run(self, toks):
        st = []
        for t in toks:
            if t in ("s32", "f32"):
                st.append(self.mem.get(int(st.pop()), 0))
            elif t in ("+", "-", "*", "==", "!=", "<", "and", "or", "hypot"):
                b, a = st.pop(), st.pop()
                st.append({"+": a + b, "-": a - b, "*": a * b, "==": float(a == b), "!=": float(a != b), "<": float(a < b),
                           "and": float(bool(a) and bool(b)), "or": float(bool(a) or bool(b)), "hypot": (a * a + b * b) ** 0.5}[t])
            elif t == "neg":
                st.append(-st.pop())
            else:
                st.append(float(t))
        return st[-1]

    def cmd(self, line, check=True):
        w = line.split()
        if w[0] == "prog":
            assert len(w) - 2 <= D.MAX_PROG_TOKENS
            self.progs[int(w[1])] = w[2:]
            return "ok"
        ids = [int(x) for x in w[1].split(",")]
        assert w[0] == "eval" and len(ids) <= 64 and all(i in self.progs for i in ids)
        self.evals.append(len(ids))
        return "ok v=" + ":".join(repr(self.run(self.progs[i])) for i in ids)


mem = {int(V.defs["px"]["addr"], 16): -40.0, int(V.defs["pz"]["addr"], 16): -45.0}
for i, (ty, use, x, z) in enumerate([(67, 1, -45, -34), (10, 1, -41, -44), (10, 2, -30, -50), (10, 0, -40, -45), (10, 1, -40, -60)]):
    b = A0 + i * SZ
    mem.update({b + 0x424: ty, b + 0x428: use, b + 0x3f0: x, b + 0x3f8: z})
fk = Fake(mem)
eq(D.eval_exprs(fk, V, ["actors(T == 10)", "actors(U == 1 and T == 10)", long_, "actors(1, 0, 127)"]), [3, 2, 1, 4],
   "actors() counts, split programs summed")
fk.evals.clear()
eq(D.eval_exprs(fk, V, [f"{k} + 1" for k in range(600)]), [k + 1 for k in range(600)], "600 expressions")
eq(max(fk.evals) <= D.EVAL_CHUNK and sum(fk.evals) == 600, True, "eval batched below the server's 64 ids")
ids, sl = D.sampled_cols(fk, V, ["scene", long_])
eq((len(ids), sl[0], sl[1][1]), (len(parts) + 1, (0, 1), len(parts) + 1), "sampled columns: a split column takes several ids")
eq(D.fold_row([1.0, 2.0, 3.0, 0.5], [(0, 1), (1, 4)]), [1, 5.5], "fold a sampled row")
try:
    D.sampled_cols(fk, V, ["scene"] * 33)
except SystemExit:
    n += 1
else:
    raise SystemExit("FAIL: 33 sampled columns accepted")

# movie windows that fit one `run`: ≤ 16 ESC / rngseed ops each
mv2 = D.Movie(None, [(f, "e", 0) for f in range(10, 50)] + [(60, "m", 0x10)], 100)
mv2.ops.sort()
ch = mv2.chunks(0, 100)
eq((ch[0][0], ch[-1][1], all(a2 == b2 for (_, a2), (b2, _) in zip(ch, ch[1:]))), (0, 100, True), "chunks cover the window")
eq(all(sum(1 for f, k, _ in mv2.ops if a2 < f <= b2 and k == "e") <= 16 for a2, b2 in ch), True, "≤ 16 ESC per chunk")
eq(mv.chunks(6, 12), [(6, 12)], "a small window is one chunk")
eq(D.parse_samples("5:1:2.5;6:0:-1"), [(5, [1.0, 2.5]), (6, [0.0, -1.0])], "samples")

# scan rows: --every / --changes / --where
rows = [(10, [1, 0, 1]), (11, [1, 0, 0]), (12, [2, 0, 1]), (13, [2, 0, 1]), (14, [3, 1, 1])]
eq(D.scan_filter(rows, 10, 3), [(f, v) for f, v in rows], "scan: every row")
eq([f for f, _ in D.scan_filter(rows, 10, 3, every=2)], [10, 12, 14], "scan: --every")
eq([f for f, _ in D.scan_filter(rows, 10, 2, changes=True)], [10, 12, 14], "scan: --changes")
eq([f for f, _ in D.scan_filter(rows, 10, 2, where=True)], [10, 12, 13, 14], "scan: --where (the last column)")
eq(D.scan_filter(rows, 10, 2, where=True)[0], (10, [1, 0]), "scan: the where column is not shown")

print(f"OK: drive ({n} checks)")
