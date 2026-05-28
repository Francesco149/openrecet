#!/usr/bin/env python3
"""
tools/diff_test.py — Phase D pure-function differential test orchestrator.

For each enabled target, generates N test vectors, dispatches them in
parallel to (a) retail via Frida runRetail* RPCs, and (b) our ported
implementation in tests/build/libengine_diff.so via ctypes.  Bit-exact
diffs.  First mismatch per target wins and is dumped in full.

Retail is spawned CREATE_SUSPENDED and never resumed — the Frida helper
thread runs the agent independently of the engine main thread, so all
NativeFunction calls / memory R/W happen in a frozen process.  No races
with the engine's own RNG/audio/render consumers.

Pattern lifted from OpenLords2's tools/diff_test.py (Phase 4).  Scaled
back here to the first target only; targets get added in D.2 / D.3.

Targets:
    rng_next15  — FUN_005041f6 (LCG step, DAT_006023a0)
    audio_fade  — FUN_00499583 (BGM cos-curve fade, ±1 centibel)

Usage:
    nix develop --command python3 tools/diff_test.py
    # → builds libengine_diff.so on demand, runs rng_next15 against
    #   retail at the default frida-remote, prints pass/fail summary.

    tools/diff_test.py --functions rng_next15 --vectors 200 --seed 0xCAFE
    tools/diff_test.py --frida-remote cutestation.soy:27042 --warmup-s 5

Exit code: 0 on all-pass, 1 on any divergence.

See docs/findings/pure-function-diff.md for the harness architecture +
how to add new targets.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import random
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import frida


# ─── paths + constants ─────────────────────────────────────────────────────

ROOT       = Path(__file__).resolve().parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"
DIFF_SO    = ROOT / "tests" / "build" / "libengine_diff.so"

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE",
                                "cutestation.soy:27042")


# ─── ctypes structs (mirror src/diff_entry.h) ──────────────────────────────

class EngineRngIn(ctypes.Structure):
    _fields_ = [
        ("seed", ctypes.c_uint32),
    ]

class EngineRngOut(ctypes.Structure):
    _fields_ = [
        ("post_state", ctypes.c_uint32),
        ("ret_value",  ctypes.c_uint16),
        ("_pad",       ctypes.c_uint16),
    ]

class EngineFadeIn(ctypes.Structure):
    _fields_ = [
        ("slider", ctypes.c_int32),
    ]

class EngineFadeOut(ctypes.Structure):
    _fields_ = [
        ("centibel", ctypes.c_int32),
    ]


# ─── port lib loader ───────────────────────────────────────────────────────

def load_port_lib() -> ctypes.CDLL:
    """Build (if needed) + ctypes.CDLL-load tests/build/libengine_diff.so."""
    if not DIFF_SO.exists():
        print(f"[diff] building {DIFF_SO.name} (first run)…", file=sys.stderr)
        subprocess.run(
            ["make", "-C", str(ROOT / "tests"), "diff"],
            check=True)
    lib = ctypes.CDLL(str(DIFF_SO))

    lib.engine_rng_next15.restype  = None
    lib.engine_rng_next15.argtypes = [ctypes.POINTER(EngineRngIn),
                                      ctypes.POINTER(EngineRngOut)]

    lib.engine_audio_fade.restype  = None
    lib.engine_audio_fade.argtypes = [ctypes.POINTER(EngineFadeIn),
                                      ctypes.POINTER(EngineFadeOut)]

    return lib


# ─── per-target callables ──────────────────────────────────────────────────
#
# Each target binds four functions: vector generator, port-side caller,
# retail-side caller, and output diff.  Wire them up in the TARGETS dict
# at the bottom of this section.

# rng_next15 ────────────────────────────────────────────────────────────────

# Edge cases the random fill won't reliably produce:
#   - 0            (would-be absorbing fixpoint if the LCG had no add)
#   - 1            (engine's .data initial seed for DAT_006023a0)
#   - 0xFFFFFFFF   (all bits set; tests u32 wraparound on imul)
#   - 0x80000000   (sign bit; would expose any signed/unsigned slip)
#   - 0x7FFFFFFF   (one below sign bit; complement of above)
#   - 0xAAAAAAAA   (alternating bits hi)
#   - 0x55555555   (alternating bits lo)
RNG_NEXT15_EDGES = [
    0x00000000, 0x00000001, 0xFFFFFFFF, 0x80000000,
    0x7FFFFFFF, 0xAAAAAAAA, 0x55555555,
]

def gen_rng_next15_vectors(n: int, rng: random.Random) -> list[dict]:
    out: list[dict] = [{"seed": s} for s in RNG_NEXT15_EDGES]
    while len(out) < n:
        out.append({"seed": rng.getrandbits(32)})
    return out[:n]

def run_port_rng_next15(lib: ctypes.CDLL, vec: dict) -> dict:
    in_  = EngineRngIn(seed=vec["seed"])
    out_ = EngineRngOut()
    lib.engine_rng_next15(ctypes.byref(in_), ctypes.byref(out_))
    return {"ret_value":  int(out_.ret_value),
            "post_state": int(out_.post_state)}

def run_retail_rng_next15(script, vec: dict) -> dict:
    r = script.exports_sync.run_retail_rng_next15(vec["seed"])
    return {"ret_value":  int(r["ret_value"]),
            "post_state": int(r["post_state"])}

def diff_rng_next15(retail: dict, port: dict) -> list[str]:
    bad = []
    if retail["ret_value"]  != port["ret_value"]:  bad.append("ret_value")
    if retail["post_state"] != port["post_state"]: bad.append("post_state")
    return bad


# audio_fade ─────────────────────────────────────────────────────────────────
#
# BGM cos-curve fade (FUN_00499583 → src/audio_fade.c). The retail side
# installs a fake AudioPath whose SetVolume slot records the applied
# centibel for a given BGM slider value; the port side calls
# audio_fade_compute(slider, 0). Because cos() differs across libm vs the
# MSVC CRT, this is NOT bit-exact: frames 1..9 allow ±1 centibel slack.
# The frame-0 hard-silence path skips cos entirely, so slider 0 must be
# exactly AUDIO_FADE_SILENCE_CENTIBEL (-10000) on both sides — and the
# engine must apply SetVolume exactly once on every path.

AUDIO_FADE_TOLERANCE_CENTIBEL = 1          # ±1 centibel = 1/100 dB
AUDIO_FADE_SILENCE_CENTIBEL   = -10000     # frame-0 hard silence

# slider 0..9 covers the engine's full valid input range; no random
# fill needed (the whole domain is enumerable).
AUDIO_FADE_SLIDERS = list(range(0, 10))

def gen_audio_fade_vectors(n: int, rng: random.Random) -> list[dict]:
    return [{"slider": s} for s in AUDIO_FADE_SLIDERS]

def run_port_audio_fade(lib: ctypes.CDLL, vec: dict) -> dict:
    in_  = EngineFadeIn(slider=vec["slider"])
    out_ = EngineFadeOut()
    lib.engine_audio_fade(ctypes.byref(in_), ctypes.byref(out_))
    return {"centibel": int(out_.centibel), "calls": 1}

def run_retail_audio_fade(script, vec: dict) -> dict:
    r = script.exports_sync.capture_fade_centibel(vec["slider"])
    return {"centibel": int(r["centibel"]), "calls": int(r["calls"])}

def diff_audio_fade(retail: dict, port: dict) -> list[str]:
    bad = []
    # The engine must apply SetVolume exactly once on every code path.
    if retail["calls"] != 1:
        bad.append("calls")
    if port.get("calls", 1) != 1:
        bad.append("port_calls")
    if port["centibel"] == AUDIO_FADE_SILENCE_CENTIBEL \
            or retail["centibel"] == AUDIO_FADE_SILENCE_CENTIBEL:
        # Hard-silence path (slider 0): exact match required.
        if retail["centibel"] != port["centibel"]:
            bad.append("centibel")
    else:
        # cos-curve path: ±1 centibel slack for libm vs MSVC CRT.
        if abs(retail["centibel"] - port["centibel"]) \
                > AUDIO_FADE_TOLERANCE_CENTIBEL:
            bad.append("centibel")
    return bad


# ─── target registry ───────────────────────────────────────────────────────

@dataclass
class Target:
    gen:    Callable[[int, random.Random], list[dict]]
    port:   Callable[[ctypes.CDLL, dict], dict]
    retail: Callable[[Any, dict], dict]
    diff:   Callable[[dict, dict], list[str]]

TARGETS: dict[str, Target] = {
    "rng_next15": Target(
        gen=gen_rng_next15_vectors,
        port=run_port_rng_next15,
        retail=run_retail_rng_next15,
        diff=diff_rng_next15),
    "audio_fade": Target(
        gen=gen_audio_fade_vectors,
        port=run_port_audio_fade,
        retail=run_retail_audio_fade,
        diff=diff_audio_fade),
}


# ─── result aggregator ─────────────────────────────────────────────────────

@dataclass
class DiffResult:
    target:   str
    total:    int
    passed:   int = 0
    failed:   int = 0
    failures: list[dict] = field(default_factory=list)

def run_target(name: str, target: Target,
               lib: ctypes.CDLL, script,
               n: int, rng: random.Random) -> DiffResult:
    vectors = target.gen(n, rng)
    r = DiffResult(target=name, total=len(vectors))
    for i, vec in enumerate(vectors):
        retail = target.retail(script, vec)
        port   = target.port(lib, vec)
        bad    = target.diff(retail, port)
        if bad:
            r.failed += 1
            r.failures.append({"idx": i, "vector": vec,
                               "retail": retail, "port": port,
                               "diff": bad})
        else:
            r.passed += 1
    return r

def print_summary(results: list[DiffResult]) -> int:
    rc = 0
    print("=" * 72)
    for r in results:
        status = "OK  " if r.failed == 0 else "FAIL"
        print(f"  {r.target:24s}  {status}  "
              f"{r.passed}/{r.total} pass, {r.failed} fail")
        if r.failed > 0:
            rc = 1
            for f in r.failures[:5]:
                print(f"    [{f['idx']:4d}] vector = {f['vector']}")
                print(f"           retail = {f['retail']}")
                print(f"           port   = {f['port']}")
                print(f"           diff   = {f['diff']}")
            if len(r.failures) > 5:
                print(f"    ... and {len(r.failures) - 5} more")
    print("=" * 72)
    return rc


# ─── frida session ─────────────────────────────────────────────────────────

def open_frida(remote: str, warmup_s: float):
    """Spawn retail CREATE_SUSPENDED, load agent in diff_test mode.
    Returns (device, pid, session, script).  Caller must run cleanup
    in a finally block."""
    dm = frida.get_device_manager()
    try:
        device = dm.add_remote_device(remote)
    except frida.InvalidArgumentError:
        device = dm.get_device(remote)

    try:
        _ = device.enumerate_processes()
    except frida.ServerNotRunningError as e:
        raise SystemExit(
            f"frida-server not reachable at {remote}: {e}\n"
            f"Start it on the Windows host "
            f"(see tools/frida_capture.py docstring).")

    win_exe = subprocess.run(
        ["wslpath", "-w", str(RETAIL_EXE)],
        capture_output=True, text=True, check=True).stdout.strip()
    win_cwd = subprocess.run(
        ["wslpath", "-w", str(ASSET_CWD)],
        capture_output=True, text=True, check=True).stdout.strip()

    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())

    ready_event = threading.Event()

    def on_message(message, data):
        if message.get("type") == "send":
            p = message.get("payload") or {}
            kind = p.get("kind")
            if kind == "log":
                print(f"  [agent] {p.get('msg','')}", file=sys.stderr)
            elif kind == "error":
                print(f"  [agent-error] {p.get('where','')}: "
                      f"{p.get('msg','')}", file=sys.stderr)
            elif kind == "ready":
                ready_event.set()
        elif message.get("type") == "error":
            print(f"  [frida-error] {message.get('description','')}",
                  file=sys.stderr)

    script.on("message", on_message)
    script.load()

    # diff_test: true forces install_hooks: false (the engine main
    # thread stays suspended; capture hooks would never fire anyway).
    script.exports_sync.init({"diff_test": True})

    # Wait for `ready` (typically <100 ms — agent runs in the Frida
    # helper thread, not gated on engine resume).
    if not ready_event.wait(timeout=warmup_s):
        raise SystemExit(
            f"[diff] agent did not send 'ready' within {warmup_s}s "
            f"— agent script load may have failed")

    return device, pid, session, script


# ─── main ──────────────────────────────────────────────────────────────────

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE,
        help="frida-server host:port (default %(default)s)")
    ap.add_argument("--functions",
        default=",".join(TARGETS.keys()),
        help="comma-separated target names (default: all). "
             f"Available: {', '.join(TARGETS.keys())}")
    ap.add_argument("--vectors", type=int, default=200,
        help="number of test vectors per target (default %(default)d). "
             "Always includes the per-target fixed-edge set.")
    ap.add_argument("--seed", type=lambda s: int(s, 0), default=0,
        help="vector-generator seed (default %(default)d). "
             "Accepts 0x-prefixed hex.")
    ap.add_argument("--warmup-s", type=float, default=10.0,
        help="seconds to wait for the agent 'ready' event "
             "(default %(default).1f)")
    args = ap.parse_args(argv)

    if not RETAIL_EXE.exists():
        raise SystemExit(
            f"retail exe missing: {RETAIL_EXE}\n"
            f"Steamless-unpack vendor/original/recettear.exe first.")

    enabled = [s.strip() for s in args.functions.split(",") if s.strip()]
    unknown = [n for n in enabled if n not in TARGETS]
    if unknown:
        raise SystemExit(
            f"unknown target(s): {', '.join(unknown)}\n"
            f"available: {', '.join(TARGETS.keys())}")
    if not enabled:
        raise SystemExit("no targets enabled")

    lib = load_port_lib()
    rng = random.Random(args.seed)

    device = pid = session = script = None
    try:
        device, pid, session, script = open_frida(
            args.frida_remote, args.warmup_s)

        results: list[DiffResult] = []
        for name in enabled:
            t0 = time.monotonic()
            print(f"[diff] running {name} (n={args.vectors}, "
                  f"seed=0x{args.seed:x})…", file=sys.stderr)
            r = run_target(name, TARGETS[name], lib, script,
                           args.vectors, rng)
            dt = time.monotonic() - t0
            print(f"[diff]   {name}: {r.passed}/{r.total} in {dt:.1f}s",
                  file=sys.stderr)
            results.append(r)

        return print_summary(results)
    finally:
        try:
            if script:  script.unload()
        except Exception:
            pass
        try:
            if session: session.detach()
        except Exception:
            pass
        try:
            if device and pid is not None: device.kill(pid)
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
