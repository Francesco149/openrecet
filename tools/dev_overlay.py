#!/usr/bin/env python3
"""
tools/dev_overlay.py — turn ON the retail engine's hidden developer overlay
on a RUNNING, interactively-playable Recettear, so you can record clips.

The overlay (player X/Y/Z coords, the per-frame RNG `%d` readout, event-script
+ message counters, the `muteki` godmode flag, free-texture counts, a tile-grid)
is fully wired in the retail binary but gated behind `DAT_06a49938 == 1`, a
debug-menu activation flag that is BSS-zero in normal play and has no input path
(engine-quirks §95, FUN_00451ea7 / FUN_00442cef).

This tool ATTACHES to a process you launched yourself (do NOT Frida-*spawn* it —
a spawned engine can't take your keyboard input, see the project notes) and holds
that flag at 1 every 250 ms.  Leave it running while you play + record; Ctrl-C to
stop (the flag clears on its own once we stop writing, or just close the game).

Typical use:
  1. Launch Recettear normally (Steam → Play, the real `recettear.exe`).
  2. nix develop --command python3 tools/dev_overlay.py
  3. Play + record.  Ctrl-C here when done.

Notes:
  - Default image auto-detects: prefers `recettear.exe` (the Steam build), falls
    back to `recettear.unpacked.exe` (the Steamless dump).  Override with --image.
  - The globals sit at a fixed 0x400000 imagebase in both builds (no ASLR), so the
    same VA works whether you run the Steam exe or the unpacked dump.
  - --full also sets DAT_06a4993c=1 (the verbose debug-menu dump: more rows + the
    hex tables).  Omit for just the coord/rng/status overlay.
"""

from __future__ import annotations

import argparse
import os
import sys
import time

DEFAULT_REMOTE = os.environ.get("OPENRECET_FRIDA_REMOTE", "cutestation.soy:27042")
GATE_VA   = 0x06A49938   # DAT_06a49938 — debug-menu activation gate (overlay render)
VERBOSE_VA = 0x06A4993C  # DAT_06a4993c — full debug-dump branch (== 1)
IMAGEBASE = 0x00400000

# Injected agent: resolve the main module base, then hold the requested debug
# flags at 1 on a timer so the engine never renders a frame with them cleared.
AGENT_JS = r"""
const GATE = %d, VERBOSE = %d, IMAGEBASE = 0x400000, FULL = %s;
function mainBase() {
    // The exe is the first enumerated module; match by name to be safe.
    const mods = Process.enumerateModules();
    for (const m of mods) {
        if (/recettear/i.test(m.name)) return m.base;
    }
    return mods[0].base;  // fallback: first module is the exe
}
const base = mainBase();
const gatePtr    = base.add(GATE - IMAGEBASE);
const verbosePtr = base.add(VERBOSE - IMAGEBASE);
function hold() {
    try {
        gatePtr.writeU32(1);
        if (FULL) verbosePtr.writeU32(1);
    } catch (e) { /* transient unmap — try again next tick */ }
}
hold();
setInterval(hold, 250);
send({kind: 'armed', base: base.toString(),
      gate: gatePtr.toString(), full: FULL});
"""


def frida_device(remote: str):
    import frida
    dm = frida.get_device_manager()
    try:
        return dm.add_remote_device(remote)
    except frida.InvalidArgumentError:
        return dm.get_device(remote)


def pids_by_name(image: str) -> list[int]:
    import subprocess
    try:
        r = subprocess.run(
            ["/mnt/c/Windows/system32/tasklist.exe",
             "/fi", f"imagename eq {image}", "/fo", "csv", "/nh"],
            capture_output=True, text=True, timeout=10)
    except Exception as e:  # noqa: BLE001
        print(f"error: tasklist failed: {e}", file=sys.stderr)
        return []
    out = []
    for line in r.stdout.splitlines():
        parts = [p.strip('"') for p in line.split(",")]
        if len(parts) >= 2:
            try:
                out.append(int(parts[1]))
            except ValueError:
                pass
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="enable retail's dev overlay (attach)")
    ap.add_argument("--remote", default=DEFAULT_REMOTE)
    ap.add_argument("--image", default=None,
                    help="process image name (default: auto recettear.exe → "
                         "recettear.unpacked.exe)")
    ap.add_argument("--pid", type=int, default=None,
                    help="attach to this PID directly (skips name lookup)")
    ap.add_argument("--full", action="store_true",
                    help="also set DAT_06a4993c=1 (verbose debug-menu dump)")
    args = ap.parse_args()

    import frida
    dev = frida_device(args.remote)

    if args.pid is not None:
        pid = args.pid
    else:
        images = [args.image] if args.image else \
                 ["recettear.exe", "recettear.unpacked.exe"]
        pid = None
        for img in images:
            pids = pids_by_name(img)
            if pids:
                pid = pids[0]
                print(f"dev_overlay: attaching to {img} pid {pid}"
                      f"{' (+%d more)' % (len(pids)-1) if len(pids) > 1 else ''}")
                break
        if pid is None:
            print("dev_overlay: no running Recettear found. Launch the game "
                  "first (Steam → Play), then re-run.", file=sys.stderr)
            return 1

    session = dev.attach(pid)
    js = AGENT_JS % (GATE_VA, VERBOSE_VA, "true" if args.full else "false")
    script = session.create_script(js)

    def on_message(msg, _data):
        if msg.get("type") == "send":
            p = msg["payload"]
            if p.get("kind") == "armed":
                print(f"dev_overlay: ARMED — module base {p['base']}, "
                      f"gate @ {p['gate']} held at 1"
                      f"{' + verbose dump' if p.get('full') else ''}.")
                print("dev_overlay: overlay is ON. Play + record. "
                      "Ctrl-C here to stop holding the flag.")
        elif msg.get("type") == "error":
            print(f"dev_overlay: script error: {msg.get('description')}",
                  file=sys.stderr)

    script.on("message", on_message)
    script.load()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\ndev_overlay: detaching (flag stops being held).")
        try:
            session.detach()
        except Exception:  # noqa: BLE001
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
