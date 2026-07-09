#!/usr/bin/env python3
"""OpenRecet MCP server — let an LLM PLAY and PROBE live retail Recettear.

A stdlib-only Model-Context-Protocol server (stdio / newline-delimited JSON-RPC
2.0, no SDK dependency) that sits on the live-probe daemon (tools/probe_daemon.py)
and exposes the running retail game as MCP tools:

  session   launch · game_status · set_interactive · set_turbo · set_audio · quit
  observe   game_state · screenshot · read_memory · read_state · anchors
  act       press · hold · walk · esc · wait
  advanced  poke_memory · call_function

The daemon holds ONE persistent game; this MCP is a thin, LLM-friendly skin over
it.  `launch` spawns the daemon as a detached process (so the game survives across
tool calls) and — crucially for the RE workflow — can BOOTSTRAP to a known state
via an input_segtrace, pin RNG, and open the no-focus-steal preview window.

Registered as the `openrecet` MCP (stdio) in .mcp.json, launched via a GC-pinned
raw python interpreter (NOT `nix develop -c python3`, whose devshell banner
corrupts the JSON-RPC stream).

Self-test without an MCP client:
    tools/openrecet_mcp.py --selftest
"""
from __future__ import annotations

import base64
import json
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CTRL = ROOT / "runs" / "probe" / "daemon.json"
DAEMON = ROOT / "tools" / "probe_daemon.py"


# ── daemon client ────────────────────────────────────────────────────────────
class DaemonError(RuntimeError):
    pass


def _port():
    if CTRL.exists():
        try:
            return int(json.loads(CTRL.read_text())["port"])
        except Exception as e:
            raise DaemonError(f"daemon.json unreadable: {e}") from e
    raise DaemonError("no daemon — call launch first (or start probe_daemon.py).")


def dsend(req, timeout=40.0):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(("127.0.0.1", _port()))
        s.sendall((json.dumps(req) + "\n").encode())
        buf = b""
        while not buf.endswith(b"\n"):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
        s.close()
    except (OSError, DaemonError) as e:
        raise DaemonError(f"daemon unreachable: {e}") from e
    return json.loads(buf.decode()) if buf else {"ok": False, "err": "no reply"}


def _daemon_alive():
    if not CTRL.exists():
        return False
    try:
        return bool(dsend({"cmd": "ping"}, timeout=5).get("alive"))
    except DaemonError:
        return False


# ── curated state formatting ─────────────────────────────────────────────────
SCENE = {0: "TITLE", 1: "INGAME", 8: "LOADING"}
CC08 = {1: "free-roam", 4: "customer-service"}


def _fmt_state(v):
    scene = SCENE.get(v.get("scene"), v.get("scene"))
    cc = CC08.get(v.get("cc08"), v.get("cc08"))
    day = (v.get("day") or 0) + 1
    lines = [
        f"scene={scene}  interaction={cc}  day={day}  gold={v.get('gold')}",
        f"dialogue={v.get('dlg')}  pause={v.get('pause')}  "
        f"loading={v.get('nowload')}/{v.get('nowload2')} worker={v.get('worker')}",
        f"player: state={v.get('player_st')} frame={v.get('player_fr')} "
        f"pos=({v.get('px'):.2f},{v.get('py'):.2f},{v.get('pz'):.2f})"
        if v.get("px") is not None else "player: (unavailable)",
        f"rng={v.get('rng')}  shoptime={v.get('shoptime')}",
    ]
    return "\n".join(lines)


