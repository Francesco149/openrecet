#!/usr/bin/env python3
"""rd — drive retail Recettear live, at savestate speed, and turn what you did into a verified trace.

One command per call; the run directory is the state. Behind a run sits a LIVE rhost process
(retail on the headless devices, `rhost --live`): it steps ticks on demand, reads memory, takes
in-process savestates and runs the exploration search. That process is only a cache — a run is
its MOVIE (every input since boot); a dead session is rebuilt by replaying it.

    rd new R --from fresh | SCENARIO[@F] | RUN[:CK|@F] | end:SCENARIO     start a run
    rd play R "A . A*3 .*30 ~ R*40"  [--until EXPR] [--avoid EXPR] [--watch EXPR,..] [--every N]
    rd tap R "A" [--n 40] [--gap 8] [--until EXPR]                        press, release, repeat
    rd status R | rd text R | rd eval R EXPR.. [--at CK | --at-frame F] | rd ram R list | read VA [N] | dwords VA [N] | str VA | define NAME .. | mark | diff
    rd scan R --from F [--to T] --watch EXPR,.. [--every N] [--changes] [--where EXPR] [--at-run RUN] [--fresh]
                                                rows of state at earlier frames: the movie replayed in a side session
    rd ck R save NAME | load NAME | list        rd undo R        rd rewind R --to F
    rd search R --goal EXPR [--avoid EXPR] [--cell TUPLE] [--actions SPEC ..] [--hold N [--hold-actions SPEC ..]] [--apply] [-j N]
    rd look R [--at F,F..]      rd strip R --from F --to F [--every N]    pictures (real-device replay)
    rd verify R [--real]        rd check R TASK.md      rd lint TASK.md --on RUN..
    rd file R NAME              tests/scenarios/NAME (then: rhost.py golden / triage / portrng NAME)
    rd port R [NAME]            file + retail golden + the port's first split from it
    rd accept R NAME [--task T] check → verify --real → file → golden → triage → report into docs/findings/probes/
    rd stop R | rd ls

Method and the full reference: tools/drive/README.md; the one-page card for a driving agent:
tools/drive/DRIVER.md.
"""
import argparse
import ast
import math
import fcntl
import glob
import gzip
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "rhost"))
import rhost as RH  # noqa: E402
sys.path.insert(0, os.path.join(ROOT, "tools", "bunker"))
import bunker as BK  # noqa: E402

RUNS = os.environ.get("RD_RUNS", "/mnt/ephemeral/openrecet/drive/runs")
SESS = os.path.join(RH.WINE_ROOT, "runs", "drive")          # live sessions' rhost run dirs (C:\orc-rhost\runs\drive\..)
VARS = os.path.join(HERE, "vars.json")
# Live sessions skip retail's overlay particle dispatcher FUN_00414ee2 (~48% of a 3D-scene tick): `rhost.py skip-check
# 414ee2 --mask …` = 121/121 scenarios end in the same frame / RNG / game state / save.dat (2026-11-10), bar two render
# scratch blocks the function writes (masked out of the state hash in both modes): the quad 76b750..76b7b0 and the two
# strip vertex tables 64c514..64da38 (the second, 64d408.., is draw kind 7's arc: Griff's skill, 2026-11-11). Nothing
# accepted rests on it: `rd verify` replays the movie WITHOUT the skip and must reach the session's state.
# RD_SKIP_FN="" turns it off.
SKIP_FN = os.environ.get("RD_SKIP_FN", "414ee2")
SKIP_MASK = os.environ.get("RD_SKIP_MASK", "76b750:76b7b0,64c514:64da38")
SKIP_MASK_V1 = "76b750:76b7b0,64c514:64d408"     # sessions started before 2026-11-11 (no "mask" in their session.json)


def skip_args():
    return ["--skip-fn", SKIP_FN, "--mask-range", SKIP_MASK] if SKIP_FN else []


SCEN_SAVES = os.path.join(RH.SCEN, "_saves")

# ------------------------------------------------------------------ buttons / input specs
BTN = {"R": 0x1, "L": 0x2, "U": 0x4, "D": 0x8, "A": 0x10, "B": 0x20, "C": 0x40, "X": 0x80, "Y": 0x100,
       "1": 0x200, "2": 0x400, "3": 0x800, "4": 0x1000, "5": 0x2000}
BTN_ORDER = "UDLRABCXY12345"


def mask_name(m):
    return "".join(k for k in BTN_ORDER if m & BTN[k]) or "."


def parse_token(t):
    """one token → [(mask, n, esc)]: RA*8, .*30, 0x11*4, ~ (ESC key press on a no-button tick), ~A"""
    m = re.fullmatch(r"(~?)([A-Za-z0-9.]*?)(?:\*(\d+))?", t)
    if not m or (not m.group(1) and not m.group(2)):
        raise SystemExit(f"bad input token {t!r} (e.g. A  RA*8  .*30  ~  0x11*4)")
    esc, body, n = bool(m.group(1)), m.group(2) or ".", int(m.group(3) or 1)
    if body.lower().startswith("0x"):
        mask = int(body, 16)
    elif body == ".":
        mask = 0
    else:
        mask = 0
        for ch in body.upper():
            if ch not in BTN:
                raise SystemExit(f"bad button {ch!r} in {t!r} (buttons: {' '.join(BTN)}; . = none; ~ = ESC key)")
            mask |= BTN[ch]
    return [(mask, n, esc)]


def parse_spec(spec):
    """"A . (A .*7)x12 R*40" → [(mask, n, esc)]"""
    spec = spec.strip().strip("\"'`").strip()
    out, i = [], 0
    while i < len(spec):
        if spec[i].isspace():
            i += 1
        elif spec[i] == "{":                    # {EXPR}*N: the mask is the expression's value, read before each tick
            j = spec.find("}", i)
            if j < 0:
                raise SystemExit("unclosed { in the input spec: {pad(ex - px, ez - pz) | A}*12")
            m = re.match(r"\*(\d+)", spec[j + 1:])
            out.append((("expr", spec[i + 1:j]), int(m.group(1)) if m else 1, False))
            i = j + 1 + (m.end() if m else 0)
        elif spec[i] == "(":
            j, depth, brace = i, 0, 0            # the group's own ")": nested groups and {EXPR} parentheses skipped
            while j < len(spec):
                ch = spec[j]
                brace += (ch == "{") - (ch == "}")
                if not brace:
                    depth += (ch == "(") - (ch == ")")
                    if depth == 0:
                        break
                j += 1
            if j >= len(spec):
                raise SystemExit("unclosed ( in the input spec")
            m = re.match(r"x(\d+)", spec[j + 1:])
            if not m:
                raise SystemExit("a group needs a repeat count: (A .*7)x12")
            out += parse_spec(spec[i + 1:j]) * int(m.group(1))
            i = j + 1 + m.end()
        else:
            j = i
            while j < len(spec) and not spec[j].isspace() and spec[j] != "(":
                j += 1
            out += parse_token(spec[i:j])
            i = j
    return out


def spec_to_run(items, prog=None):
    """[(mask, n, esc)] → (segments "m:n,m:n", [esc ticks], total); prog(expr) → program id, for {EXPR} masks"""
    segs, escs, t = [], [], 0
    for mask, n, esc in items:
        if n <= 0:
            continue
        if esc:
            escs.append(t + 1)
        if segs and segs[-1][0] == mask:
            segs[-1][1] += n
        else:
            segs.append([mask, n])
        t += n
    def one(m):
        if isinstance(m, tuple):
            if prog is None:
                raise SystemExit("{EXPR} masks need a live session (play / tap / search actions)")
            return f"p{prog(m[1])}"
        return f"{m:x}"
    return ",".join(f"{one(m)}:{n}" for m, n in segs), escs, t


def segs_to_spec(path):
    """server path "10:2,0:8" → "A*2 .*8\""""
    out = []
    for s in filter(None, path.split(",")):
        m, n = s.split(":")
        out.append(mask_name(int(m, 16)) + (f"*{n}" if n != "1" else ""))
    return " ".join(out)


# ------------------------------------------------------------------ expressions → postfix programs
# rh_live.c limits: a command line is split into at most 600 tokens (`prog ID` + 598: a longer program is cut
# SILENTLY), the VM stack holds 64 values, NPROG 256 program ids (the client wraps its ids at 250), `eval` takes
# 64 ids, `run s=` 32, `run esc=` / `rng=` 16 each, a command line 64 KiB.
MAX_PROG_TOKENS = 598
MAX_STACK = 64
EVAL_CHUNK = 48                   # programs defined + evaluated per `eval` (< 64 ids, far from the id wrap at 250)
MAX_SAMPLED = 32
MAX_RUN_OPS = 16
MAX_LINE = 60000

# actors(COND): COND is evaluated per actor slot (0x76b970 + i*0xba4) with these names bound to the slot's fields
ACTOR_BASE, ACTOR_SIZE, ACTOR_SLOTS = 0x76b970, 0xba4, 64
ACTOR_FIELDS = {"T": (0x424, "s32"), "U": (0x428, "s32"), "HP": (0x42c, "f32"), "X": (0x3f0, "f32"), "Y": (0x3f4, "f32"),
                "Z": (0x3f8, "f32"), "YAW": (0x420, "f32"), "ST": (0xa54, "s32"), "TK": (0xa58, "s32"), "SUB": (0xa5c, "s32"),
                "POW": (0xab8, "f32"), "UNHIT": (0xb08, "s32"), "STAG": (0x440, "s32"), "AGE": (0x398, "s32"), "AE4": (0xae4, "s32")}

_ARITY = {"r8": 1, "r16": 1, "r32": 1, "s8": 1, "s16": 1, "s32": 1, "f32": 1, "f64": 1, "~": 1, "not": 1, "neg": 1, "abs": 1,
          "sqrt": 1, "sin": 1, "cos": 1, "sel": 3, "frame": 0, "rng": 0}
_BINARY = {"+", "-", "*", "/", "//", "%", "&", "|", "^", "<<", ">>", "<", "<=", ">", ">=", "==", "!=", "and", "or", "min", "max",
           "atan2", "hypot", "pow"}


def stack_depth(tokens):
    """the VM stack's high-water mark for a postfix program"""
    sp = hi = 0
    for t in tokens:
        if t in _BINARY:
            sp -= 1
        elif t in _ARITY:
            sp += 1 - _ARITY[t]
        else:
            sp += 1
        hi = max(hi, sp)
    return hi


def prog_check(tokens, what="expression"):
    """a program the server runs as written: ≤ MAX_PROG_TOKENS tokens (more are cut without an error), ≤ MAX_STACK deep"""
    if len(tokens) > MAX_PROG_TOKENS:
        raise SystemExit(f"{what}: {len(tokens)} postfix tokens > {MAX_PROG_TOKENS} (the live server's limit). Values read by "
                         "eval / --watch / scan may be sums of programs (a top-level `+`, sum(..), actors(..)); a stop condition "
                         "(--until, --avoid, --goal, --score, a task step) is one program: narrow it — actors(COND, LO, HI) over fewer slots")
    if stack_depth(tokens) > MAX_STACK:
        raise SystemExit(f"{what}: needs a stack of {stack_depth(tokens)} values > {MAX_STACK}: nest less (a + (b + (c + ..))) → a + b + c ..)")
    return tokens


def split_top(s, sep=","):
    """"a, hypot(b, c), (d, e)" → ["a", "hypot(b, c)", "(d, e)"]: split on SEP outside brackets only; empty items dropped"""
    out, depth, cur = [], 0, []
    for ch in s or "":
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == sep and depth == 0:
            out.append("".join(cur)); cur = []
        else:
            cur.append(ch)
    out.append("".join(cur))
    return [x.strip() for x in out if x.strip()]


TYPES ={"u8": "r8", "s8": "s8", "u16": "r16", "s16": "s16", "u32": "r32", "s32": "s32", "i32": "s32", "f32": "f32", "f64": "f64"}
BINOP = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/", ast.FloorDiv: "//", ast.Mod: "%", ast.BitAnd: "&",
         ast.BitOr: "|", ast.BitXor: "^", ast.LShift: "<<", ast.RShift: ">>"}
CMPOP = {ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">=", ast.Eq: "==", ast.NotEq: "!="}


