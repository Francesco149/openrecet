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
//   FUN_00499200  audio_play_track   → {kind:"bgm_swap", track}  (dedup on change)
//   FUN_00499c63  audio_play_se      → {kind:"se_play",  slot, name}
//   FUN_0049933c  audio_play_se_file → {kind:"se_play",  slot:-1, name:path}
//   FUN_0047b73c  input_poll exit    → {kind:"input_state", buttons:0xNNNN}
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
//   {kind:"bgm_swap",   t_ms:T, frame:N, track:N}
//   {kind:"se_play",    t_ms:T, frame:N, slot:N, name:"se_NNN_idXXXX"|path}
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

    // FUN_004523e6 — the bottom-right "Fps NN" debug overlay draw. NOP'd
    // (Interceptor.replace) when hiding fps for clean comparisons: its value
    // is wall-clock derived, a noisy cross-target / golden delta. Mirrors the
    // port's capture-default-hide so both targets match.
    fn_fps_draw:         0x004523e6,

    // audio entry points (see docs/findings/audio-backend.md table).
    fn_audio_play_track: 0x00499200,  // BGM swap            (port audio_play_track)
    fn_audio_play_se:    0x00499c63,  // SE start/stop (slot) (port audio_play_se)
    fn_audio_play_se_file: 0x0049933c,// filename/voice SE   (port audio_play_se_file)
    // SE resource-id table (DAT_005d1584): 110 entries, 8-byte stride, id at
    // +0 / channel-flag at +4. Lets the agent rebuild the port's
    // `se_NNN_idXXXX` SE name so audio_diff output reads identically on both
    // sides. Current BGM track (DAT_005d1960) drives the bgm_swap dedup so
    // retail emits only on an actual track change, like the port.
    var_se_id_table:     0x005d1584,
    var_bgm_cur_track:   0x005d1960,

    // input poll (see docs/findings/winmain-and-bootstrap.md §"Input poll").
    fn_input_poll:       0x0047b73c,

    // TAS save capture (retail recorder). The engine reserves the ~18 MB save
    // arena at DAT_056e5770 (size 0x011f7530; see src/save_bank.h). FUN_004905a8
    // writes it to save.dat/_save.dat. The recorder snapshots the arena at
    // record-start (the trace's initial save) and after each FUN_004905a8 write
    // (each in-session save), so a retail recording carries its save state for
    // the port to replay against.
    var_save_arena:      0x056e5770,
    fn_save_write:       0x004905a8,

    // In-game PAUSE menu state (DAT_0438b150; set 1 by scene_pause_state_init,
    // cleared 0 on close). Drives the PAUSE_OPEN/PAUSE_CLOSE anchors so the
    // save/quit-to-title menu navigation in a TAS trace re-syncs to the menu's
    // own edge instead of drifting between the coarse LOADING anchors.
    var_pause_state:     0x0438b150,

    // Pause SAVE submenu state, for the SAVE_PICKER_READY anchor (the picker is
    // navigable ⇔ scene==9 && sub_anim==10 && entries[sel]==3). Same VAs the
    // port models (scene_pause.c): sub_anim DAT_074b2880, entry-type list
    // DAT_074b2844[], selected index DAT_074b2878.
    var_pause_sub_anim:  0x074b2880,
    var_pause_entries:   0x074b2844,
    var_pause_sel:       0x074b2878,

    // TITLE Continue/load picker state, for the TITLE_PICKER_READY anchor (the
    // picker is navigable ⇔ scene==0 && submenu_state==1 && cursor_anim==10).
    // Same DATs the port models (scene_title.h): submenu_state DAT_09643524,
    // cursor_anim DAT_09643520 (the 0..10 fold-in tween).
    var_title_submenu_state: 0x09643524,
    var_title_cursor_anim:   0x09643520,
    var_title_survival_state: 0x09643550,  // DAT_09643550 — Survival selector ramp (8 = open at rest)

    // WndProc ESC → skip-event entry (FUN_00453384(0)). Called when the user
    // presses ESC during a skippable event (the intro dialogues). The {esc}
    // replay path PostMessageA(WM_KEYDOWN,ESC)s into WndProc → here; hooking
    // its ENTRY captures the user's real ESC presses for the recorder (ESC is
    // keyboard/WndProc-only, NOT in the DInput mask DAT_073dddd0). See
    // docs/findings/esc-skip-event.md.
    fn_wndproc_esc_skip: 0x0045337b,

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
    rng_set_seed:        0x005041ec,  // void(seed) — DAT_006023a0 = seed; the
                                       // engine's srand, called EXACTLY once at
                                       // WinMain (FUN_005045eb wall-clock reseed,
                                       // all.c:78946).  Pin point — mirrors the
                                       // port's --rng-seed.
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
    var_worker_busy_primary: 0x06a49954, // i32 DAT_06a49954 — the PRIMARY worker
                                       // busy flag (FUN_00452911; the sim early-
                                       // return gate at FUN_004536cb).  Set 1 at
                                       // the primary spawn (FUN_00452cde), cleared
                                       // 0 by LAB_0045293d's tail when the load
                                       // finishes.  {primaryloadpin} drains it.
    var_nowloading_gate2: 0x06a49960, // i32 DAT_06a49960 — secondary load
                                       // gate. The engine's loading-overlay
                                       // test is `(DAT_06a49958==0) &&
                                       // (DAT_06a49960==0)` (all.c L50058),
                                       // so loading_active = OR of the two —
                                       // the port collapses both in
                                       // nowloading_is_active().

    // Opening-prologue dialogue anchors (TEXT_ANIM_START/END — see
    // docs/findings/opening-prologue.md §RESOLVED). The dialogue engine is
    // the 0x46c cluster, gated by DAT_0438b1c8==1; the per-char typewriter
    // reveal counter DAT_073a3e00 resets to 1 on each new line (START), and
    // the "fully revealed / awaiting input" flag DAT_073a3e04 rises 0->1 when
    // a line finishes scrolling (END). Both recur per line.
    var_dlg_active:        0x0438b1c8, // i32 — dialogue active gate (==1;
                                       // ==2 armed/loading — the tutloadpin
                                       // bracket state).
    fn_dlg_load_worker_tail: 0x00452ac2, // LAB_00452aab worker tail: the mov
                                       // run AFTER its CloseHandle — from here
                                       // it clears DAT_06a49950/5c/60 and sets
                                       // DAT_0438b1c8=1 (the WHOLE bracket-end
                                       // handoff, on the worker thread). The
                                       // {tutloadpin} CModule blocks HERE.
    var_dlg_reveal_ctr:    0x073a3e00, // i32 — per-char reveal counter
                                       // (1..0x800); resets to 1 per new line.
    var_dlg_revealed_flag: 0x073a3e04, // i32 — line fully-revealed flag (0->1).

    // cc08==4 customer-service d3e asset-load gate (RE §20) — the {csloadpin}
    // analogue of the dialogue worker tail above.  DAT_0438b1cc==2 while the
    // d3e worker loads; it → 1 at the worker tail, right after CloseHandle.
    // Blocking those tails holds b1cc==2 so the 目玉 sparkle (which fires
    // throughout the window) consumes an N-frame-deterministic rng count.  Both
    // tails are byte-identical (verified via objdump): the block point is the
    // first insn after CloseHandle returns, before the gate-clears + b1cc=1.
    fn_d3e_load_worker_tail_ae8: 0x00452af9, // LAB_00452ae8 (d3e param-0 / session_init)
    fn_d3e_load_worker_tail_b13: 0x00452b24, // LAB_00452b13 (d3e param-1 / occ3 reload)
    var_cs_load_gate:            0x0438b1cc, // i32 — the b1cc load gate (==2 loading)

    // Conversation pose (engine-quirks §86): the player actor state-machine
    // field DAT_056daafc. FUN_0048407f sets it to 6 (Recette listen-pose) while
    // the talk-event flag DAT_0450f470 is clear during the iv1_2 face-to-face,
    // 0 in free-roam. Drives CONV_POSE_START/END — the per-effect anchor the
    // blink cycle resets on (the port mirror is the actor record's
    // CHR_ACTOR_STATE, scene1_conversation_pose_player_state()).
    var_player_state:      0x056daafc, // i32 — actor 0 state (6 = conversation)
    var_player_frame:      0x056daaf8, // i32 — actor 0 anim frame idx. Anim 6's
                                       // loop is cells [38(d20),39(d6),38(d32),
                                       // 39(d6)]; frame 1 = the d20-preceded
                                       // eyes-closed/cell-39 blink — a UNIQUE
                                       // once-per-cycle CONV_POSE_BLINK sync
                                       // (frame 3 is also cell 39 but a different
                                       // cycle phase: next blink +26 vs +38).
    var_cc08:              0x0438cc08, // i32 DAT_0438cc08 — in-game interaction
                                       // state: 1 free-roam, 4 = the cc08==4
                                       // customer-service / price-haggle SELLING
                                       // mode (Z at the sell counter). Drives
                                       // CUSTOMER_SERVICE_ENTER (non-4 → 4).

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
let g_capture_stride = 1;           // with capture_all: capture every Nth frame
let g_segtrace_capstride = 1;       // {capstride:N} (D3): thin a {caprange} to every
                                    // Nth frame from its start (trace-global OVERVIEW);
                                    // mirrors the port's g_capture_stride so both
                                    // targets keep the identical kept-set, ordinal-paired
let g_suppress_loads = false;       // D1: drop captures while loading_active (opt-in;
                                    // mirrors the port's --capture-suppress-loads)
let g_segtrace_tutloadpin = 0;      // {tutloadpin:N}: extend every tutorial-dialogue
                                    // load bracket to N frames (engine-quirks §119);
                                    // 0 = off. Mirrors the port's
                                    // IVE_TUT_LOAD_FRAMES override.
let g_tlp_armed = false;            // tutloadpin: inside a pinned bracket
let g_tlp_release_frame = 0;        // tutloadpin: frame the bracket must end on
let g_tlp_prev_b1c8 = 0;            // tutloadpin: previous tick's DAT_0438b1c8
let g_tlp_flags = null;             // tutloadpin: CModule-shared [release, waiting]
let g_tlp_hook_installed = false;   // tutloadpin: worker-tail CModule attached

// {csloadpin:N} — the cc08==4 d3e load-bracket pin (RE §20): a second instance
// of the tutloadpin worker-tail blocker, on the b1cc gate + the two d3e tails.
let g_segtrace_csloadpin = 0;       // active N (0 = no pin)
let g_csl_armed = false;            // inside a pinned bracket
let g_csl_release_frame = 0;        // frame the bracket must end on
let g_csl_prev_b1cc = 0;            // previous tick's DAT_0438b1cc
let g_csl_flags = null;             // CModule-shared [release, waiting]
let g_csl_hook_installed = false;   // d3e worker-tail CModule attached
let g_csl_cmodule = null;           // keep the CModule alive (GC = crash)

// {primaryloadpin:N} — the cad868 PRIMARY-worker load-duration pin (RE §21.19(b)):
// drain DAT_06a49954 to completion at N frames so the initial Continue-load / scene
// reload lasts a DETERMINISTIC N frames (mirror of the port's
// worker_load_force_primary_complete).  No CModule needed — the worker runs on its
// own thread, so a main-thread (Present) spin waiting for busy==0 drains it.
let g_segtrace_primaryloadpin = 0;  // active N (0 = no pin)
let g_plp_armed = false;            // inside a pinned primary-load bracket
let g_plp_release_frame = 0;        // frame the primary load must end on
let g_plp_prev_busy = 0;            // previous tick's DAT_06a49954

// {bgnpcseed:V} — seed the bg-NPC warmup's LCG origin to V right before its
// NATURAL first-ever tick (RE §21.21): a narrower alternative to {phasepin}'s
// full re-arm (which also zeros db054/anim/b154/rmb and stalls the skip-path
// wrap-up cutscene).  Applied in installBgNpcPinHook, gated on the warmup
// latch (DAT_073a8bb8) still being 0 — i.e. the actual first call, not a re-arm.
let g_segtrace_bgnpcseed_active = false;  // a {bgnpcseed} op is armed
let g_segtrace_bgnpcseed = 0;             // the seed value to force
let g_segtrace_bgnpcseed_cursor = 0;      // the spawn-cursor value to force
// Dead-slot raw engine dwords (RE §21.22): [0,cursor) records, 25 dwords each,
// {bgnpcpin}-format — written into DAT_073a7f80 at the SAME instant as the
// seed/cursor.  Needed because the shadow pass only checks visible==-1, not
// dir==0, so a dead slot's leftover x/y/z still feeds a drawn contact shadow.
// Empty array = no-op (this drive's {bgnpcseed} didn't carry one).
let g_segtrace_bgnpcseed_dead = [];
let g_bgnpcseed_applied = false;          // one-shot: fired once, ever

let g_capture_dir = null;           // Windows dir: write raw frames here (no Frida xfer)
let g_memsnap_regions = [];         // {memsnap} census: [[abs_va, size], ...] to dump
let g_max_frames = 0;               // 0 = no cap; stop = die after that many sim frames

// TAS save virtualization. When set (Windows path to a per-run sandbox dir),
// every save.dat/_save.dat file open is redirected into the sandbox so a trace
// replay NEVER touches the user's real save (read OR write). Seeded by the
// harness: a "continue" trace pre-places its save in the sandbox; "@fresh"
// leaves it empty (game boots fresh + any write lands in the sandbox).
let g_save_sandbox = null;
const g_save_redirect_keep = [];    // keep redirected path strings alive past onEnter

// TAS save CAPTURE (retail recorder). When set, snapshot the 18 MB save arena at
// record-start (the trace's initial save) and after each FUN_004905a8 write (each
// in-session save), sending the bytes to the driver so a retail recording carries
// its save state for the port to replay against.
let g_capture_saves      = false;
let g_save_boot_captured = false;
let g_save_write_idx     = 0;
const SAVE_ARENA_SIZE    = 0x011f7530;
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

// Skip-event probe: when >= 0, directly invoke FUN_0045337b (the WndProc ESC
// → skip-event entry, FUN_00453384(0)) once at this frame. Used to confirm
// prologue skippability + observe the skip-prompt counter choreography, since
// the skip is keyboard-ESC-only (WndProc) and not reachable via DInput
// injection. See docs/findings/esc-skip-event.md.
let g_arm_skip_at_frame = -1;
let g_arm_skip_done      = false;
let g_esc_post           = null;   // NativeFunction(user32!PostMessageA), lazy

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

// FPS overlay suppression. Captures hide the "Fps NN" overlay by default
// (its value is wall-clock derived → noisy diff); config.show_fps re-enables
// it. Implemented by Interceptor.replace'ing FUN_004523e6 with a no-op.
let g_hide_fps         = true;
let g_hide_fps_cb      = null;   // NativeCallback retainer (GC would eat it)
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

// rng_seed: when non-null, pin DAT_006023a0 to this value right after the
// engine's one WinMain wall-clock reseed (FUN_005041ec), so retail and the
// port (which uses --rng-seed) share the same LCG sequence in comparisons.
let g_rng_seed = null;

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
let g_d3d_trace_verts   = false;    // capture per-draw vertex/index bytes

// Stable texture identity: map a live IDirect3DTexture8* (toString hex) to the
// LOAD-STABLE source name it was loaded from, so SetTexture can emit a
// "tex_name" that compares across port↔retail (mirrors the port's
// src/d3d_tex_names.c registry).  Populated by hooks on the engine's texture
// loaders (FUN_0047193c UI / FUN_00471b24 mesh), which write the created
// texture pointer to the first dword of their output slot.
let g_tex_names       = {};         // "0x.." → "bmp/foo.tga"
let g_tex_name_hooked = false;

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
let g_call_trace_fields  = {};      // {va:int -> [fieldspec]} declared payloads
let g_ct_seq             = 0;       // per-frame execution-order counter
let g_ct_seq_frame       = -1;      // frame g_ct_seq was last reset for

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
let g_record_esc           = false;  // recorder: hook FUN_0045337b → {esc_record}
let g_anchor_initialized   = false;
let g_anchor_prev_scene    = 0;      // previous-frame DAT_0438b1c0
let g_anchor_prev_loading  = false;  // previous-frame (gate1||gate2)!=0
let g_anchor_prev_reveal   = 0;      // previous-frame DAT_073a3e00 (reveal ctr)
let g_anchor_prev_revflag  = 0;      // previous-frame DAT_073a3e04 (revealed)
let g_anchor_prev_fxalpha  = 0;      // previous-frame max extra-sprite alpha
let g_anchor_prev_linep    = 0;      // previous-frame DAT_073a6a38 >= 0 (line shown)
let g_anchor_prev_convstate = 0;     // previous-frame DAT_056daafc (player state)
let g_anchor_prev_convblink = false; // previous-frame eyes-closed (blink) flag
let g_anchor_prev_cc08      = 0;     // previous-frame DAT_0438cc08 (interaction state)
let g_anchor_prev_pause     = false; // previous-frame DAT_0438b150 != 0 (pause open)
let g_anchor_prev_savepicker = false; // previous-frame Save submenu navigable (type 3)
let g_anchor_prev_encyclopedia = false; // previous-frame Encyclopedia submenu navigable (type 6)
let g_anchor_prev_options = false; // previous-frame Options submenu navigable (type 2)
let g_anchor_prev_items = false;   // previous-frame Items submenu navigable (type 1)
let g_anchor_prev_titlepicker = false; // previous-frame title Continue/load picker navigable
let g_anchor_prev_titlesettings = false; // previous-frame title Options/settings submenu navigable
let g_anchor_prev_titleencyclopedia = false; // previous-frame title all-banks 図鑑 navigable
let g_anchor_prev_titlerecords = false; // previous-frame title Records / high-score screen navigable
let g_anchor_prev_titlesurvival = false; // previous-frame title Survival difficulty selector at rest

// {phasepin} background-window-NPC normalizer (mirrors the port's
// scene1_bg_npc_phasepin).  When a phasepin re-arms the bg-NPC warmup, this is
// set so the NEXT FUN_0046f621 (warmup pump) entry forces the shared LCG to the
// canonical seed + opens the spawn gate — making the 6 drifting townsfolk a
// load-phase-independent, port↔retail-reproducible layout.  Keep BG_NPC_PIN_SEED
// in lock-step with SCENE1_BG_NPC_PHASEPIN_SEED in src/scene1_bg_npc.h.
let g_bg_npc_pin_pending   = false;
const BG_NPC_PIN_SEED      = 19937;

// One-shot latch for the {bgnpcpin} capture: dump retail's NATURAL bg-NPC SoA
// (DAT_073a7f80) the first frame the f406 first-customer entry is observed
// (cc08==4 && b51c==0).  Condition-gated, NOT segtrace-gated, so the cross-target
// wrap-up anchor desync (which stalls the segment chain — DLG_LINE_CLEAR never
// fires on retail) cannot suppress it.  See segtraceTick + RE §21.1.
let g_bgnpc_soa_dumped     = false;
let g_bgnpc_rng_log_n      = 0;   // bgnpcpin verification rng-trajectory counter
let g_grid_dumped          = false; // one-shot furniture-grid dump latch (rng-survey §21.4 ROOT 2)
// BILATERAL {bgnpcpin} (RE §21.4): the canonical bg-NPC SoA (150 u32 = 6 x 0x64-byte
// records) to WRITE into DAT_073a7f80 at the f406 entry, so retail's window NPCs match
// the port's pinned layout.  Retail's natural bg_npc varies run-to-run (the NPCs tick a
// variable # of frames during the load whose duration is a CreateThread race), so a
// PORT-ONLY pin to ONE stale capture can't align a fresh retail drive — pin BOTH sides to
// the same canonical (the {rngseed} pattern).  null = capture mode (DUMP the natural SoA).
let g_bgnpc_pin_soa        = null;

// Extra/effect-sprite standee table (the sigh / zzz / kuro fade etc). Base =
// &DAT_073a3e70, stride 0x70; field11 (active) at +0x2c, field18 (alpha float)
// at +0x48. Scan index 2..31 (chr 0/1 are the persistent speakers) — the same
// range as the port's scene1_intro_dialogue_fx_alpha so both sides agree.
const FX_STANDEE_BASE  = 0x073a3e70;
const FX_STANDEE_STRIDE = 0x70;
function anchorFxAlpha(dlgActive) {
    if (!dlgActive) return 0;
    let maxa = 0;
    for (let i = 2; i <= 31; i++) {
        const sb = FX_STANDEE_BASE + i * FX_STANDEE_STRIDE;
        if (rva(sb + 0x2c).readS32() === 0) continue;   // field11 active
        let a = rva(sb + 0x48).readFloat() | 0;          // field18 alpha
        if (a < 0) a = 0; else if (a > 255) a = 255;
        if (a > maxa) maxa = a;
    }
    return maxa;
}

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

