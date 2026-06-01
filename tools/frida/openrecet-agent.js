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

    // E.4 Tier 1 stateful diff targets (see docs/plans/e4-per-call-io-capture.md
    // + docs/findings/pure-function-diff.md). stage_gate.{c,h} ports.
    fn_stage_gate_boss_id:    0x00431990, // cdecl(int)->int boss-id range predicate
    fn_stage_gate_checkpoint: 0x0043195d, // ()->int checkpoint-floor predicate
                                          // (reads var_stage_dungeon_id + var_stage_next_floor)

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
    var_stage_dungeon_id: 0x0438b4c8, // i32 DAT_0438b4c8 — current dungeon id
    var_stage_next_floor: 0x0438b4cc, // i32 DAT_0438b4cc — next-floor id

    // TAS anchor sources (P1 retail side — see docs/plans/tas-framework.md
    // + src/anchor_trace.{c,h}). The retail counterparts of the port's
    // g_scene_state + nowloading_is_active() that the edge-triggered anchor
    // emitter samples once per Present.
    var_scene_state:     0x0438b1c0,  // i32 DAT_0438b1c0 — scene mode:
                                       // 0 TITLE / 1 INGAME / 8 LOADING (the
                                       // INGAME-dispatch global widely keyed
                                       // as `DAT_0438b1c0 == 1`).
    var_nowloading_gate:  0x06a49958, // i32 DAT_06a49958 — worker "still
                                       // loading" gate (primary).
    var_nowloading_gate2: 0x06a49960, // i32 DAT_06a49960 — secondary load
                                       // gate. The engine's loading-overlay
                                       // test is `(DAT_06a49958==0) &&
                                       // (DAT_06a49960==0)` (all.c L50058),
                                       // so loading_active = OR of the two —
                                       // the port collapses both in
                                       // nowloading_is_active().
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

    // Cchr.0 — scene-1 table-B render-record dump (find the player record).
    // Table B holds the NPC / entity / character render records the 3D
    // chr-walker FUN_004176ff draws.  512 slots × 0x49 dw (0x124 B); the
    // TYPE dword at +0x00 IS the free sentinel (0 = empty slot).  Field
    // offsets per src/scene1_records.h (SCENE1_RECORDS_B_OFF_*).
    var_records_b_base:  0x069324b0,  // slot 0, field 0 (= TYPE)
    // table A: 4096 slots × 0x25 dw; slot base (field 0 = pos.x) is
    // DAT_069b2f80, TYPE at offset 12 dw (+0x30) with -1 = empty sentinel.
    var_records_a_base:  0x069b2f80,
    var_records_a_count: 0x0076b960,  // g_scene1_records_a_count (Pass D)
    var_records_b_count: 0x0076b964,  // g_scene1_records_b_count (Pass B+C)
    var_records_c_count: 0x0076b968,  // g_scene1_records_c_count (alpha)
    // engine player world position (3 contiguous floats),
    // DAT_056da1d8/dc/e0 — many table-B spawns derive an alt-target from
    // it; lets us tell whether a live record sits at the player.
    var_player_pos:      0x056da1d8,
    // in-shop "people" table — 128 entries × 0x2e9 dw (2980 B).  Each
    // entry models one NPC / customer / event actor / (likely) the
    // player; `alive` int at byte +0x44 (0 = empty).  This is the table
    // the chr-walker FUN_004176ff actually renders character sprites from
    // (see findings/scene1-people-table.md L116), NOT records_b.  Header
    // fields per that survey.
    var_people_base:     0x0076bd54,

    // Cchr.2b — the HOUSE character-sprite leaf renderer + its data
    // globals (see docs/findings/scene1-char-sprite-render.md). The
    // leaf-capture mode hooks the renderer at ENTER to dump its inputs
    // and its own DrawPrimitiveUP to dump the resulting vertex buffer,
    // so chr_sprite_build_quads can be bit-A/B'd against retail.
    fn_chr_sprite_leaf:  0x0045a56f,  // FUN_0045a56f(param_1, char_id, char_id,
                                      //   world_mtx, color) — cdecl, 5 stack args
    var_chr_formdata:    0x00438abe0, // DAT_0438abe0 — u32 holding the
                                      // chr/formdata.bin blob pointer
    var_chr_desc_base:   0x00438ce88, // DAT_0438ce88 — per-char descriptor
                                      // array base (stride 0x5058); sheet_w
                                      // @+0x48, scale_x100 @+0x50, y_origin
                                      // @+0x54, frame-LUT @+0x58
    var_chr_tex_w:       0x073a9b1c,  // DAT_073a9b1c[char_id*0x10] — sheet tex w
    var_chr_tex_h:       0x073a9b20,  // DAT_073a9b20[char_id*0x10] — sheet tex h
    var_player_char_id:  0x056da1cc,  // DAT_056da1cc — player's char id
};

// The two DrawPrimitiveUP return addresses inside the leaf (the byte
// after each `call [dev+0x120]`: 0x45a9f8+6 and 0x45aa2b+6). A
// DrawPrimitiveUP whose caller VA is one of these is the leaf's own draw,
// so its vertex buffer is the sprite geometry to capture.
const CHR_LEAF_DRAW_RETS = [0x0045a9fe, 0x0045aa31];

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

// Additional IDirect3DDevice8 vtable slots used by the D3D state-trace
// emitter (Phase D.4). Order per d3d8.h IDirect3DDevice8Vtbl. IUnknown
// at 0-2, IDirect3DDevice8 methods start at slot 3 (TestCooperativeLevel).
//
//   61 SetTexture
//   63 SetTextureStageState
//   70 DrawPrimitive
//   71 DrawIndexedPrimitive
//   72 DrawPrimitiveUP
//   73 DrawIndexedPrimitiveUP
//   76 SetVertexShader
//   83 SetStreamSource
//   85 SetIndices
const V_Dev_SetTexture             = 61;
const V_Dev_SetTextureStageState   = 63;
const V_Dev_DrawPrimitive          = 70;
const V_Dev_DrawIndexedPrimitive   = 71;
const V_Dev_DrawPrimitiveUP        = 72;
const V_Dev_DrawIndexedPrimitiveUP = 73;
const V_Dev_SetVertexShader        = 76;
const V_Dev_SetStreamSource        = 83;
const V_Dev_SetIndices             = 85;

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

// Differential test mode (Phase D). When true, the agent is set up
// purely for state-forcing RPC calls — no capture/input/turbo/audio
// hooks install, and the engine main thread is expected to stay
// suspended (the driver never calls device.resume()). The Frida-
// injected helper thread still runs independently, so NativeFunction
// calls + memory R/W work without the engine executing. Implies
// install_hooks: false; the runRetail* RPCs additionally check this
// flag and refuse to run if it's not set (so callers from the
// capture pipeline can't accidentally clobber engine globals).
let g_diff_test_enabled = false;

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

// D3D state-trace emitter (Phase D.4). Hooks IDirect3DDevice8 vtable
// slots and emits one event per state-changing or draw call into a
// per-frame buffer; the Present hook flushes the buffer as a single
// `d3d_trace_batch` message just before bumping the frame counter.
// Batching is mandatory — per-call send() saturates the Frida wire
// (frame can have 1000+ state-change calls during INGAME).
//
// `g_d3d_trace_frames` is either null (unfiltered: capture every frame)
// or a Set of frame numbers — events only buffer when frameNo() is in
// the set. Lets a scenario request only a few frames of trace without
// drowning in megabytes per second.
//
// Caller annotation: each event records `ret_va` (the immediate caller's
// return address relative to module base), so the driver can resolve
// "which engine function emitted this" against Ghidra's FUN_ table.
// Uses `this.returnAddress` (free — already on the stack on x86) rather
// than `Thread.backtrace()` (per-call backtrace would dominate runtime).
let g_d3d_trace_enabled = false;
let g_d3d_trace_frames  = null;     // Set<int> or null
let g_d3d_trace_hooked  = false;
let g_d3d_trace_buffer  = [];       // events for current frame, flushed at Present

// Call tracer (Phase E.1). When `call_trace` is true the agent
// Interceptor.attach()es onEnter on every Ghidra-VA in
// `g_call_trace_vas` and emits one event per invocation into a
// per-frame buffer that flushes at Present (same shape as the D3D
// state-trace). Output is meant for "what fired in this frame and in
// what order" analysis — pair with a small `call_trace_frames`
// whitelist or the wire saturates.
let g_call_trace_enabled = false;
let g_call_trace_frames  = null;    // Set<int> or null (null = every frame)
let g_call_trace_vas     = [];      // [int] Ghidra-VAs to hook
let g_call_trace_hooked  = false;
let g_call_trace_buffer  = [];

// Mid-frame buffer cap.  Each event is ~80 bytes JSON.  At 1M events
// per send() we're well under GLib's 128MiB DBus message ceiling
// (~1.6GB worth of headroom) but high enough that steady-state per-
// frame batching still gets one flush per Present.  The cap exists to
// bound the FIRST flush — between agent load and the first Present,
// CRT/MFC startup with 1979 hooks installed can push 100k+ events.
const CALL_TRACE_FLUSH_AT = 50000;
let g_call_trace_n_ok    = 0;
let g_call_trace_n_fail  = 0;

// Auto-Z spam (Phase E.1+). When `auto_z_spam` is true, the
// input_poll onLeave overrides the input mask with 0x10 (button A,
// i.e. Z on the default keyboard binding) for 2 frames out of every
// 4, giving ~15 presses/sec — enough to advance dialogue without
// fighting the engine's debounce.
let g_auto_z_spam = false;

// Auto-3D-trace mode.  When `auto_3d_trace` is true, the agent
// hooks IDirect3DDevice8::DrawIndexedPrimitive (the 3D mesh-draw
// path; absent from title / main-menu UI which uses *UP variants
// only), and arms call_trace emit ONLY for the window
// [3D_seen_frame, 3D_seen_frame + g_auto_3d_trace_frames].  Once the
// window closes the agent sends a single `auto_3d_trace_done`
// message so the python driver can shut the engine down cleanly.
let g_auto_3d_trace        = false;
let g_auto_3d_seen         = false;
let g_auto_3d_seen_frame   = -1;
let g_auto_3d_trace_frames = 60;
let g_auto_3d_hooked       = false;
let g_auto_3d_done_sent    = false;

// Pre-3D-trace mode (inverse of auto_3d_trace).  When `pre_3d_trace` is
// true the agent installs the same DrawIndexedPrimitive trigger but
// arms call_trace emit for every frame BEFORE the first 3D draw, then
// sends `pre_3d_trace_done` on the first 3D draw so the python driver
// shuts the engine down cleanly.  Use with auto_z_spam to drive past
// title menus; the resulting trace covers title + intro cutscene up to
// (but not including) the first HOUSE 3D frame.
let g_pre_3d_trace         = false;
let g_pre_3d_done_sent     = false;

// TAS anchor emitter (P1 retail side — see docs/plans/tas-framework.md).
// The retail counterpart of the port's src/anchor_trace.c: once per
// Present we snapshot {scene_state, loading_active} off the engine
// globals and emit an `{kind:"anchor", anchor:NAME, frame:N}` message for
// every anchor whose rising edge fired vs the previous snapshot. Same
// anchor NAMES + wire shape as the port, so one spec aligns both sides.
// The edge logic below is a 1:1 mirror of anchor_trace.c (BOOT on the
// first tick, then NEW_GAME / LOADING_START / LOADING_END / HOUSE_FREEROAM
// in causal table order); keep the two in lockstep.
let g_anchor_trace_enabled = false;
let g_anchor_initialized   = false;
let g_anchor_prev_scene    = 0;      // previous-frame DAT_0438b1c0
let g_anchor_prev_loading  = false;  // previous-frame (gate1||gate2)!=0

// TAS P2 retail side — anchor-relative capture (`--capture-at-anchor
// NAME[+k]`). The retail counterpart of the port's --capture-at-anchor
// (src/main.c `struct anchor_capture_req` + anchor_capture_schedule()):
// resolve "capture at anchor_frame + offset" live when the named anchor
// fires, so a capture lands on the SAME semantic instant on both targets
// despite the load jitter that makes absolute frame numbers meaningless.
// g_cap_anchor_reqs is the parsed [{name, offset}] list; g_cap_anchor_unfired
// tracks distinct requested names still waiting (so we only shut down once
// every requested anchor has fired); g_cap_anchor_pending holds resolved
// future target frames not yet captured (offset 0 captures immediately).
let g_cap_anchor_reqs      = [];     // [{name:str, offset:int}]
let g_cap_anchor_unfired   = new Set();  // distinct names not yet fired
let g_cap_anchor_pending   = new Set();  // resolved target frames pending
let g_cap_anchor_done_sent = false;

