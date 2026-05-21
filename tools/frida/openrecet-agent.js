// tools/frida/openrecet-agent.js
//
// Phase B harness agent. Loads into vendor/unpacked/recettear.unpacked.exe
// (the SteamStub-decrypted retail binary) and emits the same event/frame
// schemas as Phase A's openrecet binary so tools/frida_capture.py can lay
// the output down next to scenario goldens.
//
// Hooks installed on first Present:
//   IDirect3DDevice8::Present (vtable[15])
//       → GetBackBuffer + LockRect → raw BGRA pixels via send(json, buf)
//   FUN_00499200  audio_play_track  → {kind:"bgm_swap", track}
//   FUN_00499c63  audio_play_se     → {kind:"se_play",  slot}
//   FUN_0047b73c  input_poll exit   → {kind:"input_state", buttons:0xNNNN}
//
// Address convention: the unpacked retail exe was built in 2007, predates
// /DYNAMICBASE, and Windows loads it at its preferred ImageBase 0x00400000.
// Ghidra uses the same base, so Ghidra-VAs map 1:1 to runtime VAs *after*
// adjusting for whatever base Windows actually picked (we recompute on
// load instead of trusting the preferred base).
//
// Message protocol (all stringified JSON via send()):
//   {kind:"ready",      module:"...", base:"0xNNNNNNNN"}
//   {kind:"frame",      frame:N, w:W, h:H, pitch:P}        // + binary payload
//   {kind:"bgm_swap",   t_ms:T, track:N}
//   {kind:"se_play",    t_ms:T, slot:N}
//   {kind:"input_state",t_ms:T, frame:N, buttons:0xNNNN}
//   {kind:"log",        msg:"..."}                          // diagnostics
//   {kind:"error",      where:"...", msg:"..."}
//
// The Python driver matches on `kind`, picks up the optional binary
// payload via the second arg of the on_message callback, writes it as a
// BMP, and appends jsonl rows to audio.jsonl / trace.jsonl.

'use strict';

// ─── engine addresses (Ghidra VAs, preferred ImageBase 0x00400000) ──────

const MODULE_NAME = 'recettear.unpacked.exe';
const IMAGE_BASE  = ptr('0x00400000');

const ADDR = {
    // engine wrapper around Direct3DCreate8 + IDirect3D8::CreateDevice;
    // populates DAT_073dfcbc on success. We attach to its exit so we know
    // *when* the device pointer is live.
    fn_d3d_init_wrapper: 0x0047ac6a,

    // audio entry points (see docs/findings/audio-backend.md table).
    fn_audio_play_track: 0x00499200,  // BGM swap
    fn_audio_play_se:    0x00499c63,  // SE start/stop

    // input poll (see docs/findings/winmain-and-bootstrap.md §"Input poll").
    fn_input_poll:       0x0047b73c,

    // globals.
    var_d3d_device:      0x073dfcbc,  // IDirect3DDevice8 *
    var_input_mask:      0x073dddd0,  // u16 — per-frame buttons (player 0)
    var_frame_counter:   0x073dfcfc,  // u32 — global tick frame counter
};

// ─── COM vtable indices ─────────────────────────────────────────────────

// IUnknown
const V_QueryInterface = 0;
const V_AddRef         = 1;
const V_Release        = 2;

// IDirect3DDevice8 (d3d8.h order)
const V_Dev_Present            = 15;
const V_Dev_GetBackBuffer      = 16;
const V_Dev_CreateImageSurface = 27;
const V_Dev_CopyRects          = 28;

// IDirect3DSurface8
const V_Surf_GetDesc    = 8;
const V_Surf_LockRect   = 9;
const V_Surf_UnlockRect = 10;

// D3D constants
const D3DBACKBUFFER_TYPE_MONO = 0;
const D3DLOCK_READONLY        = 0x00000010;