# ── tool implementations ─────────────────────────────────────────────────────
def tool_launch(args):
    """Spawn the probe daemon (detached) — brings the game up. Optionally
    bootstrap to a known state via an input_segtrace and pin RNG."""
    if _daemon_alive():
        st = dsend({"cmd": "status"}).get("status", {})
        return [_text("A game is already running (daemon alive). "
                      f"frame={st.get('frame')} probe_active={st.get('probe_active')}. "
                      "Call quit first to relaunch.")]
    cmd = [sys.executable, str(DAEMON)]
    # The daemon needs the nix devshell (frida, PIL). Re-exec through nix
    # develop so the detached process has the right environment.
    nix_cmd = ["nix", "develop", str(ROOT), "--command", "python3", str(DAEMON)]
    if args.get("view", True):
        nix_cmd.append("--view")
    if args.get("realtime"):
        nix_cmd.append("--realtime")
    if args.get("audio"):
        nix_cmd.append("--audio")
    if args.get("rng_seed") is not None:
        nix_cmd += ["--rng-seed", str(args["rng_seed"])]
    if args.get("segtrace"):
        seg = Path(args["segtrace"])
        if not seg.is_absolute():
            seg = ROOT / seg
        nix_cmd += ["--segtrace", str(seg)]
    logf = (ROOT / "runs" / "probe" / "daemon.boot.log")
    logf.parent.mkdir(parents=True, exist_ok=True)
    lf = open(logf, "w")
    # Detach: new session so it survives this MCP call. Discard the control
    # file first so we can detect the fresh daemon coming up.
    try:
        CTRL.unlink()
    except FileNotFoundError:
        pass
    subprocess.Popen(nix_cmd, cwd=str(ROOT), stdout=lf, stderr=subprocess.STDOUT,
                     stdin=subprocess.DEVNULL, start_new_session=True)
    # Wait for the daemon to publish its control file + become reachable.
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if _daemon_alive():
            st = dsend({"cmd": "status"}).get("status", {})
            return [_text(
                "Game launched. daemon is up.\n"
                f"frame={st.get('frame')} turbo={st.get('turbo')} "
                f"probe_active={st.get('probe_active')} "
                f"segtrace={st.get('segtrace')}\n"
                "(If a bootstrap segtrace was given, probe activates when it "
                "finishes — poll game_status.)")]
        time.sleep(1.0)
    tail = ""
    try:
        tail = "\n".join(logf.read_text().splitlines()[-15:])
    except Exception:
        pass
    return [_text(f"Launch timed out after 90s. daemon.boot.log tail:\n{tail}")]


def tool_game_status(_):
    if not _daemon_alive():
        return [_text("No game running. Call launch.")]
    st = dsend({"cmd": "status"}).get("status", {})
    return [_text(json.dumps(st, indent=2))]


def tool_game_state(_):
    r = dsend({"cmd": "state"})
    if not r.get("ok"):
        return [_text(f"state failed: {r.get('err')}")]
    return [_text(_fmt_state(r["vals"]))]


def tool_screenshot(args):
    r = dsend({"cmd": "shot", "path": args.get("path")})
    if not r.get("ok"):
        return [_text(f"screenshot FAILED: {r.get('err')}")]
    out = [_text(f"frame {r['frame_idx']} ({r['w']}×{r['h']}) → {r['path']}"
                 + (f"\nstate: {json.dumps(r['vals'])}" if r.get("vals") else ""))]
    # Inline the image so the model SEES it (unless disabled for token budget).
    if args.get("inline", True):
        try:
            data = base64.b64encode(Path(r["path"]).read_bytes()).decode()
            out.append({"type": "image", "data": data, "mimeType": "image/png"})
        except Exception as e:
            out.append(_text(f"(inline image failed: {e})"))
    return out


def tool_read_state(_):
    r = dsend({"cmd": "state"})
    return [_text(json.dumps(r.get("vals", {}), indent=2))]


def tool_read_memory(args):
    va = args["va"]
    r = dsend({"cmd": "read", "va": va, "type": args.get("type", "i32")})
    return [_text(json.dumps(r))]


def tool_poke_memory(args):
    req = {"cmd": "poke", "va": args["va"]}
    if "bytes" in args:
        req["bytes"] = args["bytes"]
    else:
        req["type"] = args.get("type", "i32")
        req["val"] = args["val"]
    return [_text(json.dumps(dsend(req)))]


def tool_call_function(args):
    req = {"cmd": "callq", "va": args["va"], "args": args.get("args", []),
           "argt": args.get("argt", []), "ret": args.get("ret", "int32"),
           "abi": args.get("abi", "mscdecl"), "timeout": args.get("timeout", 5.0)}
    return [_text(json.dumps(dsend(req)))]


def tool_press(args):
    r = dsend({"cmd": "tap", "mask": args["button"],
               "press": args.get("press", 2), "gap": args.get("gap", 2),
               "repeat": args.get("repeat", 1)})
    return [_text(json.dumps(r))]


def tool_hold(args):
    r = dsend({"cmd": "hold", "mask": args["button"], "frames": args.get("frames")})
    return [_text(json.dumps(r))]


def tool_walk(args):
    """Hold a direction for N frames (a timed move), then release."""
    r = dsend({"cmd": "hold", "mask": args["direction"],
               "frames": int(args.get("frames", 30))})
    return [_text(json.dumps(r))]


def tool_esc(_):
    return [_text(json.dumps(dsend({"cmd": "esc"})))]


