#!/usr/bin/env python3
"""
tools/state_diff/font_atlas_diff.py — font atlas differential vs retail.

Spawns vendor/unpacked/recettear.unpacked.exe under Frida, forces the
engine's atlas regen path (FUN_0047c474) via a state-forcing hook on
FUN_0047c228 that raises DAT_073dfd00 + writes a face name into
DAT_073de168. Lets the engine boot far enough to produce
fontdata.bin / fontidx.bin in retail's cwd, then byte-compares those
against our own ./font/fontdata.bin + fontidx.bin (produced by
src/font_atlas.c).

Differences here point straight at our atlas builder. A clean diff
means the bug is downstream (texture upload, draw_text, render math).

Approach: unlike the LCG / fade tests which never resume the main
thread, this one needs the engine to actually run through its boot
sequence so the regen call fires. We spawn → install the regen hook
→ resume → wait for both atlas files to appear in retail's cwd →
shut down retail → diff.

Exit code: 0 if the atlases match byte-for-byte. 1 if they differ.
2 if retail couldn't produce the atlas (timeout, missing files).

Usage:
    ./font_atlas_diff.py
    ./font_atlas_diff.py --frida-remote cutestation.soy:27042
    ./font_atlas_diff.py --face "ＭＳ Ｐゴシック"
    ./font_atlas_diff.py --keep-retail-atlas    # for manual inspection
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import frida


ROOT          = Path(__file__).resolve().parent.parent.parent.parent
AGENT_JS      = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE    = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD     = ROOT / "vendor" / "original"
OUR_FONT_DIR  = ASSET_CWD / "font"

# Where retail writes its atlas — relative to its cwd, which is ASSET_CWD.
RETAIL_FONTDATA = ASSET_CWD / "fontdata.bin"
RETAIL_FONTIDX  = ASSET_CWD / "fontidx.bin"

OUR_FONTDATA = OUR_FONT_DIR / "fontdata.bin"
OUR_FONTIDX  = OUR_FONT_DIR / "fontidx.bin"

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")

# SJIS encoding of "ＭＳ Ｐゴシック" — the engine's default Japanese font.
# Used by openrecet's font_atlas_build_win32 too; consistent across both
# sides of the diff.
DEFAULT_FACE_NAME = "ＭＳ Ｐゴシック"


def encode_face_hex(face: str) -> str:
    """Encode a Japanese face name to SJIS hex string for the agent."""
    return face.encode("cp932").hex()


def open_frida(remote: str):
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
    script  = session.create_script(AGENT_JS.read_text())

    def on_message(message, data):
        if message.get("type") == "send":
            p = message.get("payload") or {}
            kind = p.get("kind", "")
            if kind == "log":
                print(f"  [agent] {p.get('msg','')}", file=sys.stderr)
            elif kind == "error":
                print(f"  [agent-error] {p.get('where','')}: {p.get('msg','')}",
                      file=sys.stderr)
            elif kind in ("ready", "frame", "bgm_swap", "se_play", "input_state"):
                pass  # ignore capture-side events
            else:
                print(f"  [agent-msg] {p}", file=sys.stderr)
        elif message.get("type") == "error":
            print(f"  [frida-error] {message.get('description','')}",
                  file=sys.stderr)

    script.on("message", on_message)
    script.load()

    # install_hooks: false — we don't want Phase B's frame capture or
    # audio/input interceptors firing while the engine generates the
    # atlas. We do need state-forcing RPCs available.
    script.exports_sync.init({"install_hooks": False})

    return device, pid, session, script


def wait_for_atlas(timeout_s: float = 30.0) -> bool:
    """Poll the retail-side atlas files until both exist + stop growing."""
    deadline = time.time() + timeout_s
    last_sizes = (0, 0)
    stable_ticks = 0
    while time.time() < deadline:
        if RETAIL_FONTDATA.exists() and RETAIL_FONTIDX.exists():
            cur = (RETAIL_FONTDATA.stat().st_size,
                   RETAIL_FONTIDX.stat().st_size)
            if cur[0] > 0 and cur[1] > 0 and cur == last_sizes:
                stable_ticks += 1
                if stable_ticks >= 3:
                    return True  # two consecutive polls saw stable sizes
            else:
                stable_ticks = 0
                last_sizes = cur
        time.sleep(0.5)
    return False


def diff_files(retail: Path, ours: Path) -> tuple[bool, str]:
    """Byte-compare two files. Returns (match, description)."""
    if not retail.exists():
        return False, f"retail file missing: {retail}"
    if not ours.exists():
        return False, f"our file missing: {ours}"

    r_size = retail.stat().st_size
    o_size = ours.stat().st_size
    if r_size != o_size:
        return False, f"size mismatch: retail={r_size} ours={o_size}"

    r_bytes = retail.read_bytes()
    o_bytes = ours.read_bytes()
    if r_bytes == o_bytes:
        h = hashlib.sha256(r_bytes).hexdigest()
        return True, f"identical ({r_size} bytes, sha256={h[:16]}…)"

    # Find first divergence + a few divergence stats.
    first_diff = next((i for i, (a, b) in enumerate(zip(r_bytes, o_bytes))
                       if a != b), -1)
    diff_count = sum(1 for a, b in zip(r_bytes, o_bytes) if a != b)
    return False, (
        f"content mismatch: {diff_count}/{r_size} bytes differ, "
        f"first at offset 0x{first_diff:x} "
        f"(retail={r_bytes[first_diff]:02x} ours={o_bytes[first_diff]:02x})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frida-remote", default=DEFAULT_REMOTE,
                    help="frida-server addr (default: %(default)s)")
    ap.add_argument("--face", default=DEFAULT_FACE_NAME,
                    help="font face name to force regen with (default: MS PGothic)")
    ap.add_argument("--timeout", type=float, default=30.0,
                    help="seconds to wait for retail to write the atlas")
    ap.add_argument("--keep-retail-atlas", action="store_true",
                    help="don't delete retail's fontdata.bin/fontidx.bin after diff")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    # Pre-flight: our atlas must exist (regenerated by openrecet boot
    # with a `font:` config, or by first-time auto-regen since
    # ./font/fontdata.bin was missing).
    if not OUR_FONTDATA.exists() or not OUR_FONTIDX.exists():
        print(f"FAIL: our atlas missing at {OUR_FONT_DIR}", file=sys.stderr)
        print(f"      run `./build/openrecet.exe --max-duration-ms 3000` "
              f"from {ASSET_CWD} first.", file=sys.stderr)
        return 2

    # Clear any pre-existing retail-side atlas so we know the regen
    # actually fired this run.
    for p in (RETAIL_FONTDATA, RETAIL_FONTIDX):
        if p.exists():
            if args.verbose:
                print(f"  removing stale {p}")
            p.unlink()

    face_hex = encode_face_hex(args.face)
    print(f"# font_atlas_diff: face='{args.face}' "
          f"({len(face_hex)//2} bytes SJIS: {face_hex})")
    print(f"  remote = {args.frida_remote}")
    print(f"  retail cwd = {ASSET_CWD}")
    print(f"  ours    = {OUR_FONT_DIR}")

    device, pid, session, script = open_frida(args.frida_remote)
    print(f"  spawned retail pid {pid}")

    try:
        # Install the force-regen hook BEFORE resuming, so it fires the
        # first time the engine enters FUN_0047c228.
        script.exports_sync.force_atlas_regen(face_hex)
        device.resume(pid)
        print(f"  resumed — waiting up to {args.timeout:.0f}s for atlas...")

        ok = wait_for_atlas(timeout_s=args.timeout)
        if not ok:
            print("FAIL: retail didn't produce both atlas files in time",
                  file=sys.stderr)
            # Best-effort cleanup
            try:    device.kill(pid)
            except: pass
            return 2

        print(f"  retail atlas written: "
              f"fontdata.bin={RETAIL_FONTDATA.stat().st_size} bytes, "
              f"fontidx.bin={RETAIL_FONTIDX.stat().st_size} bytes")
    finally:
        try:
            device.kill(pid)
        except Exception:
            pass
        try:
            session.detach()
        except Exception:
            pass

    # ─── differential ───
    print()
    print("# diff: fontidx.bin")
    idx_match, idx_msg = diff_files(RETAIL_FONTIDX, OUR_FONTIDX)
    print(f"  {'PASS' if idx_match else 'FAIL'}: {idx_msg}")

    print()
    print("# diff: fontdata.bin")
    data_match, data_msg = diff_files(RETAIL_FONTDATA, OUR_FONTDATA)
    print(f"  {'PASS' if data_match else 'FAIL'}: {data_msg}")

    if not args.keep_retail_atlas:
        for p in (RETAIL_FONTDATA, RETAIL_FONTIDX):
            if p.exists():
                p.unlink()

    return 0 if (idx_match and data_match) else 1


if __name__ == "__main__":
    sys.exit(main())
