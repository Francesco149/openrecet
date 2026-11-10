#!/usr/bin/env python3
"""rd — drive retail Recettear live, at savestate speed, and turn what you did into a verified trace.

One command per call; the run directory is the state. Behind a run sits a LIVE rhost process
(retail on the headless devices, `rhost --live`): it steps ticks on demand, reads memory, takes
in-process savestates and runs the exploration search. That process is only a cache — a run is
its MOVIE (every input since boot); a dead session is rebuilt by replaying it.

    rd new R --from fresh | SCENARIO[@F] | RUN[:CK|@F] | end:SCENARIO     start a run
    rd play R "A . A*3 .*30 ~ R*40"  [--until EXPR] [--avoid EXPR] [--watch EXPR,..] [--every N]
    rd tap R "A" [--n 40] [--gap 8] [--until EXPR]                        press, release, repeat
    rd status R | rd text R | rd eval R EXPR.. | rd ram R list | read VA [N] | dwords VA [N] | str VA | define NAME .. | mark | diff
    rd ck R save NAME | load NAME | list        rd undo R        rd rewind R --to F
    rd search R --goal EXPR [--avoid EXPR] [--cell TUPLE] [--actions SPEC ..] [--apply] [-j N]
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

RUNS = os.environ.get("RD_RUNS", "/mnt/ephemeral/openrecet/drive/runs")
SESS = os.path.join(RH.WINE_ROOT, "runs", "drive")          # live sessions' rhost run dirs (C:\orc-rhost\runs\drive\..)
VARS = os.path.join(HERE, "vars.json")
# retail render functions whose calls are no-ops in live sessions (rhost --skip-fn): only ones that passed
# `rhost.py skip-check` (same frame / RNG / game-state hash / save.dat with and without, whole corpus).
# FUN_00414ee2 = the overlay particle dispatcher, ~48% of a 3D-scene tick. RD_SKIP_FN="" turns it off.
SKIP_FN = os.environ.get("RD_SKIP_FN", "")
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
        elif spec[i] == "(":
            j = spec.index(")", i)
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


def spec_to_run(items):
    """[(mask, n, esc)] → (segments "m:n,m:n", [esc ticks], total)"""
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
    return ",".join(f"{m:x}:{n}" for m, n in segs), escs, t


def segs_to_spec(path):
    """server path "10:2,0:8" → "A*2 .*8\""""
    out = []
    for s in filter(None, path.split(",")):
        m, n = s.split(":")
        out.append(mask_name(int(m, 16)) + (f"*{n}" if n != "1" else ""))
    return " ".join(out)


# ------------------------------------------------------------------ expressions → postfix programs
TYPES = {"u8": "r8", "s8": "s8", "u16": "r16", "s16": "s16", "u32": "r32", "s32": "s32", "i32": "s32", "f32": "f32", "f64": "f64"}
BINOP = {ast.Add: "+", ast.Sub: "-", ast.Mult: "*", ast.Div: "/", ast.FloorDiv: "//", ast.Mod: "%", ast.BitAnd: "&",
         ast.BitOr: "|", ast.BitXor: "^", ast.LShift: "<<", ast.RShift: ">>"}
CMPOP = {ast.Lt: "<", ast.LtE: "<=", ast.Gt: ">", ast.GtE: ">=", ast.Eq: "==", ast.NotEq: "!="}


class Vars:
    """named state: tools/drive/vars.json (shared, committed) + <run>/vars.json (the run's own)"""

    def __init__(self, run_dir=None):
        self.defs, self.local_path = {}, run_dir and os.path.join(run_dir, "vars.json")
        for p in (VARS, self.local_path):
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
            if n.id in ("frame", "rng"):
                return [n.id]
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
            if f == "abs" and len(n.args) == 1:
                return c(n.args[0]) + ["abs"]
            if f == "int" and len(n.args) == 1:
                return c(n.args[0]) + ["1", "//"]
            raise SystemExit(f"unknown function {f}() in {src!r} (u8 s8 u16 s16 u32 s32 f32 f64 min max abs int)")
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


