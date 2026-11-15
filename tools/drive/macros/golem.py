#!/usr/bin/env python3
"""Arma-golem macro for `rd` (d5 floor 58 / Lapis 100F): a per-tick policy expression played in chunks, no search per hit.

Policy (one `{EXPR}*N` input, re-read every tick):
  attack while the body hovers low   (gst == 0, gbrk == 0, gy < Y, gt < R): walk at the body, swing when within 7.5;
  otherwise retreat to a point D below the body and S·0.2·D to its side (≈ ±11° off the fan's octant line: the fan
  (record 152, t 120) fires 5 shots at the player's octant ±0.38 / ±0.56 rad), clamped inside the arena and out of
  the arena (box |x + 40| ≤ 18, z ≥ −68 inside the radius-~39 floor around (−40, −40): beyond it Louie falls, −23 HP,
  re-placed); a second attack window R2 < gt < R3 after the fan; while the hands' beams sweep (hand tick 140..280, record 136, the main damage source: ~60) an optional
  zone Q (above / below / left / right of the centre) and no attacks unless BA; during rain (state 2) the point (−40, −40 − RAIN) (0 = under the body; the drops spread outward to ≥ 29).
Driver: checkpoint, play CHUNK ticks; HP lost → load the checkpoint and try the next parameter set; all sets fail →
step one chunk further back. Needs the named vars below (`define()` makes them).

usage: golem.py RUN [--chunk 150] [--minutes 590] [--log FILE]
"""
import argparse, itertools, os, random, re, subprocess, sys, time

RD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "rd")
B = "0x76b970"
VARS = {
    "gst": f"s32({B} + 0xa54)", "gt": f"s32({B} + 0xa58)", "gx": f"f32({B} + 0x3f0)", "gy": f"f32({B} + 0x3f4)",
    "gz": f"f32({B} + 0x3f8)", "ghp": f"f32({B} + 0x42c)", "gbrk": f"s32({B} + 0xa60)", "gseg": f"s32({B} + 0xa64)",
    "guse": f"s32({B} + 0x428)", "ht": f"s32({B} + 2*0xba4 + 0xa58)", "hcyc": f"s32({B} + 2*0xba4 + 0xa64)",
    "gd": f"hypot(f32({B} + 0x3f0) - px, f32({B} + 0x3f8) - pz)",
}
# (S side, D distance, R last attack tick, Y max body height)
PARAMS = [(S, D, R, Y, W, RN, O, Q, BA, R2, R3) for W in (0, 6) for S in (1, -1) for D in (13, 18, 24, 28) for R in (100, 80)
          for Y in (2.0, 3.0) for RN in (0, 26) for O in (-8, 0, 8) for Q in (0, 1, 2, 3, 4) for BA in (0, 1)
          for (R2, R3) in ((999, 0), (140, 205), (150, 215))]
TRIES = 20


def rd(*a, timeout=300):
    p = subprocess.run([RD, *a], capture_output=True, text=True, timeout=timeout, env=dict(os.environ))
    return p.stdout + p.stderr


def ev(run, *names):
    out = rd("eval", run, *names)
    vals = []
    for n in names:
        m = re.search(r"^" + re.escape(n) + r" = (\S+)", out, re.M)
        vals.append(float(m.group(1)) if m else None)
    return vals


ZONES = {0: None, 1: (-40, -27), 2: (-40, -62), 3: (-53, -45), 4: (-27, -45)}


def policy(S, D, R, Y, W=0, RAIN=0, O=0, Q=0, BA=1, R2=999, R3=0):
    tx = f"max(-58, min(-22, gx + {S * 0.2 * D + O:.2f}" + (f" + {W} * sin(frame / 15)" if W else "") + "))"
    tz = f"max(-68, gz - {D})"
    bw = "(ht > 140 and ht < 280)"
    win = f"(gt < {R} or (gt > {R2} and gt < {R3}))" if R3 else f"gt < {R}"
    att = f"gst == 0 and gbrk == 0 and gy < {Y} and {win}" + ("" if BA else f" and not {bw}")
    hit = "(A if (p_act == 0 and gd < 7.5 and frame % 2 == 0) else 0)"
    away = f"dpad({tx} - px, {tz} - pz)"
    if ZONES[Q]:
        zx, zz = ZONES[Q]
        away = f"(dpad({zx} - px, {zz} - pz) if {bw} else {away})"
    return (f"{{(dpad(gx - px, gz - pz) | {hit}) if ({att}) else "
            f"(dpad(-40 - px, {-40 - RAIN} - pz) if gst == 2 else {away})}}")


def define(run):
    for n, e in VARS.items():
        rd("ram", run, "define", n, "--expr", f"({e})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--chunk", type=int, default=150)
    ap.add_argument("--minutes", type=float, default=9.5)
    ap.add_argument("--log", default=None)
    ap.add_argument("--base", required=True, help="checkpoint to fall back to when the stack runs out")
    a = ap.parse_args()
    log = open(a.log, "a") if a.log else sys.stdout
    define(a.run)
    t_end = time.time() + 60 * a.minutes
    FAILREG = {}
    stack = []          # checkpoint names of chunk starts (most recent last)
    k = 0
    start_hp = None
    while time.time() < t_end:
        hp, ghp, guse, fr = ev(a.run, "hp", "ghp", "guse", "frame")
        if guse != 1:
            print(f"DEAD body f{int(fr)}", file=log, flush=True); break
        name = f"g{k % 40}"; k += 1
        rd("ck", a.run, "save", name)
        stack.append((name, hp, ghp, fr)); stack = stack[-30:]
        ok = False
        order = PARAMS[:]
        random.shuffle(order)
        for prm in order[:TRIES]:
            o = rd("play", a.run, f"{policy(*prm)}*{a.chunk}")
            if "ran " not in o:
                print("PLAY ERROR " + o[-300:], file=log, flush=True); return
            hp2, ghp2, fr2, gst = ev(a.run, "hp", "ghp", "frame", "gst")
            if hp2 is not None and hp2 >= hp:
                ok = True
                print(f"f{int(fr)}->{int(fr2)} body {ghp:.0f}->{ghp2:.0f} hp {hp:.0f} params {prm} gst {gst:.0f}",
                      file=log, flush=True)
                break
            rd("ck", a.run, "load", name)
        if not ok:
            stack.pop()                                   # the chunk start that failed
            reg = int(fr) // 300
            FAILREG[reg] = FAILREG.get(reg, 0) + 1
            depth = min(8, 1 + FAILREG[reg] // 2 + (random.random() < 0.3))
            prev = None
            for _ in range(depth):
                if stack:
                    prev = stack.pop()
            target = prev[0] if prev else a.base
            rd("ck", a.run, "load", target)
            print(f"  back to {target}" + (f" f{int(prev[3])}" if prev else ""), file=log, flush=True)
            rd("play", a.run, random.choice([".", ". .", "U", "D", "L", "R"]))   # vary the replay
    print("DONE", file=log, flush=True)


if __name__ == "__main__":
    main()