def tool_where(_):
    r = dsend({"cmd": "where"})
    return [_text(f"player at x={r.get('x')} z={r.get('z')}")]


def tool_move_to(args):
    """Walk to a world (x,z) target or a named waypoint, with rudimentary
    collider-aware wiggling. x/z are the player-position plane (left/right=X,
    up/down=Z)."""
    req = {"cmd": "goto", "tol": args.get("tol", 0.35),
           "max_iter": args.get("max_iter", 120)}
    if "name" in args:
        req["name"] = args["name"]
    else:
        req["x"], req["z"] = args["x"], args["z"]
    r = dsend({**req}, timeout=120)
    return [_text(json.dumps(r))]


def tool_waypoint(args):
    """Record the current position as a named waypoint (action=set) or list all."""
    if args.get("action") == "set":
        return [_text(json.dumps(dsend({"cmd": "waypoint", "action": "set",
                                        "name": args["name"]})))]
    return [_text(json.dumps(dsend({"cmd": "waypoint"}), indent=2))]


def tool_teleport(args):
    """CHEAT: instantly move the player to (x,z) or a waypoint — pokes the actor
    position directly, bypassing colliders and walk time."""
    req = {"cmd": "teleport"}
    if "name" in args:
        req["name"] = args["name"]
    else:
        req["x"], req["z"] = args["x"], args["z"]
        if "y" in args:
            req["y"] = args["y"]
    return [_text(json.dumps(dsend(req)))]


def tool_set_facing(args):
    """CHEAT: force the player's facing. dir = a compass name (up/down/left/right/
    upleft/upright/downleft/downright) or an angle in radians."""
    return [_text(json.dumps(dsend({"cmd": "face", "dir": args["dir"]})))]


def tool_set_gold(args):
    """CHEAT: set the player's gold (pix)."""
    return [_text(json.dumps(dsend({"cmd": "setgold", "gold": args["gold"]})))]


def tool_wait(args):
    return [_text(json.dumps(dsend({"cmd": "sleep",
                                    "ms": int(args.get("ms", 500))})))]


def tool_anchors(args):
    r = dsend({"cmd": "anchors", "clear": bool(args.get("clear"))})
    evs = r.get("anchors", [])
    return [_text(json.dumps(evs, indent=2) if evs else "(no anchors captured yet)")]


def tool_set_interactive(args):
    """enabled=true hands input to the human (probe releases the mask);
    enabled=false re-locks it to the probe."""
    on = bool(args["enabled"])
    r = dsend({"cmd": "input", "active": not on})   # probe_active = not human
    return [_text(json.dumps(r))]


def tool_set_turbo(args):
    return [_text(json.dumps(dsend({"cmd": "turbo", "on": bool(args["on"])})))]


def tool_set_audio(args):
    return [_text(json.dumps(dsend({"cmd": "audio", "on": bool(args["on"])})))]


def tool_quit(_):
    if not _daemon_alive():
        return [_text("No game running.")]
    dsend({"cmd": "quit"})
    return [_text("Game + daemon shut down.")]


def _text(s):
    return {"type": "text", "text": s}


# ── tool registry ────────────────────────────────────────────────────────────
BTN_DESC = ("button/direction name(s): up down left right, a (confirm/talk/pick), "
            "b (cancel), c, d, e, s0..s4 (skills). Combine with '+' e.g. 'up+a', "
            "or pass a hex mask like '0x14'.")