def wine_env():
    """live sessions run without an X display, in the headless prefix (rhost.py wine_prefix: why it is separate)"""
    return {k: v for k, v in dict(os.environ, WINEPREFIX=RH.wine_prefix(True), WINEDEBUG="-all").items()
            if k not in ("DISPLAY", "WAYLAND_DISPLAY")}


def staged_exe():
    """build if stale, stage the build content-addressed under the wine root → its Windows path"""
    if RH.rhost_stale():
        RH.cmd_build(None)
    RH.wine_stage()
    return RH.bin_dir() + "\\rhost.exe"


def spawn_live(tag, movie, upto=None, idle=1800):
    """a live rhost under wine (no X): replays MOVIE to frame UPTO first → (pid, port, rundir)"""
    exe = staged_exe()
    os.makedirs(SESS, exist_ok=True)
    for n in range(100000):
        rd = os.path.join(SESS, f"{tag}-{os.getpid()}-{n}")
        try:
            os.makedirs(rd)
            break
        except FileExistsError:
            continue
    shutil.copy(os.path.join(RH.HERE, "recet.ini"), rd)
    if os.path.exists(RH.VOLATILE):
        shutil.copy(RH.VOLATILE, rd)
    if movie.save:
        open(os.path.join(rd, "save.dat"), "wb").write(RH.save_bytes(movie.save))
    win = "C:\\orc-rhost\\runs\\drive\\" + os.path.basename(rd)
    args = ["wine", exe, "--exe", f"{RH.WROOT}\\game\\recettear.unpacked.exe", "--game", f"{RH.WROOT}\\game", "--run", win,
            "--live", "0", "--live-idle", str(idle)] + (["--skip-fn", SKIP_FN] if SKIP_FN else [])
    upto = movie.frame if upto is None else upto
    seed = next((v for f, k, v in movie.ops if k == "r" and f == 0), None)
    if seed is not None:
        args += ["--seed", str(seed)]
    if upto > 0:
        movie.truncated(upto).write(os.path.join(rd, "trace.jsonl"))
        args += ["--trace", win + "\\trace.jsonl", "--frames", str(upto)]
    p = subprocess.Popen(args, stdout=open(os.path.join(rd, "stdout.txt"), "w"), stderr=subprocess.DEVNULL, env=wine_env(),
                         start_new_session=True, cwd=rd)
    pf = os.path.join(rd, "live.port")
    t0 = time.time()
    while not os.path.exists(pf):
        if p.poll() is not None or time.time() - t0 > 120:
            tail = open(os.path.join(rd, "rhost.log"), errors="replace").read()[-600:] if os.path.exists(os.path.join(rd, "rhost.log")) else ""
            raise SystemExit(f"the live session did not start ({rd})\n{tail}")
        time.sleep(0.05)
    return p.pid, int(open(pf).read().split()[0]), rd