// Trace Studio v3 — anchor-relative capture-proxy arm. When config.v3_arm =
// {anchor, offset, count} is set, the FIRST time `anchor` fires the agent calls
// the staged proxy d3d8.dll's OrV3ArmWindowAt(anchor_frame+offset, count) export
// IN-PROCESS (zero IPC latency), so the proxy keeps the post-load present-window
// despite the nondeterministic turbo load-stretch (a cfg-fixed present-count only
// reaches the deterministic-early title). No-op unless the v3 proxy is staged AND
// v3_arm is set ⇒ a normal v2 capture is never affected. See docs/plans/trace-studio-v3.md.
let g_v3_arm        = null;   // {anchor:str, offset:int, count:int, occ:int} | null
let g_v3_arm_fn     = null;   // NativeFunction(OrV3ArmWindowAt) (lazy-resolved once)
let g_v3_arm_fired  = false;  // arm exactly once
let g_v3_arm_count  = 0;      // firings of the arm anchor so far (arm on the occ-th)
// Window-aware early-exit (studio-v3 P2). >0 ⇒ shut the retail drive down at this
// frame instead of grinding on to max_frames. Set when the v3 window is armed: after
// the window's last present the drive has NOTHING left to do but over-run the load
// budget (sized for the ~13k-frame load-stretch) to max_frames — E4's ~100s of pure
// waste, re-paid every capture. The proxy present-counter and this agent frame-counter
// are the same Present clock (the bit-exact window landing proves it), so window-end +
// a small margin guarantees the proxy has kept the last frame + finalized the container.
let g_v3_shutdown_frame = 0;

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
let g_segtrace_base_anchor = null; // anchor name that entered the current segment
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
    const seg0 = () => ({entries: [], captures: [], capranges: [], calltraces: [],
                         setrngs: [], escs: [], phasepins: [], pokes: [],
                         memsnaps: [], rngcs: [], gsimpins: [], playtimepins: [],
                         tutloadpin: null, wait: null, wait_until: null});
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
        } else if (op && op.caprange !== undefined && Array.isArray(op.caprange)) {
            // {caprange:[start,count]} — contiguous capture window [base+start,
            // base+start+count). Mirrors the port's caprange so a trace.work.jsonl
            // exported for the port renders the SAME anchor-relative window on
            // retail (frame-for-frame port↔retail dust/depth comparison).
            segs[segs.length - 1].capranges.push(
                [op.caprange[0] | 0, op.caprange[1] | 0]);
        } else if (op && op.capstride !== undefined) {
            // {capstride:N} (D3) — trace-global two-tier capture cadence: thin
            // each {caprange} to every Nth frame from its start. Not segment-
            // scoped (last declaration wins); mirrors the port's g_capture_stride
            // so both targets keep the identical anchor-relative kept-set.
            g_segtrace_capstride = (op.capstride | 0) > 1 ? (op.capstride | 0) : 1;
        } else if (op && op.tutloadpin !== undefined) {
            // {tutloadpin:N} — SEGMENT-SCOPED: extend every tutorial-dialogue
            // load bracket (the DAT_0438b1c8==2 window over the FUN_00452d07
            // worker) to N frames by BLOCKING the LAB_00452aab worker at its
            // tail until N frames past the arm (see tutloadpinTick /
            // installTutLoadPinWorkerHook). EXTEND-only: a real load LONGER
            // than N is left alone (a thread can't be shortened), so the
            // trace should pick N ≥ any plausible real bracket. The blocked
            // frames run the engine's own loading path (overlay, db054++,
            // wing emits) exactly like a slow disk. Mirrors the port's
            // IVE_TUT_LOAD_FRAMES override so both sides idle EQUAL-length
            // brackets — equal db054/wing-emit consumption inside the bracket
            // and an aligned post-bracket label axis (engine-quirks §119).
            // Attached to the CURRENT segment; segtraceOnSegmentEnter applies
            // it (sticky) at that segment's entry, so the head pin stays small
            // for the confirmed region while a late segment binds the long
            // dialogue-cutscene loads (mirror of the port's rearm_tutloadpins).
            segs[segs.length - 1].tutloadpin =
                (op.tutloadpin | 0) > 0 ? (op.tutloadpin | 0) : 0;
        } else if (op && op.csloadpin !== undefined) {
            // {csloadpin:N} — trace-global: extend every cc08==4 d3e cs-load
            // bracket (the DAT_0438b1cc==2 window over LAB_00452ae8/b13) to N
            // frames by blocking those worker tails (see csloadpinTick / RE §20).
            // EXTEND-only, like {tutloadpin}; mirrors the port's csload bracket
            // so the 目玉 sparkle consumes an equal rng count on both sides.
            g_segtrace_csloadpin = (op.csloadpin | 0) > 0 ? (op.csloadpin | 0) : 0;
        } else if (op && op.primaryloadpin !== undefined) {
            // {primaryloadpin:N} — trace-global: drain the cad868 PRIMARY worker
            // (DAT_06a49954) to N frames so the rng state at scene-init is
            // deterministic on both targets (bg_npc / sparkle / chibi / window
            // NPCs follow).  Mirrors the port's worker_load_set_primary_pin.
            g_segtrace_primaryloadpin = (op.primaryloadpin | 0) > 0 ? (op.primaryloadpin | 0) : 0;
        } else if (op && op.bgnpcseed !== undefined) {
            // {bgnpcseed:V} == [V,0], {bgnpcseed:[V,C]}, or
            // {bgnpcseed:[V,C,[d0..]]} — trace-global: seed DAT_006023a0 to V,
            // the spawn cursor (DAT_073a8bb4) to C, and the C dead slots'
            // leftover engine records (DAT_073a7f80) to the optional 3rd
            // array, right before the bg-NPC warmup's NATURAL first-ever tick
            // (RE §21.21/§21.22).  See installBgNpcPinHook /
            // g_segtrace_bgnpcseed_active.
            const bns = op.bgnpcseed;
            g_segtrace_bgnpcseed = (Array.isArray(bns) ? bns[0] : bns) >>> 0;
            g_segtrace_bgnpcseed_cursor = Array.isArray(bns) ? (bns[1] | 0) : 0;
            g_segtrace_bgnpcseed_dead = (Array.isArray(bns) && Array.isArray(bns[2]))
                ? bns[2].map(d => d >>> 0) : [];
            g_segtrace_bgnpcseed_active = true;
        } else if (op && op.calltrace !== undefined) {
            // Scalar N -> [0, N]; [start, len] -> base-relative window.
            const ct = op.calltrace;
            segs[segs.length - 1].calltraces.push(
                Array.isArray(ct) ? [ct[0] | 0, ct[1] | 0] : [0, ct | 0]);
        } else if (op && op.rngcs !== undefined) {
            // {rngcs:N} or {rngcs:[start,len]} — arm the rng-callsites who-
            // consumed-the-LCG drill over [base+start, base+start+len) WITHOUT
            // the phasepin's bg-NPC re-seed / gsim-zero (a CLEAN measurement;
            // the phasepin contaminates the very rates being measured). Still
            // needs --rng-callsites N on the CLI to install the LCG hooks.
            const rc = op.rngcs;
            segs[segs.length - 1].rngcs.push(
                Array.isArray(rc) ? [rc[0] | 0, rc[1] | 0] : [0, rc | 0]);
        } else if (op && op.rngseed !== undefined && Array.isArray(op.rngseed)) {
            // {rngseed:[frame,value]} — force DAT_006023a0 to `value` at the
            // base-relative `frame`, mirroring the port's rng_seed() so both
            // targets share one LCG stream from the anchor (cross-target RNG
            // parity for the recorded segment).
            segs[segs.length - 1].setrngs.push(
                {frame: op.rngseed[0] | 0, value: op.rngseed[1] >>> 0,
                 fired: false});
        } else if (op && op.gsimpin !== undefined && Array.isArray(op.gsimpin)) {
            // {gsimpin:[frame,value]} — force g_sim_frame_count (DAT_0438b8cc)
            // to `value` at the base-relative `frame`, mirroring the port's
            // segtrace_gsimpin_cb.  Pins the 目玉 display-sparkle %8 phase (the
            // g_sim origin differs port↔retail — the port skips the intro)
            // WITHOUT the {phasepin} bg-NPC re-seed that stalls the wrap-up
            // cutscene.  `value` is retail's recorded counter, so this is a
            // no-op for retail (preserves its natural phase).  RE §21.
            segs[segs.length - 1].gsimpins.push(
                {frame: op.gsimpin[0] | 0, value: op.gsimpin[1] >>> 0,
                 fired: false});
        } else if (op && op.playtimepin !== undefined && Array.isArray(op.playtimepin)) {
            // {playtimepin:[frame,value]} — force the ACTIVE working slot's total
            // playtime frame count (working DAT_044e37a0[slot]) to `value` at the
            // base-relative `frame`, mirroring the port's segtrace_playtimepin_cb.
            // Normalizes the async-load-bracket phase origin (the house + pause
            // loads are a wall-clock CreateThread race counted into playtime) so a
            // save COMMIT compares byte-exact.  Unlike {gsimpin} (value = retail's
            // natural, a no-op), `value` is a CHOSEN canonical origin forced on
            // BOTH sides; the fire point is identical (input_poll, pre-sim) so both
            // land on value+K after K deterministic ticks to the commit snapshot.
            segs[segs.length - 1].playtimepins.push(
                {frame: op.playtimepin[0] | 0, value: op.playtimepin[1] >>> 0,
                 fired: false});
        } else if (op && op.poke !== undefined && Array.isArray(op.poke)) {
            // {poke:[frame, va, val]} — STICKY: from the base-relative `frame`
            // on, write u32 `val` into Ghidra-VA `va` every frame. Holds a flag
            // set (e.g. the debug-overlay gate DAT_06a49938=1) against any engine
            // reset. Distinct from {rngseed}/{phasepin}, which fire exactly once.
            segs[segs.length - 1].pokes.push(
                {frame: op.poke[0] | 0, va: op.poke[1] | 0, val: op.poke[2] >>> 0});
        } else if (op && op.esc !== undefined) {
            // {esc:N} — synthesise an ESC keypress at the base-relative frame N,
            // mirroring the port's {esc} op so a recorded dialogue-skip replays
            // on retail too (arms the skip-event prompt / quits at title).
            segs[segs.length - 1].escs.push({frame: op.esc | 0, fired: false});
        } else if (op && op.phasepin !== undefined) {
            // {phasepin:N} — at the base-relative frame N, normalize the
            // companion's load-dependent free-roam phase: zero the db054
            // bob/sparkle counter (DAT_056db054) and the companion sprite anim
            // cycle (FRAME/TIMER/COUNTER = DAT_056dab50/48/4c).  Mirrors the
            // port's {phasepin} so a port<->retail trace comparison is phase-clean
            // (engine-quirks 94, scene1-tear-visual-diffs.md).
            segs[segs.length - 1].phasepins.push({frame: op.phasepin | 0, fired: false});
        } else if (op && op.memsnap !== undefined) {
            // {memsnap:N} — at the base-relative frame N, dump the configured
            // writable-section regions (config.memsnap_regions, from the exe's
            // section table) straight to capture_dir via Win32 writes — the
            // phase-state census input (tools/phase_census.py). Mirrors the
            // port's {memsnap}; fires once, pre-sim, AFTER same-frame pins.
            segs[segs.length - 1].memsnaps.push({frame: op.memsnap | 0, fired: false});
        } else if (op && op.savefile !== undefined) {
            // {savefile:"<relpath>"} — trace-global embedded-save ref. The save
            // override is harness-driven (tools/trace_save.py decompresses the blob;
            // retail redirect is a TODO), so the agent just skips it. Crucially this
            // prevents the op falling into the input-entry `else` below, where its
            // missing frame/mask would inject a phantom frame-0 release.
            /* no-op */
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
    for (let i = 0; i < seg.capranges.length; i++) {
        const start = seg.capranges[i][0], count = seg.capranges[i][1];
        const lo = g_segtrace_base + start;
        // {capstride:N} thins the window to every Nth frame from its start (D3
        // OVERVIEW); stride 1 = dense (the default). Matches the port's
        // capture_in_range test (f - lo) % stride == 0 → identical kept-set.
        const stride = g_segtrace_capstride > 1 ? g_segtrace_capstride : 1;
        let kept = 0;
        for (let k = 0; k < count; k += stride) { g_capture_pending.add(lo + k); kept++; }
        log('segtrace: caprange scheduled base+' + start + '..base+' +
            (start + count) + ' -> frames ' + lo + '..' + (lo + count) +
            ' (' + count + ' frames, stride ' + stride + ' -> ' + kept + ' kept)');
        // Arm anchor-relative d3d-trace on the window edges if enabled (a full
        // contiguous d3d-trace would be huge; the ±2 round each capture in the
        // scalar-capture path covers point reads — for a caprange the consumer
        // can re-run with explicit --d3d-trace-frames if needed).
    }
    for (let i = 0; i < seg.calltraces.length; i++) {
        const start = seg.calltraces[i][0], len = seg.calltraces[i][1];
        const lo = g_segtrace_base + start, hi = lo + len;
        g_ct_windows.push([lo, hi]);
        log('segtrace: call-trace armed for frames ' + lo + '..' + hi +
            ' (base+' + start + '..base+' + (start + len) + ')');
    }
    for (let i = 0; i < (seg.rngcs || []).length; i++) {
        const start = seg.rngcs[i][0], len = seg.rngcs[i][1];
        g_rng_cs_lo = g_segtrace_base + start;
        g_rng_cs_hi = g_rng_cs_lo + len;
        g_rng_cs_buf = {}; g_rng_cs_flushed = false;
        log('segtrace: rng-callsites armed (CLEAN, no phasepin) for frames '
            + g_rng_cs_lo + '..' + g_rng_cs_hi + ' (base+' + start + ')');
    }
    // {tutloadpin:N} is SEGMENT-SCOPED (sticky): applied at the entry of each
    // declaring segment, mirroring the port's rearm_tutloadpins. Segments that
    // don't declare one (tutloadpin===null) leave the current pin unchanged, so
    // the head pin holds until a late segment re-declares it. The worker-tail
    // blocker is installed once at attach (gated on the initial value from
    // segment 0), so a later re-declaration only changes the release target N.
    if (seg.tutloadpin !== null && seg.tutloadpin !== undefined) {
        g_segtrace_tutloadpin = seg.tutloadpin | 0;
        log('segtrace: tutloadpin -> ' + g_segtrace_tutloadpin +
            ' (segment entry)');
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

// RNG caller histogram. When `rng_callers` is set, the agent hooks the
// engine LCG FUN_005041f6 (the single global generator on DAT_006023a0,
// the source of both rng_next15 and rng_next_unit) at ENTER and tallies
// the immediate caller VA. A periodic flush sends the cumulative
// {ret_va: count} map. Used to find which subsystems advance the shared
// RNG stream per frame — the metric for foot-dust / particle RNG parity
// against the port (see scene1-walk-dust.md).
const FN_RNG_LCG              = 0x005041f6;  // FUN_005041f6 (LCG step)
let g_rng_callers            = false;
let g_rng_callers_hooked     = false;
let g_rng_callers_map        = {};
// Condition-gated rng-hook DEFERRAL (RE §21.2). The LCG hook (installRngCallerHook)
// is normally installed pre-resume, but it taxes EVERY draw with a Frida trampoline.
// The initial cad868 Continue-load deserialize fires a HUGE rng burst, so a
// boot-installed hook inflates that ONE load ~3500f → ~14161f — which mis-times the
// esc-skip and breaks the f406 first-customer trace's determinism (retail runs the
// SCRIPTED tutorial, never reaching the entry). When g_rng_hook_defer is set, the
// install is DEFERRED to the f406 entry (cc08==4 && b51c==0 — the same gate the bgnpc
// SoA dump uses): pre-entry stays hook-free (load fast, skip times right), then the
// hook arms in segtraceTick and counts rng cumulative-FROM-the-entry. g_rng_hook_wanted
// records (at config time) whether any rng source was requested at all, so the deferred
// arm is a no-op when no rng was asked for.
let g_rng_hook_defer         = false;  // config.rng_hook_defer
let g_rng_hook_wanted        = false;  // any rng source requested (set at config time)

// Wrap-up skip DRIVER (RE §21.5).  The post-tutorial iv1_7 wrap-up CONV_POSE is
// skipped via ESC ("Skip this event?" box, FUN_0046c2cb) + a CB_BTN_A "Yes".
// The recording's confirm is a single blink-relative timed X-press — FRAGILE on
// retail: the arm gate is `1 < DAT_073a3e18(skip_prompt) && DAT_073a3dec==0`, and
// skip_prompt resets to 0 on each dialogue re-init (all.c:67083), so under retail's
// load jitter the ESC@+25 intermittently lands when skip_prompt<=1, the box never
// arms, the dialogue runs FREE (reaches TEXT_ANIM_END) instead of skipping, and the
// segtrace's skip-structure waits (DLG_LINE_CLEAR / CONV_POSE_END) never fire → the
// 1176-blink cross-target wrap-up deadlock (RE §21.4 ROOT 3, the determinism BLOCKER).
// The PORT (turbo fixed-clock, jitter-free) lands the timed confirm reliably, so this
// is retail-only.  Fix: drive the skip off retail's ACTUAL box state — re-post ESC
// every frame the box is closed (arms the instant skip_prompt clears 1, robust to the
// resets) and pulse CB_BTN_A once it opens.  Gamble-free (no dependence on retail's
// exact arm timing).  Gated to the wrap-up + auto-disabled at the f406 entry.
let g_wrapup_skip_active     = false;  // config.skip_wrapup (EXPLICIT-only, WIP — RE §21.5:
                                       // fixes the blink-stall but a downstream softlock
                                       // surfaced, so NOT auto-on for the canonical drive yet)
let g_wrapup_seen_tutorial   = false;  // latched once cc08==4 (the tutorial) is observed,
                                       // so the driver fires ONLY for the POST-tutorial
                                       // wrap-up, never any pre-tutorial dialogue
let g_wrapup_dbg_prev        = -1;     // RE §21.6 diag: last logged wrapup driver state
let g_wrapup_box_was_open    = false;  // RE §21.5: latched once the skip box (DAT_073a3dec)
                                       // is seen OPEN — the ARM succeeded, the recording's X
                                       // will confirm it.  Once latched the driver NEVER posts
                                       // ESC again, so the post-skip teardown→free-roam→f406
                                       // entry window is undisturbed (the old `!g_bgnpc_soa_
                                       // dumped` upper bound kept posting ESC into free-roam,
                                       // where each ESC opened the pause box (b150) and blocked
                                       // the entry → the LOADING/PAUSE softlock LOOP).
// RNG-CONSUMPTION probe (tools/phase_probe.py). g_rng_count: when set, the LCG
// hook keeps a cumulative call total emitted as vals.rngcalls in the per-frame
// watch — lets the probe diff per-frame RNG *consumption* port↔retail to find
// where the two streams desync under a shared seed. g_rng_cs_[lo,hi): a
// base-relative-resolved frame range over which the hook also records the
// CALLER VA of every LCG step (incl. periodic every-X-frame consumers), buffered
// per absolute frame and flushed as {kind:'rng_callsites'} — the who-consumed-it
// drill-down for the desync frame.
let g_rng_count             = false;
let g_rng_count_total       = 0;
let g_rng_cs_lo             = -1;   // absolute frame range [lo,hi) for call-site capture
let g_rng_cs_hi             = -1;
let g_rng_cs_buf            = {};   // {frame: {"0xVA": count}}
let g_rng_cs_flushed        = false;
let g_rng_cs_len            = 0;    // frames-after-phasepin to capture call sites
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

// Surface-level vtable fns + scratch buffers + the readback staging blob, all
// cached/reused ACROSS captured frames. These USED to be re-created every
// frame — `new NativeFunction` ×5-6 (each allocates an executable trampoline) +
// `Memory.alloc(~3 MB)` (blob) + 4 small `Memory.alloc`s — which churned
// frida-agent's OWN heap and AV'd it (0xc0000005 inside frida-agent.dll, fault
// offset ~0xbe5f4e) after ~70-130 captured frames; slower capture rates merely
// survived longer (GC kept up). All IDirect3DSurface8s share one vtable, so the
// slot fns resolve ONCE from the first backbuffer surface. See
// docs/findings/town-map-RE.md §5b / engine-quirks. (capture-local also writes
// the reused blob straight to disk via Win32 — see writeRawFile — so the
// 3 MB/frame `readByteArray` is gone too on that path; only the remote
// pixel-shipping path still allocates it, where transfer is the bottleneck.)
let g_surf_fns = null;     // {get_desc, lock_rect, unlock_rect, release}
let g_cap_scratch = null;  // {ppBB, descBuf, ppSys, lrBuf} — reused per frame
let g_cap_blob = null;     // reused readback staging buffer (grows on demand)
let g_cap_blob_size = 0;

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

// Resolve the surface-method NativeFunctions once (all surfaces share a vtable).
function ensureSurfFns(surf) {
    if (g_surf_fns) return;
    g_surf_fns = {
        get_desc: new NativeFunction(
            vtableSlot(surf, V_Surf_GetDesc), 'uint32',
            ['pointer', 'pointer'], 'stdcall'),
        lock_rect: new NativeFunction(
            vtableSlot(surf, V_Surf_LockRect), 'uint32',
            ['pointer', 'pointer', 'pointer', 'uint32'], 'stdcall'),
        unlock_rect: new NativeFunction(
            vtableSlot(surf, V_Surf_UnlockRect), 'uint32',
            ['pointer'], 'stdcall'),
        release: new NativeFunction(
            vtableSlot(surf, V_Release), 'uint32', ['pointer'], 'stdcall'),
    };
}

// One-time tiny scratch buffers (out-pointers + desc/locked-rect structs).
function ensureCapScratch() {
    if (g_cap_scratch) return;
    g_cap_scratch = {
        ppBB:    Memory.alloc(Process.pointerSize),
        descBuf: Memory.alloc(D3DSURFACE_DESC_SIZE),
        ppSys:   Memory.alloc(Process.pointerSize),
        lrBuf:   Memory.alloc(D3DLOCKED_RECT_SIZE),
    };
}

function releaseSurface(surf) {
    g_surf_fns.release(surf);   // cached; g_surf_fns set before any release path
}

// Win32 raw-file writer (kernel32 — always loaded, no CRT dependency), used by
// the capture-local fast path to dump the reused BGRX blob STRAIGHT to disk.
// This replaces `blob.readByteArray()` + `new File()` per frame, the last big
// per-captured-frame native-backed allocation (a fresh ~3 MB ArrayBuffer) that
// churned frida's heap into the 0xbe5f4e AV. All buffers are reused; only the
// three cached NativeFunctions run per frame.
let g_winfile_fns = null;   // {createFile, writeFile, closeHandle}
let g_fname_buf   = null;   // reused ANSI path buffer
let g_written_buf = null;   // DWORD out for WriteFile

function ensureWinFileFns() {
    if (g_winfile_fns) return;
    // Frida 17.x removed the legacy global Module.getExportByName(name, export);
    // resolve the kernel32 module instance once, then look up the exports off it
    // (same migration as installShowWindowHook / installSaveRedirectHook above).
    // This was MISSED in the 17.x migration, so the whole capture-local
    // writeRawFile fast-path silently threw "not a function" and captured zero
    // frames — forcing every capture onto the 3 MB/frame readByteArray+send
    // path that backpressures the remote channel and AVs frida-agent's memcpy.
    const k32 = Process.findModuleByName('kernel32.dll');
    if (!k32) { err('ensureWinFileFns', 'kernel32.dll module not loaded'); return; }
    const expCreateFileA = k32.findExportByName('CreateFileA');
    const expWriteFile   = k32.findExportByName('WriteFile');
    const expCloseHandle = k32.findExportByName('CloseHandle');
    if (!expCreateFileA || !expWriteFile || !expCloseHandle) {
        err('ensureWinFileFns', 'kernel32 file exports not found'); return;
    }
    g_winfile_fns = {
        createFile: new NativeFunction(
            expCreateFileA, 'pointer',
            ['pointer', 'uint32', 'uint32', 'pointer', 'uint32', 'uint32', 'pointer'],
            'stdcall'),
        writeFile: new NativeFunction(
            expWriteFile, 'int',
            ['pointer', 'pointer', 'uint32', 'pointer', 'pointer'], 'stdcall'),
        closeHandle: new NativeFunction(
            expCloseHandle, 'int',
            ['pointer'], 'stdcall'),
    };
    g_fname_buf   = Memory.alloc(2048);
    g_written_buf = Memory.alloc(4);
}

// CreateFileA(CREATE_ALWAYS) → WriteFile(len) → CloseHandle. Path is ANSI; the
// blob is raw native memory (the reused staging buffer). Returns true on write.
function writeRawFile(path, dataPtr, len) {
    g_fname_buf.writeAnsiString(path);     // reuse (paths are < 2 KB)
    // GENERIC_WRITE=0x40000000, CREATE_ALWAYS=2, FILE_ATTRIBUTE_NORMAL=0x80
    const h = g_winfile_fns.createFile(
        g_fname_buf, 0x40000000, 0, NULL, 2, 0x80, NULL);
    if (h.isNull() || h.toInt32() === -1) {   // INVALID_HANDLE_VALUE
        err('writeRawFile/CreateFileA', path);
        return false;
    }
    try {
        g_winfile_fns.writeFile(h, dataPtr, len, g_written_buf, NULL);
        return true;
    } finally {
        g_winfile_fns.closeHandle(h);
    }
}

function captureBackbuffer(devicePtr, frameNumber, captureVals) {
    ensureSurfaceFns(devicePtr);
    ensureCapScratch();

    // ── 1. GetBackBuffer → bbSurf (video memory, not lockable) ──
    const ppBB = g_cap_scratch.ppBB;
    ppBB.writePointer(NULL);
    let hr = g_dev_fns.get_backbuffer(devicePtr, 0, D3DBACKBUFFER_TYPE_MONO, ppBB);
    if (hr !== 0) {
        err('captureBackbuffer/GetBackBuffer', 'HRESULT 0x' + (hr >>> 0).toString(16));
        return;
    }
    const bbSurf = ppBB.readPointer();
    if (bbSurf.isNull()) { err('captureBackbuffer', 'GetBackBuffer returned NULL surface'); return; }
    ensureSurfFns(bbSurf);   // resolve surface vtable fns once (before any release path)

    let sysSurf = NULL;
    try {
        // ── 2. GetDesc(bbSurf) to learn w, h, format ──
        const descBuf = g_cap_scratch.descBuf;
        hr = g_surf_fns.get_desc(bbSurf, descBuf);
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
        const ppSys = g_cap_scratch.ppSys;
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
        const lrBuf = g_cap_scratch.lrBuf;
        hr = g_surf_fns.lock_rect(sysSurf, lrBuf, NULL, D3DLOCK_READONLY);
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
            if (g_cap_blob === null || g_cap_blob_size < total) {
                g_cap_blob = Memory.alloc(total);   // reused across frames; grows on demand
                g_cap_blob_size = total;
            }
            const blob = g_cap_blob;
            for (let y = 0; y < h; y++) {
                Memory.copy(blob.add(y * rowBytes),
                            pBits.add(y * pitch),
                            rowBytes);
            }
            // Same-machine fast path: when a capture dir is set, write the raw
            // BGRX blob straight to a WSL-accessible file (the agent runs on the
            // same host as WSL) instead of shipping ~3 MB/frame over the Frida
            // channel. Python reads the .raw files afterwards. Filename carries
            // w×h so the reader needs no sidecar. Enables whole-trace capture.
            // Win32 direct-write from the reused blob — NO per-frame ArrayBuffer
            // (readByteArray) or `new File`, the last churn that AV'd frida.
            if (g_capture_dir) {
                ensureWinFileFns();
                const name = 'frame_' + ('00000' + frameNumber).slice(-5) +
                             '_' + w + 'x' + h + '.raw';
                writeRawFile(g_capture_dir + '\\' + name, blob, total);
                // Lightweight notify (no pixel payload) so Python records the
                // frame + its capture-time watch vals without the transfer.
                send({kind: 'frame_file', frame: frameNumber, w: w, h: h,
                      file: name, t_ms: nowMs(), vals: captureVals || null});
                return;
            }

            // Remote / non-local path: ship pixels over the Frida channel (this
            // readByteArray is the ~3 MB/frame churn, but transfer is the
            // bottleneck there anyway; capture-local above avoids it entirely).
            const ab = blob.readByteArray(total);
            send({
                kind:  'frame',
                frame: frameNumber,
                w:     w,
                h:     h,
                pitch: rowBytes,
                fmt:   fmt,
                t_ms:  nowMs(),
                // Capture-time watch read (Present onEnter, post-render): the
                // screenshot's OWN sim-state label, sampled at the exact instant
                // the backbuffer is grabbed. Robust to the turbo tick/present
                // decoupling that desyncs the separately-emitted per-tick
                // {watch} record from the rendered frame. Consumers should align
                // screenshots by these vals, not the {watch} stream's db054.
                vals:  captureVals || null,
            }, ab);
        } finally {
            g_surf_fns.unlock_rect(sysSurf);
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
            // {tutloadpin} release — FIRST, before the suppress decision AND
            // anchorTick: the release frame must read gates==0 for BOTH (the
            // port captures its bracket-END frame, so suppressing it here
            // would drop one frame per bracket retail-only — seen as a 2-frame
            // kept-count skew on the first pinned recapture).
            if (g_segtrace_tutloadpin > 0) {
                try {
                    tutloadpinPresentRelease(fn);
                } catch (e) {
                    err('Present.onEnter.tutloadpin', e.message);
                }
            }
            if (g_segtrace_csloadpin > 0) {
                try {
                    csloadpinPresentRelease(fn);
                } catch (e) {
                    err('Present.onEnter.csloadpin', e.message);
                }
            }
            if (g_segtrace_primaryloadpin > 0) {
                try {
                    primaryloadpinPresentRelease(fn);
                } catch (e) {
                    err('Present.onEnter.primaryloadpin', e.message);
                }
            }
            // D1 load-suppression (Trace Studio v2, plan Phase 1) — mirror of
            // the port's g_frame_loading_active gate (src/main.c). Opt-in via
            // config.suppress_loads (default off), so existing scenarios are
            // untouched. Read the two nowloading worker-load gates HERE, fresh
            // for this frame (anchorTick reads the same two below but runs AFTER
            // this decision, so its value would be one frame stale). While a load
            // is in flight we skip the ~3 MB readback so a from-boot OVERVIEW
            // collapses the load-stretch to a zero-width seam, keeping the same
            // kept-count/order as the port. Suppression is UNIFORM (pending +
            // stride) to match the port's blanket gate so the two sides keep the
            // identical frame set. (Continue/Load only needs the worker gates;
            // the New-Game dialogue inter-script bracket — port
            // scene1_intro_dialogue_loading — is a Phase-5 concern off this path.)
            let suppress = false;
            if (g_suppress_loads) {
                try {
                    suppress =
                        (rva(ADDR.var_nowloading_gate).readS32() !== 0) ||
                        (rva(ADDR.var_nowloading_gate2).readS32() !== 0);
                } catch (e) { /* gate unreadable → treat as not-loading */ }
            }
            // capture_all honours an optional stride (every Nth frame) so a
            // whole-trace replay can be sampled at a transfer-feasible rate over
            // remote Frida (each frame ships ~3 MB of RGBA). stride<=1 = every
            // frame. Explicit g_capture_pending frames always capture.
            // Live-probe on-demand shot / stream (probe daemon): capture at
            // the NEXT Present regardless of frame number. Bypasses the
            // load-suppression gate on purpose — a probe shot of a loading
            // screen is a legitimate "what's on screen right now" answer.
            const probeWant = g_probe_shot > 0 ||
                (g_probe_stream_every > 0 &&
                 (fn % g_probe_stream_every) === 0);
            const want = probeWant || (!suppress && (g_capture_pending.has(fn) ||
                (g_capture_all && (g_capture_stride <= 1 ||
                                   (fn % g_capture_stride) === 0))));
            if (want) {
                // Read the watched sim-state HERE (Present onEnter, post-render)
                // so the screenshot carries its own atomic state label. The
                // separate per-tick {watch} record is read at input_poll and,
                // under turbo (sim-tick decoupled from Present), can name a
                // different sim frame than the one actually on screen.
                let captureVals = null;
                if (g_watch.length || g_rng_count) {
                    captureVals = {};
                    for (let i = 0; i < g_watch.length; i++) {
                        try { captureVals[g_watch[i].name] = watchRead(g_watch[i]); }
                        catch (e) { captureVals[g_watch[i].name] = null; }
                    }
                    if (g_rng_count) captureVals.rngcalls = g_rng_count_total;
                }
                try {
                    captureBackbuffer(devicePtr, fn, captureVals);
                } catch (e) {
                    err('Present.onEnter', e.message + ' @ ' + e.stack);
                }
                if (g_probe_shot > 0) g_probe_shot--;
                g_capture_pending.delete(fn);
                // Anchor-relative captures share g_capture_pending; clear
                // the resolved-target bookkeeping too so the shutdown check
                // below can tell when every requested capture has landed.
                g_cap_anchor_pending.delete(fn);
            }
            if (g_max_frames > 0 && fn >= g_max_frames) {
                send({kind: 'max_frames_reached', frame: fn});
            }
            // Window-aware early-exit (studio-v3 P2): the v3 capture window is done +
            // its container finalized, so reuse the max_frames teardown path to stop
            // the retail drive instead of over-running the load budget to max_frames.
            if (g_v3_shutdown_frame > 0 && fn >= g_v3_shutdown_frame) {
                g_v3_shutdown_frame = 0;          // fire once
                log('v3 early-exit: window captured — shutting down @frame ' + fn);
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

// Rebuild the port's `se_%03d_id%04x` SE label (src/audio.c audio_play_se) so
// audio_diff output reads identically on both sides — read the resource id
// from DAT_005d1584[slot] (8-byte stride, id at +0). null for an out-of-range
// slot (the file/voice SE path uses slot=-1 + name=path instead).
function seName(slot) {
    if (slot < 0 || slot >= 110) return null;
    let id = 0;
    try { id = rva(ADDR.var_se_id_table + slot * 8).readU16(); } catch (_) { return null; }
    const s3 = ('00'  + slot).slice(-3);
    const h4 = ('000' + (id >>> 0).toString(16)).slice(-4);
    return 'se_' + s3 + '_id' + h4;
}

function installAudioHooks() {
    Interceptor.attach(rva(ADDR.fn_audio_play_track), {
        onEnter: function (args) {
            // FUN_00499200 is stdcall(int track) — track is the first
            // (and only) stack arg at [esp+4] in stdcall, but Frida's
            // `args` array reflects the ABI. For stdcall on x86, args[0]
            // is the first parameter.
            const track = this.context.esp.add(4).readS32();
            // Match the port (audio_play_track): a swap event fires only on
            // an ACTUAL change to a valid BGM index. FUN_00499200 no-ops when
            // DAT_005d1960 (current track) already equals the request, and the
            // selector passes -2/-1 to STOP (no swap event on the port side),
            // so dedup against the live current-track global here.
            if (track < 0 || track >= 21) return;
            let cur = -1;
            try { cur = rva(ADDR.var_bgm_cur_track).readS32(); } catch (_) {}
            if (track === cur) return;
            send({kind: 'bgm_swap', t_ms: nowMs(), track: track, frame: frameNo()});
        },
    });

    Interceptor.attach(rva(ADDR.fn_audio_play_se), {
        onEnter: function (args) {
            const slot = this.context.esp.add(4).readS32();
            // ret_va = the immediate caller (module-relative), so audio_diff /
            // a quick grep can name WHICH function fires a given SE — e.g. the
            // worldmap-confirm 0x150. Free (this.returnAddress already on stack).
            send({kind: 'se_play', t_ms: nowMs(), slot: slot, frame: frameNo(),
                  name: seName(slot), ret_va: traceRetVa(this.returnAddress)});
        },
    });

    // Filename/voice SE — FUN_0049933c(char *path). The opening/tutorial
    // dialogues drive voice lines + one-off SEs by path through this (port
    // audio_play_se_file); without the hook a whole class of SE is invisible
    // on the retail side. Mirror the port: slot=-1, name=path (ANSI string).
    Interceptor.attach(rva(ADDR.fn_audio_play_se_file), {
        onEnter: function (args) {
            const p = this.context.esp.add(4).readPointer();
            let path = '';
            try { path = p.isNull() ? '' : p.readCString(); } catch (_) {}
            send({kind: 'se_play', t_ms: nowMs(), slot: -1, frame: frameNo(),
                  name: path, ret_va: traceRetVa(this.returnAddress)});
        },
    });

    log('audio hooks installed');
}

// ─── background-window-NPC {phasepin} hook ───────────────────────────────
//
// FUN_0046f621 (0x46f621) is the per-frame bg-NPC warmup pump.  When a
// {phasepin} has re-armed the warmup (g_bg_npc_pin_pending), force the shared
// LCG (DAT_006023a0) to the canonical seed and open the spawn gate
// (DAT_0438b4e0=0) HERE — at the exact entry where the re-armed 180x warmup is
// about to consume RNG — so the layout is independent of any RNG drawn between
// the pre-sim phasepin and this call.  Mirrors the port's pin_pending re-seed
// inside scene1_bg_npc_tick (src/scene1_bg_npc.c).
function installBgNpcPinHook() {
    Interceptor.attach(rva(0x0046f621), {
        onEnter: function () {
            // Diagnostic (RE §21.21): log retail's NATURAL pre-warmup LCG state
            // on the actual first-ever call (DAT_073a8bb8 latch still 0) — the
            // value a {bgnpcseed} pin needs so the port's warmup consumes the
            // SAME origin.  The generic {rngseed}-at-LOADING_END value is
            // already one frame past this point: the warmup fires on the SAME
            // frame the load-busy gate releases (both port and retail dispatch
            // the scene tick unconditionally once the worker-busy check passes,
            // same call), but a base-relative {rngseed} can only mechanically
            // apply starting the frame AFTER its anchor is detected (anchors
            // are sampled post-sim at Present, so the earliest a pin tied to
            // that anchor can fire is the next frame's pre-sim segtraceTick) —
            // one frame too late for a same-frame consumer.  Cheap, one-shot.
            if (rva(0x073a8bb8).readS32() === 0) {
                log('bg-npc: NATURAL pre-warmup seed = ' +
                    rva(0x006023a0).readU32() + ' @ frame ' + frameNo() +
                    ' cursor(bb4)=' + rva(0x073a8bb4).readS32());
            }
            // {bgnpcseed:[V,C,[d0..]]} — seed the LCG to V, the spawn cursor to
            // C, and DAT_073a7f80's [0,C) dead-slot records to the optional 3rd
            // array, right before the NATURAL first-ever warmup (RE §21.21/
            // §21.22).  C matters: the cursor was found NONZERO (1) at this
            // same natural entry on the reference savefile — slot 0 was
            // already spawned+frozen (dir==0, STATE=-1) by earlier activity
            // (title-screen bg render?), so the REAL spawn sequence starts at
            // a later slot; a seed-only pin can't reproduce that.  The
            // dead-slot records matter too: the shadow pass only checks
            // visible==-1 (not dir==0), so slot 0 still casts a contact
            // shadow — at whatever leftover x/y/z it holds.  This is a no-op
            // HERE (retail already IS the natural source these values were
            // captured from) but applied anyway for a fully self-consistent,
            // bilateral pin.  Unlike {phasepin}, this does NOT reset
            // db054/anim/b154/rmb (which stalls the skip-path wrap-up
            // cutscene) and does NOT re-arm an already-run warmup — it only
            // ever fires once, on the actual first call.  Mirrors the port's
            // scene1_bg_npc_seed_pin.
            if (g_segtrace_bgnpcseed_active && !g_bgnpcseed_applied &&
                rva(0x073a8bb8).readS32() === 0) {
                g_bgnpcseed_applied = true;
                rva(0x006023a0).writeU32(g_segtrace_bgnpcseed >>> 0);
                rva(0x073a8bb4).writeS32(g_segtrace_bgnpcseed_cursor | 0);
                if (g_segtrace_bgnpcseed_dead.length > 0) {
                    const base = rva(0x073a7f80);
                    for (let i = 0; i < g_segtrace_bgnpcseed_dead.length; i++)
                        base.add(i * 4).writeU32(g_segtrace_bgnpcseed_dead[i] >>> 0);
                }
                log('bg-npc: {bgnpcseed} applied, seed=' + g_segtrace_bgnpcseed +
                    ' cursor=' + g_segtrace_bgnpcseed_cursor +
                    ' dead-dwords=' + g_segtrace_bgnpcseed_dead.length +
                    ' @ frame ' + frameNo());
            }
            if (!g_bg_npc_pin_pending) return;
            g_bg_npc_pin_pending = false;
            rva(0x006023a0).writeU32(BG_NPC_PIN_SEED >>> 0); // LCG state
            rva(0x0438b4e0).writeS32(0);                      // spawn-freeze gate open
            log('bg-npc phasepin: warmup re-seeded to ' + BG_NPC_PIN_SEED +
                ' @ frame ' + frameNo());
        },
    });
    log('bg-npc phasepin hook installed');
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

                // ── live-probe (probe daemon) hooks — run FIRST, pre-sim ──
                // force_active: re-assert the engine tick gate every poll so
                // a user click-away (WM_ACTIVATE deactivate on the visible
                // no-activate preview window) can't park the engine in
                // WaitMessage mid-session.
                if (g_probe_force_active) {
                    try { rva(ADDR.var_pause_flag).writeU32(1); }
                    catch (e) { /* pre-window boot — gate not mapped yet */ }
                }
                // Engine-thread call queue: probeEnqueueCall RPCs park here
                // and run at THIS pre-sim point (the same slot segtrace ops
                // fire in), so a queued engine call can never race the sim.
                if (g_probe_calls.length > 0) {
                    probeRunQueuedCalls();
                }

                // Injection (optional). Advance the monotonic cursor
                // through every trace entry with frame <= fn; the last
                // such mask is the sticky value to apply. If the
                // engine ran multiple ticks between Presents (shouldn't
                // — input_poll is called once per tick — but defensive)
                // this still picks the most-recent applicable entry.
                // The live-probe branch OWNS the mask while probe_active
                // (input lock: real keyboard/pad state is overwritten
                // post-poll — toggling probe_active off restores the
                // user's interactive input).
                if (g_probe_active) {
                    rva(ADDR.var_input_mask).writeU16(probeMaskTick());
                } else if (g_segtrace_active) {
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

                // Wrap-up skip DRIVER (RE §21.5) — ARM-ONLY.  The intermittent stall is
                // ONLY the ESC failing to OPEN the "Skip this event?" box (the arm gate
                // FUN_0046c2cb needs 1<skip_prompt, and skip_prompt resets to 0 on each
                // dialogue re-init, so under load jitter the recording's single ESC@+25
                // hits skip_prompt<=1 and the box never opens — the dialogue then runs
                // free → the segtrace skip waits deadlock).  So we ONLY make the ARM
                // robust: while the post-tutorial wrap-up has a line up (DAT_073a6a38>=0,
                // cc08!=4, after the tutorial, before the f406 entry) AND the box is
                // CLOSED (DAT_073a3dec==0), re-post a real WndProc ESC every frame — it
                // opens the instant skip_prompt clears 1, robust to the resets.  We do
                // NOT touch the input mask and do NOT confirm: the scenario's recorded
                // X@(CONV_POSE_BLINK+34) confirms the now-open box at its intended time,
                // so the skip TEARDOWN + the f406 entry stay bit-identical to the
                // recording (an early agent confirm diverged the flow → no entry, the
                // 1st attempt).  Stop posting once the box opens; auto-off at the entry.
                if (g_wrapup_skip_active && !g_bgnpc_soa_dumped && !g_wrapup_box_was_open) {
                    try {
                        const csv = rva(0x0438cc08).readS32();
                        if (csv === 4) g_wrapup_seen_tutorial = true;  // latch the tutorial
                        const postTut = g_wrapup_seen_tutorial && csv !== 4; // the POST-tutorial wrap-up
                        const boxOpen = rva(0x073a3dec).readS32() !== 0;     // DAT_073a3dec — skip box OPEN
                        // Latch ONLY the post-tutorial wrap-up skip box — NOT the tutorial's
                        // own "Cancelling tutorial?" box, which ALSO touches DAT_073a3dec (that
                        // tripped the latch pre-wrap-up ⇒ the driver never armed ⇒ blink-stall).
                        // Once the wrap-up box has opened the arm succeeded, the recording's X
                        // confirms it, and we must NEVER post ESC again — an ESC in the post-skip
                        // free-roam→f406-entry window opens the pause box ⇒ the LOADING/PAUSE
                        // softlock LOOP (RE §21.5).
                        if (postTut && boxOpen) g_wrapup_box_was_open = true;
                        const lineP = rva(0x073a6a38).readS32();
                        const skipP = rva(0x073a3e18).readS32();      // DAT_073a3e18 — per-line tick ctr
                        let didPost = 0;
                        if (postTut && !boxOpen &&                    // box CLOSED ⇒ keep trying to ARM
                            lineP >= 0) {                             // DAT_073a6a38 — a line is shown
                            if (g_esc_post === null)
                                g_esc_post = new NativeFunction(
                                    rva(0x0047b2e7), 'int',
                                    ['pointer', 'uint', 'uint', 'pointer']);
                            const hwnd = rva(0x073dfc7c).readPointer();
                            g_esc_post(hwnd, 0x100, 0x1b, ptr(0));    // WndProc(WM_KEYDOWN, VK_ESCAPE)
                            didPost = 1;
                        }
                        // DIAG (RE §21.6): log on state-change so a re-drive SHOWS why the
                        // arm fires or fails (postTut/box/line/skip_prompt/posted).
                        if (g_wrapup_seen_tutorial) {
                            const st = (postTut?8:0)|(boxOpen?4:0)|(lineP>=0?2:0)|didPost;
                            if (st !== g_wrapup_dbg_prev) {
                                log('wrapup_dbg fn=' + fn + ' cc08=' + csv +
                                    ' postTut=' + (postTut?1:0) + ' box=' + (boxOpen?1:0) +
                                    ' line=' + lineP + ' skipP=' + skipP +
                                    ' posted=' + didPost + ' latched=' + (g_wrapup_box_was_open?1:0));
                                g_wrapup_dbg_prev = st;
                            }
                        }
                    } catch (e) { err('wrapup_skip', e.message); }
                }

                const mask = rva(ADDR.var_input_mask).readU16();
                // Long-lived probe sessions dedup the per-frame input_state
                // stream (change-points only — hours of idle would otherwise
                // pump 60 msg/s at the daemon); capture runs keep the dense
                // per-frame stream the recorder/distiller expect.
                if (!g_probe_mode || mask !== g_probe_last_input_sent) {
                    g_probe_last_input_sent = mask;
                    send({kind: 'input_state',
                          t_ms: nowMs(),
                          frame: fn,
                          buttons: mask});
                }

                if (g_watch.length || g_rng_count) {
                    const vals = {};
                    for (let i = 0; i < g_watch.length; i++) {
                        try { vals[g_watch[i].name] = watchRead(g_watch[i]); }
                        catch (e) { vals[g_watch[i].name] = null; }
                    }
                    if (g_rng_count) vals.rngcalls = g_rng_count_total;
                    send({kind: 'watch', frame: fn, vals: vals});
                }

                // Skip-event probe: inject a REAL keyboard ESC (WM_KEYDOWN +
                // WM_KEYUP VK_ESCAPE) to the engine window each frame across a
                // window — the faithful trigger (the engine's own message pump
                // dispatches it to WndProc FUN_0047b2e7 at the real loop point,
                // i.e. NOT a direct function call). Mirrors the user spamming
                // ESC. Stops once the skip arms (DAT_06a49998 > 0).
                if (g_arm_skip_at_frame >= 0 && !g_arm_skip_done &&
                    fn >= g_arm_skip_at_frame &&
                    fn < g_arm_skip_at_frame + 240) {
                    try {
                        if (g_esc_post === null) {
                            // DIAGNOSTIC delivery-hack: call the engine WndProc
                            // directly with the ESC keydown message — bypasses
                            // the hidden-window message pump to isolate "ESC not
                            // delivered" vs "arm gate rejects". Runs the full
                            // WndProc ESC dispatch (FUN_00452911 → b1c0 → 0045337b).
                            g_esc_post = new NativeFunction(
                                rva(0x0047b2e7), 'int',
                                ['pointer','uint','uint','pointer']);
                        }
                        const hwnd = rva(0x073dfc7c).readPointer();
                        g_esc_post(hwnd, 0x100, 0x1b, ptr(0));   // WndProc(WM_KEYDOWN,ESC)
                        if (rva(0x06a49998).readS32() > 0) {
                            send({kind: 'arm_skip', frame: fn});
                            g_arm_skip_done = true;
                        }
                    } catch (e) {
                        err('arm_skip', e.message);
                    }
                }

                // RNG caller histogram: flush the cumulative map periodically
                // (the host overwrites a single dict, so the last flush is the
                // full run total) and the reader can also see growth over time.
                if (g_rng_callers && (fn % 200 === 0)) {
                    send({kind: 'rng_callers', frame: fn,
                          hist: g_rng_callers_map});
                }
                if (g_rng_cs_hi >= 0 && !g_rng_cs_flushed && fn >= g_rng_cs_hi) {
                    send({kind: 'rng_callsites',
                          lo: g_rng_cs_lo, hi: g_rng_cs_hi, frames: g_rng_cs_buf});
                    g_rng_cs_flushed = true;
                    log('rng_callsites flushed: frames [' + g_rng_cs_lo + ',' +
                        g_rng_cs_hi + ')');
                }
            } catch (e) {
                err('input_poll.onLeave', e.message);
            }
        },
    });
    log('input hook installed');
}

// ─── ESC-record hook (recorder) ─────────────────────────────────────────
// ESC is keyboard/WndProc-only — it never appears in the DInput mask
// (DAT_073dddd0) the input hook samples, so the recorder would miss the
// user's dialogue-skip ESC presses. Hook the WndProc ESC→skip-event entry
// (FUN_0045337b, the same function the {esc} replay path posts into) and
// emit an {esc_record, frame} message on each call. The driver writes these
// as {esc} rows in the raw, so a recorded ESC skip replays via the {esc} op
// exactly the way the port's F2 recorder captures it.
function installEscRecordHook() {
    Interceptor.attach(rva(ADDR.fn_wndproc_esc_skip), {
        onEnter: function () {
            send({kind: 'esc_record', frame: frameNo()});
        },
    });
    log('esc-record hook installed (FUN_0045337b)');
}

// ─── window-hide hook ───────────────────────────────────────────────────

// SW_HIDE = 0 per WinUser.h. Documented value, hardcoded everywhere
// from MSVC's CRT to the SDK. SW_SHOWNOACTIVATE = 4 shows the window
// WITHOUT activating it — the live-probe preview seat: the user can watch
// the agent drive the game but focus never leaves their foreground app.
const SW_HIDE = 0;
const SW_SHOWNOACTIVATE = 4;

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
            const subCmd = g_hide_window ? SW_HIDE : SW_SHOWNOACTIVATE;
            args[1] = ptr(subCmd);

            // Compensate for the WM_ACTIVATE that the engine would
            // normally use to flip its pause flag to 1. Without this
            // write the engine sits in WaitMessage forever (all.c
            // line 79001 gates the tick on DAT_073dfca0 != 0).
            // Needed for BOTH modes: SW_HIDE never activates, and
            // SW_SHOWNOACTIVATE by definition doesn't either. (Probe
            // mode additionally re-forces it every input_poll via
            // force_active, so a later user click-away can't freeze
            // the engine when it delivers WM_ACTIVATE(deactivate).)
            try {
                rva(ADDR.var_pause_flag).writeU32(1);
            } catch (e) {
                err('installShowWindowHook/pause-flag', e.message);
            }
            send({kind: 'log',
                  msg: 'ShowWindow(' +
                       (g_hide_window ? 'SW_HIDE' : 'SW_SHOWNOACTIVATE') +
                       ') substituted (was nCmdShow=' +
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

// TAS save virtualization — redirect every save.dat/_save.dat open into the
// per-run sandbox so a replay can never read or write the user's real save.
// Hooks kernel32!CreateFileW + CreateFileA: the CRT's fopen("save.dat", ...)
// funnels through one of these regardless of static/dynamic CRT linkage, so it's
// robust to the build. On a matching open we rewrite the path argument in place
// to <sandbox>\<basename>. Reads of a non-existent sandbox file fail cleanly →
// the engine boots fresh (the @fresh case); writes create the sandbox file.
function installSaveRedirectHook(sandboxWin) {
    sandboxWin = String(sandboxWin).replace(/[\\/]+$/, '');

    function isSaveBase(name) {
        const m = /[^\\/]*$/.exec(name);
        const b = (m ? m[0] : name).toLowerCase();
        return b === 'save.dat' || b === '_save.dat';
    }
    function saveBase(name) {
        const m = /[^\\/]*$/.exec(name);
        return m ? m[0] : name;
    }

    function attachOne(exportName, wide) {
        // Frida 17.x: resolve the module instance first, then the export off it
        // (the legacy Module.getExportByName(name, export) global was removed).
        const k32 = Process.findModuleByName('kernel32.dll');
        const addr = k32 ? k32.findExportByName(exportName) : null;
        if (!addr) { err('saveRedirect', 'no ' + exportName); return; }
        Interceptor.attach(addr, {
            onEnter: function (args) {
                try {
                    const p = args[0];
                    if (p.isNull()) return;
                    const orig = wide ? p.readUtf16String() : p.readAnsiString();
                    if (!orig || !isSaveBase(orig)) return;
                    const np = sandboxWin + '\\' + saveBase(orig);
                    const buf = wide ? Memory.allocUtf16String(np)
                                     : Memory.allocAnsiString(np);
                    g_save_redirect_keep.push(buf);   // outlive the call
                    args[0] = buf;
                    log('saveRedirect ' + exportName + ': ' + orig + ' -> ' + np);
                } catch (e) { err('saveRedirect.onEnter', e.message); }
            }
        });
    }

    attachOne('CreateFileW', true);
    attachOne('CreateFileA', false);
    log('save redirect armed → sandbox ' + sandboxWin);
}

// Snapshot the live save arena and ship it to the driver, which writes it to a
// .bin + a {savefile}/{save_write} raw row (same format the port F2 recorder
// emits, so distill_trace.py handles it unchanged). `which` is 'boot' (initial)
// or 'write' (an in-session save); `frame` is the recorder-relative frame.
function captureSaveArena(which, frame, index) {
    try {
        const bytes = rva(ADDR.var_save_arena).readByteArray(SAVE_ARENA_SIZE);
        if (!bytes) { err('captureSaveArena', 'null arena read'); return; }
        send({kind: 'save_capture', which: which, frame: frame | 0,
              index: index | 0, size: SAVE_ARENA_SIZE}, bytes);
        log('save_capture ' + which + ' #' + (index | 0) +
            ' @frame ' + (frame | 0) + ' (' + SAVE_ARENA_SIZE + ' bytes)');
    } catch (e) { err('captureSaveArena', e.message); }
}

// Hook FUN_004905a8 (engine save-write) — snapshot the arena AFTER each write so
// the recording captures every in-session save the game made.
function installSaveWriteHook() {
    Interceptor.attach(rva(ADDR.fn_save_write), {
        onLeave: function () {
            if (!g_capture_saves) return;
            captureSaveArena('write', g_manual_frame_counter, g_save_write_idx++);
        }
    });
    log('save-write capture hook armed (FUN_004905a8)');
}

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
            // Conditional so the live-probe daemon can toggle audio at
            // runtime (probeSetSilentAudio): the clamp takes effect on the
            // NEXT SetVolume call (SEs immediately, BGM on the next
            // track-change/fade).
            if (g_silent_audio_enabled) args[1] = ptr(-10000);
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

// ─── RNG seed pinning ───────────────────────────────────────────────────
// Force DAT_006023a0 to `seed` right after the engine's lone WinMain reseed
// (FUN_005041ec @ 0x5041ec, called once from all.c:78946).  The port pins the
// same point via --rng-seed (rng.c rng_seed_from_now is replaced by
// rng_seed(n)); pinning both makes every RNG-driven position — foot-dust,
// ambient motes, particle jitter — directly comparable instead of seed-shifted.
function installRngSeedHook(seed) {
    const seedPtr = rva(ADDR.var_lcg_seed);
    Interceptor.attach(rva(ADDR.rng_set_seed), {
        onLeave: function () {
            try {
                seedPtr.writeU32(seed >>> 0);
                send({kind: 'log',
                      msg: 'rng_seed: DAT_006023a0 pinned = ' + (seed >>> 0)});
            } catch (e) {
                err('rng_seed.onLeave', e.message);
            }
        },
    });
    log('rng_seed: armed on FUN_005041ec exit (seed=' + (seed >>> 0) + ')');
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

// D3DPRIMITIVETYPE → vertex/index count referenced by a (type, prim_count)
// draw — needed to know how many stride-byte vertices to dump from an
// immediate-mode array.  Mirrors d3d_prim_vcount() in src/d3d_trace.c +
// the decode in tools/render_diff.py.
function primVcount(t, pc) {
    switch (t) {
        case 1: return pc;            // POINTLIST
        case 2: return pc * 2;        // LINELIST
        case 3: return pc ? pc + 1 : 0; // LINESTRIP
        case 4: return pc * 3;        // TRIANGLELIST
        case 5: return pc ? pc + 2 : 0; // TRIANGLESTRIP
        case 6: return pc ? pc + 2 : 0; // TRIANGLEFAN
        default: return 0;
    }
}

// Same per-draw byte cap as the port (D3D_TRACE_VB_CAP). UP draws are tiny;
// the cap only guards a pathological large draw from bloating the wire.
const D3D_TRACE_VB_CAP = 65536;
const _HEX = '0123456789abcdef';

// Read `nbytes` from `ptr` as a lowercase hex string (matching the port's
// d3d_emit_hex). Returns null on a null/zero/over-cap/failed read so the
// caller can emit an `_over`/absent marker instead.
function traceReadHex(ptr, nbytes) {
    if (ptr.isNull() || nbytes <= 0) return null;
    if (nbytes > D3D_TRACE_VB_CAP) return undefined;   // over cap
    let bytes;
    try {
        bytes = new Uint8Array(ptr.readByteArray(nbytes));
    } catch (_) {
        return null;
    }
    let s = '';
    for (let i = 0; i < bytes.length; i++) {
        const b = bytes[i];
        s += _HEX[b >> 4] + _HEX[b & 0xf];
    }
    return s;
}

// Append vertex/index byte fields to a draw event's args.  `countKey`/
// `bytesKey`/`overKey` name the JSON fields; `count`×`elemSize` bytes are
// read from `dataPtr`.  Always sets the count; sets bytes or an over-marker.
function traceAddBuf(args, countKey, count, bytesKey, overKey,
                     dataPtr, elemSize) {
    args[countKey] = count;
    if (!count) return;
    const hex = traceReadHex(dataPtr, count * elemSize);
    if (hex === undefined) args[overKey] = count * elemSize;
    else if (hex !== null) args[bytesKey] = hex;
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

// Hook the engine's texture loaders so a bound texture pointer can be
// resolved back to its source asset name (the load-stable identity the
// render diff keys on).  Both loaders are __cdecl free functions that pass
// their output slot as the FUN_004cd30e (D3DXCreateTextureFromFileInMemoryEx)
// ppTexture arg; the created IDirect3DTexture8* lands at the first dword of
// that slot.  These hooks are UNGATED (loads happen on non-captured frames).
//
//   FUN_0047193c(blend, slot, path, w, h)   — UI/2D loader, MipLevels=1
//       esp+8  = slot (ppTexture), esp+12 = path
//   FUN_00471b24(slot, path)                 — mesh loader, MipLevels=0
//       esp+4  = slot (ppTexture), esp+8  = path
function installTexNameHooks() {
    if (g_tex_name_hooked) return;

    function hookLoader(va, slotOff, pathOff) {
        Interceptor.attach(rva(va), {
            onEnter: function (args) {
                this.slot = this.context.esp.add(slotOff).readPointer();
                const p   = this.context.esp.add(pathOff).readPointer();
                try { this.name = p.isNull() ? null : p.readCString(); }
                catch (e) { this.name = null; }
            },
            onLeave: function (retval) {
                if (!this.name || this.slot.isNull()) return;
                try {
                    const tex = this.slot.readPointer();   // *ppTexture
                    if (!tex.isNull())
                        g_tex_names['0x' + tex.toString(16)] = this.name;
                } catch (e) { /* slot not yet populated — skip */ }
            },
        });
    }

    hookLoader(0x0047193c, 8, 12);   // UI loader
    hookLoader(0x00471b24, 4, 8);    // mesh loader
    g_tex_name_hooked = true;
}

function installD3dTraceHooks(devicePtr) {
    if (g_d3d_trace_hooked) return;
    installTexNameHooks();

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
            const texHex = '0x' + args[2].toString(16);
            const a = {stage: args[1].toUInt32(), texture: texHex};
            const name = g_tex_names[texHex];
            if (name) a.tex_name = name;
            traceEmit({
                op: 'SetTexture',
                args: a,
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
            const primType = args[1].toUInt32();
            const primCount = args[2].toUInt32();
            const stride = args[4].toUInt32();
            const a = {prim_type:  primType,
                       prim_count: primCount,
                       vb:         '0x' + args[3].toString(16),
                       vb_stride:  stride};
            if (g_d3d_trace_verts) {
                traceAddBuf(a, 'vb_nverts', primVcount(primType, primCount),
                            'vb_bytes', 'vb_over', args[3], stride);
            }
            traceEmit({op: 'DrawPrimitiveUP', args: a,
                       ret_va: traceRetVa(this.returnAddress)});
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
            const primType = args[1].toUInt32();
            const minVtxIdx = args[2].toUInt32();
            const numVtx = args[3].toUInt32();
            const primCount = args[4].toUInt32();
            const ibFmt = args[6].toUInt32();
            const stride = args[8].toUInt32();
            const a = {prim_type:        primType,
                       min_vtx_idx:      minVtxIdx,
                       num_vtx_indices:  numVtx,
                       prim_count:       primCount,
                       ib:               '0x' + args[5].toString(16),
                       ib_fmt:           ibFmt,
                       vb:               '0x' + args[7].toString(16),
                       vb_stride:        stride};
            if (g_d3d_trace_verts) {
                // ibFmt: D3DFMT_INDEX16 (101) → 2-byte indices, else 4-byte.
                const idxSize = (ibFmt === 101) ? 2 : 4;
                traceAddBuf(a, 'ib_nidx', primVcount(primType, primCount),
                            'ib_bytes', 'ib_over', args[5], idxSize);
                traceAddBuf(a, 'vb_nverts', minVtxIdx + numVtx,
                            'vb_bytes', 'vb_over', args[7], stride);
            }
            traceEmit({op: 'DrawIndexedPrimitiveUP', args: a,
                       ret_va: traceRetVa(this.returnAddress)});
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
const ANCHOR_SCENE_MARKET = 6;   // mode 6 — the Market scene (Merchant's Guild + ichiba)
const ANCHOR_SCENE_PAUSE  = 9;   // mode 9 — the in-game PAUSE menu (DAT_0438b1c0)

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
// Synthesise an ESC keypress on retail by calling the engine WndProc directly
// with WM_KEYDOWN VK_ESCAPE (the same faithful path the skip-event probe uses:
// runs the full WndProc ESC dispatch FUN_0047b2e7, NOT a bypass). Lazily builds
// the NativeFunction. Shared by the {esc} segtrace op and the arm-skip probe.
function synthesizeEscRetail() {
    if (g_esc_post === null) {
        g_esc_post = new NativeFunction(
            rva(0x0047b2e7), 'int', ['pointer','uint','uint','pointer']);
    }
    const hwnd = rva(0x073dfc7c).readPointer();
    g_esc_post(hwnd, 0x100, 0x1b, ptr(0));   // WndProc(WM_KEYDOWN, VK_ESCAPE)
}

// {tutloadpin:N} — the retail half of the tutorial-load-bracket pin (the port
// half overrides IVE_TUT_LOAD_FRAMES; engine-quirks §119).
//
// Mechanism (v2 — the gate-write hold was unimplementable): the tutorial
// activation (FUN_0044bd0d) sets DAT_0438b1c8=2 and calls FUN_00452d07, which
// raises the load gates and spawns the LAB_00452aab worker. The worker's TAIL
// (0x452ac2, after its CloseHandle) performs the WHOLE bracket-end transition
// itself, mid-frame, on the worker thread: handle=0, DAT_06a4995c=0,
// DAT_06a49960=0, DAT_0438b1c8=1. The main loop never polls anything — so a
// once-per-frame gate re-write can't extend the bracket (v1 raced and lost
// systematically). Instead we BLOCK the worker thread at its tail until the
// release frame: the load then genuinely lasts N frames — the engine idles
// exactly as for a slow disk (overlay, db054++, wing emits), and the handoff
// (gates→0, b1c8→1) is the engine's own code, just deferred.
//
// The block lives in a CModule (C callback — Frida JS callbacks serialize on
// the JS lock, so a JS-side sleep loop on the worker thread would deadlock
// the per-frame hooks). Shared flags: tlp_flags[0]=release granted,
// tlp_flags[1]=worker waiting. Block-by-default while a {tutloadpin} trace is
// live (flags[0]=0) so even a sub-frame load that reaches the tail before the
// pre-sim arm tick still blocks; every 452aab tail belongs to a b1c8==2
// dialogue-script bracket, whose arm always follows within a frame.
//
// Fence-posts: the arm sim-frame g fires LOADING_START@g (Present g reads the
// gates up); the pre-sim arm tick lands at g+1 → release_frame = g+N. The
// Present hook of frame g+N grants release BEFORE anchorTick and spins until
// the worker's tail completes (b1c8 leaves 2, ~µs after the 1ms sleep wakes),
// so THAT Present samples gates==0 → LOADING_END@g+N, END−START == N — and
// the script's first tick lands at g+N+1, both matching the port's
// D_TUT_LOAD counter exactly.
//
// EXTEND-only: a real load ≥ N frames reaches the tail after the release
// frame, finds flags[0] already granted, and sails through at its natural
// length (the prologue's ~68f inter-script bracket does exactly this).
// Shared worker-tail block CModule source — used by BOTH the {tutloadpin}
// (b1c8 dialogue load) and {csloadpin} (b1cc d3e cs-load) blockers.  Each
// instantiation binds its OWN flags cell (so the two pins are independent);
// the symbol names stay tlp_* in C (the JS binding object maps them).  Kept in
// one place so the TinyCC parse (validated by tools/test_tutloadpin_cmodule.py)
// covers both pins.
const WORKER_TAIL_BLOCK_CM_SRC = `
#include <gum/guminterceptor.h>
extern volatile int tlp_flags[2];
extern void *tlp_yield;   /* cell holding kernel32!SwitchToThread */
extern void *tlp_tick;    /* cell holding kernel32!GetTickCount   */
typedef unsigned int (*fn0) (void);
void
onEnter (GumInvocationContext *ic)
{
  unsigned int t0;
  (void) ic;
  if (tlp_flags[0] != 0)
    return;
  tlp_flags[1] = 1;
  t0 = ((fn0) tlp_tick) ();
  while (tlp_flags[0] == 0) {
    ((fn0) tlp_yield) ();
    if (((fn0) tlp_tick) () - t0 > 20000)   /* fail-open after ~20 s */
      break;
  }
  tlp_flags[1] = 0;
}
`;

function installTutLoadPinWorkerHook() {
    if (g_tlp_hook_installed) return;
    try {
        const k32 = Process.findModuleByName('kernel32.dll');
        const yieldAddr = k32 ? k32.findExportByName('SwitchToThread') : null;
        const tickAddr  = k32 ? k32.findExportByName('GetTickCount')  : null;
        if (!yieldAddr || !tickAddr) {
            err('tutloadpin', 'kernel32 exports not found'); return;
        }
        g_tlp_flags = Memory.alloc(8);
        g_tlp_flags.writeS32(1);            // pass-through until a trace pins
        g_tlp_flags.add(4).writeS32(0);
        // 0-ARG kernel32 functions only: for zero args, stdcall and cdecl
        // generate the identical call sequence, so TinyCC's missing __stdcall
        // (it rejected the typedef — first attempt failed to compile) costs
        // nothing. Each cell holds the export's address; the C side casts and
        // calls through it.
        const yieldCell = Memory.alloc(Process.pointerSize);
        yieldCell.writePointer(yieldAddr);
        const tickCell = Memory.alloc(Process.pointerSize);
        tickCell.writePointer(tickAddr);
        const cm = new CModule(WORKER_TAIL_BLOCK_CM_SRC,
            {tlp_flags: g_tlp_flags, tlp_yield: yieldCell, tlp_tick: tickCell});
        Interceptor.attach(rva(ADDR.fn_dlg_load_worker_tail), cm);
        g_tlp_cmodule = cm;                 // keep alive (GC'd CModule = crash)
        g_tlp_hook_installed = true;
        log('tutloadpin: worker-tail hook installed @0x' +
            (ADDR.fn_dlg_load_worker_tail >>> 0).toString(16));
    } catch (ex) {
        err('tutloadpin', 'worker hook install failed: ' + ex.message);
        if (g_tlp_flags) g_tlp_flags.writeS32(1);   // stay fail-open
    }
}
let g_tlp_cmodule = null;

// Pre-sim arm detection (input_poll). The b1c8 2-rising edge is visible here
// one frame after the arming sim — the worker can't advance b1c8 itself while
// blocked, so the edge is never missed.
function tutloadpinTick(fn) {
    if (!g_tlp_flags) return;
    const b1c8 = rva(ADDR.var_dlg_active).readS32();
    if (!g_tlp_armed && b1c8 === 2 && g_tlp_prev_b1c8 !== 2) {
        g_tlp_armed = true;
        g_tlp_release_frame = fn - 1 + g_segtrace_tutloadpin;
        g_tlp_flags.writeS32(0);            // (re-)block the worker tail
        log('tutloadpin: bracket armed at frame ' + fn + ' (release at ' +
            g_tlp_release_frame + ')');
    } else if (g_tlp_armed && b1c8 !== 2) {
        // Dialogue torn down mid-bracket (skip/teardown) — fail open.
        g_tlp_flags.writeS32(1);
        g_tlp_armed = false;
        log('tutloadpin: bracket disarmed (b1c8 left 2) at frame ' + fn);
    } else if (!g_tlp_armed && b1c8 !== 2) {
        // BETWEEN loads — keep the worker tail BLOCKED by default so the next
        // dialogue load's worker blocks on entry (the b1c8 2-edge is then never
        // missed; see the csloadpinTick twin for the full rationale + the v3
        // re-arm race this closes).  Extend-only preserved by the `!== 2` guard.
        g_tlp_flags.writeS32(0);
    }
    g_tlp_prev_b1c8 = b1c8;
}

// Present-hook release — runs BEFORE anchorTick so the release frame's own
// anchor sample reads the post-handoff state (LOADING_END fires at exactly
// release_frame).
function tutloadpinPresentRelease(fn) {
    if (!g_tlp_armed || !g_tlp_flags || fn < g_tlp_release_frame) return;
    const waiting = g_tlp_flags.add(4).readS32() !== 0;
    g_tlp_flags.writeS32(1);                // grant release
    if (waiting) {
        // The worker wakes within ~1ms and finishes its tail in µs; spin so
        // this Present's anchorTick sees gates==0 / b1c8==1.
        const t0 = nowMs();
        while (rva(ADDR.var_dlg_active).readS32() === 2) {
            if (nowMs() - t0 > 500) {
                err('tutloadpin', 'release spin timeout at frame ' + fn);
                break;
            }
            Thread.sleep(0.0005);
        }
        log('tutloadpin: bracket released at frame ' + fn);
    } else {
        log('tutloadpin: real load >= pin at frame ' + fn +
            ' - left alone (extend-only)');
    }
    g_tlp_armed = false;
}

// {csloadpin} — the cc08==4 d3e load-bracket blocker.  Same machinery as the
// tutloadpin trio above, on the b1cc gate + the two d3e worker tails (RE §20).
function installCsLoadPinWorkerHook() {
    if (g_csl_hook_installed) return;
    try {
        const k32 = Process.findModuleByName('kernel32.dll');
        const yieldAddr = k32 ? k32.findExportByName('SwitchToThread') : null;
        const tickAddr  = k32 ? k32.findExportByName('GetTickCount')  : null;
        if (!yieldAddr || !tickAddr) {
            err('csloadpin', 'kernel32 exports not found'); return;
        }
        g_csl_flags = Memory.alloc(8);
        g_csl_flags.writeS32(1);            // pass-through until a trace pins
        g_csl_flags.add(4).writeS32(0);
        const yieldCell = Memory.alloc(Process.pointerSize);
        yieldCell.writePointer(yieldAddr);
        const tickCell = Memory.alloc(Process.pointerSize);
        tickCell.writePointer(tickAddr);
        const cm = new CModule(WORKER_TAIL_BLOCK_CM_SRC,
            {tlp_flags: g_csl_flags, tlp_yield: yieldCell, tlp_tick: tickCell});
        // Both d3e worker tails (param-0 session_init / param-1 occ3 reload)
        // share the cm — only one fires per load, both block on g_csl_flags.
        Interceptor.attach(rva(ADDR.fn_d3e_load_worker_tail_ae8), cm);
        Interceptor.attach(rva(ADDR.fn_d3e_load_worker_tail_b13), cm);
        g_csl_cmodule = cm;                 // keep alive (GC'd CModule = crash)
        g_csl_hook_installed = true;
        log('csloadpin: d3e worker-tail hooks installed @0x' +
            (ADDR.fn_d3e_load_worker_tail_ae8 >>> 0).toString(16) + ' +0x' +
            (ADDR.fn_d3e_load_worker_tail_b13 >>> 0).toString(16));
    } catch (ex) {
        err('csloadpin', 'worker hook install failed: ' + ex.message);
        if (g_csl_flags) g_csl_flags.writeS32(1);   // stay fail-open
    }
}

// Pre-sim arm detection: the b1cc 2-rising edge (cc08 cs-load spawn).
function csloadpinTick(fn) {
    if (!g_csl_flags) return;
    const b1cc = rva(ADDR.var_cs_load_gate).readS32();
    if (!g_csl_armed && b1cc === 2 && g_csl_prev_b1cc !== 2) {
        g_csl_armed = true;
        g_csl_release_frame = fn - 1 + g_segtrace_csloadpin;
        g_csl_flags.writeS32(0);            // (re-)block the d3e worker tails
        log('csloadpin: bracket armed at frame ' + fn + ' (release at ' +
            g_csl_release_frame + ')');
    } else if (g_csl_armed && b1cc !== 2) {
        // Load gate torn down mid-bracket — fail open.
        g_csl_flags.writeS32(1);
        g_csl_armed = false;
        log('csloadpin: bracket disarmed (b1cc left 2) at frame ' + fn);
    } else if (!g_csl_armed && b1cc !== 2) {
        // BETWEEN loads — keep the worker tail BLOCKED by default (flags[0]=0)
        // so the NEXT cc08 load's worker blocks the instant it reaches the tail.
        // This restores the init invariant after a release: the "edge never
        // missed" guarantee (a blocked worker can't advance b1cc itself, so the
        // 2-rising edge is always visible to the arm above) only holds while
        // flags default to blocked.  Without this, flags[0] stays 1 (open) after
        // csloadpinPresentRelease, and a FAST load's worker reaches the tail
        // before this tick re-arms → it passes through → b1cc clears in a single
        // frame → the bracket never arms and the pin is silently skipped.  That
        // is the v3-harness "1 of 4 brackets" gap (RE §20 CORRECTION); --target
        // both only fired all 4 by luck (its slower retail loads let this tick
        // win the race).  Extend-only is preserved: the b1cc===2 && !armed
        // pass-through window (a real load longer than the pin, after release)
        // is left untouched by the `b1cc !== 2` guard.
        g_csl_flags.writeS32(0);
    }
    g_csl_prev_b1cc = b1cc;
}

// Present-hook release (mirror of tutloadpinPresentRelease) — grant release N
// frames past the arm + spin until the worker's tail completes so this Present's
// anchorTick samples b1cc==1 (LOADING_END at release_frame).
function csloadpinPresentRelease(fn) {
    if (!g_csl_armed || !g_csl_flags || fn < g_csl_release_frame) return;
    const waiting = g_csl_flags.add(4).readS32() !== 0;
    g_csl_flags.writeS32(1);                // grant release
    if (waiting) {
        const t0 = nowMs();
        while (rva(ADDR.var_cs_load_gate).readS32() === 2) {
            if (nowMs() - t0 > 500) {
                err('csloadpin', 'release spin timeout at frame ' + fn);
                break;
            }
            Thread.sleep(0.0005);
        }
        log('csloadpin: bracket released at frame ' + fn);
    } else {
        log('csloadpin: real load >= pin at frame ' + fn +
            ' - left alone (extend-only)');
    }
    g_csl_armed = false;
}

// {primaryloadpin} arm — the primary-busy (DAT_06a49954) rising edge (the cad868
// Continue-load / scene-reload spawn, FUN_00452cde).  Runs pre-sim from segtraceTick.
// Re-arms on each rising edge so multiple primary loads in one trace are each pinned.
function primaryloadpinTick(fn) {
    const busy = rva(ADDR.var_worker_busy_primary).readS32();
    if (!g_plp_armed && busy !== 0 && g_plp_prev_busy === 0) {
        g_plp_armed = true;
        g_plp_release_frame = fn - 1 + g_segtrace_primaryloadpin;
        log('primaryloadpin: bracket armed at frame ' + fn + ' (release at ' +
            g_plp_release_frame + ')');
    }
    g_plp_prev_busy = busy;
}

// {primaryloadpin} drain — at N frames past the arm, spin the main thread until the
// primary worker clears DAT_06a49954 (the whole save-deserialize + scene-init rng
// burst is then applied before the sim resumes → a deterministic rng state).  This
// is the retail mirror of the port's worker_load_force_primary_complete: the worker
// runs on its OWN thread, so blocking the main thread here just waits for it (like
// csloadpinPresentRelease's b1cc spin).  The d3e's extend-only worker-tail block is
// impractical for the huge primary load (RE §21.2); draining sidesteps it.
function primaryloadpinPresentRelease(fn) {
    if (!g_plp_armed || fn < g_plp_release_frame) return;
    const t0 = nowMs();
    while (rva(ADDR.var_worker_busy_primary).readS32() !== 0) {
        if (nowMs() - t0 > 8000) {
            err('primaryloadpin', 'drain spin timeout at frame ' + fn);
            break;
        }
        Thread.sleep(0.0005);
    }
    log('primaryloadpin: primary load drained at frame ' + fn);
    g_plp_armed = false;
}

function segtraceTick(fn) {
    // Trace-global tutorial-load-bracket pin — runs every pre-sim tick,
    // independent of the segment cursor (brackets arm mid-segment).
    if (g_segtrace_tutloadpin > 0) {
        try { tutloadpinTick(fn); }
        catch (ex) { err('tutloadpin', ex.message); }
    }
    if (g_segtrace_csloadpin > 0) {
        try { csloadpinTick(fn); }
        catch (ex) { err('csloadpin', ex.message); }
    }
    if (g_segtrace_primaryloadpin > 0) {
        try { primaryloadpinTick(fn); }
        catch (ex) { err('primaryloadpin', ex.message); }
    }
    for (;;) {
        const seg = g_segtrace_segments[g_segtrace_seg];
        if (!seg) break;
        if (seg.wait !== null) {
            const af = g_segtrace_fired[seg.wait];
            // Resolution guard: a DIFFERENT next anchor may fire on the SAME
            // frame the current segment was entered (recording-adjacent anchors
            // compress to one frame on replay), so it resolves at af >= entry.
            // The SAME anchor recurring (HOUSE_FREEROAM twice) must take the
            // NEXT firing, so it requires af > entry. Without the per-name
            // distinction a same-frame anchor cluster stalls the whole chain.
            const sameName  = (seg.wait === g_segtrace_base_anchor);
            const resolved  = (af !== undefined) &&
                              (sameName ? af > g_segtrace_base_arm
                                        : af >= g_segtrace_base_arm);
            if (resolved) {
                g_segtrace_seg++;
                g_segtrace_base       = af;
                g_segtrace_base_arm   = af;
                g_segtrace_base_anchor = seg.wait;
                g_segtrace_entry      = 0;
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
                g_segtrace_base       = fn;
                g_segtrace_base_arm   = fn;
                g_segtrace_base_anchor = null;   // wait_until entry is not anchor-named
                g_segtrace_entry      = 0;
                segtraceOnSegmentEnter(g_segtrace_segments[g_segtrace_seg]);
                continue;
            }
        }
        while (g_segtrace_entry < seg.entries.length &&
               g_segtrace_base + seg.entries[g_segtrace_entry].frame <= fn) {
            g_segtrace_sticky = seg.entries[g_segtrace_entry].mask & 0xffff;
            g_segtrace_entry++;
        }
        // Force the LCG state at base+frame BEFORE this frame's sim consumers.
        // segtraceTick runs in input_poll.onLeave, ahead of sim (mirrors the
        // port's input_segtrace_tick firing in input_poll). Fires once.
        for (let i = 0; i < seg.setrngs.length; i++) {
            const sr = seg.setrngs[i];
            if (!sr.fired && g_segtrace_base + sr.frame <= fn) {
                rva(ADDR.var_lcg_seed).writeU32(sr.value >>> 0);
                sr.fired = true;
                log('segtrace: forced rng seed = ' + (sr.value >>> 0) +
                    ' at frame ' + fn + ' (base+' + sr.frame + ')');
            }
        }
        // Force g_sim_frame_count (DAT_0438b8cc) at base+frame — pins the 目玉
        // display-sparkle %8 phase.  Mirrors {rngseed}: pre-sim, fires once.
        // RE §21.
        for (let i = 0; i < seg.gsimpins.length; i++) {
            const gp = seg.gsimpins[i];
            if (!gp.fired && g_segtrace_base + gp.frame <= fn) {
                rva(0x0438b8cc).writeU32(gp.value >>> 0);
                gp.fired = true;
                log('segtrace: pinned g_sim_frame_count = ' + (gp.value >>> 0) +
                    ' at frame ' + fn + ' (base+' + gp.frame + ')');
            }
        }
        // {playtimepin} — force the ACTIVE working slot's total-playtime frame
        // count (working DAT_044e37a0[slot]; slot = DAT_0438b1e0, stride 0x2dfc8)
        // pre-sim, ahead of this frame's FUN_004536cb playtime tick (mirrors the
        // port's segtrace_playtimepin_cb firing in input_poll).  Normalizes the
        // async-load-bracket phase origin so the save COMMIT compares byte-exact.
        for (let i = 0; i < seg.playtimepins.length; i++) {
            const pp = seg.playtimepins[i];
            if (!pp.fired && g_segtrace_base + pp.frame <= fn) {
                const slot = rva(0x0438b1e0).readS32() >>> 0;
                const addr = rva(0x044e37a0).add(slot * 0x2dfc8);
                const prev = addr.readU32() >>> 0;
                addr.writeU32(pp.value >>> 0);
                pp.fired = true;
                log('segtrace: pinned playtime ' + prev + ' -> ' + (pp.value >>> 0) +
                    ' (slot ' + slot + ') at frame ' + fn + ' (base+' + pp.frame + ')');
            }
        }
        // {poke} STICKY writes: hold a u32 global at `val` every frame from
        // base+frame on (e.g. enable the debug-overlay gate DAT_06a49938=1).
        for (let i = 0; i < seg.pokes.length; i++) {
            const pk = seg.pokes[i];
            if (g_segtrace_base + pk.frame <= fn) {
                try { rva(pk.va).writeU32(pk.val); }
                catch (ex) { err('segtrace-poke', ex.message); }
                if (!pk.logged) {
                    pk.logged = true;
                    log('segtrace: poke 0x' + (pk.va >>> 0).toString(16) +
                        ' = ' + pk.val + ' (sticky) from frame ' + fn +
                        ' (base+' + pk.frame + ')');
                }
            }
        }
        // ESC synthesis fires in the same pre-sim window (mirrors the port's
        // fire_escs in input_segtrace_tick). Fires once per op.
        for (let i = 0; i < seg.escs.length; i++) {
            const e = seg.escs[i];
            if (!e.fired && g_segtrace_base + e.frame <= fn) {
                try { synthesizeEscRetail(); }
                catch (ex) { err('segtrace-esc', ex.message); }
                e.fired = true;
                log('segtrace: synthesised ESC at frame ' + fn +
                    ' (base+' + e.frame + ')');
            }
        }
        // {phasepin} fires in the same pre-sim window (mirrors the port's
        // fire_phasepins). Zeroes the companion's load-dependent free-roam phase.
        for (let i = 0; i < seg.phasepins.length; i++) {
            const pp = seg.phasepins[i];
            if (!pp.fired && g_segtrace_base + pp.frame <= fn) {
                const was = rva(0x056db054).readS32();
                rva(0x056db054).writeS32(0);   // DAT_056db054 bob/sparkle counter
                rva(0x056dab50).writeS32(0);   // companion anim FRAME
                rva(0x056dab48).writeS32(0);   // companion anim TIMER (float 0.0 == 0)
                rva(0x056dab4c).writeS32(0);   // companion anim COUNTER
                // player (actor 0) anim cycle — the i*0x2c mirror of the above;
                // normalizes Recette's load-dependent IDLE phase origin so a
                // pure-idle comparison is phase-clean (mirrors player_ctrl_phasepin).
                rva(0x056daaf8).writeS32(0);   // player anim FRAME
                rva(0x056daaf0).writeS32(0);   // player anim TIMER (float 0.0 == 0)
                rva(0x056daaf4).writeS32(0);   // player anim COUNTER
                // shared menu cursor bob counter — free-runs from boot with no
                // engine reset, so its value diverges with the non-deterministic
                // load; zero it so the skip-prompt hand-cursor bob is phase-clean
                // (mirrors the port's title_save_dialog_phasepin).
                rva(0x0438b154).writeS32(0);   // DAT_0438b154 cursor bob counter
                // dialogue screen-shake (rmb) countdowns — zero them so a
                // fixed-offset capture lands on the un-shaken standee base pose
                // (mirrors scene1_intro_dialogue_phasepin; engine-quirks §105).
                rva(0x073a6d98).writeS32(0);   // DAT_073a6d98 bg-shake countdown
                rva(0x073a6d9c).writeS32(0);   // DAT_073a6d9c chr-shake countdown
                // background-window NPCs (scene1_bg_npc): re-arm the warmup so
                // the next FUN_0046f621 re-runs the 180x spawn pass from a
                // canonical seed → the drifting townsfolk are reproducible/1:1
                // (mirrors scene1_bg_npc_phasepin).  Zero the 6-record SoA
                // (DAT_073a7f80, stride 0x64) + spawn cursor (DAT_073a8bb4) +
                // warmup latch (DAT_073a8bb8); the seed + spawn-gate force is
                // done at the FUN_0046f621 entry (installBgNpcPinHook) so it
                // lands exactly when the warmup is about to consume RNG.
                for (let b = 0; b < 6 * 0x64; b += 4)
                    rva(0x073a7f80).add(b).writeS32(0);
                rva(0x073a8bb4).writeS32(0);   // DAT_073a8bb4 spawn cursor
                rva(0x073a8bb8).writeS32(0);   // DAT_073a8bb8 warmup latch (re-arm)
                // g_sim_frame_count (DAT_0438b8cc) — the load-dependent sim-frame
                // origin that gates the 目玉 display sparkle (%8==3, FUN_0048670f
                // L86580). Un-normalized it makes the sparkle fire at a different
                // db054-phase port↔retail, shifting the LCG slice the same-frame foot
                // dust draws from (the dust velocity then diverges with the stream
                // bit-identical). Zero it so the sparkle↔dust relative phase matches
                // (mirrors the port's sim_phasepin; engine-quirks §112).
                rva(0x0438b8cc).writeS32(0);   // DAT_0438b8cc g_sim_frame_count
                g_bg_npc_pin_pending = true;
                pp.fired = true;
                if (g_rng_cs_len > 0) {           // arm call-site capture from here
                    g_rng_cs_lo = fn; g_rng_cs_hi = fn + g_rng_cs_len;
                    g_rng_cs_buf = {}; g_rng_cs_flushed = false;
                }
                log('segtrace: phasepin - player+companion phase reset to 0 (db054 was ' +
                    was + ') at frame ' + fn + ' (base+' + pp.frame + ')'
                    + (g_rng_cs_len > 0 ? ' [rng-callsites ' + g_rng_cs_lo + '..'
                       + g_rng_cs_hi + ']' : ''));
            }
        }
        // {memsnap} fires in the same pre-sim window, AFTER the pins above, so
        // a pinned-census snapshot sees its own frame's post-pin state. Dumps
        // each configured region [va, size] straight from game memory to disk
        // via Win32 (writeRawFile) — never over the Frida channel (the .data
        // VirtualSize span is ~145 MB; the one-time write stalls the frame for
        // a moment, which --turbo's virtual clock makes sim-neutral).
        for (let i = 0; i < seg.memsnaps.length; i++) {
            const ms = seg.memsnaps[i];
            if (!ms.fired && g_segtrace_base + ms.frame <= fn) {
                ms.fired = true;
                const resolved = g_segtrace_base + ms.frame;
                const tag = ('00000' + resolved).slice(-5);
                if (!g_capture_dir) {
                    err('segtrace-memsnap',
                        'no capture_dir (memsnap needs capture_local)');
                } else if (!g_memsnap_regions.length) {
                    err('segtrace-memsnap', 'no memsnap_regions configured');
                } else {
                    ensureWinFileFns();
                    let wrote = 0;
                    for (let r = 0; r < g_memsnap_regions.length; r++) {
                        const va = g_memsnap_regions[r][0] >>> 0;
                        const sz = g_memsnap_regions[r][1] >>> 0;
                        try {
                            writeRawFile(g_capture_dir + '\\memsnap_' + tag +
                                         '_r' + r + '.bin', rva(va), sz);
                            wrote++;
                        } catch (ex) { err('segtrace-memsnap', ex.message); }
                    }
                    log('segtrace: memsnap @' + resolved + ' -> ' + wrote + '/' +
                        g_memsnap_regions.length + ' region(s) in ' + g_capture_dir);
                }
            }
        }
        // Condition-gated rng-hook install (RE §21.2): when the LCG hook is
        // deferred (rng_hook_defer), arm it the FIRST frame the f406 first-customer
        // entry holds (cc08==4 && b51c==0 — the same gate as the bgnpc SoA dump
        // below). Pre-entry stays hook-free so the initial cad868 Continue-load
        // doesn't stretch (a boot hook taxes its rng burst ~3500f→~14161f,
        // mis-timing the esc-skip); from here g_rng_count_total is cumulative-FROM-
        // the-entry, exactly the bgnpc-rng log / cs_walker_drill alignment origin.
        // Arm BEFORE the SoA dump so this frame's draws are already counted.
        if (g_rng_hook_defer && g_rng_hook_wanted && !g_rng_callers_hooked) {
            try {
                if (rva(0x0438cc08).readS32() === 4 &&
                    rva(0x0730b51c).readS32() === 0) {
                    installRngCallerHook();
                    log('rng LCG hook ARMED at the f406 entry (frame ' + fn +
                        ', cc08==4 b51c==0) -- deferred install (RE 21.2)');
                }
            } catch (ex) { err('rng-hook-defer-arm', ex.message); }
        }
        // One-shot bg-NPC SoA dump for the {bgnpcpin} capture (RE §21.1): the
        // FIRST frame the f406 first-customer entry holds (cc08==4 && b51c==0 —
        // the cs_walker_drill alignment point), dump DAT_073a7f80 (6 x 0x64 =
        // 0x258 B) straight to disk via Win32.  Condition-gated (NOT segtrace-
        // gated), so the cross-target wrap-up anchor desync that stalls the
        // segment chain (DLG_LINE_CLEAR never fires on retail) can't suppress it;
        // the baked {bgnpcpin} then pins the PORT to this NATURAL layout.
        if (!g_bgnpc_soa_dumped && g_capture_dir) {
            try {
                if (rva(0x0438cc08).readS32() === 4 &&
                    rva(0x0730b51c).readS32() === 0) {
                    g_bgnpc_soa_dumped = true;
                    if (g_bgnpc_pin_soa) {
                        // BILATERAL {bgnpcpin} (RE §21.4): WRITE the canonical SoA into
                        // DAT_073a7f80 (150 u32 = 0x258 B, raw engine layout) so retail's
                        // window NPCs match the port's pin.  This frame is the f406 entry
                        // PRE-sim (segtraceTick = input_poll.onLeave, ahead of the bg_npc
                        // tick) so the write is effective at off0 — the same offset the
                        // port's CONV_POSE_END-segment pin lands at.  Both sides then drift
                        // in lockstep from the identical canonical (the {rngseed} pattern).
                        const base = rva(0x073a7f80);
                        for (let i = 0; i < g_bgnpc_pin_soa.length; i++)
                            base.add(i * 4).writeU32(g_bgnpc_pin_soa[i] >>> 0);
                        log('bgnpc: PINNED DAT_073a7f80 from canonical (' +
                            g_bgnpc_pin_soa.length + ' dwords, bilateral bgnpcpin) at frame '
                            + fn + ' (cc08==4 b51c==0)');
                    } else {
                        ensureWinFileFns();
                        writeRawFile(g_capture_dir + '\\bgnpc_soa.bin',
                                     rva(0x073a7f80), 0x258);
                        log('bgnpc: dumped DAT_073a7f80 SoA (0x258 B) at frame ' + fn +
                            ' (cc08==4 b51c==0) -> ' + g_capture_dir +
                            '\\bgnpc_soa.bin');
                    }
                }
            } catch (ex) { err('bgnpc-soa', ex.message); }
        }
        // One-shot furniture-layout GRID dump (rng-survey §21.4 ROOT 2): the
        // cs-walker retarget (FUN_0046fbee) rejection-samples DAT_074b28e8; its
        // CONTENT differs port<->retail, mis-counting the off-29..32 rng cluster.
        // Dump the grid (300 int32 → grid_dump.bin) + its five inputs
        // (tier/count/origins/mesh/rot) at the SAME f406 entry frame as the bgnpc
        // SoA, to diff against the port's grid_dump.{bin,json}.  Fires in BOTH
        // capture + bilateral-pin modes (the grid is save-derived, pin-neutral).
        if (g_bgnpc_soa_dumped && !g_grid_dumped && g_capture_dir) {
            try {
                g_grid_dumped = true;
                ensureWinFileFns();
                writeRawFile(g_capture_dir + '\\grid_dump.bin',
                             rva(0x074b28e8), 0x4b0);
                const rec = rva(0x0438b1e0).readS32();
                const stride = 0x2dfc8;             // 0xb7f2 dwords per record
                const tier = rva(0x04510578 + rec * stride).readS32();
                const count = rva(0x0438bfb4).readS32();
                const n = (count > 0 && count <= 10) ? count : 10;
                let origins = [], mesh = [], rot = [];
                for (let i = 0; i < n; i++) {
                    origins.push([rva(0x045105a8 + rec * stride + i * 8).readS32(),
                                  rva(0x045105ac + rec * stride + i * 8).readS32()]);
                    mesh.push(rva(0x0438bfcc + i * 4).readS32());
                    rot.push(rva(0x0438c01c + i * 4).readFloat());
                }
                log('grid: frame=' + fn + ' rec=' + rec + ' tier=' + tier +
                    ' count=' + count + ' mesh=' + JSON.stringify(mesh) +
                    ' rot=' + JSON.stringify(rot) +
                    ' origins=' + JSON.stringify(origins) + ' -> grid_dump.bin');
            } catch (ex) { err('grid-dump', ex.message); }
        }
        // rng-trajectory log for the bgnpcpin verification (companion to the SoA
        // dump): from the f406 entry onward, emit the cumulative LCG count
        // (g_rng_count_total, maintained by the --call-trace rng hook) each frame
        // for N frames, so the port↔retail rng STREAM can be diffed offset-for-
        // offset at the entry WITHOUT the (desync-lagged) calltrace window.
        if (g_bgnpc_soa_dumped && g_bgnpc_rng_log_n < 200) {
            try {
                log('bgnpc-rng: off=' + g_bgnpc_rng_log_n + ' frame=' + fn +
                    ' rng=' + (g_rng_count_total >>> 0) +
                    ' rngst=' + (rva(0x006023a0).readU32() >>> 0) +  // LCG STATE (§21.7)
                    ' gsim=' + rva(0x0438b8cc).readS32() +
                    ' cc08=' + rva(0x0438cc08).readS32() +
                    ' b51c=' + rva(0x0730b51c).readS32() +
                    ' b534=' + rva(0x0730b534).readS32());
                g_bgnpc_rng_log_n++;
            } catch (ex) { err('bgnpc-rng', ex.message); }
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

// Emit an anchor message carrying the absolute engine frame counter
// (var_frame_counter = DAT_073dfcfc) and the live LCG state
// (var_lcg_seed = DAT_006023a0). The recorder (frida_capture --record-trace)
// needs both to write the port-format raw row {anchor,frame,gframe,rng} so
// distill --anchor-segments can re-pin RNG per anchor — i.e. a retail-recorded
// trace replays deterministically the same way a port F2 recording does.
// Trace Studio v3 anchor-relative arm: on the FIRST firing of the configured
// anchor, call the staged capture-proxy's OrV3ArmWindowAt(frame+offset, count) so
// the proxy keeps the present-window [frame+offset, +count). offset>0 ⇒ armed well
// before the window starts (no race with the per-Present keep-check). In-process ⇒
// deterministic (no Python round-trip burning frames). Silent once-and-done no-op
// if the proxy isn't staged (export absent) ⇒ a normal v2 run is unaffected.
function v3ArmOnAnchor(name, frame) {
    if (!g_v3_arm || g_v3_arm_fired || name !== g_v3_arm.anchor) return;
    // Arm at the OCC-th firing (1-based). The anchor recurs (HOUSE_FREEROAM fires
    // once per house entry — iv1_1 then iv1_2), so a cutscene-sequence window must
    // skip the earlier firings and arm at the one the scenario's {wait}s land on,
    // else the retail capture grabs the WRONG cutscene (iv1_2 grabbing iv1_1's
    // bedroom — see opening-prologue.md). occ defaults to 1 ⇒ first firing as before.
    g_v3_arm_count += 1;
    if (g_v3_arm_count < g_v3_arm.occ) {
        log('v3_arm: ' + name + ' occ ' + g_v3_arm_count + '/' + g_v3_arm.occ +
            ' @frame ' + frame + ' — waiting for the armed occurrence');
        return;
    }
    if (!g_v3_arm_fn) {
        const m = Process.findModuleByName('d3d8.dll');
        const fn = m ? m.findExportByName('OrV3ArmWindowAt') : null;
        if (!fn) {
            log('v3_arm: OrV3ArmWindowAt not found (proxy d3d8.dll not staged?) — skipped');
            g_v3_arm_fired = true;   // don't re-probe every anchor
            return;
        }
        g_v3_arm_fn = new NativeFunction(fn, 'void', ['uint', 'uint'], 'stdcall');
    }
    const start = (frame + g_v3_arm.offset) >>> 0;
    g_v3_arm_fn(start, g_v3_arm.count);
    g_v3_arm_fired = true;
    log('v3_arm: ' + name + '+' + g_v3_arm.offset + ' @frame ' + frame +
        ' -> OrV3ArmWindowAt(' + start + ',' + g_v3_arm.count + ')');
    send({kind: 'v3_arm', anchor: name, frame: frame, start: start, count: g_v3_arm.count});
    // Window-aware early-exit (P2): the window's last kept present is start+count-1,
    // where the proxy finalizes the container. Schedule shutdown a couple frames past
    // it so the retail drive stops instead of over-running to max_frames; +2 covers any
    // 1-frame skew between the two Present clocks (cost: 2 frames vs ~13k of over-run).
    g_v3_shutdown_frame = (start + g_v3_arm.count + 2) >>> 0;
    log('v3_arm: early-exit scheduled @frame ' + g_v3_shutdown_frame);
}

function sendAnchor(name, frame) {
    let gframe = 0, rng = 0;
    try { gframe = rva(ADDR.var_frame_counter).readU32(); } catch (e) {}
    try { rng    = rva(ADDR.var_lcg_seed).readU32(); } catch (e) {}
    send({kind: 'anchor', anchor: name, frame: frame, gframe: gframe, rng: rng});
    v3ArmOnAnchor(name, frame);   // studio-v3: arm the capture proxy on its anchor
}

function anchorTick(frame, devicePtr) {
    // Snapshot the engine globals. loading_active = OR of both gates
    // (all.c L50058 `(DAT_06a49958==0) && (DAT_06a49960==0)`); reading
    // both keeps parity with the port's nowloading_is_active().
    const scene = rva(ADDR.var_scene_state).readS32();
    const loading = (rva(ADDR.var_nowloading_gate).readS32() !== 0) ||
                    (rva(ADDR.var_nowloading_gate2).readS32() !== 0);
    // Dialogue reveal state (only meaningful while dlgActive; stale otherwise,
    // so both text edges below are gated on the current-frame dlgActive).
    const dlgActive = rva(ADDR.var_dlg_active).readS32() === 1;
    const reveal    = rva(ADDR.var_dlg_reveal_ctr).readS32();
    const revflag   = rva(ADDR.var_dlg_revealed_flag).readS32();
    const fxAlpha   = anchorFxAlpha(dlgActive);
    // DAT_073a6a38 >= 0 — a dialogue line is shown (box open); < 0 between lines.
    const linePresent = dlgActive && rva(0x073a6a38).readS32() >= 0;
    // DAT_056daafc — player actor state (6 = iv1_2 conversation pose); when
    // posed, an odd anim-6 frame (DAT_056daaf8) is cell 39 = eyes closed (blink).
    const convState = rva(ADDR.var_player_state).readS32();
    const convBlink = (convState === 6) &&
                      (rva(ADDR.var_player_frame).readS32() === 1);
    // DAT_0438cc08 — in-game interaction state (4 = cc08==4 selling mode).
    const cc08 = rva(ADDR.var_cc08).readS32();
    // DAT_0438b150 != 0 — the in-game PAUSE menu is open.
    const pauseActive = rva(ADDR.var_pause_state).readS32() !== 0;
    // SAVE_PICKER_READY: the Save submenu (type 3) is open + navigable —
    // scene==9, sub_anim==10, Save selected. Mirror of pause_save_picker_navigable.
    let savePickerActive = false;
    let encyclopediaActive = false;
    let optionsActive = false;
    let itemsActive = false;
    if (scene === ANCHOR_SCENE_PAUSE &&
        rva(ADDR.var_pause_sub_anim).readS32() === 10) {
        const sel = rva(ADDR.var_pause_sel).readS32();
        if (sel >= 0 && sel < 8) {
            const t = rva(ADDR.var_pause_entries).add(sel * 4).readS32();
            savePickerActive   = (t === 3);
            encyclopediaActive = (t === 6);   // ENCYCLOPEDIA_READY
            optionsActive      = (t === 2);   // OPTIONS_READY
            itemsActive        = (t === 1);   // ITEMS_READY
        }
    }
    // TITLE_PICKER_READY: the title Continue/load picker (submenu_state 1) is
    // fully open + navigable — scene==0 (TITLE), submenu_state==1, cursor_anim==10
    // (the 0..10 fold-in tween at its cap). Mirror of scene_title_continue_picker
    // _navigable. No async load, so it fires picker-time-relative on both sides.
    const titleSubmenu = (scene === ANCHOR_SCENE_TITLE &&
                          rva(ADDR.var_title_cursor_anim).readS32() === 10)
                         ? rva(ADDR.var_title_submenu_state).readS32() : 0;
    const titlePickerActive   = (titleSubmenu === 1);   // TITLE_PICKER_READY
    const titleSettingsActive = (titleSubmenu === 2);   // TITLE_SETTINGS_READY
    const titleEncyclopediaActive = (titleSubmenu === 3); // TITLE_ENCYCLOPEDIA_READY
    const titleRecordsActive  = (titleSubmenu === 4);   // TITLE_RECORDS_READY
    // TITLE_SURVIVAL_READY: the Survival difficulty selector (code-6 overlay, NOT a
    // submenu_state) at rest — scene==0, submenu_state==0, cursor_anim==0,
    // survival_state==8. Mirror of scene_title_survival_navigable.
    const titleSurvivalActive =
        (scene === ANCHOR_SCENE_TITLE
         && rva(ADDR.var_title_cursor_anim).readS32() === 0
         && rva(ADDR.var_title_submenu_state).readS32() === 0
         && rva(ADDR.var_title_survival_state).readS32() === 8);

    if (!g_anchor_initialized) {
        g_anchor_initialized  = true;
        g_anchor_prev_scene   = scene;
        g_anchor_prev_loading = loading;
        g_anchor_prev_reveal  = reveal;
        g_anchor_prev_revflag = revflag;
        g_anchor_prev_fxalpha = fxAlpha;
        g_anchor_prev_linep   = linePresent;
        g_anchor_prev_convstate = convState;
        g_anchor_prev_convblink = convBlink;
        g_anchor_prev_cc08    = cc08;
        g_anchor_prev_pause   = pauseActive;
        g_anchor_prev_savepicker = savePickerActive;
        g_anchor_prev_encyclopedia = encyclopediaActive;
        g_anchor_prev_options = optionsActive;
        g_anchor_prev_items = itemsActive;
        g_anchor_prev_titlepicker = titlePickerActive;
        g_anchor_prev_titlesettings = titleSettingsActive;
        g_anchor_prev_titleencyclopedia = titleEncyclopediaActive;
        g_anchor_prev_titlerecords = titleRecordsActive;
        g_anchor_prev_titlesurvival = titleSurvivalActive;
        sendAnchor('BOOT', frame);
        anchorCaptureSchedule('BOOT', frame, devicePtr);
        return;
    }

    const ps = g_anchor_prev_scene, pl = g_anchor_prev_loading;
    const pr = g_anchor_prev_reveal, pf = g_anchor_prev_revflag;
    const px = g_anchor_prev_fxalpha;
    const plp = g_anchor_prev_linep;

    // Table order = emission order when several fire on one frame; matches
    // anchor_trace.c's g_anchors[] (causal: NEW_GAME / LOADING_START before
    // LOADING_END / HOUSE_FREEROAM).
    // NEW_GAME — TITLE → INGAME (the engine passes through LOADING in the
    // same tick, so the observable edge is TITLE → INGAME directly).
    if (ps === ANCHOR_SCENE_TITLE && scene === ANCHOR_SCENE_INGAME) {
        sendAnchor('NEW_GAME', frame);
        anchorCaptureSchedule('NEW_GAME', frame, devicePtr);
    }
    if (!pl && loading) {
        sendAnchor('LOADING_START', frame);
        anchorCaptureSchedule('LOADING_START', frame, devicePtr);
    }
    if (pl && !loading) {
        sendAnchor('LOADING_END', frame);
        anchorCaptureSchedule('LOADING_END', frame, devicePtr);
    }
    if (!anchorIsHouseFreeroam(ps, pl) &&
        anchorIsHouseFreeroam(scene, loading)) {
        sendAnchor('HOUSE_FREEROAM', frame);
        anchorCaptureSchedule('HOUSE_FREEROAM', frame, devicePtr);
    }
    // MARKET_ENTER — the Market scene (mode 6: Merchant's Guild + ichiba variant)
    // became active, i.e. the worldmap->guild scene LOAD just finished. A SPECIFIC
    // sync point for that non-deterministic-length load (the generic LOADING_END
    // fires 4+ times and the guild one's length differs port~8f/retail~88f, so it
    // can't unambiguously rebase the post-load menu). Mirrors anchor_trace.c
    // ev_market_enter. (scene is the raw DAT_0438b1c0; 6 == Market.)
    if (ps !== ANCHOR_SCENE_MARKET && scene === ANCHOR_SCENE_MARKET) {
        sendAnchor('MARKET_ENTER', frame);
        anchorCaptureSchedule('MARKET_ENTER', frame, devicePtr);
    }
    // CUSTOMER_SERVICE_ENTER — the cc08==4 in-shop customer-service / price-haggle
    // SELLING mode became active (DAT_0438cc08 non-4 -> 4; the player Z'd at the
    // sell counter). The unambiguous haggle-window sync point: a {wait:LOADING_END}
    // resolves to a DIFFERENT physical load per side (Continue-load vs the cc08==4
    // d3e asset load), so a caprange rebased on it opens at a different cc08-offset
    // on each side. Mirrors anchor_trace.c ev_customer_service_enter.
    if (g_anchor_prev_cc08 !== 4 && cc08 === 4) {
        sendAnchor('CUSTOMER_SERVICE_ENTER', frame);
        anchorCaptureSchedule('CUSTOMER_SERVICE_ENTER', frame, devicePtr);
    }
    // TEXT_ANIM_START — a new dialogue line begins its typewriter reveal: the
    // reveal counter is forced to 1 (DAT_073a3e00, 0x46c9a2). It only equals 1
    // on the new-line frame (init=0, then climbs 1,2,…0x800), so reveal==1 with
    // prev!=1 is the exact per-line rising edge (fires the first line too:
    // 0->1). Mirrors anchor_trace.c ev_text_anim_start.
    if (dlgActive && reveal === 1 && pr !== 1) {
        sendAnchor('TEXT_ANIM_START', frame);
        anchorCaptureSchedule('TEXT_ANIM_START', frame, devicePtr);
    }
    // TEXT_ANIM_END — the line finished scrolling and settled: the
    // fully-revealed flag rises 0->1 (DAT_073a3e04, 0x46c9a2). This is the
    // deterministic "text animation done, awaiting advance" edge.
    if (dlgActive && revflag !== 0 && pf === 0) {
        sendAnchor('TEXT_ANIM_END', frame);
        anchorCaptureSchedule('TEXT_ANIM_END', frame, devicePtr);
    }
    // EXTRA_SPRITE_* — the dialogue effect-sprite (sigh / zzz / sweat / kuro
    // fade) lifecycle off the aggregate fx_alpha. 1:1 mirror of anchor_trace.c
    // ev_fx_*; START/FADED_IN coincide for an instant-appear sprite.
    if (px === 0 && fxAlpha > 0) {
        sendAnchor('EXTRA_SPRITE_START', frame);
        anchorCaptureSchedule('EXTRA_SPRITE_START', frame, devicePtr);
    }
    if (fxAlpha >= 255 && px < 255) {
        sendAnchor('EXTRA_SPRITE_FADED_IN', frame);
        anchorCaptureSchedule('EXTRA_SPRITE_FADED_IN', frame, devicePtr);
    }
    if (px >= 255 && fxAlpha < 255 && fxAlpha > 0) {
        sendAnchor('EXTRA_SPRITE_FADEOUT', frame);
        anchorCaptureSchedule('EXTRA_SPRITE_FADEOUT', frame, devicePtr);
    }
    if (px > 0 && fxAlpha === 0) {
        sendAnchor('EXTRA_SPRITE_END', frame);
        anchorCaptureSchedule('EXTRA_SPRITE_END', frame, devicePtr);
    }
    // DLG_LINE_CLEAR / DLG_LINE_SHOW — line dismissed (box closing) / next line
    // shown. Frame the between-lines gap. Mirror of anchor_trace.c ev_dlg_line_*.
    if (plp && !linePresent) {
        sendAnchor('DLG_LINE_CLEAR', frame);
        anchorCaptureSchedule('DLG_LINE_CLEAR', frame, devicePtr);
    }
    if (!plp && linePresent) {
        sendAnchor('DLG_LINE_SHOW', frame);
        anchorCaptureSchedule('DLG_LINE_SHOW', frame, devicePtr);
    }
    // CONV_POSE_START / CONV_POSE_END — the player actor state rises to / falls
    // from 6 (the iv1_2 listen pose). The blink resets on this edge, so it is
    // the per-effect anchor for phase-aligned captures (§85/§86). Mirror of
    // anchor_trace.c ev_conv_pose_*.
    if (g_anchor_prev_convstate !== 6 && convState === 6) {
        sendAnchor('CONV_POSE_START', frame);
        anchorCaptureSchedule('CONV_POSE_START', frame, devicePtr);
    }
    if (g_anchor_prev_convstate === 6 && convState !== 6) {
        sendAnchor('CONV_POSE_END', frame);
        anchorCaptureSchedule('CONV_POSE_END', frame, devicePtr);
    }
    // CONV_POSE_BLINK — eyes close (cell 39) during the pose. The post-load sync
    // point for blink-phase comparison. Mirror of anchor_trace.c ev_conv_pose_blink.
    if (!g_anchor_prev_convblink && convBlink) {
        sendAnchor('CONV_POSE_BLINK', frame);
        anchorCaptureSchedule('CONV_POSE_BLINK', frame, devicePtr);
    }
    // PAUSE_OPEN / PAUSE_CLOSE — the in-game pause menu opens/closes (DAT_0438b150
    // 0->1 / 1->0). Anchors the save/quit-to-title navigation. Mirror of
    // anchor_trace.c ev_pause_open / ev_pause_close.
    if (!g_anchor_prev_pause && pauseActive) {
        sendAnchor('PAUSE_OPEN', frame);
        anchorCaptureSchedule('PAUSE_OPEN', frame, devicePtr);
    }
    if (g_anchor_prev_pause && !pauseActive) {
        sendAnchor('PAUSE_CLOSE', frame);
        anchorCaptureSchedule('PAUSE_CLOSE', frame, devicePtr);
    }
    // PAUSE_READY — the pause menu's async asset load (FUN_00473a3e) finished
    // while paused (scene mode 9): the menu is now navigable. The load-end edge
    // GATED on scene==9 (so it never fires on the ramp==3 frame). The robust
    // rebase point for pause-SUBMENU nav: PAUSE_OPEN fires pre-load + only on the
    // port (retail's pause doesn't set b150), and the pause load stretches a
    // different frame count per side. Mirror of anchor_trace.c ev_pause_ready.
    if (pl && !loading && scene === ANCHOR_SCENE_PAUSE) {
        sendAnchor('PAUSE_READY', frame);
        anchorCaptureSchedule('PAUSE_READY', frame, devicePtr);
    }
    // SAVE_PICKER_READY — the pause SAVE submenu just became navigable (rising
    // edge of savePickerActive). Rebases save-picker nav to a picker-time-
    // relative sync so the PAUSE_READY open-ramp phase (per-side-variable, async
    // load) doesn't beat the selected-card breathing. Mirror of
    // anchor_trace.c ev_save_picker_ready.
    if (!g_anchor_prev_savepicker && savePickerActive) {
        sendAnchor('SAVE_PICKER_READY', frame);
        anchorCaptureSchedule('SAVE_PICKER_READY', frame, devicePtr);
    }
    // ENCYCLOPEDIA_READY — the pause Encyclopedia submenu just became navigable
    // (rising edge). Same per-side pause-load rebase as SAVE_PICKER_READY.
    if (!g_anchor_prev_encyclopedia && encyclopediaActive) {
        sendAnchor('ENCYCLOPEDIA_READY', frame);
        anchorCaptureSchedule('ENCYCLOPEDIA_READY', frame, devicePtr);
    }
    // OPTIONS_READY — the pause Options submenu just became navigable (rising
    // edge). Same per-side pause-load rebase; the robust v3 join anchor for
    // Options traces (fires AFTER PAUSE_OPEN on both sides). Mirror of
    // anchor_trace.c ev_options_ready.
    if (!g_anchor_prev_options && optionsActive) {
        sendAnchor('OPTIONS_READY', frame);
        anchorCaptureSchedule('OPTIONS_READY', frame, devicePtr);
    }
    // ITEMS_READY — the pause Items submenu just became navigable (rising edge).
    // Same per-side pause-load rebase; the robust v3 join anchor for Items traces
    // (fires AFTER PAUSE_OPEN on both sides). Mirror of anchor_trace.c
    // ev_items_ready.
    if (!g_anchor_prev_items && itemsActive) {
        sendAnchor('ITEMS_READY', frame);
        anchorCaptureSchedule('ITEMS_READY', frame, devicePtr);
    }
    // TITLE_RETURN — quit to the title/main menu (rising edge of scene == TITLE
    // from any non-TITLE state; the quit-to-title passes through LOADING, so a
    // strict INGAME→TITLE edge misses it). Anchors the title-menu load-slot
    // navigation. Mirror of anchor_trace.c ev_title_return.
    if (ps !== ANCHOR_SCENE_TITLE && scene === ANCHOR_SCENE_TITLE) {
        sendAnchor('TITLE_RETURN', frame);
        anchorCaptureSchedule('TITLE_RETURN', frame, devicePtr);
    }
    // TITLE_PICKER_READY — the title Continue/load picker just became navigable
    // (rising edge). No async load (unlike the pause *_READY anchors), so it's a
    // clean v3 join anchor for the title picker render (FUN_0049b556) — fires at
    // the same picker-relative frame on both sides. Mirror of anchor_trace.c
    // ev_title_picker_ready.
    if (!g_anchor_prev_titlepicker && titlePickerActive) {
        sendAnchor('TITLE_PICKER_READY', frame);
        anchorCaptureSchedule('TITLE_PICKER_READY', frame, devicePtr);
    }
    // TITLE_SETTINGS_READY — the title Options/settings submenu just became
    // navigable (rising edge). Like the picker, no async load ⇒ a clean v3 join
    // for the title settings render (FUN_0049c050). Mirror of
    // anchor_trace.c ev_title_settings_ready.
    if (!g_anchor_prev_titlesettings && titleSettingsActive) {
        sendAnchor('TITLE_SETTINGS_READY', frame);
        anchorCaptureSchedule('TITLE_SETTINGS_READY', frame, devicePtr);
    }
    // TITLE_ENCYCLOPEDIA_READY — the title all-banks 図鑑 (submenu_state 3) just
    // became navigable (rising edge). No async load ⇒ a clean v3 join for the
    // title encyclopedia render (FUN_0049f8b8). Mirror of anchor_trace.c
    // ev_title_encyclopedia_ready.
    if (!g_anchor_prev_titleencyclopedia && titleEncyclopediaActive) {
        sendAnchor('TITLE_ENCYCLOPEDIA_READY', frame);
        anchorCaptureSchedule('TITLE_ENCYCLOPEDIA_READY', frame, devicePtr);
    }
    // TITLE_RECORDS_READY — the title Records / high-score screen (submenu_state
    // 4) just became navigable (rising edge). No async load ⇒ a clean v3 join for
    // the title records render (FUN_0049c439). Mirror of anchor_trace.c
    // ev_title_records_ready.
    if (!g_anchor_prev_titlerecords && titleRecordsActive) {
        sendAnchor('TITLE_RECORDS_READY', frame);
        anchorCaptureSchedule('TITLE_RECORDS_READY', frame, devicePtr);
    }
    // TITLE_SURVIVAL_READY — the Survival difficulty selector (code-6 overlay) just
    // reached its at-rest open state (survival_state==8, rising edge). No async load
    // ⇒ a clean +0-stretch v3 join for the selector render (FUN_0049c644 @ 0x49cbe8).
    // Mirror of anchor_trace.c ev_title_survival_ready.
    if (!g_anchor_prev_titlesurvival && titleSurvivalActive) {
        sendAnchor('TITLE_SURVIVAL_READY', frame);
        anchorCaptureSchedule('TITLE_SURVIVAL_READY', frame, devicePtr);
    }

    g_anchor_prev_scene   = scene;
    g_anchor_prev_loading = loading;
    g_anchor_prev_reveal  = reveal;
    g_anchor_prev_revflag = revflag;
    g_anchor_prev_fxalpha = fxAlpha;
    g_anchor_prev_linep   = linePresent;
    g_anchor_prev_convstate = convState;
    g_anchor_prev_convblink = convBlink;
    g_anchor_prev_cc08    = cc08;
    g_anchor_prev_pause   = pauseActive;
    g_anchor_prev_savepicker = savePickerActive;
    g_anchor_prev_encyclopedia = encyclopediaActive;
    g_anchor_prev_options = optionsActive;
    g_anchor_prev_items = itemsActive;
    g_anchor_prev_titlepicker = titlePickerActive;
    g_anchor_prev_titlesettings = titleSettingsActive;
    g_anchor_prev_titleencyclopedia = titleEncyclopediaActive;
    g_anchor_prev_titlerecords = titleRecordsActive;
    g_anchor_prev_titlesurvival = titleSurvivalActive;
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

// Tally one LCG caller VA into the current frame's call-site bucket, if the
// live frame is inside the armed [lo,hi) range. Reads the frame counter only
// while armed (cheap when off).
function rngCsTally(k) {
    if (g_rng_cs_hi < 0) return;
    const f = g_manual_frame_counter;   // the real sim frame (== fn in input_poll)
    if (f < g_rng_cs_lo || f >= g_rng_cs_hi) return;
    let m = g_rng_cs_buf[f];
    if (!m) { m = {}; g_rng_cs_buf[f] = m; }
    m[k] = (m[k] || 0) + 1;
}

// ─── RNG caller histogram / consumption probe ───────────────────────────────
function installRngCallerHook() {
    if (g_rng_callers_hooked) return;
    ensureBase();
    // The int LCG: direct callers (rng_next15 via the 0x471084 jmp-thunk, or
    // direct calls) show their real VA. Float callers via FUN_00471089 all
    // collapse to the wrapper VA 0x471092 — broken open by the 2nd hook below.
    Interceptor.attach(rva(FN_RNG_LCG), {
        onEnter: function () {
            g_rng_count_total++;
            try {
                const k = '0x' + toGhidraVa(this.returnAddress).toString(16);
                if (g_rng_callers) g_rng_callers_map[k] = (g_rng_callers_map[k] || 0) + 1;
                rngCsTally(k);
            } catch (e) { /* never break the engine over a tally */ }
        },
    });
    // FUN_00471089 (rng_next_unit, the float variant): record its real caller
    // under a 'u:' prefix so the float consumers (dust, wing-sparkle, motes,
    // spawn jitter) are distinguishable from the collapsed 0x471092 bucket.
    Interceptor.attach(rva(0x00471089), {
        onEnter: function () {
            try {
                const k = 'u:0x' + toGhidraVa(this.returnAddress).toString(16);
                if (g_rng_callers) g_rng_callers_map[k] = (g_rng_callers_map[k] || 0) + 1;
                rngCsTally(k);
            } catch (e) { /* tolerate */ }
        },
    });
    g_rng_callers_hooked = true;
    log('rng LCG hook installed on FUN_005041f6 + FUN_00471089 '
        + '(callers=' + g_rng_callers + ' count=' + g_rng_count
        + ' callsites=[' + g_rng_cs_lo + ',' + g_rng_cs_hi + '))');
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

// Read one declared field (flow-trace payload) per tools/flow/retail_fields.json.
// `args` is the Frida onEnter args array — standard 0-based InvocationArguments,
// so args[0] is the FIRST cdecl param (spec `index` is 0-based; index 0 = first
// param).  Returns null on any fault so a transiently-bad address never wedges
// the trace.
function flowReadTyped(ptr, type) {
    switch (type) {
        case 'u32': return ptr.readU32();
        case 'f32': return ptr.readFloat();
        case 'hex': return '0x' + ptr.readU32().toString(16);
        case 'i32':
        default:    return ptr.readS32();
    }
}

function flowReadField(args, f) {
    try {
        if (f.src === 'global') {
            return flowReadTyped(rva(f.va | 0), f.type);
        }
        if (f.src === 'rngcalls') {
            // cumulative LCG-draw count (the determinism signal flow_diff
            // --verdict classifies). The LCG hook (installRngCallerHook) keeps
            // g_rng_count_total; it is installed whenever a call-trace field
            // declares src:'rngcalls' (see the setup gate).
            return g_rng_count_total;
        }
        if (f.src === 'argderef') {
            return flowReadTyped(args[f.index | 0].add(f.off | 0), f.type);
        }
        if (f.src === 'arg') {
            const a = args[f.index | 0];
            if (f.type === 'f32') {
                // Reinterpret the 32-bit stack slot's bits as a float.
                const b = Memory.alloc(4);
                b.writeU32(a.toUInt32());
                return b.readFloat();
            }
            if (f.type === 'u32') return a.toUInt32();
            if (f.type === 'hex') return '0x' + a.toUInt32().toString(16);
            return a.toInt32();
        }
        if (f.src === 'records_a_dust') {
            // Aggregate over the live records-A table (DAT_069b2f80, stride 0x94,
            // TYPE@+0x30, AGE@+0x34, POS@+0, VEL@+0xc) for the foot-dust type 0xe.
            // Mirrors src/scene1_player_ctrl.c's dustn/dustage/dusts*/dustv* probe
            // so flow_diff names exactly which dust field diverges (vel = spawn
            // RNG values; pos = emit+drift; age = timing; n = spawn/kill).
            const base = rva(ADDR.var_records_a_base);
            let cnt = rva(ADDR.var_records_a_count).readS32();
            if (cnt > RECORD_A_SLOTS) cnt = RECORD_A_SLOTS;
            if (cnt < 0) cnt = 0;
            const STRIDE = RECORD_A_STRIDE_DW * 4;
            let n = 0, age = 0, sx = 0, sz = 0, svx = 0, svz = 0;
            for (let s = 0; s < cnt; s++) {
                const slot = base.add(s * STRIDE);
                if (slot.add(12 * 4).readS32() !== 0xe) continue;
                n++;
                age += slot.add(13 * 4).readS32();
                sx  += slot.add(0).readFloat();
                sz  += slot.add(2 * 4).readFloat();
                svx += slot.add(3 * 4).readFloat();
                svz += slot.add(5 * 4).readFloat();
            }
            switch (f.agg) {
                case 'n':   return n;
                case 'age': return age;
                case 'sx':  return sx;
                case 'sz':  return sz;
                case 'svx': return svx;
                case 'svz': return svz;
                default:    return 0;
            }
        }
    } catch (_) {
        return null;
    }
    return null;   // retval handled onLeave (later increment)
}

// Per-frame execution-order seq for the retail call buffer — mirrors the port's
// g_seq so flow_diff.py aligns the chain. Reset when the frame advances.
function ctNextSeq() {
    if (g_manual_frame_counter !== g_ct_seq_frame) {
        g_ct_seq = 0;
        g_ct_seq_frame = g_manual_frame_counter;
    }
    return g_ct_seq++;
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
                onEnter: function (args) {
                    if (!callTraceShouldEmit()) return;
                    const rec = {
                        va:     va,
                        ret_va: traceRetVa(this.returnAddress),
                        seq:    ctNextSeq(),
                        ts:     nowMs(),
                        thr:    this.threadId,
                    };
                    const spec = g_call_trace_fields[va];
                    if (spec) {
                        const f = {};
                        for (let k = 0; k < spec.length; k++) {
                            f[spec[k].name] = flowReadField(args, spec[k]);
                        }
                        rec.f = f;
                    }
                    g_call_trace_buffer.push(rec);
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
    // The unpacked dump (spawn path) is recettear.unpacked.exe; the real
    // Steam-launched retail (attach/record path) is recettear.exe — SteamStub
    // decrypts the original .text in place at the same VAs the dump was taken
    // from, so the offsets resolve identically against either module base.
    // Try the dump name, then the packed name, then the process main module.
    let mod = Process.findModuleByName(MODULE_NAME) ||
              Process.findModuleByName('recettear.exe');
    if (!mod) {
        try { mod = Process.mainModule; } catch (e) {}
    }
    if (!mod) throw new Error('module not found: ' + MODULE_NAME +
                              ' / recettear.exe / mainModule');
    g_base = mod.base;
    g_module_resolved = mod.name;
}
let g_module_resolved = MODULE_NAME;

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

// ─── live-probe layer (tools/probe_daemon.py) ────────────────────────────
//
// The persistent-session substrate: a probe daemon spawns retail ONCE, keeps
// this agent alive, and drives the game interactively — synthetic input via
// the same var_input_mask write path the TAS uses (identical code path to a
// real player's DInput poll), on-demand screenshots, typed memory reads/pokes,
// and engine-thread function calls. Mirrors OpenLords2's probe layer; the
// input story is simpler here (button mask, no mouse).
//
// Ownership model: while g_probe_active the probe queue OWNS the input mask
// (real keyboard/pad is overwritten post-poll = interactive input LOCKED).
// probeActivate(false) hands the game back to the human. The daemon
// bootstraps via an input_segtrace when it wants a known state first — probe
// stays inactive until the segtrace is done, then the daemon flips it on.

let g_probe_mode         = false;  // init flag: probe niceties (dedup input_state, clock stub)
let g_probe_active       = false;  // probe owns the input mask (input lock)
let g_probe_force_active = false;  // re-assert var_pause_flag=1 every poll
let g_probe_hold         = 0;      // sticky held mask (walking etc.)
let g_probe_queue        = [];     // [{mask, n}] — n input-polls each, FIFO
let g_probe_shot         = 0;      // capture at the next N Presents
let g_probe_stream_every = 0;      // >0: capture every Nth Present (recording)
let g_probe_calls        = [];     // queued engine-thread calls
let g_probe_call_id      = 0;
let g_probe_last_input_sent = -1;  // input_state change-point dedup
let g_show_noactivate    = false;  // preview window: show without activation

// Probe clock: fn_clock_ms is ALWAYS replaced in probe mode so turbo can
// toggle at runtime. Continuity offsets keep the returned u32 ms monotonic
// across toggles (a backwards clock jump would confuse the engine's frame
// pacing; forward jumps read as one long frame — harmless).
let g_probe_clock_epoch  = 0;      // Date.now() at install (real-ms base)
let g_probe_clock_toff   = 0;      // turbo-mode continuity offset
let g_probe_clock_roff   = 0;      // real-mode continuity offset
let g_probe_clock_last   = 0;      // last value handed to the engine

function installProbeClockHooks() {
    g_probe_clock_epoch = Date.now();
    g_turbo_clock_cb = new NativeCallback(function () {
        let v;
        if (g_turbo_enabled) {
            v = (g_virtual_now_ms + g_probe_clock_toff) >>> 0;
        } else {
            v = ((Date.now() - g_probe_clock_epoch) + g_probe_clock_roff) >>> 0;
        }
        g_probe_clock_last = v;
        return v;
    }, 'uint32', []);
    Interceptor.replace(rva(ADDR.fn_clock_ms), g_turbo_clock_cb);
    Interceptor.attach(rva(ADDR.fn_tick), {
        onEnter: function () {
            g_virtual_now_ms = (g_virtual_now_ms + g_turbo_step_ms) >>> 0;
        },
    });
    log('probe clock installed (turbo=' + g_turbo_enabled +
        ', step_ms=' + g_turbo_step_ms + ')');
}

function probeSetTurboImpl(on) {
    on = !!on;
    if (on === g_turbo_enabled) return g_turbo_enabled;
    if (on) {
        g_probe_clock_toff =
            (g_probe_clock_last - g_virtual_now_ms) >>> 0;
    } else {
        g_probe_clock_roff =
            (g_probe_clock_last - (Date.now() - g_probe_clock_epoch)) >>> 0;
    }
    g_turbo_enabled = on;
    log('probe: turbo ' + (on ? 'ON' : 'OFF') +
        ' (clock continuity @ ' + g_probe_clock_last + 'ms)');
    return g_turbo_enabled;
}

// One input-poll tick of probe input: front of the tap queue OR'd over the
// sticky hold. Timing is frame-exact (one queue step per engine input poll).
function probeMaskTick() {
    let m = g_probe_hold & 0xffff;
    if (g_probe_queue.length > 0) {
        const e = g_probe_queue[0];
        m |= (e.mask & 0xffff);
        if (--e.n <= 0) g_probe_queue.shift();
    }
    return m;
}

// Run every queued engine call at the pre-sim input_poll point (engine
// thread — the ONLY safe place to call sim-touching engine functions; a
// call from the frida RPC thread would race the running sim). Results go
// back as {kind:'call_result'} messages keyed by id.
function probeRunQueuedCalls() {
    while (g_probe_calls.length > 0) {
        const c = g_probe_calls.shift();
        let ret = null, error = null, seedAtCall = null, seedAfterCall = null;
        try {
            const argt = c.argt || [];
            const fn = new NativeFunction(rva(c.va), c.ret || 'int32',
                                          argt, c.abi || 'mscdecl');
            const argv = (c.args || []).map(function (a, i) {
                return (argt[i] === 'pointer') ? ptr(a) : a;
            });
            // Snapshot the RNG state (DAT_006023a0) on the ENGINE thread the
            // instant before AND after the call — the exact seed window the
            // callee consumed, free of the RPC-thread→engine-thread drift a
            // client-side read has (the before-read cracked the roster-scan
            // golden; the after-read cracked the NEWS golden — a client-side
            // "final seed" read races the resumed sim and over-counts draws).
            try { seedAtCall = rva(0x006023a0).readU32(); } catch (e) {}
            ret = fn.apply(null, argv);
            try { seedAfterCall = rva(0x006023a0).readU32(); } catch (e) {}
            if (ret instanceof NativePointer) ret = ret.toString();
        } catch (e) {
            error = e.message;
        }
        send({kind: 'call_result', id: c.id, ret: ret, err: error,
              seed_at_call: seedAtCall, seed_after_call: seedAfterCall,
              frame: frameNo()});
    }
}

// Typed single-value read at a Ghidra VA (probe daemon's read/reads/state).
function probeReadTyped(va, type) {
    const p = rva(va);
    switch (type) {
        case 'u8':  return p.readU8();
        case 'i8':  return p.readS8();
        case 'u16': return p.readU16();
        case 'i16': return p.readS16();
        case 'u32': return p.readU32();
        case 'f32': return p.readFloat();
        case 'f64': return p.readDouble();
        case 'ptr': return p.readPointer().toString();
        default:    return p.readS32();   // 'i32'/'s32'
    }
}

function probeWriteTyped(va, type, val) {
    const p = rva(va);
    switch (type) {
        case 'u8':  p.writeU8(val & 0xff); break;
        case 'i8':  p.writeS8(val | 0); break;
        case 'u16': p.writeU16(val & 0xffff); break;
        case 'i16': p.writeS16(val | 0); break;
        case 'u32': p.writeU32(val >>> 0); break;
        case 'f32': p.writeFloat(+val); break;
        case 'f64': p.writeDouble(+val); break;
        default:    p.writeS32(val | 0); break;
    }
}

rpc.exports = {
    init: function (config) {
        config = config || {};
        if (Array.isArray(config.capture_frames)) {
            for (const f of config.capture_frames) g_capture_pending.add(f);
        }
        g_capture_all = !!config.capture_all;
        g_capture_stride = (config.capture_stride | 0) > 0 ? (config.capture_stride | 0) : 1;
        g_suppress_loads = !!config.suppress_loads;
        g_capture_dir = (typeof config.capture_dir === 'string'
                         && config.capture_dir) ? config.capture_dir : null;
        g_memsnap_regions = Array.isArray(config.memsnap_regions)
                            ? config.memsnap_regions : [];
        // Bilateral {bgnpcpin}: the canonical SoA to write at the f406 entry (RE §21.4).
        g_bgnpc_pin_soa = (Array.isArray(config.bgnpc_pin_soa)
                           && config.bgnpc_pin_soa.length) ? config.bgnpc_pin_soa : null;
        g_bgnpc_soa_dumped = false;
        g_wrapup_box_was_open = false;   // RE §21.5: re-arm the skip-box latch each run
        g_max_frames  = config.max_frames | 0;
        g_save_sandbox = (typeof config.save_sandbox === 'string'
                          && config.save_sandbox) ? config.save_sandbox : null;
        g_capture_saves      = !!config.capture_saves;
        g_save_boot_captured = false;
        g_save_write_idx     = 0;

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
        g_segtrace_base_anchor = null;
        g_segtrace_sticky   = 0;
        g_segtrace_fired    = {};
        g_ct_windows        = [];
        g_ct_window_mode    = false;
        g_segtrace_tutloadpin = 0;   // set per-segment by segtraceOnSegmentEnter
                                     // (segment 0's {tutloadpin}, before install)
        g_tlp_armed         = false;
        g_tlp_prev_b1c8     = 0;
        g_segtrace_csloadpin = 0;    // re-set by a {csloadpin} op below
        g_csl_armed         = false;
        g_csl_prev_b1cc     = 0;
        g_segtrace_primaryloadpin = 0;  // re-set by a {primaryloadpin} op below
        g_plp_armed         = false;
        g_plp_prev_busy     = 0;
        g_segtrace_bgnpcseed_active = false;  // re-set by a {bgnpcseed} op below
        g_segtrace_bgnpcseed = 0;
        g_segtrace_bgnpcseed_cursor = 0;
        g_segtrace_bgnpcseed_dead = [];
        g_bgnpcseed_applied = false;
        if (Array.isArray(config.input_segtrace) &&
            config.input_segtrace.length > 0) {
            g_segtrace_segments = segtraceBuildSegments(config.input_segtrace);
            g_segtrace_active = true;
            // Window mode: if any segment declares a calltrace op, the call
            // tracer emits ONLY inside armed anchor-relative windows.
            g_ct_window_mode = g_segtrace_segments.some(
                function (s) { return s.calltraces.length > 0; });
            // ({tutloadpin:N} in the ops attaches its worker-tail blocker in
            // the install-hooks block below — it needs rva()/the resolved
            // module base, which isn't available at parse time.)
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

        g_arm_skip_at_frame   = (config.arm_skip_at_frame === undefined)
                                ? -1 : (config.arm_skip_at_frame | 0);

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

        // Live-probe mode (tools/probe_daemon.py). probe_mode arms the
        // runtime-toggleable clock stub + input_state dedup + the always-on
        // silent-audio clamp hook (conditional, so audio can toggle live).
        // probe_active decides who owns the input mask from boot; the
        // daemon usually passes true unless bootstrapping via a segtrace.
        g_probe_mode         = !!config.probe_mode;
        g_probe_active       = !!config.probe_active;
        g_probe_force_active = !!config.force_active;
        g_show_noactivate    = !!config.show_window_noactivate;
        g_probe_hold = 0; g_probe_queue = []; g_probe_shot = 0;
        g_probe_stream_every = 0; g_probe_calls = [];
        g_probe_last_input_sent = -1;

        // FPS overlay: hidden by default for clean comparisons; show_fps
        // re-enables it (matches the port's --show-fps / capture-default-hide).
        g_hide_fps = (config.show_fps !== true);

        // force_resolution: [w, h] or null. When set, hook recet.ini
        // parse exit and overwrite the engine's screen-size globals.
        g_force_resolution_w = 0;
        g_force_resolution_h = 0;
        if (Array.isArray(config.force_resolution) &&
            config.force_resolution.length === 2) {
            g_force_resolution_w = config.force_resolution[0] | 0;
            g_force_resolution_h = config.force_resolution[1] | 0;
        }

        // rng_seed: pin DAT_006023a0 to this value (mirrors the port's
        // --rng-seed) so RNG-driven positions are comparable across targets.
        g_rng_seed = (config.rng_seed === null || config.rng_seed === undefined)
                   ? null : (config.rng_seed >>> 0);

        // D3D state-trace (Phase D.4). When d3d_trace:true, the agent
        // installs vtable hooks on Direct3DDevice8 methods as soon as
        // the device pointer is live (driven by the d3d_init_wrapper
        // hook in installInitHook). d3d_trace_frames is an optional
        // list of frame numbers — when set, only those frames have
        // their events buffered + flushed (use this to keep output
        // small for render-heavy scenarios; INGAME frames can run 1000+
        // state-change calls each). Null = unfiltered (every frame).
        g_d3d_trace_enabled = !!config.d3d_trace;
        g_d3d_trace_verts   = !!config.d3d_trace_verts;
        g_d3d_trace_hooked  = false;
        g_d3d_trace_buffer  = [];
        g_tex_names         = {};
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
        // Flow-trace declared payloads (tools/flow/retail_fields.json, keyed by
        // engine VA). Normalise the va keys to ints so the onEnter lookup
        // (g_call_trace_fields[va]) hits.
        g_call_trace_fields = {};
        g_ct_seq = 0;
        g_ct_seq_frame = -1;
        if (config.call_trace_fields && typeof config.call_trace_fields === 'object') {
            const src = config.call_trace_fields;
            for (const k in src) {
                if (Object.prototype.hasOwnProperty.call(src, k)) {
                    g_call_trace_fields[(k | 0) || parseInt(k, 0)] = src[k];
                }
            }
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
        g_record_esc           = !!config.record_esc;
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

        // Trace Studio v3 — anchor-relative capture-proxy arm (opt-in; a silent
        // no-op without the staged proxy d3d8.dll, so v2 captures are unaffected).
        // Like capture_at_anchor it needs the anchor poll, so force it on.
        g_v3_arm = (config.v3_arm && typeof config.v3_arm.anchor === 'string')
            ? {anchor: config.v3_arm.anchor,
               offset: (config.v3_arm.offset | 0),
               count:  (config.v3_arm.count  | 0) || 1,
               // OCCURRENCE to arm at (1-based): the anchor may recur (HOUSE_FREEROAM
               // fires once per house entry — iv1_1 then iv1_2 on a new game), so a
               // cutscene-sequence window must arm at the Nth firing matching the
               // scenario's {wait} count, not the first. Default 1 ⇒ no change for
               // every unique-anchor scenario (PAUSE_READY / *_READY fire once).
               occ:    (config.v3_arm.occ | 0) || 1}
            : null;
        g_v3_arm_fn = null;
        g_v3_arm_fired = false;
        g_v3_arm_count = 0;
        g_v3_shutdown_frame = 0;
        if (g_v3_arm) g_anchor_trace_enabled = true;

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

        // RNG caller histogram (finds per-frame shared-LCG consumers).
        g_rng_callers        = !!config.rng_callers;
        g_rng_count          = !!config.rng_count;
        // rng_callsites = N frames after the {phasepin} fire to record LCG
        // caller VAs over (resolved agent-side at the pin, so it's load-jitter
        // immune — same single run as the watch). 0/absent = off.
        g_rng_cs_len         = config.rng_callsites | 0;
        g_rng_callers_hooked = false;
        g_rng_callers_map    = {};
        // Defer the LCG hook install to the f406 entry (RE §21.2) — see the
        // g_rng_hook_defer decl + segtraceTick. g_rng_hook_wanted is decided at
        // the install gate below (it also folds in the call-trace rngcalls field).
        g_rng_hook_defer     = !!config.rng_hook_defer;
        g_rng_hook_wanted    = false;
        // Drive the iv1_7 wrap-up skip off retail's box state (RE §21.5) — see the
        // g_wrapup_skip_active decl + the input hook.  Auto-on with a {bgnpcpin}
        // (frida_capture sets it), so the canonical f406 drive just works.
        g_wrapup_skip_active = !!config.skip_wrapup;
        g_wrapup_seen_tutorial = false;

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
            // Save virtualization FIRST — must catch the engine's boot save-load
            // (CreateFile on save.dat) before it runs, so the real save is never
            // read or written by a replay. Pre-resume install guarantees it.
            if (g_save_sandbox) {
                installSaveRedirectHook(g_save_sandbox);
            }
            // TAS save capture (retail recorder): snapshot the boot save (the
            // state the game loaded) + arm the per-write hook. Boot capture runs
            // here at install (= attach) time, when the arena already holds the
            // loaded save.
            if (g_capture_saves) {
                installSaveWriteHook();
                captureSaveArena('boot', 0, 0);
                g_save_boot_captured = true;
            }
            installInitHook();
            installAudioHooks();
            installInputHook();
            installBgNpcPinHook();   // {phasepin} window-NPC warmup re-seed
            // {tutloadpin}: block-by-default from install (a sub-frame load
            // could reach the worker tail before the first pre-sim arm tick;
            // every 452aab tail is a dialogue bracket, so an arm + release
            // always follows). Abort loudly if the hook can't attach — the
            // trace pins the PORT unconditionally, so proceeding would
            // produce a silently half-pinned session (cost a recapture
            // cycle to spot, 2026-06-10).
            if (g_segtrace_tutloadpin > 0) {
                installTutLoadPinWorkerHook();
                if (g_tlp_hook_installed && g_tlp_flags) {
                    g_tlp_flags.writeS32(0);
                    log('tutloadpin: active, N=' + g_segtrace_tutloadpin);
                } else {
                    throw new Error('tutloadpin: worker hook unavailable - ' +
                                    'aborting capture (would be half-pinned)');
                }
            }
            if (g_segtrace_csloadpin > 0) {
                installCsLoadPinWorkerHook();
                if (g_csl_hook_installed && g_csl_flags) {
                    g_csl_flags.writeS32(0);
                    log('csloadpin: active, N=' + g_segtrace_csloadpin);
                } else {
                    throw new Error('csloadpin: worker hook unavailable - ' +
                                    'aborting capture (would be half-pinned)');
                }
            }
            // Window-hide/noactivate hook needs to install BEFORE resume so
            // it can intercept the engine's first ShowWindow call. Same
            // lifetime as the other capture-side hooks.
            if (g_hide_window || g_show_noactivate) {
                installShowWindowHook();
            }
            // Turbo + silent-audio. Both install pre-resume so they
            // catch the very first dispatcher entry / audio_init exit.
            // Probe mode installs the runtime-toggleable clock stub
            // INSTEAD of the fixed turbo hooks (same fn_clock_ms replace +
            // fn_tick attach — never install both).
            if (g_probe_mode) {
                installProbeClockHooks();
            } else if (g_turbo_enabled) {
                installTurboHooks();
            }
            if (g_silent_audio_enabled || g_probe_mode) {
                installSilentAudioHook();
            }
            // FPS overlay hide — NOP FUN_004523e6 so retail captures match
            // the port's capture-default-hide. Pre-resume is fine (replace
            // takes effect before the first render). show_fps skips it.
            if (g_hide_fps) {
                try {
                    g_hide_fps_cb = new NativeCallback(function () {},
                                                       'void', []);
                    Interceptor.replace(rva(ADDR.fn_fps_draw), g_hide_fps_cb);
                    log('fps overlay hidden (FUN_004523e6 NOP)');
                } catch (e) {
                    err('hideFps', e.message);
                }
            }
            // Resolution injection — must install pre-resume so we
            // catch the recet.ini parse onLeave before window creation.
            if (g_force_resolution_w > 0 && g_force_resolution_h > 0) {
                installForceResolutionHook(g_force_resolution_w,
                                           g_force_resolution_h);
            }
            // RNG seed pin — must install pre-resume so we catch the single
            // WinMain reseed (FUN_005041ec) before any RNG consumption.
            if (g_rng_seed !== null) {
                installRngSeedHook(g_rng_seed);
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
            // RNG caller histogram / consumption probe — a single Interceptor
            // on the LCG step, shared by callers / rngcalls-count / call-sites /
            // any flow-trace field declaring src:'rngcalls' (so flow_diff
            // --verdict's per-frame consumption row is populated automatically).
            let wantRngcallsField = false;
            for (const va in g_call_trace_fields) {
                const spec = g_call_trace_fields[va];
                if (Array.isArray(spec) && spec.some(function (f) {
                    return f && f.src === 'rngcalls'; })) {
                    wantRngcallsField = true; break;
                }
            }
            g_rng_hook_wanted = g_rng_callers || g_rng_count ||
                                g_rng_cs_len > 0 || wantRngcallsField;
            if (g_rng_hook_wanted) {
                if (g_rng_hook_defer) {
                    // A boot-installed LCG hook taxes the initial Continue-load's
                    // rng burst into a ~14000f stretch that breaks the f406 trace's
                    // determinism (RE §21.2). Arm it at the f406 entry instead
                    // (segtraceTick, cc08==4 && b51c==0).
                    log('rng LCG hook DEFERRED to the f406 entry (cc08==4 && '
                        + 'b51c==0) -- rng_hook_defer set');
                } else {
                    installRngCallerHook();
                }
            }
            // ESC-record hook (recorder) — capture WndProc ESC-skip presses.
            if (g_record_esc) {
                try { installEscRecordHook(); }
                catch (e) { err('installEscRecordHook', e.message); }
            }
            // ATTACH path: when we attach to an already-running retail (the
            // recorder), the d3d-init wrapper already ran, so installInitHook's
            // onLeave will never fire and the Present hook (which advances the
            // frame counter + drives anchorTick) would never install. Detect
            // the live device global and install the Present hook now.
            try {
                const dev = rva(ADDR.var_d3d_device).readPointer();
                if (!dev.isNull() && !g_present_hooked) {
                    g_device_inst = dev;
                    log('attach: existing IDirect3DDevice8* = ' + dev +
                        ' — installing Present hook now');
                    installPresentHook(dev);
                    if (g_d3d_trace_enabled) {
                        try { installD3dTraceHooks(dev); } catch (e) {
                            err('attach installD3dTraceHooks', e.message);
                        }
                    }
                }
            } catch (e) {
                err('attach device discovery', e.message);
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

    // ── live-probe surface (tools/probe_daemon.py) ──
    // NB frida-python converts snake_case RPC names to camelCase — all
    // exports here must be camelCase (see the note above queueCapture).

    // Who owns the input mask: true = probe queue (real input locked),
    // false = the human at the keyboard.
    probeActivate: function (on) {
        g_probe_active = !!on;
        return g_probe_active;
    },

    // Queue a tap: `mask` held for `press` polls, then released for `gap`
    // polls, `repeat` times. Frame-exact (one queue step per input poll).
    probeTap: function (mask, press, gap, repeat) {
        press = (press | 0) || 2;
        gap = (gap | 0) || 2;
        repeat = (repeat | 0) || 1;
        for (let i = 0; i < repeat; i++) {
            g_probe_queue.push({mask: mask & 0xffff, n: press});
            g_probe_queue.push({mask: 0, n: gap});
        }
        return g_probe_queue.length;
    },

    // Sticky hold OR'd under the tap queue (walking + tapping compose).
    probeHold: function (mask) {
        g_probe_hold = mask & 0xffff;
        return g_probe_hold;
    },

    // Hold `mask` for exactly `n` polls (a timed walk), via the queue.
    probeHoldFor: function (mask, n) {
        g_probe_queue.push({mask: mask & 0xffff, n: (n | 0) || 1});
        return g_probe_queue.length;
    },

    probeRelease: function () {
        g_probe_hold = 0;
        g_probe_queue = [];
        return true;
    },

    // Synthesize a real WndProc ESC (the keyboard-only skip/pause path —
    // ESC is NOT in the DInput mask).
    probeEsc: function () {
        synthesizeEscRetail();
        return true;
    },

    probeShot: function (n) {
        g_probe_shot += (n | 0) || 1;
        return g_probe_shot;
    },

    probeStream: function (every) {
        g_probe_stream_every = every | 0;
        return g_probe_stream_every;
    },

    probeSetTurbo: function (on) {
        return probeSetTurboImpl(on);
    },

    probeSetSilentAudio: function (on) {
        g_silent_audio_enabled = !!on;
        return g_silent_audio_enabled;
    },

    probeRead: function (va, type) {
        ensureBase();
        return probeReadTyped(va | 0, type || 'i32');
    },

    // Batched typed reads: [{name, va, type}] → {name: value}. One RPC
    // round-trip for a whole curated state snapshot.
    probeReads: function (specs) {
        ensureBase();
        const out = {};
        for (let i = 0; i < specs.length; i++) {
            const s = specs[i];
            try { out[s.name] = probeReadTyped(s.va | 0, s.type || 'i32'); }
            catch (e) { out[s.name] = null; }
        }
        return out;
    },

    probePoke: function (va, type, val) {
        ensureBase();
        probeWriteTyped(va | 0, type || 'i32', val);
        return true;
    },

    probePokeBytes: function (va, bytes) {
        ensureBase();
        const buf = Memory.alloc(bytes.length);
        buf.writeByteArray(bytes);
        Memory.copy(rva(va | 0), buf, bytes.length);
        return bytes.length;
    },

    // Enqueue an engine-thread function call (runs at the next pre-sim
    // input_poll — the safe slot). Result arrives as a {kind:'call_result'}
    // message keyed by the returned id. argt: frida NativeFunction types
    // (['int','pointer',...]); abi: 'mscdecl'|'stdcall'|'thiscall'|'fastcall'.
    probeEnqueueCall: function (va, args, argt, ret, abi) {
        ensureBase();
        const id = ++g_probe_call_id;
        g_probe_calls.push({id: id, va: va | 0, args: args || [],
                            argt: argt || [], ret: ret || 'int32',
                            abi: abi || 'mscdecl'});
        return id;
    },

    probeStatus: function () {
        let seg = null;
        if (g_segtrace_active) {
            seg = {seg: g_segtrace_seg, total: g_segtrace_segments.length,
                   done: g_segtrace_seg >= g_segtrace_segments.length};
        }
        return {
            frame: frameNo(),
            probe_active: g_probe_active,
            turbo: g_turbo_enabled,
            silent_audio: g_silent_audio_enabled,
            hold: g_probe_hold,
            queue_len: g_probe_queue.length,
            calls_pending: g_probe_calls.length,
            shots_pending: g_probe_shot,
            stream_every: g_probe_stream_every,
            segtrace: seg,
            base: g_base ? g_base.toString() : null,
        };
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

    // Walk-dust param diff — call FUN_00447f4f (records-A spawn) for a
    // given type with a FORCED RNG seed, read back the slot's RNG-computed
    // fields (vel/rot/scale/age/pos). Lets the port↔retail particle-param
    // computation be diffed bit-for-bit across seeds, isolated from the
    // per-frame RNG stream + render. Snapshot/restore slot 0, the records
    // count, and the LCG seed so the frozen engine state is untouched.
    // Args are passed cdecl (the engine fn is __cdecl). Returns the slot
    // fields as JS numbers.
    runRetailDustSpawn: function (seed_u32, x, y, z, type, scale) {
        if (!g_diff_test_enabled) {
            throw new Error('runRetailDustSpawn: diff_test mode required');
        }
        ensureBase();
        const seedPtr  = rva(ADDR.var_lcg_seed);
        const base     = rva(ADDR.var_records_a_base);   // 0x069b2f80
        const countPtr = rva(ADDR.var_records_a_count);  // 0x0076b960
        const STRIDE   = RECORD_A_STRIDE_DW * 4;         // 0x94
        const slot0    = base;                            // we force the spawn into slot 0
        const savedSeed  = seedPtr.readU32();
        const savedCount = countPtr.readS32();
        const savedSlot  = slot0.readByteArray(STRIDE);
        try {
            seedPtr.writeU32(seed_u32 >>> 0);
            countPtr.writeS32(0);
            slot0.add(12 * 4).writeS32(-1);  // type = -1 (empty) so the spawn takes slot 0
            const fn = new NativeFunction(
                rva(0x00447f4f), 'void',
                ['int', 'float', 'float', 'float', 'int', 'float', 'int']);
            fn(0, x, y, z, type | 0, scale, 1);
            const f = (n) => slot0.add(n * 4).readFloat();
            const i = (n) => slot0.add(n * 4).readS32();
            return {
                pos:  [f(0), f(1), f(2)],
                vel:  [f(3), f(4), f(5)],
                rot:  [f(6), f(7), f(8)],
                type: i(12), age: i(13), scale: f(14),
            };
        } finally {
            slot0.writeByteArray(savedSlot);
            countPtr.writeS32(savedCount);
            seedPtr.writeU32(savedSeed);
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
