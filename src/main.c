/*
 * OpenRecet — drop-in replacement for recettear.exe (skeleton).
 *
 * Boots, registers the "Azumanga Main Window" class, creates the window
 * titled "RECETTEAR Ver 1.108", dynamically loads d3d8.dll, creates an
 * IDirect3DDevice8, runs the message pump, presents a clear color, and
 * exits cleanly. Every subsystem is a stub — see docs/findings/winmain-
 * and-bootstrap.md for the addresses being shadowed.
 *
 * Build: `make -C src` (uses i686-w64-mingw32-gcc from the nix dev shell).
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS                    /* IDirect3D8_CreateDevice etc. */
#define CINTERFACE                    /* C-style COM vtables */
#include <windows.h>
#include <d3d8.h>
#include <mmsystem.h>                 /* timeBeginPeriod */
#include <stdio.h>
#include <stdint.h>

#include "msgbox_hook.h"
#include "storage.h"
#include "sprite.h"
#include "input.h"
#include "anchor_trace.h"
#include "scene1_intro_dialogue.h"   /* TEXT_ANIM anchor sources */
#include "scene1_dialogue_draw.h"    /* opening-prologue dialogue render pass */
#include "input_trace.h"
#include "input_segtrace.h"
#include "layers.h"
#include "tables.h"
#include "recet_ini.h"
#include "prewindow.h"
#include "render_quad.h"
#include "rng.h"
#include "fade.h"
#include "nowloading.h"
#include "scene.h"
#include "esc_dispatch.h"
#include "scene_ingame.h"
#include "scene_title.h"
#include "scene_buy.h"
#include "scene_floor.h"
#include "scene_jutan.h"
#include "scene_pause.h"
#include "scene_sc1.h"
#include "scene_table.h"
#include "scene_walls.h"
#include "scene_worldmap.h"
#include "sysassets.h"
#include "sim.h"
#include "music.h"
#include "audio.h"
#include "audio_fade.h"
#include "save_bank.h"
#include "save_io.h"
#include "font.h"
#include "font_alloc.h"
#include "font_atlas.h"
#include "font_upload.h"
#include "tables_config.h"        /* g_config for font_atlas regen gate */
#include "math3d.h"
#include "mesh.h"
#include "mesh_draw.h"
#include "mesh_load.h"
#include "scene1_camera.h"
#include "scene1_combat_sm.h"
#include "scene1_dungeon_clear_banner.h"
#include "scene1_overlay_table.h"
#include "scene1_pass_f.h"
#include "scene1_per_frame_open.h"
#include "scene1_postload.h"
#include "scene1_preload.h"
#include "scene1_records.h"
#include "scene1_render.h"
#include "scene1_player_ctrl.h"
#include "scene1_hud.h"
#include "scene1_shop_walker.h"
#include "scene1_chr_sprite.h"
#include "scene1_chr_walker.h"
#include "chr_sprite_meta.h"
#include "sprite.h"
#include "stage_palette.h"
#include "stage_post_load.h" /* stage_post_load_get_dat_056da1cc (player char) */
#include "stage_state.h"
#include "tick.h"
#include "d3d_trace.h"
#include "call_trace.h"

/* ─── original-engine constants (from RE) ───────────────────────────────── */
#define AZUMANGA_CLASS  "Azumanga Main Window"
#define AZUMANGA_TITLE  "RECETTEAR Ver 1.108"
#define ICON_RES_ID     0x67
#define MENU_RES_ID     0xB7          /* used when windowed; we have no menu yet */

/* ─── globals matching the original's named "DAT_*" engine state ─────────
 * Names mirror the role of the original's globals (see docs/findings/
 * winmain-and-bootstrap.md). When we trace subsystems we'll relocate them.
 */
static HINSTANCE        g_hInstance;            /* DAT_073de628 */
static HWND             g_hwnd;                 /* DAT_073dfc7c */
static IDirect3D8      *g_d3d;                  /* DAT_073dfcb8 */
static IDirect3DDevice8 *g_dev;
static BOOL             g_paused = FALSE;       /* DAT_073dfca0 */
static BOOL             g_windowed = TRUE;      /* DAT_0438b164 — overwritten from g_ini after load */
static BOOL             g_skip_quit_prompt = FALSE; /* DAT_0964356c — set by EXIT menu before PostMessage(WM_CLOSE) */
static struct recet_ini g_ini;                  /* recet.ini contents, populated in WinMain pre-window */

/* ─── frame-capture globals ─────────────────────────────────────────────
 * CLI: --capture-to <dir> --capture-every-ms <millis>
 * Time-based sampling: write a BMP if at least <every_ms> have elapsed
 * since the previous capture. Default 1000ms = 1 capture/sec — enough for
 * rough smoke tests without flooding the filesystem.  We can add
 * fps-bucketed or frame-indexed modes later if a test needs them.
 */
static char            *g_capture_dir       = NULL;
static unsigned         g_capture_every_ms  = 1000;
static unsigned         g_capture_last_ms   = 0;     /* timeGetTime() of last capture */
static unsigned         g_capture_count     = 0;     /* monotonic capture index */

/* --show-sprite <name>: load an asset at startup via sprite_load (the
 * engine-style loader — disk first, storage overlay fallback) and draw
 * it as a sprite at (32, 32) every tick. <name> can be a disk path or
 * a storage asset name (e.g. "bmp/ivent/ed_kasi11.tga"). */
static char            *g_show_sprite_name  = NULL;
static sprite_t         g_show_sprite       = {0};

/* --show-mesh <path>: C7a visual smoke for the mesh pipeline. Loads one
 * .x file via mesh_load + mesh_load_finalize_win32 at startup, draws it
 * every frame with an orbital camera. <path> is a storage-relative or
 * disk path (e.g. "xfile/etc/ice01.x"). Independent of the scene state
 * — drawn on top of whatever the current scene rendered, before the
 * fade/nowloading overlays so the mesh sits under fade tints. */
static char            *g_show_mesh_path    = NULL;
static mesh_t          *g_show_mesh         = NULL;
static float            g_show_mesh_zoom    = 1.0f;

/* --house-preview [--house-preview-path <path>]: C7d throwaway. When
 * set + scene_state == INGAME, replaces the placeholder ingame screen
 * with shop_1st.x (or any .x via --house-preview-path) drawn through
 * mesh_draw_d3d8 + a slow orbital camera. Lets us see house geometry
 * inside the actual scene-1 dispatch *before* the proper load chain
 * (C7e) and the FUN_0040a765 walker (C7i+) land. Off by default —
 * title-z-press stays bit-exact. Gets ripped out when C7n delivers
 * the real walker. */
static int              g_house_preview        = 0;
static char            *g_house_preview_path   = "xfile/shop/shop_1st.x";
static char            *g_house_preview_dump   = NULL;  /* see --house-preview-dump */
static mesh_t          *g_house_preview_mesh   = NULL;

/* --show-pass-f-test: MVP visual smoke for the C8c/C8e/wide-followup
 * draw chain.  When set, injects one type-0x92 record into table A at
 * scene1_preload_house entry and renders it every INGAME frame via
 * scene1_pass_f_render under a stand-alone identity-view + RH-perspective
 * camera.  Validates the Pass F render contract without first porting
 * FUN_0040fb3a (the 8071 B integrator) or FUN_00447f4f (the 11826 B
 * spawn API).  See docs/findings/scene1-particles-tick.md "Option A". */
static int              g_show_pass_f_test     = 0;

/* --force-pass-d-mesh <path>: C8e.bridge visual smoke.  Loads one .x
 * file via mesh_load + mesh_load_finalize_win32 at boot and hands it
 * to scene1_shop_walker_set_pass_d_mesh().  Stand-in for the engine's
 * static &DAT_073a9680 (train_iwa.x — loaded only in FUN_00474a9a's
 * DUNGEON branch).  Combined with `--force-ambient-spawn
 * --ambient-spawn-type 0x79`, this surfaces visible Pass D draws for
 * the {0x74, 0x79, 0x96} record types without first porting the
 * DUNGEON preload.  Default unset → Pass D stays dormant (engine
 * HOUSE default). */
static char            *g_force_pass_d_mesh_path = NULL;
static mesh_t          *g_force_pass_d_mesh      = NULL;

/* --force-player-sprite <inject>: Cchr.2b first-pixels MVP (strategy-B
 * step 5).  Reads a flat inject file (produced by tools/chr_leaf_to_inject.py
 * from a retail --chr-leaf capture) holding ONE leaf call's inputs:
 * char_id, color, sheet tex dims, the world matrix, the actor
 * sprite-state struct, and an optional sheet texture to bind.  On HOUSE
 * INGAME frames it injects those into scene1_chr_sprite_render so the
 * ported leaf draws the player billboard on top of the scene, for visual
 * A/B vs retail.  The Cchr.2a loaders (chr_formdata_load / chr_meta_load)
 * are wired at boot under this flag so g_chr_formdata + the descriptor
 * hold real data; build_quads then resolves the same cell retail did.
 * Off by default — normal boot doesn't load the chr data. */
static int              g_force_player_sprite      = 0;
/* --force-chr-walker: populator-survey MVP.  Seeds ONE standing-Recette
 * render slot into the Cchr.2d walker (FUN_00456f56) so it draws her in
 * HOUSE end-to-end (matrix + alpha + state + leaf) WITHOUT the ~18 KB
 * FUN_0048b850/FUN_0044376a per-frame actor populator.  Unlike
 * --force-player-sprite (which calls the leaf directly with a captured
 * world matrix), this validates the WALKER's own matrix/alpha chain.
 * Diffuse-only (no sheet bound).  See scene1-char-sprite-render.md banner. */
static int              g_force_chr_walker         = 0;
/* --no-chr-player (Cchr.2h): suppress the default standing-player billboard.
 * The player now draws by default on HOUSE entry (the real engine-global
 * actor model, seeded by scene1_postload_pose_house_standing); this disables
 * the pose seed (char id stays -1 → the sw_pass_light gate skips it) for A/B
 * vs the player-less scene. */
static int              g_no_chr_player            = 0;
static char             g_fps_inject_path[MAX_PATH] = {0};
static int              g_fps_loaded               = 0;  /* inject parsed ok */
static int32_t          g_fps_actor[0x11]          = {0};
static float            g_fps_world[16]            = {0};
static int              g_fps_char_id              = 0;
static uint32_t         g_fps_color                = 0xffffffffu;
static int              g_fps_tex_w                = 0;
static int              g_fps_tex_h                = 0;
static char             g_fps_sheet[160]           = {0};  /* "" / "-" = none */
static sprite_t         g_fps_sheet_sprite         = {0};
static int              g_fps_sheet_tried          = 0;    /* lazy-load latch */

/* --debug-pass-d-unlit: C8e.smoke visual smoke.  Brute-force state
 * override that mirrors the C8e.bridge proof-of-life — forces
 * LIGHTING=FALSE + LightEnable(0,FALSE) + CULLMODE=NONE +
 * COLOROP=SELECTARG1 + COLORARG1=DIFFUSE inside sw_pass_d, bypassing
 * the engine's L548-562 lit preamble.  Surfaces visible Pass D pixels
 * through the production walker + emit + spawn + camera chain when
 * combined with --force-pass-d-mesh + --force-ambient-spawn
 * --ambient-spawn-type 0x79 (or 0x74 / 0x96).  Off by default —
 * goldens stay bit-exact.  Diverges from engine state; use only to
 * verify the chain end-to-end, not as a long-lived fidelity option. */
static int              g_debug_pass_d_unlit     = 0;

/* --force-ambient-spawn / --ambient-spawn-type <N>: bypass the
 * stage_palette->ambient_spawn_flag gate in scene1_postload_ambient_spawn
 * (the FUN_00436f97 tail port at L690-700) and optionally swap the
 * hardcoded type 0x4f for `N`.  `--ambient-spawn-type 0x92` is the
 * postload-path equivalent of `--show-pass-f-test`'s manual injection:
 * surfaces Pass F pixels through the real spawn API + integrator chain
 * instead of dropping a single record into the table directly.
 *
 * Override value of -1 means "no override (engine default 0x4f)". */
static int              g_force_ambient_spawn          = 0;
static int              g_ambient_spawn_type_override  = -1;

/* --ambient-spawn-pose <x>,<y>,<z>: parsed as three comma-separated
 * decimals (strtof).  Replaces the ambient-spawn anchor (which would
 * otherwise come from g_scene1_player_pos + (0, 2, 0)) so the smoke
 * lands inside the HOUSE engine camera frustum.  See Cc.0 survey in
 * docs/findings/scene1-camera-helpers.md — HOUSE camera anchors near
 * world origin, not at the player's HOUSE-default (-40, 0, -60). */
static int              g_ambient_spawn_pose_set       = 0;
static float            g_ambient_spawn_pose[3]        = {0.0f, 0.0f, 0.0f};

/* --force-c-pickup <type> / --force-c-world-drop <type>: C8j.fin.c
 * smoke wiring for the table C spawn allocators (C8j.2).  Fires
 * `scene1_records_c_spawn_pickup` and/or `_spawn_world_drop_default`
 * once per HOUSE entry from `scene1_postload_smoke_c_spawn()`.  The
 * records then tick every INGAME frame via the C8j.3 default-arm.
 *
 * Position reuses --ambient-spawn-pose when set; otherwise spawns at
 * (player.x, player.y + 2, player.z), same convention as
 * --force-ambient-spawn.
 *
 * Pass C/D walker bodies (scene1_wide_followup.c) are TODO stubs, so
 * this doesn't yet produce visible pixels — but the populator + tick
 * path is exercised end-to-end (the C8j.1 integrator advances the
 * slots' age + physics every frame, evicts at age==0xf0).
 *
 * Override of -1 = "no smoke spawn (engine HOUSE default)". */
static int              g_force_c_pickup_type         = -1;
static int              g_force_c_world_drop_type     = -1;
static int              g_force_c_world_drop_count    = 8;
static float            g_force_c_world_drop_mag      = 1.0f;

/* --force-b-npc <type> / --force-b-entity <type>: C8j.fin.b smoke
 * wiring for the two table B allocators (C8j.5-13).  Spawns one slot
 * each from a fake-owner blob seeded inside scene1_postload.c (NPC:
 * 1024 B at +0x3f8 max; entity: 3760 B at +0xeac max), called once
 * per HOUSE entry from the preload tail.
 *
 * Spawn pose reuses --ambient-spawn-pose when set; else
 * (player.x, player.y + 2, player.z).
 *
 * Anchor types: NPC 0xe / 0x97 / 0x46 (LAB_00447584 trivial tail);
 * entity 0x24 (pure preamble).  More-complex types work too — they
 * exercise per-type bodies that read additional owner fields the
 * smoke blob doesn't populate, so the per-type writes get junk inputs
 * but the allocator + preamble + slot commit still succeed.
 *
 * Override of -1 = "no smoke spawn (engine HOUSE default)". */
static int              g_force_b_npc_type            = -1;
static int              g_force_b_entity_type         = -1;

/* --force-walker-phase2 <N>: enable Cf.minimal writer chunk on HOUSE
 * entry with scene_type=N (valid range [0..4]).  -1 disables (default).
 *
 * scene_type 0 → real new-game HOUSE: applies the retail-captured,
 *   test-verified ground truth (count=3 furniture meshes, real layout).
 * scene_type 1 → 4 furniture meshes (slots 0..3), synthetic positions.
 * scene_type 3 or 4 → 10 furniture meshes (slots 0..9), synthetic.
 *
 * Acts on `scene1_postload_walker_phase2_init` from scene1_preload_house.
 * FUN_00436f97 (block 21) DOES fire on new-game HOUSE entry (proven
 * 2026-05-29 via the E.1 call tracer — earlier docs claiming otherwise
 * were a static-analysis error).  This flag supplies the writer's three
 * runtime inputs (scene_type / ivar8 / stage_positions) whose engine
 * sources are not yet ported, so HOUSE-from-title produces visible
 * shop_table furniture pixels via PII.3b's draw loop B. */
static int              g_force_walker_phase2_scene_type = -1;

/* Scene-0 (title) state now lives in scene_title.c as module globals
 * (`g_scene_title_menu`, `g_scene_title_anim`, `g_scene_title_assets_loaded`).
 * sim_step_a and render_dispatch both reach in directly via those externs;
 * main.c only seeds them at boot. */

/* --max-duration-ms <ms>: PostQuitMessage after this many milliseconds.
 * Lets the harness (and ad-hoc smoke runs) get a clean shutdown — the
 * normal message-pump exit path — instead of SIGTERM/taskkill leaving
 * orphan windows on the host. 0 = no limit.
 *
 * Implemented via SetTimer so it also fires while g_paused=TRUE (window
 * inactive → main loop sits in WaitMessage instead of ticking). Without
 * the timer the pump would never wake to check the deadline. */
#define AUTO_EXIT_TIMER_ID  1
static unsigned         g_max_duration_ms   = 0;

/* --audio-trace <path>: opt-in JSONL log of audio events (BGM swap so
 * far, SE/fade once those ports land). One line per event, schema in
 * src/audio.h. Tracking is initialised in audio_trace_open(). */
static char            *g_audio_trace_path  = NULL;

/* --save-write: opt-in shutdown save-back. When set, the in-memory
 * save arena (header + 100 banks) is written to save.dat and
 * _save.dat at shutdown — mirrors engine FUN_004905a8(-1). Off by
 * default so harness runs and ad-hoc smokes don't accidentally
 * overwrite the user's real save with whatever state happened to be
 * in memory when the run ended.
 *
 * When the user explicitly opts in (e.g., `--save-write` from
 * openrecet-debug.exe), settings-menu slider changes persist across
 * boots (slider write → save_header → disk → save_io_try_load on
 * next boot → audio_fade sync from save_header). Tested manually
 * by adjusting Music slider, quitting, re-running. */
static int              g_save_write         = 0;

/* --play-se <slot[,slot,...]>: comma-separated SE slot indices to fire
 * post-boot. Phase A audio_play_se() is a trace-only shell so this only
 * exercises the dispatch shell + JSONL emit today; phase B's live
 * PlaySegmentEx will audibly fire them. Both flags default to "off" /
 * sensible smoke values. */
#define PLAY_SE_MAX_SLOTS       16
#define PLAY_SE_TIMER_ID        2
static int              g_play_se_slots[PLAY_SE_MAX_SLOTS];
static int              g_play_se_count       = 0;
static int              g_play_se_index       = 0;     /* next slot to fire */
static unsigned         g_play_se_after_ms    = 1000;  /* delay until first fire */
static unsigned         g_play_se_interval_ms = 250;   /* gap between fires */

