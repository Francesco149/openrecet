#!/usr/bin/env python3
"""
Quick probe: spawn retail, wait past config.idx parse, read the font
edge / kanjioff globals + the loaded face name and dilation params.

Usage:
    ./font_globals_probe.py [--frida-remote ...]
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

ROOT       = Path(__file__).resolve().parent.parent.parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

# globals
VAR_EDGEWI       = 0x005cbc74  # float — outline radius
VAR_EDGEDEL      = 0x005cbc78  # float — outline falloff
VAR_KANJIOFF     = 0x005cbc70  # u32 — 1 if config.idx had kanjioff:
VAR_EFFECTMODE   = 0x073dddb4  # u32 — 1 if config.idx had effectmode:
VAR_FONT_NAME    = 0x073de168  # char[256]
VAR_ATLAS_REGEN  = 0x073dfd00  # u32 — set when config.idx had font:

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frida-remote",
                    default=os.environ.get("OPENRECET_FRIDA_REMOTE",
                                           "127.0.0.1:27042"))
    args = ap.parse_args()

    dm = frida.get_device_manager()
    try:
        device = dm.add_remote_device(args.frida_remote)
    except frida.InvalidArgumentError:
        device = dm.get_device(args.frida_remote)

    win_exe = subprocess.run(["wslpath", "-w", str(RETAIL_EXE)],
                             capture_output=True, text=True, check=True).stdout.strip()
    win_cwd = subprocess.run(["wslpath", "-w", str(ASSET_CWD)],
                             capture_output=True, text=True, check=True).stdout.strip()

    pid = device.spawn([win_exe], cwd=win_cwd)
    session = device.attach(pid)
    script  = session.create_script(AGENT_JS.read_text())
    def on_msg(m, d):
        if m.get("type") == "send":
            p = m.get("payload") or {}
            if p.get("kind") == "log":
                print(f"  [agent] {p.get('msg','')}", file=sys.stderr)
    script.on("message", on_msg)
    script.load()
    script.exports_sync.init({"install_hooks": False})

    # Resume the engine so config.idx gets parsed. Then sleep + sample.
    device.resume(pid)
    print("# resumed retail, waiting 8s for boot to settle...")
    time.sleep(8.0)

    # Read the globals via the existing readMemory RPC.
    def read_u32(va):
        return int.from_bytes(bytes.fromhex(
            script.exports_sync.read_memory(va, 4)), 'little')
    def read_float(va):
        return struct.unpack('<f', bytes.fromhex(
            script.exports_sync.read_memory(va, 4)))[0]
    def read_str(va, n):
        b = bytes.fromhex(script.exports_sync.read_memory(va, n))
        return b.rstrip(b'\x00')

    print(f"  edgewi      (0x{VAR_EDGEWI:08x}) = {read_float(VAR_EDGEWI):.4f}")
    print(f"  edgedel     (0x{VAR_EDGEDEL:08x}) = {read_float(VAR_EDGEDEL):.4f}")
    print(f"  kanjioff    (0x{VAR_KANJIOFF:08x}) = {read_u32(VAR_KANJIOFF)}")
    print(f"  effectmode  (0x{VAR_EFFECTMODE:08x}) = {read_u32(VAR_EFFECTMODE)}")
    print(f"  atlas_regen (0x{VAR_ATLAS_REGEN:08x}) = {read_u32(VAR_ATLAS_REGEN)}")
    fn = read_str(VAR_FONT_NAME, 64)
    print(f"  font_name   (0x{VAR_FONT_NAME:08x}) = {fn.hex()} "
          f"({fn.decode('cp932', 'replace')!r})")

    try:
        device.kill(pid)
    except Exception:
        pass

if __name__ == "__main__":
    sys.exit(main() or 0)