// TAS P3 retail side — anchor-segmented input forcing (`--input-segtrace`),
// the deterministic replacement for the `auto_z_spam` hack. The op list is a
// strict superset of the sparse {frame,mask} input trace: a `{wait:NAME}` op
// breaks the timeline into segments, each counted from the frame its opening
// anchor fired. So a logical trace ("mash A at title; wait HOUSE_FREEROAM;
// spam A through the dialogue; wait HOUSE_FREEROAM again [2nd load]; spam the
// 3D dialogue then hold UP") lowers identically onto port and retail despite
// load-frame jitter. Two key properties:
//   * spam-until-anchor: a segment's entries may run arbitrarily long; the
//     terminating `wait` SHORT-CIRCUITS the moment its anchor fires (at a
//     frame strictly after the segment was entered), abandoning the remaining
//     entries. This is how we A-spam an unknown-length dialogue and stop
//     exactly when the next load completes.
//   * an anchor may recur (HOUSE_FREEROAM fires twice on a new game — the
//     intro forces a second load, engine-quirks §55); a later `wait` on the
//     same name resolves against the NEXT firing (strictly > segment entry).
// With no `wait` ops the single segment has base 0 → identical to an absolute
// trace. The fired-frame map is fed by segtraceOnAnchor() from
// anchorCaptureSchedule(); mirrors the resolve-on-fire shape of g_cap_anchor_*.
let g_segtrace_active   = false;
let g_segtrace_segments = [];   // [{entries:[{frame,mask}], wait:str|null}]
let g_segtrace_seg      = 0;    // current segment index
let g_segtrace_entry    = 0;    // next entry within the current segment
let g_segtrace_base     = 0;    // absolute frame of the current segment's frame 0
let g_segtrace_base_arm = 0;    // frame the current segment was entered (wait guard)
let g_segtrace_sticky   = 0;    // last applied mask (held between entries)
let g_segtrace_fired    = {};   // anchor name -> frame it most recently fired
let g_ct_windows        = [];   // [[lo,hi],...] anchor-relative call-trace windows
let g_ct_window_mode    = false; // segtrace declares calltrace ops -> windows authoritative

// Lower a flat op list into segments: a maximal run of entries terminated by
// the `wait` that follows them (or null for the last). Op kinds:
//   {wait:NAME}    — segment break; next segment's frame 0 = NAME's fire frame
//   {frame,mask}   — set the input mask at base+frame
//   {capture:N}    — screenshot the deterministic frame base+N (a few frames
//                    after this segment's anchor, for visual state checks)
//   {calltrace:N}  — arm the call tracer for [base, base+N] (N frames from this
//                    segment's anchor) — anchor-relative, no absolute frames
function segtraceBuildSegments(ops) {
    const seg0 = () => ({entries: [], captures: [], calltraces: [],
                         wait: null, wait_until: null});
    const segs = [seg0()];
    for (let i = 0; i < ops.length; i++) {
        const op = ops[i];
        if (op && typeof op.wait === 'string') {
            segs[segs.length - 1].wait = op.wait;
            segs.push(seg0());
        } else if (op && op.wait_until && typeof op.wait_until === 'object') {
            // Threshold segment-break: like `wait`, but fires when a live
            // global crosses a comparator (e.g. hold UP until pz<=3). The
            // next segment rebases onto the frame the predicate first holds.
            const w = op.wait_until;
            segs[segs.length - 1].wait_until = {
                va: w.va | 0, type: w.type || 'f32',
                op: String(w.op || '<='), val: +w.val,
            };
            segs.push(seg0());
        } else if (op && op.capture !== undefined) {
            segs[segs.length - 1].captures.push(op.capture | 0);
        } else if (op && op.calltrace !== undefined) {
            // Scalar N -> [0, N]; [start, len] -> base-relative window.
            const ct = op.calltrace;
            segs[segs.length - 1].calltraces.push(
                Array.isArray(ct) ? [ct[0] | 0, ct[1] | 0] : [0, ct | 0]);
        } else {
            segs[segs.length - 1].entries.push(
                {frame: op.frame | 0, mask: (op.mask | 0) & 0xffff});
        }
    }
    return segs;
}

// Run once when a segment becomes active (base known): queue its capture
// frames (base+N) for the Present hook and arm its call-trace windows
// ([base, base+N]) for callTraceShouldEmit. Both land on deterministic,
// anchor-relative frames regardless of load jitter.
function segtraceOnSegmentEnter(seg) {
    if (!seg) return;
    for (let i = 0; i < seg.captures.length; i++) {
        const tgt = g_segtrace_base + seg.captures[i];
        g_capture_pending.add(tgt);
        log('segtrace: capture scheduled at base+' + seg.captures[i] +
            ' -> frame ' + tgt);
        // Anchor-relative d3d-trace arming: when d3d-trace is enabled with a
        // (possibly empty) frame Set, also trace each capture frame (±2) so a
        // render-state read lands on the same deterministic, jitter-immune
        // instants the screenshots do. Lets a segtrace drive d3d-trace without
        // guessing absolute frames (the load jitter shifts the anchor by
        // hundreds of frames run-to-run).
        if (g_d3d_trace_enabled && g_d3d_trace_frames !== null) {
            for (let d = -2; d <= 2; d++) g_d3d_trace_frames.add(tgt + d);
            log('segtrace: d3d-trace armed at frames ' + (tgt - 2) +
                '..' + (tgt + 2));
        }
    }
    for (let i = 0; i < seg.calltraces.length; i++) {
        const start = seg.calltraces[i][0], len = seg.calltraces[i][1];
        const lo = g_segtrace_base + start, hi = lo + len;
        g_ct_windows.push([lo, hi]);
        log('segtrace: call-trace armed for frames ' + lo + '..' + hi +
            ' (base+' + start + '..base+' + (start + len) + ')');
    }
}

// Per-frame global watch (`--watch NAME=0xVA:type`). When set, the agent
// reads each address once per input_poll LEAVE and emits a single
// {kind:"watch", frame, vals:{name:value}} record. A lightweight,
// general-purpose state probe — e.g. player pos DAT_056da1d8/dc/e0 (f32),
// controller state DAT_0438cc08 (s32), record-active count DAT_0076b964 (s32)
// — for locating *when* a state begins changing under a forced input.
let g_watch = [];                    // [{name:str, va:int, type:'f32'|'s32'|'u16'}]

function watchRead(w) {
    const p = rva(w.va);
    if (w.type === 'f32') return p.readFloat();
    if (w.type === 'u16') return p.readU16();
    return p.readS32();
}

// Cchr.0 table-B dump mode.  When `dump_records_b` is set the agent polls
// the records_b active-count (DAT_0076b964) every Present and anchors the
// dump window on the FIRST frame where it goes non-zero — i.e. when the 3D
// chr-walker FUN_004176ff is actually fed records (free-roam HOUSE), NOT
// the first 3D draw.  The first 3D draw lands during the intro dialogue,
// where the walker is dormant and the on-screen characters come from the
// dialogue layer rather than table B (see findings/scene1-chr-walker.md +
// the E.2 dialogue-frame note).  Anchoring on count_b>0 targets the frame
// the player record actually exists.
//
// On the anchor frame and each subsequent frame whose offset is in
// g_dump_records_b_offsets it reads the live records + the three per-pass
// counts + player pos, emits a `records_b_dump` message, and (if
// g_dump_records_b_capture) also grabs a backbuffer screenshot for visual
// confirmation.  After the last offset it sends `dump_records_b_done`.
// A `records_b_sample` heartbeat (counts + per-frame draw count) is emitted
// every g_dump_records_b_heartbeat frames so a run that never populates
// table B still reports progress instead of silently idling.
let g_dump_records_b           = false;
let g_dump_records_b_offsets   = [0, 30, 120, 300];
let g_dump_records_b_fired     = new Set();
let g_dump_records_b_done      = false;
let g_dump_records_b_capture   = false;
let g_dump_records_b_heartbeat = 1024;
let g_dump_b_anchor_frame      = -1;   // first count_b>0 frame, or -1
let g_draw_count_this_frame    = 0;    // DrawIndexedPrimitive calls this frame
let g_draw_count_max           = 0;    // peak per-frame draw count (diag)

// Cchr.1 — 2D quad-add caller histogram.  Piggybacks on the dump_records_b
// drive (same anchor + offset machinery): when g_quad_hist is set, the
// agent hooks the engine's 2D quad emitter FUN_00404efc (render_quad_add)
// plus DrawPrimitive(UP)/SetTexture, and records every call on the exact
// frames the dump offsets fire (and the frame before, to catch the player
// having moved).  Per the Cchr.0 trace the visible HOUSE characters are
// 2D billboards on a dedicated sprite path — this names the caller VA +
// texture block that emits the player/companion sprite by spotting the
// bucket whose dst rect tracks g_player_pos.
const FN_QUAD_ADD              = 0x00404efc;  // FUN_00404efc render_quad_add
let g_quad_hist                = false;
let g_quad_hist_hooked         = false;
let g_quad_record              = false;  // armed for the current frame's draws
let g_quad_events              = [];     // current frame's ordered draw events
let g_quad_events_cap          = 6000;   // per-frame event hard cap (safety)
let g_quad_hist_map            = {};     // cumulative per-ret_va aggregate

// Cchr.2b leaf-capture (rides the dump_records_b drive + the g_quad_record
// arming, like quad_hist). When set, hook the character-sprite leaf
// renderer at ENTER (dump its 5 inputs + the sheet tex dims + the
// formdata frame entry it resolves) and its own DrawPrimitiveUP (dump the
// full FVF-0x142 vertex buffer it built), so the port's
// chr_sprite_build_quads can be bit-compared against retail.
let g_chr_leaf                 = false;
let g_chr_leaf_hooked          = false;
let g_chr_leaf_events          = [];     // current frame's leaf in/out events

// Memory-access watch (Phase D.7). When `mem_watch` is true, the agent
// arms Frida's MemoryAccessMonitor over the regions in
// `g_mem_watch_regions` and emits one `mem_access` record per trapped
// access (faulting instruction VA + accessed data address, both as
// Ghidra VAs). This is a *capability unblocker*, not verification: it
// finds the writer of a region we can't locate by reading the
// decompile (indirect dispatch / dropped Ghidra output), so the chip
// that fills it can be ported. See docs/plans/d7-mem-watch.md.
//
// Page granularity + the precision problem: MemoryAccessMonitor traps at
// OS-page (4KiB) resolution and notifies only on the FIRST access of each
// page, then disables that page. So if any *page neighbor* of the watched
// field is touched before the writer we're hunting, that neighbor
// consumes the one-shot and we learn nothing about our field. (Observed
// in the var_input_mask smoke: a frame-0 startup write 0xdc8 bytes below
// the field tripped the page first.)
//
// Precise mode (default) closes this: on a trap we check whether the
// accessed address actually lands inside [va, va+size). If it's a page
// neighbor, we re-arm the monitor and DON'T record it — effectively
// single-stepping page accesses until the field itself is written. A cap
// (MEM_WATCH_REARM_CAP) bounds this so a genuinely hot page can't spin
// forever; when the cap is hit before any in-region write, the agent logs
// a warning (use the HW-breakpoint fallback for that field). In-region
// hits are recorded and we keep re-arming up to MEM_WATCH_MAX_HITS so
// multiple distinct writers of the same field all surface.
//
// Events are buffered + flushed at Present (same wire-pressure discipline
// as d3d_trace / call_trace), with a hard cap on batch size.
let g_mem_watch_enabled  = false;
let g_mem_watch_regions  = [];    // [{va, size, label, access}] — Ghidra VAs
let g_mem_watch_ranges   = [];    // cached [{base: NativePointer, size}] for re-arm
let g_mem_watch_buffer   = [];    // batched {op, from, addr, region, label}
let g_mem_watch_n        = 0;     // in-region hits recorded this session
let g_mem_watch_neighbor = 0;     // page-neighbor traps skipped (precise mode)
let g_mem_watch_rearm    = 0;     // re-arm count (budget against the cap)
let g_mem_watch_precise  = true;
const MEM_WATCH_FLUSH_AT  = 50000;
const MEM_WATCH_REARM_CAP = 8000;  // max re-arms before giving up on a hot page
const MEM_WATCH_MAX_HITS  = 64;    // stop re-arming after this many in-region hits

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

