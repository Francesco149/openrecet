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
//   {kind:"mesh_dump",  path:"xfile/...", buffer:"vb"|"ib",  // + binary payload
//                       num_vertices:N, num_faces:M, fvf:F, vert_size:S,
//                       index_size:I}
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

    // game tick scheduler + clock (see docs/findings/winmain-and-bootstrap.md
    // §"Game tick scheduler"). Used by the turbo mode: the entry hook on
    // fn_tick advances a virtual clock by `g_turbo_step_ms` per call, and
    // fn_clock_ms is `Interceptor.replace`d with a stub that returns that
    // virtual clock instead of QPC. The engine's dispatcher then sees
    // delta >= the 60 FPS threshold every iteration, so the inner sim+
    // render path runs every loop pass with no Sleep — i.e. as fast as
    // the host can chew through it, but with the timestep the engine
    // would have honored at 60 FPS.
    fn_tick:             0x0047be92,  // FUN_0047be92 — main-loop dispatcher
    fn_clock_ms:         0x0047be2f,  // FUN_0047be2f — QPC*1000/freq fallback timeGetTime

    // BGM audio path. Created in FUN_00498ef4's
    // CreateStandardAudioPath sequence; same vtable shared with the two
    // SE paths (DAT_0964310c / DAT_09643110), so one Interceptor on
    // vtable[5] (SetVolume) silences all three. Used by the silent-audio
    // mode.
    fn_audio_init:       0x00498ef4,  // FUN_00498ef4 — init daoudio

    // mesh loader. FUN_00472836 is the engine's .x asset loader wrapper:
    // it calls FUN_004c8f74 (D3DXLoadMeshFromXof clone) to parse the file,
    // then post-processes the result (locks VB once to scan / classify
    // textures, may CloneMeshFVF to 0x152 if the D3DX-parsed FVF differs).
    //
    //   stdcall FUN_00472836(void **mesh_struct_out, const char *fmt_arg,
    //                        int param3) -> int
    //
    // The first arg is filled with an engine mesh struct whose first DWORD
    // holds the ID3DXMesh interface pointer; the second arg is a path-like
    // string (either a filename to sprintf into, or an asset id depending
    // on call site); the return value is 1 on success / 0 on failure.
    //
    // We hook this to dump every loaded mesh's VB+IB via the ID3DXMesh
    // vtable so we can bit-compare against our own xfile + mesh_build
    // pipeline. See installMeshDumpHook below.
    fn_mesh_load_wrapper: 0x00472836,

    // engine render-thread top-level (FUN_004547ab @ 0x4547ab, 1670 B).
    // Called by the main message pump per iteration. Owns BeginScene /
    // scene-state switch (which dispatches to FUN_00474a9a et al) /
    // EndScene / Present / device-lost recovery. The mesh-render demo
    // replaces this whole function with a minimal demo loop:
    //   Clear → BeginScene → SetTransform×3 → SetRenderState batch
    //   → SetLight + LightEnable → SetMaterial → ID3DXMesh::DrawSubset
    //   loop → EndScene → Present
    // The existing Present hook still fires on our self-issued Present
    // (it's the IDirect3DDevice8 vtable hook, not call-site specific),
    // so frame capture works unchanged.
    fn_render_thread_top: 0x004547ab,

    // recet.ini parser + back-buffer dimension globals. The parser maps
    // `screen=0/1/2/3` → 640×480 / 800×600 / 1024×768 / 1280×960 and
    // writes the result here; downstream window-creation + D3D init
    // reads these for the present-params back-buffer dims. The
    // resolution-injection mode hooks the parser's onLeave and
    // overwrites both globals so retail captures land at the same
    // dimensions openrecet uses (1024×768 by default), instead of
    // whatever stale `screen=` value is in vendor/unpacked/recet.ini.
    fn_recet_ini_parse:   0x0047a474,  // FUN_0047a474
    var_screen_width:     0x005cbc04,  // u32 — backbuffer width
    var_screen_height:    0x005cbc08,  // u32 — backbuffer height

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

// ID3DXBaseMesh (d3dx8mesh.h order, post-IUnknown). The engine's loaded
// meshes are ID3DXMesh which inherits from ID3DXBaseMesh, so these slot
// indices apply directly. The full table:
//
//   3  DrawSubset
//   4  GetNumFaces
//   5  GetNumVertices
//   6  GetFVF
//   7  GetDeclaration
//   8  GetOptions
//   9  GetDevice
//  10  CloneMeshFVF
//  11  CloneMesh
//  12  GetVertexBuffer
//  13  GetIndexBuffer
//  14  LockVertexBuffer
//  15  UnlockVertexBuffer
//  16  LockIndexBuffer
//  17  UnlockIndexBuffer
//
const V_Mesh_GetNumFaces       = 4;
const V_Mesh_GetNumVertices    = 5;
const V_Mesh_GetFVF            = 6;
const V_Mesh_GetOptions        = 8;
const V_Mesh_LockVertexBuffer  = 14;
const V_Mesh_UnlockVertexBuffer = 15;
const V_Mesh_LockIndexBuffer   = 16;
const V_Mesh_UnlockIndexBuffer = 17;

// D3DXMESH option bits we care about (from d3dx8mesh.h).
const D3DXMESH_32BIT = 0x001;

// IDirect3DDevice8 vtable slots used by the render-demo path. Indices
// counted from the IUnknown methods at slots 0-2.
const V_Dev_Present_           = 15;
const V_Dev_BeginScene         = 34;
const V_Dev_EndScene           = 35;
const V_Dev_Clear              = 36;
const V_Dev_SetTransform       = 37;
const V_Dev_SetMaterial        = 42;
const V_Dev_SetLight           = 44;
const V_Dev_LightEnable        = 46;
const V_Dev_SetRenderState     = 50;

