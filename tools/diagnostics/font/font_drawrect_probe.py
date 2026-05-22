#!/usr/bin/env python3
"""
font_drawrect_probe.py — capture the actual dst rect passed by the engine's
FUN_0047ca05 (draw_text) to FUN_00404efc (render_quad_add), to verify the
hypothesis that engine renders a fixed (cell_inc_x × 42)*fVar2 quad rather
than a per-glyph (tex_w × tex_h)*fVar2 one.

We filter by src=(1.0, 1.0, 41.0, 41.0) which is the unique-to-draw_text
constant block referenced in FUN_0047ca05.

Spawns retail, waits past title boot, then logs N draw_text calls. We
also dump the texture pointer to correlate calls to slots; the slot's
piVar4[0] (=cell_inc_x) and piVar4[1] (=effective_width) lets us match
to fontidx records.

Usage:
    nix develop --command python3 tools/diagnostics/font/font_drawrect_probe.py \\
        --frida-remote cutestation.soy:27042 [--count 80] [--settle-s 12]
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
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

# Engine VAs (preferred ImageBase 0x00400000, no ASLR)
VA_DRAW_RECT     = 0x00404efc   # FUN_00404efc — the inner quad-add
VA_DRAW_TEXT     = 0x0047ca05   # FUN_0047ca05 — draw_text body
VA_DRAW_TEXT_END = 0x0047ca05 + 0x1c6   # end of FUN_0047ca05
VA_DAT_073DDE44  = 0x073dde44   # texture-pointer table base

PROBE_JS2 = r"""
'use strict';
const MODULE_NAME = 'recettear.unpacked.exe';
const IMAGE_BASE = ptr('0x00400000');
let g_base = null;
function rva(va) { return g_base.add(va - IMAGE_BASE.toUInt32()); }

const VA_FONT_ALLOC = 0x0047cbcb;
const VA_DRAW_TEXT  = 0x0047ca05;
const VA_DRAW_TEXT_END = 0x0047ca05 + 0x1c6;

let g_logged = 0;
let g_limit = 80;

rpc.exports = {
    init(config) {
        config = config || {};
        g_limit = (config.limit | 0) || 80;
        const mod = Process.findModuleByName(MODULE_NAME);
        if (!mod) throw new Error('module not loaded: ' + MODULE_NAME);
        g_base = mod.base;
        send({kind: 'log', msg: 'base=' + g_base});

        Interceptor.attach(rva(VA_FONT_ALLOC), {
            onEnter(args) {
                if (g_logged >= g_limit) return;
                const ret = this.returnAddress.toUInt32();
                const baseu = g_base.toUInt32();
                const retVa = ret - baseu + IMAGE_BASE.toUInt32();
                if (retVa < VA_DRAW_TEXT || retVa >= VA_DRAW_TEXT_END) return;
                this._cap = true;
                this._pcVar7 = args[0];
                const b0 = args[0].readU8();
                this._b0 = b0;
                if (b0 >= 0x80) this._b1 = args[0].add(1).readU8();
                else            this._b1 = 0;
            },
            onLeave(retval) {
                if (!this._cap) return;
                if (retval.isNull()) return;
                const piVar4 = retval;
                const cell_inc_x = piVar4.add(0).readU32();
                const eff_width  = piVar4.add(4).readU32();
                const tex_slot   = piVar4.add(12).readU32();
                send({kind: 'alloc',
                      idx: g_logged,
                      cp: [this._b0, this._b1],
                      ch: this._b0 < 0x80 && this._b0 >= 0x20 ? String.fromCharCode(this._b0) : '?',
                      cell_inc_x: cell_inc_x,
                      eff_width: eff_width,
                      tex_slot: tex_slot});
                g_logged++;
            }
        });
        send({kind: 'log', msg: 'font_alloc hook installed; limit=' + g_limit});
    }
};
"""

PROBE_JS = r"""
'use strict';
const MODULE_NAME = 'recettear.unpacked.exe';
const IMAGE_BASE = ptr('0x00400000');
let g_base = null;
function rva(va) { return g_base.add(va - IMAGE_BASE.toUInt32()); }

const VA_DRAW_RECT     = 0x00404efc;
const VA_DRAW_TEXT     = 0x0047ca05;
const VA_DRAW_TEXT_END = 0x0047ca05 + 0x1c6;
const VA_TEXTURE_TABLE = 0x073dde44;

const SRC_MATCH = [1.0, 1.0, 41.0, 41.0];

let g_logged = 0;
let g_limit = 80;