/* ─── Phase A harness flags ─────────────────────────────────────────────
 *
 *   --input-trace-record <file>  emit a sparse JSONL trace of
 *                                g_input_state[0].buttons each ticked
 *                                frame (changes only — see input_trace.h)
 *   --input-trace-replay <file>  load the same format, replace the
 *                                tick-callback input_poll with a lookup
 *                                that writes the recorded mask into
 *                                g_input_state[0].buttons each frame.
 *                                Skips input_init / DirectInput. Pins
 *                                g_paused=FALSE so a window deactivation
 *                                can't stall the replay. Pairs with
 *                                --rng-seed for full determinism.
 *   --anchor-trace-record <file> emit the TAS anchor event stream
 *                                ({"anchor":NAME,"frame":N} JSONL) to
 *                                <file>; also echoed to stderr. Keyed on
 *                                g_tick.frame_count (== --capture-frames
 *                                index). See src/anchor_trace.h.
 *   --capture-at-anchor NAME[+k] capture at frame (anchor_frame + k)
 *                                when anchor NAME fires — robust to the
 *                                non-deterministic load frame (unlike a
 *                                fixed --capture-frames). Repeatable.
 *                                Needs --capture-to. k may be signed.
 *   --rng-seed <n>               replace `rng_seed_from_now()` with
 *                                `rng_seed(n)` so the title's BG scroll
 *                                + cursor pulse phase are deterministic
 *                                across replays.
 *   --max-frames <n>             PostQuitMessage(0) after n rendered
 *                                frames; companion to --max-duration-ms
 *                                for scenarios that want a fixed frame
 *                                budget instead of a wall-clock budget.
 *   --capture-frames i,j,k       capture ONLY at the listed sim-frame
 *                                indices (g_tick.frame_count). Used by
 *                                the scenario runner for fixed
 *                                pixel-diff anchors. When set, the
 *                                older time-based --capture-every-ms
 *                                is ignored.
 *
 * All five flags are independent and additive. Recording does NOT need
 * --rng-seed (replays don't care about seed match), but a replay you
 * intend to diff against a golden should pass the same --rng-seed.
 */
static char           *g_input_trace_record_path = NULL;
static char           *g_input_trace_replay_path = NULL;

/* --input-segtrace <file>: anchor-segmented input forcing (the TAS spam-Z
 * replacement; see src/input_segtrace.h + docs/plans/tas-framework.md). Owns
 * the input mask like --input-trace-replay, but timing is anchor-relative so
 * the same trace drives the port and retail to the same instants despite load
 * jitter. Its `{capture:N}` ops schedule anchor-relative backbuffer captures
 * via the same g_capture_frames path --capture-at-anchor uses. */
static char                   *g_input_segtrace_path = NULL;
static struct input_segtrace   g_segtrace            = {0};

/* True when input is harness-driven (absolute replay OR anchor-segmented
 * forcing). Both own the input mask, so we suppress live DirectInput, pin
 * g_paused=FALSE, and drive virtual time for a deterministic one-tick-per-loop. */
static int input_harness_driven(void)
{
    return g_input_trace_replay_path != NULL || g_input_segtrace_path != NULL;
}

/* --anchor-trace-record <file>: emit the TAS anchor event stream
 * ({"anchor":NAME,"frame":N} JSONL) to <file>.  Always also echoed to
 * stderr (prefixed "anchor:") so it shows up in run-openrecet logs even
 * without a file sink.  See src/anchor_trace.h.  The stream is keyed on
 * g_tick.frame_count — the same index --capture-frames uses — so an
 * anchor frame is directly comparable to a capture frame. */
static char                     *g_anchor_trace_record_path = NULL;
static FILE                     *g_anchor_record_fp         = NULL;
static struct anchor_trace_state g_anchor_state             = {0};

/* --player-pos-log <file>: per-frame JSONL dump of the player world position
 * ({"frame":N,"px":..,"py":..,"pz":..}) while in the INGAME (HOUSE) scene.
 * The port-side equivalent of the retail Frida --watch on 0x056da1d8 — used to
 * verify collision/movement behavior (e.g. wall blocking) from a TAS drive
 * without a debugger.  The actor positions live in scene1_particles_tick.h as
 * the contiguous g_scene1_actor_pos[3][3] (slot 0 = player, slot 2 = companion);
 * g_scene1_player_pos is its slot-0 alias macro. */
extern float                     g_scene1_actor_pos[3][3];
#define g_scene1_player_pos      (g_scene1_actor_pos[0])
static char                     *g_player_pos_log_path      = NULL;
static FILE                     *g_player_pos_log_fp        = NULL;

/* ─── in-engine TAS trace recorder (F2 start/stop, F3 capture-point) ──────────
 * Buffers the per-frame player-1 button mask while recording, plus a list of
 * capture frames (F3).  On stop, dumps a RAW recording (every frame's mask,
 * relative to the F2 press) to a unique file and prints the path to stdout;
 * tools/distill_trace.py collapses it into the sparse change-point trace format
 * (+ optional new-game→HOUSE intro wrap).  Console build (openrecet-debug.exe)
 * so stdout is visible while you drive in real time. */
static int       g_trace_rec_active       = 0;
static uint32_t  g_trace_rec_start_frame  = 0;
static uint16_t *g_trace_rec_masks        = NULL;
static uint32_t  g_trace_rec_count        = 0;
static uint32_t  g_trace_rec_cap          = 0;
static uint32_t  g_trace_rec_caps[256];
static int       g_trace_rec_caps_count   = 0;
static uint32_t  g_trace_rec_ct[256][2];      /* [start,len] call-trace windows (F4) */
static int       g_trace_rec_ct_count     = 0;
static int       g_trace_rec_ct_open       = -1; /* open-window start frame, or -1 */
static int       g_trace_rec_seq          = 0;
#define TRACE_REC_MAX_FRAMES  600000u   /* ~2.8 h at 60fps — a hard backstop */

static void trace_rec_start(void)
{
    if (!g_trace_rec_masks) {
        g_trace_rec_cap   = 4096;
        g_trace_rec_masks = (uint16_t *)malloc(g_trace_rec_cap * sizeof(uint16_t));
        if (!g_trace_rec_masks) { g_trace_rec_cap = 0; return; }
    }
    g_trace_rec_count       = 0;
    g_trace_rec_caps_count  = 0;
    g_trace_rec_ct_count    = 0;
    g_trace_rec_ct_open     = -1;
    g_trace_rec_start_frame = g_tick.frame_count;
    g_trace_rec_active      = 1;
    printf("[trace-rec] recording STARTED at frame %u "
           "(F2 stop, F3 capture-point, F4 call-trace window on/off)\n",
           g_trace_rec_start_frame);
    fflush(stdout);
}

/* Append this frame's player-1 button mask (called once per ticked frame). */
static void trace_rec_tick(uint16_t mask)
{
    if (!g_trace_rec_active) return;
    if (g_trace_rec_count >= TRACE_REC_MAX_FRAMES) return;
    if (g_trace_rec_count >= g_trace_rec_cap) {
        uint32_t ncap = g_trace_rec_cap * 2;
        uint16_t *nb = (uint16_t *)realloc(g_trace_rec_masks, ncap * sizeof(uint16_t));
        if (!nb) return;               /* keep what we have; stop growing */
        g_trace_rec_masks = nb;
        g_trace_rec_cap   = ncap;
    }
    g_trace_rec_masks[g_trace_rec_count++] = mask;
}

static void trace_rec_add_capture(void)
{
    if (!g_trace_rec_active) {
        printf("[trace-rec] F3 ignored — not recording (press F2 first)\n");
        fflush(stdout);
        return;
    }
    if (g_trace_rec_caps_count < (int)(sizeof g_trace_rec_caps / sizeof g_trace_rec_caps[0])) {
        uint32_t rel = g_trace_rec_count;   /* relative frame of the NEXT recorded frame */
        g_trace_rec_caps[g_trace_rec_caps_count++] = rel;
        printf("[trace-rec] capture point #%d at relative frame %u\n",
               g_trace_rec_caps_count, rel);
        fflush(stdout);
    }
}

/* F4: toggle a call-trace window.  First press opens at the current relative
 * frame; second press closes it (recording [start, len]).  distill_trace.py
 * turns each window into a {calltrace:[start,len]} op that auto-enables windowed
 * call-tracing on both the port and retail. */
static void trace_rec_toggle_calltrace(void)
{
    if (!g_trace_rec_active) {
        printf("[trace-rec] F4 ignored — not recording (press F2 first)\n");
        fflush(stdout);
        return;
    }
    if (g_trace_rec_ct_open < 0) {
        g_trace_rec_ct_open = (int)g_trace_rec_count;
        printf("[trace-rec] call-trace window OPEN at relative frame %u (F4 to close)\n",
               g_trace_rec_count);
    } else {
        uint32_t start = (uint32_t)g_trace_rec_ct_open;
        uint32_t len   = g_trace_rec_count - start;
        if (g_trace_rec_ct_count <
                (int)(sizeof g_trace_rec_ct / sizeof g_trace_rec_ct[0])) {
            g_trace_rec_ct[g_trace_rec_ct_count][0] = start;
            g_trace_rec_ct[g_trace_rec_ct_count][1] = len;
            g_trace_rec_ct_count++;
            printf("[trace-rec] call-trace window CLOSED: [%u, %u]\n", start, len);
        }
        g_trace_rec_ct_open = -1;
    }
    fflush(stdout);
}

static void trace_rec_stop(void)
{
    if (!g_trace_rec_active) return;
    g_trace_rec_active = 0;
    /* Auto-close a still-open call-trace window at the final frame. */
    if (g_trace_rec_ct_open >= 0) {
        uint32_t start = (uint32_t)g_trace_rec_ct_open;
        if (g_trace_rec_ct_count <
                (int)(sizeof g_trace_rec_ct / sizeof g_trace_rec_ct[0])) {
            g_trace_rec_ct[g_trace_rec_ct_count][0] = start;
            g_trace_rec_ct[g_trace_rec_ct_count][1] = g_trace_rec_count - start;
            g_trace_rec_ct_count++;
        }
        g_trace_rec_ct_open = -1;
    }

    char path[256];
    snprintf(path, sizeof path, "openrecet-trace-%lu-%d.raw.jsonl",
             (unsigned long)GetCurrentProcessId(), g_trace_rec_seq++);
    FILE *f = fopen(path, "w");
    if (!f) {
        printf("[trace-rec] ERROR: could not open %s for writing\n", path);
        fflush(stdout);
        return;
    }
    /* RAW recording: one row per recorded frame (relative frame + mask), then
     * the capture points.  tools/distill_trace.py collapses the per-frame rows
     * into sparse change-points and (optionally) wraps a bootable segtrace. */
    fprintf(f, "{\"_rec\":\"openrecet-tas-raw-v1\",\"frames\":%u,\"start_abs\":%u}\n",
            g_trace_rec_count, g_trace_rec_start_frame);
    for (uint32_t i = 0; i < g_trace_rec_count; i++)
        fprintf(f, "{\"frame\":%u,\"buttons\":\"0x%04x\"}\n", i, g_trace_rec_masks[i]);
    for (int c = 0; c < g_trace_rec_caps_count; c++)
        fprintf(f, "{\"capture\":%u}\n", g_trace_rec_caps[c]);
    for (int c = 0; c < g_trace_rec_ct_count; c++)
        fprintf(f, "{\"calltrace\":[%u,%u]}\n",
                g_trace_rec_ct[c][0], g_trace_rec_ct[c][1]);
    fclose(f);
    printf("[trace-rec] recording STOPPED: %u frames, %d capture(s), %d call-trace window(s)\n",
           g_trace_rec_count, g_trace_rec_caps_count, g_trace_rec_ct_count);
    printf("[trace-rec] wrote %s\n", path);
    printf("[trace-rec] distill: nix develop --command python3 tools/distill_trace.py %s\n", path);
    fflush(stdout);
}

static int             g_rng_seed_set            = 0;
static uint32_t        g_rng_seed_value          = 1;
static uint32_t        g_max_frames              = 0;

#define CAPTURE_FRAMES_MAX  32
static uint32_t        g_capture_frames[CAPTURE_FRAMES_MAX];
static int             g_capture_frames_count    = 0;

/* --capture-at-anchor NAME[+k|-k]: schedule a capture at frame
 * (anchor_frame + k) when the named anchor fires.  This is the
 * anchor-relative capture the TAS framework needs: the absolute frame an
 * event lands on is non-deterministic (the new-game->HOUSE load jitters
 * ~100 frames run-to-run, see anchor_trace.h), so "capture at frame N"
 * can't reliably hit a specific instant — but "HOUSE_FREEROAM + 5"
 * always does.  When the anchor fires, anchor_capture_schedule() appends
 * the resolved frame to g_capture_frames; the normal capture_frame_is_
 * listed() path (checked later in the SAME render_dispatch) picks it up.
 * Negative offsets that resolve before the current frame are dropped
 * (you can't capture the past). Requires --capture-to. */
#define ANCHOR_CAPTURE_MAX  16
struct anchor_capture_req {
    char    name[24];
    int32_t offset;
};
static struct anchor_capture_req g_anchor_captures[ANCHOR_CAPTURE_MAX];
static int             g_anchor_captures_count   = 0;

/* --d3d-trace <path> + --d3d-trace-frames i,j,k.  See d3d_trace.h.
 * Path nonzero turns the emitter on; if frames list is empty, every
 * frame's D3D calls are emitted.  D.5 of docs/harness-roadmap.md. */
#define D3D_TRACE_FRAMES_MAX 64
static char           *g_d3d_trace_path                            = NULL;
static unsigned        g_d3d_trace_frames[D3D_TRACE_FRAMES_MAX];
static int             g_d3d_trace_frames_count                    = 0;

/* --call-trace <path> + --call-trace-frames i,j,k.  See call_trace.h.
 * Port-side per-frame function-entry tracer; symmetric counterpart of
 * the Frida agent's call_trace.jsonl.  Phase E.2. */
#define CALL_TRACE_FRAMES_MAX_CLI 256
static char           *g_call_trace_path                              = NULL;
static unsigned        g_call_trace_frames[CALL_TRACE_FRAMES_MAX_CLI];
static int             g_call_trace_frames_count                     = 0;

/* --hidden: ShowWindow(SW_HIDE) instead of nCmdShow. D3D rendering
 * continues to work on a non-visible window (back buffer is video
 * memory, decoupled from on-screen visibility), so frame capture +
 * input replay run unaffected. No WM_ACTIVATE fires for a window
 * that never becomes visible, so g_paused stays at its default
 * FALSE — main loop ticks normally. Used by tools/scenario-test.py
 * so capture runs can't be accidentally clobbered by the user
 * typing into the game window. */
static int             g_hidden                  = 0;

/* --turbo / --silent-audio. Mirror the retail-side knobs in
 * tools/frida/openrecet-agent.js so a `--target both` scenario run
 * can crank both pipelines through their scenarios at host speed.
 *
 *   --turbo            Frame-limiter bypass: tick_set_turbo(1, 17),
 *                      so tick_step_win32 feeds the dispatcher a
 *                      virtual clock that advances 17 ms per loop
 *                      iteration and never Sleeps. Engine animations
 *                      / audio fades / RNG that key off tick_now_ms
 *                      stay consistent with what they'd be at 60 FPS
 *                      — wall time just compresses. Independent of
 *                      --input-trace-replay (replay already has its
 *                      own per-frame virtual clock; turbo wins on the
 *                      non-replay path).
 *   --silent-audio     Install a silencing wrapper around
 *                      audio_fade_apply_hook_win32 that clamps the
 *                      centibel passed to SetVolume to -10000
 *                      regardless of channel. Game's audio code
 *                      (PlaySegmentEx, fades, etc.) all run normally;
 *                      only the COM-level master attenuation is
 *                      pinned to silence. Recommended alongside
 *                      --turbo since DirectMusic complains about
 *                      being clocked far above its expected rate. */
static int             g_turbo                   = 0;
/* --auto-z-spam: write button A (mask 0x10) into the player-0 input
 * mask on alternating frames after input_poll returns.  Drives the
 * title menu past "Continue / New game" without keyboard input so an
 * unattended capture run boots straight into HOUSE.  Mutually
 * exclusive with --input-trace-replay (which OWNS the input mask).
 * Port-side analogue of the Frida agent's same-named flag (E.2.2). */
static int             g_auto_z_spam             = 0;
static int             g_silent_audio            = 0;

/* --no-singleton: bypass the cross-process singleton mutex acquired in
 * WinMain. Off by default — concurrent openrecet instances clobber
 * each other's save state and leak orphan windows during test
 * iteration, so a second launch normally refuses with a MessageBox.
 * The bypass exists for the rare case where two simultaneous runs are
 * actually intended (e.g. side-by-side visual comparison of two
 * builds). Equivalent to setting OPENRECET_NO_SINGLETON=1 in the
 * environment. */
static int             g_no_singleton            = 0;

/* --no-msgbox-hook: bypass the global MessageBox-to-stderr redirector
 * installed early in WinMain.  Off by default so autonomous runs never
 * block on a modal popup (from our own code OR from the DirectX
 * runtime); flip on when interactively debugging a path that needs the
 * real popup behaviour.  See src/msgbox_hook.h for the hook's scope
 * and limitations. */
static int             g_no_msgbox_hook          = 0;

/* Populated at boot when --input-trace-replay is set. The replay
 * stand-in for input_poll reads this each tick. */
static struct input_trace g_replay_trace = {0};

/* Dynamically-resolved DX entry point — matches the original's
 * LoadLibraryA("d3d8.dll") + GetProcAddress("Direct3DCreate8") pattern. */
typedef IDirect3D8 *(WINAPI *PFN_Direct3DCreate8)(UINT);
static HMODULE              g_d3d8_dll;
static PFN_Direct3DCreate8  g_pDirect3DCreate8;

/* ─── forward decls ──────────────────────────────────────────────────────── */
static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
static BOOL  create_main_window(HINSTANCE hInst, int nCmdShow);
static BOOL  load_d3d8(void);
static BOOL  init_render(HWND hwnd);
static void  shutdown_render(void);
static void  render_dispatch(void);
static void  parse_cmdline(LPSTR lpCmdLine);
static void  capture_backbuffer(void);
static mesh_t *house_preview_load_dump(const char *dir, IDirect3DDevice8 *dev);

/* Phase A wrappers. `recording_input_poll` is the wrapping
 * input-callback when --input-trace-record is set; `replay_input_poll`
 * replaces it under --input-trace-replay. Both pure-Win32 because they
 * read/write g_input_state[].buttons which only the Win32 build
 * sources via DI. */
static void  recording_input_poll(void);
static void  replay_input_poll(void);
static void  segtrace_input_poll(void);
static void  auto_z_spam_input_poll(void);
static void  segtrace_ct_cb(uint32_t lo, uint32_t hi, void *user);
static int   capture_frame_is_listed(uint32_t frame);

/* Cross-process singleton lock. A second openrecet instance trying to
 * boot while one is already running refuses to start so it can't
 * clobber save state, double-grab DirectInput, or leave behind orphan
 * windows that confuse the next test run.
 *
 * Held for the process lifetime via the kernel mutex object — when the
 * process exits (clean, crash, TerminateProcess, supervisor-job reap),
 * the kernel releases all handles and the next launch succeeds. We
 * intentionally never CloseHandle it.
 *
 * Global\\ namespace so the check is per-Windows-host, not per-WSL-
 * session: two `wsl bash` terminals on the same host count as a
 * conflict, which is what we want.
 *
 * Bypassed by --no-singleton or OPENRECET_NO_SINGLETON=1 in the env. */
#define OPENRECET_SINGLETON_MUTEX_NAME "Global\\openrecet-singleton"
static HANDLE g_singleton_mutex = NULL;

