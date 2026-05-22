/*
 * scene_walls.c — see scene_walls.h.
 *
 * Engine source: FUN_0047474e @ 0x47474e (142 bytes). Filename table
 * at 0x5c7f78..0x5c7fb4 (15 entries). Sprintf format at 0x5ca210.
 * Per-stage selector at offset 0x57c of the 0x2dfc8-byte stage record
 * (DAT_0451057c + DAT_0438b1e0 * 0x2dfc8 in engine).
 */

#include "scene_walls.h"

#include <stdio.h>

#include "worker_load.h"

/* ─── pre-baked filename table ────────────────────────────────────────── */

/* Extracted from vendor/unpacked/recettear.unpacked.exe via
 *   tools/analyze/pe.py str 0x005ca11c 0x005ca12c … 0x005ca200
 * Order matches the pointer table at 0x5c7f78. The engine never
 * mutates these strings — they live in .rdata. */
static const char *const g_wall_filenames[SCENE_WALLS_COUNT] = {
    "kabe_sikkui.bmp",
    "kabe_ita.bmp",
    "kabe_hosi.bmp",
    "kabe_umi.bmp",
    "kabe_moru.bmp",
    "kabe_renga.bmp",
    "kabe_giseki.bmp",
    "kabe_8bit.bmp",
    "kabe_jya.bmp",
    "kabe_iseki.bmp",
    "kabe_euria.bmp",
    "kabe_namako.bmp",
    "kabe_chuka.bmp",
    "kabe_kouhaku.bmp",
    "kabe_check.bmp",
};

/* Engine s_xfile_wall__s_005ca210. */
static const char *const g_wall_path_fmt = "xfile/wall/%s";

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_walls_selector = 0;

const char *scene_walls_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_WALLS_COUNT) return 0;
    return g_wall_filenames[slot];
}

const char *scene_walls_format_string(void)
{
    return g_wall_path_fmt;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_walls_load_with(scene_walls_load_fn load_fn,
                          void *userdata,
                          int param)
{
    /* Engine FUN_0047474e bodies a 15-iteration loop with a 1-bit
     * predicate inverted by `param`:
     *   param == 0  →  load i where i == selector
     *   param != 0  →  load i where i != selector
     *
     * The selector range-check is implicit: an out-of-range selector
     * (e.g. -1 or 15) means no slot matches, so param=0 loads nothing
     * and param=1 loads everything. Matches engine behaviour. */
    const int32_t selector = g_scene_walls_selector;
    int loads = 0;

    for (int i = 0; i < SCENE_WALLS_COUNT; i++) {
        int match = (i == selector);
        int do_load = (param == 0) ? match : !match;
        if (!do_load) continue;

        char path[256];
        /* Engine: FUN_005038ff = unbounded sprintf-with-NUL. We use
         * snprintf for safety; longest possible path is "xfile/wall/"
         * (11) + "kabe_kouhaku.bmp" (16) + NUL = 28 bytes, well under
         * 256. */
        snprintf(path, sizeof(path), g_wall_path_fmt, g_wall_filenames[i]);

        if (load_fn) load_fn(path, i, userdata);
        loads++;
    }
    return loads;
}

/* ─── Win32 worker_load wiring + sprite storage ──────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

sprite_t g_scene_walls[SCENE_WALLS_COUNT];

static IDirect3DDevice8 *g_scene_walls_dev = 0;

/* Win32 load_fn: drives sprite_load against the per-slot sprite_t. */
static int win32_load_fn(const char *path, int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    /* Engine: FUN_0047193c(3, dst, name, 0x100, 0x100). The first arg
     * (format flag = 3) is dropped here — same as the rest of the
     * openrecet sprite_load call sites. */
    return sprite_load(dev, path, 0x100, 0x100, &g_scene_walls[slot]);
}

static void scene_walls_body(void)
{
    /* Engine LAB_00452b3e's inner-body call:
     *     FUN_0047474e(1)
     * objdump @ 0x452b3e..0x452b43:
     *     push esi ; push $0x1 ; pop esi ; push esi ; call 0x47474e
     * — the literal `1` is hard-coded at this call site. ESI=1 is
     * reused for the subsequent `cmp esi, [DAT_06a49980]` fade-kick
     * gate (worker_load_sec_post_body), which is THE place where the
     * latched param matters.
     *
     * The OTHER call site for FUN_0047474e (in the main-thread scene-1
     * init around line 73066 of decompiled/all.c) passes literal `0`
     * — i.e. "load only the selector". Param is always a compile-time
     * constant for this loader; it's never a runtime variable.
     *
     * Earlier port chip passed g_worker_sec_param here — corrected
     * 2026-05-22 after asm re-read. Dormant bug: no caller of
     * worker_load_spawn_d85 exists yet so the mismatch never fired. */
    scene_walls_load_with(win32_load_fn, g_scene_walls_dev, 1);
}

void scene_walls_init(struct IDirect3DDevice8 *dev)
{
    g_scene_walls_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B3E, scene_walls_body);
}

void scene_walls_reset(void)
{
    for (int i = 0; i < SCENE_WALLS_COUNT; i++) {
        /* Win32: same as sprite_destroy minus the IDirect3D release.
         * Tests on Win32 don't run sprite_load so the textures are
         * always NULL; engine path uses sysassets-style release at
         * device-loss time, which we defer with the rest of the lost-
         * device handling. */
        g_scene_walls[i].tex    = 0;
        g_scene_walls[i].width  = 0;
        g_scene_walls[i].height = 0;
    }
    g_scene_walls_selector = 0;
}

#else  /* !_WIN32 — Linux test build */

void scene_walls_reset(void)
{
    g_scene_walls_selector = 0;
}

#endif /* _WIN32 */
