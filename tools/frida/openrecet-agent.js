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

    // pure-math functions used by the state-forcing differential tests.
    // see docs/decompiled/by-address/{5041f6,499583}.c
    fn_lcg_step:         0x005041f6,  // void→u15 (output in low 15 bits of u32)
    fn_audio_fade_apply: 0x00499583,  // BGM cos-curve fade dispatcher

    // font system globals (see docs/findings/winmain-and-bootstrap.md
    // §"Font system" for the full pipeline).
    fn_font_init:        0x0047c228,  // "init fontsys ok" boundary
    fn_font_atlas_load:  0x0047c3a5,  // atlas loader entry
    var_atlas_regen_flag:0x073dfd00,  // u32 — set by config.idx `font:` key,
                                       // gates FUN_0047c474 invocation in WinMain
    var_font_name:       0x073de168,  // char[256] — face name copied from
                                       // config.idx for CreateFontIndirectA
    var_fontdata_ptr:    0x073dde2c,  // u32 ptr — loaded fontdata.bin buffer
    var_fontidx_ptr:     0x073dde30,  // u32 ptr — loaded fontidx.bin buffer

    // globals.
    var_d3d_device:      0x073dfcbc,  // IDirect3DDevice8 *
    var_input_mask:      0x073dddd0,  // u16 — per-frame buttons (player 0)
    var_frame_counter:   0x073dfcfc,  // u32 — title-scene BG-scroll tick.
                                       // NOT a global frame counter: stops
                                       // advancing once the title scene
                                       // dispatches into a sub-scene
                                       // (settings, shop, dungeon, …). The
                                       // agent maintains its own manual
                                       // counter (g_manual_frame_counter,
                                       // bumped on every Present onEnter)
                                       // for scene-agnostic frame numbering.
                                       // This address is kept here for
                                       // state-forcing / diagnostics only.
                                       // See engine-quirks §"Frame counter
                                       // pauses on scene transition (Phase
                                       // B)".
    var_lcg_seed:        0x006023a0,  // u32 — engine RNG state
    var_bgm_slider:      0x056e5778,  // u32 — BGM volume slider 0..9
    var_bgm_audiopath:   0x09643108,  // IDirectMusicAudioPath * (COM ptr)
    var_mci_debug_gate:  0x0438ccb4,  // u32 — non-zero recomputes fade
                                       // through MCI bridge (BSS zero default)
    var_pause_flag:      0x073dfca0,  // u32 — engine's "should tick" gate.
                                       // 0 = WaitMessage / paused, 1 = run
                                       // FUN_0047be92. Engine init writes 0;
                                       // WndProc WM_ACTIVATE flips it to 1
                                       // when the window activates. With the
                                       // window hidden no WM_ACTIVATE fires,
                                       // so the agent forces this to 1 in
                                       // the ShowWindow hook (otherwise the
                                       // engine sits in WaitMessage forever).
                                       // See findings/winmain-and-bootstrap.md
                                       // line 79001.
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

// Window-hide control. When true, the first call to user32!ShowWindow
// against the engine's main window gets its nCmdShow argument rewritten
// to SW_HIDE (0). The engine's pause flag (DAT_073dfca0) is also forced
// to 1 in the same hook — without that the engine's main loop sits in
// WaitMessage forever (the flag normally flips to 1 via WM_ACTIVATE,
// which never fires for a window that's never shown). Set via the
// init RPC's `hide_window` field.
let g_hide_window         = false;
let g_show_window_handled = false;  // pin to first call only

// Manual frame counter. Engine-side DAT_073dfcfc is the title-scene
// BG-scroll tick — it freezes the moment the title hands off to a
// sub-scene (settings, shop, etc.), so it can't be used for any
// scenario that crosses a scene boundary. Instead the agent bumps
// `g_manual_frame_counter` once per Present (the only call site the
// engine guarantees runs every rendered frame), and every event
// emitted by the agent — frame captures, audio events, input_state,
// input-injection trace lookup — uses this counter via frameNo().
//
// Semantics:
//   - Counter starts at 0 before the first Present.
//   - input_poll / sim / audio events that fire during the cycle
//     leading to Present N read frameNo() == N (counter hasn't bumped
//     yet — bump happens at the END of Present onEnter).
//   - Present onEnter for the Nth Present captures frame=N, then
//     bumps to N+1.
//
// This matches Phase A's per-sim counter semantics: frame 0 is the
// first sim/render cycle, frame N is the (N+1)th. Goldens authored
// against `DAT_073dfcfc` (pre-2026-05-22 retail goldens) will need
// re-blessing if there are any pre-title Presents on the host — the
// engine's BG-scroll tick starts at 0 when the title scene's sim
// first runs, which may not be the very first Present in the
// pipeline.
let g_manual_frame_counter = 0;