class Vars:
    """named state: tools/drive/vars.json (shared, committed) + <run>/vars.json (the run's own)"""

    def __init__(self, run_dir=None, also=()):
        """ALSO: other runs' dirs whose names are visible too (below this run's own)"""
        self.defs, self.local_path, self._slot = {}, run_dir and os.path.join(run_dir, "vars.json"), None
        for p in [VARS] + [os.path.join(d, "vars.json") for d in also] + [self.local_path]:
            if p and os.path.exists(p):
                self.defs.update({k: v for k, v in json.load(open(p)).items() if not k.startswith("_")})

    def define(self, name, d, shared=False):
        p = VARS if shared else self.local_path
        cur = json.load(open(p)) if os.path.exists(p) else {}
        cur[name] = d
        json.dump(cur, open(p + ".tmp", "w"), indent=1)
        os.replace(p + ".tmp", p)
        self.defs[name] = d

    def compile(self, expr, depth=0):
        """infix (Python syntax over named vars) → list of postfix tokens"""
        try:
            tree = ast.parse(expr.strip(), mode="eval").body
        except SyntaxError as e:
            raise SystemExit(f"bad expression {expr!r}: {e.msg}")
        return self._c(tree, depth, expr)

    def _c(self, n, depth, src):
        c = lambda x: self._c(x, depth, src)
        if isinstance(n, ast.Constant) and isinstance(n.value, (int, float, bool)):
            return [repr(float(n.value)) if isinstance(n.value, float) else str(int(n.value))]
        if isinstance(n, ast.Name):
            if self._slot is not None and n.id in self._slot:
                return list(self._slot[n.id])
            if n.id in ("frame", "rng"):
                return [n.id]
            if n.id == "pi" and "pi" not in self.defs:
                return [repr(math.pi)]
            if n.id not in self.defs and n.id.isupper() and all(ch in BTN for ch in n.id):
                return [str(sum({BTN[ch] for ch in n.id}))]         # a button mask: A, UR, B
            if n.id not in self.defs:
                raise SystemExit(f"unknown name {n.id!r} in {src!r} — `rd ram R list`, or define it (`rd ram R define`)")
            if depth > 12:
                raise SystemExit(f"variable definitions recurse at {n.id!r}")
            d = self.defs[n.id]
            if "expr" in d:
                return self.compile(d["expr"], depth + 1)
            return [str(int(str(d["addr"]), 16)), TYPES[d.get("type", "s32")]]
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name):
            f = n.func.id
            if f in TYPES and len(n.args) == 1:
                return c(n.args[0]) + [TYPES[f]]
            if f in ("min", "max") and len(n.args) >= 2:
                out = c(n.args[0])
                for a in n.args[1:]:
                    out += c(a) + [f]
                return out
            if f == "sum":
                return self._fold([c(e) for e in self._sum_args(n, src)])
            if f == "actors":
                return self._fold(self._actor_terms(n, depth, src))
            if f == "abs" and len(n.args) == 1:
                return c(n.args[0]) + ["abs"]
            if f == "int" and len(n.args) == 1:
                return c(n.args[0]) + ["1", "//"]
            if f in ("sqrt", "sin", "cos") and len(n.args) == 1:
                return c(n.args[0]) + [f]
            if f in ("atan2", "hypot", "pow") and len(n.args) == 2:
                return c(n.args[0]) + c(n.args[1]) + [f]
            if f in ("pad", "dpad") and len(n.args) == 2:
                # the d-pad mask (8 directions) that walks along the world vector (dx, dz): pad = the shop's axes
                # (R = +x, D = +z), dpad = the dungeons' (flipped). 0 when the vector is 0.
                x, z = (f"({'-' if f == 'dpad' else ''}({ast.unparse(a)}))" for a in n.args)
                return self.compile(f"((1 if {x} > 0 else 2) if abs({x}) > 0.41421356 * abs({z}) else 0) + "
                                    f"((8 if {z} > 0 else 4) if abs({z}) > 0.41421356 * abs({x}) else 0)", depth + 1)
            raise SystemExit(f"unknown function {f}() in {src!r} (u8 s8 u16 s16 u32 s32 f32 f64 min max abs int sqrt sin cos atan2 hypot pow "
                             "pad dpad sum actors)")
        if isinstance(n, ast.BinOp) and isinstance(n.op, ast.Pow):
            return c(n.left) + c(n.right) + ["pow"]
        if isinstance(n, ast.BinOp) and type(n.op) in BINOP:
            return c(n.left) + c(n.right) + [BINOP[type(n.op)]]
        if isinstance(n, ast.UnaryOp):
            if isinstance(n.op, ast.USub):
                return c(n.operand) + ["neg"]
            if isinstance(n.op, ast.Not):
                return c(n.operand) + ["not"]
            if isinstance(n.op, ast.Invert):
                return c(n.operand) + ["~"]
            if isinstance(n.op, ast.UAdd):
                return c(n.operand)
        if isinstance(n, ast.BoolOp):
            out = c(n.values[0])
            for v in n.values[1:]:
                out += c(v) + ["and" if isinstance(n.op, ast.And) else "or"]
            return out
        if isinstance(n, ast.IfExp):
            return c(n.test) + c(n.body) + c(n.orelse) + ["sel"]
        if isinstance(n, ast.Compare):
            out, left = None, n.left
            for op, right in zip(n.ops, n.comparators):
                if isinstance(op, (ast.In, ast.NotIn)):
                    if not isinstance(right, (ast.Tuple, ast.List)) or not right.elts:
                        raise SystemExit(f"`in` needs a tuple of values in {src!r}")
                    one = None
                    for e in right.elts:
                        t = c(left) + c(e) + ["=="]
                        one = t if one is None else one + t + ["or"]
                    if isinstance(op, ast.NotIn):
                        one += ["not"]
                elif type(op) in CMPOP:
                    one = c(left) + c(right) + [CMPOP[type(op)]]
                else:
                    raise SystemExit(f"unsupported comparison in {src!r}")
                out = one if out is None else out + one + ["and"]
                left = right
            return out
        raise SystemExit(f"unsupported syntax in {src!r}: {ast.dump(n)[:80]}")

    @staticmethod
    def _fold(terms):
        """[t1, t2, t3] → t1 t2 + t3 + (a left fold: the stack stays 2 deep)"""
        out = list(terms[0]) if terms else ["0"]
        for t in terms[1:]:
            out += t + ["+"]
        return out

    @staticmethod
    def _sum_args(n, src):
        """sum(e1, e2, ..) or sum((e1, e2, ..)) / sum([..]) → the element nodes"""
        if n.keywords:
            raise SystemExit(f"sum() takes no keywords in {src!r}")
        if len(n.args) == 1 and isinstance(n.args[0], (ast.Tuple, ast.List)):
            return n.args[0].elts
        if len(n.args) == 1 and not isinstance(n.args[0], ast.Constant):
            raise SystemExit(f"sum() takes the terms themselves: sum(a, b, c) or sum((a, b, c)) — no comprehensions ({src!r})")
        return n.args

    def _actor_terms(self, n, depth, src):
        """actors(COND[, LO, HI]) → one program per slot LO..HI (default 0..63): 1 when the slot is in use (U != 0, unless
        COND names U itself) and COND holds; COND reads the slot as T U HP X Y Z YAW ST TK SUB POW UNHIT STAG AGE AE4, and I
        (the slot index)"""
        if self._slot is not None:
            raise SystemExit(f"actors() cannot nest ({src!r})")
        if not 1 <= len(n.args) <= 3 or n.keywords:
            raise SystemExit(f"actors(COND) / actors(COND, HI) / actors(COND, LO, HI) in {src!r}")
        try:
            rng_ = [int(ast.literal_eval(a)) for a in n.args[1:]]
        except ValueError:
            raise SystemExit(f"actors(): the slot range must be numbers ({src!r})")
        lo, hi = (0, ACTOR_SLOTS - 1) if not rng_ else (0, rng_[0]) if len(rng_) == 1 else tuple(rng_)
        if not 0 <= lo <= hi < 128:
            raise SystemExit(f"actors(): slots {lo}..{hi} outside 0..127 ({src!r})")
        cond = n.args[0]
        names_u = any(isinstance(x, ast.Name) and x.id == "U" for x in ast.walk(cond))
        terms = []
        for i in range(lo, hi + 1):
            base = ACTOR_BASE + i * ACTOR_SIZE
            self._slot = {k: [str(base + off), TYPES[ty]] for k, (off, ty) in ACTOR_FIELDS.items()}
            self._slot["I"] = [str(i)]
            try:
                t = self._c(cond, depth, src)
                if not names_u:
                    t = self._slot["U"] + t + ["and"]                 # and → 0 / 1
                elif not (isinstance(cond, (ast.Compare, ast.BoolOp)) or (isinstance(cond, ast.UnaryOp) and isinstance(cond.op, ast.Not))):
                    t = t + ["0", "!="]                              # a count, not a sum of values
            finally:
                self._slot = None
            terms.append(t)
        return terms

    def _terms(self, n, depth, src):
        """the top-level summands of an expression: [(sign, tokens)] (a + b - c, sum(..), actors(..), a name defined as one)"""
        if isinstance(n, ast.BinOp) and isinstance(n.op, (ast.Add, ast.Sub)):
            right = self._terms(n.right, depth, src)
            if isinstance(n.op, ast.Sub):
                right = [(-s, t) for s, t in right]
            return self._terms(n.left, depth, src) + right
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == "sum":
            return [x for e in self._sum_args(n, src) for x in self._terms(e, depth, src)]
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id == "actors":
            return [(1, t) for t in self._actor_terms(n, depth, src)]
        if isinstance(n, ast.Name) and n.id in self.defs and "expr" in self.defs[n.id] and depth <= 12:
            d = self.defs[n.id]["expr"]
            try:
                return self._terms(ast.parse(d.strip(), mode="eval").body, depth + 1, d)
            except SyntaxError as e:
                raise SystemExit(f"bad expression {d!r}: {e.msg}")
        return [(1, self._c(n, depth, src))]

    def compile_parts(self, expr):
        """infix → [tokens, ..] whose values SUM to the expression's: one program when it fits the server's limits, else
        its top-level summands packed into programs that do (for values read by eval / --watch / scan)"""
        toks = self.compile(expr)
        if len(toks) <= MAX_PROG_TOKENS and stack_depth(toks) <= MAX_STACK:
            return [toks]
        terms = self._terms(ast.parse(expr.strip(), mode="eval").body, 0, expr)
        if len(terms) == 1:
            prog_check(toks, repr(expr))                     # not a sum: nothing to split
        parts, cur = [], None
        for s, t in terms:
            if len(t) + 1 > MAX_PROG_TOKENS or stack_depth(t) + 1 > MAX_STACK:
                prog_check(["0"] + t + ["+"], f"a summand of {expr!r}")
            if cur is not None and len(cur) + len(t) + 1 <= MAX_PROG_TOKENS:
                cur += t + (["+"] if s > 0 else ["-"])
            else:
                if cur is not None:
                    parts.append(cur)
                cur = t + ([] if s > 0 else ["neg"])
        parts.append(cur)
        return parts

    def program(self, expr, what=None):
        """infix → ONE program (a stop condition, a goal, a mask): SystemExit when it exceeds the server's limits"""
        return prog_check(self.compile(expr), what or repr(expr))

    def compile_tuple(self, expr):
        """"(scene, px//1, pz//1)" → [tokens, tokens, ..]"""
        tree = ast.parse(expr.strip(), mode="eval").body
        elts = tree.elts if isinstance(tree, ast.Tuple) else [tree]
        return [self._c(e, 0, expr) for e in elts]


# ------------------------------------------------------------------ the movie
class Movie:
    """every input since boot: ops (frame, kind, value), kind m(ask) / e(sc) / r(ngseed); + the start save"""

    def __init__(self, save=None, ops=None, frame=0):
        self.save, self.ops, self.frame = save, ops or [], frame

    @staticmethod
    def from_server(text, save):
        kv = dict(t.split("=", 1) for t in text.split()[1:] if "=" in t)
        ops = []
        for o in filter(None, kv.get("ops", "").split(",")):
            k, rest = o[0], o[1:]
            if k == "e":
                ops.append((int(rest), "e", 0))
            else:
                f, v = rest.split(":")
                ops.append((int(f), k, int(v, 16)))
        return Movie(save, ops, int(kv["frame"]))

    @staticmethod
    def load(path):
        mv = Movie()
        for line in open(path):
            line = line.strip()
            if not line.startswith("{"):
                continue
            d = json.loads(line)
            if "savefile" in d:
                mv.save = None if d["savefile"] == "@fresh" else os.path.normpath(os.path.join(os.path.dirname(path), d["savefile"]))
            elif "rngseed" in d:
                mv.ops.append((int(d["rngseed"][0]), "r", int(d["rngseed"][1])))
            elif "esc" in d:
                mv.ops.append((int(d["esc"]), "e", 0))
            elif "frame" in d:
                mv.ops.append((int(d["frame"]), "m", int(str(d["buttons"]), 16) if isinstance(d["buttons"], str) else int(d["buttons"])))
            elif "end" in d:
                mv.frame = int(d["end"])
        if not mv.frame and mv.ops:
            mv.frame = max(o[0] for o in mv.ops)
        return mv

    def write(self, path, comment="", rel_to=None):
        """a flat rhost trace: replays to exactly self.frame (`--frames`), or to its last op + tail"""
        with open(path + ".tmp", "w") as f:
            f.write(f"# rhost flat trace (rd movie{': ' + comment if comment else ''}): frame-absolute from boot; replays deterministically in rhost\n")
            if self.save:
                ref = os.path.relpath(self.save, rel_to or os.path.dirname(path))
                f.write(json.dumps({"savefile": ref}) + "\n")
            else:
                f.write('{"savefile": "@fresh"}\n')
            last = 0
            for fr, k, v in self.ops:
                if fr > self.frame:
                    break
                if k == "m":
                    f.write(f'{{"frame": {fr}, "buttons": "0x{v:04x}"}}\n'); last = v
                elif k == "e":
                    f.write(f'{{"esc": {fr}}}\n')
                else:
                    f.write(f'{{"rngseed": [{fr}, {v}]}}\n')
            f.write(f'{{"frame": {self.frame}, "buttons": "0x{last:04x}"}}\n')
        os.replace(path + ".tmp", path)

    def truncated(self, frame):
        return Movie(self.save, [o for o in self.ops if o[0] <= frame], min(frame, self.frame))

    def mask_at(self, frame):
        m = 0
        for fr, k, v in self.ops:
            if fr > frame:
                break
            if k == "m":
                m = v
        return m

    def run_args(self, lo, hi):
        """the inputs of ticks lo+1..hi as a live `run` command's arguments"""
        segs, escs, rngs = [], [], []
        cur, at = self.mask_at(lo), lo + 1
        for fr, k, v in self.ops:
            if fr <= lo or fr > hi:
                continue
            if k == "m":
                if fr > at:
                    segs.append((cur, fr - at)); at = fr
                cur = v
            elif k == "e":
                escs.append(fr - lo)
            else:
                rngs.append((fr - lo, v))
        if hi + 1 > at:
            segs.append((cur, hi + 1 - at))
        s = ",".join(f"{m:x}:{n}" for m, n in segs)
        if escs:
            s += " esc=" + ",".join(map(str, escs))
        if rngs:
            s += " rng=" + ",".join(f"{t}:{v:x}" for t, v in rngs)
        return s

    def chunks(self, lo, hi, max_ticks=4000):
        """ticks lo+1..hi as [(a, b)] windows whose `run` fits the server: ≤ MAX_RUN_OPS ESC and rngseed ops each (it drops
        the rest silently), a line < MAX_LINE"""
        out, a = [], lo
        while a < hi:
            b = min(hi, a + max_ticks)
            while b > a + 1:
                esc = sum(1 for f, k, _ in self.ops if a < f <= b and k == "e")
                rng = sum(1 for f, k, _ in self.ops if a < f <= b and k == "r")
                if esc <= MAX_RUN_OPS and rng <= MAX_RUN_OPS and len(self.run_args(a, b)) < MAX_LINE:
                    break
                b = a + (b - a) // 2
            out.append((a, b))
            a = b
        return out


# ------------------------------------------------------------------ a live rhost process
class SessionEnded(Exception):
    """the rhost process went away mid-command: the game exited (EXIT on the title, a quit dialog) or crashed"""


class Live:
    def __init__(self, port, timeout=3600):
        last = None
        for _ in range(150):
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError as e:
                last = e
                time.sleep(0.1)
        else:
            raise OSError(f"cannot connect to the live session on port {port}: {last}")
        self.s.settimeout(timeout)
        self.s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.f = self.s.makefile("rwb")
        self.nprog = 0

    def cmd(self, line, check=True):
        self.f.write(line.encode() + b"\n")
        self.f.flush()
        r = self.f.readline().decode(errors="replace").rstrip("\n")
        if not r:
            raise SessionEnded()
        if check and not r.startswith("ok"):
            raise SystemExit(f"live: {r}\n  (command: {line[:200]})")
        return r

    @staticmethod
    def kv(r):
        return dict(t.split("=", 1) for t in r.split()[1:] if "=" in t)

    def prog(self, tokens):
        prog_check(tokens, "program " + " ".join(tokens[:8]) + (" …" if len(tokens) > 8 else ""))
        if self.nprog >= 250:
            self.nprog = 0
        pid = self.nprog
        self.nprog += 1
        self.cmd(f"prog {pid} " + " ".join(tokens))
        return pid

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass


def num(v):
    return int(v) if v == int(v) and abs(v) < 1e15 else v


def eval_exprs(lv, V, exprs):
    """the expressions' values now; batched (EVAL_CHUNK programs per `eval`), an over-long sum split (compile_parts)"""
    flat = [(i, p) for i, e in enumerate(exprs) for p in V.compile_parts(e)]
    vals = [0.0] * len(exprs)
    for c in range(0, len(flat), EVAL_CHUNK):
        batch = flat[c:c + EVAL_CHUNK]
        ids = [lv.prog(p) for _, p in batch]
        for (i, _), x in zip(batch, Live.kv(lv.cmd("eval " + ",".join(map(str, ids))))["v"].split(":")):
            vals[i] += float(x)
    return [num(v) for v in vals]


def sampled_cols(lv, V, exprs, room=MAX_SAMPLED):
    """programs for sampled columns (`run s=`) → (ids, slices): a column's value = the sum of its slice of the row"""
    ids, slices = [], []
    for e in exprs:
        parts = V.compile_parts(e)
        slices.append((len(ids), len(ids) + len(parts)))
        ids += [None] * len(parts)
        if len(ids) > room:
            raise SystemExit(f"at most {room} sampled programs per run (the live server's limit; a long actors()/sum column "
                             f"takes several): {len(ids)}+ for {len(exprs)} columns — split the watch list over two calls")
        for k, p in enumerate(parts):
            ids[slices[-1][0] + k] = lv.prog(p)
    return ids, slices


def fold_row(vals, slices):
    return [num(sum(vals[a:b])) for a, b in slices]


def parse_samples(text):
    """`run … e=1` samples "f:v:v;f:v:v" → [(frame, [float])]"""
    rows = []
    for row in filter(None, (text or "").split(";")):
        p = row.split(":")
        rows.append((int(p[0]), [float(x) for x in p[1:]]))
    return rows


def wine_env():
    """live sessions run without an X display, in the headless prefix (rhost.py wine_prefix: why it is separate)"""
    return {k: v for k, v in dict(os.environ, WINEPREFIX=RH.wine_prefix(True), WINEDEBUG="-all").items()
            if k not in ("DISPLAY", "WAYLAND_DISPLAY")}