// Inverse of rva(): a live runtime pointer → its Ghidra VA (preferred
// ImageBase 0x00400000). Returns a JS number. Used by mem_watch to
// report faulting-instruction / accessed-data addresses in the same VA
// space the port-ledger + functions.csv use, so the driver can map them
// to engine functions without knowing the load base.
function toGhidraVa(p) {
    return (p.sub(g_base).toUInt32() + IMAGE_BASE.toUInt32()) >>> 0;
}

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
                // Anchor-relative captures share g_capture_pending; clear
                // the resolved-target bookkeeping too so the shutdown check
                // below can tell when every requested capture has landed.
                g_cap_anchor_pending.delete(fn);
            }
            if (g_max_frames > 0 && fn >= g_max_frames) {
                send({kind: 'max_frames_reached', frame: fn});
            }
            // Flush the D3D trace buffer BEFORE bumping the counter —
            // every state-change call during this cycle was buffered
            // with frameNo() == fn. Buffer is empty unless d3d_trace is
            // enabled AND the frame is in the (optional) filter set.
            if (g_d3d_trace_enabled) {
                traceFlush(fn);
            }
            // Same flush-before-bump invariant for call_trace.
            if (g_call_trace_enabled) {
                callTraceFlush(fn);
            }
            // ...and for the memory-access watch. Accesses trapped during
            // this cycle were buffered with frameNo() == fn.
            if (g_mem_watch_enabled) {
                memWatchFlush(fn);
            }
            // Cchr.0 table-B dump.  Anchors on count_b>0 and fires the
            // configured offsets; reset the per-frame draw counter after.
            if (g_dump_records_b) {
                dumpRecordsBTick(fn, devicePtr);
                g_draw_count_max = Math.max(g_draw_count_max,
                                            g_draw_count_this_frame);
                g_draw_count_this_frame = 0;
            }
            // TAS anchor emit. Sample scene/loading state for THIS frame
            // (frameNo() == fn) and emit any rising-edge anchors before the
            // bump, so the emitted frame matches every other fn-keyed event.
            if (g_anchor_trace_enabled) {
                try {
                    anchorTick(fn, devicePtr);
                } catch (e) {
                    err('Present.onEnter.anchor', e.message);
                }
            }
            // Anchor-relative capture shutdown: once every requested anchor
            // has fired AND every resolved target frame has been captured,
            // signal the driver to shut down (mirrors auto_3d_trace_done).
            // Waiting on g_cap_anchor_unfired (not just pending) is what lets
            // a multi-anchor spec or an offset-0 immediate capture settle
            // before we tear down.
            if (g_cap_anchor_reqs.length > 0 && !g_cap_anchor_done_sent &&
                g_cap_anchor_unfired.size === 0 &&
                g_cap_anchor_pending.size === 0) {
                g_cap_anchor_done_sent = true;
                send({kind: 'capture_at_anchor_done', frame: fn});
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
                if (g_segtrace_active) {
                    // Anchor-segmented forcing (supersedes auto_z_spam).
                    // segtraceTick rebases on the live anchor stream so the
                    // logical trace lands identically despite load jitter.
                    rva(ADDR.var_input_mask).writeU16(segtraceTick(fn));
                } else if (g_input_force_active) {
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
                } else if (g_auto_z_spam) {
                    // 2-on / 2-off pattern over every 4 frames.  ~15
                    // distinct presses per second — fast enough to clear
                    // tutorial dialogue, slow enough that the engine's
                    // 1-frame debounce sees each press as a fresh edge.
                    const pressed = (fn % 4) < 2;
                    rva(ADDR.var_input_mask).writeU16(pressed ? 0x0010 : 0);
                }

                const mask = rva(ADDR.var_input_mask).readU16();
                send({kind: 'input_state',
                      t_ms: nowMs(),
                      frame: fn,
                      buttons: mask});

                if (g_watch.length) {
                    const vals = {};
                    for (let i = 0; i < g_watch.length; i++) {
                        try { vals[g_watch[i].name] = watchRead(g_watch[i]); }
                        catch (e) { vals[g_watch[i].name] = null; }
                    }
                    send({kind: 'watch', frame: fn, vals: vals});
                }
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

// ─── MessageBox-to-log redirector ───────────────────────────────────────
//
// Catches every user32!MessageBoxA / MessageBoxW call from the retail
// process (engine code AND any DirectX runtime popups), prints the
// caption + text to the harness log via send(), and auto-returns IDOK
// so the modal never blocks an autonomous capture run.
//
// Pairs with src/msgbox_hook.c for openrecet.exe — same behavior on
// both sides of the validation pipeline.
//
// IDOK = 1 per WinUser.h. Hardcoded everywhere from MSVC's CRT to the
// SDK.
const IDOK = 1;

function installMessageBoxHook() {
    const u32 = Process.findModuleByName('user32.dll');
    if (!u32) {
        err('installMessageBoxHook', 'user32.dll module not loaded');
        return;
    }

    // Both A and W variants. The retail engine uses A everywhere
    // (Shift-JIS captions), but DirectX runtime popups may use W.
    const variants = [
        {name: 'MessageBoxA', isWide: false},
        {name: 'MessageBoxW', isWide: true},
    ];

    for (const v of variants) {
        const addr = u32.findExportByName(v.name);
        if (!addr) {
            err('installMessageBoxHook',
                'user32!' + v.name + ' not found (skipping)');
            continue;
        }
        // Interceptor.replace (not .attach) so the modal NEVER opens —
        // attach would let the real fn run and block on the OK button.
        // Replacement signature: int(HWND, LPCSTR/W, LPCSTR/W, UINT).
        const cb = new NativeCallback(function (hwnd, textPtr, capPtr, type) {
            let text = '(null)', cap = '(null)';
            try {
                if (!textPtr.isNull()) {
                    text = v.isWide
                        ? textPtr.readUtf16String()
                        : textPtr.readAnsiString();
                }
            } catch (e) { text = '(unreadable: ' + e.message + ')'; }
            try {
                if (!capPtr.isNull()) {
                    cap = v.isWide
                        ? capPtr.readUtf16String()
                        : capPtr.readAnsiString();
                }
            } catch (e) { cap = '(unreadable: ' + e.message + ')'; }
            send({kind: 'msgbox_redirected',
                  variant: v.name,
                  caption: cap,
                  text: text,
                  type: type});
            log('========== ' + v.name + ' REDIRECTED ==========');
            log('  caption: ' + cap);
            log('  text:    ' + text);
            log('  type:    0x' + type.toString(16));
            log('  return:  IDOK (auto-dismissed; would otherwise block)');
            log('============================================');
            return IDOK;
        }, 'int32', ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall');

        // Keep a reference so GC doesn't free the trampoline.
        g_messagebox_callbacks.push(cb);
        Interceptor.replace(addr, cb);
        log(v.name + ' redirector installed');
    }
}

// Global so Frida's GC doesn't free the NativeCallback trampolines
// (the Interceptor.replace would dangle).
const g_messagebox_callbacks = [];

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
            // D3D trace install gated on the init flag. Must happen
            // here (after the device pointer is live) so the vtable
            // hooks bind to real method addresses.
            if (g_d3d_trace_enabled) {
                try {
                    installD3dTraceHooks(dev);
                } catch (e) {
                    err('installD3dTraceHooks', e.message);
                }
            }
            // Auto-3D trigger needs the device's DrawIndexedPrimitive
            // vtable slot; install here for the same reason.  Shared
            // between auto_3d_trace (post-3D window) and pre_3d_trace
            // (inverse — pre-3D window, sends pre_3d_trace_done on
            // first 3D draw).
            if (g_auto_3d_trace || g_pre_3d_trace || g_dump_records_b) {
                try {
                    installAuto3dTrigger(dev);
                } catch (e) {
                    err('installAuto3dTrigger', e.message);
                }
            }
            // Cchr.1 quad-add caller trace — needs both the internal
            // FUN_00404efc hook and the device draw vtable slots.
            if (g_quad_hist) {
                try {
                    installQuadHistHooks(dev);
                } catch (e) {
                    err('installQuadHistHooks', e.message);
                }
            }
            // Cchr.2b leaf capture — also rides the dump_records_b drive.
            if (g_chr_leaf) {
                try {
                    installChrLeafHooks(dev);
                } catch (e) {
                    err('installChrLeafHooks', e.message);
                }
            }
            // Signal "device ready" so RPC callers that depend on the
            // device being live (invokeMeshLoader, …) know when it's
            // safe to fire.
            send({kind: 'd3d_device_ready', device: dev.toString()});
        },
    });
    log('d3d init hook installed @ ' + rva(ADDR.fn_d3d_init_wrapper));
}

// ─── D3D state-trace hooks (Phase D.4) ──────────────────────────────────
//
// Hook a subset of IDirect3DDevice8 vtable methods (state-changing +
// draw calls) and buffer one event per call. The Present hook flushes
// the buffer as a single batched send() — per-call send() saturates the
// Frida wire on render-heavy frames.
//
// Args reading uses Frida's `args` array. For x86 stdcall COM methods,
// args[0] is the `this` pointer (IDirect3DDevice8*), args[1] onwards are
// the method's declared params in left-to-right order.
//
// Schema (one event per call):
//   {op:"SetRenderState", args:{state:N, value:N}, ret_va:N}
//   {op:"SetTextureStageState", args:{stage:N, type:N, value:N}, ret_va:N}
//   {op:"SetTransform", args:{state:N, matrix:[16 floats]}, ret_va:N}
//   {op:"SetMaterial", args:{material:[17 floats]}, ret_va:N}
//   {op:"SetTexture", args:{stage:N, texture:"0xNN"}, ret_va:N}
//   {op:"SetStreamSource", args:{stream:N, vb:"0xNN", stride:N}, ret_va:N}
//   {op:"SetIndices", args:{ib:"0xNN", base_vertex:N}, ret_va:N}
//   {op:"SetVertexShader", args:{handle:N}, ret_va:N}
//   {op:"DrawIndexedPrimitive", args:{prim_type:N, min_idx:N,
//        num_vertices:N, start_idx:N, prim_count:N}, ret_va:N}
//   {op:"DrawIndexedPrimitiveUP", args:{prim_type:N, min_vtx_idx:N,
//        num_vtx_indices:N, prim_count:N, ib:"0xNN", ib_fmt:N,
//        vb:"0xNN", vb_stride:N}, ret_va:N}
//
// `ret_va` is module-relative (caller VA - g_base) so it maps directly
// to a Ghidra VA via + IMAGE_BASE.

function traceShouldEmit() {
    if (!g_d3d_trace_enabled) return false;
    if (g_d3d_trace_frames === null) return true;
    return g_d3d_trace_frames.has(g_manual_frame_counter);
}

function traceRetVa(returnAddress) {
    // returnAddress is a NativePointer; sub g_base for the module-
    // relative offset (uint, since the .text is well below 4GB).
    try {
        return returnAddress.sub(g_base).toUInt32() | 0;
    } catch (_) {
        return -1;
    }
}

function traceReadMatrix(ptrArg) {
    // D3DMATRIX = 16 floats, row-major. Read as a flat list so the JSON
    // diff is index-by-index without struct decoding driver-side.
    if (ptrArg.isNull()) return null;
    const out = new Array(16);
    for (let i = 0; i < 16; i++) {
        out[i] = ptrArg.add(i * 4).readFloat();
    }
    return out;
}

function traceReadMaterial(ptrArg) {
    // D3DMATERIAL8 = 4×D3DCOLORVALUE (Diffuse, Ambient, Specular,
    // Emissive — each 4 floats) + 1 float (Power) = 17 floats, 68 bytes.
    if (ptrArg.isNull()) return null;
    const out = new Array(17);
    for (let i = 0; i < 17; i++) {
        out[i] = ptrArg.add(i * 4).readFloat();
    }
    return out;
}

function traceEmit(ev) {
    g_d3d_trace_buffer.push(ev);
}

function traceFlush(frameNumber) {
    if (g_d3d_trace_buffer.length === 0) return;
    // Pull the buffer reference and swap in a fresh one before send()
    // so any re-entrant emit during marshalling doesn't double-flush.
    const events = g_d3d_trace_buffer;
    g_d3d_trace_buffer = [];
    send({kind: 'd3d_trace_batch',
          frame: frameNumber,
          count: events.length,
          events: events});
}