TOOLS = [
    ("launch", "Start the game: spawn the live-probe daemon (detached, survives "
     "across calls). Opens a no-focus-steal preview window and locks human input "
     "to the probe. Optionally BOOTSTRAP to a known state via an input_segtrace "
     "and pin RNG (for deterministic RE work).",
     {"type": "object", "properties": {
         "view": {"type": "boolean", "description": "show the preview window (default true)"},
         "realtime": {"type": "boolean", "description": "run at 1× instead of turbo"},
         "audio": {"type": "boolean", "description": "leave audio on (default silenced)"},
         "rng_seed": {"type": ["integer", "null"], "description": "pin DAT_006023a0 (e.g. 19937)"},
         "segtrace": {"type": "string", "description": "input_segtrace path to bootstrap to a known state"}}},
     tool_launch),
    ("game_status", "Daemon/agent status: frame, turbo, probe_active (input lock), "
     "segtrace bootstrap progress, queue depth.",
     {"type": "object", "properties": {}}, tool_game_status),
    ("game_state", "Curated live game state: scene, interaction mode, day, gold, "
     "dialogue/pause/loading gates, player pos+anim, rng.",
     {"type": "object", "properties": {}}, tool_game_state),
    ("screenshot", "Capture the current frame to PNG and (by default) inline it so "
     "you can SEE the screen. Bypasses load-suppression (shows loading screens too).",
     {"type": "object", "properties": {
         "path": {"type": "string"},
         "inline": {"type": "boolean", "description": "embed the image (default true)"}}},
     tool_screenshot),
    ("press", "Tap a button for a few frames (confirm dialogue, pick up, menu). " + BTN_DESC,
     {"type": "object", "properties": {
         "button": {"type": ["string", "integer"]},
         "press": {"type": "integer", "description": "frames held (default 2)"},
         "gap": {"type": "integer", "description": "frames released after (default 2)"},
         "repeat": {"type": "integer"}}, "required": ["button"]}, tool_press),
    ("hold", "Set a sticky held button (walk continuously). Omit frames to hold "
     "until changed; give frames for a timed hold. " + BTN_DESC,
     {"type": "object", "properties": {
         "button": {"type": ["string", "integer"]},
         "frames": {"type": "integer"}}, "required": ["button"]}, tool_hold),
    ("walk", "Hold a direction for N frames then release (a timed move). "
     "direction: up/down/left/right.",
     {"type": "object", "properties": {
         "direction": {"type": "string"},
         "frames": {"type": "integer"}}, "required": ["direction"]}, tool_walk),
    ("esc", "Press ESC (keyboard path — pause menu / skip cutscene event).",
     {"type": "object", "properties": {}}, tool_esc),
    ("where", "Player world position (x,z plane). left/right move X, up/down move Z.",
     {"type": "object", "properties": {}}, tool_where),
    ("move_to", "Walk to a world (x,z) target OR a named waypoint, with rudimentary "
     "collider-aware wiggling (greedy walk toward it, slides along walls when stuck). "
     "Returns reached + the path taken. Use `where` / screenshots to pick targets and "
     "`waypoint` to remember spots (counter, display stands).",
     {"type": "object", "properties": {
         "x": {"type": "number"}, "z": {"type": "number"},
         "name": {"type": "string", "description": "a saved waypoint name (instead of x/z)"},
         "tol": {"type": "number", "description": "arrival tolerance (default 0.35)"},
         "max_iter": {"type": "integer"}}}, tool_move_to),
    ("waypoint", "action=set records the current position under `name`; omit action "
     "to list all saved waypoints.",
     {"type": "object", "properties": {
         "action": {"type": "string", "enum": ["set", "list"]},
         "name": {"type": "string"}}}, tool_waypoint),
    ("teleport", "CHEAT: instantly move the player to (x,z) or a named waypoint "
     "(pokes the actor position — bypasses colliders + walk time). Far faster than "
     "move_to when you know the destination.",
     {"type": "object", "properties": {
         "x": {"type": "number"}, "z": {"type": "number"}, "y": {"type": "number"},
         "name": {"type": "string"}}}, tool_teleport),
    ("set_facing", "CHEAT: force the player's facing direction. dir = a compass name "
     "(up/down/left/right/upleft/upright/downleft/downright) or an angle (radians).",
     {"type": "object", "properties": {"dir": {"type": ["string", "number"]}},
      "required": ["dir"]}, tool_set_facing),
    ("set_gold", "CHEAT: set the player's gold (pix).",
     {"type": "object", "properties": {"gold": {"type": "integer"}},
      "required": ["gold"]}, tool_set_gold),
    ("wait", "Let the game run for a real wall-clock interval (ms). Use between "
     "actions so dialogue/animation/loads advance before you screenshot.",
     {"type": "object", "properties": {"ms": {"type": "integer"}}}, tool_wait),
    ("anchors", "Semantic anchor firings captured since launch (LOADING_END, "
     "CONV_POSE_START, CUSTOMER_SERVICE_ENTER, TEXT_ANIM_*, …) with frame+rng — "
     "the raw material for setting up deterministic traces. clear=true to drain.",
     {"type": "object", "properties": {"clear": {"type": "boolean"}}}, tool_anchors),
    ("set_interactive", "enabled=true hands game input to the HUMAN at the keyboard "
     "(probe releases the mask); enabled=false re-locks it to the probe.",
     {"type": "object", "properties": {"enabled": {"type": "boolean"}},
      "required": ["enabled"]}, tool_set_interactive),
    ("set_turbo", "Toggle turbo fast-forward at runtime (on=true fast, on=false 1×).",
     {"type": "object", "properties": {"on": {"type": "boolean"}}, "required": ["on"]},
     tool_set_turbo),
    ("set_audio", "Toggle game audio (on=true audible, on=false silenced).",
     {"type": "object", "properties": {"on": {"type": "boolean"}}, "required": ["on"]},
     tool_set_audio),
    ("read_state", "Raw curated state VAs as JSON (scene/rng/cc08/player/gold/day/…).",
     {"type": "object", "properties": {}}, tool_read_state),
    ("read_memory", "ADVANCED: read a game memory address (Ghidra VA; type in "
     "u8/i8/u16/i16/u32/i32/f32/f64/ptr).",
     {"type": "object", "properties": {
         "va": {"type": ["integer", "string"]}, "type": {"type": "string"}},
      "required": ["va"]}, tool_read_memory),
    ("poke_memory", "ADVANCED: write a typed value (or raw bytes[]) to a game "
     "memory address.",
     {"type": "object", "properties": {
         "va": {"type": ["integer", "string"]}, "type": {"type": "string"},
         "val": {"type": "number"}, "bytes": {"type": "array"}},
      "required": ["va"]}, tool_poke_memory),
    ("call_function", "ADVANCED: call a game function on the ENGINE thread (safe, "
     "runs at the pre-sim input point). Prefer this over pokes once a function is "
     "mapped + confirmed to reproduce an input's code path. va + args[] + optional "
     "argt[] (frida types) + ret + abi (mscdecl/stdcall/thiscall/fastcall).",
     {"type": "object", "properties": {
         "va": {"type": ["integer", "string"]}, "args": {"type": "array"},
         "argt": {"type": "array"}, "ret": {"type": "string"}, "abi": {"type": "string"}},
      "required": ["va"]}, tool_call_function),
    ("quit", "Shut down the game + daemon.",
     {"type": "object", "properties": {}}, tool_quit),
]
HANDLERS = {name: fn for name, _d, _s, fn in TOOLS}