// D3D8 transform-state IDs (D3DTRANSFORMSTATETYPE).
const D3DTS_VIEW       = 2;
const D3DTS_PROJECTION = 3;
const D3DTS_WORLD      = 256;

// D3D8 clear flags + a few render-state IDs we touch from JS. (Full set
// in d3d8types.h; we only break out what the demo render uses.)
const D3DCLEAR_TARGET   = 0x01;
const D3DCLEAR_ZBUFFER  = 0x02;
const D3DRS_ZENABLE                  = 7;
const D3DRS_FILLMODE                 = 8;
const D3DRS_ZWRITEENABLE             = 14;
const D3DRS_CULLMODE                 = 22;
const D3DRS_LIGHTING                 = 137;
const D3DRS_AMBIENT                  = 139;
const D3DRS_COLORVERTEX              = 140;
const D3DRS_NORMALIZENORMALS         = 142;
const D3DRS_DIFFUSEMATERIALSOURCE    = 145;
const D3DRS_AMBIENTMATERIALSOURCE    = 147;
const D3DCULL_CCW = 3;

// ID3DXBaseMesh::DrawSubset slot — vtable index 3 in d3dx8mesh.h's
// ID3DXBaseMesh declaration (after the 3 IUnknown slots).
const V_Mesh_DrawSubset = 3;

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

// Turbo mode. When enabled, the agent fakes the engine's wall-clock
// reader (FUN_0047be2f) so the dispatcher's `delta_thirds < threshold`
// check always trips into the tick path (no Sleep). Each entry into
// FUN_0047be92 advances g_virtual_now_ms by g_turbo_step_ms (default 17,
// i.e. one 60 FPS frame budget), and the replaced FUN_0047be2f stub
// returns g_virtual_now_ms. Within a single tick all callers of the
// clock fn see the same value, so audio fades / animations stay
// consistent with what the engine would have done at 60 FPS — just
// crunched into "as fast as the host can run the loop".
//
// We have to keep the NativeCallback alive (Frida releases it when GC'd
// and the engine then crashes calling a freed thunk), so g_turbo_clock_cb
// is held in module scope after installation.
let g_turbo_enabled  = false;
let g_turbo_step_ms  = 17;
let g_virtual_now_ms = 0;
let g_turbo_clock_cb = null;

// Silent audio. When enabled, vtable[5] (SetVolume) of the BGM
// IDirectMusicAudioPath is hooked so every call has lVolume rewritten
// to -10000 (hard silence). All three audio paths share the same
// vtable, so the hook silences BGM + SE-A + SE-B in one shot. The
// engine's audio path is otherwise untouched — PlaySegmentEx still
// fires, fade animations still tick, segment-states still queue. We
// install lazily on the FIRST observed `audio_init` exit (when
// DAT_09643108 first becomes non-null), since the AudioPath instance
// has to exist before we can read its vtable.
let g_silent_audio_enabled = false;
let g_silent_audio_hooked  = false;

// Resolution injection. When enabled, the engine's recet.ini parser
// (FUN_0047a474) is hooked at onLeave to overwrite DAT_005cbc04
// (width) + DAT_005cbc08 (height) with the requested dimensions
// before the window-creation + D3D-init paths read them. Lets us
// pin retail captures at 1024×768 even when its
// vendor/unpacked/recet.ini is empty / has a stale `screen=` value.
// Without this, retail falls back to the engine's default 640×480
// while openrecet runs at the 1024×768 from vendor/original/recet.ini,
// and the captures don't line up for side-by-side diffs.
let g_force_resolution_w = 0;
let g_force_resolution_h = 0;

// Mesh dump runtime state. `g_mesh_dump_filters` is a list of substrings;
// when set, only paths containing any of them get dumped (case-sensitive
// substring match). Empty list = dump every .x load.
// `g_mesh_dump_seen` records sanitized paths already dumped so a single
// session never emits two payloads for the same file (the engine may load
// the same mesh twice during a stage transition).
let g_mesh_dump_enabled = false;
let g_mesh_dump_filters = [];
let g_mesh_dump_seen    = new Set();
let g_mesh_dump_count   = 0;

// Render-demo state. When `g_demo_active` is true, the
// Interceptor.replace on FUN_004547ab routes every render-thread tick
// through the JS demo callback below instead of the engine's normal
// scene render. `g_demo_mesh_struct` holds the engine-mesh-struct
// pointer returned by FUN_00472836 for the demo target (the first
// dword is the ID3DXMesh interface). The view/proj/world/light
// payloads are pre-allocated in retail memory so the per-frame
// callback doesn't have to re-allocate.
let g_demo_active        = false;
let g_demo_mesh_struct   = null;     // NativePointer to engine mesh struct
let g_demo_num_subsets   = 0;        // material_count from the mesh struct
let g_demo_view_ptr      = null;     // float[16] in retail memory
let g_demo_proj_ptr      = null;     // float[16]
let g_demo_world_ptr     = null;     // float[16] (identity)
let g_demo_light_ptr     = null;     // D3DLIGHT8 (104 bytes)
let g_demo_material_ptr  = null;     // D3DMATERIAL8 (68 bytes)
let g_demo_keepalive     = [];       // NativeCallback retainer — GC eats them otherwise

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

// ─── turbo (frame-limiter bypass + virtual 16.6 ms clock) ──────────────