static void singleton_acquire_or_die(void)
{
    if (g_no_singleton) return;
    if (GetEnvironmentVariableA("OPENRECET_NO_SINGLETON", NULL, 0) > 0) {
        g_no_singleton = 1;
        return;
    }

    SetLastError(0);
    g_singleton_mutex = CreateMutexA(NULL, FALSE,
                                     OPENRECET_SINGLETON_MUTEX_NAME);
    DWORD err = GetLastError();

    if (g_singleton_mutex == NULL) {
        /* Couldn't even create the kernel object — most likely a
         * non-fatal permission/namespace edge case (e.g. Global\\
         * denied on a locked-down host). Warn and proceed; we'd rather
         * boot than refuse on a misconfigured but benign system. */
        fprintf(stderr,
                "openrecet: CreateMutex(singleton) failed (err=%lu); "
                "running without singleton guard\n",
                (unsigned long)err);
        return;
    }

    if (err == ERROR_ALREADY_EXISTS) {
        const char *msg =
            "Another openrecet instance is already running.\n\n"
            "Refusing to start a second copy (would clobber save state "
            "and leak orphan windows during test iteration).\n\n"
            "Close the other instance first, or pass --no-singleton "
            "(or set OPENRECET_NO_SINGLETON=1) to bypass.";
        fprintf(stderr, "openrecet: singleton conflict — %s\n", msg);
        /* Skip the modal MessageBox in test mode (no human to dismiss
         * it). --max-duration-ms is the canonical "harness running"
         * signal; same gate the sprite_load failure path uses. */
        if (g_max_duration_ms == 0) {
            MessageBoxA(NULL, msg, "openrecet", MB_ICONERROR | MB_OK);
        }
        ExitProcess(2);
    }
}

/* save_bank header-init hook. Engine calls FUN_00499583
 * (= audio_fade_apply(BGM)) once during FUN_004901c2 when the shared
 * header magic flips from 0 → live; we forward that callback through
 * the existing audio_fade apply hook so save_bank doesn't need a
 * direct dependency on audio.c. */
static void save_bank_apply_bgm_via_audio_fade(void)
{
    audio_fade_apply(AUDIO_FADE_CHANNEL_BGM);
}

/* C8e.smoke — registered as scene1_preload's post-house callback when
 * --force-pass-d-mesh is set.  Fires once per HOUSE entry, AFTER
 * scene1_preload_house's mesh_tex_cache_reset() + foreground sprite
 * loads have settled.  Releases any prior loaded mesh (HOUSE entry can
 * happen multiple times; each fresh load needs to register its own
 * texture cache slots) and re-loads from g_force_pass_d_mesh_path. */
static void force_pass_d_mesh_reload(void)
{
    if (g_force_pass_d_mesh) {
        scene1_shop_walker_set_pass_d_mesh(NULL);
        mesh_free(g_force_pass_d_mesh);
        g_force_pass_d_mesh = NULL;
    }
    if (!g_force_pass_d_mesh_path) return;

    g_force_pass_d_mesh = mesh_load(g_force_pass_d_mesh_path, -1);
    if (!g_force_pass_d_mesh) {
        fprintf(stderr, "openrecet: force-pass-d-mesh load failed: %s\n",
                g_force_pass_d_mesh_path);
        return;
    }
    if (g_force_pass_d_mesh->error[0]) {
        fprintf(stderr, "openrecet: force-pass-d-mesh build error in %s: %s\n",
                g_force_pass_d_mesh_path, g_force_pass_d_mesh->error);
        mesh_free(g_force_pass_d_mesh);
        g_force_pass_d_mesh = NULL;
        return;
    }
    HRESULT hr = mesh_load_finalize_win32(g_force_pass_d_mesh, g_dev);
    if (FAILED(hr)) {
        fprintf(stderr,
                "openrecet: force-pass-d-mesh finalize failed: 0x%08lx\n",
                (unsigned long)hr);
        mesh_free(g_force_pass_d_mesh);
        g_force_pass_d_mesh = NULL;
        return;
    }
    fprintf(stderr,
            "force-pass-d-mesh: %s reloaded (verts=%d idx=%d "
            "submeshes=%d materials=%d)\n",
            g_force_pass_d_mesh_path,
            g_force_pass_d_mesh->vertex_count,
            g_force_pass_d_mesh->index_count,
            g_force_pass_d_mesh->submesh_count,
            g_force_pass_d_mesh->material_count);
    scene1_shop_walker_set_pass_d_mesh(g_force_pass_d_mesh);
}

/* Cchr.2h — post-house dispatcher.  scene1_preload owns a single post-house
 * callback slot (fires once per HOUSE entry, after the mesh-cache reset +
 * foreground sprite loads).  Always registered; routes the optional
 * --force-pass-d-mesh reload AND the default standing-player pose setup
 * (position + the player_ctrl actor model) so sw_pass_light draws Recette by
 * default.  The party walking-sprite sheets (player 0, companion 1, guest 2)
 * are now loaded at boot by scene1_preload_chr_party_sheets() — the engine's
 * FUN_00472f5d "read systemtex" point (§72) — not here.  --no-chr-player
 * suppresses the pose so the actor model's char id stays -1 → the draw gate
 * skips it (the sheets stay resident, harmless, exactly as in the engine). */
static void post_house_hook(void)
{
    if (g_force_pass_d_mesh_path)
        force_pass_d_mesh_reload();
    if (!g_no_chr_player)
        scene1_postload_pose_house_standing();
}

/* Parse a --force-player-sprite inject file.  Line-based, tolerant of
 * blank/`#` lines and any key order:
 *
 *   char_id <int>
 *   color   <hex u32>
 *   tex     <w> <h>
 *   world   <16 floats, row-major>
 *   actor   <17 ints>          (the param_1 sprite-state struct dwords)
 *   sheet   <name|->           (optional; bound via sprite_load, "-"=none)
 *
 * Returns 1 if at least char_id + tex + world + actor were seen. */
static int force_player_sprite_load_inject(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "openrecet: force-player-sprite: cannot open %s\n", path);
        return 0;
    }
    int have_char = 0, have_tex = 0, have_world = 0, have_actor = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char key[32];
        if (sscanf(line, "%31s", key) != 1) continue;
        if (key[0] == '#') continue;
        const char *rest = line + strlen(key);
        if (strcmp(key, "char_id") == 0) {
            if (sscanf(rest, "%d", &g_fps_char_id) == 1) have_char = 1;
        } else if (strcmp(key, "color") == 0) {
            unsigned int c;
            if (sscanf(rest, "%x", &c) == 1) g_fps_color = (uint32_t)c;
        } else if (strcmp(key, "tex") == 0) {
            if (sscanf(rest, "%d %d", &g_fps_tex_w, &g_fps_tex_h) == 2)
                have_tex = 1;
        } else if (strcmp(key, "world") == 0) {
            int n = 0; const char *p = rest;
            for (; n < 16; n++) {
                int adv = 0;
                if (sscanf(p, "%f%n", &g_fps_world[n], &adv) != 1) break;
                p += adv;
            }
            if (n == 16) have_world = 1;
        } else if (strcmp(key, "actor") == 0) {
            int n = 0; const char *p = rest;
            for (; n < 0x11; n++) {
                int adv = 0, v = 0;
                if (sscanf(p, "%d%n", &v, &adv) != 1) break;
                g_fps_actor[n] = (int32_t)v;
                p += adv;
            }
            if (n == 0x11) have_actor = 1;
        } else if (strcmp(key, "sheet") == 0) {
            char s[160];
            if (sscanf(rest, "%159s", s) == 1)
                lstrcpynA(g_fps_sheet, s, (int)sizeof(g_fps_sheet));
        }
    }
    fclose(f);
    if (!(have_char && have_tex && have_world && have_actor)) {
        fprintf(stderr, "openrecet: force-player-sprite: %s missing required "
                "keys (char_id/tex/world/actor)\n", path);
        return 0;
    }
    fprintf(stderr, "force-player-sprite: char_id=%d color=0x%08x tex=%dx%d "
            "anim=%d frame=%d facing=%d age=%d sheet=%s\n",
            g_fps_char_id, (unsigned)g_fps_color, g_fps_tex_w, g_fps_tex_h,
            g_fps_actor[CHR_ACTOR_ANIM], g_fps_actor[CHR_ACTOR_FRAME],
            g_fps_actor[CHR_ACTOR_FACING], g_fps_actor[CHR_ACTOR_AGE],
            g_fps_sheet[0] ? g_fps_sheet : "(none)");
    return 1;
}

