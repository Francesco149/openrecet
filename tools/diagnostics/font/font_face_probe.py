#!/usr/bin/env python3
"""
Probe what GDI font CreateFontIndirectA actually selects in retail's
process after the atlas-regen hook fires. Hooks GetTextFaceA which
returns the resolved face name (post-substitution) — that tells us
whether retail and openrecet are both getting MS PGothic, or if one
of them is falling through to a Latin substitute.

Usage:
    ./font_face_probe.py [--frida-remote ...]
"""
from __future__ import annotations
import argparse, os, subprocess, sys, time
from pathlib import Path
import frida

ROOT       = Path(__file__).resolve().parent.parent.parent.parent
AGENT_JS   = ROOT / "tools" / "frida" / "openrecet-agent.js"
RETAIL_EXE = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
ASSET_CWD  = ROOT / "vendor" / "original"

# Custom probe script — injected on top of the agent. Hooks
# GetTextFaceA in gdi32 to capture the actual selected face name
# whenever the engine queries it.
PROBE_JS = r"""
try {
send({kind: 'log', msg: 'probe overlay starting'});
const gdiMod = Process.getModuleByName('gdi32.dll');
send({kind: 'log', msg: 'gdi32.dll @ ' + gdiMod.base});
const gdi = gdiMod.getExportByName('GetTextFaceA');
if (!gdi) {
    send({kind: 'log', msg: 'GetTextFaceA export not found'});
} else {
    Interceptor.attach(gdi, {
        onLeave: function (retval) {
            // args: hdc, count, lpString — captured at entry below
            const r = retval.toInt32();
            if (r > 0 && this.lp) {
                const name = this.lp.readCString();
                send({kind: 'log',
                      msg: 'GetTextFaceA -> ' + JSON.stringify(name)});
            }
        },
        onEnter: function (args) {
            this.lp = args[2];
        }
    });
    send({kind: 'log', msg: 'GetTextFaceA hook installed'});
}

// Also hook CreateFontIndirectA to log what LOGFONTA face name + key
// fields the engine is requesting.
const cfi = gdiMod.getExportByName('CreateFontIndirectA');
if (cfi) {
    Interceptor.attach(cfi, {
        onEnter: function (args) {
            const lf = args[0];
            // LOGFONTA layout: lfHeight @ 0, lfWeight @ 16,
            // lfCharSet @ 23, lfOutPrecision @ 24, lfClipPrecision @ 25,
            // lfQuality @ 26, lfPitchAndFamily @ 27, lfFaceName @ 28
            const height = lf.add(0).readS32();
            const weight = lf.add(16).readS32();
            const charset = lf.add(23).readU8();
            const outPrec = lf.add(24).readU8();
            const quality = lf.add(26).readU8();
            const pitchFamily = lf.add(27).readU8();
            const faceName = lf.add(28).readCString(32);
            send({kind: 'log',
                  msg: 'CreateFontIndirectA: h=' + height + ' w=' + weight +
                       ' cs=0x' + charset.toString(16) +
                       ' op=' + outPrec + ' q=' + quality +
                       ' pf=0x' + pitchFamily.toString(16) +
                       ' face=' + JSON.stringify(faceName)});
        }
    });
    send({kind: 'log', msg: 'CreateFontIndirectA hook installed'});
}

// SelectObject — when the engine selects our HFONT into the DC, call
// GetTextFaceA to see what GDI ended up picking after substitution.
// We watch ALL SelectObjects but only probe when the second arg matches
// the HFONT returned by CreateFontIndirectA.
const so = gdiMod.getExportByName('SelectObject');
const gtfA = gdiMod.getExportByName('GetTextFaceA');
const fnGetTextFaceA = new NativeFunction(gtfA, 'int',
    ['pointer','int','pointer']);
let g_hfont = ptr(0);
// Augment the existing CreateFontIndirectA hook to capture the HFONT
// it returns.
const cfi2 = gdiMod.getExportByName('CreateFontIndirectA');
Interceptor.attach(cfi2, {
    onLeave: function (retval) {
        g_hfont = retval;
        send({kind: 'log',
              msg: 'CreateFontIndirectA returned HFONT=' + retval});
    }
});
if (so) {
    let soCallCount = 0;
    Interceptor.attach(so, {
        onEnter: function (args) {
            this.hdc = args[0];
            this.gdiobj = args[1];
            soCallCount++;
            // Log SelectObjects that happen AFTER the engine got an HFONT.
            if (!g_hfont.isNull() && soCallCount < 100) {
                const isOurs = this.gdiobj.equals(g_hfont) ? ' MATCH' : '';
                send({kind: 'log',
                      msg: 'SO[' + soCallCount + '] hdc=' + this.hdc +
                           ' obj=' + this.gdiobj + isOurs});
            }
        },
        onLeave: function (retval) {
            if (g_hfont.isNull() || !this.gdiobj.equals(g_hfont)) return;
            const buf = Memory.alloc(128);
            const n = fnGetTextFaceA(this.hdc, 128, buf);
            if (n > 0) {
                const name = buf.readCString(n);
                send({kind: 'log',
                      msg: 'GDI selected face = ' + JSON.stringify(name) +
                           ' (n=' + n + ') after SelectObject of HFONT=' +
                           g_hfont});
            }
        }
    });
}

// GetGlyphOutlineA — capture the requested codepoint + returned size
// for the first ~50 calls + a few diagnostic codepoints later in the
// walk where ours and retail diverge.
const DIAG_CPS = [0x42, 0x44, 0x69, 0x8140, 0x9540, 0x9580, 0x8893];
const ggo = gdiMod.getExportByName('GetGlyphOutlineA');
if (ggo) {
    let callCount = 0;
    Interceptor.attach(ggo, {
        onEnter: function (args) {
            this.cp = args[1].toInt32();
            this.fmt = args[2].toInt32();
            this.gm = args[3];
            this.size = args[4].toInt32();
            this.buf = args[5];
        },
        onLeave: function (retval) {
            const want = callCount < 50 || DIAG_CPS.indexOf(this.cp) >= 0;
            if (want) {
                const r = retval.toInt32();
                let bbX = 0, bbY = 0, incX = 0;
                if (this.gm && r !== -1) {
                    bbX = this.gm.add(0).readU32();
                    bbY = this.gm.add(4).readU32();
                    incX = this.gm.add(16).readS16();
                }
                send({kind: 'log',
                      msg: 'GGO cp=0x' + this.cp.toString(16) +
                           ' fmt=' + this.fmt +
                           ' size=' + this.size +
                           ' ret=' + r +
                           ' bb=' + bbX + 'x' + bbY +
                           ' inc=' + incX});
                callCount++;
                if (callCount === 50) {
                    send({kind: 'log', msg: '... GGO logging capped at 50'});
                }
            }
        }
    });
    send({kind: 'log', msg: 'GetGlyphOutlineA hook installed'});
}
} catch (e) {
    send({kind: 'log', msg: 'probe overlay error: ' + e.message});
}
"""


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

    # Build composite script: agent + probe overlay.
    composite = AGENT_JS.read_text() + "\n\n// ─── probe ──────\n" + PROBE_JS
    script = session.create_script(composite)
    def on_msg(m, d):
        if m.get("type") == "send":
            p = m.get("payload") or {}
            if p.get("kind") == "log":
                print(f"  {p.get('msg','')}", file=sys.stderr)
    script.on("message", on_msg)
    script.load()
    script.exports_sync.init({"install_hooks": False})

    # Force atlas regen so CreateFontIndirectA / GetGlyphOutlineA fire.
    face_hex = "ＭＳ Ｐゴシック".encode("cp932").hex()
    script.exports_sync.force_atlas_regen(face_hex)
    device.resume(pid)
    time.sleep(8.0)

    try:
        device.kill(pid)
    except Exception:
        pass

if __name__ == "__main__":
    sys.exit(main() or 0)
