#!/usr/bin/env python3
"""Live-probe daemon for retail Recettear — the persistent interactive session
behind the openrecet MCP server (tools/openrecet_mcp.py).

Spawns + attaches to retail ONCE and keeps the frida session alive, so game
state persists across many commands (unlike frida_capture, which spawns/reaps
per run).  Drives the live game via the agent's live-probe RPC surface:
synthetic button input (the same var_input_mask write-path a real DInput poll
takes → identical engine code path), on-demand screenshots, typed memory
reads/pokes, and engine-thread function calls.  Optionally bootstraps to a
known state with an input_segtrace first (e.g. new-game → HOUSE), then hands
control to the probe.

    # boot to the HOUSE free-roam, seed-pinned, preview window visible:
    nix develop --command python3 tools/probe_daemon.py \
        --segtrace tests/scenarios/<scen>/trace.jsonl --rng-seed 19937 --view &
    # then drive it via tools/probe.py or the MCP server.

Commands are line-delimited JSON over 127.0.0.1:<port>; see tools/probe.py.
Turbo defaults ON (fast-forward); pass --realtime to watch at 1×.  The preview
window (--view) is shown WITHOUT stealing focus (SW_SHOWNOACTIVATE) and human
input is LOCKED by default (probe owns the mask); toggle with the `input`
command / MCP set_interactive.
"""
from __future__ import annotations

import argparse
import json
import socket
import struct
import threading
import time
from pathlib import Path

# Reuse frida_capture's bootstrap (frida-server autostart, path xlate, consts).
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import frida  # noqa: E402
import frida_capture as fc  # noqa: E402

ROOT = fc.ROOT
CONTROL_JSON = ROOT / "runs" / "probe" / "daemon.json"   # client auto-discovery

# ─── Recettear input button masks (from src/input.c input_binding_mask) ──────
# The engine reads these OR'd bits from DAT_073dddd0 (var_input_mask).
BTN = {
    "up": 0x0004, "right": 0x0001, "down": 0x0008, "left": 0x0002,
    "a": 0x0010,   # confirm / pick up / talk (the "Z" face button)
    "b": 0x0020,   # cancel / back            ("X")
    "c": 0x0040,   # ("C")
    "d": 0x0080,   # ("V")
    "e": 0x0100,
    "s0": 0x0200, "s1": 0x0400, "s2": 0x0800, "s3": 0x1000, "s4": 0x2000,
}

# ─── curated state VAs (Ghidra preferred ImageBase 0x00400000) ───────────────
# Sources: tools/frida/openrecet-agent.js ADDR table, src/save_bank.h,
# src/scene1_tutorial_dispatch.c, docs/FRONT.md anchor list.
WORKING_BANK = 0x044E3798            # working save-arena base (word 0)
STATE_SPECS = [
    ("scene",     0x0438B1C0, "i32"),  # 0=TITLE 1=INGAME 8=LOADING
    ("rng",       0x006023A0, "u32"),  # LCG state
    ("cc08",      0x0438CC08, "i32"),  # 1=free-roam 4=customer-service
    ("dlg",       0x0438B1C8, "i32"),  # dialogue active (1=running 2=loading)
    ("pause",     0x0438B150, "i32"),  # pause menu open flag
    ("nowload",   0x06A49958, "i32"),  # scene-load gate
    ("nowload2",  0x06A49960, "i32"),  # dialogue-load gate
    ("worker",    0x06A49954, "i32"),  # primary worker busy
    ("player_st", 0x056DAAFC, "i32"),  # player actor state (6=conv pose)
    ("player_fr", 0x056DAAF8, "i32"),  # player anim frame
    ("px",        0x056DA1D8, "f32"),  # player world x
    ("py",        0x056DA1DC, "f32"),  # player world y
    ("pz",        0x056DA1E0, "f32"),  # player world z
    ("gold",      WORKING_BANK + 3 * 4, "i32"),         # save[3]
    ("day",       0x0450FB84, "i32"),                   # save[0x2c3ec] (+1 on HUD)
    ("shoptime",  0x0450FB88, "i32"),                   # save[0x2c3f0]
]