/* ─── WinMain — mirrors FUN_0047bfb3 ─────────────────────────────────────── */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrev;

    g_hInstance = hInst;
    /* Line-buffer stdio so each fprintf surfaces immediately. Only the
     * console-subsystem build (`openrecet-debug.exe` — see src/Makefile)
     * has stdio wired to anything visible; the GUI build silently drops
     * writes because `-mwindows` strips the inherited stdin/out/err
     * handles. Use the debug binary for interactive log inspection. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    /* Source files are UTF-8, so `—`/`→`/`⚠` in log strings emit as UTF-8
     * bytes. Default Windows consoles (cmd/PowerShell with no locale tweak)
     * decode stderr as CP437/CP932 and mojibake them ("ΓÇö" etc.). Tell the
     * console to interpret bytes as UTF-8 — no-op for the GUI build, which
     * has no attached console, and harmless when invoked under WSLInterop. */
    SetConsoleOutputCP(CP_UTF8);

    parse_cmdline(lpCmdLine);

    /* Install MessageBox-to-stderr redirector BEFORE singleton check
     * (whose conflict path itself pops a MessageBoxA) and BEFORE any
     * window / D3D / DInput / DSound init (so init-failure popups from
     * those subsystems get caught too).  Off via --no-msgbox-hook. */
    if (!g_no_msgbox_hook) {
        msgbox_install_global_hook();
    }

    /* Refuse to start a second instance — prevents test iterations
     * from being silently shadowed by a stray previous run. Must run
     * after parse_cmdline so --no-singleton is honoured, but before
     * any window / D3D / save-file init so a refusal is cheap and
     * side-effect-free. */
    singleton_acquire_or_die();

    /* High-resolution timer (matches the original's TIMECAPS dance). */
    TIMECAPS tc;
    timeGetDevCaps(&tc, sizeof(tc));
    timeBeginPeriod(tc.wPeriodMin);

    /* "very early init" — FUN_00451790. Seeds camera/projection matrices
     * and a 100-particle table from the deterministic boot RNG (seed=1).
     * Runs before the RNG-from-time reseed below — the engine relies on
     * that ordering so the particle table is identical every boot. */
    prewindow_init();

    /* "rng reseed" — FUN_005045eb → FUN_00471050 → FUN_005041ec.
     * Replaces the boot seed with one derived from wall-clock time so
     * subsequent rand calls during gameplay are non-deterministic.
     *
     * Phase A harness override: --rng-seed pins the seed (skipping the
     * time-derived path) so the title BG scroll + cursor pulse stay
     * frame-identical across replays. */
    if (g_rng_seed_set) {
        rng_seed(g_rng_seed_value);
    } else {
        rng_seed_from_now();
    }

    timeBeginPeriod(10);

    /* FUN_0047aa30 is a 1-byte empty stub in the original (return;) —
     * presumably a removed log call between init phases. Intentionally
     * omitted here. */

    /* "pre-window init" — FUN_0047a474. Must run before create_main_window:
     * `screen` selects the requested back-buffer size, `winmode` selects
     * windowed vs fullscreen.
     *
     * Path resolution: the engine looks next to the exe via _splitpath(argv[0]).
     * Our dev workflow puts the exe in build/ and data (including recet.ini)
     * in vendor/original/ — and runs from inside vendor/original/ so
     * storage_init's relative lookups for lnkdatas.bin work. So we look in
     * CWD first, then fall back to next-to-exe for the eventual deployment
     * shape where openrecet.exe sits alongside the data files. */
    {
        FILE *probe = fopen("recet.ini", "rb");
        if (probe) {
            fclose(probe);
            recet_ini_load("recet.ini", &g_ini);
        } else {
            char ini_path[MAX_PATH];
            if (recet_ini_default_path(ini_path, sizeof ini_path) == 0) {
                recet_ini_load(ini_path, &g_ini);
            } else {
                recet_ini_set_defaults(&g_ini);
            }
        }
        g_windowed = (g_ini.winmode == 1);
        mesh_load_set_easydisp(g_ini.easydisp);
        fprintf(stderr,
            "recet.ini: winmode=%d screen=%d (%dx%d) se=%d mu=%d\n",
            g_ini.winmode, g_ini.screen, g_ini.width, g_ini.height,
            g_ini.se, g_ini.mu);
    }

    if (!create_main_window(hInst, nCmdShow)) {
        return 0;
    }

    /* "init strage ok" — FUN_004341fe (lnkdatas loader). MessageBoxA
     * already shown on failure inside storage_init. */
    if (!storage_init()) {
        return 0;
    }

    /* Cchr.2a character-sprite loaders (engine FUN_004341fe tail +
     * FUN_00479f78): load g_chr_formdata + the per-char descriptors so the
     * billboard leaf has real cell data before the first HOUSE render.
     * Cchr.2h: the standing player draws by default, so these load by
     * default too (suppressed only by --no-chr-player).  --force-player-
     * sprite still pulls them for its captured-matrix inject path. */
    if (g_force_player_sprite || !g_no_chr_player) {
        int fd = chr_formdata_load();
        int nidx = chr_meta_load();
        fprintf(stderr, "chr-sprite loaders: chr_formdata_load=%d "
                "chr_meta_load parsed %d idx files\n", fd, nidx);
        if (g_force_player_sprite) {
            g_fps_loaded = force_player_sprite_load_inject(g_fps_inject_path);
            if (!fd || !g_fps_loaded)
                fprintf(stderr, "force-player-sprite: disabled (formdata=%d "
                        "inject=%d)\n", fd, g_fps_loaded);
        }
        if (!g_no_chr_player && !fd)
            fprintf(stderr, "chr-player: disabled (formdata load failed)\n");
    }

    /* TODO "init print ok"   — FUN_00451863 */

    if (!load_d3d8() || !init_render(g_hwnd)) {
        MessageBoxA(g_hwnd, "Failed to initialize Direct3D 8",
                    "openrecet", MB_OK | MB_ICONERROR);
        return 0;
    }
    /* "init start" — section marker logged by FUN_0047ac6a on success. */

    /* D.5: open trace file + remember which device to wrap.  Wrap is at
     * the call-site macro level (see d3d_trace_macros.h, injected via
     * -include in src/Makefile), so there's no vtable hot-patch here.
     * d3d_trace_install just notes the device pointer so the wrappers
     * can validate `p == g_dev` quickly. */
    if (g_d3d_trace_path) {
        d3d_trace_init_from_cli(g_d3d_trace_path,
                                g_d3d_trace_frames_count > 0
                                    ? g_d3d_trace_frames : NULL,
                                (size_t)g_d3d_trace_frames_count);
        d3d_trace_install(g_dev);
    }

    /* E.2: open call_trace file.  No device pointer needed — the probes
     * are direct CALL_TRACE_ENTER() macros sprinkled through src/. */
    if (g_call_trace_path) {
        call_trace_init_from_cli(g_call_trace_path,
                                 g_call_trace_frames_count > 0
                                     ? g_call_trace_frames : NULL,
                                 (size_t)g_call_trace_frames_count);
    }

    /* "init dinput ok" — FUN_0047af52 — keyboard + up to 4 joysticks.
     * Skipped under --input-trace-replay: a replay drives
     * g_input_state[0].buttons directly from the trace file, so we
     * don't want live keypresses leaking into the simulated frame. */
    if (!input_harness_driven()) {
        input_init(g_hInstance, g_hwnd);
    }

    /* Flatten recet.ini pad/skill into the engine's per-controller
     * binding-block layout that input_poll walks each frame. Must run
     * after recet_ini_load + input_init (the latter sets g_joy_count
     * which the poll loop guards on). */
    input_bindings_load(&g_ini);

    /* "init render ok" — FUN_00454e69 — fan out caps + back-buffer-desc
     * to the 24 engine render-layer objects. Must run after the device
     * exists; original ordering puts it right after DInput init. */
    layers_init(g_d3d, g_dev);

    if (g_show_sprite_name) {
        if (!sprite_load(g_dev, g_show_sprite_name, 0, 0, &g_show_sprite)) {
            /* Skip the modal MessageBox under --max-duration-ms so smoke
             * harnesses don't block on a dialog that has no human to
             * dismiss it; emit to stderr instead. */
            if (g_max_duration_ms == 0) {
                MessageBoxA(g_hwnd, g_show_sprite_name,
                            "openrecet: sprite_load failed", MB_OK | MB_ICONERROR);
            } else {
                fprintf(stderr, "openrecet: sprite_load failed: %s\n",
                        g_show_sprite_name);
            }
            return 0;
        }
    }

    /* --show-mesh: C7a visual smoke. mesh_load handles storage_read +
     * xfile_parse + mesh_build + bounds + texture-cache dedupe;
     * finalize_win32 then uploads VB/IB + sprite_loads every texture.
     * Failure paths print to stderr and skip the draw — we deliberately
     * keep the rest of boot alive so a bad --show-mesh path doesn't
     * brick the title scene. */
    if (g_show_mesh_path) {
        g_show_mesh = mesh_load(g_show_mesh_path, -1);
        if (!g_show_mesh) {
            fprintf(stderr, "openrecet: mesh_load failed: %s\n",
                    g_show_mesh_path);
        } else if (g_show_mesh->error[0]) {
            fprintf(stderr, "openrecet: mesh build error in %s: %s\n",
                    g_show_mesh_path, g_show_mesh->error);
            mesh_free(g_show_mesh);
            g_show_mesh = NULL;
        } else {
            HRESULT hr = mesh_load_finalize_win32(g_show_mesh, g_dev);
            if (FAILED(hr)) {
                fprintf(stderr, "openrecet: mesh_load_finalize_win32 failed: 0x%08lx\n",
                        (unsigned long)hr);
                mesh_free(g_show_mesh);
                g_show_mesh = NULL;
            } else {
                fprintf(stderr,
                        "show-mesh: %s loaded (verts=%d idx=%d submeshes=%d "
                        "materials=%d centroid=(%.2f, %.2f, %.2f) radius=%.2f)\n",
                        g_show_mesh_path,
                        g_show_mesh->vertex_count, g_show_mesh->index_count,
                        g_show_mesh->submesh_count, g_show_mesh->material_count,
                        g_show_mesh->centroid[0], g_show_mesh->centroid[1],
                        g_show_mesh->centroid[2], g_show_mesh->radius);
            }
        }
    }

    /* --house-preview: C7d throwaway. Same shape as --show-mesh above,
     * just a separately-tracked mesh that render_dispatch's INGAME branch
     * swaps in for the real scene-1 render chain. Failures here are
     * fatal-to-the-preview but not to boot — fall back to the chain.
     *
     * --house-preview-dump <dir> swaps the mesh_load call for a direct
     * VB+IB read from a retail-format dump dir (vb.bin + ib.bin). This
     * lets us A/B test render vs parser: if retail's bytes render
     * correctly through our pipeline but our parser's output doesn't,
     * the bug is in our parser; if both look the same, parser is fine
     * and any remaining divergence is in the render path. */
    if (g_house_preview && g_house_preview_dump) {
        g_house_preview_mesh = house_preview_load_dump(g_house_preview_dump, g_dev);
        if (!g_house_preview_mesh) {
            fprintf(stderr, "openrecet: house-preview-dump load failed: %s\n",
                    g_house_preview_dump);
        }
    } else if (g_house_preview) {
        g_house_preview_mesh = mesh_load(g_house_preview_path, -1);
        if (!g_house_preview_mesh) {
            fprintf(stderr, "openrecet: house-preview mesh_load failed: %s\n",
                    g_house_preview_path);
        } else if (g_house_preview_mesh->error[0]) {
            fprintf(stderr, "openrecet: house-preview build error in %s: %s\n",
                    g_house_preview_path, g_house_preview_mesh->error);
            mesh_free(g_house_preview_mesh);
            g_house_preview_mesh = NULL;
        } else {
            HRESULT hr = mesh_load_finalize_win32(g_house_preview_mesh, g_dev);
            if (FAILED(hr)) {
                fprintf(stderr, "openrecet: house-preview finalize failed: 0x%08lx\n",
                        (unsigned long)hr);
                mesh_free(g_house_preview_mesh);
                g_house_preview_mesh = NULL;
            } else {
                fprintf(stderr,
                        "house-preview: %s loaded (verts=%d idx=%d submeshes=%d "
                        "materials=%d centroid=(%.2f, %.2f, %.2f) radius=%.2f)\n",
                        g_house_preview_path,
                        g_house_preview_mesh->vertex_count,
                        g_house_preview_mesh->index_count,
                        g_house_preview_mesh->submesh_count,
                        g_house_preview_mesh->material_count,
                        g_house_preview_mesh->centroid[0],
                        g_house_preview_mesh->centroid[1],
                        g_house_preview_mesh->centroid[2],
                        g_house_preview_mesh->radius);
            }
        }
    }

    /* --force-pass-d-mesh: C8e.bridge visual smoke.  Loads one .x file
     * via mesh_load + mesh_load_finalize_win32 and hands it to
     * scene1_shop_walker_set_pass_d_mesh so sw_pass_d's per-record call
     * to scene1_emit_record draws this mesh at the per-record world
     * matrix.  Combined with --force-ambient-spawn --ambient-spawn-type
     * 0x79 (or 0x74 / 0x96), the production spawn pipeline populates
     * table A records that trip the Pass D filter and produce visible
     * draws.  Off by default — Pass D stays dormant (matches engine
     * HOUSE behavior).
     *
     * C8e.smoke ordering: scene1_preload_house_cb runs
     * mesh_tex_cache_reset() on every HOUSE entry, which wipes any
     * boot-time cache slots.  If we loaded the mesh at boot, by the
     * time Pass D fires its texture_slots[] indices point at stale
     * cache rows (or past the new count).  Instead we register
     * force_pass_d_mesh_reload as scene1_preload's post-house hook —
     * the hook fires AFTER the reset + after the HOUSE foreground
     * loads, so the mesh's texture slots land in fresh cache rows
     * the next sw_pass_d frame can resolve correctly. */
    /* Cchr.2h: always register the post-house hook — it now owns the default
     * standing-player setup (sheet + pose) on top of the optional
     * --force-pass-d-mesh reload.  --no-chr-player gates the player half. */
    scene1_preload_set_post_house_callback(post_house_hook);

    /* --debug-pass-d-unlit: see flag comment above.  Wire the parsed
     * value into the walker before the first render tick. */
    if (g_debug_pass_d_unlit) {
        scene1_shop_walker_set_debug_pass_d_unlit(1);
        fprintf(stderr,
                "debug-pass-d-unlit: forcing Pass D state to "
                "SELECTARG1+DIFFUSE (engine state overridden)\n");
    }

    /* "init indexfile ok" — FUN_00475270 — gameplay-table loader.
     * Currently skeleton only: dispatcher fires all 14 storage reads
     * (proving the assets resolve) but the per-file parsers are stubs
     * that just log the size. Parsers land one per commit in Phase B
     * — see docs/findings/tables-loader.md. */
    tables_load_all();

    /* "init fontsys ok" — FUN_0047c228 — clear the 200-slot LRU cache
     * + parallel texture-pointer table, seed default font height. */
    font_init();
    fprintf(stderr, "font: cache initialized (%d slots)\n", FONT_SLOT_COUNT);

    /* 2D quad batcher — one-time vbuf prefill + screen-width-scale. */
    render_quad_init((uint32_t)g_ini.width);

    /* Title-scene bootstrap — mirrors FUN_0047b29e first two writes:
     * snap scene_state back to 0 (TITLE) after prewindow_init left it
     * at 1, then load assets + seed menu/anim. The rest of
     * FUN_0047b29e (FUN_00452917 / FUN_00474e7a / FUN_00453373 et al)
     * lands as their target ports come online. */
    scene_state_set_title();

    /* "read titletex ok" — FUN_004733d5 — load the 7 title-scene
     * textures (bg2, 01, fuki, waku + pause/result/dungeon). Sets
     * g_scene_title_assets_loaded on full success. */
    (void)scene_title_load_assets(g_dev);

    /* Overlay table parser (O.10) — FUN_00474f4f called four times
     * from the tables-loader case 3 dispatch (engine all.c L76530-35).
     * Populates g_scene1_overlay_layer_count + filenames + the per-
     * shape UV/animation table from `ef/grpN.idx` (N=1..4).  Must run
     * before sysassets_load_all below: sysassets's per-layer sprite
     * loader (engine all.c L71673-83) reads the filename table the
     * parser writes.  Dormant in the dispatcher until a spawn caller
     * fires, but the table now matches engine state for HOUSE. */
    (void)scene1_overlay_table_load_all();

    /* Parent template table loader (PFO.7) — engine FUN_00412a89's
     * file-loading loop, called from the same `case 3` dispatch inside
     * FUN_00475270 right after the four overlay-table parser calls.
     * Reads `ef/effect{1..4}.dat` and copies each file's parent-template
     * chunk (bytes 17200..55199) into g_scene1_pfo_parent_table.  This
     * is the binary blob the Table A tick (PFO.5a) reads to drive
     * `scene1_overlay_spawn` calls per inner sub-record.  Dormant in
     * HOUSE today — no PFO.6 allocator consumer fires until per-stage
     * particle event triggers port. */
    (void)scene1_pfo_parent_table_load_all();

    /* System overlay textures — FUN_00472f5d called from FUN_0047b29e
     * L233 right after FUN_0049a3a3 / FUN_00434dbf / FUN_00491b3f.
     * Loads the ~30 textures every UI overlay consumes (nowloading,
     * save/data/item windows, character portraits, HP/MP gauges,
     * status effects, per-category item icon pages). Wired here even
     * though our scene_title_load_assets call doesn't match the
     * engine's exact ordering — both fire before the first
     * scene_title render, which is all that matters for boot. */
    (void)sysassets_load_all(g_dev);

    /* Register the AE8 + B13 + B3E + B82 + BC6 + C4E + C96 secondary-
     * worker inner bodies (engine FUN_0047329b buy-phase page-0
     * inventory loader + FUN_0047333b buy-phase current-page loader +
     * FUN_0047474e wall loader + FUN_004747dc floor loader +
     * FUN_0047486a jutan/rug loader + FUN_00473a3e pause+status asset
     * loader plus the unnamed 0x435873 FPU init that precedes it +
     * FUN_004735ad world-map BMP loader). scene_buy_init registers
     * BOTH AE8 and B13 in one call (sibling bodies sharing the same
     * per-page state arrays). scene_worldmap_init wires only the
     * BMP-loader HALF of C96 — the engine's FUN_0049de20 state-machine
     * pre-call is deferred (deep INGAME world-map deps). None fire
     * until something calls the matching spawner
     * (worker_load_spawn_d3e(0) for AE8 / _d3e(non-zero) for B13 /
     * _d85 / _dc1 / _dfd / _e75 / _eb1 respectively), which the
     * scene-1 stage transition will do once it ports. */
    scene_buy_init(g_dev);
    scene_walls_init(g_dev);
    scene_floor_init(g_dev);
    scene_jutan_init(g_dev);
    scene_pause_init(g_dev);
    scene_worldmap_init(g_dev);
    scene_table_init(g_dev);
    scene_sc1_init(g_dev);

    /* C7c: seed per-stage runtime state for stage 0 (HOUSE / shop).
     * Writes the four scene-1 prop selectors to engine fresh-game
     * defaults (all zero — picks the starter wall / floor / rug /
     * table from each module's slot-0 entry). Idempotent. Must run
     * after the scene_*_init calls above so a future test that
     * resets+re-seeds works. The load chain (worker_load_spawn) is
     * still NOT kicked yet — that's C7e (FUN_00474a9a port). */
    stage_init_house();

    /* C7d: point g_stage_palette at the stage-0 HOUSE palette record
     * (engine analog: DAT_068dd2f0). Fields scene-1 reads (mode,
     * gravity vec, lighting flags, clear color) all stay at zero —
     * matches engine BSS-zero defaults for HOUSE. The clear color
     * path in FUN_004547ab and the lighting/fog state in FUN_00459dfd
     * will both read from this record once their porters land. */
    stage_palette_init_house();

    /* Apply --force-ambient-spawn / --ambient-spawn-type /
     * --ambient-spawn-pose overrides before the worker fires
     * scene1_preload_house.  All three are no-ops unless their CLI
     * flags were given. */
    if (g_force_ambient_spawn) {
        scene1_postload_set_force_ambient(1);
    }
    if (g_ambient_spawn_type_override >= 0) {
        scene1_postload_set_ambient_type_override(g_ambient_spawn_type_override);
    }
    if (g_ambient_spawn_pose_set) {
        scene1_postload_set_ambient_pose_override(1,
                                                  g_ambient_spawn_pose[0],
                                                  g_ambient_spawn_pose[1],
                                                  g_ambient_spawn_pose[2]);
    }

    /* C8j.fin.c — table C smoke wiring.  Applied before the worker
     * fires scene1_preload_house so the runner sees the overrides on
     * the first HOUSE entry.  Defaults (-1 / -1) make
     * scene1_postload_smoke_c_spawn() a no-op (engine HOUSE default). */
    if (g_force_c_pickup_type >= 0) {
        scene1_postload_set_force_c_pickup_type(g_force_c_pickup_type);
    }
    if (g_force_c_world_drop_type >= 0) {
        scene1_postload_set_force_c_world_drop_type(g_force_c_world_drop_type);
        scene1_postload_set_force_c_world_drop_count(g_force_c_world_drop_count);
        scene1_postload_set_force_c_world_drop_mag(g_force_c_world_drop_mag);
    }

    /* C8j.fin.b — table B smoke wiring. */
    if (g_force_b_npc_type >= 0) {
        scene1_postload_set_force_b_npc_type(g_force_b_npc_type);
    }
    if (g_force_b_entity_type >= 0) {
        scene1_postload_set_force_b_entity_type(g_force_b_entity_type);
    }

    /* Cf — phase-2 walker writer inputs.  De-MVP'd: scene1_preload_house
     * now calls scene1_postload_load_house_phase2_inputs() unconditionally,
     * sourcing scene_type / ivar8 / stage_positions / char_mode from real
     * engine state (the stage selector + the seeded per-save-slot record),
     * so HOUSE furniture renders with NO flag.  `--force-walker-phase2 N`
     * is now only a test override for the synthetic scene_type tiers 1..4
     * (N<0, the default, leaves the real HOUSE value 0). */
    if (g_force_walker_phase2_scene_type >= 0) {
        scene1_postload_set_house_scene_type_override(
            g_force_walker_phase2_scene_type);
    }

    /* Cchr.2h: the standing player now draws by default — seeded by the
     * post-house hook (sheet load + scene1_postload_pose_house_standing),
     * reading the real engine-global actor model in sw_pass_light.  The old
     * --force-chr-walker per-call inject is retired; the flag is still parsed
     * (now a no-op, since char billboards are default) so existing run
     * scripts don't error.  --no-chr-player suppresses the default draw. */

    /* Cc.1: initialise scene-1 camera state.  Sets the first-frame
     * snap flag so the first scene1_render_camera_setup pass writes a real
     * eye/lookat, and sets the faithful compose-add constants (14/21/-1.8).
     * The HOUSE bias stand-in + char_mode are applied per HOUSE entry by
     * scene1_preload_house (load_house_phase2_inputs + the bias apply). */
    scene1_camera_init();

    /* C8jb.fin — wire scene1_combat_sm_tick as the records_b_tick
     * state-machine hook.  This is the engine's FUN_0043865e (Mt.
     * Everest #2) port.  No observable change in HOUSE today: table B
     * is BSS-zero (C8j allocator unwired without smoke flags), so every
     * slot is dead and the SM never fires.  Smoke flags
     * (--force-b-npc / --force-b-entity) spawn slots that exercise the
     * SM through the integrator's state_machine_call(_ret) helpers. */
    scene1_combat_sm_install();

    /* C7e: wire FUN_00474a9a (scene-1 pre-load entry, HOUSE branch) as
     * the worker_load slot-1 INGAME callback. After title fade-out,
     * scene_post_fade_init → worker_load_spawn() picks the INGAME
     * slot and now actually fires scene-1 asset loads:
     *   - leve_win + mood_para singleton sprites
     *   - 21-entry chr portrait loop (dormant w/h until chara state lands)
     *   - 4× foreground walls/floor/jutan/table selector loads
     * Variant-set loading (the OTHER 14 walls, 14 floors, etc.) waits
     * on the secondary spawners — they fire from stage-change paths
     * that haven't ported yet. */
    scene1_preload_init(g_dev);

    /* Build the menu items table (FUN_0049a43d). Fresh boot = no
     * saves; 4 items: New Game / Ranking / Options / Exit. Then seed
     * the title sim state the same way FUN_0049a3a3 ("bootstrap done")
     * does, and park the cursor on the default item. */
    scene_title_menu_init_fresh(&g_scene_title_menu);
    scene_title_anim_init_fresh(&g_scene_title_anim);
    g_scene_title_anim.cursor_pos =
        (uint32_t)g_scene_title_menu.default_cursor;
    sim_init();
    music_init();

    /* Open the audio trace file (opt-in via --audio-trace) BEFORE
     * audio_init so the boot-time anchor lines up with the first
     * PlaySegmentEx; the first BGM swap that follows logs t_ms close
     * to zero. Non-fatal if the path is bad. */
    if (g_audio_trace_path && audio_trace_open(g_audio_trace_path)) {
        fprintf(stderr, "openrecet: audio trace → %s\n", g_audio_trace_path);
    } else if (g_audio_trace_path) {
        fprintf(stderr, "openrecet: failed to open audio trace %s\n",
                g_audio_trace_path);
    }

    /* Input-trace record: open the file before the first input_poll
     * fires so we never miss the seed line. */
    if (g_input_trace_record_path) {
        if (input_trace_record_open(g_input_trace_record_path)) {
            fprintf(stderr, "openrecet: input trace recording → %s\n",
                    g_input_trace_record_path);
        } else {
            fprintf(stderr,
                "openrecet: failed to open input trace for recording %s\n",
                g_input_trace_record_path);
        }
    }

    /* Anchor-trace record: open the file sink before the loop so the
     * BOOT anchor (first ticked frame) is captured.  Failure is
     * non-fatal — the stderr echo still emits the stream. */
    if (g_anchor_trace_record_path) {
        g_anchor_record_fp = fopen(g_anchor_trace_record_path, "w");
        if (g_anchor_record_fp) {
            fprintf(stderr, "openrecet: anchor trace recording → %s\n",
                    g_anchor_trace_record_path);
        } else {
            fprintf(stderr,
                "openrecet: failed to open anchor trace for recording %s "
                "(stderr echo still active)\n",
                g_anchor_trace_record_path);
        }
    }

    /* Player-pos log: open the per-frame world-position sink.  Non-fatal. */
    if (g_player_pos_log_path) {
        g_player_pos_log_fp = fopen(g_player_pos_log_path, "w");
        if (g_player_pos_log_fp) {
            fprintf(stderr, "openrecet: player-pos log → %s\n",
                    g_player_pos_log_path);
        } else {
            fprintf(stderr,
                "openrecet: failed to open player-pos log %s\n",
                g_player_pos_log_path);
        }
    }

    /* Input-trace replay: parse the file now (one shot at boot — the
     * file is small and lookups are binary search at runtime). */
    if (g_input_trace_replay_path) {
        if (input_trace_load(g_input_trace_replay_path, &g_replay_trace)) {
            fprintf(stderr,
                "openrecet: input trace replaying ← %s (%zu entries)\n",
                g_input_trace_replay_path, g_replay_trace.count);
        } else {
            fprintf(stderr,
                "openrecet: failed to load input trace %s — replay disabled\n",
                g_input_trace_replay_path);
            input_trace_free(&g_replay_trace);   /* drop any partial table */
            g_input_trace_replay_path = NULL;
        }
    }

    if (g_input_segtrace_path) {
        if (input_segtrace_load(g_input_segtrace_path, &g_segtrace)) {
            fprintf(stderr,
                "openrecet: input segtrace ← %s (%zu segments)\n",
                g_input_segtrace_path, g_segtrace.n_segs);
            /* Call-graph-trace ops drive the port's tracer directly: route
             * resolved windows to call_trace_arm_window via the callback, and
             * if the trace declares a window but no --call-trace path was
             * given, auto-open a default output so a bare replay just works. */
            input_segtrace_set_calltrace_cb(&g_segtrace, segtrace_ct_cb, NULL);
            if (input_segtrace_has_calltrace(&g_segtrace) &&
                !call_trace_is_open()) {
                static char ctpath[256];
                snprintf(ctpath, sizeof ctpath,
                         "openrecet-calltrace-%lu.jsonl",
                         (unsigned long)GetCurrentProcessId());
                call_trace_init_from_cli(ctpath, NULL, 0);
                fprintf(stderr,
                    "openrecet: call-trace auto-enabled from segtrace "
                    "calltrace op → %s\n", ctpath);
            }
        } else {
            fprintf(stderr,
                "openrecet: failed to load segtrace %s — disabled\n",
                g_input_segtrace_path);
            input_segtrace_free(&g_segtrace);
            g_input_segtrace_path = NULL;
        }
    }

    /* "init daoudio ok" — FUN_00498ef4 — DirectMusic 8: Performance +
     * Loader + BGM AudioPath + preload all 21 segments. Sound-effect
     * path (2 more AudioPaths + 27 resource-loaded WAVs) lands in the
     * next commit. On failure, log + continue silently — boot still
     * works, just without music. */
    if (!audio_init(g_hwnd)) {
        fprintf(stderr, "openrecet: audio_init failed — running muted\n");
    }

    /* --silent-audio: replace audio.c's SetVolume forwarder with one
     * that clamps every call to -10000 centibel. Game's audio code
     * (PlaySegmentEx, per-tick fade computation, etc.) still runs
     * normally — only the master attenuation forwarded to the
     * IDirectMusicAudioPath is pinned to silence. Audio is silent but
     * the engine state machine + trace events behave exactly as if
     * audio were playing. Mirrors the retail-side hook in
     * tools/frida/openrecet-agent.js installSilentAudioFromPath. */
    if (g_silent_audio) {
        audio_fade_set_apply_hook(silent_audio_apply_hook);
        fprintf(stderr,
            "audio: --silent-audio active — SetVolume clamped to -10000\n");
    }

    /* "load savefile ok" — FUN_004902fe. The full save-load (parse
     * save.dat into the arena) isn't ported yet, but its always-fires
     * side-effect IS: FUN_004901c2 = save_bank_init_all, which seeds
     * the shared header magic + slider defaults (9/5/9/1) and verifies
     * all 100 banks. Wires up the engine's FUN_00499583 callback via
     * an init-hook so save_bank doesn't link audio directly.
     *
     * Recet.ini's mu/se keys then overlay the engine defaults — the
     * user's preference takes precedence over the engine's 9/5 baseline
     * for as long as save-load isn't ported. Once save-load lands,
     * recet.ini becomes a fresh-init-only seed (engine behavior:
     * save-load overwrites everything, and ini sliders are only
     * consulted for the "no save file" path).
     *
     * audio_fade slider state is then synced from the save header so
     * the per-channel apply hook draws from one source of truth. */
    save_bank_set_header_init_hook(save_bank_apply_bgm_via_audio_fade);
    save_bank_init_all();
    fprintf(stderr,
            "save_bank: arena initialized (header magic=0x%08x, "
            "sliders se=%d bgm=%d se-b=%d slider3=%d)\n",
            save_header_magic(),
            save_header_get_se_slider(),
            save_header_get_bgm_slider(),
            save_header_get_se_b_slider(),
            save_header_get_slider3());

    /* Engine FUN_004902fe — try save.dat then _save.dat. If either is
     * readable, copies its contents into the arena and re-validates
     * each bank's checksum (re-init'ing any with bad checksum). If
     * neither file exists, the arena keeps the fresh state from
     * save_bank_init_all above.
     *
     * Engine cwd is the game install dir, where save.dat lives
     * alongside lnkdatas.bin. We use bare filenames so the engine's
     * cwd-resolution (storage_init's prior chdir) places us in the
     * same directory. */
    if (save_io_try_load("save.dat", "_save.dat")) {
        /* On a successful load, the audio sliders in the header may
         * differ from the fresh defaults — re-sync audio_fade and
         * re-apply the BGM volume so any music already playing picks
         * up the loaded levels. */
        audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM,
                              save_header_get_bgm_slider());
        audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A,
                              save_header_get_se_slider());
        audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_B,
                              save_header_get_se_b_slider());
        save_bank_apply_bgm_via_audio_fade();
    }

    /* Re-build the title menu with the (possibly loaded) save state.
     * has_any_score / has_any_adv_cleared / has_any_adv8_cleared /
     * hidden_char_unlocked drive which menu items appear — see
     * scene_title_menu_init for the gating logic. */
    {
        scene_title_save_t loaded_save;
        save_io_scan_for_title_menu(&loaded_save);
        scene_title_menu_init(&loaded_save, &g_scene_title_menu);
        g_scene_title_anim.cursor_pos =
            (uint32_t)g_scene_title_menu.default_cursor;
        fprintf(stderr,
                "save_io: title menu rebuilt — items=%d "
                "(adv_cleared=%d adv8=%d score=%d hidden=%d)\n",
                g_scene_title_menu.count,
                loaded_save.has_any_adv_cleared,
                loaded_save.has_any_adv8_cleared,
                loaded_save.has_any_score,
                loaded_save.hidden_char_unlocked);
    }

    /* Sync audio_fade sliders from the save header (one source of
     * truth). audio_fade owns the per-channel apply hook
     * (audio_fade_apply_hook_win32) and SetVolume timing.
     *
     * Until save-load landed, this block also overlaid recet.ini's
     * mu/se values on top of the engine defaults (9/5/9/1). The
     * overlay was a stand-in for save.dat-persisted sliders. With
     * save_io_try_load above, save.dat is now the authoritative
     * source — its sliders win regardless of recet.ini. (The engine
     * itself ignores recet.ini's mu/se at boot; we now match.) */
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM,  save_header_get_bgm_slider());
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, save_header_get_se_slider());
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_B, save_header_get_se_b_slider());
    fprintf(stderr,
            "audio: sliders seeded — bgm=%d se-a=%d se-b=%d "
            "(authoritative source: save_header)\n",
            audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM),
            audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A),
            audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B));

    /* FUN_0047c474 — the GDI atlas builder — is **dev-time only**.
     * The EN retail build ships its atlas (fontdata.bin + fontidx.bin)
     * INSIDE lnkdatas. The engine's FUN_0047c3a5 loader fopens the
     * cwd first (legacy path used during the original dev cycle), then
     * falls back to storage_read which finds the shipped atlas.
     *
     * Vendor config.idx has `/font:` commented, so DAT_073dfd00 never
     * raises and FUN_0047c474 never fires under normal play. The atlas
     * builder is essentially dead code in the shipped game — preserved
     * for fidelity in `src/font_atlas.c` but the runtime never calls
     * it. (Useful again if/when we port the JP version, which may
     * actively regen.)
     *
     * font_atlas_load below tries ./font/, then ./fontdata.bin, then
     * storage_read — the third hit is what the shipped EN game uses.
     * The shipped atlas was built on the original Japanese dev's
     * locale-correct Windows, with real kanji + correct metrics for
     * the engine's draw_text math. Our GDI regen on EN-locale Windows
     * produces visually-mangled output because GDI picks a different
     * font variant; documented as engine quirk in
     * docs/findings/engine-quirks.md §"Font atlas is shipped, not
     * regenerated". */

    /* "fontsystem ok" — FUN_0047c3a5 — pull fontdata.bin + fontidx.bin
     * back from disk into g_font_atlas. The slot allocator / glyph
     * uploader / draw_text consumers (next commits) read from there. */
    if (!font_atlas_load(NULL)) {
        fprintf(stderr,
            "font: atlas load failed — text rendering will be a no-op\n");
    }

    /* Wire the slot allocator's eviction hook to actually Release the
     * GPU texture. Pure-C font_alloc.c doesn't know about D3D so this
     * happens at the main.c seam. */
    g_font_alloc_release_cb = font_slot_release;

    /* TODO "read systemtex ok"  — FUN_00472f5d
     * TODO "load savefile ok"   — FUN_004902fe (parse save.dat;
     *      the always-fires side-effect, FUN_004901c2, is wired above
     *      via save_bank_init_all)
     * TODO "read titletex ok"   — FUN_0043609b
     * TODO bootstrap done       — FUN_0049a3a3 (enters main loop)
     */

    ShowWindow(g_hwnd, g_hidden ? SW_HIDE : nCmdShow);
    UpdateWindow(g_hwnd);

    if (g_max_duration_ms > 0) {
        SetTimer(g_hwnd, AUTO_EXIT_TIMER_ID, g_max_duration_ms, NULL);
    }

    /* --play-se: arm the first-fire timer. The WM_TIMER handler walks
     * g_play_se_slots[] and re-arms itself per interval until done. */
    if (g_play_se_count > 0) {
        fprintf(stderr,
                "openrecet: --play-se will fire %d slot%s starting at "
                "+%u ms (interval %u ms)\n",
                g_play_se_count, g_play_se_count == 1 ? "" : "s",
                g_play_se_after_ms, g_play_se_interval_ms);
        SetTimer(g_hwnd, PLAY_SE_TIMER_ID, g_play_se_after_ms, NULL);
    }

    /* ─── main loop — mirrors the PeekMessage/WaitMessage idle pattern ─── */
    MSG msg = {0};
    /* Game-tick callbacks. sim_b (FUN_0049966a, music selector) picks
     * a track each frame and dispatches a real DirectMusic swap via
     * the g_music_swap_fn bridge that audio_init installs (see
     * src/audio.{c,h} + src/music.{c,h}).
     *
     * Phase A harness overrides:
     *   --input-trace-replay sets input_poll = replay_input_poll, which
     *     bypasses DirectInput entirely and writes the recorded mask
     *     into g_input_state[0].buttons directly.
     *   --input-trace-record sets input_poll = recording_input_poll,
     *     which calls the real input_poll then records the post-poll
     *     mask.
     * The two are mutually exclusive (recording during replay would
     * just dump the trace back out); replay wins if both are passed. */
    void (*active_input_poll)(void) = input_poll;
    if (g_input_segtrace_path) {
        active_input_poll = segtrace_input_poll;
    } else if (g_input_trace_replay_path) {
        active_input_poll = replay_input_poll;
    } else if (g_input_trace_record_path) {
        active_input_poll = recording_input_poll;
    } else if (g_auto_z_spam) {
        active_input_poll = auto_z_spam_input_poll;
    }
    const struct tick_callbacks tick_cb = {
        .input_poll = active_input_poll,
        .sim_a      = sim_step_a,
        .sim_b      = music_step_default,
        .render     = render_dispatch,
    };
    tick_init();
    if (g_turbo) {
        /* Default step 17 ms = one 60 FPS frame budget rounded up.
         * Engine animations / fades that key off tick_now_ms stay
         * consistent with what they'd be at 60 FPS; only wall time
         * compresses (loop runs as fast as the host allows). */
        tick_set_turbo(1, 17);
        fprintf(stderr,
            "tick: --turbo active — virtual 17ms/frame, no Sleep\n");
    }
    /* "Already-logged" suppression for unimplemented title menu items.
     * One slot per SCENE_TITLE_MENU_* code (0..8). Without this, an
     * unimplemented selection re-fires every frame for as long as the
     * player is on the title screen — the engine's would-be dispatch
     * left `select_phase` at 0xf and relied on the destination scene's
     * `menu_folding_out = 0` to break out. Our bare slice has no
     * destination scenes; we suppress the log + force a snap-back
     * to `select_phase = 0` so the player can pick a different item. */
    int title_action_logged[9] = {0};
    for (;;) {
        while (!PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE)) {
            /* Phase A: under --input-trace-replay, pin g_paused=FALSE
             * each iter so a window-focus loss can't stall the replay
             * mid-scenario (WM_ACTIVATE inactive would otherwise drop
             * us into WaitMessage waiting on user input that never
             * comes). */
            if (input_harness_driven()) g_paused = FALSE;

            if (g_paused) {
                WaitMessage();
            } else {
                /* Under --input-trace-replay, drive virtual time so
                 * the tick scheduler never returns DELAYED — we want
                 * exactly one ticked frame per loop iteration, no
                 * wall-clock gating, no Sleep. 20ms per call >
                 * threshold[0]=16.67ms so each call is TICKED. */
                if (input_harness_driven()) {
                    uint32_t vms = (g_tick.frame_count + 1) * 20u;
                    tick_step_with_now(vms,
                                       g_d3d != NULL && g_dev != NULL,
                                       &tick_cb, NULL);
                } else {
                    tick_step_win32(g_d3d != NULL && g_dev != NULL, &tick_cb);
                }

                /* Frame-budget exit. Checked AFTER tick so the Nth
                 * frame actually renders + (if listed) captures
                 * before we quit. */
                if (g_max_frames > 0 && g_tick.frame_count >= g_max_frames) {
                    PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
                    g_skip_quit_prompt = TRUE;
                }

                /* Title press-dispatch outbox: scene_title_sim sets
                 * `pending_action` to a SCENE_TITLE_MENU_* code on the
                 * frame select_phase reaches 0xf. Handle it here so the
                 * pure sim module stays Win32-free. */
                const int32_t act = g_scene_title_anim.pending_action;
                if (act != SCENE_TITLE_ACTION_NONE) {
                    g_scene_title_anim.pending_action = SCENE_TITLE_ACTION_NONE;
                    switch (act) {
                    case SCENE_TITLE_MENU_EXIT:
                        /* Engine FUN_0049a59e L525-526 verbatim:
                         *   DAT_0964356c = 1;
                         *   PostMessageA(DAT_073dfc7c, WM_CLOSE, 0, 0);
                         * The flag tells WndProc's WM_CLOSE arm
                         * (FUN_0047b2e7 L85-94) to skip the "Do you really
                         * want to quit?" prompt — the prompt is only meant
                         * for clicks on the system X button. */
                        g_skip_quit_prompt = TRUE;
                        PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
                        /* Leave select_phase at 0xf — window is closing
                         * anyway, no further frames will run. */
                        break;
                    default:
                        if (act >= 0 && act < 9 && !title_action_logged[act]) {
                            title_action_logged[act] = 1;
                            fprintf(stderr,
                                "title: menu item %d selected — "
                                "destination scene not ported yet\n",
                                (int)act);
                        }
                        /* Snap select_phase back so the player can pick
                         * a different item without being stuck. */
                        g_scene_title_anim.select_phase = 0;
                        break;
                    }
                }

                /* NEW GAME / NEW_HAS_SAVE / CONT_HAS_SAVE route through
                 * the fade-out counter + g_fade_phase1 instead of the
                 * `pending_action` outbox. scene_title_sim flips
                 * g_scene_state to INGAME once fade_is_done() returns 1
                 * (via scene_post_fade_init, which also kicks the
                 * phase-(-1) fade-IN so the black quad ramps out over
                 * the next 17 sim ticks revealing the placeholder
                 * ingame render). */
                if (g_scene_state == SCENE_STATE_INGAME
                    && !title_action_logged[0]) {
                    int code = -1;
                    if (g_scene_title_menu.count > 0
                        && g_scene_title_anim.cursor_pos
                           < (uint32_t)g_scene_title_menu.count) {
                        code = g_scene_title_menu.items[
                            g_scene_title_anim.cursor_pos];
                    }
                    title_action_logged[0] = 1;
                    fprintf(stderr,
                        "title: menu item %d → INGAME (placeholder)\n",
                        code);
                }
            }
        }
        if (!GetMessageA(&msg, NULL, 0, 0)) break;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* ─── shutdown ──────────────────────────────────────────────────────── */

    /* Save-back (engine FUN_004905a8(-1)): only when explicitly opted
     * in via --save-write. Writes the in-memory save arena (with any
     * settings-menu slider changes applied during this run) to BOTH
     * save.dat and _save.dat. Default is OFF so harness/smoke runs
     * don't accidentally overwrite the user's real save. */
    if (g_save_write) {
        save_io_write_arena("save.dat", "_save.dat");
    }

    audio_shutdown();
    audio_trace_close();
    input_trace_record_close();
    input_trace_free(&g_replay_trace);
    if (g_anchor_record_fp) { fclose(g_anchor_record_fp); g_anchor_record_fp = NULL; }
    if (g_player_pos_log_fp) { fclose(g_player_pos_log_fp); g_player_pos_log_fp = NULL; }
    sprite_destroy(&g_show_sprite);
    if (g_show_mesh) { mesh_free(g_show_mesh); g_show_mesh = NULL; }
    if (g_house_preview_mesh) {
        mesh_free(g_house_preview_mesh);
        g_house_preview_mesh = NULL;
    }
    if (g_force_pass_d_mesh) {
        scene1_shop_walker_set_pass_d_mesh(NULL);
        mesh_free(g_force_pass_d_mesh);
        g_force_pass_d_mesh = NULL;
    }
    mesh_tex_cache_reset();
    fade_unload_system_texture();
    scene_title_unload_assets();
    if (!input_harness_driven()) {
        input_shutdown();
    }
    storage_shutdown();
    shutdown_render();
    timeEndPeriod(10);
    timeEndPeriod(tc.wPeriodMin);
    return (int)msg.wParam;
}