function installTurboHooks() {
    // 1. Replace FUN_0047be2f with a virtual-clock stub. The engine
    //    calls this once per tick (and a handful of other places — fade
    //    animations etc.); all callers see the same value within a
    //    single dispatcher iteration. cdecl, no args, returns u32 ms in
    //    EAX — matches the engine's __allmul/__alldiv path.
    g_turbo_clock_cb = new NativeCallback(function () {
        return g_virtual_now_ms >>> 0;
    }, 'uint32', []);
    Interceptor.replace(rva(ADDR.fn_clock_ms), g_turbo_clock_cb);

    // 2. Bump the virtual clock on every dispatcher entry. With a 17 ms
    //    step the engine sees delta_thirds = 17*3 = 51 ≥ 50 (the 60 FPS
    //    threshold), so it always takes the sim+render branch and never
    //    Sleeps. Increasing the step doesn't speed the game up — it just
    //    inflates the engine's internal "time per frame" while the loop
    //    still runs at host speed — so 17 is the right default.
    Interceptor.attach(rva(ADDR.fn_tick), {
        onEnter: function (args) {
            g_virtual_now_ms = (g_virtual_now_ms + g_turbo_step_ms) >>> 0;
        },
    });

    log('turbo hooks installed (step_ms=' + g_turbo_step_ms + ')');
}

// ─── silent audio (downstream SetVolume clamp to -10000) ───────────────

function installSilentAudioFromPath(audiopathPtr) {
    if (g_silent_audio_hooked) return;
    if (audiopathPtr.isNull()) {
        err('installSilentAudio', 'audiopath pointer is NULL — audio init failed?');
        return;
    }

    // IDirectMusicAudioPath vtable layout (dmusici.h):
    //   0..2   IUnknown        (QueryInterface / AddRef / Release)
    //   3      GetObjectInPath
    //   4      Activate
    //   5      SetVolume   <-- engine indexes `*(vt + 0x14)` = vt[5]
    //                         (FUN_00499583 + FUN_00499c63, also the
    //                         existing captureFadeCentibel helper).
    const SETVOLUME_SLOT = 5;
    const setVolume = vtableSlot(audiopathPtr, SETVOLUME_SLOT);
    log('silent-audio: AudioPath @ ' + audiopathPtr +
        '  vtable[' + SETVOLUME_SLOT + '] (SetVolume) @ ' + setVolume);

    Interceptor.attach(setVolume, {
        onEnter: function (args) {
            // SetVolume(this, lVolume, dwDuration). args[0] = this,
            // args[1] = lVolume (LONG, signed centibel), args[2] = dwDuration.
            // Clamp to -10000 = absolute silence. Engine's fade animation
            // still runs and still calls SetVolume normally; we just
            // swallow the magnitude before it reaches DirectMusic.
            args[1] = ptr(-10000);
        },
    });

    g_silent_audio_hooked = true;
    log('silent-audio: SetVolume vtable hook live (BGM + SE share the same vtable)');
}

// ─── resolution injection ───────────────────────────────────────────────

function installForceResolutionHook(w, h) {
    const widthPtr  = rva(ADDR.var_screen_width);
    const heightPtr = rva(ADDR.var_screen_height);
    Interceptor.attach(rva(ADDR.fn_recet_ini_parse), {
        onLeave: function (retval) {
            try {
                widthPtr.writeU32(w >>> 0);
                heightPtr.writeU32(h >>> 0);
                send({kind: 'log',
                      msg: 'force_resolution: DAT_005cbc04/08 = ' + w + '×' + h});
            } catch (e) {
                err('force_resolution.onLeave', e.message);
            }
        },
    });
    log('force_resolution: armed on FUN_0047a474 exit (' + w + '×' + h + ')');
}

function installSilentAudioHook() {
    // Install at the exit of FUN_00498ef4 (audio_init). At that point
    // CreateStandardAudioPath has populated DAT_09643108, so we can
    // read the BGM audiopath pointer and grab its vtable. Hooking the
    // path's vtable[5] silences all three paths (they share a vtable).
    const var_bgm_audiopath = rva(ADDR.var_bgm_audiopath);
    Interceptor.attach(rva(ADDR.fn_audio_init), {
        onLeave: function (retval) {
            try {
                const path = var_bgm_audiopath.readPointer();
                installSilentAudioFromPath(path);
            } catch (e) {
                err('silent_audio.onLeave', e.message);
            }
        },
    });
    log('silent-audio: armed on FUN_00498ef4 exit');
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
            // Signal "device ready" so RPC callers that depend on the
            // device being live (invokeMeshLoader, …) know when it's
            // safe to fire.
            send({kind: 'd3d_device_ready', device: dev.toString()});
        },
    });
    log('d3d init hook installed @ ' + rva(ADDR.fn_d3d_init_wrapper));
}