rpc.exports = {
    init(config) {
        config = config || {};
        g_limit = (config.limit | 0) || 80;

        const mod = Process.findModuleByName(MODULE_NAME);
        if (!mod) throw new Error('module not loaded: ' + MODULE_NAME);
        g_base = mod.base;
        send({kind: 'log', msg: 'base=' + g_base});

        Interceptor.attach(rva(VA_DRAW_RECT), {
            onEnter(args) {
                if (g_logged >= g_limit) return;
                // Check caller: is it FUN_0047ca05?
                const ret = this.returnAddress.toUInt32();
                const baseu = g_base.toUInt32();
                const retVa = ret - baseu + IMAGE_BASE.toUInt32();
                if (retVa < VA_DRAW_TEXT || retVa >= VA_DRAW_TEXT_END) return;

                const p_dst = args[0];
                const p_src = args[1];
                const sx0 = p_src.add(0).readFloat();
                const sy0 = p_src.add(4).readFloat();
                const sx1 = p_src.add(8).readFloat();
                const sy1 = p_src.add(12).readFloat();
                if (sx0 !== SRC_MATCH[0] || sy0 !== SRC_MATCH[1] ||
                    sx1 !== SRC_MATCH[2] || sy1 !== SRC_MATCH[3]) return;

                const dx = p_dst.add(0).readFloat();
                const dy = p_dst.add(4).readFloat();
                const dw = p_dst.add(8).readFloat();
                const dh = p_dst.add(12).readFloat();

                // arg2 is the &DAT_073b18b8 tex-size block; arg3 is diffuse
                const p_dim = args[2];
                const tw = p_dim.add(4).readU32();
                const th = p_dim.add(8).readU32();

                send({kind: 'draw',
                      idx: g_logged,
                      caller_va: '0x' + retVa.toString(16),
                      dst: [dx, dy, dw, dh],
                      tex_dim: [tw, th]});
                g_logged++;
                if (g_logged === g_limit) {
                    send({kind: 'log', msg: 'limit reached, detaching'});
                }
            }
        });
        send({kind: 'log', msg: 'hooks installed; limit=' + g_limit});
    }
};
"""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frida-remote",
                    default=os.environ.get("OPENRECET_FRIDA_REMOTE",
                                           "127.0.0.1:27042"))
    ap.add_argument("--count", type=int, default=80,
                    help="number of draw calls to log")
    ap.add_argument("--settle-s", type=float, default=12.0,
                    help="seconds to wait after resume before reading the log")
    ap.add_argument("--mode", choices=["drawrect", "alloc"], default="drawrect",
                    help="drawrect = hook FUN_00404efc; alloc = hook FUN_0047cbcb to see stored cell_inc_x")
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
    script = session.create_script(PROBE_JS2 if args.mode == "alloc" else PROBE_JS)

    rows = []
    last_seen = [0]
    def on_msg(m, d):
        if m.get("type") != "send": return
        p = m.get("payload") or {}
        k = p.get("kind")
        if k == "log":
            print(f"  [agent] {p.get('msg')}", file=sys.stderr)
        elif k == "draw":
            rows.append(p)
            dx, dy, dw, dh = p["dst"]
            tw, th = p["tex_dim"]
            print(f"  [draw {p['idx']:>3}] dst=({dx:>7.2f},{dy:>7.2f},{dw:>7.2f},{dh:>7.2f}) tex=({tw},{th})", file=sys.stderr)
            last_seen[0] = time.monotonic()
        elif k == "alloc":
            rows.append(p)
            cp = p["cp"]; ch = p["ch"]
            print(f"  [alloc {p['idx']:>3}] cp=({cp[0]:#04x},{cp[1]:#04x}) ch='{ch}'  cell_inc_x={p['cell_inc_x']:>3}  eff={p['eff_width']:>3}  slot={p['tex_slot']}", file=sys.stderr)
            last_seen[0] = time.monotonic()

    script.on("message", on_msg)
    script.load()
    script.exports_sync.init({"limit": args.count})

    device.resume(pid)
    print(f"# resumed retail; navigate to settings panel (Options → A) within {args.settle_s}s", file=sys.stderr)
    print(f"# probe streams draws live; will exit after settle_s OR {args.count} draws", file=sys.stderr)
    deadline = time.monotonic() + args.settle_s
    while time.monotonic() < deadline and len(rows) < args.count:
        time.sleep(0.5)

    # Try to detach cleanly
    try:
        session.detach()
    except Exception:
        pass
    try:
        device.kill(pid)
    except Exception:
        pass

    print(f"# captured {len(rows)} draw calls")
    print(f"# {'idx':>3} {'dst.x':>8} {'dst.y':>8} {'dst.w':>8} {'dst.h':>8} {'tex_w':>6} {'tex_h':>6}")
    for r in rows:
        dx, dy, dw, dh = r["dst"]
        tw, th = r["tex_dim"]
        print(f"  {r['idx']:>3} {dx:>8.3f} {dy:>8.3f} {dw:>8.3f} {dh:>8.3f} {tw:>6d} {th:>6d}")

    if rows:
        # Aggregate dst.h to verify "always 42*fVar2"
        h_values = set(round(r["dst"][3], 3) for r in rows)
        print(f"\n# distinct dst.h values: {sorted(h_values)}")
        w_values = sorted(set(round(r["dst"][2], 3) for r in rows))
        print(f"# distinct dst.w values: {w_values[:20]}{'...' if len(w_values) > 20 else ''}  (total {len(w_values)})")
        tex_dims = sorted(set((r["tex_dim"][0], r["tex_dim"][1]) for r in rows))
        print(f"# distinct (tex_w, tex_h): {tex_dims[:8]}{'...' if len(tex_dims) > 8 else ''}  (total {len(tex_dims)})")


if __name__ == "__main__":
    main()
