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

print(f"OK: drive ({n} checks)")