def staged_exe(host="wine"):
    """build if stale, stage the build content-addressed (wine root, or bin\\<sha> on the Windows box / bunker) → its Windows path"""
    if host == "bunker":
        return f"{RH.WROOT}\\bin\\{BK.stage()}\\rhost.exe"
    if host == "win":
        RH.ensure_staged()
        return RH.bin_dir() + "\\rhost.exe"
    if RH.rhost_stale():
        RH.cmd_build(None)
    RH.wine_stage()
    return RH.bin_dir() + "\\rhost.exe"


# ---- where live sessions run. RD_HOST=wine: under wine on this box (headless prefix, admit_local). RD_HOST=win: natively
# on the Windows box (ssh $RHOST_WIN, session 0 — fine for the fake devices): started detached through WMI (sshd kills its
# job's processes on disconnect; a Win32_Process.Create child is not in that job), admitted by the same machine-wide gate
# as the queue runner's batches (rhost.py PS_ADMIT: process cap + RAM floor, one Global mutex) plus RD_WIN_MAX_SESSIONS
# live rd sessions; reached through an ssh local forward whose remote command waits on the rhost PID (the forward ends with
# the process). RD_HOST=bunker: under wine on the temporary NixOS box (tools/bunker: one bounded pool, sessions reached
# through forwards on ONE multiplexed ssh connection; admission = pool.py spawn: <= RD_BUNKER_MAX_SESSIONS rd sessions and
# RD_BUNKER_MEM_FLOOR_MB free inside the 6 G bound). RD_HOST=auto: win → bunker → wine, each tried when the previous does
# not answer within 3 s or does not admit within its wait (RD_WIN_ADMIT_WAIT 60 s, RD_BUNKER_ADMIT_WAIT 30 s). A session
# keeps the host it started on (session.json "host"); a rebuilt session takes the current choice.
RD_HOST = os.environ.get("RD_HOST", "auto")
HOSTS = ("win", "bunker", "wine")                      # the auto order
WIN_MAX_SESSIONS = int(os.environ.get("RD_WIN_MAX_SESSIONS", "8"))
BUNKER_MAX_SESSIONS = int(os.environ.get("RD_BUNKER_MAX_SESSIONS", "6"))
BUNKER_FLOOR_MB = int(os.environ.get("RD_BUNKER_MEM_FLOOR_MB", "1024"))
ADMIT_WAIT = {"win": float(os.environ.get("RD_WIN_ADMIT_WAIT", "60")), "bunker": float(os.environ.get("RD_BUNKER_ADMIT_WAIT", "30"))}
WIN_SESS = "C:\\orc-rhost\\runs\\drive"
_up, _up_lock = {"wine": True}, threading.Lock()


def host_up(h):
    """does host H answer (once per process)"""
    with _up_lock:
        if h not in _up:
            _up[h] = win_up(timeout=3) if h == "win" else BK.up(timeout=3)
        return _up[h]


def pick_host():
    """the first host of the order that answers (RD_HOST=win|bunker|wine: that one)"""
    if RD_HOST not in ("auto",) + HOSTS:
        raise SystemExit(f"RD_HOST={RD_HOST!r}: auto | win | bunker | wine")
    return RD_HOST if RD_HOST != "auto" else next(h for h in HOSTS if host_up(h))


def local_listening(port):
    """is something LISTENing on 127.0.0.1:PORT here (the ssh forward is up)?"""
    for f in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            for l in open(f).readlines()[1:]:
                p = l.split()
                if p[3] == "0A" and int(p[1].rsplit(":", 1)[1], 16) == port:
                    return True
        except OSError:
            pass
    return False


def free_local_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def win_ps(ps, timeout=600, host=None):
    """a PowerShell command on the Windows box → (rc, stdout); transient ssh failures retried (RH.ssh_win's rule)"""
    for attempt in range(4):
        r = subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", host or RH.WIN, ps], text=True, capture_output=True, timeout=timeout)
        if r.returncode == 255 and RH.SSH_TRANSIENT.search(r.stderr) and attempt < 3:
            time.sleep(2 + 3 * attempt)
            continue
        return r.returncode, r.stdout
    return r.returncode, r.stdout


class NotAdmitted(SystemExit):
    """the Windows box's admission gave no slot in time"""


def win_spawn(name, ld, rhost_args, wait, tag):
    """ld (a local dir with the session's files) → C:\\orc-rhost\\runs\\drive\\NAME; start rhost there detached, admitted;
    → (windows pid, its live port)"""
    exe = staged_exe("win")
    rd = f"{WIN_SESS}\\{name}"
    dst = f"{RH.WIN}:{WIN_SESS.replace(chr(92), '/')}/"
    if subprocess.run(["scp", "-q", "-r", ld, dst], capture_output=True).returncode != 0:
        win_ps(f"New-Item -ItemType Directory -Force '{WIN_SESS}' | Out-Null")
        RH.scp(ld, dst, recursive=True)
    shutil.rmtree(ld, ignore_errors=True)
    cl = " ".join([exe] + [f'"{x}"' if " " in x else x for x in rhost_args])
    ps = ("$ErrorActionPreference = 'Stop'\n"
          f"$rd = '{rd}'; $cl = '{cl.replace(chr(39), chr(39) * 2)}'\n"
          "$m = New-Object System.Threading.Mutex($false, 'Global\\orc-rhost-admit')\n"
          f"$deadline = (Get-Date).AddSeconds({wait}); $wpid = 0\n"
          "while ($true) {\n"
          "  $cap = 12; try { $cap = [int](Get-Content 'C:\\orc-rhost\\queue\\procs.txt' -TotalCount 1 -ErrorAction Stop) } catch {}\n"
          "  try { [void]$m.WaitOne() } catch [System.Threading.AbandonedMutexException] {}\n"
          "  $ours = @(Get-CimInstance Win32_Process -Filter \"Name='rhost.exe' or Name='openrecet.exe'\" | Where-Object { $_.ExecutablePath -like 'C:\\orc-rhost\\*' })\n"
          "  $n = $ours.Count; $mine = @($ours | Where-Object { $_.CommandLine -like '*\\runs\\drive\\*' }).Count\n"
          "  $free = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory\n"
          f"  if ($mine -lt {WIN_MAX_SESSIONS} -and ($n -eq 0 -or ($n -lt $cap -and $free -gt 1500000))) {{\n"
          "    $r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = $cl; CurrentDirectory = $rd }\n"
          "    $wpid = $r.ProcessId\n"
          "    try { if ((Get-Content 'C:\\orc-rhost\\queue\\priority.txt' -TotalCount 1 -ErrorAction Stop).Trim() -eq 'low') { (Get-Process -Id $wpid).PriorityClass = 'BelowNormal' } } catch {}\n"
          "    Start-Sleep -Milliseconds 300; $m.ReleaseMutex(); break }\n"
          "  $m.ReleaseMutex()\n"
          f"  if ((Get-Date) -gt $deadline) {{ \"NOADMIT $n rhost/port processes (cap $cap), $mine rd sessions (cap {WIN_MAX_SESSIONS}), $([int]($free / 1024)) MB free (floor 1465)\"; Remove-Item $rd -Recurse -Force -ErrorAction SilentlyContinue; exit 0 }}\n"
          "  Start-Sleep -Milliseconds 500 }\n"
          "$t0 = Get-Date\n"
          "while (!(Test-Path \"$rd\\live.port\")) {\n"
          "  if (!(Get-Process -Id $wpid -ErrorAction SilentlyContinue) -or ((Get-Date) - $t0).TotalSeconds -gt 120) {\n"
          "    \"NOSTART pid $wpid\"; if (Test-Path \"$rd\\rhost.log\") { Get-Content \"$rd\\rhost.log\" -Tail 8 }; exit 0 }\n"
          "  Start-Sleep -Milliseconds 50 }\n"
          "\"LIVE $wpid \" + (Get-Content \"$rd\\live.port\" -TotalCount 1)\n")
    rc, out = win_ps(ps, timeout=wait + 300)
    m = re.search(r"^LIVE (\d+) (\d+) ", out, re.M)
    if not m:
        if "NOADMIT" in out:
            raise NotAdmitted(f"live session {tag} on {RH.WIN}: not admitted — " + out.split("NOADMIT", 1)[1].strip() +
                             ". Stop idle sessions (`./rd ls`, `./rd stop R`) or wait.")
        raise SystemExit(f"the live session did not start on {RH.WIN} ({rd}, rc {rc})\n{out[-800:]}")
    return int(m.group(1)), int(m.group(2))