/* ─── window class register + create — mirrors FUN_0047aa8b ──────────────── */
static BOOL create_main_window(HINSTANCE hInst, int nCmdShow)
{
    (void)nCmdShow;

    WNDCLASSEXA wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC;                          /* 0x40 */
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconA(hInst, MAKEINTRESOURCEA(ICON_RES_ID));
    wc.hCursor       = LoadCursorA(NULL, MAKEINTRESOURCEA(32512)); /* IDC_ARROW = 0x7F00 */
    wc.hbrBackground = NULL;
    wc.lpszMenuName  = g_windowed ? MAKEINTRESOURCEA(MENU_RES_ID) : NULL;
    wc.lpszClassName = AZUMANGA_CLASS;

    if (!RegisterClassExA(&wc)) {
        return FALSE;
    }

    RECT rc = {0, 0, g_ini.width, g_ini.height};
    DWORD style = WS_OVERLAPPEDWINDOW;                    /* 0xCF0000 */
    AdjustWindowRect(&rc, style, /*hasMenu=*/g_windowed ? TRUE : FALSE);

    g_hwnd = CreateWindowExA(
        0, AZUMANGA_CLASS, AZUMANGA_TITLE, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInst, NULL);
    return g_hwnd != NULL;
}

/* ─── WndProc — mirrors FUN_0047b2e7 (skeleton subset) ───────────────────── */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        /* original: FUN_0040110f(); optional CheckMenuItem(0x9C44, ...) */
        return 0;

    case WM_DESTROY:
        /* original: save window pos, FUN_0040112a(), PostQuitMessage(0) */
        PostQuitMessage(0);
        return 0;

    case WM_ACTIVATE:
        /* LOWORD(wParam) is the WA_* code (0=inactive); HIWORD is the
         * minimized flag. Original: paused = inactive-or-minimized, then
         * un/acquire all DInput devices accordingly. */
        if (LOWORD(wParam) == WA_INACTIVE || HIWORD(wParam) != 0) {
            g_paused = TRUE;
            input_unacquire_all();
        } else {
            g_paused = FALSE;
            input_acquire_all();
        }
        return 0;

    case WM_CLOSE:
        /* Mirror FUN_0047b2e7 L85-94: prompt only when windowed AND the
         * "skip prompt" flag (DAT_0964356c) is clear. Title-menu EXIT
         * raises that flag before posting WM_CLOSE, so this branch is
         * effectively the X-button / Alt-F4 path. Also skipped under
         * --max-duration-ms so smoke runs need no human interaction. */
        if (g_windowed && g_max_duration_ms == 0 && !g_skip_quit_prompt) {
            int r = MessageBoxA(hwnd,
                "Do you really want to quit the game?",
                "EXIT?", MB_OKCANCEL);
            if (r != IDOK) return 0;
        }
        return DefWindowProcA(hwnd, msg, wParam, lParam);

    case WM_TIMER:
        if (wParam == AUTO_EXIT_TIMER_ID) {
            KillTimer(hwnd, AUTO_EXIT_TIMER_ID);
            DestroyWindow(hwnd);    /* → WM_DESTROY → PostQuitMessage(0) */
            return 0;
        }
        if (wParam == PLAY_SE_TIMER_ID) {
            KillTimer(hwnd, PLAY_SE_TIMER_ID);
            if (g_play_se_index < g_play_se_count) {
                int slot = g_play_se_slots[g_play_se_index++];
                fprintf(stderr, "openrecet: --play-se → slot %d\n", slot);
                audio_play_se(slot);
            }
            if (g_play_se_index < g_play_se_count) {
                SetTimer(hwnd, PLAY_SE_TIMER_ID, g_play_se_interval_ms, NULL);
            }
            return 0;
        }
        return DefWindowProcA(hwnd, msg, wParam, lParam);

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            /* Context-sensitive ESC routing (engine WndProc FUN_0047b2e7 ESC
             * arm). Only the title-screen / no-overlay case quits; in-game and
             * dialogue contexts swallow ESC (the skip-event prompt lands in a
             * later phase). See src/esc_dispatch.c. */
            if (esc_pressed() == ESC_RESULT_QUIT)
                PostMessageA(hwnd, WM_CLOSE, 0, 0);
        } else if (wParam == VK_F2) {
            /* TAS recorder: toggle start/stop.  (F10 was avoided — Win32 traps
             * F10 as the system-menu key, which pauses the message loop.) */
            if (g_trace_rec_active) trace_rec_stop();
            else                    trace_rec_start();
        } else if (wParam == VK_F3) {
            /* Mark a capture point at the current frame in the recording. */
            trace_rec_add_capture();
        } else if (wParam == VK_F4) {
            /* Toggle a call-graph-trace window in the recording. */
            trace_rec_toggle_calltrace();
        }
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