class ProbeDaemon:
    def __init__(self, args):
        self.args = args
        self.run_dir = ROOT / "runs" / "probe" / (args.run_id or "session")
        (self.run_dir / "shots").mkdir(parents=True, exist_ok=True)
        self.log = (self.run_dir / "daemon.log").open("w", buffering=1)
        self.device = None
        self.pid = None
        self.session = None
        self.script = None
        self._stop = threading.Event()
        self._frame_lock = threading.Lock()
        self._frame = None                # (idx, w, h, bgra_bytes, vals)
        self._frame_evt = threading.Event()
        self._rec = None                  # active recording
        self._shot_seq = 0
        self._call_results = {}
        self._call_evt = threading.Event()
        self._anchors = []                # accumulated anchor firings
        self._anchor_lock = threading.Lock()

    def _logline(self, s: str):
        self.log.write(s + "\n")

    # ── frida message pump ─────────────────────────────────────────────────
    def _on_message(self, message, data):
        if message.get("type") == "error":
            self._logline(f"[frida-error] {message.get('description','')}")
            return
        if message.get("type") != "send":
            return
        p = message.get("payload") or {}
        kind = p.get("kind")
        if kind == "frame" and data is not None:
            # BGRA top-down, w*h*4 bytes. Store raw; convert lazily on save.
            with self._frame_lock:
                self._frame = (p.get("frame"), p.get("w"), p.get("h"),
                               bytes(data), p.get("vals"))
            self._frame_evt.set()
            rec = self._rec
            if rec is not None and rec.get("proc") is not None:
                self._rec_write(rec, p.get("w"), p.get("h"), bytes(data))
            return
        if kind == "call_result":
            self._call_results[p.get("id")] = p
            self._call_evt.set()
            return
        if kind == "anchor":
            with self._anchor_lock:
                self._anchors.append(p)
            self._logline(f"[anchor] {json.dumps(p)}")
            return
        if kind in ("log", "error"):
            self._logline(f"[agent] {p.get('msg') or p.get('where','')}: "
                          f"{p.get('msg','')}")
            return
        if kind in ("input_state", "max_frames_reached", "segtrace_done"):
            self._logline(f"[{kind}] {json.dumps(p)}")

    # ── lifecycle ──────────────────────────────────────────────────────────
    def start(self):
        a = self.args
        if not fc.ensure_frida_server(a.remote, fc.DEFAULT_FRIDA_SERVER_EXE):
            raise SystemExit(f"frida-server unreachable at {a.remote}")
        exe = Path(a.exe).resolve()
        cwd = Path(a.cwd).resolve()
        argv = [fc.wslpath_w(exe)]
        win_cwd = fc.wslpath_w(cwd)
        self.device = frida.get_device_manager().add_remote_device(a.remote)
        # Reap any stray retail (a prior daemon that died without device.kill)
        # so the singleton mutex doesn't block our spawn.
        try:
            for pr in self.device.enumerate_processes():
                if pr.name.lower() == exe.name.lower():
                    self._logline(f"[reap] killing stray {pr.name} pid={pr.pid}")
                    try:
                        self.device.kill(pr.pid)
                    except Exception:
                        pass
            time.sleep(0.5)
        except Exception as e:
            self._logline(f"[reap] enumerate failed: {e!r}")

        seg_ops = []
        if a.segtrace:
            seg_ops = self._load_segtrace(Path(a.segtrace).resolve())

        # Save sandbox: NEVER touch the user's real save. Seed from the real
        # save so in-game Continue/Load sees it, redirect writes to the sandbox.
        import shutil
        sandbox = self.run_dir / "saveout"
        sandbox.mkdir(parents=True, exist_ok=True)
        if not a.no_seed_save:
            for nm in ("save.dat", "_save.dat"):
                real = cwd / nm
                if real.exists():
                    shutil.copyfile(real, sandbox / nm)
        save_sandbox_win = fc.wslpath_w(sandbox)

        self._logline(f"[spawn] {argv} cwd={win_cwd}")
        self.pid = self.device.spawn(argv, cwd=win_cwd)
        self.session = self.device.attach(self.pid)
        self.script = self.session.create_script(fc.AGENT_JS.read_text())
        self.script.on("message", self._on_message)
        self.script.load()

        init_cfg = {
            "module": exe.name,
            "probe_mode": True,
            # Bootstrapping via a segtrace? Stay inactive until it finishes,
            # then the daemon flips probe_active on. No segtrace → active now.
            "probe_active": not bool(seg_ops),
            "force_active": True,          # keep the tick gate armed
            # Window: hidden, or shown WITHOUT focus theft (preview seat).
            "hide_window": not a.view,
            "show_window_noactivate": bool(a.view),
            "silent_audio": not a.audio,
            "turbo": not a.realtime,
            "turbo_step_ms": int(a.turbo_step_ms),
            "force_resolution": [640, 480],
            "anchor_trace": True,
            "save_sandbox": save_sandbox_win,
            "input_segtrace": seg_ops,
            "rng_seed": (int(a.rng_seed) & 0xffffffff
                         if a.rng_seed is not None else None),
            "max_frames": 0,
        }
        self.script.exports_sync.init(init_cfg)
        self.device.resume(self.pid)
        self._logline(f"[ready] pid={self.pid} view={a.view} turbo={not a.realtime} "
                      f"segtrace={a.segtrace} active={not bool(seg_ops)}")
        # If we bootstrapped, wait for the segtrace to drain, then activate.
        if seg_ops:
            threading.Thread(target=self._await_segtrace_then_activate,
                             daemon=True).start()

    def _await_segtrace_then_activate(self, timeout=120.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline and not self._stop.is_set():
            try:
                st = self.script.exports_sync.probe_status()
                seg = st.get("segtrace")
                if seg and seg.get("done"):
                    self.script.exports_sync.probe_activate(True)
                    self._logline("[bootstrap] segtrace done → probe activated")
                    return
            except Exception:
                pass
            time.sleep(0.5)
        self._logline("[bootstrap] segtrace wait timed out — activating anyway")
        try:
            self.script.exports_sync.probe_activate(True)
        except Exception:
            pass

    def _load_segtrace(self, path: Path):
        """Parse an input_segtrace JSONL (same grammar frida_capture parses).
        Thin loader — we only need the ops list passed to the agent verbatim,
        with va-strings normalized to ints where the agent expects them."""
        ops = []
        for raw in path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            rec = json.loads(line)
            if "buttons" in rec and "frame" in rec:
                ops.append({"frame": int(rec["frame"]),
                            "mask": int(str(rec["buttons"]), 0)})
            else:
                ops.append(rec)
        return ops

    def _heartbeat_ok(self):
        try:
            self.script.exports_sync.probe_status()
            return True
        except Exception:
            return False

    def stop(self):
        self._stop.set()
        try:
            if self.pid is not None:
                self.device.kill(self.pid)
        except Exception:
            pass

    # ── input ──────────────────────────────────────────────────────────────
    @staticmethod
    def _mask(spec) -> int:
        """Accept an int mask or a '+'-joined button-name string ('up+a')."""
        if isinstance(spec, int):
            return spec & 0xffff
        m = 0
        for tok in str(spec).replace(",", "+").split("+"):
            tok = tok.strip().lower()
            if not tok:
                continue
            if tok.startswith("0x"):
                m |= int(tok, 16)
            elif tok in BTN:
                m |= BTN[tok]
            else:
                raise ValueError(f"unknown button {tok!r}")
        return m & 0xffff

    def _tap(self, spec, press=2, gap=2, repeat=1):
        m = self._mask(spec)
        self.script.exports_sync.probe_tap(m, press, gap, repeat)
        return {"ok": True, "mask": m, "press": press, "gap": gap, "repeat": repeat}

    def _hold(self, spec, frames=None):
        m = self._mask(spec)
        if frames:
            self.script.exports_sync.probe_hold_for(m, int(frames))
        else:
            self.script.exports_sync.probe_hold(m)
        return {"ok": True, "mask": m, "frames": frames}

    # ── navigation (world px/pz plane) ──────────────────────────────────────
    # Movement axis mapping (measured live): left/right = -/+ player X
    # (DAT_056da1d8), up/down = -/+ player Z (DAT_056da1e0). py is height (~0).
    PX_VA, PZ_VA = 0x056DA1D8, 0x056DA1E0

    def _pos(self):
        x = self.script.exports_sync.probe_read(self.PX_VA, "f32")
        z = self.script.exports_sync.probe_read(self.PZ_VA, "f32")
        return x, z

    @staticmethod
    def _dir_mask(dx, dz, dead):
        """8-way button mask toward (dx,dz). left/right=-/+X, up/down=-/+Z."""
        m = 0
        if dx > dead:
            m |= BTN["right"]
        elif dx < -dead:
            m |= BTN["left"]
        if dz > dead:
            m |= BTN["down"]
        elif dz < -dead:
            m |= BTN["up"]
        return m

    def _goto(self, tx, tz, tol=0.35, max_iter=120, step=4):
        """Rudimentary collider-aware walk to world (tx,tz). Greedy 8-way toward
        the target with an ADAPTIVE step (in turbo one long hold overshoots by
        ~1 unit, so shrink the hold to 1 frame within ~1.5 units to fine-home).
        On a stall try single-axis slides (X-only, Z-only) to slide along a wall.
        Stops at the CLOSEST point reached if it can't improve (oscillation / a
        collider between us and an unreachable target). Not a real path planner."""
        x, z = self._pos()
        path = [(round(x, 2), round(z, 2))]
        best = (x - tx) ** 2 + (z - tz) ** 2
        no_improve = 0
        mode = 0   # 0=diagonal 1=X-only slide 2=Z-only slide
        for it in range(max_iter):
            x, z = self._pos()
            dx, dz = tx - x, tz - z
            dist = (dx * dx + dz * dz) ** 0.5
            if dist <= tol:
                self.script.exports_sync.probe_release()
                return {"ok": True, "reached": True, "iters": it,
                        "pos": [round(x, 2), round(z, 2)], "dist": round(dist, 3),
                        "path": path}
            # Adaptive hold length: far → long strides, near → single frames so
            # we don't blow past the tolerance band.
            s = 1 if dist < 1.5 else (3 if dist < 4 else step + 2)
            dead = tol * 0.4
            if mode == 0:
                m = self._dir_mask(dx, dz, dead)
            elif mode == 1:
                m = self._dir_mask(dx, 0, dead)
            else:
                m = self._dir_mask(0, dz, dead)
            if m == 0:
                m = self._dir_mask(dx, dz, 0)
            self.script.exports_sync.probe_hold_for(m, s)
            time.sleep(s * 0.02 + 0.05)
            nx, nz = self._pos()
            if (round(nx, 2), round(nz, 2)) != path[-1]:
                path.append((round(nx, 2), round(nz, 2)))
            d2 = (nx - tx) ** 2 + (nz - tz) ** 2
            if d2 < best - 1e-4:
                best = d2
                no_improve = 0
                mode = 0
            else:
                no_improve += 1
                if no_improve == 3:
                    mode = (mode + 1) % 3          # try sliding along a wall
                elif no_improve >= 8:
                    break                           # can't get closer — give up
        self.script.exports_sync.probe_release()
        x, z = self._pos()
        return {"ok": True, "reached": False, "iters": it + 1,
                "pos": [round(x, 2), round(z, 2)],
                "dist": round(((tx - x) ** 2 + (tz - z) ** 2) ** 0.5, 3),
                "closest_dist": round(best ** 0.5, 3), "path": path}

    # ── cheats (direct state pokes — bypass gameplay for the driving agent) ──
    # Teleport instantly (poke the actor-0 position; render + logic read it
    # directly, per findings/conversation-pose-driver + scene1_shop_walker.c:779).
    PLAYER_Y_VA = 0x056DA1DC
    FACING_VA   = 0x056DB05C   # s_player_facing world angle (radians)
    STICKY_VA   = 0x056DAE3C   # diagonal-snap sticky bias
    GOLD_VA     = WORKING_BANK + 3 * 4
    # 8 compass facings → world angle (radians). Calibrated to idle +pi/2 = down
    # (toward camera, octant 6). CCW by pi/4 per step around the px/pz plane.
    import math as _math
    FACING_DIR = {
        "down": _math.pi / 2, "downleft": 3 * _math.pi / 4, "left": _math.pi,
        "upleft": -3 * _math.pi / 4, "up": -_math.pi / 2, "upright": -_math.pi / 4,
        "right": 0.0, "downright": _math.pi / 4,
    }

    def _teleport(self, x, z, y=None):
        x = float(x); z = float(z)
        self.script.exports_sync.probe_poke(self.PX_VA, "f32", x)
        self.script.exports_sync.probe_poke(self.PZ_VA, "f32", z)
        if y is not None:
            self.script.exports_sync.probe_poke(self.PLAYER_Y_VA, "f32", float(y))
        nx, nz = self._pos()
        return {"ok": True, "pos": [round(nx, 3), round(nz, 3)]}

    def _set_facing(self, spec):
        if isinstance(spec, (int, float)) and not isinstance(spec, bool):
            ang = float(spec)
            name = None
        else:
            name = str(spec).lower()
            if name not in self.FACING_DIR:
                return {"ok": False, "err": f"dir {name!r} not in {list(self.FACING_DIR)}"}
            ang = self.FACING_DIR[name]
        self.script.exports_sync.probe_poke(self.FACING_VA, "f32", ang)
        return {"ok": True, "facing": name or ang, "angle_rad": round(ang, 4)}

    def _set_gold(self, n):
        self.script.exports_sync.probe_poke(self.GOLD_VA, "i32", int(n))
        return {"ok": True, "gold": int(n)}

    # Named waypoints (per-run, persisted). Record current pos → name, recall
    # by name. Lets the agent build up a map of the shop as it explores.
    def _waypoints_path(self):
        return self.run_dir / "waypoints.json"

    def _waypoints(self):
        p = self._waypoints_path()
        if p.exists():
            return json.loads(p.read_text())
        return {}

    def _waypoint_set(self, name):
        wps = self._waypoints()
        x, z = self._pos()
        wps[name] = [round(x, 3), round(z, 3)]
        self._waypoints_path().write_text(json.dumps(wps, indent=2))
        return {"ok": True, "name": name, "pos": wps[name]}

    # ── screenshot ───────────────────────────────────────────────────────
    def _shot(self, path=None, timeout=3.0):
        got = None
        for _ in range(6):
            self._frame_evt.clear()
            with self._frame_lock:
                prev = self._frame[0] if self._frame else -1
            self.script.exports_sync.probe_shot(1)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if self._frame_evt.wait(0.2):
                    with self._frame_lock:
                        if self._frame and self._frame[0] != prev:
                            got = self._frame
                            break
                    self._frame_evt.clear()
            if got is not None:
                break
            time.sleep(0.2)
        if got is None:
            return {"ok": False, "err": "no frame (is the game presenting?)"}
        fi, w, h, bgra, vals = got
        if path:
            out = Path(path)
        else:
            self._shot_seq += 1
            out = self.run_dir / "shots" / f"shot_{self._shot_seq:04d}.png"
        out.parent.mkdir(parents=True, exist_ok=True)
        _save_png(out, w, h, bgra)
        return {"ok": True, "path": str(out), "w": w, "h": h,
                "frame_idx": fi, "vals": vals}

    def _rec_write(self, rec, w, h, bgra):
        if w != rec["w"] or h != rec["h"]:
            return
        try:
            rec["proc"].stdin.write(_bgra_to_rgb(w, h, bgra))
            rec["n"] += 1
        except (BrokenPipeError, ValueError):
            pass

    def _record_start(self, path, fps=30):
        import subprocess
        if self._rec:
            return {"ok": False, "err": "already recording"}
        fr = self._shot()
        if not fr.get("ok"):
            return {"ok": False, "err": "no frame for dims"}
        w, h = fr["w"], fr["h"]
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        proc = subprocess.Popen(
            ["ffmpeg", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
             "-s", f"{w}x{h}", "-r", str(fps), "-i", "-",
             "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", str(path)],
            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        self._rec = {"proc": proc, "path": str(path), "w": w, "h": h,
                     "fps": fps, "n": 0, "t0": time.monotonic()}
        # Stream every present (turbo → fast timelapse; realtime → real fps).
        self.script.exports_sync.probe_stream(1)
        return {"ok": True, "path": str(path), "w": w, "h": h, "fps": fps}

    def _record_stop(self):
        if not self._rec:
            return {"ok": False, "err": "not recording"}
        try:
            self.script.exports_sync.probe_stream(0)
        except Exception:
            pass
        time.sleep(0.25)
        rec, self._rec = self._rec, None
        try:
            rec["proc"].stdin.close()
            rec["proc"].wait(timeout=15)
        except Exception:
            try:
                rec["proc"].kill()
            except Exception:
                pass
        dur = time.monotonic() - rec["t0"]
        return {"ok": True, "path": rec["path"], "frames": rec["n"],
                "duration_s": round(dur, 2)}

    # ── engine-thread call ──────────────────────────────────────────────────
    def _callq(self, va, args, argt, ret, abi, timeout):
        self._call_evt.clear()
        cid = self.script.exports_sync.probe_enqueue_call(
            va, args or [], argt or [], ret or "int32", abi or "mscdecl")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if cid in self._call_results:
                r = self._call_results.pop(cid)
                return {"ok": r.get("err") is None, "ret": r.get("ret"),
                        "err": r.get("err"), "id": cid, "frame": r.get("frame")}
            self._call_evt.wait(0.2)
            self._call_evt.clear()
        return {"ok": False, "err": "call timed out (engine not ticking?)",
                "id": cid}

    # ── command dispatch ─────────────────────────────────────────────────────
    def dispatch(self, req: dict) -> dict:
        cmd = req.get("cmd")
        try:
            x = self.script.exports_sync
            if cmd == "ping":
                return {"ok": True, "pid": self.pid, "alive": self._heartbeat_ok()}
            if cmd == "status":
                return {"ok": True, "status": x.probe_status()}
            if cmd == "shot":
                return self._shot(req.get("path"))
            if cmd == "record":
                if req.get("action") == "start":
                    return self._record_start(req["path"], int(req.get("fps", 30)))
                return self._record_stop()
            if cmd == "tap":
                return self._tap(req["mask"], int(req.get("press", 2)),
                                 int(req.get("gap", 2)), int(req.get("repeat", 1)))
            if cmd == "hold":
                return self._hold(req["mask"], req.get("frames"))
            if cmd == "release":
                x.probe_release()
                return {"ok": True}
            if cmd == "where":
                px, pz = self._pos()
                return {"ok": True, "x": round(px, 3), "z": round(pz, 3)}
            if cmd == "goto":
                if "name" in req:
                    wps = self._waypoints()
                    if req["name"] not in wps:
                        return {"ok": False, "err": f"no waypoint {req['name']!r}",
                                "known": list(wps)}
                    tx, tz = wps[req["name"]]
                else:
                    tx, tz = float(req["x"]), float(req["z"])
                return self._goto(tx, tz, float(req.get("tol", 0.35)),
                                  int(req.get("max_iter", 120)),
                                  int(req.get("step", 4)))
            if cmd == "waypoint":
                if req.get("action") == "set":
                    return self._waypoint_set(req["name"])
                return {"ok": True, "waypoints": self._waypoints()}
            if cmd == "teleport":
                if "name" in req:
                    wps = self._waypoints()
                    if req["name"] not in wps:
                        return {"ok": False, "err": f"no waypoint {req['name']!r}"}
                    tx, tz = wps[req["name"]]
                    return self._teleport(tx, tz)
                return self._teleport(req["x"], req["z"], req.get("y"))
            if cmd == "face":
                return self._set_facing(req["dir"])
            if cmd == "setgold":
                return self._set_gold(req["gold"])
            if cmd == "esc":
                x.probe_esc()
                return {"ok": True}
            if cmd == "input":            # who owns the mask (interactive toggle)
                on = bool(req["active"])  # active=True → probe owns (human LOCKED)
                x.probe_activate(on)
                # Handing control to the human → drop turbo to 1× so the game is
                # playable in real time; re-locking to the probe → restore turbo.
                # Opt out with {"keep_turbo": true}.
                turbo = None
                if not req.get("keep_turbo"):
                    turbo = x.probe_set_turbo(on)   # human(off)→1×, probe(on)→turbo
                return {"ok": True, "probe_active": on,
                        "human_input": "locked" if on else "enabled",
                        "turbo": turbo}
            if cmd == "turbo":
                return {"ok": True, "turbo": x.probe_set_turbo(bool(req["on"]))}
            if cmd == "audio":
                return {"ok": True, "silent": x.probe_set_silent_audio(
                    not bool(req["on"]))}
            if cmd == "read":
                va = int(req["va"], 0) if isinstance(req["va"], str) else req["va"]
                return {"ok": True, "val": x.probe_read(va, req.get("type", "i32"))}
            if cmd == "reads":
                return {"ok": True, "vals": x.probe_reads(req["specs"])}
            if cmd == "state":
                specs = [{"name": n, "va": va, "type": t} for n, va, t in STATE_SPECS]
                return {"ok": True, "vals": x.probe_reads(specs)}
            if cmd == "poke":
                va = int(req["va"], 0) if isinstance(req["va"], str) else req["va"]
                if "bytes" in req:
                    return {"ok": True, "n": x.probe_poke_bytes(va, req["bytes"])}
                x.probe_poke(va, req.get("type", "i32"), req["val"])
                return {"ok": True}
            if cmd == "readmem":            # bulk byte-range read → hex (snapshot)
                va = int(req["va"], 0) if isinstance(req["va"], str) else req["va"]
                return {"ok": True, "hex": x.read_memory(va, int(req["len"]))}
            if cmd == "writemem":           # bulk byte-range write from hex (restore)
                va = int(req["va"], 0) if isinstance(req["va"], str) else req["va"]
                return {"ok": True, "n": x.write_memory(va, req["hex"])}
            if cmd == "callq":
                va = int(req["va"], 0) if isinstance(req["va"], str) else req["va"]
                return self._callq(va, req.get("args", []), req.get("argt", []),
                                   req.get("ret", "int32"), req.get("abi", "mscdecl"),
                                   float(req.get("timeout", 5.0)))
            if cmd == "anchors":
                with self._anchor_lock:
                    evs = list(self._anchors)
                    if req.get("clear"):
                        self._anchors = []
                return {"ok": True, "anchors": evs}
            if cmd == "sleep":
                time.sleep(float(req.get("ms", 200)) / 1000.0)
                return {"ok": True}
            if cmd == "quit":
                return {"ok": True, "bye": True}
            return {"ok": False, "err": f"unknown cmd {cmd!r}"}
        except Exception as e:
            self._logline(f"[dispatch] {cmd} failed: {e!r}")
            return {"ok": False, "err": repr(e)}

    def serve(self, port: int):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", port))
        srv.listen(4)
        port = srv.getsockname()[1]
        CONTROL_JSON.parent.mkdir(parents=True, exist_ok=True)
        CONTROL_JSON.write_text(json.dumps(
            {"port": port, "pid": self.pid, "run_dir": str(self.run_dir)}))
        self._logline(f"[serve] listening on 127.0.0.1:{port}")
        print(f"[probe_daemon] ready on 127.0.0.1:{port} (pid={self.pid}); "
              f"run_dir={self.run_dir}", flush=True)
        try:
            while not self._stop.is_set():
                conn, _ = srv.accept()
                with conn:
                    buf = b""
                    while not buf.endswith(b"\n"):
                        chunk = conn.recv(65536)
                        if not chunk:
                            break
                        buf += chunk
                    if not buf:
                        continue
                    try:
                        req = json.loads(buf.decode())
                    except Exception as e:
                        conn.sendall((json.dumps({"ok": False,
                                     "err": f"bad json: {e}"}) + "\n").encode())
                        continue
                    reply = self.dispatch(req)
                    conn.sendall((json.dumps(reply) + "\n").encode())
                    if reply.get("bye"):
                        break
        finally:
            try:
                CONTROL_JSON.unlink()
            except Exception:
                pass
            self.stop()


def _bgra_to_rgb(w: int, h: int, bgra: bytes) -> bytes:
    from PIL import Image
    img = Image.frombytes("RGBA", (w, h), bgra, "raw", "BGRA")
    return img.convert("RGB").tobytes()


def _save_png(out: Path, w: int, h: int, bgra: bytes):
    from PIL import Image
    Image.frombytes("RGBA", (w, h), bgra, "raw", "BGRA").convert("RGB").save(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--remote", default=fc.DEFAULT_REMOTE)
    ap.add_argument("--exe", default=str(fc.RETAIL_EXE))
    ap.add_argument("--cwd", default=str(fc.ASSET_CWD))
    ap.add_argument("--port", type=int, default=27100)
    ap.add_argument("--run-id", default="session")
    ap.add_argument("--segtrace", default=None,
                    help="bootstrap input_segtrace to reach a known state first")
    ap.add_argument("--rng-seed", default=None,
                    help="pin DAT_006023a0 to this seed (e.g. 19937)")
    ap.add_argument("--view", action="store_true",
                    help="show the preview window WITHOUT stealing focus")
    ap.add_argument("--realtime", action="store_true",
                    help="run at 1× (default: turbo fast-forward)")
    ap.add_argument("--turbo-step-ms", type=int, default=17)
    ap.add_argument("--audio", action="store_true",
                    help="leave audio on (default: silenced)")
    ap.add_argument("--no-seed-save", action="store_true",
                    help="don't seed the sandbox from the real save (fresh boot)")
    args = ap.parse_args()

    d = ProbeDaemon(args)
    d.start()
    d.serve(args.port)


if __name__ == "__main__":
    main()