def win_forward(wpid, wport):
    """an ssh local forward to the session's port, alive exactly as long as the Windows process → (ssh pid, local port)"""
    for attempt in range(4):
        lp = free_local_port()
        f = subprocess.Popen(["ssh", "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes", "-o", "ServerAliveInterval=30",
                              "-o", "ServerAliveCountMax=4", "-L", f"127.0.0.1:{lp}:127.0.0.1:{wport}", RH.WIN,
                              f"Wait-Process -Id {wpid} -ErrorAction SilentlyContinue"],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        t0 = time.time()
        while f.poll() is None and time.time() - t0 < 30:
            if local_listening(lp):
                return f.pid, lp
            time.sleep(0.05)
        try:
            os.killpg(f.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        f.wait()
        time.sleep(1 + 2 * attempt)
    raise SystemExit(f"no ssh forward to the live session (pid {wpid}, port {wport}) on {RH.WIN}")


def spawn_live(tag, movie, upto=None, idle=1800, wait=300, host=None, src_trace=None, limit=None):
    """a live rhost (headless) on HOST (pick_host()): replays MOVIE to frame UPTO first — or SRC_TRACE (any dialect) up to
    LIMIT — → the session dict {host, pid, port, rundir[, fwd, wport]}: `port` is always a local port (wine: the session's
    own; win: the ssh forward's). Admitted by the host's machine-wide gate: WAIT seconds for a slot, else SystemExit"""
    if host is None and RD_HOST == "auto":
        # auto: the Windows box when it has a slot within a minute (gates fill it for an hour at a time), else the bunker
        # when it admits within its wait, else wine here
        for h in ("win", "bunker"):
            if not host_up(h):
                continue
            try:
                return spawn_live(tag, movie, upto, idle, min(wait, ADMIT_WAIT[h]), h, src_trace, limit)
            except NotAdmitted as e:
                print(f"[{e} — trying the next host]")
                wait = max(0, wait - ADMIT_WAIT[h])
        host = "wine"
    host = host or pick_host()
    name = f"{tag}-{os.getpid()}-{int(time.time() * 1000) % 10 ** 9}"
    if host in ("win", "bunker"):
        ld = os.path.join(RUNS, ".winstage", name)
        os.makedirs(ld)
        win = f"{WIN_SESS}\\{name}"
    else:
        exe = staged_exe("wine")
        os.makedirs(SESS, exist_ok=True)
        ld = os.path.join(SESS, name)
        os.makedirs(ld)
        win = "C:\\orc-rhost\\runs\\drive\\" + name
    shutil.copy(os.path.join(RH.HERE, "recet.ini"), ld)
    if os.path.exists(RH.VOLATILE):
        shutil.copy(RH.VOLATILE, ld)
    if movie.save:
        open(os.path.join(ld, "save.dat"), "wb").write(RH.save_bytes(movie.save))
    args = ["--exe", f"{RH.WROOT}\\game\\recettear.unpacked.exe", "--game", f"{RH.WROOT}\\game", "--run", win,
            "--live", "0", "--live-idle", str(idle)] + skip_args()
    if src_trace:
        shutil.copy(src_trace, os.path.join(ld, "trace.jsonl"))
        args += ["--trace", win + "\\trace.jsonl"] + (["--frames", str(limit)] if limit else [])
    else:
        upto = movie.frame if upto is None else upto
        seed = next((v for f, k, v in movie.ops if k == "r" and f == 0), None)
        if seed is not None:
            args += ["--seed", str(seed)]
        if upto > 0:
            movie.truncated(upto).write(os.path.join(ld, "trace.jsonl"))
            args += ["--trace", win + "\\trace.jsonl", "--frames", str(upto)]
    if host == "win":
        try:
            wpid, wport = win_spawn(name, ld, args, wait, tag)
        finally:
            shutil.rmtree(ld, ignore_errors=True)
        s = {"host": "win", "ssh": RH.WIN, "pid": wpid, "wport": wport, "rundir": win}
        try:
            s["fwd"], s["port"] = win_forward(wpid, wport)
        except BaseException:
            kill_session(s)
            raise
        return s
    if host == "bunker":
        try:
            files = {f: open(os.path.join(ld, f), "rb").read() for f in os.listdir(ld)}
        finally:
            shutil.rmtree(ld, ignore_errors=True)
        exe = staged_exe("bunker")
        r = BK.spawn_session(name, files, exe, args, wait, BUNKER_MAX_SESSIONS, BUNKER_FLOOR_MB)
        if r[0] == "NOADMIT":
            raise NotAdmitted(f"live session {tag} on bunker: not admitted — {r[1]}. Stop idle sessions (`./rd ls`, `./rd stop R`) or wait.")
        if r[0] != "LIVE":
            raise SystemExit(f"the live session did not start on bunker ({win})\n{r[1]}")
        s = {"host": "bunker", "ssh": BK.HOST, "pid": r[1], "wport": r[2], "rundir": win, "name": name}
        try:
            for attempt in range(4):                   # a local forward on the one multiplexed connection
                lp = free_local_port()
                if BK.forward(lp, r[2]):
                    s["port"] = lp
                    break
                time.sleep(0.5 + attempt)
            else:
                raise SystemExit(f"no forward to the live session on bunker (pid {r[1]}, port {r[2]})")
        except BaseException:
            kill_session(s)
            raise
        return s
    try:
        with RH.admit_local(wait=wait, what=f"live session {tag}"):
            p = subprocess.Popen(["wine", exe] + args, stdout=open(os.path.join(ld, "stdout.txt"), "w"), stderr=subprocess.DEVNULL,
                                 env=wine_env(), start_new_session=True, cwd=ld)
    except SystemExit:
        shutil.rmtree(ld, ignore_errors=True)
        raise
    pf = os.path.join(ld, "live.port")
    t0 = time.time()
    while not os.path.exists(pf):
        if p.poll() is not None or time.time() - t0 > 120:
            tail = open(os.path.join(ld, "rhost.log"), errors="replace").read()[-600:] if os.path.exists(os.path.join(ld, "rhost.log")) else ""
            raise SystemExit(f"the live session did not start ({ld})\n{tail}")
        time.sleep(0.05)
    return {"host": "wine", "pid": p.pid, "port": int(open(pf).read().split()[0]), "rundir": ld}


def kill_session(s):
    """end OUR session (never by name): wine — its process group; win — the ssh forward, then the Windows process by PID
    after checking its command line is this session's; drop its run dir"""
    if not s:
        return
    if s.get("host") == "bunker":
        if s.get("port") and s.get("wport"):
            BK.forward(s["port"], s["wport"], cancel=True)
        if s.get("pid") and s.get("name") and s.get("rundir") == f"{WIN_SESS}\\{s['name']}":
            BK.kill_session(s["pid"], s["name"])       # pool.py: by PID after checking its argv is this session's
        return
    if s.get("host", "wine") == "win":
        if s.get("fwd"):
            try:
                os.killpg(s["fwd"], signal.SIGKILL)
            except (ProcessLookupError, PermissionError):
                pass
        rd = s.get("rundir", "")
        if not rd.startswith(WIN_SESS + "\\") or not s.get("pid"):
            return
        win_ps(host=s.get("ssh"), ps=f"$p = Get-CimInstance Win32_Process -Filter 'ProcessId={int(s['pid'])}'; "
               f"if ($p -and $p.Name -eq 'rhost.exe' -and $p.CommandLine -like '*{rd}*') {{ Stop-Process -Id {int(s['pid'])} -Force; "
               f"Wait-Process -Id {int(s['pid'])} -Timeout 10 -ErrorAction SilentlyContinue }}; "
               f"Remove-Item '{rd}' -Recurse -Force -ErrorAction SilentlyContinue; exit 0", timeout=120)   # the box it started on
        return
    try:
        os.killpg(s["pid"], signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    rundir = s.get("rundir")
    if rundir and os.path.realpath(rundir).startswith(os.path.realpath(SESS) + os.sep):
        shutil.rmtree(rundir, ignore_errors=True)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


def sess_alive(s):
    """win: the forward lives exactly as long as the Windows process (its remote command waits on it); bunker: the
    forward on the master is listening and the session's process is alive there (one round trip on the master)"""
    if s and s.get("host") == "bunker":
        return bool(s.get("port")) and local_listening(s["port"]) and BK.session_alive(s["pid"], s["name"])
    return bool(s) and pid_alive(s["fwd"] if s.get("host", "wine") == "win" else s["pid"])


def sess_file(s, f, dst):
    """copy file F of the session's run dir to DST → True when it exists"""
    if s.get("host") == "bunker":
        data = BK.session_file(s.get("name", ""), f)
        if data is None:
            return False
        open(dst, "wb").write(data)
        return True
    if s.get("host", "wine") == "win":
        return subprocess.run(["scp", "-q", f"{s.get('ssh', RH.WIN)}:{s['rundir'].replace(chr(92), '/')}/{f}", dst], capture_output=True).returncode == 0 \
            and os.path.exists(dst)
    src = os.path.join(s["rundir"], f)
    if not os.path.exists(src):
        return False
    shutil.copy(src, dst)
    return True


# ------------------------------------------------------------------ a run
class Run:
    def __init__(self, name, create=False, lock=True):
        """lock=False: a read-only view (movie, checkpoints, names) that never writes the run and does not wait for it"""
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise SystemExit(f"bad run name {name!r}")
        self.name, self.dir = name, os.path.join(RUNS, name)
        if create:
            os.makedirs(self.dir, exist_ok=True)
        elif not os.path.exists(os.path.join(self.dir, "meta.json")):
            raise SystemExit(f"no run {name!r} (rd ls; rd new {name} --from ..)")
        self.lock = None
        if lock:
            self.lock = open(os.path.join(self.dir, ".lock"), "w")
            try:
                fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                self.lock.close()
                raise SystemExit(f"run {name} is busy: another rd command is working on it — wait for it")
        self.meta_p, self.sess_p, self.movie_p = (os.path.join(self.dir, f) for f in ("meta.json", "session.json", "movie.jsonl"))
        self.meta = json.load(open(self.meta_p)) if os.path.exists(self.meta_p) else {"checkpoints": {}, "poked": False}
        self.movie = Movie.load(self.movie_p) if os.path.exists(self.movie_p) else Movie()
        self.vars = Vars(self.dir)
        self.live, self.sess = None, None

    # ---- persistence
    def save_meta(self):
        self.meta["frame"] = self.movie.frame
        json.dump(self.meta, open(self.meta_p + ".tmp", "w"), indent=1)
        os.replace(self.meta_p + ".tmp", self.meta_p)

    def save_sess(self):
        json.dump(self.sess, open(self.sess_p + ".tmp", "w"))
        os.replace(self.sess_p + ".tmp", self.sess_p)

    def sync_movie(self):
        """the server's movie is the truth after any step / restore"""
        r = self.live.cmd("movie")
        self.movie = Movie.from_server(r, self.movie.save)
        self.movie.write(self.movie_p, comment=self.name, rel_to=self.dir)
        if "poked=1" in r:
            self.meta["poked"] = True
        self.save_meta()

    # ---- the session
    def attach(self, cached_only=False):
        """a live session at the movie's head: the cached one if it is still there and in step, else a fresh replay
        (cached_only: None instead of the replay)"""
        if self.live:
            return self.live
        s = json.load(open(self.sess_p)) if os.path.exists(self.sess_p) else None
        if s and sess_alive(s) and s.get("skip", "") == SKIP_FN and \
                s.get("bin") == RH.file_sha12(os.path.join(RH.BUILD, "rhost.exe"), os.path.join(RH.BUILD, "v3proxy_d3d8.dll")):
            try:
                lv = Live(s["port"])
                if int(Live.kv(lv.cmd("ping"))["frame"]) == self.movie.frame:
                    self.live, self.sess = lv, s
                    return lv
                lv.close()
            except (OSError, SessionEnded):
                pass
        if cached_only:
            return None
        if s:
            kill_session(s)
            if s.get("snaps") or s.get("mark") is not None:
                print("[the session was rebuilt (it ended, or rhost was rebuilt): its savestates are gone — checkpoints load by replay, "
                      "`ram mark` has to be taken again]")
        t0 = time.time()
        h = spawn_live(self.name, self.movie)
        try:
            self.live = Live(h["port"])
            k = Live.kv(self.live.cmd("ping"))
        except BaseException:
            kill_session(h)
            raise
        if int(k["frame"]) != self.movie.frame:
            raise SystemExit(f"the replayed session is at frame {k['frame']}, the movie at {self.movie.frame} (see {h['rundir']}/rhost.log on {h['host']})")
        self.sess = dict(h, epoch=int(k["epoch"]), snaps={}, mark=None, skip=SKIP_FN, mask=SKIP_MASK,
                         bin=RH.file_sha12(os.path.join(RH.BUILD, "rhost.exe"), os.path.join(RH.BUILD, "v3proxy_d3d8.dll")))
        self.save_sess()
        if self.movie.frame > 2000:
            print(f"[session rebuilt by replaying {self.movie.frame} frames in {time.time() - t0:.1f}s]")
        return self.live

    def stop(self):
        s = json.load(open(self.sess_p)) if os.path.exists(self.sess_p) else None
        if s:
            kill_session(s)
            for w in s.get("workers", []):
                kill_session(w)
            os.remove(self.sess_p)

    def epoch_check(self, r):
        """an anchor on the server (explicit, or a search's) invalidated every savestate id we hold"""
        e = Live.kv(r).get("epoch")
        if e and int(e) != self.sess["epoch"]:
            if self.sess.get("mark") is not None:
                print("[the search re-anchored the savestates: the `ram mark` is gone — mark again before the action you want to diff]")
            self.sess.update(epoch=int(e), snaps={}, mark=None)    # undo entries name their (pid, epoch): stale ones replay

    def snap(self):
        r = self.live.cmd("snap")
        self.epoch_check(r)
        return int(Live.kv(r)["id"])

    def sid_ok(self, u):
        """is the undo entry's savestate one of THIS session's (same process, no re-anchor since)?"""
        return bool(self.sess and u.get("sid") is not None and u.get("pid") == self.sess["pid"] and u.get("epoch") == self.sess["epoch"])

    def push_undo(self, tail=False):
        """before every state-changing command: a savestate (instant undo while the session lives) + what a replay
        needs when it does not — the frame, the checkpoints, and for a step back in time (ck load, rewind: tail=True)
        a copy of the movie it cuts. The stack lives in meta.json: it survives session rebuilds."""
        u = {"sid": self.snap() if self.live else None, "pid": self.sess and self.sess["pid"], "epoch": self.sess and self.sess["epoch"],
             "frame": self.movie.frame, "cks": dict(self.meta.get("checkpoints", {})), "tail": None}
        if tail:
            os.makedirs(os.path.join(self.dir, "undo"), exist_ok=True)
            u["tail"] = f"undo/{int(time.time() * 1000)}-f{self.movie.frame}.jsonl"
            self.movie.write(os.path.join(self.dir, u["tail"]), comment=self.name + " (undo copy)")
        stack = self.meta.setdefault("undo", [])
        stack.append(u)
        while len(stack) > 8:
            old = stack.pop(0)
            if self.sid_ok(old) and old["sid"] not in ({v[0] for v in self.sess["snaps"].values()} | {self.sess.get("mark")}):
                self.live.cmd(f"free {old['sid']}", check=False)
            if old.get("tail") and os.path.exists(os.path.join(self.dir, old["tail"])):
                os.remove(os.path.join(self.dir, old["tail"]))
        self.save_meta()

    def drop_undo(self):
        """take back the entry a command pushed when it ended up changing nothing (a search that applied no path): `rd undo`
        must step back over the last change, not over a no-op"""
        stack = self.meta.get("undo", [])
        if not stack:
            return
        u = stack.pop()
        if self.live and self.sid_ok(u) and u["sid"] not in ({v[0] for v in self.sess["snaps"].values()} | {self.sess.get("mark")}):
            self.live.cmd(f"free {u['sid']}", check=False)
        if u.get("tail") and os.path.exists(os.path.join(self.dir, u["tail"])):
            os.remove(os.path.join(self.dir, u["tail"]))
        self.save_meta()

    # ---- reading state
    def ev(self, *exprs):
        return eval_exprs(self.attach(), self.vars, exprs)

    def status_line(self):
        lv = self.attach()
        k = Live.kv(lv.cmd("ping"))
        names = [n for n, d in self.vars.defs.items() if d.get("status")]
        vals = self.ev(*names) if names else []
        s = f"frame {k['frame']}  rng {k['rng']}  " + "  ".join(f"{n}={fmt(v)}" for n, v in zip(names, vals))
        if self.meta.get("poked"):
            s += "  [POKED: memory was written by hand — this run's movie does not reproduce it]"
        return s

    # ---- acting
    def play(self, spec, until=None, avoid=None, watch=(), every=0, quiet=False, rows=40, undo=True):
        lv = self.attach()
        segs, escs, total = spec_to_run(parse_spec(spec), lambda e: lv.prog(self.vars.compile(e)))
        if not total:
            raise SystemExit("empty input spec")
        if undo:
            self.push_undo()
        cmd = f"run {segs}"
        if escs:
            cmd += " esc=" + ",".join(map(str, escs))
        if until:
            cmd += f" u={lv.prog(self.vars.compile(until))}"
        if avoid:
            cmd += f" a={lv.prog(self.vars.compile(avoid))}"
        ids, wsl = sampled_cols(lv, self.vars, watch)
        ev_names = [n for n, d in self.vars.defs.items() if d.get("event")]
        if len(ids) + len(ev_names) > MAX_SAMPLED:      # the watch columns first; the event names get what is left
            if not quiet:
                print(f"[--watch takes {len(ids)} of the {MAX_SAMPLED} sampled programs: events not tracked this call: "
                      f"{' '.join(ev_names[MAX_SAMPLED - len(ids):])}]")
            ev_names = ev_names[:MAX_SAMPLED - len(ids)]
        ids += [lv.prog(self.vars.program(e)) for e in ev_names]
        nw = len(ids) - len(ev_names)
        cols = list(watch) + ev_names
        if cols:
            cmd += " s=" + ",".join(map(str, ids))
            cmd += f" e={every if (every and watch) else 1}"
        r = lv.cmd(cmd)
        k = Live.kv(r)
        self.sync_movie()
        self.save_sess()
        out = {"ran": int(k["ran"]), "stop": k["stop"], "frame": int(k["frame"]), "rows": [], "events": []}
        if cols and k.get("samples"):
            prev = None
            for f, vals in parse_samples(k["samples"]):
                if watch and (not every or (f - (out["frame"] - out["ran"])) % every == 0):
                    out["rows"].append((f, fold_row(vals[:nw], wsl)))
                evs = vals[nw:]
                if prev is not None:
                    for n, a, b in zip(ev_names, prev, evs):
                        if a != b:
                            out["events"].append((f, n, a, b))
                prev = evs
        if not quiet:
            print(f"ran {out['ran']} ticks ({out['stop']})  " + self.status_line())
            by = {}
            for f, n, a, b in out["events"]:
                by.setdefault(n, []).append((f, a, b))
            busy = {n for n, v in by.items() if len(v) > 8}        # a counter / a position: one line, not one per tick
            for n in sorted(busy):
                v = by[n]
                vals = [x[1] for x in v] + [v[-1][2]]
                print(f"  {n}: {len(v)} changes f{v[0][0]}..f{v[-1][0]}, {fmt(v[0][1])} → {fmt(v[-1][2])} (range {fmt(min(vals))}..{fmt(max(vals))}); "
                      f"first f{v[0][0]} {fmt(v[0][1])}→{fmt(v[0][2])}, last f{v[-1][0]} {fmt(v[-1][1])}→{fmt(v[-1][2])}")
            sparse = [e for e in out["events"] if e[1] not in busy]
            for f, n, a, b in sparse[:60]:
                print(f"  f{f}: {n} {fmt(a)} → {fmt(b)}")
            if len(sparse) > 60:
                print(f"  … {len(sparse) - 60} more events")
            if watch:
                print("  frame  " + "  ".join(watch))
                if len(out["rows"]) > rows:
                    print(f"  … {len(out['rows']) - rows} earlier rows not shown (--rows N)")
                for f, vals in out["rows"][-rows:]:
                    print(f"  {f:6d} " + "  ".join(fmt(v) for v in vals))
        return out

    def load_frame(self, frame, sid=None, movie=None):
        """go to FRAME of this movie (or of MOVIE: an undo's copy): by savestate when one is held, else by a fresh
        replay of the prefix"""
        if sid is not None and self.live:
            r = self.live.cmd(f"restore {sid}", check=False)
            if r.startswith("ok") and int(Live.kv(r)["frame"]) == frame:
                self.sync_movie()
                return "savestate"
        if self.live:
            self.live.close()
        self.stop()
        self.live = None
        self.movie = (movie or self.movie).truncated(frame)
        self.movie.write(self.movie_p, comment=self.name, rel_to=self.dir)
        self.save_meta()
        self.attach()
        return "replay"

    def close(self):
        if self.live:
            self.live.close()
            self.live = None
        if self.lock:
            self.lock.close()
            self.lock = None


def fmt(v):
    if isinstance(v, float) and v != int(v):
        return f"{v:.4f}".rstrip("0").rstrip(".")
    return str(int(v))


# ------------------------------------------------------------------ earlier frames: rd scan / rd eval --at-frame
def replay_window(lv, movie, at, lo, hi, V, exprs):
    """LV stands at frame AT (≤ LO) of MOVIE: feed it MOVIE's own inputs (masks, ESC posts, rngseed ops) to LO, then
    sample EXPRS after every tick LO..HI → [(frame, [values])]. LO == HI: one row, any number of expressions."""
    k = Live.kv(lv.cmd("ping"))
    if int(k["frame"]) != at:
        raise SystemExit(f"scan: the session is at frame {k['frame']}, expected {at}")
    for a, b in movie.chunks(at, lo):
        k = Live.kv(lv.cmd("run " + movie.run_args(a, b)))
        if int(k["frame"]) != b:
            raise SystemExit(f"scan: the replay stopped at frame {k['frame']} (run {a}..{b}: {k.get('stop')})")
    if lo == hi:
        return [(lo, eval_exprs(lv, V, exprs))]
    ids, sl = sampled_cols(lv, V, exprs)
    first = [float(f"{float(x):.9g}") for x in Live.kv(lv.cmd("eval " + ",".join(map(str, ids))))["v"].split(":")]
    rows = [(lo, fold_row(first, sl))]           # (%.9g: as `run` prints its samples — one value, one spelling)
    for a, b in movie.chunks(lo, hi):
        k = Live.kv(lv.cmd(f"run {movie.run_args(a, b)} s={','.join(map(str, ids))} e=1"))
        got = parse_samples(k.get("samples"))
        if int(k["frame"]) != b or len(got) != b - a:
            raise SystemExit(f"scan: run {a}..{b} ended at frame {k['frame']} with {len(got)} rows")
        rows += [(f, fold_row(v, sl)) for f, v in got]
    return rows


def scan_frames(movie, V, lo, hi, exprs, live_run=None, tag="scan"):
    """EXPRS at every frame LO..HI of MOVIE's replay → (rows, info). Never changes a run. Two ways:
    - LIVE_RUN (a locked Run whose live session plays exactly MOVIE) holds a checkpoint savestate at or before LO: take a
      savestate at its head, restore the nearest checkpoint (after checking the session's movie there is MOVIE's), play
      MOVIE's inputs on, restore the head, free the savestate. The run's movie, checkpoints, undo stack stay as they were;
    - else a FRESH side session (the best host, as `rd new` picks it) replays MOVIE from boot to LO; stopped afterwards."""
    t0, info = time.time(), {}
    if live_run is not None and live_run.attach(cached_only=True):
        r, lv = live_run, live_run.live
        cks = r.meta.get("checkpoints", {})
        cands = [(sf, n, sid) for n, (sid, sf) in (r.sess.get("snaps") or {}).items()
                 if n in cks and cks[n]["frame"] == sf and sf <= lo]
        if r.movie.frame <= lo:
            cands.append((r.movie.frame, "head", None))
        best = max(cands, default=None)
        if best and best[2] is None:                    # the head itself (LO = HI = the run's frame)
            rows = replay_window(lv, movie, best[0], lo, hi, V, exprs)
            return rows, dict(how="the run's live session at its head", host=r.sess.get("host", "wine"), reach=0,
                              wall=time.time() - t0)
        if best:
            sf, n, sid = best
            sr = lv.cmd("snap", check=False)
            if sr.startswith("ok") and int(Live.kv(sr).get("epoch", -1)) == r.sess.get("epoch"):
                here = int(Live.kv(sr)["id"])
                try:
                    rr = lv.cmd(f"restore {sid}", check=False)
                    if rr.startswith("ok") and int(Live.kv(rr)["frame"]) == sf and \
                            Movie.from_server(lv.cmd("movie"), None).ops == movie.truncated(sf).ops:
                        rows = replay_window(lv, movie, sf, lo, hi, V, exprs)
                        return rows, dict(how=f"the run's live session from checkpoint {n} (f{sf})", host=r.sess.get("host", "wine"),
                                          reach=lo - sf, wall=time.time() - t0)
                    print(f"[checkpoint {n}'s savestate is not this movie's any more: replaying from boot instead]")
                finally:
                    lv.cmd(f"restore {here}")
                    lv.cmd(f"free {here}")
                    k = Live.kv(lv.cmd("ping"))
                    if int(k["frame"]) != r.movie.frame:
                        raise SystemExit(f"scan: the run's session did not return to its head (f{k['frame']}, movie f{r.movie.frame})")
            elif sr.startswith("ok"):                    # (the session.json epoch was stale: keep the truth)
                lv.cmd(f"free {Live.kv(sr)['id']}", check=False)
                r.epoch_check(sr)
                r.save_sess()
    h = spawn_live(tag, movie.truncated(hi), upto=lo, idle=300)
    try:
        lv = Live(h["port"])
        try:
            lv.cmd("ping")                              # answers once the replay to LO is done
            t1 = time.time()
            rows = replay_window(lv, movie, lo, lo, hi, V, exprs)
        finally:
            lv.close()
    finally:
        kill_session(h)
    return rows, dict(how=f"a fresh session replaying from boot", host=h["host"], reach=lo, wall=time.time() - t0,
                      spawn=t1 - t0, sample=time.time() - t1)


# ------------------------------------------------------------------ start points
def scenario_end_save(name):
    """the save.dat scenario NAME ends with → its _saves blob: the end-save cache, the real-device golden, or a headless replay"""
    tr, start = RH.scenario_inputs(name)
    hit = RH.endsave_cache(tr)
    if hit:
        return hit[0]
    import argparse as _ap
    g = RH.golden_dir(_ap.Namespace(target="win"), name, tr, start)
    tmp = os.path.join(RUNS, f".endsave-{os.getpid()}.dat")
    os.makedirs(RUNS, exist_ok=True)
    frame = None
    if os.path.exists(os.path.join(g, "save.dat.gz")):
        open(tmp, "wb").write(gzip.open(os.path.join(g, "save.dat.gz"), "rb").read())
        frame = json.load(open(os.path.join(g, "meta.json")))["result"].get("frame")
    else:
        b = RH.replay_args(target=pick_host())
        b.jobs, b.wine_timeout, b.headless, b.no_golden = 1, 14400, True, True
        _, bd, res, out = RH.run_batch(b, [(name, tr, None, start)], "rd-endsave", fetch=("rhost.log", "save.dat"))
        rr = res.get(name) or {}
        if rr.get("complete") != "1" or not os.path.exists(os.path.join(bd, name, "save.dat")):
            raise SystemExit(f"end:{name}: the replay did not complete or commits no save")
        shutil.copy(os.path.join(bd, name, "save.dat"), tmp)
        frame = rr.get("frame")
        shutil.rmtree(bd, ignore_errors=True)
    blob = RH.store_end_save(name, tr, start, tmp, frame)
    os.remove(tmp)
    if not blob:
        raise SystemExit(f"end:{name}: that scenario commits no save of its own")
    return blob


def cmd_endsave(a):
    """the save.dat this run's movie has committed (an in-game Save) → a _saves blob: `rd new R2 --from save:SHA` starts from it"""
    r = Run(a.run)
    r.attach()
    sp = os.path.join(r.dir, ".endsave.dat")
    if os.path.exists(sp):
        os.remove(sp)
    if not sess_file(r.sess, "save.dat", sp) or (r.movie.save and open(sp, "rb").read() == RH.save_bytes(r.movie.save)):
        raise SystemExit("this run has not committed a save of its own (pause menu → Save, or a day-end save)")
    sha, blob = RH.trace_save.store_save(sp, SCEN_SAVES)
    os.remove(sp)
    r.meta["endsave"] = {"sha": sha, "frame": r.movie.frame}
    r.save_meta()
    print(f"end save {sha[:12]} (at frame {r.movie.frame}): rd new NEXT --from save:{sha[:12]}")
    r.close()


def resolve_start(src):
    """--from → (Movie, replay source trace or None, frame limit)"""
    if src == "fresh":
        return Movie(None, [], 0), None, None
    if src.startswith("end:"):                       # the save a scenario ends with; the movie starts at boot with it
        return Movie(str(scenario_end_save(src[4:])), [], 0), None, None
    if src.startswith("save:"):                      # a _saves blob (sha prefix) or a path
        return Movie(str(RH.save_arg(src[5:])), [], 0), None, None
    m = re.fullmatch(r"([^:@]+)(?::([^@]+))?(?:@(\d+))?", src)
    if not m:
        raise SystemExit(f"--from {src!r}: fresh | SCENARIO[@F] | RUN[:CHECKPOINT|@F] | end:SCENARIO | save:SHA")
    name, ck, fr = m.group(1), m.group(2), m.group(3) and int(m.group(3))
    rd = os.path.join(RUNS, name)
    if os.path.exists(os.path.join(rd, "movie.jsonl")):
        mv = Movie.load(os.path.join(rd, "movie.jsonl"))
        if ck:
            cks = json.load(open(os.path.join(rd, "meta.json"))).get("checkpoints", {})
            if ck not in cks:
                raise SystemExit(f"run {name} has no checkpoint {ck!r} ({', '.join(cks) or 'none'})")
            fr = cks[ck]["frame"]
        return (mv.truncated(fr) if fr is not None else mv), None, None
    if os.path.exists(os.path.join(RH.scen_dir(name), "trace.jsonl")):
        tr, save = RH.scenario_inputs(name)
        return Movie(save, [], 0), tr, fr
    raise SystemExit(f"--from {src!r}: no such run or scenario")


def cmd_new(a):
    rd = os.path.join(RUNS, a.run)
    if os.path.exists(os.path.join(rd, "meta.json")):
        if not a.force:
            raise SystemExit(f"run {a.run} exists (--force replaces it)")
        r = Run(a.run); r.stop(); r.close()
        shutil.rmtree(rd)
    mv, trace, limit = resolve_start(a.src)
    r = Run(a.run, create=True)
    r.meta.update(created=time.strftime("%Y-%m-%d %H:%M:%S"), start=a.src)
    r.movie = mv
    if trace:
        # an existing scenario (any trace dialect): replay it in a live session; its resolved inputs become our movie
        t0 = time.time()
        h = spawn_live(a.run + "-new", mv, src_trace=trace, limit=limit)
        try:
            r.live = Live(h["port"])
            k = Live.kv(r.live.cmd("ping"))
        except BaseException:
            kill_session(h)
            raise
        r.sess = dict(h, epoch=int(k["epoch"]), snaps={}, skip=SKIP_FN, mask=SKIP_MASK, mark=None,
                      bin=RH.file_sha12(os.path.join(RH.BUILD, "rhost.exe"), os.path.join(RH.BUILD, "v3proxy_d3d8.dll")))
        r.sync_movie()
        r.save_sess()
        print(f"[replayed {a.src} to frame {r.movie.frame} in {time.time() - t0:.1f}s]")
    else:
        r.movie.write(r.movie_p, comment=a.run, rel_to=r.dir)
        r.save_meta()
        r.attach()
    print(f"run {a.run}: {r.status_line()}")
    r.close()


def cmd_play(a):
    r = Run(a.run)
    r.play(a.spec, until=a.until, avoid=a.avoid, watch=split_top(a.watch), every=a.every, rows=a.rows)
    r.close()


def cmd_tap(a):
    r = Run(a.run)
    if not a.text:
        r.play(f"({a.spec} .*{a.gap})x{a.n}", until=a.until, avoid=a.avoid)
        r.close()
        return
    # a transcript: the text on screen just before each press, rows new since the previous press only
    lv = r.attach()
    r.push_undo()
    f0, seen, why, held = r.movie.frame, set(), "spec ended", None

    def flush(now=()):
        """print the held sample, minus the rows that were still being typed (a prefix of the row now at that spot)"""
        nonlocal held
        if held:
            at = {(x, y): t for x, y, t in now}
            keep = [(x, y, t) for x, y, t in held[1] if not (at.get((x, y), "") != t and at.get((x, y), "").startswith(t))]
            if keep:
                print(f"f{held[0]}:")
                for x, y, t in keep:
                    print(f"  {x:5d} {y:4d}  {t}")
        held = None

    def show():
        nonlocal held
        rows = [(x, y, t if bud < 0 else t[:bud]) for x, y, col, bud, t in screen_text(lv) if t.strip()]
        flush(rows)
        new = [w for w in rows if w not in seen]
        if new:
            held = (r.movie.frame, new)
        seen.clear(); seen.update(rows)
    for _ in range(a.n):
        show()
        o = r.play(f"{a.spec} .*{a.gap}", until=a.until, avoid=a.avoid, quiet=True, undo=False)
        if o["stop"] != "end":
            why = o["stop"]
            break
    show()
    flush()
    print(f"ran {r.movie.frame - f0} ticks ({why})  " + r.status_line())
    r.close()


def screen_text(lv):
    """the strings the last tick drew → [(x, y, color, budget, text)]; a dialogue row's budget = characters revealed"""
    k = Live.kv(lv.cmd("text"))
    rows = []
    for t in filter(None, k.get("rows", "").split(";")):
        x, y, col, bud, hx = t.split(":")
        rows.append((int(x), int(y), int(col, 16), int(bud), bytes.fromhex(hx).decode("cp932", errors="replace")))
    return rows


def cmd_text(a):
    r = Run(a.run)
    rows = screen_text(r.attach())
    print(f"frame {r.movie.frame}: {len(rows)} text draws (x, y: screen px of 1024x768; draw order)" + ("" if rows else " — none this tick (step a tick: rd play R .)"))
    for x, y, col, bud, txt in rows:
        shown = txt if bud < 0 else txt[:bud] + ("▏" + txt[bud:] if bud < len(txt) else "")
        print(f"  {x:5d} {y:4d}  {col:08x}  {shown!r}")
    r.close()


def cmd_status(a):
    r = Run(a.run)
    print(r.status_line())
    cks = r.meta.get("checkpoints", {})
    if cks:
        print("checkpoints: " + "  ".join(f"{n}@{c['frame']}" for n, c in sorted(cks.items(), key=lambda kv: kv[1]["frame"])))
    r.close()


def cmd_eval(a):
    r = Run(a.run)
    if a.at or a.at_frame is not None:
        # peek at an earlier frame: the nearest checkpoint savestate of the live session (restored, then the head again),
        # else a fresh side session replaying the movie — the run itself does not move
        cks = r.meta.get("checkpoints", {})
        if a.at and a.at not in cks:
            raise SystemExit(f"no checkpoint {a.at!r} ({', '.join(cks) or 'none'})")
        fr = cks[a.at]["frame"] if a.at else a.at_frame
        if not 0 <= fr <= r.movie.frame:
            raise SystemExit(f"frame {fr} is outside the movie (0..{r.movie.frame})")
        rows, info = scan_frames(r.movie, r.vars, fr, fr, a.exprs, live_run=r, tag=f"{r.name}-at")
        vals = rows[0][1]
        print(f"at {'checkpoint ' + a.at + ', ' if a.at else ''}frame {fr} ({info['how']}, {info['host']}, {info['wall']:.1f}s); "
              f"the run stays at frame {r.movie.frame}")
    else:
        vals = r.ev(*a.exprs)
    for e, v in zip(a.exprs, vals):
        print(f"{e} = {repr(v) if a.raw else fmt(v)}" + (f"  (0x{int(v) & 0xffffffff:x})" if isinstance(v, int) and abs(v) > 9 else ""))
    r.close()


def scan_filter(rows, lo, nw, every=0, changes=False, where=False):
    """rows [(f, [watch values.., where value])] → the rows to print: every N-th tick from LO, only those whose watched
    values differ from the previous printed row (--changes), only where the last column holds (--where)"""
    out, prev = [], None
    for f, v in rows:
        if every and (f - lo) % every:
            continue
        if where and not v[nw]:
            continue
        if changes and prev is not None and v[:nw] == prev:
            continue
        out.append((f, v[:nw]))
        prev = v[:nw]
    return out


def cmd_scan(a):
    exprs = split_top(a.watch)
    if not exprs:
        raise SystemExit("--watch EXPR,EXPR..: what to read at each frame")
    view = Run(a.run, lock=False)                      # the names (and, without --at-run, the movie): read only
    src = Run(a.at_run, lock=False) if a.at_run else view
    V = Vars(view.dir, also=[src.dir] if a.at_run else ())
    live_run = None
    if not a.fresh and not a.at_run and os.path.exists(view.sess_p):
        try:                                           # its live session's checkpoints are a head start
            live_run = Run(a.run)
        except SystemExit:
            live_run = None
    try:
        movie = (live_run or src).movie
        lo, hi = a.frm, movie.frame if a.to is None else a.to
        if not 0 <= lo <= hi <= movie.frame:
            raise SystemExit(f"--from {lo} --to {hi}: the movie of {src.name} is 0..{movie.frame}")
        cols = exprs + ([a.where] if a.where else [])
        rows, info = scan_frames(movie, V, lo, hi, cols, live_run=live_run, tag=f"{a.run}-scan")
    finally:
        if live_run:
            live_run.close()
    shown = scan_filter(rows, lo, len(exprs), a.every, a.changes, bool(a.where))
    sampled = hi - lo + 1
    print(f"scan {src.name} f{lo}..f{hi}: {sampled} frames in {info['wall']:.1f}s — {info['how']} on {info['host']}, "
          f"{info['reach']} ticks to f{lo}" + (f" (spawn + replay {info['spawn']:.1f}s, sampling {info['sample']:.2f}s = "
                                              f"{sampled / max(info['sample'], 1e-3):.0f} frames/s)" if "spawn" in info else "")
          + (f"; {len(shown)} rows shown" if len(shown) != len(rows) else ""))
    print("  frame  " + "  ".join(exprs))
    show = (lambda x: repr(x)) if a.raw else fmt
    for f, v in shown:
        print(f"  {f:6d} " + "  ".join(show(x) for x in v))


def cmd_ram(a):
    r = Run(a.run)
    if a.op == "list":
        for n, d in sorted(r.vars.defs.items()):
            what = d.get("expr") or f"{d.get('type', 's32')} @ {d['addr']}"
            flags = "".join(f" [{f}]" for f in ("status", "event") if d.get(f))
            print(f"{n:16s} {what:44s}{flags}  {d.get('note', '')}")
    elif a.op == "status":
        print(r.status_line())
    elif a.op == "read":
        va, n = int(a.args[0], 16), int(a.args[1], 0) if len(a.args) > 1 else 64
        h = bytes.fromhex(r.attach().cmd(f"r {va:x} {n}").split()[1])
        for i in range(0, len(h), 16):
            row = h[i:i + 16]
            dw = " ".join(f"{int.from_bytes(row[j:j + 4], 'little'):08x}" for j in range(0, len(row) - 3, 4))
            print(f"{va + i:08x}  {dw}  {''.join(chr(b) if 32 <= b < 127 else '.' for b in row)}")
    elif a.op == "actors":                     # the dungeon actor table: every slot in use
        import struct
        lv = r.attach()
        px, pz = r.ev("px", "pz")
        print("  i type use     hp       x       y       z    yaw  st    t  sub  ae4   pow  dist  unhit stag   age   (aI<field> names: `rd ram R list`)")
        for i in range(128):
            h = bytes.fromhex(lv.cmd(f"r {0x76b970 + i * 0xba4:x} {0xba4}").split()[1])
            s32_ = lambda o: int.from_bytes(h[o:o + 4], "little", signed=True)
            f32_ = lambda o: struct.unpack("<f", h[o:o + 4])[0]
            if not s32_(0x428):
                continue
            x, z = f32_(0x3f0), f32_(0x3f8)
            print(f"{i:3d} {s32_(0x424):4d} {s32_(0x428):3d} {f32_(0x42c):6.0f} {x:7.2f} {f32_(0x3f4):7.2f} {z:7.2f} {f32_(0x420):6.2f} {s32_(0xa54):3d} {s32_(0xa58):4d} {s32_(0xa5c):4d} "
                  f"{s32_(0xae4):4d} {f32_(0xab8):5.2f} {math.hypot(x - px, z - pz):5.1f} {s32_(0xb08):6d} {s32_(0x440):4d} {s32_(0x398):5d}")
    elif a.op == "dwords":                     # N dwords from VA: hex, signed, float
        import struct
        va, n = int(a.args[0], 16), int(a.args[1], 0) if len(a.args) > 1 else 16
        h = bytes.fromhex(r.attach().cmd(f"r {va:x} {n * 4}").split()[1])
        for i in range(n):
            u = int.from_bytes(h[i * 4:i * 4 + 4], "little")
            f = struct.unpack("<f", h[i * 4:i * 4 + 4])[0]
            print(f"{va + i * 4:08x}  [{i:3d}]  {u:08x}  {s32(u):>11d}" + (f"  {f:.6g}" if u and 1e-6 < abs(f) < 1e9 else ""))
    elif a.op == "str":                        # the C string at VA (cp932), up to N bytes
        va, n = int(a.args[0], 16), int(a.args[1], 0) if len(a.args) > 1 else 128
        h = bytes.fromhex(r.attach().cmd(f"r {va:x} {n}").split()[1])
        print(repr(h.split(b"\0")[0].decode("cp932", errors="replace")))
    elif a.op == "define":
        if not a.args:
            raise SystemExit("rd ram R define NAME --addr HEX [--type u8|s8|u16|s16|u32|s32|f32|f64] | --expr EXPR [--status] [--event] [--note ..]")
        d = {}
        if a.expr:
            d["expr"] = a.expr
        elif a.addr:
            d.update(addr=f"0x{int(a.addr, 16):x}", type=a.type)
            if a.type not in TYPES:
                raise SystemExit(f"--type: one of {' '.join(TYPES)}")
        else:
            raise SystemExit("define needs --addr or --expr")
        for f in ("status", "event"):
            if getattr(a, f):
                d[f] = True
        if a.note:
            d["note"] = a.note
        r.vars.define(a.args[0], d, shared=a.shared)
        v = r.ev(a.args[0])[0]
        print(f"{a.args[0]} = {fmt(v)}  ({'tools/drive/vars.json' if a.shared else 'this run'})")
    elif a.op == "mark":
        lv = r.attach()
        r.sess["mark"] = r.snap()
        r.save_sess()
        print(f"marked frame {r.movie.frame}: now act (rd play ..), then `rd ram {a.run} diff`")
    elif a.op == "diff":
        lv = r.attach()
        if not r.sess.get("mark"):
            raise SystemExit("no mark (rd ram R mark) — or a search / anchor since dropped it")
        cmd = f"diff {r.sess['mark']} max={a.max}"
        if a.lo:
            cmd += f" lo={int(a.lo, 16):x}"
        if a.hi:
            cmd += f" hi={int(a.hi, 16):x}"
        k = Live.kv(lv.cmd(cmd))
        rows = [tuple(int(x, 16) for x in t.split(":")) for t in filter(None, k.get("d", "").split(","))]
        minus = set()
        if a.minus:
            minus = {int(l.split()[0], 16) for l in open(os.path.join(r.dir, a.minus + ".diff")) if l.strip()}
        rows = [x for x in rows if x[0] not in minus]
        if a.save:
            with open(os.path.join(r.dir, a.save + ".diff"), "w") as f:
                for va, was, now in rows:
                    f.write(f"{va:08x} {was:08x} {now:08x}\n")
        import struct
        print(f"{k['n']} dwords differ from the mark" + (f" ({len(rows)} after --minus {a.minus})" if a.minus else "") +
              (f" → saved as {a.save}" if a.save else ""))
        for va, was, now in rows[:a.show]:
            fw, fn = struct.unpack("<f", struct.pack("<I", was))[0], struct.unpack("<f", struct.pack("<I", now))[0]
            fl = f"  f32 {fw:.4g} → {fn:.4g}" if all(1e-6 < abs(x) < 1e7 or x == 0 for x in (fw, fn)) else ""
            print(f"  {va:08x}  {was:08x} → {now:08x}  ({s32(was)} → {s32(now)}){fl}")
        if len(rows) > a.show:
            print(f"  … {len(rows) - a.show} more (--show N, --lo/--hi VA to narrow, --minus NAME to drop a noise set)")
    else:
        raise SystemExit("rd ram R list | status | read VA [NBYTES] | dwords VA [N] | str VA [N] | define NAME .. | mark | diff")
    r.close()


def s32(v):
    return v - (1 << 32) if v & 0x80000000 else v


def cmd_ck(a):
    r = Run(a.run)
    cks = r.meta.setdefault("checkpoints", {})
    if a.op == "list":
        for n, c in sorted(cks.items(), key=lambda kv: kv[1]["frame"]):
            print(f"{n:20s} frame {c['frame']:>7d}  {c.get('note', '')}")
    elif a.op == "save":
        r.attach()
        sid = r.snap()
        cks[a.name] = {"frame": r.movie.frame, "note": " ".join(a.note)}
        r.sess["snaps"][a.name] = [sid, r.movie.frame]
        r.save_meta(); r.save_sess()
        print(f"checkpoint {a.name} at frame {r.movie.frame}")
    elif a.op == "load":
        if a.name not in cks:
            raise SystemExit(f"no checkpoint {a.name!r} ({', '.join(cks) or 'none'})")
        fr = cks[a.name]["frame"]
        was = r.movie.frame
        r.attach(cached_only=True)                     # no session: straight to the prefix replay
        r.push_undo(tail=True)
        how = r.load_frame(fr, (r.sess["snaps"].get(a.name) or [None])[0] if r.sess else None)
        dropped = [n for n, c in cks.items() if c["frame"] > fr]
        for n in dropped:
            del cks[n]                                 # checkpoints of the abandoned future
        r.save_meta(); r.save_sess()
        print(f"loaded {a.name} ({how})  " + r.status_line())
        if was > fr:
            print(f"  the movie now ends at f{fr} (was f{was})" + (f"; checkpoints dropped: {' '.join(dropped)}" if dropped else "") +
                  f" — `rd undo {a.run}` brings it all back. To only LOOK at an earlier state: rd eval {a.run} EXPR --at {a.name}")
    r.close()


def cmd_undo(a):
    r = Run(a.run)
    stack = r.meta.get("undo", [])
    if not stack:
        raise SystemExit("nothing to undo (rd rewind R --to F replays a prefix)")
    u = stack.pop()
    tail = os.path.join(r.dir, u["tail"]) if u.get("tail") else None
    movie = Movie.load(tail) if tail and os.path.exists(tail) else None
    if u["frame"] > r.movie.frame and not movie:
        raise SystemExit(f"the undo copy of the movie is gone ({u.get('tail')}): cannot return to f{u['frame']}")
    r.attach(cached_only=True)
    how = r.load_frame(u["frame"], u["sid"] if r.sid_ok(u) else None, movie=movie)
    r.meta["checkpoints"] = {n: c for n, c in u["cks"].items() if c["frame"] <= u["frame"]}
    if tail and os.path.exists(tail):
        os.remove(tail)
    r.save_meta(); r.save_sess()
    print(f"undone ({how})  " + r.status_line())
    r.close()


def cmd_rewind(a):
    r = Run(a.run)
    fr = a.to if a.to is not None else r.movie.frame - a.frames
    if fr < 0 or fr > r.movie.frame:
        raise SystemExit(f"frame {fr} is outside the movie (0..{r.movie.frame})")
    r.attach(cached_only=True)
    r.push_undo(tail=True)                             # `rd undo` brings the tail back (by savestate, or by replay)
    # a live checkpoint exactly there = this movie's own savestate: no replay
    sid = next((r.sess["snaps"][n][0] for n, c in r.meta.get("checkpoints", {}).items()
                if r.sess and c["frame"] == fr and r.sess["snaps"].get(n, [None, -1])[1] == fr), None)
    r.load_frame(fr, sid)
    cks = r.meta.get("checkpoints", {})
    for n in [n for n, c in cks.items() if c["frame"] > fr]:
        del cks[n]
    r.save_meta()
    print("rewound  " + r.status_line())
    r.close()


# ------------------------------------------------------------------ search
DEFAULT_ACTIONS = ["R*8", "L*8", "U*8", "D*8", "UR*8", "UL*8", "DR*8", "DL*8", "R*2", "L*2", "U*2", "D*2", "A .*7", "B .*7", ".*8"]
HOLD_ACTIONS = ["."]                                    # --hold's default tail: stand still (each hold action is cycled to N ticks)


def action_arg(specs, prog=None):
    acts = []
    for s in specs:
        seg, escs, total = spec_to_run(parse_spec(s), prog)
        if escs:
            raise SystemExit(f"action {s!r}: ESC (~) cannot be part of a search action — play it yourself")
        if not total:
            raise SystemExit(f"empty action {s!r}")
        acts.append(seg.replace(",", "+"))
    return "|".join(acts)


def search_progs(V, a):
    """every program a search sends, compiled and checked against the server's limits HERE, before any session moves:
    a bad / over-long expression is its own message (with -j N it used to die inside a worker thread: a traceback)"""
    P = {"goal": V.program(a.goal, f"--goal {a.goal!r}"),
         "cell": [prog_check(t, f"--cell {a.cell!r}") for t in V.compile_tuple(a.cell)],
         "avoid": a.avoid and V.program(a.avoid, f"--avoid {a.avoid!r}"),
         "score": a.score and V.program(a.score, f"--score {a.score!r}")}
    check = lambda e: (V.program(e, f"action mask {{{e}}}"), 0)[1]     # noqa: E731 — {EXPR} masks inside action specs
    action_arg(a.actions or DEFAULT_ACTIONS, check)
    if getattr(a, "hold", 0):
        if a.hold < 0:
            raise SystemExit("--hold N: N ≥ 0 ticks")
        action_arg(a.hold_actions or HOLD_ACTIONS, check)
    return P


def explore_cmd(lv, V, a, seed, apply, P=None):
    P = P or search_progs(V, a)
    g = lv.prog(P["goal"])
    cells = [lv.prog(t) for t in P["cell"]]
    cmd = f"explore g={g} c={','.join(map(str, cells))} acts={action_arg(a.actions or DEFAULT_ACTIONS, lambda e: lv.prog(V.compile(e)))}"
    if P["avoid"]:
        cmd += f" a={lv.prog(P['avoid'])}"
    if P["score"]:
        cmd += f" score={lv.prog(P['score'])} greed={a.greed}"
    if getattr(a, "hold", 0):
        cmd += f" hold={a.hold} hacts={action_arg(a.hold_actions or HOLD_ACTIONS, lambda e: lv.prog(V.compile(e)))}"
        if a.hold_no_goal:
            cmd += " hgoal=0"
    cmd += f" t={int(a.time_limit * 1000)} refine={int(a.refine * 1000)} seed={seed} maxlen={a.max_len} sticky={a.sticky} apply={1 if apply else 0}"
    return cmd


def hold_report(results, a):
    """--hold: how many goal arrivals the hold turned away, over every session"""
    if not getattr(a, "hold", 0):
        return
    rej = sum(int(k.get("rejected", 0)) for k, _ in results)
    held = sum(int(k.get("held", 0)) for k, _ in results)
    print(f"  hold {a.hold}: {rej} goal arrival(s) rejected (an --avoid hit or the goal lost within {a.hold} ticks on every hold action), "
          f"{held} survived")


def cmd_search(a):
    if a.apply_best:
        a.apply = True                                 # --apply-best keeps a found path too, not only the best cell's
    r = Run(a.run)
    if not a.cell:
        a.cell = r.vars.defs.get("_cell", {}).get("expr") or "(scene, int(px), int(pz))"
    P = search_progs(r.vars, a)                        # before anything moves: a bad expression is just its message
    lv = r.attach()
    if r.ev(a.goal)[0]:
        print("the goal already holds here — nothing to search")
        r.close()
        return
    r.push_undo()
    st = {"applied": False}
    try:
        return search_body(r, lv, a, P, st)
    finally:
        if not st["applied"]:                          # nothing played: the undo entry would be a no-op step
            try:
                r.drop_undo()
            except (OSError, SessionEnded):
                pass
        r.save_sess()
        r.close()


def search_body(r, lv, a, P, st):
    t0 = time.time()
    results = []
    if a.jobs <= 1:
        res = lv.cmd(explore_cmd(lv, r.vars, a, a.seed, a.apply, P), check=False)
        if not res.startswith("ok"):
            raise SystemExit("search: " + res)
        r.epoch_check(res)
        results.append((Live.kv(res), "main"))
        if Live.kv(res).get("applied") == "1":
            st["applied"] = True
    else:
        # extra live sessions at the same frame (each replays the movie), one exploration per session with its own seed
        workers, errs = [], []
        main = r.sess.get("host", "wine")
        # the main session's host first; under RD_HOST=auto a worker it does not admit goes to the next host of the order
        # (each worker replays the movie: its savestates are its own anyway)
        order = [main] + ([h for h in HOSTS if h != main] if RD_HOST == "auto" else [])

        passed = {}                         # host → why it did not take a worker (the first reason)

        def boot(i):
            last = None
            for h in order:
                if h != main and not host_up(h):
                    continue
                try:
                    hs = spawn_live(f"{r.name}-w{i}", r.movie, idle=600, wait=0, host=h)   # optional: no slot → fewer workers
                    workers.append((i, hs))
                    return
                except BaseException as e:      # noqa: B902 — SystemExit / NotAdmitted from spawn_live
                    last = e
                    passed.setdefault(h, str(e).split(" — ", 1)[-1][:160])
            errs.append(str(last))
        ts = [threading.Thread(target=boot, args=(i,)) for i in range(1, a.jobs)]
        [t.start() for t in ts]

        def one(lvx, seed, apply, tag):
            try:
                res = lvx.cmd(explore_cmd(lvx, r.vars, a, seed, apply, P), check=False)
            except BaseException as e:          # noqa: B902 — a thread's SystemExit / socket error: keep the message
                res = f"err {type(e).__name__}: {e}"
            results.append((Live.kv(res) if res.startswith("ok") else {"found": "0", "err": res}, tag))
        try:
            [t.join() for t in ts]
            if errs:
                print(f"[{len(errs)} worker(s) did not start: {errs[0][:120]}]")
            workers.sort()
            by = {}
            for tag, h in [("main", main)] + [(f"w{i}", hs["host"]) for i, hs in workers]:
                by.setdefault(h, []).append(tag)
            print(f"[{len(workers) + 1} sessions at frame {r.movie.frame}, {time.time() - t0:.1f}s to start the extra ones; "
                  + ", ".join(f"{h}: {' '.join(t)}" for h, t in by.items()) + "]")
            for h, why in passed.items():
                print(f"  [{h} took no more: {why}]")
            ths = [threading.Thread(target=one, args=(lv, a.seed, False, f"main@{main}"))]
            lvs = []
            for i, h in workers:
                lx = Live(h["port"]); lvs.append(lx)
                ths.append(threading.Thread(target=one, args=(lx, a.seed + i * 7919, False, f"w{i}@{h['host']}")))
            [t.start() for t in ths]
            [t.join() for t in ths]
            [lx.close() for lx in lvs]
        finally:
            ks = [threading.Thread(target=kill_session, args=(h,)) for i, h in workers]
            [t.start() for t in ks]
            [t.join() for t in ks]
        r.sess.update(snaps={}, mark=None)            # the main session's explore re-anchored
        r.sess["epoch"] = int(Live.kv(lv.cmd("ping"))["epoch"])
    errs = [(k["err"], tag) for k, tag in results if "err" in k]
    if errs and len(errs) == len(results):
        raise SystemExit(f"search: {errs[0][0]}  [{errs[0][1]}]")
    for e, tag in errs:
        print(f"[session {tag} failed: {e[:300]}]")
    results = [(k, tag) for k, tag in results if "err" not in k]
    found = [(int(k["frames"]), k, tag) for k, tag in results if k.get("found") == "1"]
    ticks = sum(int(k.get("ticks", 0)) for k, _ in results)
    if len(results) > 1:
        per = {}
        for k, tag in results:
            h = tag.split("@")[-1]
            per.setdefault(h, [0, 0])
            per[h][0] += 1
            per[h][1] += int(k.get("ticks", 0))
        # explore's clock starts after a worker's replay: ticks / --time-limit = the searching rate
        print("  per host: " + ", ".join(f"{h} {n} session(s) {t} ticks = {t / a.time_limit:.0f}/s ({t / a.time_limit / n:.0f} each)"
                                         for h, (n, t) in per.items()))
    cells = max(int(k.get("cells", 0)) for k, _ in results)
    wall = time.time() - t0
    if not found:
        k = results[0][0]
        print(f"found: false   {cells} cells, {ticks} ticks searched in {wall:.1f}s ({ticks / max(wall, 0.01):.0f} ticks/s)")
        print(f"  newest cells (frames@{a.cell}): " + "  ".join(k.get("newest", "").split(";")))
        print("  one cell only = your actions change nothing the cell sees; split the goal at where it got to, add the missing action, "
              "or put the mechanic's state in --cell")
        hold_report(results, a)
        if a.score:                                    # progress to keep: the best-scored cell any session reached
            cands = [(float(k["bscore"]), -int(k["bframes"]), i) for i, (k, _) in enumerate(results) if k.get("bpath")]
            if not cands:
                print(f"  score: nothing scored above the start ({results[0][0].get('score0', '?')})")
            else:
                bs, _, i = max(cands)
                k = results[i][0]
                spec = segs_to_spec(k["bpath"])
                print(f"  best score {fmt(bs)} (start {fmt(float(k['score0']))}) after {k['bframes']} frames")
                if a.save:
                    open(a.save, "w").write(spec + "\n")
                if a.apply_best:
                    lv.cmd("run " + k["bpath"])
                    st["applied"] = True
                    r.sync_movie()
                    print(f"  applied the path to that cell (--apply-best): score now {fmt(r.ev(a.score)[0])}  " + r.status_line())
                    print(f"  search again from here; `rd undo {a.run}` drops it")
                    return 1
                print(f"  path to it: {spec if len(spec) < 600 else spec[:600] + ' …'}\n  (--apply-best plays it, so the next search starts there)")
        r.sync_movie()
        return 1
    frames, k, tag = min(found, key=lambda x: x[0])
    spec = segs_to_spec(k["path"])
    print(f"found: true   {frames} frames  ({cells} cells, {ticks} ticks in {wall:.1f}s = {ticks / max(wall, 0.01):.0f} ticks/s"
          + (f", best of {len(found)}/{len(results)} sessions [{tag}]" if len(results) > 1 else "") + ")")
    if k.get("arrive"):
        hs = (a.hold_actions or HOLD_ACTIONS)[int(k.get("tail", 0))]
        print(f"  goal reached at +{k['arrive']}, then held {a.hold} ticks on hold action {hs!r} (the path includes them)")
    hold_report(results, a)
    print(f"  path: {spec if len(spec) < 900 else spec[:900] + ' …'}")
    if a.save:
        open(a.save, "w").write(spec + "\n")
    if a.apply:
        if a.jobs > 1 or k.get("applied") != "1":
            lv.cmd("run " + k["path"])
        st["applied"] = True
        holds, *av = r.ev(a.goal, *([a.avoid] if a.avoid else []))
        r.sync_movie()
        print(f"  applied: goal {'holds' if holds else 'DOES NOT HOLD'}" + (f", avoid {'TRUE' if av[0] else 'clear'}" if av else "")
              + "  " + r.status_line())
    else:
        r.sync_movie()
        print("  not applied (--apply plays it; --save FILE keeps it: rd play R \"$(cat FILE)\")")
    return 0


# ------------------------------------------------------------------ pictures: a real-device replay with shots
def shots(r, frames, target=None, tag="look"):
    """replay the movie on REAL Direct3D (Windows via the runner; wine + Xvfb as the fallback) → {frame: png path}"""
    frames = sorted(set(f for f in frames if 0 < f <= r.movie.frame))
    if not frames:
        raise SystemExit("no frames to shoot (1..movie length)")
    target = target or os.environ.get("RD_LOOK_TARGET") or ("win" if win_up() else "wine")
    tr = os.path.join(r.dir, "look.trace.jsonl")
    r.movie.truncated(max(frames)).write(tr, comment=r.name, rel_to=r.dir)
    a = RH.replay_args(shot_at=",".join(map(str, frames)), frames=max(frames) + 1, target=target)
    a.jobs, a.wine_timeout, a.headless, a.no_golden = 1, 3600, False, True
    t0 = time.time()
    _, bd, res, out = RH.run_batch(a, [(tag, tr, None, r.movie.save)], "rd-" + tag, fetch=("rhost.log", "shot_*.png"))
    vd = os.path.join(r.dir, "view")
    os.makedirs(vd, exist_ok=True)
    got = {}
    for f in frames:
        src = os.path.join(bd, tag, f"shot_f{f:06d}.png")
        if os.path.exists(src):
            dst = os.path.join(vd, f"f{f:06d}.png")
            shutil.copy(src, dst)
            got[f] = dst
    shutil.rmtree(bd, ignore_errors=True)
    print(f"[{len(got)}/{len(frames)} pictures from a real-device replay on {target} in {time.time() - t0:.0f}s]")
    return got


def win_up(timeout=4):
    try:
        return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", f"ConnectTimeout={timeout}", RH.WIN, "echo ok"], capture_output=True,
                              text=True, timeout=timeout + 8).stdout.strip().endswith("ok")
    except (subprocess.TimeoutExpired, OSError):
        return False


def sheet(paths, out, cols=4, scale=0.375, labels=None):
    from PIL import Image, ImageDraw
    ims = [Image.open(p).convert("RGB") for p in paths]
    w, h = int(ims[0].width * scale), int(ims[0].height * scale)
    rows = -(-len(ims) // cols)
    sh = Image.new("RGB", (cols * w, rows * h), (20, 20, 20))
    d = ImageDraw.Draw(sh)
    for i, im in enumerate(ims):
        x, y = (i % cols) * w, (i // cols) * h
        sh.paste(im.resize((w, h), Image.LANCZOS), (x, y))
        if labels:
            d.rectangle([x, y, x + 8 + 7 * len(labels[i]), y + 14], fill=(0, 0, 0))
            d.text((x + 3, y + 1), labels[i], fill=(255, 255, 0))
    sh.save(out)
    return out


def cmd_look(a):
    r = Run(a.run)
    frames = [int(x) for x in a.at.split(",")] if a.at else [r.movie.frame]
    got = shots(r, frames, a.target)
    for f, p in got.items():
        if a.scale != 1.0:
            from PIL import Image
            im = Image.open(p)
            im.resize((int(im.width * a.scale), int(im.height * a.scale)), Image.LANCZOS).save(p)
        print(f"frame {f}: {p}")
    r.close()


def cmd_strip(a):
    r = Run(a.run)
    lo, hi = a.frm, a.to if a.to is not None else r.movie.frame
    every = a.every or max(1, (hi - lo) // 11)
    frames = list(range(max(1, lo), hi + 1, every))[:48]
    got = shots(r, frames, a.target, tag="strip")
    if got:
        out = os.path.join(r.dir, "view", f"strip_{lo}_{hi}.png")
        fs = sorted(got)
        sheet([got[f] for f in fs], out, cols=a.cols, labels=[f"f{f}" for f in fs])
        print(f"strip {lo}..{hi} every {every}: {out}")
    r.close()


# ------------------------------------------------------------------ verify / check / lint
def cmd_verify(a):
    """the movie replayed in a FRESH process reaches the live session's state (hashes), and — --real — the real game's"""
    r = Run(a.run)
    lv = r.attach()
    live = Live.kv(lv.cmd("hash"))
    tr = os.path.join(r.dir, "verify.trace.jsonl")
    r.movie.write(tr, comment=r.name, rel_to=r.dir)
    host = (r.sess or {}).get("host", "wine")      # the session's host: hashes compare like for like
    if host == "win":
        RH.WIN = r.sess.get("ssh", RH.WIN)         # (wine vs Windows: float ulps differ — README "Where sessions run")
    b = RH.replay_args(frames=r.movie.frame, target=host)
    b.jobs, b.wine_timeout, b.headless, b.no_golden = 1, 3600, True, True
    # plain retail (no skip); the scratch the SESSION left out of its hash (its own mask: a session outlives a mask change)
    b.extra = f"--mask-range {(r.sess or {}).get('mask', SKIP_MASK_V1)}" if SKIP_FN else None
    _, bd, res, out = RH.run_batch(b, [("verify", tr, None, r.movie.save)], "rd-verify", fetch=("rhost.log",))
    v = res.get("verify") or {}
    shutil.rmtree(bd, ignore_errors=True)
    # device memory is compared only without the skip: the draws / state setters of a skipped render function are missing from it
    keys = (("frame", "frame"), ("rng", "rng"), ("state_sem", "sem")) + (() if SKIP_FN else (("state_dev", "dev"),))
    same = all(v.get(x) == live.get(y) for x, y in keys)
    print(f"fresh headless replay: frame {v.get('frame')} rng {v.get('rng')} sem {v.get('state_sem')} dev {v.get('state_dev')}"
          + ("  (unskipped retail)" if SKIP_FN else "") + f"  [{host}]")
    print(f"live session:          frame {live['frame']} rng {live['rng']} sem {live['sem']} dev {live['dev']}"
          + (f"  (skips {SKIP_FN}: dev not compared)" if SKIP_FN else "") + f"  [{host}]")
    if not same and v.get("frame") == live.get("frame") and v.get("rng") == live.get("rng") and v.get("state_sem") != live.get("sem"):
        # same frame + RNG, another game-state hash: write-tracking residue of the live session (a write fault's exception
        # frame on the game stack, read back by retail as an uninitialised local — headless-savestates.md §3)? A session
        # rebuilt by a clean replay has taken no savestate yet, so nothing is tracked: it must hash like the fresh replay.
        r.live.close(); r.live = None
        r.stop()
        lv = r.attach()
        again = Live.kv(lv.cmd("hash"))
        print(f"clean session (replay): frame {again['frame']} rng {again['rng']} sem {again['sem']} dev {again['dev']}  [{r.sess.get('host', 'wine')}]")
        if all(v.get(x) == again.get(y) for x, y in keys):
            print("the first session's state differed from the movie's only by write-tracking residue (frame and RNG agreed): "
                  "its savestates are gone, the run continues on the clean session")
            same, live = True, again
    ok = same
    if r.meta.get("poked"):
        print("note: this run was POKED (rd ram w / live `w`): the movie cannot reproduce hand-written memory")
    if a.real:
        if not win_up():
            raise SystemExit(f"--real needs the Windows box (ssh {RH.WIN}); the headless verdict above is {'MATCH' if same else 'MISMATCH'}")
        c = RH.replay_args(frames=r.movie.frame, target="win")
        c.jobs, c.headless, c.no_golden = 1, False, True
        _, bd, res, out = RH.run_batch(c, [("verify", tr, None, r.movie.save)], "rd-verify-real", fetch=("rhost.log",))
        w = res.get("verify") or {}
        shutil.rmtree(bd, ignore_errors=True)
        real = w.get("frame") == live["frame"] and w.get("rng") == live["rng"] and w.get("scene") == live["scene"]
        print(f"real-device replay:    frame {w.get('frame')} rng {w.get('rng')} scene {w.get('scene')}  (Windows, Direct3D 8)")
        ok = ok and real
    print("MATCH" if ok else "MISMATCH")
    r.close()
    return 0 if ok else 1


def load_task(path):
    txt = open(path).read()
    m = re.match(r"---\n(.*?)\n---\n?(.*)", txt, re.S)
    if not m:
        raise SystemExit(f"{path}: needs a --- header ---")
    t = {"name": os.path.splitext(os.path.basename(path))[0], "start": "fresh", "steps": [], "forbid": [], "text": m.group(2).strip()}
    in_list = None
    for line in m.group(1).splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        s = re.match(r"\s+-\s*(.+)$", line)
        if in_list and s:
            item, hit = s.group(1), None
            for c in re.finditer(r":\s+", item):
                try:
                    ast.parse(item[c.end():].strip(), mode="eval")      # Python drops a trailing "# comment" itself
                except SyntaxError:
                    continue
                hit = (item[:c.start()].strip(), item[c.end():].split("#")[0].strip())
                break
            if not hit:
                raise SystemExit(f"{path}: cannot read step {line.strip()!r} (\"- label: EXPRESSION\")")
            t[in_list].append(hit)
            continue
        k = re.match(r"(\w+):\s*(.*?)\s*(?:#.*)?$", line)
        if not k:
            raise SystemExit(f"{path}: cannot read header line {line!r}")
        in_list = k.group(1) if k.group(1) in ("steps", "forbid") else None
        if not in_list:
            t[k.group(1)] = k.group(2)
    if not t["steps"]:
        raise SystemExit(f"{path}: no steps")
    return t


def start_frame(task):
    """steps are judged only after the task's start prefix (a step that already held inside it proves nothing)"""
    mv, trace, limit = resolve_start(task["start"])
    if trace:
        return limit or 0, True
    return mv.frame, False


def walk_movie(movie, V, preds, after=0, stop_when=None):
    """a fresh live session fed the movie tick by tick → for each predicate the frames where it holds (first, count),
    evaluated in order: `preds` = [(name, expr)]; returns per-frame truth via sampling in chunks"""
    h = spawn_live("check", movie, upto=0, idle=300)
    try:
        lv = Live(h["port"])
        ids = [lv.prog(V.compile(e)) for _, e in preds]
        truth = [[] for _ in preds]
        for at, hi in movie.chunks(0, movie.frame):
            k = Live.kv(lv.cmd(f"run {movie.run_args(at, hi)} s={','.join(map(str, ids))} e=1"))
            for row in filter(None, k.get("samples", "").split(";")):
                p = row.split(":")
                f = int(p[0])
                for i, x in enumerate(p[1:]):
                    if float(x) != 0:
                        truth[i].append(f)
        end = Live.kv(lv.cmd("hash"))
        lv.close()
        return truth, end
    finally:
        kill_session(h)


def cmd_check(a):
    r = Run(a.run)
    t = load_task(a.task)
    after, _ = start_frame(t)
    preds = t["steps"] + t["forbid"]
    truth, end = walk_movie(r.movie, r.vars, preds, after)
    ns, at, ok, first_step = len(t["steps"]), after, True, None
    print(f"task {t['name']}: {ns} steps, {len(t['forbid'])} forbids; movie {r.movie.frame} frames; steps judged after frame {after}")
    for i, (name, expr) in enumerate(t["steps"]):
        hit = next((f for f in truth[i] if f > at), None)
        if hit is None:
            ok = False
            print(f"  step {i + 1} NOT REACHED: {name}: {expr}")
            break
        if first_step is None:
            first_step = hit
        print(f"  step {i + 1} at frame {hit}: {name}")
        at = hit
    for j, (name, expr) in enumerate(t["forbid"]):
        bad = [f for f in truth[ns + j] if first_step is not None and f >= first_step]
        if bad:
            ok = False
            print(f"  FORBIDDEN at frame {bad[0]} ({len(bad)} frames): {name}: {expr}")
    print("PASS" if ok else "FAIL")
    r.close()
    return 0 if ok else 1


def cmd_lint(a):
    t = load_task(a.task)
    after, _ = start_frame(t)
    for name in a.on:
        r = Run(name)
        truth, _ = walk_movie(r.movie, r.vars, t["steps"] + t["forbid"])
        print(f"{name} ({r.movie.frame} frames; the task's start prefix ends at {after}):")
        for (label, expr), fs in zip(t["steps"] + t["forbid"], truth):
            if not fs:
                print(f"  never      {label}: {expr}")
            else:
                pre = sum(1 for f in fs if f <= after)
                print(f"  {len(fs):6d} f  first {fs[0]:>7d} last {fs[-1]:>7d}" + (f"  [{pre} inside the start prefix]" if pre else "") + f"  {label}")
        r.close()


# ------------------------------------------------------------------ filing + the port
def file_scenario(r, name, note=""):
    d = os.path.join(RH.SCEN, name)
    os.makedirs(d, exist_ok=True)
    save = r.movie.save
    if save and not os.path.realpath(save).startswith(os.path.realpath(RH.ROOT) + os.sep):
        raise SystemExit(f"the start save {save} is outside the repo")
    p = os.path.join(d, "trace.jsonl")
    r.movie.write(p, comment=f"{name}, authored with rd from {r.meta.get('start', '?')}" + (f"; {note}" if note else ""))
    return p


def cmd_file(a):
    r = Run(a.run)
    if r.meta.get("poked"):
        raise SystemExit("this run was POKED: its movie does not reproduce its state — not filed")
    p = file_scenario(r, a.name, " ".join(a.note))
    print(f"filed {os.path.relpath(p, ROOT)} ({r.movie.frame} frames). Next: rhost.py golden {a.name}; rhost.py triage {a.name}")
    r.close()


def cmd_port(a):
    """the payoff: this movie against the port — retail golden on the real device, then the port's first RNG split"""
    r = Run(a.run)
    name = a.name or ("_rd-" + r.name)
    if r.meta.get("poked"):
        raise SystemExit("this run was POKED: not comparable")
    file_scenario(r, name, "scratch scenario of `rd port`" if not a.name else "")
    r.close()
    py = [sys.executable, os.path.join(RH.HERE, "rhost.py")]
    rc = subprocess.call(py + ["golden", name, "--refresh"])
    if rc:
        return rc
    return subprocess.call(py + ["triage", name, "--refresh"])


AGENTS = os.environ.get("RD_AGENTS", "/mnt/ephemeral/openrecet/drive/agents")


def cmd_accept(a):
    """the orchestrator's acceptance in one command: check (if a task is given) → verify --real → file as NAME →
    retail golden → port triage → the driver's report archived under docs/findings/probes/NAME.md"""
    import argparse as _ap
    lines = []
    if a.task:
        rc = cmd_check(_ap.Namespace(run=a.run, task=a.task))
        lines.append(f"check {'PASS' if not rc else 'FAIL'} ({os.path.relpath(os.path.abspath(a.task), ROOT)})")
        if rc and not a.force:
            raise SystemExit("check FAILED: not accepted (--force files it anyway — say why in the note)")
    rc = cmd_verify(_ap.Namespace(run=a.run, real=not a.no_real))
    lines.append(f"verify{'' if a.no_real else ' --real'} {'MATCH' if not rc else 'MISMATCH'}")
    if rc:
        raise SystemExit("verify MISMATCH: not accepted")
    r = Run(a.run)
    if r.meta.get("poked"):
        raise SystemExit("this run was POKED: not accepted")
    p = file_scenario(r, a.name, " ".join(a.note))
    frames, start = r.movie.frame, r.meta.get("start", "?")
    r.stop()
    r.close()
    print(f"filed {os.path.relpath(p, ROOT)} ({frames} frames)")
    py = [sys.executable, os.path.join(RH.HERE, "rhost.py")]
    if subprocess.call(py + ["golden", a.name]):
        raise SystemExit("the retail golden failed (Windows box up? RHOST_WIN)")
    lines.append("retail golden made (Windows, real device)")
    if not a.no_triage:
        out = subprocess.run(py + ["triage", a.name, "--refresh"], capture_output=True, text=True).stdout
        v = next((l.strip() for l in out.splitlines() if l.strip().startswith(("identical", "split", "stalled", "error"))), "no verdict")
        print("triage: " + v)
        lines.append("port triage: " + v)
    rp = os.path.join(AGENTS, a.run, "report.md")
    if os.path.exists(rp):
        body = open(rp).read().rstrip()
        m = re.search(r"\n---\nmodel (.*)$", body, re.S)
        meta = m.group(1).strip() if m else ""
        body = body[:m.start()].rstrip() if m else body
        os.makedirs(os.path.join(ROOT, "docs", "findings", "probes"), exist_ok=True)
        dst = os.path.join(ROOT, "docs", "findings", "probes", a.name + ".md")
        open(dst, "w").write(
            f"# Probe: {a.name}\n\n- **Run:** `{a.run}` · **start:** {start}" + (f" · **task:** `{os.path.relpath(os.path.abspath(a.task), ROOT)}`" if a.task else "") +
            f"\n- **Driver:** API harness (`tools/drive/agent.py`), {meta}\n- **Scenario:** `tests/scenarios/{a.name}/` ({frames} frames)\n"
            f"- **Verdicts ({time.strftime('%Y-%m-%d')}):** {'; '.join(lines)}\n\nThe driver's report, as written (retail facts observed live through `rd`; "
            f"claims read from code only are leads):\n\n---\n\n{body}\n")
        print(f"report archived: {os.path.relpath(dst, ROOT)}")
    print("accepted: " + "; ".join(lines))


def cmd_stop(a):
    r = Run(a.run)
    r.stop()
    print(f"stopped the live session of {a.run} (the run and its movie stay; the next command replays)")
    r.close()


def cmd_ls(a):
    if not os.path.isdir(RUNS):
        return
    for n in sorted(os.listdir(RUNS)):
        mp = os.path.join(RUNS, n, "meta.json")
        if not os.path.exists(mp):
            continue
        m = json.load(open(mp))
        sp = os.path.join(RUNS, n, "session.json")
        s = json.load(open(sp)) if os.path.exists(sp) else None
        live = sess_alive(s)
        print(f"{n:28s} frame {m.get('frame', 0):>7d}  from {m.get('start', '?'):28s} {(s.get('host', 'wine') if live else '-'):6s} "
              f"{' '.join(sorted(m.get('checkpoints', {})))}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("new"); p.add_argument("run"); p.add_argument("--from", dest="src", required=True); p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_new)
    p = sp.add_parser("play"); p.add_argument("run"); p.add_argument("spec"); p.add_argument("--until"); p.add_argument("--avoid")
    p.add_argument("--watch", help="EXPR,EXPR..: print these per sampled tick"); p.add_argument("--every", type=int, default=0)
    p.add_argument("--rows", type=int, default=40, help="watch rows shown (the last N)"); p.set_defaults(fn=cmd_play)
    p = sp.add_parser("tap"); p.add_argument("run"); p.add_argument("spec"); p.add_argument("--n", type=int, default=40); p.add_argument("--gap", type=int, default=8)
    p.add_argument("--until"); p.add_argument("--avoid")
    p.add_argument("--text", action="store_true", help="a transcript: print the text on screen before each press (rows new since the last one)")
    p.set_defaults(fn=cmd_tap)
    p = sp.add_parser("status"); p.add_argument("run"); p.set_defaults(fn=cmd_status)
    p = sp.add_parser("text"); p.add_argument("run"); p.set_defaults(fn=cmd_text)
    p = sp.add_parser("endsave"); p.add_argument("run"); p.set_defaults(fn=cmd_endsave)
    p = sp.add_parser("eval"); p.add_argument("run"); p.add_argument("exprs", nargs="+")
    p.add_argument("--at", help="evaluate at this checkpoint's state; the run does not move (no savestate: by replay)")
    p.add_argument("--at-frame", type=int, help="evaluate at frame F of the run's movie (a replay; the run does not move)")
    p.add_argument("--raw", action="store_true", help="full precision values (default: 4 decimals)")
    p.set_defaults(fn=cmd_eval)
    p = sp.add_parser("scan", help="rows of state at earlier frames: the movie replayed in a side session")
    p.add_argument("run"); p.add_argument("--from", dest="frm", type=int, required=True); p.add_argument("--to", type=int)
    p.add_argument("--watch", required=True, help="EXPR,EXPR..: the columns (commas inside brackets do not split)")
    p.add_argument("--every", type=int, default=0, help="every N-th frame from --from")
    p.add_argument("--changes", action="store_true", help="only rows where a watched value changed")
    p.add_argument("--where", help="only rows where EXPR holds")
    p.add_argument("--at-run", help="replay this run's movie instead (read only); names: R's, then RUN's")
    p.add_argument("--fresh", action="store_true", help="always a fresh side session (not the run's checkpoint savestates)")
    p.add_argument("--raw", action="store_true", help="full precision values (default: 4 decimals)")
    p.set_defaults(fn=cmd_scan)
    p = sp.add_parser("ram"); p.add_argument("run"); p.add_argument("op"); p.add_argument("args", nargs="*")
    p.add_argument("--addr"); p.add_argument("--type", default="s32"); p.add_argument("--expr"); p.add_argument("--status", action="store_true")
    p.add_argument("--event", action="store_true"); p.add_argument("--note"); p.add_argument("--shared", action="store_true", help="define in tools/drive/vars.json (all runs)")
    p.add_argument("--max", type=int, default=4000); p.add_argument("--show", type=int, default=60); p.add_argument("--lo"); p.add_argument("--hi")
    p.add_argument("--minus", help="drop the addresses of a saved diff (a noise set)"); p.add_argument("--save", help="save this diff's addresses under a name")
    p.set_defaults(fn=cmd_ram)
    p = sp.add_parser("ck"); p.add_argument("run"); p.add_argument("op", choices=("save", "load", "list")); p.add_argument("name", nargs="?"); p.add_argument("note", nargs="*"); p.set_defaults(fn=cmd_ck)
    p = sp.add_parser("undo"); p.add_argument("run"); p.set_defaults(fn=cmd_undo)
    p = sp.add_parser("rewind"); p.add_argument("run"); p.add_argument("--to", type=int); p.add_argument("--frames", type=int, default=0); p.set_defaults(fn=cmd_rewind)
    p = sp.add_parser("search"); p.add_argument("run"); p.add_argument("--goal", required=True); p.add_argument("--avoid"); p.add_argument("--cell")
    p.add_argument("--actions", nargs="+"); p.add_argument("--time-limit", type=float, default=60); p.add_argument("--refine", type=float, default=6)
    p.add_argument("--apply", action="store_true"); p.add_argument("--save"); p.add_argument("--seed", type=int, default=1); p.add_argument("--max-len", type=int, default=8)
    p.add_argument("--sticky", type=int, default=60); p.add_argument("-j", "--jobs", type=int, default=1, help="parallel sessions, one exploration each")
    p.add_argument("--score", help="EXPR, higher = closer: cells are expanded in proportion to it (direction), and the best one is reported")
    p.add_argument("--greed", type=float, default=3.0, help="--score: the best cell is picked e^GREED times as often as the worst (default 3)")
    p.add_argument("--apply-best", action="store_true", help="--apply, and with --score: goal not reached in time → keep the path to the best-scored cell")
    p.add_argument("--hold", type=int, default=0, help="a goal arrival counts only if some hold action then survives N ticks: --avoid never true, "
                   "the goal still true at the end; the path includes that tail (any fight search: an attack already committed lands after)")
    p.add_argument("--hold-actions", nargs="+", help="the tails tried in order, each cycled to N ticks (default: \".\" = stand still); "
                   "e.g. . R L U D UR UL DR DL")
    p.add_argument("--hold-no-goal", action="store_true", help="--hold: only --avoid must stay false, the goal need not hold at the end")
    p.set_defaults(fn=cmd_search)
    p = sp.add_parser("look"); p.add_argument("run"); p.add_argument("--at"); p.add_argument("--target", choices=("win", "wine")); p.add_argument("--scale", type=float, default=1.0); p.set_defaults(fn=cmd_look)
    p = sp.add_parser("strip"); p.add_argument("run"); p.add_argument("--from", dest="frm", type=int, required=True); p.add_argument("--to", type=int)
    p.add_argument("--every", type=int); p.add_argument("--cols", type=int, default=4); p.add_argument("--target", choices=("win", "wine")); p.set_defaults(fn=cmd_strip)
    p = sp.add_parser("verify"); p.add_argument("run"); p.add_argument("--real", action="store_true", help="also replay on the real device (Windows)"); p.set_defaults(fn=cmd_verify)
    p = sp.add_parser("check"); p.add_argument("run"); p.add_argument("task"); p.set_defaults(fn=cmd_check)
    p = sp.add_parser("lint"); p.add_argument("task"); p.add_argument("--on", nargs="+", required=True); p.set_defaults(fn=cmd_lint)
    p = sp.add_parser("file"); p.add_argument("run"); p.add_argument("name"); p.add_argument("note", nargs="*"); p.set_defaults(fn=cmd_file)
    p = sp.add_parser("port"); p.add_argument("run"); p.add_argument("name", nargs="?"); p.set_defaults(fn=cmd_port)
    p = sp.add_parser("accept", help="check → verify --real → file → golden → triage → archive the report")
    p.add_argument("run"); p.add_argument("name"); p.add_argument("note", nargs="*"); p.add_argument("--task")
    p.add_argument("--force", action="store_true", help="file even though the task check fails")
    p.add_argument("--no-real", action="store_true"); p.add_argument("--no-triage", action="store_true"); p.set_defaults(fn=cmd_accept)
    p = sp.add_parser("stop"); p.add_argument("run"); p.set_defaults(fn=cmd_stop)
    sp.add_parser("ls").set_defaults(fn=cmd_ls)
    # an expression may start with "-" (--score "-exitd"): hand it to argparse as --flag=VALUE
    argv, i = [], 1
    while i < len(sys.argv):
        t = sys.argv[i]
        if t in ("--goal", "--avoid", "--until", "--score", "--cell", "--watch", "--expr", "--where") and i + 1 < len(sys.argv) and sys.argv[i + 1].startswith("-") \
                and not sys.argv[i + 1].startswith("--"):
            argv.append(f"{t}={sys.argv[i + 1]}"); i += 2
        else:
            argv.append(t); i += 1
    a = ap.parse_args(argv)
    try:
        sys.exit(a.fn(a) or 0)
    except SessionEnded:
        rd = os.path.join(RUNS, getattr(a, "run", "") or "", "session.json")
        log, why = "", ""
        if os.path.exists(rd):
            s = json.load(open(rd))
            log = os.path.join(os.path.dirname(rd), ".ended.log")
            if sess_file(s, "rhost.log", log):
                tail = open(log, errors="replace").read()[-1500:]
                os.remove(log)
                why = " | ".join(l[:160] for l in tail.splitlines() if l.startswith(("game:", "exception", "FATAL", "RESULT")))
            log = f"{s.get('rundir')}\\rhost.log ({s.get('host', 'wine')})" if s.get("host") in ("win", "bunker") else os.path.join(s.get("rundir", ""), "rhost.log")
        print(f"the live session ended during this command: the game exited (EXIT on the title, a quit) or crashed.\n  {why or log}\n"
              "  The run is unchanged (its movie ends before this command); the next rd command replays it.", file=sys.stderr)
        sys.exit(3)


if __name__ == "__main__":
    main()