/* ─── d3d8.dll dynamic load — mirrors the original's LoadLibrary pattern ─── */
static BOOL load_d3d8(void)
{
    g_d3d8_dll = LoadLibraryA("d3d8.dll");
    if (!g_d3d8_dll) {
        g_d3d8_dll = LoadLibraryA("d3d8d.dll");        /* debug runtime fallback */
        if (!g_d3d8_dll) return FALSE;
    }
    g_pDirect3DCreate8 = (PFN_Direct3DCreate8)(void(*)(void))
        GetProcAddress(g_d3d8_dll, "Direct3DCreate8");
    return g_pDirect3DCreate8 != NULL;
}

/* ─── Direct3D 8 device creation ──────────────────────────────────────────
 * Mirrors FUN_0047ac6a — see docs/findings/winmain-and-bootstrap.md
 * §"D3D8 device creation" for the full present-params field map and the
 * unusual windowed=DISCARD / fullscreen=COPY+VSYNC swap-effect choice.
 *
 * Deliberate deviations from the original (all behaviorally compatible):
 *   - hDeviceWindow set explicitly to hwnd; the original leaves it NULL
 *     and lets D3D fall back to the focus window (same hwnd anyway).
 *   - Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER when --capture-to is on;
 *     the original never sets Flags, so this is a capture-only toggle.
 *   - Resolution + windowed/fullscreen are pulled from `g_ini` (recet.ini
 *     [setup] screen / winmode), loaded in WinMain before this runs.
 */
static BOOL init_render(HWND hwnd)
{
    g_d3d = g_pDirect3DCreate8(D3D_SDK_VERSION);
    if (!g_d3d) return FALSE;

    D3DDISPLAYMODE mode = {0};
    if (FAILED(IDirect3D8_GetAdapterDisplayMode(g_d3d, D3DADAPTER_DEFAULT, &mode))) {
        return FALSE;
    }

    D3DPRESENT_PARAMETERS pp = {0};
    pp.BackBufferWidth        = g_ini.width;
    pp.BackBufferHeight       = g_ini.height;
    pp.BackBufferFormat       = mode.Format;
    pp.BackBufferCount        = 1;
    pp.MultiSampleType        = D3DMULTISAMPLE_NONE;
    pp.Windowed               = g_windowed ? TRUE : FALSE;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D16;
    pp.hDeviceWindow          = hwnd;
    if (g_windowed) {
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    } else {
        pp.SwapEffect                      = D3DSWAPEFFECT_COPY;
        pp.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        pp.FullScreen_RefreshRateInHz      = 0;
    }
    if (g_capture_dir) {
        pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    }

    /* The original calls GetDeviceCaps later (inside FUN_00454e69 — see
     * layers_init) using the caps to seed the 24 render-layer objects.
     * Earlier skeletons had a redundant call here; removed now that the
     * real owner is in place. */

    /* Behavior-flag fallback chain — same order as FUN_0047ac6a:
     * HARDWARE+MULTITHREADED, then MIXED, then SOFTWARE. */
    static const DWORD bf[] = {
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, /* 0x44 */
        D3DCREATE_MIXED_VERTEXPROCESSING,                              /* 0x80 */
        D3DCREATE_SOFTWARE_VERTEXPROCESSING,                           /* 0x20 */
        0
    };
    HRESULT hr = E_FAIL;
    for (int i = 0; bf[i]; i++) {
        hr = IDirect3D8_CreateDevice(
            g_d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
            bf[i], &pp, &g_dev);
        if (SUCCEEDED(hr)) break;
    }
    return SUCCEEDED(hr);
}

static void shutdown_render(void)
{
    d3d_trace_shutdown();
    call_trace_shutdown();
    if (g_dev) { IDirect3DDevice8_Release(g_dev); g_dev = NULL; }
    if (g_d3d) { IDirect3D8_Release(g_d3d); g_d3d = NULL; }
    if (g_d3d8_dll) { FreeLibrary(g_d3d8_dll); g_d3d8_dll = NULL; }
}

/* ─── house-preview dump loader ──────────────────────────────────────────
 *
 * Reads a retail-format mesh dump (vb.bin + ib.bin from tools/dump-
 * retail-meshes.py or tools/dump-our-mesh) into a fresh mesh_t,
 * uploads VB+IB to D3D, computes bounds. The mesh has a single
 * submesh covering all indices and no materials/textures — meant for
 * "did the geometry render correctly" smoke tests, not pretty
 * rendering. mesh_draw_d3d8 with no texture falls back to white
 * diffuse, so the output is white-shaded geometry under the preview
 * lighting setup.
 *
 * Format assumed (FVF 0x152, 16-bit indices):
 *   vb.bin   raw mesh_vertex array (36 B/vertex)
 *   ib.bin   raw uint16 indices, globally addressed into VB
 *
 * Returns NULL on any failure (file not found, size mismatch, upload
 * fail). Caller mesh_free()s the result.
 */
static char *house_preview_slurp(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = (char *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return NULL; }
    *out_len = (size_t)sz;
    return buf;
}

static mesh_t *house_preview_load_dump(const char *dir, IDirect3DDevice8 *dev)
{
    char path_vb[MAX_PATH], path_ib[MAX_PATH];
    snprintf(path_vb, sizeof path_vb, "%s/vb.bin", dir);
    snprintf(path_ib, sizeof path_ib, "%s/ib.bin", dir);

    size_t vb_len = 0, ib_len = 0;
    char *vb_buf = house_preview_slurp(path_vb, &vb_len);
    char *ib_buf = house_preview_slurp(path_ib, &ib_len);
    if (!vb_buf || !ib_buf) {
        fprintf(stderr, "house-preview-dump: missing vb.bin or ib.bin in %s\n", dir);
        free(vb_buf); free(ib_buf);
        return NULL;
    }
    if (vb_len % sizeof(mesh_vertex) != 0 || ib_len % sizeof(uint16_t) != 0) {
        fprintf(stderr, "house-preview-dump: %s sizes don't divide cleanly "
                "(vb=%zu ib=%zu)\n", dir, vb_len, ib_len);
        free(vb_buf); free(ib_buf);
        return NULL;
    }

    int32_t vcount = (int32_t)(vb_len / sizeof(mesh_vertex));
    int32_t icount = (int32_t)(ib_len / sizeof(uint16_t));

    mesh_t *m = (mesh_t *)calloc(1, sizeof *m);
    if (!m) { free(vb_buf); free(ib_buf); return NULL; }
    snprintf(m->path, sizeof m->path, "%s (dump)", dir);
    m->vertices      = (mesh_vertex *)vb_buf;   /* takes ownership */
    m->vertex_count  = vcount;
    m->indices       = (uint16_t *)ib_buf;       /* takes ownership */
    m->index_count   = icount;

    /* Single submesh covering everything. material_index=-1 →
     * mesh_draw_d3d8 sets texture to NULL (white diffuse). */
    m->submeshes = (mesh_submesh *)calloc(1, sizeof(mesh_submesh));
    if (!m->submeshes) { mesh_free(m); return NULL; }
    m->submeshes[0].vertex_offset  = 0;
    m->submeshes[0].vertex_count   = vcount;
    m->submeshes[0].index_offset   = 0;
    m->submeshes[0].index_count    = icount;
    m->submeshes[0].material_index = -1;
    m->submesh_count = 1;

    HRESULT hr = mesh_upload_d3d8(m, dev);
    if (FAILED(hr)) {
        fprintf(stderr, "house-preview-dump: mesh_upload_d3d8 failed: 0x%08lx\n",
                (unsigned long)hr);
        mesh_free(m);
        return NULL;
    }

    mesh_compute_bounds(m);
    fprintf(stderr,
            "house-preview-dump: %s loaded (verts=%d idx=%d centroid="
            "(%.2f, %.2f, %.2f) radius=%.2f)\n",
            dir, m->vertex_count, m->index_count,
            m->centroid[0], m->centroid[1], m->centroid[2], m->radius);
    return m;
}

/* ─── frame render — partial FUN_004547ab port ──────────────────────────
 * Driven by tick_step_win32 as the `render` callback. The engine's
 * full FUN_004547ab dispatch fans into many per-state render functions
 * (FUN_0045bbf9 / FUN_0040a765 / FUN_00417504 / FUN_0045404b / etc.) —
 * we port them one scene at a time. Today:
 *   SCENE_STATE_TITLE   — scene_title_render
 *   SCENE_STATE_INGAME  — scene1_render_camera_setup (Cr.1; calls
 *                        scene1_render_meshes which has 14 walker stubs,
 *                        so visible 3D output is dormant today)
 * Other states leave the back buffer at the per-state clear color.
 *
 * Engine clear color for state-0 is 0xff17f0ff (pink-blue) — visible
 * only at the edges before bg2.bmp fully covers the framebuffer.
 *
 * BeginScene/EndScene/Present + the screen-capture sample point all
 * live here. The capture has to run before Present because
 * D3DSWAPEFFECT_DISCARD leaves the post-Present back buffer undefined.
 */
/* Resolve any --capture-at-anchor requests matching the just-fired
 * anchor into concrete capture frames.  Called from the anchor sink, so
 * it runs in render_dispatch BEFORE the capture_frame_is_listed() check
 * later in the same frame — an offset of 0 therefore captures the anchor
 * frame itself. */
static void anchor_capture_schedule(const char *name, uint32_t frame)
{
    for (int i = 0; i < g_anchor_captures_count; i++) {
        if (lstrcmpA(g_anchor_captures[i].name, name) != 0) continue;
        int64_t target = (int64_t)frame + g_anchor_captures[i].offset;
        if (target < (int64_t)frame) {
            /* offset resolved into the past — can't capture a frame we've
             * already presented. */
            fprintf(stderr,
                "anchor: capture %s%+d → frame %lld is in the past "
                "(now %u) — dropped\n",
                name, (int)g_anchor_captures[i].offset,
                (long long)target, frame);
            continue;
        }
        if (g_capture_frames_count >= CAPTURE_FRAMES_MAX) {
            fprintf(stderr, "anchor: capture list full — dropping %s%+d\n",
                    name, (int)g_anchor_captures[i].offset);
            continue;
        }
        g_capture_frames[g_capture_frames_count++] = (uint32_t)target;
        fprintf(stderr, "anchor: scheduled capture at %s%+d → frame %u\n",
                name, (int)g_anchor_captures[i].offset, (uint32_t)target);
    }
}

/* Tee sink for the anchor stream: stderr (always, prefixed) + the
 * optional --anchor-trace-record file (pure JSONL) + anchor-relative
 * capture scheduling.  `user` is unused — destinations are globals. */
static void anchor_emit_tee(const char *name, uint32_t frame, void *user)
{
    (void)user;
    fprintf(stderr, "anchor: {\"anchor\":\"%s\",\"frame\":%u}\n", name, frame);
    if (g_anchor_record_fp) {
        anchor_trace_sink_jsonl(name, frame, g_anchor_record_fp);
        fflush(g_anchor_record_fp);
    }
    anchor_capture_schedule(name, frame);
    /* Feed the anchor-segmented input forcing so `wait NAME` ops resolve. */
    if (g_input_segtrace_path) input_segtrace_on_anchor(&g_segtrace, name, frame);
}

