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
#include "layers.h"
#include "tables.h"
#include "recet_ini.h"
#include "prewindow.h"
#include "render_quad.h"
#include "rng.h"
#include "scene_title.h"
#include "sim.h"
#include "music.h"
#include "audio.h"
#include "audio_fade.h"
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
     * subsequent rand calls during gameplay are non-deterministic. */
    rng_seed_from_now();

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

    /* "init dinput ok" — FUN_0047af52 — keyboard + up to 4 joysticks. */
    input_init(g_hInstance, g_hwnd);

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

    /* "init indexfile ok" — FUN_00475270 — gameplay-table loader.
     * Currently skeleton only: dispatcher fires all 14 storage reads
     * (proving the assets resolve) but the per-file parsers are stubs
     * that just log the size. Parsers land one per commit in Phase B
     * — see docs/findings/tables-loader.md. */
    tables_load_all();

    /* 2D quad batcher — one-time vbuf prefill + screen-width-scale. */
    render_quad_init((uint32_t)g_ini.width);

    /* "read titletex ok" — FUN_004733d5 — load the 7 title-scene
     * textures (bg2, 01, fuki, waku + pause/result/dungeon). Sets
     * g_scene_title_assets_loaded on full success. */
    (void)scene_title_load_assets(g_dev);

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

    /* "init daoudio ok" — FUN_00498ef4 — DirectMusic 8: Performance +
     * Loader + BGM AudioPath + preload all 21 segments. Sound-effect
     * path (2 more AudioPaths + 27 resource-loaded WAVs) lands in the
     * next commit. On failure, log + continue silently — boot still
     * works, just without music. */
    if (!audio_init(g_hwnd)) {
        fprintf(stderr, "openrecet: audio_init failed — running muted\n");
    }

    /* Seed BGM/SE-A volume sliders from recet.ini. Engine doesn't do this —
     * it owns sliders in the save-arena (FUN_004901c2 inits 5/9/9, save-load
     * FUN_004902fe overwrites). Until save-load lands, recet.ini is the
     * closest persistent user-configurable source. SE-B is dormant in vendor
     * data (engine-quirks #46), no recet.ini key for it. */
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM,  g_ini.mu);
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_SE_A, g_ini.se);
    fprintf(stderr, "audio: sliders seeded from recet.ini — bgm=%d se-a=%d\n",
            g_ini.mu, g_ini.se);

    /* TODO "init fontsys ok"    — FUN_0047c228
     * TODO "fontsystem ok"      — FUN_0047c3a5
     * TODO "read systemtex ok"  — FUN_00472f5d
     * TODO "load savefile ok"   — FUN_004902fe
     * TODO "read titletex ok"   — FUN_0043609b
     * TODO bootstrap done       — FUN_0049a3a3 (enters main loop)
     */

    ShowWindow(g_hwnd, nCmdShow);
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
     * src/audio.{c,h} + src/music.{c,h}). */
    const struct tick_callbacks tick_cb = {
        .input_poll = input_poll,
        .sim_a      = sim_step_a,
        .sim_b      = music_step_default,
        .render     = render_dispatch,
    };
    tick_init();
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
            if (g_paused) {
                WaitMessage();
            } else {
                tick_step_win32(g_d3d != NULL && g_dev != NULL, &tick_cb);

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
            }
        }
        if (!GetMessageA(&msg, NULL, 0, 0)) break;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    /* ─── shutdown ──────────────────────────────────────────────────────── */
    audio_shutdown();
    audio_trace_close();
    sprite_destroy(&g_show_sprite);
    scene_title_unload_assets();
    input_shutdown();
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
 * Driven by tick_step_win32 as the `render` callback. Currently only the
 * scene==0 (title) dispatch path is wired up; other scene states fall
 * through to a blank clear. The full FUN_004547ab dispatch + per-state
 * fan-out lands as the other scene ports come online.
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

    /* Engine's title-state clear color: ARGB(0xff, 0x17, 0xf0, 0xff). */
    IDirect3DDevice8_Clear(
        g_dev, 0, NULL,
        D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
        0xff17f0ff,
        1.0f, 0);
    IDirect3DDevice8_BeginScene(g_dev);

    if (g_scene_title_assets_loaded) {
        scene_title_render(g_dev,
                           &g_scene_title_menu,
                           &g_scene_title_anim);
    }

    if (g_show_sprite.tex) {
        sprite_draw(g_dev, &g_show_sprite, 32.0f, 32.0f);
    }
    IDirect3DDevice8_EndScene(g_dev);

    if (g_capture_dir) {
        unsigned now_ms = timeGetTime();
        if (g_capture_last_ms == 0 ||
            (now_ms - g_capture_last_ms) >= g_capture_every_ms) {
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
        }
        tok = strtok(NULL, " ");
    }
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

    char path[MAX_PATH];
    wsprintfA(path, "%s\\frame_%05u.bmp", g_capture_dir, g_capture_count);

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