// Input injection. When g_input_force_active is true, the input_poll
// onLeave hook overwrites DAT_073dddd0 (var_input_mask) with the
// sticky-trace mask for the current engine frame, then re-emits the
// `input_state` event reflecting the forced value. The trace is the
// same sparse {frame, mask} schema Phase A uses; entries hold between
// transitions. See docs/findings/winmain-and-bootstrap.md §"Input
// injection".
let g_input_trace        = [];     // [{frame, mask}, ...] sorted ascending
let g_input_trace_i      = 0;      // monotonic cursor into g_input_trace
let g_input_force_active = false;
let g_input_last_forced  = 0;      // sticky mask between sparse entries

// ─── helpers ────────────────────────────────────────────────────────────

function rva(va) { return g_base.add(va - IMAGE_BASE.toUInt32()); }

function nowMs() { return (Date.now() - g_start_real_ms) | 0; }

function log(msg) { send({kind: 'log', msg: String(msg)}); }
function err(where, msg) { send({kind: 'error', where: where, msg: String(msg)}); }

function vtableSlot(thisPtr, idx) {
    const vtable = thisPtr.readPointer();
    return vtable.add(idx * Process.pointerSize).readPointer();
}

// Manual, scene-agnostic frame counter (bumped in Present onEnter).
function frameNo() {
    return g_manual_frame_counter;
}

// The engine's own counter at DAT_073dfcfc. Reflects the title scene's
// BG-scroll tick; freezes on scene transition. Exposed for diagnostics
// + state-forcing tests, but NOT used for capture / event timing.
function engineFrameNo() {
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
            // Capture BEFORE the buffer flips to the front. `fn` is the
            // manual counter (g_manual_frame_counter), bumped at the end
            // of this onEnter — see the comment on g_manual_frame_counter
            // for why we don't trust engine-side DAT_073dfcfc here.
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
            // Bump AFTER the capture decision. Audio/input events that
            // fired during the cycle leading to this Present have
            // already observed frameNo() == fn (this is the desired
            // alignment — see g_manual_frame_counter comment).
            g_manual_frame_counter++;
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
                const fn = frameNo();

                // Injection (optional). Advance the monotonic cursor
                // through every trace entry with frame <= fn; the last
                // such mask is the sticky value to apply. If the
                // engine ran multiple ticks between Presents (shouldn't
                // — input_poll is called once per tick — but defensive)
                // this still picks the most-recent applicable entry.
                if (g_input_force_active) {
                    while (g_input_trace_i < g_input_trace.length &&
                           g_input_trace[g_input_trace_i].frame <= fn) {
                        g_input_last_forced =
                            g_input_trace[g_input_trace_i].mask & 0xffff;
                        g_input_trace_i++;
                    }
                    // Write u16 — the engine global is a 16-bit slot
                    // (binding table tops out at bit 0x2000), and
                    // sim_a / button-state ring are the next readers
                    // after this onLeave returns.
                    rva(ADDR.var_input_mask).writeU16(g_input_last_forced);
                }

                const mask = rva(ADDR.var_input_mask).readU16();
                send({kind: 'input_state',
                      t_ms: nowMs(),
                      frame: fn,
                      buttons: mask});
            } catch (e) {
                err('input_poll.onLeave', e.message);
            }
        },
    });
    log('input hook installed');
}

// ─── window-hide hook ───────────────────────────────────────────────────

// SW_HIDE = 0 per WinUser.h. Documented value, hardcoded everywhere
// from MSVC's CRT to the SDK.
const SW_HIDE = 0;