static void render_dispatch(void)
{
    if (!g_dev) return;

    /* D.5: re-evaluate frame filter so the call-site wrappers know
     * whether to emit this frame.  No-op when --d3d-trace is off. */
    d3d_trace_begin_frame(g_tick.frame_count);

    /* E.2: same gate for the port-side call tracer.  CALL_TRACE_ENTER
     * macros below check the per-frame emit flag this sets. */
    call_trace_begin_frame(g_tick.frame_count);

    /* TAS P1: emit the anchor event stream.  Runs every ticked frame
     * (including nowloading frames) so the load-overlay edges are seen.
     * The snapshot is read here, after sim_a/sim_b have committed this
     * frame's scene_state + loading gate.  Always active (the BOOT
     * anchor + stderr echo cost nothing); the file sink is opt-in. */
    {
        struct anchor_world w = {
            .scene_state    = g_scene_state,
            /* Primary worker-load gate (new-game HOUSE scene load = LOADING #1)
             * OR the dialogue's inter-script bracket (iv1_1→iv1_2 = LOADING #2,
             * replacing the retired scene1_intro_events stub). */
            .loading_active = nowloading_is_active()
                              || scene1_intro_dialogue_loading(),
            .dlg_active     = scene1_intro_dialogue_active(),
            .text_reveal    = scene1_intro_dialogue_text_reveal(),
            .text_revealed  = scene1_intro_dialogue_text_revealed(),
        };
        anchor_trace_tick(&g_anchor_state, g_tick.frame_count, w,
                          anchor_emit_tee, NULL);
    }

    /* TAS recorder: capture this frame's player-1 input mask (read here, before
     * the engine clears g_input_state at the end of the tick).  Records every
     * ticked frame while active so the recording is complete from the F2 press. */
    trace_rec_tick(g_input_state[0].buttons);

    /* Player-pos log: one JSONL row per frame while in the HOUSE/INGAME scene
     * (read after sim has committed this frame's player position). */
    if (g_player_pos_log_fp && g_scene_state == SCENE_STATE_INGAME) {
        /* W4.7: also dump velocity (daabc/daac4), stored facing (db05c) and the
         * held d-pad mask, so the facing-slew law can be reconstructed/diffed
         * against retail (engine-quirks §69). */
        float vx = 0.0f, vz = 0.0f, facing = 0.0f; int sticky = 0;
        player_ctrl_debug_state(&vx, &vz, &facing, &sticky);
        /* W3b: actor-0 walk-cycle state (anim id / counter / cycle-frame) so the
         * chr_anim_tick timing can be diffed against retail (runs/w3b-anim-watch). */
        const int32_t *rec = player_ctrl_actor_record(0);
        int a_anim = rec ? rec[CHR_ACTOR_ANIM]    : -1;
        int a_cnt  = rec ? rec[CHR_ACTOR_COUNTER] : -1;
        int a_frm  = rec ? rec[CHR_ACTOR_FRAME]   : -1;
        int a_oct  = rec ? rec[CHR_ACTOR_FACING]  : -1;
        /* §71: companion (actor 2 — the fairy) position + facing/anim, so the
         * hover-follow can be diffed against retail (runs/companion-truth). */
        const int32_t *crec = player_ctrl_actor_record(2);
        int c_char = player_ctrl_actor_char(2);
        int c_anim = crec ? crec[CHR_ACTOR_ANIM]   : -1;
        int c_oct  = crec ? crec[CHR_ACTOR_FACING] : -1;
        fprintf(g_player_pos_log_fp,
                "{\"frame\":%u,\"px\":%.5f,\"py\":%.5f,\"pz\":%.5f,"
                "\"vx\":%.6f,\"vz\":%.6f,\"facing\":%.6f,\"sticky\":%d,"
                "\"buttons\":%u,\"anim\":%d,\"counter\":%d,\"aframe\":%d,\"oct\":%d,"
                "\"cchar\":%d,\"cx\":%.5f,\"cy\":%.5f,\"cz\":%.5f,\"canim\":%d,\"coct\":%d,"
                "\"rng\":%d}\n",
                g_tick.frame_count,
                g_scene1_player_pos[0], g_scene1_player_pos[1],
                g_scene1_player_pos[2],
                vx, vz, facing, sticky, g_input_state[0].buttons,
                a_anim, a_cnt, a_frm, a_oct,
                c_char, g_scene1_actor_pos[2][0], g_scene1_actor_pos[2][1],
                g_scene1_actor_pos[2][2], c_anim, c_oct,
                (int32_t)g_rng_seed);
        fflush(g_player_pos_log_fp);
    }

    /* E.2 probe — FUN_004547ab @ 0x4547ab (render dispatch root).
     * Must be AFTER call_trace_begin_frame so the first emit lands in
     * the new frame's bucket. */
    CALL_TRACE_ENTER(0x4547abu);

    /* Engine FUN_004547ab L17: unconditional scene-effect counter pump.
     * Separate from the sim-side worker-load-busy gated call in
     * sim_step_a — the engine runs this every render-tick regardless of
     * scene state or loading status.  Without it the counter pump only
     * fires when worker_load_busy() is true (= rarely on the
     * load-everything-sync port), so retail-vs-port call_trace diff
     * shows a persistent -1 per frame on 0x4532df (found 2026-05-27
     * via pre_3d_trace methodology).  See call_trace_diff.py output. */
    sim_loading_pump();

    /* Per-state clear color. Engine FUN_004547ab L33-44 derives the
     * scene-1 clear from DAT_068dd2f0's stage palette; we use a fixed
     * placeholder until the stage system ports. Title clear stays at
     * 0xff17f0ff. */
    DWORD clear_argb = 0xff17f0ff;
    if (g_scene_state == SCENE_STATE_INGAME) {
        if (g_house_preview && g_house_preview_mesh) {
            /* C7d preview: engine HOUSE clear color is black
             * (stage_palette_house.clear_r/g/b all zero). Use black so
             * the preview matches what FUN_004547ab will eventually
             * emit, rather than the placeholder navy. */
            clear_argb = 0xff000000u;
        } else {
            clear_argb = scene_ingame_clear_argb();
        }
    }
    IDirect3DDevice8_Clear(
        g_dev, 0, NULL,
        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
        clear_argb,
        1.0f, 0);
    IDirect3DDevice8_BeginScene(g_dev);

    /* Engine FUN_004547ab L60: SetRenderState(D3DRS_CULLMODE,
     * D3DCULL_NONE). The engine renders the entire scene + overlays
     * with culling disabled (it re-sets to CW at L207, but only after
     * everything has drawn). Our defaults inherit D3DCULL_CCW which
     * silently drops the rotated "Now Loading…" spinner (its strip
     * winding is opposite render_quad_add's). */
    IDirect3DDevice8_SetRenderState(g_dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* --show-mesh skips the scene draw so the mesh sits alone on the
     * clear color — easier to read in capture sheets and the eventual
     * contact sheet. The scene transitions still tick in the
     * background (sim_step_a continues running, audio plays, etc), we
     * just don't paint the scene's 2D layers. */
    if (!g_show_mesh) {
        switch (g_scene_state) {
        case SCENE_STATE_TITLE:
            if (g_scene_title_assets_loaded) {
                scene_title_render(g_dev,
                                   &g_scene_title_menu,
                                   &g_scene_title_anim);
            }
            break;
        case SCENE_STATE_INGAME:
            if (g_house_preview && g_house_preview_mesh) {
                /* C7d throwaway: render the house mesh under a fixed
                 * 3/4 isometric camera that approximates the engine's
                 * scene-1 view (FUN_0045bbf9 + FUN_0040120c). The real
                 * engine camera reads eye/at from DAT_073de31c/328
                 * (player-relative gameplay state); for the preview we
                 * pin them to a hardcoded shop interior POV so the
                 * image is steady and easy to compare against
                 * Frida-captured retail frames. Replaced by the real
                 * walker post-C7n.
                 *
                 * Projection matches FUN_004a3ee8(fov=45°, aspect=4/3,
                 * near=1.0, far=350.0) — engine scene-1 defaults
                 * (DAT_073de3a0 / DAT_073de2dc). Aspect honors the
                 * actual back buffer so non-4:3 dev runs aren't
                 * letterboxed. */
                mesh_set_default_render_state(g_dev);
                mesh_setup_preview_light(g_dev);

                /* Centroid-relative offset scaled by bound radius so
                 * the camera always frames the full mesh regardless
                 * of which house variant is loaded via
                 * --house-preview-path. The 0.8 factor was tuned for
                 * shop_1st.x — its centroid sits high on the back
                 * wall (skybox pulls it up) and the floor is tens of
                 * units below, so a wide distance + slight downward
                 * look (target.y -= 0.05·r) gives a 3/4 isometric
                 * that includes floor + counter + windows. */
                const float *cn = g_house_preview_mesh->centroid;
                float r         = g_house_preview_mesh->radius;
                if (r <= 0.0f) r = 1.0f;
                float d         = r * 0.8f;
                float eye[3]    = { cn[0] + d * 0.866f,
                                    cn[1] + d * 0.500f,
                                    cn[2] - d * 0.866f };
                float target[3] = { cn[0], cn[1] - r * 0.05f, cn[2] };
                float up[3]     = { 0.0f, 1.0f, 0.0f };
                float view[16], proj[16];
                mat4_lookat_rh(view, eye, target, up);

                float fov_y  = 45.0f * 3.14159265358979323846f / 180.0f;
                float aspect = (g_ini.height > 0)
                                   ? ((float)g_ini.width / (float)g_ini.height)
                                   : 1.3333333f;
                /* Loose near/far derived from bound radius so the
                 * whole mesh fits when --house-preview-path swaps in
                 * something larger than shop_1st. */
                float z_near = 0.5f;
                float z_far  = r * 6.0f + 100.0f;
                mat4_perspective_fov_rh(proj, fov_y, aspect, z_near, z_far);

                IDirect3DDevice8_SetTransform(g_dev, D3DTS_VIEW,
                                              (const D3DMATRIX *)view);
                IDirect3DDevice8_SetTransform(g_dev, D3DTS_PROJECTION,
                                              (const D3DMATRIX *)proj);
                float ident[16] = {
                    1, 0, 0, 0,
                    0, 1, 0, 0,
                    0, 0, 1, 0,
                    0, 0, 0, 1
                };
                IDirect3DDevice8_SetTransform(g_dev, D3DTS_WORLD,
                                              (const D3DMATRIX *)ident);
                mesh_draw_d3d8(g_dev, g_house_preview_mesh);
            } else if (!nowloading_is_active()) {
                /* Cr.1 (2026-05-23) + Cr.2 (2026-05-25): real scene-1
                 * mesh render chain.  Engine FUN_004547ab L70-73
                 * (DAT_0438b1c0==1 && DAT_0438b1d0==1, the most common
                 * INGAME path):
                 *
                 *     FUN_0045bbf9();   scene1_render_camera_setup
                 *                       (calls scene1_render_meshes
                 *                        which calls 14 walker stubs)
                 *     FUN_0040a765();   2D HUD aggregator (C7j: shell +
                 *                       Passes 1-3 landed; 4-9 = C7k+)
                 *     FUN_00417504();   scene1_render_overlay     (DEFER)
                 *     FUN_0045404b();   scene1_render_fx_tail
                 *
                 * Cr.1 wired camera_setup; Cr.2 (this commit) added
                 * fx_tail.  scene1_render_overlay is DEFERRED — wiring
                 * it makes the title-z-press frame-90 "Now Loading…"
                 * CD icon render pure black due to a COLORARG2 =
                 * D3DTA_SPECULAR write at FUN_00417504 L18 that leaks
                 * into nowloading_render.  Engine compensation source
                 * unidentified; see pending-human-check #18 for the
                 * Frida-read needed to either confirm retail has the
                 * same icon-disappears artifact or to find the engine
                 * COLORARG2 restorer.
                 *
                 * scene1_render_fx_tail is dormant in HOUSE today —
                 * gated on `sim_get_counter_994() > 0` (BSS-zero) for
                 * the inner draw, and the head call to FUN_00454191 is
                 * still stubbed (`scene1_fx_overlays_TODO`).  Wiring it
                 * is safe: when both gates are zero the call is a pure
                 * no-op with no state writes.
                 *
                 * Today every walker call inside scene1_render_meshes is
                 * a TODO stub, so visible 3D output remains zero.  The
                 * wiring lands so that as the walker chips (C8c/d/e/
                 * wide-followup) port, real records from the integrator
                 * (C8h) + spawn API (C8i.1-5c) + ambient spawn loop
                 * (Cf.1) start producing visible pixels without another
                 * wiring chip.
                 *
                 * 2026-05-27: gated on `!nowloading_is_active()` to mirror
                 * engine FUN_004547ab L51100, which skips the entire
                 * per-state render dispatch while the nowloading overlay
                 * is up.  Without this gate our port jumps straight from
                 * the title fade-out into a fully-rendered 3D HOUSE on
                 * the first INGAME frame; retail spends the post-NEW-GAME
                 * window showing the nowloading overlay over a black
                 * background and only reaches 3D once the worker drops.
                 * Our sync-load collapses the window to ~1 frame but the
                 * structural gate is what makes the visible boot path
                 * match.  (The engine's second gate `DAT_06a4999c < 4 ||
                 * 0xc < DAT_06a4999c` — the scene-transition counter — is
                 * BSS-zero in port today, so omitted; revisit when the
                 * scene-transition state machine ports.) */
                scene1_render_camera_setup(g_dev);
                /* C7j (2026-05-29): FUN_0040a765 2D HUD aggregator,
                 * between the 3D walker and the overlay dispatcher
                 * (engine FUN_004547ab L71).  Entry shell + Passes 1-3
                 * landed; all dormant in HOUSE (Pass 1 DUNGEON-gated,
                 * Pass 2 letterbox BSS-zero, Pass 3 status-screen
                 * BSS-zero) so this is visually inert today but wires
                 * the foundation later passes (C7k+) inherit. */
                /* Opening-prologue dialogue (FUN_0046c9a2, via the engine's
                 * render root FUN_004547ab→FUN_0046c090).  Drawn after the 3D
                 * scene — the prologue's painted 2D bg covers the HOUSE — and
                 * before the HUD, which overlays it (cap_20: money HUD over the
                 * dialogue bg).  No-op unless a script is active. */
                scene1_dialogue_draw(g_dev);
                scene1_hud_render(g_dev);
                scene1_render_overlay(g_dev);
                /* scene1_render_fx_tail is moved out of this branch
                 * and called unconditionally below — engine has it
                 * both inline here AND in the LAB_00454be4 fallthrough
                 * (set bVar1=false to skip fallthrough), but since
                 * fx_tail is currently a no-op stub in port, calling
                 * it exactly once per frame from the fallthrough is
                 * the simpler match.  Restore the inline call here
                 * (and add bVar1 tracking) only if fx_tail acquires
                 * state-write side effects that depend on order. */
            }

            /* --show-pass-f-test: overlay one type-0x92 billboard from
             * scene1_pass_f_render on top of whatever the INGAME branch
             * just drew.  Re-injects every frame (idempotent — always
             * slot 0) so a future scene1_records_reset can't wipe it
             * out mid-run.  Identity view + RH perspective with the same
             * fov/aspect as scene1_render's wide projection (z_far=2000).
             *
             * Particle placed at world (0, 0, -50) — visible from the
             * identity view's eye at origin looking down -Z.  Use
             * pos.y=0 so the gate `piVar11[1] >= 0` (age >= 0) is
             * satisfied; the y=0 position itself is fine for the view.
             *
             * Diverges from the eventual scene1_render_meshes wiring
             * (which sets camera + walker chain from the engine's
             * camera helpers); this is a stand-alone path purely for
             * the Pass F MVP visual smoke. */
            if (g_show_pass_f_test) {
                scene1_records_inject_test_type92(0.0f, 0.0f, -50.0f);

                float fov_y  = 45.0f * 3.14159265358979323846f / 180.0f;
                float aspect = 4.0f / 3.0f;
                float view[16], proj[16];
                mat4_identity(view);
                mat4_perspective_fov_rh(proj, fov_y, aspect, 1.0f, 2000.0f);
                IDirect3DDevice8_SetTransform(g_dev, D3DTS_VIEW,
                                              (const D3DMATRIX *)view);
                IDirect3DDevice8_SetTransform(g_dev, D3DTS_PROJECTION,
                                              (const D3DMATRIX *)proj);

                scene1_pass_f_render((struct IDirect3DDevice8 *)g_dev);
            }

            /* --force-player-sprite: Cchr.2b first-pixels MVP.  Overlays
             * the ported leaf renderer's player billboard on top of the
             * HOUSE scene the INGAME branch just drew, inheriting its
             * VIEW/PROJECTION (the captured world matrix is in the same
             * HOUSE world space).  Lazily binds the sheet texture on the
             * first frame; "-"/empty sheet → diffuse-only (white
             * silhouette — still validates geometry/placement). */
            if (g_force_player_sprite && g_fps_loaded && g_dev) {
                if (!g_fps_sheet_tried) {
                    g_fps_sheet_tried = 1;
                    if (g_fps_sheet[0] && strcmp(g_fps_sheet, "-") != 0) {
                        if (!sprite_load(g_dev, g_fps_sheet,
                                         (uint32_t)g_fps_tex_w,
                                         (uint32_t)g_fps_tex_h,
                                         &g_fps_sheet_sprite)) {
                            fprintf(stderr, "force-player-sprite: sheet load "
                                    "failed (%s); drawing diffuse-only\n",
                                    g_fps_sheet);
                        }
                    }
                }
                IDirect3DDevice8_SetTexture(g_dev, 0,
                    (IDirect3DBaseTexture8 *)g_fps_sheet_sprite.tex);
                scene1_chr_sprite_render((struct IDirect3DDevice8 *)g_dev,
                                         g_fps_actor, g_fps_char_id,
                                         g_fps_world, g_fps_color,
                                         g_fps_tex_w, g_fps_tex_h);
            }
            break;
        default:
            break;
        }
    }

    /* --show-mesh preview: drawn on top of whatever the scene rendered,
     * before fade/nowloading so those still tint correctly. Camera
     * orbits the Y axis once every 6 seconds at host pace (frame_count
     * mod 360 → phase). The scene_*_render functions above only touch
     * the 2D quad pipeline (FVF 0x142/0x1c4) and leave depth/lighting
     * in their own state — we re-establish mesh draw state here. */
    if (g_show_mesh) {
        mesh_set_default_render_state(g_dev);
        mesh_setup_preview_light(g_dev);
        mesh_orbital_set_zoom(g_show_mesh_zoom);
        float phase = (float)(g_tick.frame_count % 360) / 360.0f;
        mesh_orbital_view_proj(g_dev,
                               g_show_mesh->centroid, g_show_mesh->radius,
                               phase,
                               (int)g_ini.width, (int)g_ini.height);
        mesh_draw_d3d8(g_dev, g_show_mesh);
    }

    /* Engine FUN_004547ab LAB_00454be4 (fallthrough at L183): the
     * render dispatcher exits via either an inline FUN_0045404b call
     * inside one of the state-1 substates (with bVar1 cleared) or this
     * unconditional fallthrough.  Result: exactly 1 fx_tail call per
     * frame across every state.  Pre-3D-trace diff (2026-05-27) showed
     * a persistent -1 here because our port only had the inline
     * INGAME call; TITLE + other states missed the fallthrough.  See
     * call_trace_diff.py output. */
    scene1_render_fx_tail(g_dev);

    /* Engine FUN_004547ab L51237-51239: dungeon-clear banner overlay
     * (FUN_0048fe43).  Gated on `state==INGAME || (3 < transition_counter
     * < 0xd)`.  Transition_counter is BSS-zero in our port (the scene-
     * transition state machine hasn't ported), so only the INGAME arm
     * fires.  Body is a no-op when its own internal counter is 0 (= the
     * banner isn't currently animating) but still calls
     * render_quad_state_setup as a state-write side effect — that's why
     * we call it unconditionally on INGAME frames, not gated on the
     * counter (the counter gate lives inside the function). */
    if (g_scene_state == SCENE_STATE_INGAME) {
        scene1_dungeon_clear_banner_render(g_dev);
    }

    /* Engine FUN_004547ab L202: scene-fade alpha quad. Runs after the
     * scene-render dispatch so it darkens whatever the scene just drew
     * (or, in the post-fade frames, draws over the empty back buffer to
     * keep the screen solid black). No-op when no fade is in progress. */
    fade_render(g_dev);

    /* Engine FUN_004547ab L203: "Now Loading…" overlay. Drawn after
     * the fade quad so the spinner sits on top of any cross-fade
     * darkening. nowloading_render() is gated internally on
     * nowloading_set_active() — does only an alpha-counter decay tick
     * if the gate is 0. Set by scene_post_fade_init() at the moment
     * the LOADING→INGAME flip happens. */
    nowloading_render(g_dev);

    if (g_show_sprite.tex) {
        sprite_draw(g_dev, &g_show_sprite, 32.0f, 32.0f);
    }
    IDirect3DDevice8_EndScene(g_dev);

    if (g_capture_dir) {
        /* Phase A: when --capture-frames is set, capture ONLY at the
         * listed sim-frame indices. Otherwise fall back to the
         * existing wall-clock sampler (--capture-every-ms). This keeps
         * the old smoke-test path working untouched while the
         * scenario runner gets deterministic anchors.
         *
         * The capture decision runs every render call, before
         * Present, so g_tick.frame_count is the index of the frame
         * about to be presented. The post-render frame_count++ in
         * tick.c bumps it AFTER this returns. */
        int should_capture = 0;
        unsigned now_ms = timeGetTime();
        /* Listed mode = an explicit frame list OR pending anchor-relative
         * captures (which populate the list at runtime when their anchor
         * fires). In listed mode we capture ONLY listed frames — never
         * fall through to the wall-clock sampler, or a frame-0 grab would
         * sneak in before the first anchor resolves. The time-based path
         * is the legacy smoke-test default (neither flag set).
         *
         * A segtrace also counts as listed mode even before any capture is
         * scheduled: its {capture} ops populate g_capture_frames only when a
         * segment activates (its anchor fires), so until the first one
         * resolves g_capture_frames_count is 0 — without this guard the
         * wall-clock sampler would sneak in a spurious frame-0 grab and shift
         * every capture-index golden by one. */
        if (g_capture_frames_count > 0 || g_anchor_captures_count > 0
                || g_input_segtrace_path != NULL) {
            should_capture =
                capture_frame_is_listed(g_tick.frame_count);
        } else if (g_capture_last_ms == 0 ||
                   (now_ms - g_capture_last_ms) >= g_capture_every_ms) {
            should_capture = 1;
        }
        if (should_capture) {
            capture_backbuffer();
            g_capture_last_ms = now_ms;
            g_capture_count++;
        }
    }

    IDirect3DDevice8_Present(g_dev, NULL, NULL, NULL, NULL);

    /* D.5: fflush so a mid-scenario crash still leaves the trace on
     * disk through the last completed frame. */
    d3d_trace_end_frame();
    call_trace_end_frame();
}

/* ─── CLI parsing — hand-tokenize lpCmdLine on ASCII spaces ──────────────
 * Supports:  --capture-to <dir>        (next token, may contain backslashes)
 *            --capture-every-ms <ms>   (next token, decimal unsigned int)
 * No quoting support needed for the smoke harness (paths injected by the
 * test runner are known-simple).
 */
static void parse_cmdline(LPSTR lpCmdLine)
{
    if (!lpCmdLine || *lpCmdLine == '\0') return;

    /* Duplicate so we can tokenize in-place with strtok. */
    static char buf[4096];
    lstrcpynA(buf, lpCmdLine, (int)sizeof(buf));

    char *tok = strtok(buf, " ");
    while (tok) {
        if (lstrcmpA(tok, "--capture-to") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* Persist in a static buffer — lives for the process lifetime. */
                static char dir_buf[MAX_PATH];
                lstrcpynA(dir_buf, val, (int)sizeof(dir_buf));
                g_capture_dir = dir_buf;
            }
        } else if (lstrcmpA(tok, "--capture-every-ms") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                unsigned n = (unsigned)strtoul(val, NULL, 10);
                if (n > 0) g_capture_every_ms = n;
            }
        } else if (lstrcmpA(tok, "--show-sprite") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char name_buf[MAX_PATH];
                lstrcpynA(name_buf, val, (int)sizeof(name_buf));
                g_show_sprite_name = name_buf;
            }
        } else if (lstrcmpA(tok, "--show-mesh") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char mesh_buf[MAX_PATH];
                lstrcpynA(mesh_buf, val, (int)sizeof(mesh_buf));
                g_show_mesh_path = mesh_buf;
            }
        } else if (lstrcmpA(tok, "--mesh-zoom") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                float f = (float)atof(val);
                if (f > 0.0f) g_show_mesh_zoom = f;
            }
        } else if (lstrcmpA(tok, "--house-preview") == 0) {
            g_house_preview = 1;
        } else if (lstrcmpA(tok, "--house-preview-path") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char house_buf[MAX_PATH];
                lstrcpynA(house_buf, val, (int)sizeof(house_buf));
                g_house_preview_path = house_buf;
                g_house_preview = 1;
            }
        } else if (lstrcmpA(tok, "--house-preview-dump") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char dump_buf[MAX_PATH];
                lstrcpynA(dump_buf, val, (int)sizeof(dump_buf));
                g_house_preview_dump = dump_buf;
                g_house_preview = 1;
            }
        } else if (lstrcmpA(tok, "--show-pass-f-test") == 0) {
            g_show_pass_f_test = 1;
        } else if (lstrcmpA(tok, "--force-pass-d-mesh") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char path_buf[MAX_PATH];
                lstrcpynA(path_buf, val, (int)sizeof(path_buf));
                g_force_pass_d_mesh_path = path_buf;
            }
        } else if (lstrcmpA(tok, "--force-player-sprite") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                lstrcpynA(g_fps_inject_path, val, (int)sizeof(g_fps_inject_path));
                g_force_player_sprite = 1;
            }
        } else if (lstrcmpA(tok, "--debug-pass-d-unlit") == 0) {
            g_debug_pass_d_unlit = 1;
        } else if (lstrcmpA(tok, "--force-ambient-spawn") == 0) {
            g_force_ambient_spawn = 1;
        } else if (lstrcmpA(tok, "--ambient-spawn-type") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* strtoul base=0 → accept 0x.., decimal, or octal.
                 * Type ids are byte-wide; clamp into [0, 0xff]. */
                unsigned long n = strtoul(val, NULL, 0);
                if (n <= 0xffu) g_ambient_spawn_type_override = (int)n;
            }
        } else if (lstrcmpA(tok, "--force-c-pickup") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* strtoul base=0 → accept 0x.., decimal, or octal.
                 * Type ids are int-wide (table C accepts the full
                 * RNG-ramp window up to 0xc80+); accept up to INT_MAX. */
                long n = strtol(val, NULL, 0);
                if (n >= 0 && n <= 0x7fffffff) {
                    g_force_c_pickup_type = (int)n;
                }
            }
        } else if (lstrcmpA(tok, "--force-c-world-drop") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                long n = strtol(val, NULL, 0);
                if (n >= 0 && n <= 0x7fffffff) {
                    g_force_c_world_drop_type = (int)n;
                }
            }
        } else if (lstrcmpA(tok, "--force-c-world-drop-count") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                long n = strtol(val, NULL, 0);
                /* Engine scan cap is 136 for type<=6, 200 for type>6.
                 * Accept any positive count; the spawner silently stops
                 * when the table fills. */
                if (n > 0 && n <= 200) {
                    g_force_c_world_drop_count = (int)n;
                }
            }
        } else if (lstrcmpA(tok, "--force-c-world-drop-mag") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                char *end = NULL;
                float f = strtof(val, &end);
                if (end != val) {
                    g_force_c_world_drop_mag = f;
                }
            }
        } else if (lstrcmpA(tok, "--force-b-npc") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                long n = strtol(val, NULL, 0);
                if (n >= 0 && n <= 0xff) {
                    g_force_b_npc_type = (int)n;
                }
            }
        } else if (lstrcmpA(tok, "--force-b-entity") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                long n = strtol(val, NULL, 0);
                if (n >= 0 && n <= 0xff) {
                    g_force_b_entity_type = (int)n;
                }
            }
        } else if (lstrcmpA(tok, "--force-walker-phase2") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                long n = strtol(val, NULL, 0);
                if (n >= 0 && n <= 4) {
                    g_force_walker_phase2_scene_type = (int)n;
                }
            }
        } else if (lstrcmpA(tok, "--force-chr-walker") == 0) {
            g_force_chr_walker = 1;   /* Cchr.2h: now a no-op (default behavior) */
        } else if (lstrcmpA(tok, "--no-chr-player") == 0) {
            g_no_chr_player = 1;
        } else if (lstrcmpA(tok, "--ambient-spawn-pose") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* Parse "x,y,z" as three comma-separated floats.
                 * Reject the whole spec on the first malformed
                 * component so a typo doesn't silently land a
                 * half-set override. */
                char *p   = val;
                char *end = NULL;
                float x = strtof(p, &end);
                if (end != p && *end == ',') {
                    p = end + 1;
                    float y = strtof(p, &end);
                    if (end != p && *end == ',') {
                        p = end + 1;
                        float z = strtof(p, &end);
                        if (end != p) {
                            g_ambient_spawn_pose[0] = x;
                            g_ambient_spawn_pose[1] = y;
                            g_ambient_spawn_pose[2] = z;
                            g_ambient_spawn_pose_set = 1;
                        }
                    }
                }
            }
        } else if (lstrcmpA(tok, "--max-duration-ms") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                unsigned n = (unsigned)strtoul(val, NULL, 10);
                if (n > 0) g_max_duration_ms = n;
            }
        } else if (lstrcmpA(tok, "--audio-trace") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char trace_buf[MAX_PATH];
                lstrcpynA(trace_buf, val, (int)sizeof(trace_buf));
                g_audio_trace_path = trace_buf;
            }
        } else if (lstrcmpA(tok, "--save-write") == 0) {
            g_save_write = 1;
        } else if (lstrcmpA(tok, "--play-se") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* Comma-separated decimal slot indices, capped at
                 * PLAY_SE_MAX_SLOTS. Bad tokens silently skipped. */
                char *p = val;
                while (*p && g_play_se_count < PLAY_SE_MAX_SLOTS) {
                    char *end = NULL;
                    long n = strtol(p, &end, 10);
                    if (end != p && n >= 0 && n < 110) {
                        g_play_se_slots[g_play_se_count++] = (int)n;
                    }
                    if (end == NULL || *end == '\0') break;
                    p = end + (*end == ',' ? 1 : 0);
                    if (*end != ',') break;
                }
            }
        } else if (lstrcmpA(tok, "--play-se-after-ms") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                unsigned n = (unsigned)strtoul(val, NULL, 10);
                if (n > 0) g_play_se_after_ms = n;
            }
        } else if (lstrcmpA(tok, "--play-se-interval-ms") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                unsigned n = (unsigned)strtoul(val, NULL, 10);
                if (n > 0) g_play_se_interval_ms = n;
            }
        } else if (lstrcmpA(tok, "--input-trace-record") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char rec_buf[MAX_PATH];
                lstrcpynA(rec_buf, val, (int)sizeof(rec_buf));
                g_input_trace_record_path = rec_buf;
            }
        } else if (lstrcmpA(tok, "--input-trace-replay") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char rep_buf[MAX_PATH];
                lstrcpynA(rep_buf, val, (int)sizeof(rep_buf));
                g_input_trace_replay_path = rep_buf;
            }
        } else if (lstrcmpA(tok, "--input-segtrace") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char seg_buf[MAX_PATH];
                lstrcpynA(seg_buf, val, (int)sizeof(seg_buf));
                g_input_segtrace_path = seg_buf;
            }
        } else if (lstrcmpA(tok, "--anchor-trace-record") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char anc_buf[MAX_PATH];
                lstrcpynA(anc_buf, val, (int)sizeof(anc_buf));
                g_anchor_trace_record_path = anc_buf;
            }
        } else if (lstrcmpA(tok, "--player-pos-log") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char pos_buf[MAX_PATH];
                lstrcpynA(pos_buf, val, (int)sizeof(pos_buf));
                g_player_pos_log_path = pos_buf;
            }
        } else if (lstrcmpA(tok, "--capture-at-anchor") == 0) {
            /* NAME[+k|-k] — e.g. HOUSE_FREEROAM+5, LOADING_END, NEW_GAME+0 */
            char *val = strtok(NULL, " ");
            if (val && g_anchor_captures_count < ANCHOR_CAPTURE_MAX) {
                struct anchor_capture_req *req =
                    &g_anchor_captures[g_anchor_captures_count];
                /* Split on the first +/- (anchor names are UPPER_SNAKE,
                 * no digits or signs), the rest is a signed offset. */
                int   off = 0;
                char *sep = val;
                while (*sep && *sep != '+' && *sep != '-') sep++;
                if (*sep) { off = (int)strtol(sep, NULL, 10); }
                size_t nlen = (size_t)(sep - val);
                if (nlen >= sizeof(req->name)) nlen = sizeof(req->name) - 1;
                memcpy(req->name, val, nlen);
                req->name[nlen] = '\0';
                req->offset     = off;
                g_anchor_captures_count++;
            }
        } else if (lstrcmpA(tok, "--rng-seed") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                g_rng_seed_value = (uint32_t)strtoul(val, NULL, 0);
                g_rng_seed_set   = 1;
            }
        } else if (lstrcmpA(tok, "--max-frames") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                unsigned n = (unsigned)strtoul(val, NULL, 10);
                if (n > 0) g_max_frames = n;
            }
        } else if (lstrcmpA(tok, "--capture-frames") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* Comma-separated decimal sim-frame indices, capped at
                 * CAPTURE_FRAMES_MAX. Bad tokens silently skipped. */
                char *p = val;
                while (*p && g_capture_frames_count < CAPTURE_FRAMES_MAX) {
                    char *end = NULL;
                    long n = strtol(p, &end, 10);
                    if (end != p && n >= 0) {
                        g_capture_frames[g_capture_frames_count++] = (uint32_t)n;
                    }
                    if (end == NULL || *end == '\0') break;
                    p = end + (*end == ',' ? 1 : 0);
                    if (*end != ',') break;
                }
            }
        } else if (lstrcmpA(tok, "--d3d-trace") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char d3d_buf[MAX_PATH];
                lstrcpynA(d3d_buf, val, (int)sizeof(d3d_buf));
                g_d3d_trace_path = d3d_buf;
            }
        } else if (lstrcmpA(tok, "--d3d-trace-frames") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                /* Same parser shape as --capture-frames: comma-separated
                 * decimal sim-frame indices, capped at D3D_TRACE_FRAMES_MAX. */
                char *p = val;
                while (*p && g_d3d_trace_frames_count
                                 < D3D_TRACE_FRAMES_MAX) {
                    char *end = NULL;
                    long n = strtol(p, &end, 10);
                    if (end != p && n >= 0) {
                        g_d3d_trace_frames[g_d3d_trace_frames_count++] =
                            (unsigned)n;
                    }
                    if (end == NULL || *end == '\0') break;
                    p = end + (*end == ',' ? 1 : 0);
                    if (*end != ',') break;
                }
            }
        } else if (lstrcmpA(tok, "--call-trace") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                static char call_buf[MAX_PATH];
                lstrcpynA(call_buf, val, (int)sizeof(call_buf));
                g_call_trace_path = call_buf;
            }
        } else if (lstrcmpA(tok, "--call-trace-frames") == 0) {
            char *val = strtok(NULL, " ");
            if (val) {
                char *p = val;
                while (*p && g_call_trace_frames_count
                                 < CALL_TRACE_FRAMES_MAX_CLI) {
                    char *end = NULL;
                    long n = strtol(p, &end, 10);
                    if (end != p && n >= 0) {
                        g_call_trace_frames[g_call_trace_frames_count++] =
                            (unsigned)n;
                    }
                    if (end == NULL || *end == '\0') break;
                    p = end + (*end == ',' ? 1 : 0);
                    if (*end != ',') break;
                }
            }
        } else if (lstrcmpA(tok, "--hidden") == 0) {
            g_hidden = 1;
        } else if (lstrcmpA(tok, "--auto-z-spam") == 0) {
            g_auto_z_spam = 1;
        } else if (lstrcmpA(tok, "--turbo") == 0) {
            g_turbo = 1;
        } else if (lstrcmpA(tok, "--silent-audio") == 0) {
            g_silent_audio = 1;
        } else if (lstrcmpA(tok, "--no-singleton") == 0) {
            g_no_singleton = 1;
        } else if (lstrcmpA(tok, "--no-msgbox-hook") == 0) {
            g_no_msgbox_hook = 1;
        }
        tok = strtok(NULL, " ");
    }
}