// D3DSURFACE_DESC layout (8 dwords, total 32 bytes)
//   0  Format
//   4  Type
//   8  Usage
//  12  Pool
//  16  Size
//  20  MultiSampleType
//  24  Width
//  28  Height
const D3DSURFACE_DESC_SIZE = 32;
const D3DSURFACE_DESC_W    = 24;
const D3DSURFACE_DESC_H    = 28;

// D3DLOCKED_RECT layout
//   0  Pitch (int)
//   4  pBits (void*)
const D3DLOCKED_RECT_SIZE  = 8;
const D3DLOCKED_RECT_PITCH = 0;
const D3DLOCKED_RECT_PBITS = 4;

// ─── runtime state ──────────────────────────────────────────────────────

let g_base = null;       // NativePointer — actual module base
let g_device_inst = null;  // NativePointer — IDirect3DDevice8*
let g_present_hooked = false;
let g_capture_pending = new Set();  // frame numbers to dump on next Present
let g_capture_all = false;          // if true, dump every Present
let g_max_frames = 0;               // 0 = no cap; stop = die after that many sim frames
let g_boot_ms = 0;
let g_start_real_ms = Date.now();

// ─── helpers ────────────────────────────────────────────────────────────

function rva(va) { return g_base.add(va - IMAGE_BASE.toUInt32()); }

function nowMs() { return (Date.now() - g_start_real_ms) | 0; }

function log(msg) { send({kind: 'log', msg: String(msg)}); }
function err(where, msg) { send({kind: 'error', where: where, msg: String(msg)}); }

function vtableSlot(thisPtr, idx) {
    const vtable = thisPtr.readPointer();
    return vtable.add(idx * Process.pointerSize).readPointer();
}

function frameNo() {
    return rva(ADDR.var_frame_counter).readU32();
}

// ─── frame capture ──────────────────────────────────────────────────────

let g_dev_fns = null;  // {get_backbuffer, create_image_surface, copy_rects}

function ensureSurfaceFns(devicePtr) {
    if (g_dev_fns) return;
    g_dev_fns = {
        get_backbuffer: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_GetBackBuffer),
            'uint32',
            ['pointer', 'uint32', 'uint32', 'pointer'],
            'stdcall'),
        create_image_surface: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_CreateImageSurface),
            'uint32',
            ['pointer', 'uint32', 'uint32', 'uint32', 'pointer'],
            'stdcall'),
        copy_rects: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_CopyRects),
            'uint32',
            ['pointer', 'pointer', 'pointer', 'uint32', 'pointer', 'pointer'],
            'stdcall'),
    };
}

function releaseSurface(surf) {
    const release = new NativeFunction(
        vtableSlot(surf, V_Release), 'uint32', ['pointer'], 'stdcall');
    release(surf);
}