function installD3dTraceHooks(devicePtr) {
    if (g_d3d_trace_hooked) return;

    // SetRenderState(state, value)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetRenderState), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetRenderState',
                args: {state: args[1].toUInt32(),
                       value: args[2].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetTextureStageState(stage, type, value)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetTextureStageState), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetTextureStageState',
                args: {stage: args[1].toUInt32(),
                       type:  args[2].toUInt32(),
                       value: args[3].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetTransform(state, *matrix)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetTransform), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetTransform',
                args: {state:  args[1].toUInt32(),
                       matrix: traceReadMatrix(args[2])},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetMaterial(*material)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetMaterial), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetMaterial',
                args: {material: traceReadMaterial(args[1])},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetTexture(stage, *texture)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetTexture), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetTexture',
                args: {stage:   args[1].toUInt32(),
                       texture: '0x' + args[2].toString(16)},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetStreamSource(stream, *vb, stride)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetStreamSource), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetStreamSource',
                args: {stream: args[1].toUInt32(),
                       vb:     '0x' + args[2].toString(16),
                       stride: args[3].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetIndices(*ib, base_vertex_index)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetIndices), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetIndices',
                args: {ib:           '0x' + args[1].toString(16),
                       base_vertex:  args[2].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // SetVertexShader(handle) — handle is either an FVF code or a
    // shader handle; the trace records the raw value and lets the
    // driver disambiguate (FVF codes have the high bit clear in d3d8).
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetVertexShader), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'SetVertexShader',
                args: {handle: args[1].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // DrawPrimitive(prim_type, start_vertex, prim_count)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawPrimitive), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'DrawPrimitive',
                args: {prim_type:    args[1].toUInt32(),
                       start_vertex: args[2].toUInt32(),
                       prim_count:   args[3].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // DrawPrimitiveUP(prim_type, prim_count, *vertex_data, vertex_stride)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawPrimitiveUP), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'DrawPrimitiveUP',
                args: {prim_type:  args[1].toUInt32(),
                       prim_count: args[2].toUInt32(),
                       vb:         '0x' + args[3].toString(16),
                       vb_stride:  args[4].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // DrawIndexedPrimitive(prim_type, min_idx, num_vertices, start_idx,
    //                      prim_count)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawIndexedPrimitive), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'DrawIndexedPrimitive',
                args: {prim_type:    args[1].toUInt32(),
                       min_idx:      args[2].toUInt32(),
                       num_vertices: args[3].toUInt32(),
                       start_idx:    args[4].toUInt32(),
                       prim_count:   args[5].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    // DrawIndexedPrimitiveUP(prim_type, min_vtx_idx, num_vtx_indices,
    //                       prim_count, *index_data, index_fmt,
    //                       *vertex_data, vertex_stride)
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawIndexedPrimitiveUP), {
        onEnter: function (args) {
            if (!traceShouldEmit()) return;
            traceEmit({
                op: 'DrawIndexedPrimitiveUP',
                args: {prim_type:        args[1].toUInt32(),
                       min_vtx_idx:      args[2].toUInt32(),
                       num_vtx_indices:  args[3].toUInt32(),
                       prim_count:       args[4].toUInt32(),
                       ib:               '0x' + args[5].toString(16),
                       ib_fmt:           args[6].toUInt32(),
                       vb:               '0x' + args[7].toString(16),
                       vb_stride:        args[8].toUInt32()},
                ret_va: traceRetVa(this.returnAddress),
            });
        },
    });

    g_d3d_trace_hooked = true;
    log('d3d trace hooks installed (12 vtable slots)');
}

// ─── call tracer ────────────────────────────────────────────────────────

function callTraceShouldEmit() {
    if (!g_call_trace_enabled) return false;
    if (g_auto_3d_trace) {
        if (!g_auto_3d_seen) return false;
        const fn = g_manual_frame_counter;
        if (fn > g_auto_3d_seen_frame + g_auto_3d_trace_frames) {
            if (!g_auto_3d_done_sent) {
                g_auto_3d_done_sent = true;
                send({kind: 'auto_3d_trace_done',
                      first_frame: g_auto_3d_seen_frame,
                      last_frame:  g_auto_3d_seen_frame +
                                   g_auto_3d_trace_frames});
            }
            return false;
        }
        return true;
    }
    if (g_pre_3d_trace) {
        // Emit every frame until the first 3D draw fires.  The trigger
        // sets g_auto_3d_seen + sends pre_3d_trace_done on first call.
        return !g_auto_3d_seen;
    }
    const fn = g_manual_frame_counter;
    // Anchor-relative windows from segtrace `{calltrace:...}` ops: deterministic
    // arming keyed to segment anchors, not absolute frames. When the trace
    // declares ANY calltrace op (window mode), the windows are authoritative for
    // the WHOLE run — emit iff inside an armed window — so frames before the
    // first window is armed are NOT traced (a still-empty list ≠ "trace all").
    if (g_ct_window_mode) {
        for (let i = 0; i < g_ct_windows.length; i++) {
            if (fn >= g_ct_windows[i][0] && fn <= g_ct_windows[i][1]) return true;
        }
        return false;
    }
    if (g_call_trace_frames === null) return true;
    return g_call_trace_frames.has(fn);
}

// Hook DrawIndexedPrimitive once the D3D device is live and mark
// g_auto_3d_seen on the first call.  Idempotent.
function installAuto3dTrigger(devicePtr) {
    if (g_auto_3d_hooked) return;
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawIndexedPrimitive), {
        onEnter: function () {
            // Per-frame draw tally (diag for the records_b dump heartbeat;
            // distinguishes dialogue frames ~250 from walker frames ~1900).
            g_draw_count_this_frame++;
            if (g_auto_3d_seen) return;
            g_auto_3d_seen = true;
            g_auto_3d_seen_frame = g_manual_frame_counter;
            send({kind: 'auto_3d_scene_reached',
                  frame: g_auto_3d_seen_frame});
            // pre_3d_trace shares the trigger but inverts the semantics:
            // the first 3D draw means "we are leaving the cutscene
            // window, shut down".  Sent once.
            if (g_pre_3d_trace && !g_pre_3d_done_sent) {
                g_pre_3d_done_sent = true;
                send({kind: 'pre_3d_trace_done',
                      last_frame: g_auto_3d_seen_frame});
            }
        },
    });
    g_auto_3d_hooked = true;
    log('auto-3d trigger installed (DrawIndexedPrimitive vtable[' +
        V_Dev_DrawIndexedPrimitive + '])');
}

function callTraceFlush(frameNumber) {
    if (g_call_trace_buffer.length === 0) return;
    const events = g_call_trace_buffer;
    g_call_trace_buffer = [];
    send({kind: 'call_trace_batch',
          frame: frameNumber,
          count: events.length,
          events: events});
}

// ─── TAS anchor emitter (mirrors src/anchor_trace.c) ────────────────────
//
// Called once per Present with the manual frame number. Reads the engine
// scene-state + the two loading gates, computes rising edges against the
// previous snapshot, and emits each fired anchor as
// `{kind:"anchor", anchor:NAME, frame:N}`. The first call seeds the
// baseline and emits BOOT (no edge predicates run — there is no previous
// world to edge against). Edge rules + table order are identical to the
// port so the emitted NAMES line up; only the per-side frame numbers
// differ (that divergence is the whole point — see the plan).
const ANCHOR_SCENE_TITLE  = 0;
const ANCHOR_SCENE_INGAME = 1;

function anchorIsHouseFreeroam(scene, loading) {
    return scene === ANCHOR_SCENE_INGAME && !loading;
}

// Resolve any --capture-at-anchor requests matching the just-fired anchor
// into concrete capture frames. 1:1 with the port's anchor_capture_schedule()
// (src/main.c): offset 0 captures the anchor frame itself (we're in
// Present.onEnter pre-flip, the same sample point the normal capture path
// uses); a future offset is queued; a past offset is dropped. Removes NAME
// from g_cap_anchor_unfired so the shutdown check knows it has fired.
// Record an anchor's fire-frame for the input-segtrace state machine. Called
// for every emitted anchor (incl. BOOT) from anchorCaptureSchedule, so a
// `wait` op can resolve against any anchor name. Latest-wins: an anchor that
// re-fires (e.g. a second HOUSE_FREEROAM after a reload) updates the frame.
function segtraceOnAnchor(name, frame) {
    g_segtrace_fired[name] = frame;
}

// Evaluate a `wait_until` predicate against the live global it names. Reuses
// the watch `rva`+typed-read path; a read fault reads as "not yet" (false) so
// a transiently-unmapped address never wedges the trace.
function segtraceWaitUntilHolds(w) {
    let cur;
    try {
        const p = rva(w.va);
        cur = (w.type === 'f32') ? p.readFloat()
            : (w.type === 'u16') ? p.readU16()
            : p.readS32();
    } catch (e) { return false; }
    switch (w.op) {
        case '<=': return cur <= w.val;
        case '>=': return cur >= w.val;
        case '<':  return cur <  w.val;
        case '>':  return cur >  w.val;
        case '==': return cur === w.val;
        case '!=': return cur !== w.val;
        default:   return false;
    }
}

// Resolve the input mask for engine frame `fn` under an anchor-segmented
// trace. Each frame: if the current segment's terminating `wait` anchor has
// fired strictly after the segment was entered, advance to the next segment
// (rebasing onto the fire-frame and abandoning any remaining entries — the
// spam-until-anchor short-circuit); otherwise apply this segment's entries
// whose absolute frame (base + frame) has arrived. The sticky mask holds.
function segtraceTick(fn) {
    for (;;) {
        const seg = g_segtrace_segments[g_segtrace_seg];
        if (!seg) break;
        if (seg.wait !== null) {
            const af = g_segtrace_fired[seg.wait];
            if (af !== undefined && af > g_segtrace_base_arm) {
                g_segtrace_seg++;
                g_segtrace_base     = af;
                g_segtrace_base_arm = af;
                g_segtrace_entry    = 0;
                segtraceOnSegmentEnter(g_segtrace_segments[g_segtrace_seg]);
                continue;  // re-evaluate the next segment this same frame
            }
        } else if (seg.wait_until !== null && fn > g_segtrace_base_arm) {
            // Hold this segment's input until a live global crosses the
            // comparator (polled each frame), then rebase onto this frame.
            // Strictly-after-entry guard mirrors the anchor `wait` path so a
            // predicate already true at entry still elapses ≥1 frame.
            if (segtraceWaitUntilHolds(seg.wait_until)) {
                if (!seg.wait_until._fired) {
                    seg.wait_until._fired = true;
                    log('segtrace: wait_until 0x' +
                        (seg.wait_until.va >>> 0).toString(16) + ' ' +
                        seg.wait_until.op + ' ' + seg.wait_until.val +
                        ' satisfied at frame ' + fn);
                }
                g_segtrace_seg++;
                g_segtrace_base     = fn;
                g_segtrace_base_arm = fn;
                g_segtrace_entry    = 0;
                segtraceOnSegmentEnter(g_segtrace_segments[g_segtrace_seg]);
                continue;
            }
        }
        while (g_segtrace_entry < seg.entries.length &&
               g_segtrace_base + seg.entries[g_segtrace_entry].frame <= fn) {
            g_segtrace_sticky = seg.entries[g_segtrace_entry].mask & 0xffff;
            g_segtrace_entry++;
        }
        break;
    }
    return g_segtrace_sticky;
}

function anchorCaptureSchedule(name, frame, devicePtr) {
    segtraceOnAnchor(name, frame);
    g_cap_anchor_unfired.delete(name);
    for (let i = 0; i < g_cap_anchor_reqs.length; i++) {
        const req = g_cap_anchor_reqs[i];
        if (req.name !== name) continue;
        const target = frame + req.offset;
        if (target < frame) {
            log('anchor: capture ' + name + (req.offset >= 0 ? '+' : '') +
                req.offset + ' -> frame ' + target + ' is in the past (now ' +
                frame + ') -- dropped');
            continue;
        }
        if (target === frame) {
            // Offset 0: capture now. The normal capture check (Present
            // onEnter, before anchorTick) has already run for this frame,
            // so we must grab the backbuffer directly here rather than
            // adding `frame` to the pending set (which would miss it).
            try {
                captureBackbuffer(devicePtr, frame);
            } catch (e) {
                err('anchorCaptureSchedule', e.message + ' @ ' + e.stack);
            }
            log('anchor: captured at ' + name + '+0 -> frame ' + frame);
            continue;
        }
        g_capture_pending.add(target);
        g_cap_anchor_pending.add(target);
        log('anchor: scheduled capture at ' + name +
            (req.offset >= 0 ? '+' : '') + req.offset + ' -> frame ' + target);
    }
}

function anchorTick(frame, devicePtr) {
    // Snapshot the engine globals. loading_active = OR of both gates
    // (all.c L50058 `(DAT_06a49958==0) && (DAT_06a49960==0)`); reading
    // both keeps parity with the port's nowloading_is_active().
    const scene = rva(ADDR.var_scene_state).readS32();
    const loading = (rva(ADDR.var_nowloading_gate).readS32() !== 0) ||
                    (rva(ADDR.var_nowloading_gate2).readS32() !== 0);

    if (!g_anchor_initialized) {
        g_anchor_initialized  = true;
        g_anchor_prev_scene   = scene;
        g_anchor_prev_loading = loading;
        send({kind: 'anchor', anchor: 'BOOT', frame: frame});
        anchorCaptureSchedule('BOOT', frame, devicePtr);
        return;
    }

    const ps = g_anchor_prev_scene, pl = g_anchor_prev_loading;

    // Table order = emission order when several fire on one frame; matches
    // anchor_trace.c's g_anchors[] (causal: NEW_GAME / LOADING_START before
    // LOADING_END / HOUSE_FREEROAM).
    // NEW_GAME — TITLE → INGAME (the engine passes through LOADING in the
    // same tick, so the observable edge is TITLE → INGAME directly).
    if (ps === ANCHOR_SCENE_TITLE && scene === ANCHOR_SCENE_INGAME) {
        send({kind: 'anchor', anchor: 'NEW_GAME', frame: frame});
        anchorCaptureSchedule('NEW_GAME', frame, devicePtr);
    }
    if (!pl && loading) {
        send({kind: 'anchor', anchor: 'LOADING_START', frame: frame});
        anchorCaptureSchedule('LOADING_START', frame, devicePtr);
    }
    if (pl && !loading) {
        send({kind: 'anchor', anchor: 'LOADING_END', frame: frame});
        anchorCaptureSchedule('LOADING_END', frame, devicePtr);
    }
    if (!anchorIsHouseFreeroam(ps, pl) &&
        anchorIsHouseFreeroam(scene, loading)) {
        send({kind: 'anchor', anchor: 'HOUSE_FREEROAM', frame: frame});
        anchorCaptureSchedule('HOUSE_FREEROAM', frame, devicePtr);
    }

    g_anchor_prev_scene   = scene;
    g_anchor_prev_loading = loading;
}