def kill_session(pid, rundir=None):
    """end OUR process group (never by name); drop its run dir"""
    try:
        os.killpg(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    if rundir and os.path.realpath(rundir).startswith(os.path.realpath(SESS) + os.sep):
        shutil.rmtree(rundir, ignore_errors=True)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except (ProcessLookupError, PermissionError):
        return False


# ------------------------------------------------------------------ a run
class Run:
    def __init__(self, name, create=False):
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", name):
            raise SystemExit(f"bad run name {name!r}")
        self.name, self.dir = name, os.path.join(RUNS, name)
        if create:
            os.makedirs(self.dir, exist_ok=True)
        elif not os.path.exists(os.path.join(self.dir, "meta.json")):
            raise SystemExit(f"no run {name!r} (rd ls; rd new {name} --from ..)")
        self.lock = open(os.path.join(self.dir, ".lock"), "w")
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
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
    def attach(self):
        """a live session at the movie's head: the cached one if it is still there and in step, else a fresh replay"""
        if self.live:
            return self.live
        s = json.load(open(self.sess_p)) if os.path.exists(self.sess_p) else None
        if s and pid_alive(s["pid"]) and s.get("bin") == RH.file_sha12(os.path.join(RH.BUILD, "rhost.exe"), os.path.join(RH.BUILD, "v3proxy_d3d8.dll")):
            try:
                lv = Live(s["port"])
                if int(Live.kv(lv.cmd("ping"))["frame"]) == self.movie.frame:
                    self.live, self.sess = lv, s
                    return lv
                lv.close()
            except OSError:
                pass
        if s:
            kill_session(s["pid"], s.get("rundir"))
        t0 = time.time()
        pid, port, rd = spawn_live(self.name, self.movie)
        self.live = Live(port)
        k = Live.kv(self.live.cmd("ping"))
        if int(k["frame"]) != self.movie.frame:
            raise SystemExit(f"the replayed session is at frame {k['frame']}, the movie at {self.movie.frame} (see {rd}/rhost.log)")
        self.sess = {"pid": pid, "port": port, "rundir": rd, "epoch": int(k["epoch"]), "snaps": {}, "undo": [], "mark": None,
                     "bin": RH.file_sha12(os.path.join(RH.BUILD, "rhost.exe"), os.path.join(RH.BUILD, "v3proxy_d3d8.dll"))}
        self.save_sess()
        if self.movie.frame > 2000:
            print(f"[session rebuilt by replaying {self.movie.frame} frames in {time.time() - t0:.1f}s]")
        return self.live

    def stop(self):
        s = json.load(open(self.sess_p)) if os.path.exists(self.sess_p) else None
        if s:
            kill_session(s["pid"], s.get("rundir"))
            for w in s.get("workers", []):
                kill_session(w["pid"], w.get("rundir"))
            os.remove(self.sess_p)

    def epoch_check(self, r):
        """an anchor on the server (explicit, or a search's) invalidated every savestate id we hold"""
        e = Live.kv(r).get("epoch")
        if e and int(e) != self.sess["epoch"]:
            self.sess.update(epoch=int(e), snaps={}, undo=[], mark=None)

    def snap(self):
        r = self.live.cmd("snap")
        self.epoch_check(r)
        return int(Live.kv(r)["id"])

    def push_undo(self):
        """a savestate before every state-changing command: `rd undo` is instant"""
        sid = self.snap()
        self.sess["undo"].append([sid, self.movie.frame])
        while len(self.sess["undo"]) > 6:
            old = self.sess["undo"].pop(0)
            keep = {v[0] for v in self.sess["snaps"].values()} | {self.sess.get("mark")}
            if old[0] not in keep:
                self.live.cmd(f"free {old[0]}")

    # ---- reading state
    def ev(self, *exprs):
        lv = self.attach()
        ids = [lv.prog(self.vars.compile(e)) for e in exprs]
        r = lv.cmd("eval " + ",".join(map(str, ids)))
        vals = [float(x) for x in Live.kv(r)["v"].split(":")]
        return [int(v) if v == int(v) and abs(v) < 1e15 else v for v in vals]

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
    def play(self, spec, until=None, avoid=None, watch=(), every=0, quiet=False, rows=40):
        lv = self.attach()
        segs, escs, total = spec_to_run(parse_spec(spec))
        if not total:
            raise SystemExit("empty input spec")
        self.push_undo()
        cmd = f"run {segs}"
        if escs:
            cmd += " esc=" + ",".join(map(str, escs))
        if until:
            cmd += f" u={lv.prog(self.vars.compile(until))}"
        if avoid:
            cmd += f" a={lv.prog(self.vars.compile(avoid))}"
        ev_names = [n for n, d in self.vars.defs.items() if d.get("event")]
        cols = list(watch) + ev_names
        if cols:
            cmd += " s=" + ",".join(str(lv.prog(self.vars.compile(e))) for e in cols)
            cmd += f" e={every if (every and watch) else 1}"
        r = lv.cmd(cmd)
        k = Live.kv(r)
        self.sync_movie()
        self.save_sess()
        out = {"ran": int(k["ran"]), "stop": k["stop"], "frame": int(k["frame"]), "rows": [], "events": []}
        if cols and k.get("samples"):
            prev = None
            for row in k["samples"].split(";"):
                p = row.split(":")
                f, vals = int(p[0]), [float(x) for x in p[1:]]
                if watch and (not every or (f - (out["frame"] - out["ran"])) % every == 0):
                    out["rows"].append((f, vals[:len(watch)]))
                evs = vals[len(watch):]
                if prev is not None:
                    for n, a, b in zip(ev_names, prev, evs):
                        if a != b:
                            out["events"].append((f, n, a, b))
                prev = evs
        if not quiet:
            print(f"ran {out['ran']} ticks ({out['stop']})  " + self.status_line())
            for f, n, a, b in out["events"][:60]:
                print(f"  f{f}: {n} {fmt(a)} → {fmt(b)}")
            if len(out["events"]) > 60:
                print(f"  … {len(out['events']) - 60} more events")
            if watch:
                print("  frame  " + "  ".join(watch))
                if len(out["rows"]) > rows:
                    print(f"  … {len(out['rows']) - rows} earlier rows not shown (--rows N)")
                for f, vals in out["rows"][-rows:]:
                    print(f"  {f:6d} " + "  ".join(fmt(v) for v in vals))
        return out

    def load_frame(self, frame, sid=None):
        """go to FRAME of this movie: by savestate when one is held, else by a fresh replay of the prefix"""
        if sid is not None and self.live:
            r = self.live.cmd(f"restore {sid}", check=False)
            if r.startswith("ok") and int(Live.kv(r)["frame"]) == frame:
                self.sync_movie()
                return "savestate"
        self.stop()
        self.live = None
        self.movie = self.movie.truncated(frame)
        self.movie.write(self.movie_p, comment=self.name, rel_to=self.dir)
        self.save_meta()
        self.attach()
        return "replay"

    def close(self):
        if self.live:
            self.live.close()
        self.lock.close()


def fmt(v):
    if isinstance(v, float) and v != int(v):
        return f"{v:.4f}".rstrip("0").rstrip(".")
    return str(int(v))


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
        b = RH.replay_args(target="wine")
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
    sp = os.path.join(r.sess["rundir"], "save.dat")
    if not os.path.exists(sp) or (r.movie.save and open(sp, "rb").read() == RH.save_bytes(r.movie.save)):
        raise SystemExit("this run has not committed a save of its own (pause menu → Save, or a day-end save)")
    sha, blob = RH.trace_save.store_save(sp, SCEN_SAVES)
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
        exe = staged_exe()
        t0 = time.time()
        os.makedirs(SESS, exist_ok=True)
        srd = os.path.join(SESS, f"{a.run}-{os.getpid()}-new")
        shutil.rmtree(srd, ignore_errors=True); os.makedirs(srd)
        shutil.copy(os.path.join(RH.HERE, "recet.ini"), srd)
        shutil.copy(trace, os.path.join(srd, "trace.jsonl"))
        if os.path.exists(RH.VOLATILE):
            shutil.copy(RH.VOLATILE, srd)
        if mv.save:
            open(os.path.join(srd, "save.dat"), "wb").write(RH.save_bytes(mv.save))
        win = "C:\\orc-rhost\\runs\\drive\\" + os.path.basename(srd)
        args = ["wine", exe, "--exe", f"{RH.WROOT}\\game\\recettear.unpacked.exe", "--game", f"{RH.WROOT}\\game", "--run", win,
                "--live", "0", "--trace", win + "\\trace.jsonl"] + (["--frames", str(limit)] if limit else []) + \
               (["--skip-fn", SKIP_FN] if SKIP_FN else [])
        p = subprocess.Popen(args, stdout=open(os.path.join(srd, "stdout.txt"), "w"), stderr=subprocess.DEVNULL, env=wine_env(),
                             start_new_session=True, cwd=srd)
        pf = os.path.join(srd, "live.port")
        while not os.path.exists(pf):
            if p.poll() is not None:
                raise SystemExit(f"the live session did not start ({srd})")
            time.sleep(0.05)
        r.live = Live(int(open(pf).read().split()[0]))
        k = Live.kv(r.live.cmd("ping"))
        r.sess = {"pid": p.pid, "port": int(open(pf).read().split()[0]), "rundir": srd, "epoch": int(k["epoch"]), "snaps": {}, "undo": [],
                  "mark": None, "bin": RH.file_sha12(os.path.join(RH.BUILD, "rhost.exe"), os.path.join(RH.BUILD, "v3proxy_d3d8.dll"))}
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
    r.play(a.spec, until=a.until, avoid=a.avoid, watch=[w for w in (a.watch or "").split(",") if w.strip()], every=a.every, rows=a.rows)
    r.close()


def cmd_tap(a):
    r = Run(a.run)
    spec = f"({a.spec} .*{a.gap})x{a.n}"
    r.play(spec, until=a.until, avoid=a.avoid)
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
    vals = r.ev(*a.exprs)
    for e, v in zip(a.exprs, vals):
        print(f"{e} = {fmt(v)}" + (f"  (0x{int(v) & 0xffffffff:x})" if isinstance(v, int) and abs(v) > 9 else ""))
    r.close()


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
        r.attach()
        if fr > r.movie.frame or any(c["frame"] > fr for c in cks.values()):
            pass
        how = r.load_frame(fr, (r.sess["snaps"].get(a.name) or [None])[0])
        for n in [n for n, c in cks.items() if c["frame"] > fr]:
            del cks[n]                                 # checkpoints of the abandoned future
        r.save_meta(); r.save_sess()
        print(f"loaded {a.name} ({how})  " + r.status_line())
    r.close()


def cmd_undo(a):
    r = Run(a.run)
    r.attach()
    if not r.sess["undo"]:
        raise SystemExit("nothing to undo in this session (rd rewind R --to F replays a prefix)")
    sid, fr = r.sess["undo"].pop()
    how = r.load_frame(fr, sid)
    cks = r.meta.get("checkpoints", {})
    for n in [n for n, c in cks.items() if c["frame"] > fr]:
        del cks[n]
    r.save_meta(); r.save_sess()
    print(f"undone ({how})  " + r.status_line())
    r.close()


def cmd_rewind(a):
    r = Run(a.run)
    fr = a.to if a.to is not None else r.movie.frame - a.frames
    if fr < 0 or fr > r.movie.frame:
        raise SystemExit(f"frame {fr} is outside the movie (0..{r.movie.frame})")
    r.load_frame(fr)
    cks = r.meta.get("checkpoints", {})
    for n in [n for n, c in cks.items() if c["frame"] > fr]:
        del cks[n]
    r.save_meta()
    print("rewound  " + r.status_line())
    r.close()


# ------------------------------------------------------------------ search
DEFAULT_ACTIONS = ["R*8", "L*8", "U*8", "D*8", "UR*8", "UL*8", "DR*8", "DL*8", "R*2", "L*2", "U*2", "D*2", "A .*7", "B .*7", ".*8"]


def action_arg(specs):
    acts = []
    for s in specs:
        seg, escs, total = spec_to_run(parse_spec(s))
        if escs:
            raise SystemExit(f"action {s!r}: ESC (~) cannot be part of a search action — play it yourself")
        if not total:
            raise SystemExit(f"empty action {s!r}")
        acts.append(seg.replace(",", "+"))
    return "|".join(acts)


def explore_cmd(lv, V, a, seed, apply):
    g = lv.prog(V.compile(a.goal))
    cells = [lv.prog(t) for t in V.compile_tuple(a.cell)]
    cmd = f"explore g={g} c={','.join(map(str, cells))} acts={action_arg(a.actions or DEFAULT_ACTIONS)}"
    if a.avoid:
        cmd += f" a={lv.prog(V.compile(a.avoid))}"
    cmd += f" t={int(a.time_limit * 1000)} refine={int(a.refine * 1000)} seed={seed} maxlen={a.max_len} sticky={a.sticky} apply={1 if apply else 0}"
    return cmd


def cmd_search(a):
    r = Run(a.run)
    lv = r.attach()
    if not a.cell:
        a.cell = r.vars.defs.get("_cell", {}).get("expr") or "(scene, int(px), int(pz))"
    if r.ev(a.goal)[0]:
        print("the goal already holds here — nothing to search")
        r.close()
        return
    r.push_undo()
    t0 = time.time()
    results = []
    if a.jobs <= 1:
        res = lv.cmd(explore_cmd(lv, r.vars, a, a.seed, a.apply), check=False)
        if not res.startswith("ok"):
            raise SystemExit("search: " + res)
        r.epoch_check(res)
        results.append((Live.kv(res), "main"))
    else:
        # extra live sessions at the same frame (each replays the movie), one exploration per session with its own seed
        workers, errs = [], []

        def boot(i):
            try:
                pid, port, rd = spawn_live(f"{r.name}-w{i}", r.movie, idle=600)
                workers.append((i, pid, port, rd))
            except BaseException as e:      # noqa: B902 — SystemExit from spawn_live
                errs.append(str(e))
        ts = [threading.Thread(target=boot, args=(i,)) for i in range(1, a.jobs)]
        [t.start() for t in ts]

        def one(lvx, seed, apply, tag):
            res = lvx.cmd(explore_cmd(lvx, r.vars, a, seed, apply), check=False)
            results.append((Live.kv(res) if res.startswith("ok") else {"found": "0", "err": res}, tag))
        try:
            [t.join() for t in ts]
            if errs:
                print(f"[{len(errs)} worker(s) did not start: {errs[0][:120]}]")
            print(f"[{len(workers) + 1} sessions at frame {r.movie.frame}, {time.time() - t0:.1f}s to start the extra ones]")
            ths = [threading.Thread(target=one, args=(lv, a.seed, False, "main"))]
            lvs = []
            for i, pid, port, rd in workers:
                lx = Live(port); lvs.append(lx)
                ths.append(threading.Thread(target=one, args=(lx, a.seed + i * 7919, False, f"w{i}")))
            [t.start() for t in ths]
            [t.join() for t in ths]
            [lx.close() for lx in lvs]
        finally:
            for i, pid, port, rd in workers:
                kill_session(pid, rd)
        r.sess.update(snaps={}, undo=[], mark=None)   # the main session's explore re-anchored
        r.sess["epoch"] = int(Live.kv(lv.cmd("ping"))["epoch"])
    found = [(int(k["frames"]), k, tag) for k, tag in results if k.get("found") == "1"]
    ticks = sum(int(k.get("ticks", 0)) for k, _ in results)
    cells = max(int(k.get("cells", 0)) for k, _ in results)
    wall = time.time() - t0
    if not found:
        k = results[0][0]
        print(f"found: false   {cells} cells, {ticks} ticks searched in {wall:.1f}s ({ticks / max(wall, 0.01):.0f} ticks/s)")
        print(f"  newest cells (frames@{a.cell}): " + "  ".join(k.get("newest", "").split(";")))
        print("  one cell only = your actions change nothing the cell sees; split the goal at where it got to, add the missing action, "
              "or put the mechanic's state in --cell")
        r.sync_movie(); r.save_sess(); r.close()
        return 1
    frames, k, tag = min(found, key=lambda x: x[0])
    spec = segs_to_spec(k["path"])
    print(f"found: true   {frames} frames  ({cells} cells, {ticks} ticks in {wall:.1f}s = {ticks / max(wall, 0.01):.0f} ticks/s"
          + (f", best of {len(found)}/{len(results)} sessions [{tag}]" if len(results) > 1 else "") + ")")
    print(f"  path: {spec if len(spec) < 900 else spec[:900] + ' …'}")
    if a.save:
        open(a.save, "w").write(spec + "\n")
    if a.apply:
        if a.jobs > 1 or k.get("applied") != "1":
            lv.cmd("run " + k["path"])
        holds = r.ev(a.goal)[0]
        r.sync_movie()
        print(f"  applied: goal {'holds' if holds else 'DOES NOT HOLD'}  " + r.status_line())
    else:
        r.sync_movie()
        print("  not applied (--apply plays it; --save FILE keeps it: rd play R \"$(cat FILE)\")")
    r.save_sess()
    r.close()
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


def win_up():
    try:
        return subprocess.run(["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=4", RH.WIN, "echo ok"], capture_output=True,
                              text=True, timeout=12).stdout.strip().endswith("ok")
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
    b = RH.replay_args(frames=r.movie.frame, target="wine")
    b.jobs, b.wine_timeout, b.headless, b.no_golden = 1, 3600, True, True
    b.extra = f"--skip-fn {SKIP_FN}" if SKIP_FN else None          # the same device-side behaviour as the live session
    _, bd, res, out = RH.run_batch(b, [("verify", tr, None, r.movie.save)], "rd-verify", fetch=("rhost.log",))
    v = res.get("verify") or {}
    shutil.rmtree(bd, ignore_errors=True)
    same = all(v.get(x) == live.get(y) for x, y in (("frame", "frame"), ("rng", "rng"), ("state_sem", "sem"), ("state_dev", "dev")))
    print(f"fresh headless replay: frame {v.get('frame')} rng {v.get('rng')} sem {v.get('state_sem')} dev {v.get('state_dev')}")
    print(f"live session:          frame {live['frame']} rng {live['rng']} sem {live['sem']} dev {live['dev']}")
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
        s = re.match(r"\s+-\s*(.+?):\s+(.+?)\s*(?:#.*)?$", line)
        if in_list and s:
            t[in_list].append((s.group(1).strip(), s.group(2).strip()))
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
    pid, port, rd = spawn_live("check", movie, upto=0, idle=300)
    try:
        lv = Live(port)
        ids = [lv.prog(V.compile(e)) for _, e in preds]
        truth = [[] for _ in preds]
        at = 0
        while at < movie.frame:
            hi = min(movie.frame, at + 4000)
            k = Live.kv(lv.cmd(f"run {movie.run_args(at, hi)} s={','.join(map(str, ids))} e=1"))
            for row in filter(None, k.get("samples", "").split(";")):
                p = row.split(":")
                f = int(p[0])
                for i, x in enumerate(p[1:]):
                    if float(x) != 0:
                        truth[i].append(f)
            at = hi
        end = Live.kv(lv.cmd("hash"))
        lv.close()
        return truth, end
    finally:
        kill_session(pid, rd)


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
        live = os.path.exists(sp) and pid_alive(json.load(open(sp))["pid"])
        print(f"{n:28s} frame {m.get('frame', 0):>7d}  from {m.get('start', '?'):28s} {'live' if live else '-':5s} "
              f"{' '.join(sorted(m.get('checkpoints', {})))}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("new"); p.add_argument("run"); p.add_argument("--from", dest="src", required=True); p.add_argument("--force", action="store_true"); p.set_defaults(fn=cmd_new)
    p = sp.add_parser("play"); p.add_argument("run"); p.add_argument("spec"); p.add_argument("--until"); p.add_argument("--avoid")
    p.add_argument("--watch", help="EXPR,EXPR..: print these per sampled tick"); p.add_argument("--every", type=int, default=0)
    p.add_argument("--rows", type=int, default=40, help="watch rows shown (the last N)"); p.set_defaults(fn=cmd_play)
    p = sp.add_parser("tap"); p.add_argument("run"); p.add_argument("spec"); p.add_argument("--n", type=int, default=40); p.add_argument("--gap", type=int, default=8)
    p.add_argument("--until"); p.add_argument("--avoid"); p.set_defaults(fn=cmd_tap)
    p = sp.add_parser("status"); p.add_argument("run"); p.set_defaults(fn=cmd_status)
    p = sp.add_parser("text"); p.add_argument("run"); p.set_defaults(fn=cmd_text)
    p = sp.add_parser("endsave"); p.add_argument("run"); p.set_defaults(fn=cmd_endsave)
    p = sp.add_parser("eval"); p.add_argument("run"); p.add_argument("exprs", nargs="+"); p.set_defaults(fn=cmd_eval)
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
    a = ap.parse_args()
    try:
        sys.exit(a.fn(a) or 0)
    except SessionEnded:
        rd = os.path.join(RUNS, getattr(a, "run", "") or "", "session.json")
        log, why = "", ""
        if os.path.exists(rd):
            log = os.path.join(json.load(open(rd)).get("rundir", ""), "rhost.log")
            if os.path.exists(log):
                tail = open(log, errors="replace").read()[-1500:]
                why = " | ".join(l[:160] for l in tail.splitlines() if l.startswith(("game:", "exception", "FATAL", "RESULT")))
        print(f"the live session ended during this command: the game exited (EXIT on the title, a quit) or crashed.\n  {why or log}\n"
              "  The run is unchanged (its movie ends before this command); the next rd command replays it.", file=sys.stderr)
        sys.exit(3)


if __name__ == "__main__":
    main()