function installShowWindowHook() {
    // Frida 17.x removed the legacy Module.findExportByName(name, export)
    // global helper. The per-module instance methods stayed, so resolve
    // user32.dll first then look up the export off the Module instance.
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) {
        err('installShowWindowHook', 'user32.dll module not loaded');
        return;
    }
    const showWindow = u32.findExportByName('ShowWindow');
    if (!showWindow) {
        err('installShowWindowHook', 'user32!ShowWindow not found');
        return;
    }
    Interceptor.attach(showWindow, {
        onEnter: function (args) {
            // Catch the engine's first ShowWindow against its main
            // window (CreateWindowExA in FUN_0047aa8b populates
            // DAT_073dfc7c right before the WinMain call site at all.c
            // line 78958 invokes ShowWindow on it). Pin the substitution
            // to one call so any later ShowWindow against dialogs / error
            // boxes / MessageBoxA-spawned popups passes through untouched.
            if (g_show_window_handled) return;
            g_show_window_handled = true;

            const originalCmd = args[1].toInt32();
            args[1] = ptr(SW_HIDE);

            // Compensate for the WM_ACTIVATE that the engine would
            // normally use to flip its pause flag to 1. Without this
            // write the engine sits in WaitMessage forever (all.c
            // line 79001 gates the tick on DAT_073dfca0 != 0).
            try {
                rva(ADDR.var_pause_flag).writeU32(1);
            } catch (e) {
                err('installShowWindowHook/pause-flag', e.message);
            }
            send({kind: 'log',
                  msg: 'ShowWindow(SW_HIDE) substituted (was nCmdShow=' +
                       originalCmd + '); var_pause_flag forced to 1'});
        },
    });
    log('ShowWindow hook installed');
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

// ─── state-forcing helpers (used by tools/state_diff/) ─────────────────
//
// These run before the target's main thread is resumed — Frida's helper
// thread is alive in the suspended process from script.load() onward, so
// NativeFunction calls work without ever advancing engine code.
//
// All addresses are passed in as Ghidra VAs and translated through rva()
// against the actual module base picked by the loader.

function hexToBytes(hex) {
    if (typeof hex !== 'string') throw new Error('hexToBytes: not a string');
    if (hex.length & 1) throw new Error('hexToBytes: odd length');
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; i++) {
        out[i] = parseInt(hex.substr(i * 2, 2), 16);
    }
    return out;
}

function bytesToHex(arr) {
    const view = new Uint8Array(arr);
    let s = '';
    for (let i = 0; i < view.length; i++) {
        const b = view[i];
        s += (b < 16 ? '0' : '') + b.toString(16);
    }
    return s;
}

function ensureBase() {
    if (g_base) return;
    const mod = Process.findModuleByName(MODULE_NAME);
    if (!mod) throw new Error('module not found: ' + MODULE_NAME);
    g_base = mod.base;
}