// ─── mesh dump hook ─────────────────────────────────────────────────────
//
// Bit-level parser validation. Hooks FUN_00472836 (the engine's .x
// loader wrapper) onEnter to record which file is about to be loaded
// (param 2 is the path string pointer), then onLeave to walk the
// resulting ID3DXMesh interface and emit its VB+IB bytes.
//
// We send TWO `mesh_dump` messages per loaded mesh — one for the vertex
// buffer, one for the index buffer. The vertex bytes are the raw FVF-
// 0x152 stream (36 bytes per vertex: float3 pos + float3 normal + DWORD
// diffuse + float2 UV) as D3DXLoadMeshFromXof emitted them, post any
// engine CloneMeshFVF rewrite. The index bytes are 16-bit unless the
// mesh was created with D3DXMESH_32BIT — we read GetOptions and tag the
// `index_size` field accordingly so the host driver can size correctly.
//
// Failure modes (logged via `err`, not fatal):
//   - param_2 unreadable / NULL                 → skip the dump silently
//   - filename filter set but no match          → skip the dump silently
//   - ID3DXMesh ptr is NULL after onLeave       → log and skip
//   - LockVertexBuffer / LockIndexBuffer fails  → log HRESULT and skip
//
// We always Unlock on the lock-success path even when read fails, to
// avoid leaving the engine in a half-locked state that would block
// downstream render frames.
function installMeshDumpHook() {
    const addr = rva(ADDR.fn_mesh_load_wrapper);

    Interceptor.attach(addr, {
        onEnter: function (args) {
            this._mesh_path    = null;
            this._mesh_outptr  = null;
            if (!g_mesh_dump_enabled) return;

            const meshOutPtr = args[0];        // engine mesh struct (filled by call)
            const pathArg    = args[1];        // path string (or fmt arg for sprintf)
            if (meshOutPtr.isNull()) return;

            let path = null;
            try {
                path = pathArg.readCString();
            } catch (e) {
                /* pathArg might be an integer that the engine sprintfs into
                 * a format string — see the DUNGEON branch of FUN_00474a9a
                 * where param_2 IS a string but other call sites may pass an
                 * integer. We can't read it as a string in those cases; skip. */
                return;
            }
            if (!path || path.length === 0 || path.length > 256) return;

            // Filter by substring if filters were configured.
            if (g_mesh_dump_filters.length > 0) {
                const matched = g_mesh_dump_filters.some(function (s) {
                    return path.indexOf(s) !== -1;
                });
                if (!matched) return;
            }

            // De-dupe: don't emit the same file twice within a session
            // even if the engine reloads it (stage re-entry, etc.).
            if (g_mesh_dump_seen.has(path)) return;

            this._mesh_path   = path;
            this._mesh_outptr = meshOutPtr;
        },

        onLeave: function (retval) {
            if (!this._mesh_path || !this._mesh_outptr) return;
            const path     = this._mesh_path;
            const meshOut  = this._mesh_outptr;

            // FUN_00472836 returns 1 on success; on failure the engine has
            // already MessageBoxA'd, but the value at *meshOut may be
            // garbage — bail unless we explicitly succeeded.
            const rc = retval.toInt32();
            if (rc !== 1) {
                send({kind: 'log',
                      msg: 'mesh_dump: skipping ' + path + ' (load rc=' + rc + ')'});
                return;
            }

            const id3dxmesh = meshOut.readPointer();
            if (id3dxmesh.isNull()) {
                err('mesh_dump', 'ID3DXMesh* is NULL after success on ' + path);
                return;
            }

            try {
                dumpMeshBuffers(path, id3dxmesh);
                g_mesh_dump_seen.add(path);
                g_mesh_dump_count++;
            } catch (e) {
                err('mesh_dump:' + path, e.message);
            }
        },
    });
    log('mesh dump hook installed @ ' + addr +
        ' (filters=' + JSON.stringify(g_mesh_dump_filters) + ')');
}

