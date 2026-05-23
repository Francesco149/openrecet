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

#include "storage.h"
#include "sprite.h"
#include "input.h"
#include "input_trace.h"
#include "layers.h"
#include "tables.h"
#include "recet_ini.h"
#include "prewindow.h"
#include "render_quad.h"
#include "rng.h"
#include "fade.h"
#include "nowloading.h"
#include "scene.h"
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
#include "mesh.h"
#include "mesh_draw.h"
#include "mesh_load.h"
#include "tick.h"

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
static int             g_rng_seed_set            = 0;
static uint32_t        g_rng_seed_value          = 1;
static uint32_t        g_max_frames              = 0;

#define CAPTURE_FRAMES_MAX  32
static uint32_t        g_capture_frames[CAPTURE_FRAMES_MAX];
static int             g_capture_frames_count    = 0;

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
static int             g_silent_audio            = 0;

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

/* Phase A wrappers. `recording_input_poll` is the wrapping
 * input-callback when --input-trace-record is set; `replay_input_poll`
 * replaces it under --input-trace-replay. Both pure-Win32 because they
 * read/write g_input_state[].buttons which only the Win32 build
 * sources via DI. */
static void  recording_input_poll(void);
static void  replay_input_poll(void);
static int   capture_frame_is_listed(uint32_t frame);

/* save_bank header-init hook. Engine calls FUN_00499583
 * (= audio_fade_apply(BGM)) once during FUN_004901c2 when the shared
 * header magic flips from 0 → live; we forward that callback through
 * the existing audio_fade apply hook so save_bank doesn't need a
 * direct dependency on audio.c. */
static void save_bank_apply_bgm_via_audio_fade(void)
{
    audio_fade_apply(AUDIO_FADE_CHANNEL_BGM);
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

    /* TODO "init print ok"   — FUN_00451863 */

    if (!load_d3d8() || !init_render(g_hwnd)) {
        MessageBoxA(g_hwnd, "Failed to initialize Direct3D 8",
                    "openrecet", MB_OK | MB_ICONERROR);
        return 0;
    }
    /* "init start" — section marker logged by FUN_0047ac6a on success. */

    /* "init dinput ok" — FUN_0047af52 — keyboard + up to 4 joysticks.
     * Skipped under --input-trace-replay: a replay drives
     * g_input_state[0].buttons directly from the trace file, so we
     * don't want live keypresses leaking into the simulated frame. */
    if (!g_input_trace_replay_path) {
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
            g_input_trace_replay_path = NULL;
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
    if (g_input_trace_replay_path) {
        active_input_poll = replay_input_poll;
    } else if (g_input_trace_record_path) {
        active_input_poll = recording_input_poll;
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
            if (g_input_trace_replay_path) g_paused = FALSE;

            if (g_paused) {
                WaitMessage();
            } else {
                /* Under --input-trace-replay, drive virtual time so
                 * the tick scheduler never returns DELAYED — we want
                 * exactly one ticked frame per loop iteration, no
                 * wall-clock gating, no Sleep. 20ms per call >
                 * threshold[0]=16.67ms so each call is TICKED. */
                if (g_input_trace_replay_path) {
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
    sprite_destroy(&g_show_sprite);
    if (g_show_mesh) { mesh_free(g_show_mesh); g_show_mesh = NULL; }
    mesh_tex_cache_reset();
    fade_unload_system_texture();
    scene_title_unload_assets();
    if (!g_input_trace_replay_path) {
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
            /* original: FUN_00452911() — open in-game pause menu.
             * Skeleton: just close on ESC for now. */
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
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
    if (g_dev) { IDirect3DDevice8_Release(g_dev); g_dev = NULL; }
    if (g_d3d) { IDirect3D8_Release(g_d3d); g_d3d = NULL; }
    if (g_d3d8_dll) { FreeLibrary(g_d3d8_dll); g_d3d8_dll = NULL; }
}

/* ─── frame render — partial FUN_004547ab port ──────────────────────────
 * Driven by tick_step_win32 as the `render` callback. The engine's
 * full FUN_004547ab dispatch fans into many per-state render functions
 * (FUN_0045bbf9 / FUN_0040a765 / FUN_00417504 / FUN_0045404b / etc.) —
 * we port them one scene at a time. Today:
 *   SCENE_STATE_TITLE   — scene_title_render
 *   SCENE_STATE_INGAME  — scene_ingame_render (placeholder)
 * Other states leave the back buffer at the per-state clear color.
 *
 * Engine clear color for state-0 is 0xff17f0ff (pink-blue) — visible
 * only at the edges before bg2.bmp fully covers the framebuffer.
 *
 * BeginScene/EndScene/Present + the screen-capture sample point all
 * live here. The capture has to run before Present because
 * D3DSWAPEFFECT_DISCARD leaves the post-Present back buffer undefined.
 */
static void render_dispatch(void)
{
    if (!g_dev) return;

    /* Per-state clear color. Engine FUN_004547ab L33-44 derives the
     * scene-1 clear from DAT_068dd2f0's stage palette; we use a fixed
     * placeholder until the stage system ports. Title clear stays at
     * 0xff17f0ff. */
    DWORD clear_argb = 0xff17f0ff;
    if (g_scene_state == SCENE_STATE_INGAME) {
        clear_argb = scene_ingame_clear_argb();
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
            scene_ingame_render(g_dev);
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
        if (g_capture_frames_count > 0) {
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
        } else if (lstrcmpA(tok, "--hidden") == 0) {
            g_hidden = 1;
        } else if (lstrcmpA(tok, "--turbo") == 0) {
            g_turbo = 1;
        } else if (lstrcmpA(tok, "--silent-audio") == 0) {
            g_silent_audio = 1;
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

static void recording_input_poll(void)
{
    input_poll();
    input_trace_record_frame(g_tick.frame_count, g_input_state[0].buttons);
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
    if (fp) {
        fwrite(fhdr, 1, 14, fp);
        fwrite(ihdr, 1, 40, fp);
        /* Write row by row in case pitch > row_bytes */
        const uint8_t *src = (const uint8_t *)lr.pBits;
        for (DWORD row = 0; row < h; row++) {
            fwrite(src + row * (DWORD)lr.Pitch, 1, row_bytes, fp);
        }
        fclose(fp);
    }

    IDirect3DSurface8_UnlockRect(surf);
    IDirect3DSurface8_Release(surf);
}