function captureBackbuffer(devicePtr, frameNumber) {
    ensureSurfaceFns(devicePtr);

    // ── 1. GetBackBuffer → bbSurf (video memory, not lockable) ──
    const ppBB = Memory.alloc(Process.pointerSize);
    ppBB.writePointer(NULL);
    let hr = g_dev_fns.get_backbuffer(devicePtr, 0, D3DBACKBUFFER_TYPE_MONO, ppBB);
    if (hr !== 0) {
        err('captureBackbuffer/GetBackBuffer', 'HRESULT 0x' + (hr >>> 0).toString(16));
        return;
    }
    const bbSurf = ppBB.readPointer();
    if (bbSurf.isNull()) { err('captureBackbuffer', 'GetBackBuffer returned NULL surface'); return; }

    let sysSurf = NULL;
    try {
        // ── 2. GetDesc(bbSurf) to learn w, h, format ──
        const descBuf = Memory.alloc(D3DSURFACE_DESC_SIZE);
        const getDesc = new NativeFunction(
            vtableSlot(bbSurf, V_Surf_GetDesc),
            'uint32', ['pointer', 'pointer'], 'stdcall');
        hr = getDesc(bbSurf, descBuf);
        if (hr !== 0) {
            err('captureBackbuffer/GetDesc', 'HRESULT 0x' + (hr >>> 0).toString(16));
            return;
        }
        const fmt = descBuf.readU32();   // Format at offset 0
        const w   = descBuf.add(D3DSURFACE_DESC_W).readU32();
        const h   = descBuf.add(D3DSURFACE_DESC_H).readU32();

        // ── 3. CreateImageSurface(w, h, fmt, &sysSurf) ──
        // The engine ships its back buffer non-lockable (no
        // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER), so we bounce through a
        // sysmem image surface that *is* lockable by construction. The
        // GetDesc.Format is whatever D3DFMT_X8R8G8B8 / A8R8G8B8 the
        // engine asked for, which is always 32-bit BGRX-style — no
        // conversion needed.
        const ppSys = Memory.alloc(Process.pointerSize);
        ppSys.writePointer(NULL);
        hr = g_dev_fns.create_image_surface(devicePtr, w, h, fmt, ppSys);
        if (hr !== 0) {
            err('captureBackbuffer/CreateImageSurface',
                'HRESULT 0x' + (hr >>> 0).toString(16));
            return;
        }
        sysSurf = ppSys.readPointer();
        if (sysSurf.isNull()) {
            err('captureBackbuffer', 'CreateImageSurface returned NULL');
            return;
        }

        // ── 4. CopyRects(bbSurf → sysSurf) ──
        hr = g_dev_fns.copy_rects(devicePtr, bbSurf, NULL, 0, sysSurf, NULL);
        if (hr !== 0) {
            err('captureBackbuffer/CopyRects',
                'HRESULT 0x' + (hr >>> 0).toString(16));
            return;
        }

        // ── 5. LockRect on sysSurf + send pixels ──
        const lrBuf = Memory.alloc(D3DLOCKED_RECT_SIZE);
        const lockRect = new NativeFunction(
            vtableSlot(sysSurf, V_Surf_LockRect),
            'uint32', ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall');
        hr = lockRect(sysSurf, lrBuf, NULL, D3DLOCK_READONLY);
        if (hr !== 0) {
            err('captureBackbuffer/LockRect(sys)',
                'HRESULT 0x' + (hr >>> 0).toString(16));
            return;
        }
        try {
            const pitch = lrBuf.add(D3DLOCKED_RECT_PITCH).readS32();
            const pBits = lrBuf.add(D3DLOCKED_RECT_PBITS).readPointer();

            const rowBytes = w * 4;
            const total    = rowBytes * h;
            const blob     = Memory.alloc(total);
            for (let y = 0; y < h; y++) {
                Memory.copy(blob.add(y * rowBytes),
                            pBits.add(y * pitch),
                            rowBytes);
            }
            const ab = blob.readByteArray(total);

            send({
                kind:  'frame',
                frame: frameNumber,
                w:     w,
                h:     h,
                pitch: rowBytes,
                fmt:   fmt,
                t_ms:  nowMs(),
            }, ab);
        } finally {
            const unlockRect = new NativeFunction(
                vtableSlot(sysSurf, V_Surf_UnlockRect),
                'uint32', ['pointer'], 'stdcall');
            unlockRect(sysSurf);
        }
    } finally {
        if (!sysSurf.isNull()) releaseSurface(sysSurf);
        releaseSurface(bbSurf);
    }
}

// ─── Present hook ───────────────────────────────────────────────────────

function installPresentHook(devicePtr) {
    if (g_present_hooked) return;

    const presentAddr = vtableSlot(devicePtr, V_Dev_Present);
    log('Present @ ' + presentAddr);

    Interceptor.attach(presentAddr, {
        onEnter: function (args) {
            // Capture BEFORE the buffer flips to the front. We read the
            // engine's frame counter rather than counting Presents so the
            // numbering matches the scenario.yaml capture_frames list.
            const fn = frameNo();
            const want = g_capture_all || g_capture_pending.has(fn);
            if (want) {
                try {
                    captureBackbuffer(devicePtr, fn);
                } catch (e) {
                    err('Present.onEnter', e.message + ' @ ' + e.stack);
                }
                g_capture_pending.delete(fn);
            }
            if (g_max_frames > 0 && fn >= g_max_frames) {
                send({kind: 'max_frames_reached', frame: fn});
            }
        },
    });

    g_present_hooked = true;
    send({kind: 'present_hook_ready', frame: frameNo()});
}

// ─── audio hooks ────────────────────────────────────────────────────────

function installAudioHooks() {
    Interceptor.attach(rva(ADDR.fn_audio_play_track), {
        onEnter: function (args) {
            // FUN_00499200 is stdcall(int track) — track is the first
            // (and only) stack arg at [esp+4] in stdcall, but Frida's
            // `args` array reflects the ABI. For stdcall on x86, args[0]
            // is the first parameter.
            const track = this.context.esp.add(4).readS32();
            send({kind: 'bgm_swap', t_ms: nowMs(), track: track, frame: frameNo()});
        },
    });

    Interceptor.attach(rva(ADDR.fn_audio_play_se), {
        onEnter: function (args) {
            const slot = this.context.esp.add(4).readS32();
            send({kind: 'se_play', t_ms: nowMs(), slot: slot, frame: frameNo()});
        },
    });

    log('audio hooks installed');
}

// ─── input hook ─────────────────────────────────────────────────────────

function installInputHook() {
    Interceptor.attach(rva(ADDR.fn_input_poll), {
        onLeave: function (retval) {
            // After the poll, DAT_073dddd0 holds the per-frame button mask
            // for player 0. Word-sized because the binding table tops out
            // at 14 bits.
            try {
                const mask = rva(ADDR.var_input_mask).readU16();
                send({kind: 'input_state',
                      t_ms: nowMs(),
                      frame: frameNo(),
                      buttons: mask});
            } catch (e) {
                err('input_poll.onLeave', e.message);
            }
        },
    });
    log('input hook installed');
}

// ─── d3d init wrapper hook ──────────────────────────────────────────────

function installInitHook() {
    Interceptor.attach(rva(ADDR.fn_d3d_init_wrapper), {
        onLeave: function (retval) {
            const dev = rva(ADDR.var_d3d_device).readPointer();
            if (dev.isNull()) {
                err('init_wrapper', 'device pointer still NULL after init');
                return;
            }
            g_device_inst = dev;
            log('IDirect3DDevice8* = ' + dev);
            try {
                installPresentHook(dev);
            } catch (e) {
                err('installPresentHook', e.message);
            }
        },
    });
    log('d3d init hook installed @ ' + rva(ADDR.fn_d3d_init_wrapper));
}

// ─── rpc surface ────────────────────────────────────────────────────────

rpc.exports = {
    init: function (config) {
        config = config || {};
        if (Array.isArray(config.capture_frames)) {
            for (const f of config.capture_frames) g_capture_pending.add(f);
        }
        g_capture_all = !!config.capture_all;
        g_max_frames  = config.max_frames | 0;

        const mod = Process.findModuleByName(MODULE_NAME);
        if (!mod) throw new Error('module not found: ' + MODULE_NAME);
        g_base = mod.base;
        g_boot_ms = nowMs();
        g_start_real_ms = Date.now();

        send({kind: 'ready',
              module: MODULE_NAME,
              base: g_base,
              capture_pending: Array.from(g_capture_pending),
              capture_all: g_capture_all,
              max_frames: g_max_frames});

        installInitHook();
        installAudioHooks();
        installInputHook();
    },

    queue_capture: function (frame) {
        g_capture_pending.add(frame | 0);
    },

    get_frame: function () {
        return frameNo();
    },
};