// Capture FUN_00499583's would-be SetVolume argument for a given BGM
// slider value, without touching real audio output.
//
// Strategy: FUN_00499583 reads the slider from DAT_056e5778, computes
// the centibel via cos curve, and calls `(*pAudioPath->vtable[5])(pAudioPath,
// centibel, 0)`. We swap DAT_09643108 to a fake AudioPath whose
// vtable[5] is a NativeCallback that records the centibel and returns
// S_OK. Frame-0 (slider 0) takes the early-return path with -10000.
//
// Side-effect-free: saves and restores both globals around the call.
// MCI debug gate (DAT_0438ccb4) is checked + temporarily zeroed so the
// duplicate-recompute branch is suppressed even on a host where it
// happened to be set (BSS-zero by default, but defensive).
function captureFadeCentibel(slider) {
    ensureBase();

    const slotPtr = rva(ADDR.var_bgm_audiopath);
    const sliderPtr = rva(ADDR.var_bgm_slider);
    const mciGatePtr = rva(ADDR.var_mci_debug_gate);

    const savedSlot   = slotPtr.readPointer();
    const savedSlider = sliderPtr.readU32();
    const savedGate   = mciGatePtr.readU32();

    // Fake AudioPath: { vtable_ptr } pointing to a 6-slot vtable. Only
    // slot 5 (SetVolume, offset 0x14) is dereferenced by the engine on
    // this path — IUnknown slots 0..2 and the four IDirectMusicAudioPath
    // methods at 3..5 aren't all touched, but we fill the whole vtable
    // with a "should-not-be-called" stub to fail loud if the engine
    // calls something unexpected.
    const fakeObj    = Memory.alloc(Process.pointerSize);
    const fakeVtable = Memory.alloc(Process.pointerSize * 8);
    fakeObj.writePointer(fakeVtable);

    let captured = 0x7fffffff;       // sentinel — must be overwritten
    let captureCount = 0;

    const captureCb = new NativeCallback(function (this_, lVolume, dwDuration) {
        captured = lVolume;
        captureCount++;
        return 0;  // S_OK
    }, 'uint32', ['pointer', 'int32', 'uint32'], 'stdcall');

    const trapCb = new NativeCallback(function () {
        send({kind: 'error', where: 'fake_audiopath',
              msg: 'unexpected vtable slot called'});
        return 0x80004001;  // E_NOTIMPL
    }, 'uint32', ['pointer'], 'stdcall');

    for (let i = 0; i < 8; i++) {
        fakeVtable.add(i * Process.pointerSize).writePointer(trapCb);
    }
    // SetVolume — IDirectMusicAudioPath::SetVolume vtable slot index 5
    // (3 IUnknown + GetObjectInPath + SetVolume).
    fakeVtable.add(5 * Process.pointerSize).writePointer(captureCb);

    slotPtr.writePointer(fakeObj);
    sliderPtr.writeU32(slider >>> 0);
    mciGatePtr.writeU32(0);

    try {
        // No abi argument → Frida picks the platform default
        // (MSVC cdecl on Windows x86). Explicit 'cdecl' is rejected by
        // gum_native_function_call_abi; the valid token would be
        // 'mscdecl' but the default has equivalent behaviour for our
        // no-arg / void-return cases.
        const fn = new NativeFunction(rva(ADDR.fn_audio_fade_apply), 'void', []);
        fn();
    } finally {
        slotPtr.writePointer(savedSlot);
        sliderPtr.writeU32(savedSlider);
        mciGatePtr.writeU32(savedGate);
    }

    return {centibel: captured, calls: captureCount};
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

        // Input injection setup. The driver passes a pre-sorted list of
        // {frame, mask} entries (sparse trace, post-dense expansion is
        // done lazily here via the cursor walk in the input hook). An
        // empty list with force_input=true forces 0 every frame, which
        // is a useful "pin all inputs released" mode for repro work.
        if (Array.isArray(config.input_trace)) {
            g_input_trace = config.input_trace
                .map(function (e) {
                    return {frame: e.frame | 0, mask: (e.mask | 0) & 0xffff};
                })
                .sort(function (a, b) { return a.frame - b.frame; });
        } else {
            g_input_trace = [];
        }
        g_input_trace_i      = 0;
        g_input_last_forced  = 0;
        g_input_force_active = !!config.force_input;

        g_hide_window         = !!config.hide_window;
        g_show_window_handled = false;

        // Defensive reset — driver creates a fresh agent per spawn so
        // this is a no-op in practice, but keeps init self-contained.
        g_manual_frame_counter = 0;

        ensureBase();
        g_boot_ms = nowMs();
        g_start_real_ms = Date.now();

        send({kind: 'ready',
              module: MODULE_NAME,
              base: g_base,
              capture_pending: Array.from(g_capture_pending),
              capture_all: g_capture_all,
              max_frames: g_max_frames,
              input_trace_entries: g_input_trace.length,
              force_input: g_input_force_active,
              hide_window: g_hide_window});

        // The capture-side hooks expect to fire on a running engine.
        // State-forcing tests skip resume() entirely, so we make the
        // hook installation opt-in via config.install_hooks (default
        // true to preserve the Phase B capture pipeline behavior).
        const install = config.install_hooks !== false;
        if (install) {
            installInitHook();
            installAudioHooks();
            installInputHook();
            // Window-hide hook needs to install BEFORE resume so it can
            // intercept the engine's first ShowWindow call. Same lifetime
            // as the other capture-side hooks.
            if (g_hide_window) {
                installShowWindowHook();
            }
        }
    },

    // Note on naming: frida-python's RPC layer converts snake_case method
    // names to camelCase before dispatch (`script.exports_sync.read_u32(va)`
    // → JS lookup of `readU32`). So *all* exports here must be camelCase,
    // even though the rest of the file uses snake_case. The capture-side
    // entry points (queueCapture, getFrame) were previously `queue_capture`
    // / `get_frame` and silently broken — Phase B's driver only ever
    // called `init`, which has no underscores.
    queueCapture: function (frame) {
        g_capture_pending.add(frame | 0);
    },

    getFrame: function () {
        return frameNo();
    },

    // ── state-forcing surface ──
    // All RPCs use Ghidra VAs (preferred ImageBase 0x00400000); the
    // agent translates to the actual load base on every call.

    readMemory: function (va, len) {
        ensureBase();
        const bytes = rva(va).readByteArray(len | 0);
        return bytesToHex(bytes);
    },

    writeMemory: function (va, hex) {
        ensureBase();
        const bytes = hexToBytes(hex);
        const buf = Memory.alloc(bytes.length);
        buf.writeByteArray(Array.from(bytes));
        Memory.copy(rva(va), buf, bytes.length);
        return bytes.length;
    },

    readU32: function (va) {
        ensureBase();
        return rva(va).readU32();
    },

    writeU32: function (va, val) {
        ensureBase();
        rva(va).writeU32(val >>> 0);
    },

    // Invoke a cdecl function returning u32, no args. Used for the LCG
    // step (FUN_005041f6) and as the simplest call-function building
    // block. Add overloads as needed (we don't need a fully generic
    // call_function until a test demands non-trivial arg lists).
    callU32NoArgs: function (va) {
        ensureBase();
        const fn = new NativeFunction(rva(va), 'uint32', []);
        return fn();
    },

    // Force the engine to regenerate its font atlas via the legit
    // boot-time path (FUN_0047c474). Engine gates the regen call on
    // `DAT_073dfd00 != 0` (raised by `font:` in config.idx); we set
    // the flag + face name in a hook on FUN_0047c228 entry, which
    // fires AFTER tables_load_all has parsed config.idx but BEFORE
    // the engine reads the regen gate.
    //
    // Caller passes the SJIS face name as hex (e.g. "ＭＳ Ｐゴシック"
    // = "824c8272208246835383568362834e"). We don't validate — invalid
    // names just produce a CreateFontIndirectA failure inside the
    // engine's FUN_0047c474.
    //
    // Returns nothing immediately; the atlas write happens at engine
    // boot time, fontdata.bin / fontidx.bin land in retail's cwd.
    forceAtlasRegen: function (face_name_hex) {
        ensureBase();
        const nameBytes = hexToBytes(face_name_hex);
        if (nameBytes.length > 255) {
            throw new Error('face name too long: ' + nameBytes.length);
        }

        Interceptor.attach(rva(ADDR.fn_font_init), {
            onEnter: function (args) {
                // Write face name to DAT_073de168 (256-byte buffer,
                // zero-terminated). Clear the buffer first to match
                // the engine's "memset 256 + strcpy" sequence in the
                // config.idx parser.
                //
                // NOTE: Memory.alloc returns UNINITIALIZED memory, not
                // zeroed. Use a directly-written buffer instead so we
                // don't smear garbage into the engine's face-name
                // global — which would make CreateFontIndirectA see
                // "MS PGothic<random>" and fall through GDI's font
                // substitution to whatever default it picks.
                const namePtr = rva(ADDR.var_font_name);
                for (let i = 0; i < 256; i++) {
                    namePtr.add(i).writeU8(0);
                }
                for (let i = 0; i < nameBytes.length; i++) {
                    namePtr.add(i).writeU8(nameBytes[i]);
                }
                // Raise the regen gate so FUN_0047c474 fires.
                rva(ADDR.var_atlas_regen_flag).writeU32(1);
                send({kind: 'log',
                      msg: 'forceAtlasRegen: regen flag raised + face name set'});
            },
        });
        send({kind: 'log',
              msg: 'forceAtlasRegen: hook installed on FUN_0047c228'});
    },

    // Read the runtime atlas pointers + their first N bytes. Useful
    // for confirming the loader populated the buffers after a regen
    // pass. The buffers are heap-allocated with no embedded size, so
    // the caller specifies how many bytes to dump per buffer.
    //
    // Returns {fontdata_ptr, fontidx_ptr, fontdata_head, fontidx_head}
    // where the *_ptr fields are hex strings and *_head are hex-encoded
    // first-`head_len` bytes (or empty string if pointer is null).
    dumpAtlasPtrs: function (head_len) {
        ensureBase();
        head_len = (head_len | 0) || 0;
        const fdPtrRaw = rva(ADDR.var_fontdata_ptr).readU32();
        const fiPtrRaw = rva(ADDR.var_fontidx_ptr).readU32();
        let fontdata_head = '';
        let fontidx_head = '';
        if (head_len > 0) {
            if (fdPtrRaw !== 0) {
                const bytes = ptr(fdPtrRaw).readByteArray(head_len);
                fontdata_head = bytesToHex(bytes);
            }
            if (fiPtrRaw !== 0) {
                const bytes = ptr(fiPtrRaw).readByteArray(head_len);
                fontidx_head = bytesToHex(bytes);
            }
        }
        return {
            fontdata_ptr: '0x' + fdPtrRaw.toString(16),
            fontidx_ptr:  '0x' + fiPtrRaw.toString(16),
            fontdata_head: fontdata_head,
            fontidx_head:  fontidx_head,
        };
    },

    // BGM-fade centibel capture for slider value `slider` ∈ [0, 9].
    // Returns {centibel, calls} where `calls` is the number of times
    // the engine called our fake SetVolume (expected 1 on every code
    // path — frame-0 hits the early-return SetVolume, frames 1..9 hit
    // the main-path SetVolume).
    captureFadeCentibel: function (slider) {
        return captureFadeCentibel(slider | 0);
    },
};