/* ─── Phase A wrappers ─────────────────────────────────────────────────
 *
 * Replay: write the recorded mask for the upcoming frame straight into
 *   g_input_state[0].buttons; player 1 stays zero. This shadows
 *   input_poll's first two lines (the pre-poll clear) + the keyboard
 *   accumulator step. No DirectInput call — input_init was skipped.
 *
 * Record: call the real input_poll, then snapshot the resulting mask.
 *   `record_frame(g_tick.frame_count, …)` records under the frame
 *   index of the frame we're about to render (tick.c bumps frame_count
 *   AFTER render returns), so trace lines line up with capture indices.
 */
static void replay_input_poll(void)
{
    g_input_state[0].buttons =
        input_trace_lookup(&g_replay_trace, g_tick.frame_count);
    g_input_state[1].buttons = 0;
}

/* Capture sink for input_segtrace `{capture:N}` ops: append the resolved
 * absolute frame to the same list --capture-frames / --capture-at-anchor use
 * (capture_frame_is_listed() picks it up later in render_dispatch). */
static void segtrace_capture_cb(uint32_t frame, void *user)
{
    (void)user;
    if (g_capture_frames_count >= CAPTURE_FRAMES_MAX) {
        fprintf(stderr, "segtrace: capture list full — dropping frame %u\n",
                (unsigned)frame);
        return;
    }
    g_capture_frames[g_capture_frames_count++] = frame;
    fprintf(stderr, "segtrace: scheduled capture at frame %u\n", (unsigned)frame);
}

/* Call-trace window sink for input_segtrace `{calltrace:[start,len]}` ops:
 * arm the resolved [lo, hi) window on the port's call tracer. */
static void segtrace_ct_cb(uint32_t lo, uint32_t hi, void *user)
{
    (void)user;
    call_trace_arm_window(lo, hi);
    fprintf(stderr, "segtrace: armed call-trace window [%u, %u)\n",
            (unsigned)lo, (unsigned)hi);
}

/* --input-segtrace replacement for input_poll: anchor-segmented forcing.
 * Anchor fire-frames are fed in from anchor_emit_tee(); captures land via
 * segtrace_capture_cb. */
static void segtrace_input_poll(void)
{
    g_input_state[0].buttons =
        input_segtrace_tick(&g_segtrace, g_tick.frame_count,
                            segtrace_capture_cb, NULL);
    g_input_state[1].buttons = 0;
}

static void recording_input_poll(void)
{
    input_poll();
    input_trace_record_frame(g_tick.frame_count, g_input_state[0].buttons);
}

/* --auto-z-spam wrapper.  Runs the real input_poll (so any user
 * keystrokes still count) then OR's in button A on alternating
 * frames.  Press-then-release at 30 Hz gives the engine's button-edge
 * detection a clean transition every other tick — same shape the
 * Frida agent uses on the retail side. */
static void auto_z_spam_input_poll(void)
{
    input_poll();
    /* Toggle every frame so the engine sees press-then-release; any
     * menu that uses edge detection (most title-menu nav does) will
     * fire on the press half-frame. */
    if ((g_tick.frame_count & 1u) == 0u) {
        g_input_state[0].buttons |= 0x0010u;  /* button A */
    } else {
        g_input_state[0].buttons &= ~0x0010u;
    }
}

static int capture_frame_is_listed(uint32_t frame)
{
    for (int i = 0; i < g_capture_frames_count; i++) {
        if (g_capture_frames[i] == frame) return 1;
    }
    return 0;
}

/* ─── BMP back-buffer capture ────────────────────────────────────────────
 * Writes a 32-bit top-down BMP to <g_capture_dir>\frame_NNNNN.bmp.
 * Layout: BITMAPFILEHEADER (14 bytes) + BITMAPINFOHEADER (40 bytes) + pixels.
 * Negative biHeight → top-down; stride = w*4 (X8R8G8B8 is already BGRA-ish).
 */
static void capture_backbuffer(void)
{
    IDirect3DSurface8 *surf = NULL;
    if (FAILED(IDirect3DDevice8_GetBackBuffer(
            g_dev, 0, D3DBACKBUFFER_TYPE_MONO, &surf))) return;

    D3DSURFACE_DESC desc = {0};
    if (FAILED(IDirect3DSurface8_GetDesc(surf, &desc))) {
        IDirect3DSurface8_Release(surf);
        return;
    }

    D3DLOCKED_RECT lr = {0};
    if (FAILED(IDirect3DSurface8_LockRect(surf, &lr, NULL, D3DLOCK_READONLY))) {
        IDirect3DSurface8_Release(surf);
        return;
    }

    DWORD w = desc.Width, h = desc.Height;
    DWORD row_bytes = w * 4;
    DWORD img_size  = row_bytes * h;

    /* 14-byte BITMAPFILEHEADER */
    uint8_t fhdr[14];
    DWORD file_size = 14 + 40 + img_size;
    fhdr[0]  = 'B';  fhdr[1]  = 'M';
    fhdr[2]  = (uint8_t)(file_size);
    fhdr[3]  = (uint8_t)(file_size >> 8);
    fhdr[4]  = (uint8_t)(file_size >> 16);
    fhdr[5]  = (uint8_t)(file_size >> 24);
    fhdr[6]  = 0; fhdr[7]  = 0;           /* reserved */
    fhdr[8]  = 0; fhdr[9]  = 0;           /* reserved */
    fhdr[10] = 54; fhdr[11] = 0;          /* pixel data offset = 54 */
    fhdr[12] = 0;  fhdr[13] = 0;

    /* 40-byte BITMAPINFOHEADER — negative height = top-down */
    int32_t  neg_h   = -(int32_t)h;
    uint8_t  ihdr[40] = {0};
    ihdr[0]  = 40;                         /* biSize */
    ihdr[4]  = (uint8_t)(w);
    ihdr[5]  = (uint8_t)(w >> 8);
    ihdr[6]  = (uint8_t)(w >> 16);
    ihdr[7]  = (uint8_t)(w >> 24);
    ihdr[8]  = (uint8_t)(neg_h);
    ihdr[9]  = (uint8_t)((uint32_t)neg_h >> 8);
    ihdr[10] = (uint8_t)((uint32_t)neg_h >> 16);
    ihdr[11] = (uint8_t)((uint32_t)neg_h >> 24);
    ihdr[12] = 1;  ihdr[13] = 0;          /* biPlanes = 1 */
    ihdr[14] = 32; ihdr[15] = 0;          /* biBitCount = 32 */
    /* biCompression = BI_RGB = 0 (already zeroed) */
    ihdr[20] = (uint8_t)(img_size);
    ihdr[21] = (uint8_t)(img_size >> 8);
    ihdr[22] = (uint8_t)(img_size >> 16);
    ihdr[23] = (uint8_t)(img_size >> 24);
    /* biXPelsPerMeter, biYPelsPerMeter, biClrUsed, biClrImportant = 0 */

    /* Filename: under --capture-frames, name by sim-frame index so the
     * scenario runner can match against `golden/frame_NNNNN.bmp` by
     * the same number. Without --capture-frames (legacy time-based
     * capture), fall back to the monotonic capture counter. */
    char path[MAX_PATH];
    unsigned tag = (g_capture_frames_count > 0)
                       ? (unsigned)g_tick.frame_count
                       : g_capture_count;
    wsprintfA(path, "%s\\frame_%05u.bmp", g_capture_dir, tag);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        /* fopen runs Windows-side under WSLInterop: g_capture_dir must be a
         * Windows-resolvable path (drive-letter or \\wsl.localhost UNC), NOT
         * a Unix /path or a relative dir (which resolves against the game
         * asset cwd, not the repo).  Surface the failure loudly so capture
         * runs don't silently produce an empty dir — see run-openrecet.sh,
         * which auto-converts --capture-to via `wslpath -w`. */
        fprintf(stderr,
                "capture: FAILED to open '%s' (frame %u) — is --capture-to a "
                "Windows-resolvable path? (use run-openrecet.sh, which "
                "converts it)\n",
                path, tag);
    }
    if (fp) {
        fwrite(fhdr, 1, 14, fp);
        fwrite(ihdr, 1, 40, fp);
        /* Write row by row in case pitch > row_bytes */
        const uint8_t *src = (const uint8_t *)lr.pBits;
        for (DWORD row = 0; row < h; row++) {
            fwrite(src + row * (DWORD)lr.Pitch, 1, row_bytes, fp);
        }
        fclose(fp);
        fprintf(stderr, "capture: wrote %s (%lux%lu)\n",
                path, (unsigned long)w, (unsigned long)h);
    }

    IDirect3DSurface8_UnlockRect(surf);
    IDirect3DSurface8_Release(surf);
}