# ── MCP JSON-RPC (stdio, newline-delimited) ──────────────────────────────────
def _log(msg):
    print(f"[openrecet-mcp] {msg}", file=sys.stderr, flush=True)


def handle(msg):
    mid = msg.get("id")
    method = msg.get("method")
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": mid, "result": {
            "protocolVersion": msg.get("params", {}).get("protocolVersion", "2024-11-05"),
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "openrecet", "version": "0.1.0"}}}
    if method in ("notifications/initialized", "initialized"):
        return None
    if method == "ping":
        return {"jsonrpc": "2.0", "id": mid, "result": {}}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": mid, "result": {
            "tools": [{"name": n, "description": d, "inputSchema": s}
                      for n, d, s, _ in TOOLS]}}
    if method == "tools/call":
        params = msg.get("params", {})
        name = params.get("name")
        args = params.get("arguments", {}) or {}
        fn = HANDLERS.get(name)
        if not fn:
            return {"jsonrpc": "2.0", "id": mid,
                    "error": {"code": -32601, "message": f"no tool {name}"}}
        try:
            content = fn(args)
            return {"jsonrpc": "2.0", "id": mid, "result": {"content": content}}
        except DaemonError as e:
            return {"jsonrpc": "2.0", "id": mid, "result": {
                "content": [_text(f"GAME NOT REACHABLE: {e}")], "isError": True}}
        except Exception as e:  # noqa: BLE001
            return {"jsonrpc": "2.0", "id": mid, "result": {
                "content": [_text(f"tool error: {e!r}")], "isError": True}}
    if mid is not None:
        return {"jsonrpc": "2.0", "id": mid,
                "error": {"code": -32601, "message": f"unknown method {method}"}}
    return None


def serve_stdio():
    _log("ready (stdio); tools connect to the probe daemon on demand")
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue
        resp = handle(msg)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


def selftest():
    for req in [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize",
         "params": {"protocolVersion": "2024-11-05"}},
        {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
        {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
         "params": {"name": "game_status", "arguments": {}}},
    ]:
        resp = handle(req)
        print(f"\n>>> {req.get('method')} {req.get('params',{}).get('name','')}")
        if resp and "result" in resp:
            res = resp["result"]
            if "content" in res:
                for c in res["content"]:
                    if c.get("type") == "text":
                        print(c["text"])
                    else:
                        print(f"[{c.get('type')} content]")
            elif "tools" in res:
                print("tools:", ", ".join(t["name"] for t in res["tools"]))
        elif resp:
            print(resp)


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    else:
        serve_stdio()