// Walk the ID3DXMesh interface to extract metadata + locked VB/IB bytes,
// then emit two `mesh_dump` messages with binary payloads. Throws on any
// unrecoverable failure (caller logs + continues).
function dumpMeshBuffers(path, id3dxmesh) {
    // Metadata via vtable scalars. These have stdcall ABI on Windows
    // (D3DX8 was always __stdcall); thiscall has the `this` pointer in
    // ecx but Frida's NativeFunction with stdcall treats first arg as
    // `this`, which works because the engine's compiled call site does
    // exactly that — push `this` last, callee pops `this` as the first
    // arg. (See vtableSlot usage above for IDirect3DDevice8 — same
    // shape.)
    const getNumFaces = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_GetNumFaces),
        'uint32', ['pointer'], 'stdcall');
    const getNumVerts = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_GetNumVertices),
        'uint32', ['pointer'], 'stdcall');
    const getFVF = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_GetFVF),
        'uint32', ['pointer'], 'stdcall');
    const getOptions = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_GetOptions),
        'uint32', ['pointer'], 'stdcall');

    const numFaces = getNumFaces(id3dxmesh) >>> 0;
    const numVerts = getNumVerts(id3dxmesh) >>> 0;
    const fvf      = getFVF(id3dxmesh)      >>> 0;
    const options  = getOptions(id3dxmesh)  >>> 0;

    // Vertex size from FVF. The engine clones to 0x152 after load
    // (D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_TEX1):
    //   D3DFVF_XYZ      0x002 → 12 bytes (3 floats: x, y, z)
    //   D3DFVF_NORMAL   0x010 → 12 bytes (3 floats: nx, ny, nz)
    //   D3DFVF_DIFFUSE  0x040 →  4 bytes (DWORD ARGB)
    //   D3DFVF_SPECULAR 0x080 →  4 bytes (DWORD ARGB)
    //   tex count       (fvf >> 8) & 0xf, each → 8 bytes (2 floats)
    let vertSize = 0;
    if (fvf & 0x002) vertSize += 12;
    if (fvf & 0x010) vertSize += 12;
    if (fvf & 0x040) vertSize += 4;
    if (fvf & 0x080) vertSize += 4;
    const texCount = (fvf >>> 8) & 0xf;
    vertSize += texCount * 8;

    if (vertSize === 0) {
        throw new Error('FVF 0x' + fvf.toString(16) + ' resolves to 0-byte vertices');
    }

    const indexSize = (options & D3DXMESH_32BIT) ? 4 : 2;

    // Lock + read + unlock the vertex buffer.
    const lockVB = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_LockVertexBuffer),
        'uint32', ['pointer', 'uint32', 'pointer'], 'stdcall');
    const unlockVB = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_UnlockVertexBuffer),
        'uint32', ['pointer'], 'stdcall');

    const vbOut = Memory.alloc(Process.pointerSize);
    vbOut.writePointer(NULL);
    const hrVB = lockVB(id3dxmesh, 0, vbOut) >>> 0;
    if (hrVB !== 0) {
        throw new Error('LockVertexBuffer failed HRESULT 0x' + hrVB.toString(16));
    }
    try {
        const vbPtr   = vbOut.readPointer();
        const vbBytes = vbPtr.readByteArray(numVerts * vertSize);
        send({
            kind:          'mesh_dump',
            path:          path,
            buffer:        'vb',
            num_vertices:  numVerts,
            num_faces:     numFaces,
            fvf:           fvf,
            options:       options,
            vert_size:     vertSize,
            index_size:    indexSize,
            size_bytes:    numVerts * vertSize,
        }, vbBytes);
    } finally {
        unlockVB(id3dxmesh);
    }

    // Lock + read + unlock the index buffer.
    const lockIB = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_LockIndexBuffer),
        'uint32', ['pointer', 'uint32', 'pointer'], 'stdcall');
    const unlockIB = new NativeFunction(
        vtableSlot(id3dxmesh, V_Mesh_UnlockIndexBuffer),
        'uint32', ['pointer'], 'stdcall');

    const ibOut = Memory.alloc(Process.pointerSize);
    ibOut.writePointer(NULL);
    const hrIB = lockIB(id3dxmesh, 0, ibOut) >>> 0;
    if (hrIB !== 0) {
        throw new Error('LockIndexBuffer failed HRESULT 0x' + hrIB.toString(16));
    }
    try {
        const ibPtr   = ibOut.readPointer();
        const ibLen   = numFaces * 3 * indexSize;
        const ibBytes = ibPtr.readByteArray(ibLen);
        send({
            kind:          'mesh_dump',
            path:          path,
            buffer:        'ib',
            num_vertices:  numVerts,
            num_faces:     numFaces,
            fvf:           fvf,
            options:       options,
            vert_size:     vertSize,
            index_size:    indexSize,
            size_bytes:    ibLen,
        }, ibBytes);
    } finally {
        unlockIB(id3dxmesh);
    }
}

// ─── render-demo hook ──────────────────────────────────────────────────
//
// Replace FUN_004547ab (the engine's render thread top-level) with a
// minimal "draw one mesh under a fixed camera" loop. Used by
// tools/render-retail-demo.py to capture retail's rendering of the
// same geometry openrecet's --house-preview draws, so we can pixel-
// diff the two and isolate render-code divergences.
//
// Why replace instead of attach: the render thread runs Clear / scene
// dispatch / EndScene / Present all inline; Frida's Interceptor.attach
// can't abort the original. Replacement lets us own the entire frame.
// The existing Present hook (vtable-level, not call-site) still fires
// on our self-issued Present so frame capture is unchanged.
//
// Memory layout for the per-frame payloads (all allocated once in
// setupRenderDemo, reused every callback fire):
//
//   view  / proj / world : float[16]  (64 B each)
//   light                : D3DLIGHT8  (104 B)
//   material             : D3DMATERIAL8 (68 B)
//
// D3DLIGHT8 byte layout (d3d8types.h):
//   +0   Type (DWORD)                        — 2 = D3DLIGHT_DIRECTIONAL
//   +4   Diffuse (4 floats: r,g,b,a)         — 16 B
//   +20  Specular (4 floats)                 — 16 B
//   +36  Ambient (4 floats)                  — 16 B
//   +52  Position (3 floats)                 — 12 B
//   +64  Direction (3 floats)                — 12 B
//   +76  Range (float)                       —  4 B
//   +80  Falloff (float)                     —  4 B
//   +84  Attenuation0/1/2 (3 floats)         — 12 B
//   +96  Theta (float)                       —  4 B
//   +100 Phi (float)                         —  4 B
//   total: 104 B
//
// D3DMATERIAL8 byte layout:
//   +0   Diffuse  (4 floats)
//   +16  Ambient  (4 floats)
//   +32  Specular (4 floats)
//   +48  Emissive (4 floats)
//   +64  Power    (float)
//   total: 68 B

function writeMatrix16(ptr, m16) {
    for (let i = 0; i < 16; i++) ptr.add(i * 4).writeFloat(m16[i]);
}

function makeLight(diffuse, direction, ambient) {
    const buf = Memory.alloc(104);
    /* zero it */
    for (let i = 0; i < 104; i += 4) buf.add(i).writeU32(0);
    buf.add(0).writeU32(2);                          /* D3DLIGHT_DIRECTIONAL */
    buf.add(4).writeFloat(diffuse[0]);
    buf.add(8).writeFloat(diffuse[1]);
    buf.add(12).writeFloat(diffuse[2]);
    buf.add(16).writeFloat(diffuse[3]);
    /* Specular = (0,0,0,0) — left zeroed */
    buf.add(36).writeFloat(ambient[0]);
    buf.add(40).writeFloat(ambient[1]);
    buf.add(44).writeFloat(ambient[2]);
    buf.add(48).writeFloat(ambient[3]);
    /* Position unused for DIRECTIONAL */
    buf.add(64).writeFloat(direction[0]);
    buf.add(68).writeFloat(direction[1]);
    buf.add(72).writeFloat(direction[2]);
    /* Range/Falloff/Attenuation/Theta/Phi all left at zero — only
     * Range matters for non-POINT/SPOT lights and DIRECTIONAL ignores
     * it. */
    return buf;
}

