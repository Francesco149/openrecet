#!/usr/bin/env python3
"""
Spawn retail, force atlas regen, wait for boot to settle, then read
the engine's HDC + HFONT globals and probe GetTextFaceA + TextMetrics
externally via Frida's NativeFunction. Direct way to see what font
GDI actually selected without depending on hook-order timing.
"""
from __future__ import annotations
import argparse, os, struct, subprocess, sys, time
from pathlib import Path
import frida

ROOT       = Path(__file__).resolve().parent.parent.parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

POSTPROBE_JS = r"""
rpc.exports.probeFont = function (hdcVA, hfontVA) {
    const gdi = Process.getModuleByName('gdi32.dll');
    const getTextFaceA = new NativeFunction(
        gdi.getExportByName('GetTextFaceA'),
        'int', ['pointer', 'int', 'pointer'], 'stdcall');
    const getTextMetricsA = new NativeFunction(
        gdi.getExportByName('GetTextMetricsA'),
        'int', ['pointer', 'pointer'], 'stdcall');

    ensureBase();
    const hdc = rva(hdcVA).readPointer();
    const hfont = rva(hfontVA).readPointer();

    if (hdc.isNull()) {
        return {hdc: '0', hfont: '0', error: 'hdc is null'};
    }
    const buf = Memory.alloc(128);
    const n = getTextFaceA(hdc, 128, buf);
    const name = n > 0 ? buf.readCString(n) : '';

    // TEXTMETRIC ANSI — 57 bytes for the A variant.
    const tmBuf = Memory.alloc(64);
    getTextMetricsA(hdc, tmBuf);
    const tmHeight       = tmBuf.add(0).readS32();
    const tmAscent       = tmBuf.add(4).readS32();
    const tmDescent      = tmBuf.add(8).readS32();
    const tmCharSet      = tmBuf.add(48).readU8();
    const tmPitchFamily  = tmBuf.add(47).readU8();

    return {
        hdc:   '0x' + hdc.toInt32().toString(16),
        hfont: '0x' + hfont.toInt32().toString(16),
        face:  name,
        face_hex: name ? (name).split('').map(c =>
            c.charCodeAt(0).toString(16).padStart(2, '0')).join('') : '',
        tmHeight: tmHeight,
        tmAscent: tmAscent,
        tmDescent: tmDescent,
        tmCharSet: tmCharSet,
        tmPitchFamily: '0x' + tmPitchFamily.toString(16),
    };
};
"""

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frida-remote",
                    default=os.environ.get("OPENRECET_FRIDA_REMOTE",
                                           "cutestation.soy:27042"))
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

    composite = AGENT_JS.read_text() + "\n\n// ─── postprobe ──\n" + POSTPROBE_JS
    script = session.create_script(composite)
    def on_msg(m, d):
        if m.get("type") == "send":
            p = m.get("payload") or {}
            if p.get("kind") == "log":
                print(f"  [agent] {p.get('msg','')}", file=sys.stderr)
    script.on("message", on_msg)
    script.load()
    script.exports_sync.init({"install_hooks": False})

    face_hex = "ＭＳ Ｐゴシック".encode("cp932").hex()
    script.exports_sync.force_atlas_regen(face_hex)
    device.resume(pid)
    print("# resumed, waiting 8s for atlas regen to complete...")
    time.sleep(8.0)

    # Engine globals:
    #   DAT_073dde34 — HDC (set by FUN_0047c474 GetDC(NULL))
    #   DAT_073de62c — HFONT (CreateFontIndirectA return)
    print()
    print("# probing retail's font globals:")
    result = script.exports_sync.probe_font(0x073dde34, 0x073de62c)
    for k, v in result.items():
        print(f"  {k:>16}: {v}")

    try:
        device.kill(pid)
    except Exception:
        pass

if __name__ == "__main__":
    sys.exit(main() or 0)