// ─── Cchr.0 table-B record dump ─────────────────────────────────────────

const RECORD_B_STRIDE_DW = 0x49;   // 0x124 bytes per slot
const RECORD_B_SLOTS     = 512;
const RECORD_B_MAX_EMIT  = 96;     // cap live records emitted per dump

const RECORD_A_STRIDE_DW = 0x25;   // 0x94 bytes per slot
const RECORD_A_SLOTS     = 4096;
const RECORD_A_MAX_EMIT  = 96;
const RECORD_A_EMPTY      = 0xffffffff;  // TYPE==-1 sentinel (offset 12)

const PEOPLE_STRIDE_DW   = 0x2e9;  // 2980 bytes per entry
const PEOPLE_SLOTS       = 128;
const PEOPLE_MAX_EMIT    = 128;
// Header dword indices (byte offset / 4) per scene1-people-table.md.
const PEOPLE_OFF_ALIVE   = 0x44 / 4;   // int, 0 = empty slot
const PEOPLE_OFF_ACTION  = 0x5c / 4;   // int action_id
const PEOPLE_OFF_STATE   = 0x910 / 4;  // int state_counter
const PEOPLE_OFF_COOLDOWN= 0x934 / 4;  // int cooldown

// Lower-case hex of `n` bytes at `p`, or null on a read fault.
function hexBytes(p, n) {
    try {
        const u8 = new Uint8Array(p.readByteArray(n));
        let s = '';
        for (let i = 0; i < u8.length; i++) {
            s += (u8[i] < 16 ? '0' : '') + u8[i].toString(16);
        }
        return s;
    } catch (e) {
        return null;
    }
}

// An owner pointer stored in a record is a runtime-absolute address (the
// engine computed it live), so dereference it directly — NOT via rva().
// The chr-walker keys character type on owner+0x948; capture it plus a
// head slice for context.  Guarded: a stale/NULL owner just returns null.
function readOwnerClass(ownerVa) {
    if (!ownerVa) return null;
    try {
        const p = ptr(ownerVa >>> 0);
        return {class948: p.add(0x948).readU32(), head: hexBytes(p, 0x20)};
    } catch (e) {
        return {error: e.message};
    }
}

// Read one live table-B slot into a structured record, or null if free.
function dumpRecordBSlot(base, slot) {
    const rec = base.add(slot * RECORD_B_STRIDE_DW * 4);
    const dw  = function (n) { return rec.add(n * 4); };
    const type = dw(0).readU32();
    if (type === 0) return null;   // TYPE==0 sentinel: free slot
    const ownerA = dw(4).readU32();   // +0x10 entity-alloc owner
    const ownerB = dw(5).readU32();   // +0x14 npc-alloc owner
    const f32 = function (n) { return dw(n).readFloat(); };
    return {
        slot:       slot,
        type:       type,
        flag_a:     dw(1).readU32(),
        self_idx:   dw(2).readU32(),
        flag_b:     dw(3).readS32(),
        owner_a:    '0x' + (ownerA >>> 0).toString(16),
        owner_b:    '0x' + (ownerB >>> 0).toString(16),
        pos:        [f32(23), f32(24), f32(25)],
        vel:        [f32(26), f32(27), f32(28)],
        alt_pos:    [f32(29), f32(30), f32(31)],
        rot_x:      f32(36),
        rot_z:      f32(37),
        age:        dw(38).readU32(),
        scale_x:    f32(45),
        scale_y:    f32(47),
        owner_flag: dw(46).readU32(),
        seq_id:     dw(71).readU32(),
        owner_a_class: readOwnerClass(ownerA),
        owner_b_class: readOwnerClass(ownerB),
        raw:        hexBytes(rec, RECORD_B_STRIDE_DW * 4),
    };
}

// Read one live table-A slot (stride 0x25 dw, TYPE at offset 12 with -1 =
// empty), or null if free.  Field map per src/scene1_records.h.
function dumpRecordASlot(base, slot) {
    const rec = base.add(slot * RECORD_A_STRIDE_DW * 4);
    const dw  = function (n) { return rec.add(n * 4); };
    const type = dw(12).readU32();
    if (type === RECORD_A_EMPTY) return null;   // -1 sentinel: free slot
    const f32 = function (n) { return dw(n).readFloat(); };
    return {
        slot:    slot,
        type:    dw(12).readS32(),
        pos:     [f32(0), f32(1), f32(2)],
        vel:     [f32(3), f32(4), f32(5)],
        rot:     [f32(6), f32(7), f32(8)],
        base_xyz:[f32(9), f32(10), f32(11)],
        age:     dw(13).readU32(),
        scale:   f32(14),
        param1:  dw(16).readU32(),
        param2:  dw(17).readU32(),
        raw:     hexBytes(rec, RECORD_A_STRIDE_DW * 4),
    };
}

// Read one live people-table entry (alive!=0 at +0x44), or null if empty.
// Captures the integrator/render header fields; the character sprite the
// chr-walker draws is keyed off these (pos + the ~2900 B sprite/AI scratch
// we don't decode here).
function dumpPeopleSlot(base, slot) {
    const rec = base.add(slot * PEOPLE_STRIDE_DW * 4);
    const dw  = function (n) { return rec.add(n * 4); };
    const alive = dw(PEOPLE_OFF_ALIVE).readS32();
    if (alive === 0) return null;
    const f32 = function (n) { return dw(n).readFloat(); };
    return {
        slot:          slot,
        alive:         alive,
        pos:           [f32(0), f32(1), f32(2)],
        target:        [f32(3), f32(4), f32(5)],
        action_id:     dw(PEOPLE_OFF_ACTION).readS32(),
        state_counter: dw(PEOPLE_OFF_STATE).readS32(),
        cooldown:      dw(PEOPLE_OFF_COOLDOWN).readS32(),
        head:          hexBytes(rec, 0x68),
    };
}

// Scan both record tables, emit live records + counts + player pos for the
// given frame / anchor-relative offset as a single `records_b_dump`
// message.  Table A is included because retail's fresh-HOUSE characters
// live in table A (count_a>0), not table B (count_b==0) — see the Cchr.0
// trace finding.
function emitRecordsBDump(frameNumber, offset) {
    const baseB = rva(ADDR.var_records_b_base);
    const liveB = [];
    let liveTotalB = 0;
    for (let s = 0; s < RECORD_B_SLOTS; s++) {
        const r = dumpRecordBSlot(baseB, s);
        if (r === null) continue;
        liveTotalB++;
        if (liveB.length < RECORD_B_MAX_EMIT) liveB.push(r);
    }
    const baseA = rva(ADDR.var_records_a_base);
    const liveA = [];
    let liveTotalA = 0;
    for (let s = 0; s < RECORD_A_SLOTS; s++) {
        const r = dumpRecordASlot(baseA, s);
        if (r === null) continue;
        liveTotalA++;
        if (liveA.length < RECORD_A_MAX_EMIT) liveA.push(r);
    }
    const baseP = rva(ADDR.var_people_base);
    const liveP = [];
    let liveTotalP = 0;
    for (let s = 0; s < PEOPLE_SLOTS; s++) {
        const r = dumpPeopleSlot(baseP, s);
        if (r === null) continue;
        liveTotalP++;
        if (liveP.length < PEOPLE_MAX_EMIT) liveP.push(r);
    }
    const ppos = rva(ADDR.var_player_pos);
    send({kind: 'records_b_dump',
          frame: frameNumber,
          offset_from_3d: offset,
          count_a: rva(ADDR.var_records_a_count).readS32(),
          count_b: rva(ADDR.var_records_b_count).readS32(),
          count_c: rva(ADDR.var_records_c_count).readS32(),
          player_pos: [ppos.readFloat(),
                       ppos.add(4).readFloat(),
                       ppos.add(8).readFloat()],
          live_total:    liveTotalB,
          emitted:       liveB.length,
          records:       liveB,
          live_total_a:  liveTotalA,
          emitted_a:     liveA.length,
          records_a:     liveA,
          live_total_people: liveTotalP,
          emitted_people:    liveP.length,
          people:            liveP});
}

// ─── Cchr.1 quad-add caller trace ───────────────────────────────────────
//
// Hooks the engine's 2D quad emitter FUN_00404efc plus the device draw /
// SetTexture vtable slots.  Every hook is gated on g_quad_record (armed
// one frame ahead of each dump offset by dumpRecordsBTick), so the global
// overhead during the long fast-forward to free-roam is a single boolean
// test per call.  Recorded events go into g_quad_events (the current
// frame's ordered list, serialized + cleared at each offset) and, for
// quad-adds, the cumulative per-ret_va g_quad_hist_map (dst-rect spread +
// texture-block tally; the player sprite bucket is the one whose dst.x/y
// range is wide because the player walked across the recorded frames).
//
// FUN_00404efc(float *dst, float *src, int dim_block, D3DCOLOR diffuse):
//   dst = {x, y, w, h} (640-relative screen space, pre-scale)
//   src = {left, top, right, bottom} texel coords
//   dim_block: +0 = texture object ptr, +4 = tex_w, +8 = tex_h
//              (each loaded texture has its own .data dim block → stable
//               per-texture identity, reported as its Ghidra VA)
function quadHistRecordAdd(retVa, dst, src, dim, diffuse) {
    const dx = dst.readFloat(),     dy = dst.add(4).readFloat();
    const dw = dst.add(8).readFloat(), dh = dst.add(12).readFloat();
    let tw = 0, th = 0, tex0 = '0x0', dimVa = 0;
    try {
        tw    = dim.add(4).readU32();
        th    = dim.add(8).readU32();
        tex0  = '0x' + dim.readPointer().toString(16);
        dimVa = toGhidraVa(dim);
    } catch (e) { /* dim may be a non-.data pointer; tolerate */ }
    const diff = '0x' + (diffuse >>> 0).toString(16);

    if (g_quad_events.length < g_quad_events_cap) {
        g_quad_events.push({
            ev: 'quad', va: retVa,
            dst: [dx, dy, dw, dh],
            src: [src.readFloat(), src.add(4).readFloat(),
                  src.add(8).readFloat(), src.add(12).readFloat()],
            dim_va: dimVa, tex0: tex0, tw: tw, th: th, diffuse: diff,
        });
    }

    const key = String(retVa);
    let h = g_quad_hist_map[key];
    if (!h) {
        h = {va: retVa, count: 0,
             dx_min: dx, dx_max: dx, dy_min: dy, dy_max: dy,
             dims: {}, diffuse: diff, last_dst: null};
        g_quad_hist_map[key] = h;
    }
    h.count++;
    if (dx < h.dx_min) h.dx_min = dx;  if (dx > h.dx_max) h.dx_max = dx;
    if (dy < h.dy_min) h.dy_min = dy;  if (dy > h.dy_max) h.dy_max = dy;
    h.dims[dimVa] = (h.dims[dimVa] || 0) + 1;
    h.last_dst = [dx, dy, dw, dh];
}