function makeMaterial(diffuse, ambient, specular, emissive, power) {
    const buf = Memory.alloc(68);
    for (let i = 0; i < 4; i++) buf.add(0  + i * 4).writeFloat(diffuse[i]);
    for (let i = 0; i < 4; i++) buf.add(16 + i * 4).writeFloat(ambient[i]);
    for (let i = 0; i < 4; i++) buf.add(32 + i * 4).writeFloat(specular[i]);
    for (let i = 0; i < 4; i++) buf.add(48 + i * 4).writeFloat(emissive[i]);
    buf.add(64).writeFloat(power);
    return buf;
}

// Build the per-call NativeFunctions for the device + mesh interfaces
// we use in the demo callback. Stashed under `g_demo_keepalive` so the
// JS GC doesn't free the underlying trampoline buffers.
function buildDemoFns(devicePtr, id3dxmeshPtr) {
    const f = {
        Clear: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_Clear),
            'uint32',
            ['pointer', 'uint32', 'pointer', 'uint32', 'uint32', 'float', 'uint32'],
            'stdcall'),
        BeginScene: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_BeginScene),
            'uint32', ['pointer'], 'stdcall'),
        EndScene: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_EndScene),
            'uint32', ['pointer'], 'stdcall'),
        Present: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_Present_),
            'uint32', ['pointer', 'pointer', 'pointer', 'pointer', 'pointer'],
            'stdcall'),
        SetTransform: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_SetTransform),
            'uint32', ['pointer', 'uint32', 'pointer'], 'stdcall'),
        SetRenderState: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_SetRenderState),
            'uint32', ['pointer', 'uint32', 'uint32'], 'stdcall'),
        SetLight: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_SetLight),
            'uint32', ['pointer', 'uint32', 'pointer'], 'stdcall'),
        LightEnable: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_LightEnable),
            'uint32', ['pointer', 'uint32', 'uint32'], 'stdcall'),
        SetMaterial: new NativeFunction(
            vtableSlot(devicePtr, V_Dev_SetMaterial),
            'uint32', ['pointer', 'pointer'], 'stdcall'),
        DrawSubset: new NativeFunction(
            vtableSlot(id3dxmeshPtr, V_Mesh_DrawSubset),
            'uint32', ['pointer', 'uint32'], 'stdcall'),
    };
    g_demo_keepalive.push(f);
    return f;
}

