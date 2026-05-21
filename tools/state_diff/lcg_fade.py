#!/usr/bin/env python3
"""
tools/state_diff/lcg_fade.py — first state-forcing differential test.

Compares two pure-math subsystems in the retail unpacked exe against
our own ports:

  - LCG step (FUN_005041f6 / DAT_006023a0)        → src/rng.c
  - BGM cos-curve fade (FUN_00499583)             → src/audio_fade.c

Approach: spawn vendor/unpacked/recettear.unpacked.exe under Frida in
CREATE_SUSPENDED state and never resume the main thread. The agent runs
in a Frida-injected helper thread that lives independently of the main
thread, so it can invoke NativeFunction calls + read/write process
memory without the engine's own code executing at all. That means no
races against engine RNG consumers (FUN_00451790 particle init) or
against the audio backend's own SetVolume calls.

For each subsystem:

  LCG
    - For each test seed: write DAT_006023a0, call FUN_005041f6 N times,
      record the post-step seed after each call. Run the same seed
      against our oracle binary (which links src/rng.c) for N steps.
      Diff the two sequences.
    - On a host with matching MSVC LCG semantics, both sequences must
      be bit-identical (the LCG is a single 32-bit imul + adds — no
      floating-point, no platform variation).

  Audio fade
    - For each slider value 0..9, call the agent's capture helper which
      installs a fake AudioPath whose vtable[5] (SetVolume) records the
      centibel before the engine would otherwise apply it. Compare to
      our audio_fade_compute(slider, 0).
    - cos() implementations differ across libm vs MSVC CRT, so this is
      NOT bit-exact. Tolerance is ±1 centibel (1/100 dB; DirectMusic's
      SetVolume granularity).

Exit code: 0 on all-pass, 1 on any divergence.

Usage:
    ./lcg_fade.py
    ./lcg_fade.py --frida-remote cutestation.soy:27042
    ./lcg_fade.py --verbose       # print full sequences on success too
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import frida


ROOT       = Path(__file__).resolve().parent.parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"
ORACLE_BIN = Path(__file__).resolve().parent / "build" / "oracle"

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "127.0.0.1:27042")


# ─── oracle wrapper ───────────────────────────────────────────────────────


class Oracle:
    """Long-running subprocess wrapping tools/state_diff/build/oracle."""

    def __init__(self, exe: Path = ORACLE_BIN):
        if not exe.exists():
            raise SystemExit(
                f"oracle binary missing: {exe}\n"
                f"Build with: nix develop --command make -C tools/state_diff")
        self._p = subprocess.Popen(
            [str(exe)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1)

    def _send(self, line: str) -> None:
        assert self._p.stdin is not None
        self._p.stdin.write(line + "\n")
        self._p.stdin.flush()

    def _readline(self) -> str:
        assert self._p.stdout is not None
        s = self._p.stdout.readline()
        if not s:
            raise RuntimeError("oracle closed stdout unexpectedly")
        return s.rstrip("\n")

    def rng_seq(self, seed: int, n: int) -> list[int]:
        self._send(f"rng_seq {seed:08x} {n}")
        return [int(self._readline(), 16) for _ in range(n)]

    def fade_compute(self, slider: int) -> int:
        self._send(f"fade_compute {slider}")
        return int(self._readline(), 10)

    def close(self) -> None:
        try:
            self._send("quit")
        except Exception:
            pass
        try:
            self._p.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            self._p.kill()


# ─── frida session ────────────────────────────────────────────────────────


def open_frida(remote: str):
    """Return (device, pid, session, script) — process spawned suspended."""
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
            f"Start it on the Windows host (see tools/frida_capture.py docstring).")

    win_exe = subprocess.run(
        ["wslpath", "-w", str(RETAIL_EXE)],
        capture_output=True, text=True, check=True).stdout.strip()
    win_cwd = subprocess.run(
        ["wslpath", "-w", str(ASSET_CWD)],
        capture_output=True, text=True, check=True).stdout.strip()

    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script = session.create_script(AGENT_JS.read_text())

    def on_message(message, data):
        if message.get("type") == "send":
            p = message.get("payload") or {}
            if p.get("kind") == "log":
                print(f"  [agent] {p.get('msg','')}", file=sys.stderr)
            elif p.get("kind") == "error":
                print(f"  [agent-error] {p.get('where','')}: {p.get('msg','')}",
                      file=sys.stderr)
            elif p.get("kind") == "ready":
                pass
            else:
                print(f"  [agent-msg] {p}", file=sys.stderr)
        elif message.get("type") == "error":
            print(f"  [frida-error] {message.get('description','')}",
                  file=sys.stderr)

    script.on("message", on_message)
    script.load()

    # install_hooks: false → don't attach the D3D/audio/input interceptors.
    # We never resume the main thread, so they'd never fire anyway, but
    # leaving them off keeps the agent's surface minimal and avoids any
    # confusion if something does run.
    script.exports_sync.init({"install_hooks": False})

    return device, pid, session, script


# ─── tests ────────────────────────────────────────────────────────────────


# Seeds chosen to exercise: default boot seed (1), arbitrary mid-range
# (12345), high-bit-set values that would expose any signed/unsigned
# slip (0x80000000, 0xfffffff0), zero (degenerate fixpoint avoidance —
# the LCG's add constant prevents zero from being absorbing).
LCG_SEEDS = [0x00000001, 0x00003039, 0xdeadbeef, 0x80000000, 0xfffffff0, 0x00000000]
LCG_STEPS = 256


def test_lcg(script, oracle: Oracle, verbose: bool) -> tuple[int, int]:
    """Returns (pass_count, fail_count)."""
    VAR_SEED = 0x006023a0  # DAT_006023a0
    FN_STEP  = 0x005041f6  # FUN_005041f6

    passed = failed = 0
    print(f"\n# LCG step (FUN_005041f6, DAT_006023a0)")
    for seed in LCG_SEEDS:
        # Force seed on the retail side, then step LCG_STEPS times.
        script.exports_sync.write_u32(VAR_SEED, seed)
        seen = []
        for _ in range(LCG_STEPS):
            script.exports_sync.call_u32_no_args(FN_STEP)
            seen.append(script.exports_sync.read_u32(VAR_SEED) & 0xffffffff)

        # Same on our side.
        ours = oracle.rng_seq(seed, LCG_STEPS)

        ok = (seen == ours)
        if ok:
            passed += 1
            print(f"  pass seed=0x{seed:08x}  ({LCG_STEPS} steps bit-exact)")
            if verbose:
                preview = ", ".join(f"0x{x:08x}" for x in seen[:6])
                print(f"        first 6: {preview} …")
        else:
            failed += 1
            # Find the first divergence and print a small window around it.
            first = next((i for i in range(LCG_STEPS) if seen[i] != ours[i]), -1)
            print(f"  FAIL seed=0x{seed:08x}  first divergence at step {first}")
            lo = max(0, first - 2)
            hi = min(LCG_STEPS, first + 3)
            for i in range(lo, hi):
                marker = "  *" if i == first else "   "
                print(f"      {marker} step {i:3d}  retail=0x{seen[i]:08x}  "
                      f"ours=0x{ours[i]:08x}")
    return passed, failed


SLIDER_RANGE = list(range(0, 10))   # 0..9 inclusive
FADE_TOLERANCE_CENTIBEL = 1         # ±1 centibel = 1/100 dB


def test_fade(script, oracle: Oracle, verbose: bool) -> tuple[int, int]:
    passed = failed = 0
    print(f"\n# BGM fade curve (FUN_00499583)")
    for slider in SLIDER_RANGE:
        r = script.exports_sync.capture_fade_centibel(slider)
        retail_cb = int(r["centibel"])
        calls     = int(r["calls"])
        ours_cb   = oracle.fade_compute(slider)
        delta     = retail_cb - ours_cb

        # The frame-0 hard-silence path doesn't go through cos at all,
        # so it must be exactly -10000 on both sides. Frames 1..9 allow
        # ±1 centibel slack for cos-implementation differences.
        if slider == 0:
            ok = (calls == 1 and retail_cb == -10000 and ours_cb == -10000)
        else:
            ok = (calls == 1 and abs(delta) <= FADE_TOLERANCE_CENTIBEL)

        if ok:
            passed += 1
            print(f"  pass slider={slider}  retail={retail_cb:6d}  "
                  f"ours={ours_cb:6d}  Δ={delta:+d}cb  calls={calls}")
        else:
            failed += 1
            print(f"  FAIL slider={slider}  retail={retail_cb:6d}  "
                  f"ours={ours_cb:6d}  Δ={delta:+d}cb  calls={calls}  "
                  f"(tolerance ±{FADE_TOLERANCE_CENTIBEL}cb"
                  f"{', exact at slider 0' if slider == 0 else ''})")
    return passed, failed


# ─── main ─────────────────────────────────────────────────────────────────


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE,
                    help="frida-server host:port (default %(default)s)")
    ap.add_argument("--verbose", action="store_true",
                    help="print full sequences even when tests pass")
    ap.add_argument("--skip-lcg",  action="store_true")
    ap.add_argument("--skip-fade", action="store_true")
    args = ap.parse_args(argv)

    if not RETAIL_EXE.exists():
        raise SystemExit(f"retail exe missing: {RETAIL_EXE}\n"
                         "Steamless-unpack vendor/original/recettear.exe first.")

    oracle = Oracle()
    device = pid = session = script = None
    try:
        device, pid, session, script = open_frida(args.frida_remote)

        total_pass = total_fail = 0
        if not args.skip_lcg:
            p, f = test_lcg(script, oracle, args.verbose); total_pass += p; total_fail += f
        if not args.skip_fade:
            p, f = test_fade(script, oracle, args.verbose); total_pass += p; total_fail += f

        print(f"\n{total_pass} passed, {total_fail} failed")
        return 0 if total_fail == 0 else 1
    finally:
        oracle.close()
        try:
            if script:  script.unload()
        except Exception:
            pass
        try:
            if session: session.detach()
        except Exception:
            pass
        try:
            if device and pid: device.kill(pid)
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())