function installQuadHistHooks(devicePtr) {
    if (g_quad_hist_hooked) return;

    // Internal 2D quad emitter (the per-sprite caller VA lives here; the
    // batch flush FUN_00405354 would collapse every sprite to one VA).
    Interceptor.attach(rva(FN_QUAD_ADD), {
        onEnter: function (args) {
            if (!g_quad_record) return;
            try {
                quadHistRecordAdd(toGhidraVa(this.returnAddress),
                                  args[0], args[1], args[2],
                                  args[3].toUInt32());
            } catch (e) { /* swallow — never break the render thread */ }
        },
    });

    // Device draws + texture binds, so a player sprite drawn as a 3D
    // billboard (DrawPrimitiveUP) instead of a 2D quad still surfaces,
    // and so we can attribute each draw's bound texture.
    const pushDev = function (ev) {
        if (!g_quad_record) return;
        if (g_quad_events.length < g_quad_events_cap) g_quad_events.push(ev);
    };
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetTexture), {
        onEnter: function (args) {
            pushDev({ev: 'settex', va: toGhidraVa(this.returnAddress),
                     stage: args[1].toUInt32(),
                     texture: '0x' + args[2].toString(16)});
        },
    });
    // SetTransform(state, *matrix) — billboards are positioned by the
    // WORLD (0x100) matrix, NOT their (origin-local) vertices, so capture
    // its translation row [m41,m42,m43] = offsets 48/52/56.  Matching that
    // to g_player_pos names the player's sprite draw.
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_SetTransform), {
        onEnter: function (args) {
            if (!g_quad_record) return;
            const ev = {ev: 'xform', va: toGhidraVa(this.returnAddress),
                        state: args[1].toUInt32()};
            try {
                const m = args[2];
                ev.translation = [m.add(48).readFloat(),
                                  m.add(52).readFloat(),
                                  m.add(56).readFloat()];
            } catch (e) { /* tolerate */ }
            if (g_quad_events.length < g_quad_events_cap) g_quad_events.push(ev);
        },
    });
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawPrimitive), {
        onEnter: function (args) {
            pushDev({ev: 'dp', va: toGhidraVa(this.returnAddress),
                     prim_type: args[1].toUInt32(),
                     start_vertex: args[2].toUInt32(),
                     prim_count: args[3].toUInt32()});
        },
    });
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawPrimitiveUP), {
        onEnter: function (args) {
            if (!g_quad_record) return;
            const ev = {ev: 'dpup', va: toGhidraVa(this.returnAddress),
                        prim_type: args[1].toUInt32(),
                        prim_count: args[2].toUInt32(),
                        vb_stride: args[4].toUInt32()};
            // Capture the first few vertices' position (first 3 floats of
            // each vertex).  For a stride-24 world billboard these are
            // world XYZ — matching a quad's centre to g_player_pos names
            // the player sprite; for stride-32 RHW UI quads they're screen
            // coords.  prim_type/topology is recorded so the reader knows
            // the vertex count (strip: prim+2, list: prim*3).
            try {
                const vb = args[3];
                const stride = ev.vb_stride;
                const nv = Math.min(6, ev.prim_count + 2);
                if (!vb.isNull() && stride >= 12) {
                    const vs = [];
                    for (let i = 0; i < nv; i++) {
                        const p = vb.add(i * stride);
                        vs.push([p.readFloat(), p.add(4).readFloat(),
                                 p.add(8).readFloat()]);
                    }
                    ev.verts = vs;
                }
            } catch (e) { /* tolerate unreadable UP buffer */ }
            pushDev(ev);
        },
    });

    g_quad_hist_hooked = true;
    log('quad-hist hooks installed (FUN_00404efc + 3 vtable slots)');
}

// ─── Cchr.2b character-sprite leaf capture ──────────────────────────────
//
// Hook FUN_0045a56f (cdecl, 5 stack args) at ENTER and its own
// DrawPrimitiveUP at the two in-leaf call sites.  On a recorded frame
// (g_quad_record, armed by the dump_records_b drive) each call appends:
//
//   {ev:'leaf_in',  ret_va, char_id, char_id3, color, tex_w, tex_h,
//                   actor:[17 i32], matrix:[16 f32],
//                   fd_base, fd_ncells, fd_start}
//   {ev:'leaf_out', ret_va, prim_type, prim_count, vb_stride,
//                   verts:[[x,y,z, diffuse_u32, u,v], ...]}
//
// The reader pairs leaf_in/leaf_out by order (the leaf draws once per
// call, immediately after building) and feeds leaf_in into the port's
// chr_sprite_build_quads to bit-compare against leaf_out.
function chrLeafReadActor(p) {
    // param_1 = pointer to the actor sprite-state struct (>= 0x44 bytes).
    const out = new Array(17);
    for (let i = 0; i < 17; i++) out[i] = p.add(i * 4).readS32();
    return out;
}

// The leaf's 8-entry facing→bank table (DAT_005c5a54), mirrored here so
// the capture can resolve the LUT cell exactly as the renderer does.
const CHR_FACING_BANK = [0, 2, 4, 3, 1, 3, 4, 2];

function chrLeafBeU16(p) {
    return (p.readU8() << 8) | p.add(1).readU8();
}

// Derive everything chr_sprite_build_quads consumes, straight from
// retail's descriptor + formdata, so the host diff is self-contained
// (no asset files needed): the descriptor fields, the resolved LUT cell,
// and the formdata frame entry (ncells / start / the sheet-position
// array).  All reads are wrapped — a bad pointer yields -1 fields, never
// a render-thread crash.
function chrLeafReadDerived(actor, charId) {
    const out = {
        sheet_w: -1, scale_x100: -1, y_origin: -1,
        bank: -1, cell: -1,
        fd_base: -1, fd_ncells: -1, fd_start: -1, fd_pos: [],
    };
    try {
        const anim   = actor[0] | 0;
        const frame  = actor[4] | 0;
        const facing = actor[6] | 0;
        const bank = (facing >= 0 && facing < 8) ? CHR_FACING_BANK[facing] : 0;
        out.bank = bank;

        const block = rva(ADDR.var_chr_desc_base).add(charId * 0x5058);
        out.sheet_w    = block.add(0x48).readS32();
        out.scale_x100 = block.add(0x50).readS32();
        out.y_origin   = block.add(0x54).readS32();
        const lutIdx = anim * 0x100 + frame * 6 + bank;
        const cell = block.add(0x58 + lutIdx * 4).readS32();
        out.cell = cell;

        const blob = rva(ADDR.var_chr_formdata).readU32();
        if (blob && cell >= 0) {
            const bp = ptr(blob).add(charId * 4);
            const base = (bp.readU8() << 24) | (bp.add(1).readU8() << 16) |
                         (bp.add(2).readU8() << 8) | bp.add(3).readU8();
            out.fd_base = base;
            const cellAt = ptr(blob).add(base + cell * 2);
            const ncells = chrLeafBeU16(cellAt.add(0x400));
            const start  = chrLeafBeU16(cellAt.add(0x600));
            out.fd_ncells = ncells;
            out.fd_start  = start;
            const cap = Math.min(ncells, 256);
            for (let i = 0; i < cap; i++) {
                out.fd_pos.push(
                    chrLeafBeU16(ptr(blob).add(base + (start + i) * 2 + 0x800)));
            }
        }
    } catch (e) { /* tolerate — partial derivation is still useful */ }
    return out;
}

function installChrLeafHooks(devicePtr) {
    if (g_chr_leaf_hooked) return;

    // Leaf ENTER — dump the inputs.
    Interceptor.attach(rva(ADDR.fn_chr_sprite_leaf), {
        onEnter: function (args) {
            if (!g_quad_record) return;
            if (g_chr_leaf_events.length >= g_quad_events_cap) return;
            try {
                const charId = args[1].toInt32();
                const tw = rva(ADDR.var_chr_tex_w)
                               .add(charId * 0x10).readS32();
                const th = rva(ADDR.var_chr_tex_h)
                               .add(charId * 0x10).readS32();
                const ev = {
                    ev: 'leaf_in',
                    ret_va: toGhidraVa(this.returnAddress),
                    char_id: charId,
                    char_id3: args[2].toInt32(),
                    color: args[4].toUInt32(),
                    tex_w: tw, tex_h: th,
                    actor: chrLeafReadActor(args[0]),
                    matrix: traceReadMatrix(args[3]),
                };
                const d = chrLeafReadDerived(ev.actor, charId);
                ev.sheet_w = d.sheet_w;
                ev.scale_x100 = d.scale_x100;
                ev.y_origin = d.y_origin;
                ev.bank = d.bank;
                ev.cell = d.cell;
                ev.fd_base = d.fd_base;
                ev.fd_ncells = d.fd_ncells;
                ev.fd_start = d.fd_start;
                ev.fd_pos = d.fd_pos;
                g_chr_leaf_events.push(ev);
            } catch (e) { /* never break the render thread */ }
        },
    });

    // The leaf's own DrawPrimitiveUP — dump the built vertex buffer.
    Interceptor.attach(vtableSlot(devicePtr, V_Dev_DrawPrimitiveUP), {
        onEnter: function (args) {
            if (!g_quad_record) return;
            if (g_chr_leaf_events.length >= g_quad_events_cap) return;
            const retVa = toGhidraVa(this.returnAddress);
            if (CHR_LEAF_DRAW_RETS.indexOf(retVa) < 0) return;
            try {
                const primCount = args[2].toUInt32();
                const stride    = args[4].toUInt32();
                const vb        = args[3];
                const ev = {
                    ev: 'leaf_out', ret_va: retVa,
                    prim_type: args[1].toUInt32(),
                    prim_count: primCount,
                    vb_stride: stride,
                    verts: [],
                };
                // TRIANGLELIST: vertex count = prim_count * 3.  Cap the
                // payload at 900 verts (150 quads) for safety.
                const nv = Math.min(primCount * 3, 900);
                if (!vb.isNull() && stride >= 24) {
                    for (let i = 0; i < nv; i++) {
                        const p = vb.add(i * stride);
                        ev.verts.push([
                            p.readFloat(), p.add(4).readFloat(),
                            p.add(8).readFloat(), p.add(12).readU32(),
                            p.add(16).readFloat(), p.add(20).readFloat(),
                        ]);
                    }
                }
                g_chr_leaf_events.push(ev);
            } catch (e) { /* tolerate */ }
        },
    });

    g_chr_leaf_hooked = true;
    log('chr-leaf-capture hooks installed (FUN_0045a56f + DrawPrimitiveUP)');
}

// Called from the Present hook every frame.  Polls count_b, anchors the
// dump window on its first non-zero value, fires the configured offsets
// (with optional screenshot), emits heartbeats, and signals done.
function dumpRecordsBTick(fn, devicePtr) {
    if (!g_dump_records_b) return;

    const countA = rva(ADDR.var_records_a_count).readS32();
    const countB = rva(ADDR.var_records_b_count).readS32();

    // Heartbeat — lets a non-populating run still report progress.
    if (g_dump_records_b_heartbeat > 0 &&
        (fn % g_dump_records_b_heartbeat) === 0) {
        send({kind: 'records_b_sample',
              frame: fn,
              count_a: countA,
              count_b: countB,
              count_c: rva(ADDR.var_records_c_count).readS32(),
              draws: g_draw_count_this_frame,
              draws_max: g_draw_count_max,
              anchored: g_dump_b_anchor_frame >= 0});
    }

    // Anchor on the first frame EITHER table is populated.  On a fresh
    // retail HOUSE table A fills (characters/entities) while table B stays
    // empty, so anchoring on count_b alone would never fire.
    if (g_dump_b_anchor_frame < 0) {
        if (countA <= 0 && countB <= 0) return;
        g_dump_b_anchor_frame = fn;
        send({kind: 'records_b_populated',
              frame: fn, count_a: countA, count_b: countB});
    }

    const off = fn - g_dump_b_anchor_frame;
    if (g_dump_records_b_offsets.indexOf(off) >= 0 &&
        !g_dump_records_b_fired.has(off)) {
        g_dump_records_b_fired.add(off);
        try {
            emitRecordsBDump(fn, off);
            if (g_dump_records_b_capture && devicePtr) {
                captureBackbuffer(devicePtr, fn);
            }
            // Cchr.1: g_quad_events now holds THIS frame's draws (armed at
            // the previous offset-1 frame).  Serialize the ordered list +
            // player_pos so the screenshot, the records dump and the quad
            // trace all describe the same frame.
            if (g_quad_hist) {
                const ppos = rva(ADDR.var_player_pos);
                send({kind: 'quad_frame',
                      frame: fn, offset_from_3d: off,
                      player_pos: [ppos.readFloat(), ppos.add(4).readFloat(),
                                   ppos.add(8).readFloat()],
                      event_count: g_quad_events.length,
                      events: g_quad_events});
            }
            // Cchr.2b — this frame's leaf in/out events (armed at off-1).
            if (g_chr_leaf) {
                const ppos = rva(ADDR.var_player_pos);
                let playerCharId = -1;
                try { playerCharId = rva(ADDR.var_player_char_id).readS32(); }
                catch (e) { /* tolerate */ }
                send({kind: 'chr_leaf',
                      frame: fn, offset_from_3d: off,
                      player_pos: [ppos.readFloat(), ppos.add(4).readFloat(),
                                   ppos.add(8).readFloat()],
                      player_char_id: playerCharId,
                      event_count: g_chr_leaf_events.length,
                      events: g_chr_leaf_events});
            }
        } catch (e) {
            err('emitRecordsBDump', e.message + ' @ ' + e.stack);
        }
    }

    // Cchr.1 arming: record the NEXT frame's draws iff it is itself an
    // offset frame (one frame of lead — the FUN_00404efc hook only buffers
    // while g_quad_record is true).  Clear the per-frame buffer each tick
    // so a serialized offset frame starts the next one clean.
    if (g_quad_hist || g_chr_leaf) {
        g_quad_events = [];
        g_chr_leaf_events = [];
        g_quad_record = g_dump_records_b_offsets.indexOf(off + 1) >= 0;
    }

    const lastOff =
        g_dump_records_b_offsets[g_dump_records_b_offsets.length - 1];
    if (off > lastOff && !g_dump_records_b_done) {
        g_dump_records_b_done = true;
        if (g_quad_hist) {
            // Flatten the cumulative per-ret_va aggregate (dst-rect spread
            // + texture-block tallies) for the final verdict.
            const buckets = Object.keys(g_quad_hist_map).map(function (k) {
                return g_quad_hist_map[k];
            }).sort(function (a, b) { return b.count - a.count; });
            send({kind: 'quad_hist',
                  first_frame: g_dump_b_anchor_frame, last_frame: fn,
                  bucket_count: buckets.length, buckets: buckets});
        }
        send({kind: 'dump_records_b_done',
              first_frame: g_dump_b_anchor_frame,
              last_frame:  fn});
    }
}