function setupRenderDemo(meshPath, viewMatrix16, projMatrix16,
                        lightDirection3, ambientRgba4)
{
    ensureBase();
    const dev = rva(ADDR.var_d3d_device).readPointer();
    if (dev.isNull()) {
        throw new Error('setupRenderDemo: D3D device not yet initialised');
    }

    // 1. Load the mesh via the existing invokeMeshLoader pipeline
    //    semantics — except we DON'T fire the mesh dump (the demo is
    //    a render-side test, not a parser-side one). Allocate the
    //    engine mesh struct here and call the loader directly.
    const meshStruct = Memory.alloc(64 * 4);
    for (let i = 0; i < 16; i++) meshStruct.add(i * 4).writeU32(0);
    const pathBuf = Memory.allocUtf8String(meshPath);
    const loader = new NativeFunction(
        rva(ADDR.fn_mesh_load_wrapper),
        'uint32', ['pointer', 'pointer', 'uint32'], 'stdcall');
    const rc = loader(meshStruct, pathBuf, 0xffffffff) >>> 0;
    if (rc !== 1) {
        throw new Error('setupRenderDemo: loader rc=' + rc + ' for ' + meshPath);
    }

    const id3dxmesh = meshStruct.readPointer();
    if (id3dxmesh.isNull()) {
        throw new Error('setupRenderDemo: loader returned NULL ID3DXMesh');
    }

    // Subset count = material_count = meshStruct + 0x10 (param_1[4] in
    // the engine's struct layout — see FUN_00472836:93).
    const numSubsets = meshStruct.add(16).readU32() >>> 0;

    g_demo_mesh_struct  = meshStruct;
    g_demo_num_subsets  = numSubsets;

    // 2. Allocate + populate per-frame payloads in retail memory.
    g_demo_view_ptr  = Memory.alloc(64);
    g_demo_proj_ptr  = Memory.alloc(64);
    g_demo_world_ptr = Memory.alloc(64);
    writeMatrix16(g_demo_view_ptr,  viewMatrix16);
    writeMatrix16(g_demo_proj_ptr,  projMatrix16);
    writeMatrix16(g_demo_world_ptr,
                  [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]);

    g_demo_light_ptr = makeLight(
        /*diffuse*/  [1.0, 1.0, 1.0, 1.0],
        /*direction*/lightDirection3,
        /*ambient*/  [0.0, 0.0, 0.0, 0.0]);  // global ambient handled via RS_AMBIENT
    g_demo_material_ptr = makeMaterial(
        /*diffuse*/  [1.0, 1.0, 1.0, 1.0],
        /*ambient*/  [1.0, 1.0, 1.0, 1.0],
        /*specular*/ [0.0, 0.0, 0.0, 0.0],
        /*emissive*/ [0.0, 0.0, 0.0, 0.0],
        /*power*/    0.0);

    // 3. Build the device + mesh function pointer cache.
    const fns = buildDemoFns(dev, id3dxmesh);

    // 4. Encode the global ambient as ARGB DWORD (alpha first, then R,G,B).
    const ambient_argb =
        ((Math.round(ambientRgba4[3] * 255) & 0xff) << 24) |
        ((Math.round(ambientRgba4[0] * 255) & 0xff) << 16) |
        ((Math.round(ambientRgba4[1] * 255) & 0xff) <<  8) |
        ((Math.round(ambientRgba4[2] * 255) & 0xff));

    // 5. The replacement function. Signature matches FUN_004547ab:
    //    `void __cdecl/stdcall()` — no args, no return. We use stdcall
    //    here; the engine's original is presumably __cdecl (no args
    //    means no callee pops, so the convention doesn't matter for
    //    return-stack hygiene).
    const replacementCb = new NativeCallback(function () {
        try {
            // Clear back buffer + Z. Black is the engine HOUSE default
            // and the same color our --house-preview Z-clears to.
            fns.Clear(dev, 0, NULL,
                      D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                      0xff000000, 1.0, 0);
            fns.BeginScene(dev);

            // Render state baseline. Matches the C7b/mesh_draw default
            // (CULL_CCW + LIGHTING + Z buffer + COLORVERTEX from
            // material's COLOR1) so the demo's render state is identical
            // to ours in openrecet.
            fns.SetRenderState(dev, D3DRS_ZENABLE,            1);
            fns.SetRenderState(dev, D3DRS_ZWRITEENABLE,       1);
            fns.SetRenderState(dev, D3DRS_CULLMODE,           D3DCULL_CCW);
            fns.SetRenderState(dev, D3DRS_LIGHTING,           1);
            fns.SetRenderState(dev, D3DRS_AMBIENT,            ambient_argb);
            fns.SetRenderState(dev, D3DRS_COLORVERTEX,        1);
            fns.SetRenderState(dev, D3DRS_NORMALIZENORMALS,   1);
            fns.SetRenderState(dev, D3DRS_DIFFUSEMATERIALSOURCE, 1); /* D3DMCS_COLOR1 */
            fns.SetRenderState(dev, D3DRS_AMBIENTMATERIALSOURCE, 1);

            // Light + transforms.
            fns.SetLight(dev, 0, g_demo_light_ptr);
            fns.LightEnable(dev, 0, 1);
            fns.SetTransform(dev, D3DTS_VIEW,       g_demo_view_ptr);
            fns.SetTransform(dev, D3DTS_PROJECTION, g_demo_proj_ptr);
            fns.SetTransform(dev, D3DTS_WORLD,      g_demo_world_ptr);
            fns.SetMaterial(dev, g_demo_material_ptr);

            // Draw every subset on the loaded mesh.
            for (let s = 0; s < g_demo_num_subsets; s++) {
                fns.DrawSubset(id3dxmesh, s);
            }

            fns.EndScene(dev);
            // Self-issue Present so the existing Present hook fires
            // and the capture pipeline grabs the back buffer.
            fns.Present(dev, NULL, NULL, NULL, NULL);
        } catch (e) {
            err('demoRender', e.message + ' ' + e.stack);
        }
    }, 'void', [], 'stdcall');
    g_demo_keepalive.push(replacementCb);

    // 6. Replace FUN_004547ab. Once installed the engine's main loop
    //    calls our function every iteration instead of the real one.
    Interceptor.replace(rva(ADDR.fn_render_thread_top), replacementCb);
    g_demo_active = true;

    log('render-demo: armed (' + meshPath + ', ' + numSubsets + ' subsets, ' +
        'ambient_argb=0x' + ambient_argb.toString(16) + ')');
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

        // Turbo + silent-audio knobs (off by default — preserves the
        // capture-pipeline behavior for callers that don't opt in).
        g_turbo_enabled  = !!config.turbo;
        g_turbo_step_ms  = (config.turbo_step_ms | 0) || 17;
        g_virtual_now_ms = 0;
        g_silent_audio_enabled = !!config.silent_audio;
        g_silent_audio_hooked  = false;

        // force_resolution: [w, h] or null. When set, hook recet.ini
        // parse exit and overwrite the engine's screen-size globals.
        g_force_resolution_w = 0;
        g_force_resolution_h = 0;
        if (Array.isArray(config.force_resolution) &&
            config.force_resolution.length === 2) {
            g_force_resolution_w = config.force_resolution[0] | 0;
            g_force_resolution_h = config.force_resolution[1] | 0;
        }

        // Mesh dump knob. config.dump_meshes is either:
        //   true             — dump every .x load
        //   ['xfile/shop/']  — dump only paths containing any of these
        //                      substrings
        //   undefined/false  — disabled
        g_mesh_dump_seen.clear();
        g_mesh_dump_count = 0;
        g_mesh_dump_filters = [];
        g_mesh_dump_enabled = false;
        if (config.dump_meshes === true) {
            g_mesh_dump_enabled = true;
        } else if (Array.isArray(config.dump_meshes)) {
            g_mesh_dump_enabled = true;
            g_mesh_dump_filters = config.dump_meshes
                .filter(function (s) { return typeof s === 'string'; });
        }

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
              hide_window: g_hide_window,
              turbo: g_turbo_enabled,
              turbo_step_ms: g_turbo_step_ms,
              silent_audio: g_silent_audio_enabled});

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
            // Turbo + silent-audio. Both install pre-resume so they
            // catch the very first dispatcher entry / audio_init exit.
            if (g_turbo_enabled) {
                installTurboHooks();
            }
            if (g_silent_audio_enabled) {
                installSilentAudioHook();
            }
            // Resolution injection — must install pre-resume so we
            // catch the recet.ini parse onLeave before window creation.
            if (g_force_resolution_w > 0 && g_force_resolution_h > 0) {
                installForceResolutionHook(g_force_resolution_w,
                                           g_force_resolution_h);
            }
            // Mesh dump — must install pre-resume so we catch every
            // FUN_00472836 invocation including the first ones during
            // INGAME bootstrap.
            if (g_mesh_dump_enabled) {
                installMeshDumpHook();
            }
        }
    },

    // RPC for the host driver to query how many meshes have been dumped
    // so it can decide when to send the stop signal. Snapshots
    // g_mesh_dump_count + the seen-paths list.
    getMeshDumpStatus: function () {
        return {
            count: g_mesh_dump_count,
            paths: Array.from(g_mesh_dump_seen),
        };
    },

    // Directly invoke the engine's FUN_00472836 loader with a chosen
    // path, bypassing the natural scene-1 asset-load chain. Used by
    // tools/dump-retail-meshes.py to dump any .x file without having
    // to drive the engine to the corresponding gameplay state — which
    // for shop_1st.x means clearing the intro cutscene + tutorials.
    //
    // The path arg is a relative engine asset path *as it appears
    // in the engine's call sites* (e.g. "xfile/shop/shop_1st.x"). The
    // engine's loader prepends nothing — its call sites pass the full
    // path. FUN_00472836 internally feeds the path to FUN_004c8f74
    // (the D3DXLoadMeshFromXof clone) which fopens through the
    // storage system, so the file resolves from lnkdatas.bin / the
    // vendor data dir like any other engine asset.
    //
    // Prereqs (enforced by check at top): D3D device must be live
    // (DAT_073dfcbc != NULL). Frida's init blocks before device
    // creation; the driver should wait for a `d3d_device_ready`
    // event before calling this RPC.
    // Wire up the render-demo: load the mesh, populate view/proj/light/
    // material payloads, and Interceptor.replace FUN_004547ab so every
    // engine render-thread tick runs the demo callback instead. The
    // existing Present hook still fires on the self-issued Present so
    // the capture pipeline (capture_frames in init) works unchanged.
    //
    // config = {
    //   mesh_path:   "xfile/shop/shop_1st.x",
    //   view:        [16 floats, row-major, row-vector convention],
    //   proj:        [16 floats, ditto],
    //   light_dir:   [x, y, z]  -- directional light, gets normalised
    //                              by D3D
    //   ambient:     [r, g, b, a]  -- global ambient (0..1)
    // }
    setupRenderDemo: function (config) {
        try {
            if (!config) throw new Error('config required');
            const path = config.mesh_path;
            if (typeof path !== 'string' || !path) {
                throw new Error('config.mesh_path required');
            }
            if (!Array.isArray(config.view) || config.view.length !== 16) {
                throw new Error('config.view must be 16 floats');
            }
            if (!Array.isArray(config.proj) || config.proj.length !== 16) {
                throw new Error('config.proj must be 16 floats');
            }
            const lightDir = (Array.isArray(config.light_dir) &&
                              config.light_dir.length === 3)
                                 ? config.light_dir
                                 : [0.6, -1.0, -0.5];
            const ambient = (Array.isArray(config.ambient) &&
                             config.ambient.length === 4)
                                ? config.ambient
                                : [0.25, 0.25, 0.25, 1.0];

            setupRenderDemo(path, config.view, config.proj, lightDir, ambient);
            return true;
        } catch (e) {
            err('setupRenderDemo', e.message + ' ' + (e.stack || ''));
            return false;
        }
    },

    invokeMeshLoader: function (path) {
        if (typeof path !== 'string' || path.length === 0) {
            err('invokeMeshLoader', 'bad path: ' + path);
            return null;
        }
        ensureBase();
        const dev = rva(ADDR.var_d3d_device).readPointer();
        if (dev.isNull()) {
            err('invokeMeshLoader', 'D3D device not yet initialised');
            return null;
        }

        // Engine mesh struct: 10 dwords (0x28 bytes) is what we see
        // FUN_00472836 fill (param_1[0..9]). Over-allocate to 64 dwords
        // for safety against any post-write the engine might do via
        // pointer adjustments we missed.
        const meshStruct = Memory.alloc(64 * 4);
        Memory.protect(meshStruct, 64 * 4, 'rw-');
        // Zero out — defensive; calloc semantics for the engine's
        // checks at param_1[9] etc.
        for (let i = 0; i < 16; i++) {
            meshStruct.add(i * 4).writeU32(0);
        }

        // Allocate path string in retail address space.
        const pathBuf = Memory.allocUtf8String(path);

        // Force the dump filter to MATCH this path (otherwise the
        // hook's filter would suppress it). Save + restore so the
        // outer filter set isn't mutated permanently.
        const savedEnabled = g_mesh_dump_enabled;
        const savedFilters = g_mesh_dump_filters;
        g_mesh_dump_enabled = true;
        g_mesh_dump_filters = [path];

        let ok = false;
        try {
            const loader = new NativeFunction(
                rva(ADDR.fn_mesh_load_wrapper),
                'uint32', ['pointer', 'pointer', 'uint32'], 'stdcall');
            const rc = loader(meshStruct, pathBuf, 0xffffffff) >>> 0;
            ok = (rc === 1);
            log('invokeMeshLoader(' + path + ') → rc=' + rc);
        } catch (e) {
            err('invokeMeshLoader:' + path, e.message);
        } finally {
            g_mesh_dump_enabled = savedEnabled;
            g_mesh_dump_filters = savedFilters;
        }
        return ok;
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