// ─── memory-access watch (Phase D.7) ────────────────────────────────────

function memWatchFlush(frameNumber) {
    if (g_mem_watch_buffer.length === 0) return;
    const events = g_mem_watch_buffer;
    g_mem_watch_buffer = [];
    send({kind: 'mem_access_batch',
          frame: frameNumber,
          count: events.length,
          events: events});
}

// Arm MemoryAccessMonitor over `regions` (array of {va, size, label,
// access}). va is a Ghidra VA, translated to the live address via rva().
//
// IMPORTANT — MemoryAccessMonitor semantics: it notifies on the *first*
// access of each monitored OS page, then disables monitoring of that
// page (hence the pagesCompleted/pagesTotal fields). It is NOT a
// per-access stream and it can't distinguish reads from writes at arm
// time — any touch of the page (read or write) consumes its one-shot
// trap. So we capture EVERY trapped op (read + write) and let the driver
// rank by op; dropping reads here would risk a read silently consuming
// the page before the writer we're hunting ever runs. `access` is kept
// as caller intent / metadata only.
//
// Page granularity: a tight `size` still watches the enclosing 4KiB
// page(s). Keep regions small so the field of interest is likely the
// first thing touched on its page.
//
// MemoryAccessMonitor.enable replaces any prior monitor, so calling this
// twice re-arms with the latest region set.
// Whether a trapped access landed inside the recorded extent of region
// `idx` (vs. a page neighbor that merely shares the 4KiB page).
function memWatchInRegion(idx, addrPtr) {
    const r = g_mem_watch_regions[idx];
    if (!r) return false;
    const base = rva(r.va);
    return addrPtr.compare(base) >= 0 &&
           addrPtr.compare(base.add(r.size)) < 0;
}

// (Re-)arm MemoryAccessMonitor over the cached ranges. Idempotent —
// MemoryAccessMonitor.enable() replaces any prior monitor, which is
// exactly the re-arm we want after a page's one-shot fires.
function memWatchArm() {
    MemoryAccessMonitor.enable(g_mem_watch_ranges, {onAccess: memWatchOnAccess});
}

function memWatchOnAccess(details) {
    const idx = details.rangeIndex | 0;
    const region = g_mem_watch_regions[idx] || {};
    const inRegion = memWatchInRegion(idx, details.address);

    if (g_mem_watch_precise && !inRegion) {
        // Page neighbor consumed this page's one-shot. Re-arm and keep
        // hunting, unless the page is so hot we've burned the budget.
        g_mem_watch_neighbor++;
        if (g_mem_watch_rearm < MEM_WATCH_REARM_CAP) {
            g_mem_watch_rearm++;
            try { memWatchArm(); }
            catch (e) { err('memWatchArm', e.message); }
        } else if (g_mem_watch_rearm === MEM_WATCH_REARM_CAP) {
            g_mem_watch_rearm++;   // log-once sentinel
            log('mem_watch: re-arm budget (' + MEM_WATCH_REARM_CAP +
                ') exhausted on a hot page for region "' +
                (region.label || idx) + '" before any in-region write — ' +
                'consider a narrower region or a HW write breakpoint.');
        }
        return;
    }

    // In-region hit (or non-precise mode: record everything).
    g_mem_watch_n++;
    g_mem_watch_buffer.push({
        op:     details.operation,            // read | write | execute
        from:   toGhidraVa(details.from),     // faulting insn VA
        addr:   toGhidraVa(details.address),   // accessed data VA
        region: idx,
        label:  region.label || '',
        ts:     nowMs(),
    });
    if (g_mem_watch_buffer.length >= MEM_WATCH_FLUSH_AT) {
        memWatchFlush(g_manual_frame_counter);
    }
    // Keep watching so additional distinct writers of the same field
    // surface, up to a sane cap. (In non-precise mode the one-shot has
    // disabled the page and we deliberately let it stay disabled.)
    if (g_mem_watch_precise &&
        g_mem_watch_n < MEM_WATCH_MAX_HITS &&
        g_mem_watch_rearm < MEM_WATCH_REARM_CAP) {
        g_mem_watch_rearm++;
        try { memWatchArm(); }
        catch (e) { err('memWatchArm', e.message); }
    }
}

function installMemoryWatch(regions, precise) {
    ensureBase();
    g_mem_watch_regions = (regions || []).map(function (r) {
        return {
            va:     r.va | 0,
            size:   (r.size | 0) || 16,
            label:  String(r.label || ('0x' + (r.va >>> 0).toString(16))),
            access: (r.access === 'rw') ? 'rw' : 'w',
        };
    });
    if (g_mem_watch_regions.length === 0) {
        err('installMemoryWatch', 'no regions given');
        return false;
    }
    g_mem_watch_precise  = (precise !== false);
    g_mem_watch_n        = 0;
    g_mem_watch_neighbor = 0;
    g_mem_watch_rearm    = 0;
    g_mem_watch_ranges   = g_mem_watch_regions.map(function (r) {
        return {base: rva(r.va), size: r.size};
    });

    try {
        memWatchArm();
    } catch (e) {
        err('installMemoryWatch', e.message + ' ' + (e.stack || ''));
        return false;
    }

    g_mem_watch_enabled = true;
    log('mem_watch: armed ' + g_mem_watch_regions.length + ' region(s) [' +
        (g_mem_watch_precise ? 'precise' : 'raw') + ']: ' +
        g_mem_watch_regions.map(function (r) {
            return r.label + '@0x' + (r.va >>> 0).toString(16) +
                   '+' + r.size + '(' + r.access + ')';
        }).join(', '));
    send({kind: 'mem_watch_ready',
          precise: g_mem_watch_precise,
          regions: g_mem_watch_regions.map(function (r) {
              return {va: r.va, size: r.size, label: r.label,
                      access: r.access};
          })});
    return true;
}

function installCallTraceHooks(vasArray) {
    if (g_call_trace_hooked) return;
    // closures capture `va` directly (let-binding per iteration) so each
    // hook reports its own address; using `this.context.eip` would also
    // work but reads the register on every fire.
    for (let i = 0; i < vasArray.length; i++) {
        const va = vasArray[i] | 0;
        try {
            Interceptor.attach(rva(va), {
                onEnter: function () {
                    if (!callTraceShouldEmit()) return;
                    g_call_trace_buffer.push({
                        va:     va,
                        ret_va: traceRetVa(this.returnAddress),
                        ts:     nowMs(),
                        thr:    this.threadId,
                    });
                    // Cap: per-frame flush is fine for steady-state but
                    // the pre-Present startup window (CRT/MFC init with
                    // 1979 hooks firing) can dump hundreds of thousands
                    // of events into one batch.  GLib's DBus transport
                    // tops out at 128MiB per blob — exceed that and
                    // frida-server kills the channel + the target dies
                    // process-terminated with no diagnostic.  Force a
                    // flush whenever the buffer crosses CALL_TRACE_FLUSH_AT
                    // events so individual send() messages stay bounded.
                    if (g_call_trace_buffer.length >= CALL_TRACE_FLUSH_AT) {
                        callTraceFlush(g_manual_frame_counter);
                    }
                },
            });
            g_call_trace_n_ok++;
        } catch (e) {
            // Skipped: Frida couldn't trampoline this VA (instruction
            // prefix unsupported, prior hook on the same byte, etc.).
            // Counted so the driver can spot a degraded run.
            g_call_trace_n_fail++;
        }
    }
    g_call_trace_hooked = true;
    log('call_trace: hooked ' + g_call_trace_n_ok + ' VAs (' +
        g_call_trace_n_fail + ' failed) of ' + vasArray.length);
    send({kind: 'call_trace_hooked',
          n_ok:   g_call_trace_n_ok,
          n_fail: g_call_trace_n_fail,
          n_req:  vasArray.length});
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

        // TAS P3 — anchor-segmented input forcing. input_segtrace is the
        // ordered op list ({wait:NAME} | {frame:k, mask:int}); when present it
        // owns the input mask (checked before force_input / auto_z_spam) and
        // implies the anchor poll (a `wait` op needs the fired-frame stream).
        g_segtrace_active   = false;
        g_segtrace_segments = [];
        g_segtrace_seg      = 0;
        g_segtrace_entry    = 0;
        g_segtrace_base     = 0;
        g_segtrace_base_arm = 0;
        g_segtrace_sticky   = 0;
        g_segtrace_fired    = {};
        g_ct_windows        = [];
        g_ct_window_mode    = false;
        if (Array.isArray(config.input_segtrace) &&
            config.input_segtrace.length > 0) {
            g_segtrace_segments = segtraceBuildSegments(config.input_segtrace);
            g_segtrace_active = true;
            // Window mode: if any segment declares a calltrace op, the call
            // tracer emits ONLY inside armed anchor-relative windows.
            g_ct_window_mode = g_segtrace_segments.some(
                function (s) { return s.calltraces.length > 0; });
            // Segment 0 has base 0 from boot; arm its captures/call-trace now.
            segtraceOnSegmentEnter(g_segtrace_segments[0]);
        }

        // Per-frame global watch. [{name, va, type}] read each input_poll.
        g_watch = Array.isArray(config.watch)
            ? config.watch.map(function (w) {
                  return {name: String(w.name), va: w.va | 0,
                          type: (w.type === 'f32' || w.type === 'u16')
                                ? w.type : 's32'};
              })
            : [];

        g_hide_window         = !!config.hide_window;
        g_show_window_handled = false;

        // diff_test: true implies install_hooks: false (the engine main
        // thread stays suspended, so capture-side hooks would never
        // fire anyway). Tracked as its own flag so the runRetail* RPCs
        // can gate themselves.
        g_diff_test_enabled = !!config.diff_test;

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

        // D3D state-trace (Phase D.4). When d3d_trace:true, the agent
        // installs vtable hooks on Direct3DDevice8 methods as soon as
        // the device pointer is live (driven by the d3d_init_wrapper
        // hook in installInitHook). d3d_trace_frames is an optional
        // list of frame numbers — when set, only those frames have
        // their events buffered + flushed (use this to keep output
        // small for render-heavy scenarios; INGAME frames can run 1000+
        // state-change calls each). Null = unfiltered (every frame).
        g_d3d_trace_enabled = !!config.d3d_trace;
        g_d3d_trace_hooked  = false;
        g_d3d_trace_buffer  = [];
        if (Array.isArray(config.d3d_trace_frames)) {
            g_d3d_trace_frames = new Set(
                config.d3d_trace_frames.map(function (f) { return f | 0; }));
        } else {
            g_d3d_trace_frames = null;
        }

        // Call tracer (Phase E.1). When call_trace:true, hook every VA
        // in call_trace_vas (array of Ghidra-VAs; default empty = no-op)
        // with an onEnter that pushes one record per invocation into a
        // per-frame buffer flushed at Present. call_trace_frames is an
        // optional whitelist — when set, only those frames emit; the
        // hook still fires on every other frame but the early-return in
        // callTraceShouldEmit() makes it cheap.
        g_call_trace_enabled = !!config.call_trace;
        g_call_trace_hooked  = false;
        g_call_trace_buffer  = [];
        g_call_trace_n_ok    = 0;
        g_call_trace_n_fail  = 0;
        g_call_trace_vas     = Array.isArray(config.call_trace_vas)
            ? config.call_trace_vas.map(function (v) { return v | 0; })
            : [];
        if (Array.isArray(config.call_trace_frames)) {
            g_call_trace_frames = new Set(
                config.call_trace_frames.map(function (f) { return f | 0; }));
        } else {
            g_call_trace_frames = null;
        }

        // Auto-Z-spam + auto-3D-trace.  Pair these to drive retail past
        // the intro cutscene unattended and start capturing once the
        // HOUSE / 3D scene activates.  auto_3d_trace overrides the
        // call_trace_frames whitelist with a dynamic window anchored on
        // the first DrawIndexedPrimitive.
        g_auto_z_spam            = !!config.auto_z_spam;
        g_auto_3d_trace          = !!config.auto_3d_trace;
        g_auto_3d_seen           = false;
        g_auto_3d_seen_frame     = -1;
        g_auto_3d_hooked         = false;
        g_auto_3d_done_sent      = false;
        g_auto_3d_trace_frames   = (config.auto_3d_trace_frames | 0) || 60;
        g_pre_3d_trace           = !!config.pre_3d_trace;
        g_pre_3d_done_sent       = false;

        // TAS anchor emitter (P1 retail side). When anchor_trace:true the
        // Present hook samples the scene/loading globals each frame and
        // emits {kind:"anchor",...} on rising edges. Pure read-only poll —
        // pairs with auto_z_spam (drive past the title to HOUSE) and the
        // capture flags. Reset the edge state so a re-init starts clean.
        g_anchor_trace_enabled = !!config.anchor_trace;
        g_anchor_initialized   = false;
        g_anchor_prev_scene    = 0;
        g_anchor_prev_loading  = false;
        // A `wait` op resolves against the live anchor stream, so segtrace
        // needs the anchor poll running regardless of --anchor-trace.
        if (g_segtrace_active) g_anchor_trace_enabled = true;

        // TAS P2 retail — anchor-relative capture. capture_at_anchor is a
        // list of {name, offset}; each resolves to a capture at
        // anchor_frame + offset when NAME fires (see anchorCaptureSchedule).
        // It implies the anchor poll, so force anchor_trace on when present.
        g_cap_anchor_reqs      = [];
        g_cap_anchor_unfired   = new Set();
        g_cap_anchor_pending   = new Set();
        g_cap_anchor_done_sent = false;
        if (Array.isArray(config.capture_at_anchor)) {
            for (const r of config.capture_at_anchor) {
                if (!r || typeof r.name !== 'string') continue;
                g_cap_anchor_reqs.push({name: r.name, offset: (r.offset | 0)});
                g_cap_anchor_unfired.add(r.name);
            }
            if (g_cap_anchor_reqs.length > 0) g_anchor_trace_enabled = true;
        }

        // Cchr.0 table-B dump.  Anchors on the first count_b>0 frame; pair
        // with auto_z_spam to drive a fresh new-game to HOUSE.
        // dump_records_b_offsets is an optional list of frame offsets from
        // the anchor (default [0, 30, 120, 300]; sorted ascending so the
        // terminal-done check reads the last entry).  dump_records_b_capture
        // also grabs a backbuffer screenshot at each dump frame.
        g_dump_records_b         = !!config.dump_records_b;
        g_dump_records_b_capture = !!config.dump_records_b_capture;
        g_dump_records_b_fired   = new Set();
        g_dump_records_b_done    = false;
        g_dump_b_anchor_frame    = -1;
        g_draw_count_this_frame  = 0;
        g_draw_count_max         = 0;
        g_dump_records_b_heartbeat =
            (config.dump_records_b_heartbeat | 0) || 1024;
        if (Array.isArray(config.dump_records_b_offsets) &&
            config.dump_records_b_offsets.length > 0) {
            g_dump_records_b_offsets = config.dump_records_b_offsets
                .map(function (o) { return o | 0; })
                .sort(function (a, b) { return a - b; });
        } else {
            g_dump_records_b_offsets = [0, 30, 120, 300];
        }

        // Cchr.1 quad-add caller trace (rides the dump_records_b drive).
        g_quad_hist        = !!config.quad_hist;
        g_quad_hist_hooked = false;
        // Cchr.2b leaf capture (rides the same dump_records_b drive).
        g_chr_leaf         = !!config.chr_leaf;
        g_chr_leaf_hooked  = false;
        g_chr_leaf_events  = [];
        g_quad_record      = false;
        g_quad_events      = [];
        g_quad_hist_map    = {};

        // Memory-access watch (Phase D.7). The region list is parsed
        // here; the monitor is armed below (after ensureBase, before
        // resume) so the very first write — including an init-time
        // writer during HOUSE bootstrap — is trapped. Regions carry
        // Ghidra VAs; see installMemoryWatch.
        g_mem_watch_enabled  = false;
        g_mem_watch_buffer   = [];
        g_mem_watch_n        = 0;
        g_mem_watch_neighbor = 0;
        g_mem_watch_rearm    = 0;
        // Default precise (re-arm on page-neighbor traps). Pass
        // mem_watch_precise:false for the raw one-shot-per-page behavior.
        g_mem_watch_precise  = (config.mem_watch_precise !== false);
        g_mem_watch_regions  = Array.isArray(config.mem_watch_regions)
            ? config.mem_watch_regions
            : [];
        const wantMemWatch = !!config.mem_watch &&
                             g_mem_watch_regions.length > 0;

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
              silent_audio: g_silent_audio_enabled,
              diff_test: g_diff_test_enabled,
              d3d_trace: g_d3d_trace_enabled,
              d3d_trace_frames: g_d3d_trace_frames === null
                  ? null : Array.from(g_d3d_trace_frames),
              call_trace: g_call_trace_enabled,
              call_trace_n_vas: g_call_trace_vas.length,
              call_trace_frames: g_call_trace_frames === null
                  ? null : Array.from(g_call_trace_frames),
              auto_z_spam: g_auto_z_spam,
              auto_3d_trace: g_auto_3d_trace,
              auto_3d_trace_frames: g_auto_3d_trace_frames,
              pre_3d_trace: g_pre_3d_trace,
              anchor_trace: g_anchor_trace_enabled,
              capture_at_anchor: g_cap_anchor_reqs,
              dump_records_b: g_dump_records_b,
              dump_records_b_offsets: g_dump_records_b_offsets,
              dump_records_b_capture: g_dump_records_b_capture,
              mem_watch: wantMemWatch,
              mem_watch_regions: g_mem_watch_regions.length});

        // The capture-side hooks expect to fire on a running engine.
        // State-forcing tests skip resume() entirely, so we make the
        // hook installation opt-in via config.install_hooks (default
        // true to preserve the Phase B capture pipeline behavior).
        // diff_test: true forces the same skip — explicit flag, same
        // effect as install_hooks: false.
        const install = !g_diff_test_enabled &&
                        config.install_hooks !== false;
        if (install) {
            // Install the MessageBox redirector FIRST so any popup from
            // subsequent installers — or from the engine's own boot path
            // — gets caught and never blocks the harness.
            installMessageBoxHook();
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
            // Call tracer — install last (after other hooks) so the
            // call_trace doesn't also fire on hook-injected detour
            // entries. Hooking 2000+ trampolines takes a couple seconds
            // pre-resume; the engine doesn't start running until
            // device.resume(pid) on the driver side.
            if (g_call_trace_enabled && g_call_trace_vas.length > 0) {
                installCallTraceHooks(g_call_trace_vas);
            }
            // Memory-access watch — arm pre-resume so an init-time writer
            // during HOUSE bootstrap is trapped on its first write. The
            // watched pages are static .data/.bss globals, committed at
            // image load (well before this point), so enable() succeeds.
            if (wantMemWatch) {
                installMemoryWatch(g_mem_watch_regions, g_mem_watch_precise);
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

    // Phase D.7 late-install entry point. Arms MemoryAccessMonitor over
    // the given regions after the engine is already running — use when
    // the writer fires at a known scene transition and watching from
    // boot would trap unrelated earlier writes. Most callers instead set
    // mem_watch + mem_watch_regions in init() to arm pre-resume.
    // regions: [{va: <Ghidra VA>, size, label, access: "w"|"rw"}]
    installMemoryWatch: function (regions, precise) {
        return installMemoryWatch(regions, precise);
    },

    getMemWatchStatus: function () {
        return {
            enabled:    g_mem_watch_enabled,
            precise:    g_mem_watch_precise,
            n_inregion: g_mem_watch_n,
            n_neighbor: g_mem_watch_neighbor,
            n_rearm:    g_mem_watch_rearm,
            buffered:   g_mem_watch_buffer.length,
            regions:    g_mem_watch_regions.length,
        };
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

    // ── Phase D differential-test RPCs ──
    //
    // Each runRetail* RPC follows the same pattern: snapshot the engine
    // globals it touches, inject inputs, call the retail function,
    // observe outputs, restore in a finally block. This guarantees the
    // engine state is left exactly as it was found — so back-to-back
    // RPCs with different inputs don't accidentally chain through a
    // shared global.
    //
    // The agent gates these on g_diff_test_enabled to prevent the
    // capture pipeline from calling them by mistake (those callers run
    // with a live engine main thread; a per-RPC global stomp could
    // race with engine reads).

    // rng_next15 (FUN_005041f6 reading/writing DAT_006023a0).
    // Injects `seed_u32` as the pre-state, calls one LCG step, reads
    // back the post-state and the 15-bit return value, restores.
    //
    // Returns {ret_value: u16, post_state: u32}.
    //
    // Race surface: FUN_005041f6 is called from many engine sites
    // (particle init, RNG consumers across scene1). With the engine
    // main thread suspended (diff_test mode), there's nothing else
    // reading or writing DAT_006023a0 between our snapshot and the
    // restore. The Frida helper thread is single-threaded for our
    // RPCs, so concurrent runRetailRngNext15 invocations serialise.
    runRetailRngNext15: function (seed_u32) {
        if (!g_diff_test_enabled) {
            throw new Error(
                'runRetailRngNext15: diff_test mode required ' +
                '(init with diff_test: true)');
        }
        ensureBase();
        const seedPtr = rva(ADDR.var_lcg_seed);
        const saved = seedPtr.readU32();
        try {
            seedPtr.writeU32(seed_u32 >>> 0);
            const fn = new NativeFunction(rva(ADDR.fn_lcg_step),
                                          'uint32', []);
            const ret = fn() >>> 0;
            const post = seedPtr.readU32() >>> 0;
            // FUN_005041f6 returns the 15-bit field directly via EAX.
            // Mask defensively in case a future engine variant widens
            // the return to a full u16 / u32 — the diff harness only
            // compares the 15 bits the engine actually uses.
            return {ret_value: ret & 0x7fff, post_state: post};
        } finally {
            seedPtr.writeU32(saved);
        }
    },

    // E.4 Tier 1 — stage_gate_boss_id_allowed (FUN_00431990).
    //
    // Pure cdecl(int)->int range predicate; no engine globals. This is
    // the first diff target that injects an ARG rather than a global —
    // the id rides in on the stack ([esp+4]), Frida marshals it via the
    // ['int'] arg list, and EAX comes back as the 0/1 result. No
    // snapshot/restore needed (nothing in the engine is touched).
    //
    // Returns {allowed: 0|1}.
    runRetailStageGateBossIdAllowed: function (enemy_id) {
        if (!g_diff_test_enabled) {
            throw new Error(
                'runRetailStageGateBossIdAllowed: diff_test mode required ' +
                '(init with diff_test: true)');
        }
        ensureBase();
        const fn = new NativeFunction(rva(ADDR.fn_stage_gate_boss_id),
                                      'int', ['int']);
        return {allowed: fn(enemy_id | 0) | 0};
    },

    // E.4 Tier 1 — stage_gate_floor_is_checkpoint (FUN_0043195d).
    //
    // The canonical stateful pure-ish leaf: no args, reads two engine
    // globals (DAT_0438b4c8 dungeon id, DAT_0438b4cc next floor) and
    // returns 0/1. Snapshot both, inject the vector, call, read back,
    // restore in a finally — so back-to-back vectors don't chain through
    // a shared global. With the engine main thread suspended (diff_test
    // mode) there's no concurrent reader/writer of either global between
    // our snapshot and restore.
    //
    // Both globals are signed i32 in the engine (next_floor goes through
    // a signed idiv for `% 5`); we round-trip the raw 32 bits via
    // writeS32/readU32 so negative test vectors land bit-exact.
    //
    // Returns {is_checkpoint: 0|1}.
    runRetailStageGateFloorIsCheckpoint: function (dungeon_id, next_floor) {
        if (!g_diff_test_enabled) {
            throw new Error(
                'runRetailStageGateFloorIsCheckpoint: diff_test mode ' +
                'required (init with diff_test: true)');
        }
        ensureBase();
        const pDungeon = rva(ADDR.var_stage_dungeon_id);
        const pFloor   = rva(ADDR.var_stage_next_floor);
        const savedDungeon = pDungeon.readU32();
        const savedFloor   = pFloor.readU32();
        try {
            pDungeon.writeS32(dungeon_id | 0);
            pFloor.writeS32(next_floor | 0);
            const fn = new NativeFunction(rva(ADDR.fn_stage_gate_checkpoint),
                                          'int', []);
            return {is_checkpoint: fn() | 0};
        } finally {
            pDungeon.writeU32(savedDungeon);
            pFloor.writeU32(savedFloor);
        }
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
