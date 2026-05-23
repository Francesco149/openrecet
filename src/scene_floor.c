/*
 * scene_floor.c — see scene_floor.h.
 *
 * Engine source: FUN_004747dc @ 0x4747dc (142 bytes). Filename table
 * at 0x5c7fb8..0x5c7ff4 (15 entries). Sprintf format at 0x5ca324.
 * Per-stage selector at offset 0x580 of the 0x2dfc8-byte stage record
 * (DAT_04510580 + DAT_0438b1e0 * 0x2dfc8 in engine).
 */

#include "scene_floor.h"

#include <stdio.h>

#include "worker_load.h"

/* ─── pre-baked filename table ────────────────────────────────────────── */

/* Extracted from vendor/unpacked/recettear.unpacked.exe via
 *   tools/analyze/pe.py str 0x005ca220 0x005ca230 … 0x005ca314
 * Order matches the pointer table at 0x5c7fb8. The engine never
 * mutates these strings — they live in .rdata. */
static const char *const g_floor_filenames[SCENE_FLOOR_COUNT] = {
    "yuka_ita2.bmp",
    "yuka_dairiseki.bmp",
    "yuka_ishi.bmp",
    "yuka_renga.bmp",
    "yuka_isekiyuka.bmp",
    "yuka_tatami.bmp",
    "yuka_akaju.bmp",
    "yuka_rapyuta.bmp",
    "yuka_mori.bmp",
    "yuka_tuchi.bmp",
    "yuka_kyousitu.bmp",
    "yuka_chaishi.bmp",
    "yuka_euria.bmp",
    "yuka_check.bmp",
    "yuka_jya.bmp",
};

/* Engine s_xfile_floor__s_005ca324. */
static const char *const g_floor_path_fmt = "xfile/floor/%s";

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_floor_selector = 0;

const char *scene_floor_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_FLOOR_COUNT) return 0;
    return g_floor_filenames[slot];
}

const char *scene_floor_format_string(void)
{
    return g_floor_path_fmt;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_floor_load_with(scene_floor_load_fn load_fn,
                          void *userdata,
                          int param)
{
    /* Engine FUN_004747dc bodies a 15-iteration loop with a 1-bit
     * predicate inverted by `param`:
     *   param == 0  →  load i where i == selector
     *   param != 0  →  load i where i != selector
     *
     * Same shape as the sibling wall loader at FUN_0047474e — see
     * scene_walls.c for the analogous comment. */
    const int32_t selector = g_scene_floor_selector;
    int loads = 0;

    for (int i = 0; i < SCENE_FLOOR_COUNT; i++) {
        int match = (i == selector);
        int do_load = (param == 0) ? match : !match;
        if (!do_load) continue;

        char path[256];
        /* Engine: FUN_005038ff = unbounded sprintf-with-NUL. We use
         * snprintf for safety; longest possible path is "xfile/floor/"
         * (12) + "yuka_isekiyuka.bmp" (18) + NUL = 31 bytes, well under
         * 256. */
        snprintf(path, sizeof(path), g_floor_path_fmt, g_floor_filenames[i]);

        if (load_fn) load_fn(path, i, userdata);
        loads++;
    }
    return loads;
}

/* ─── Win32 worker_load wiring + sprite storage ──────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

sprite_t g_scene_floor[SCENE_FLOOR_COUNT];

static IDirect3DDevice8 *g_scene_floor_dev = 0;

/* Win32 load_fn: drives sprite_load against the per-slot sprite_t. */
static int win32_load_fn(const char *path, int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    /* Engine: FUN_0047193c(3, dst, name, 0x100, 0x100). The first arg
     * (format flag = 3) is dropped here — same as the rest of the
     * openrecet sprite_load call sites. */
    return sprite_load(dev, path, 0x100, 0x100, &g_scene_floor[slot]);
}

static void scene_floor_body(void)
{
    /* Engine LAB_00452b82's inner-body call:
     *     FUN_004747dc(1)
     * objdump @ 0x452b82..0x452b87:
     *     push esi ; push $0x1 ; pop esi ; push esi ; call 0x4747dc
     * — the literal `1` is hard-coded at this call site. ESI=1 is
     * reused for the subsequent `cmp esi, [DAT_06a49980]` fade-kick
     * gate, which is THE place where the latched param matters.
     *
     * The OTHER call site for FUN_004747dc (in the main-thread scene-1
     * init around line 73067 of decompiled/all.c) passes literal `0`
     * — i.e. "load only the selector". Param is always a compile-time
     * constant for this loader; it's never a runtime variable. */
    scene_floor_load_with(win32_load_fn, g_scene_floor_dev, 1);
}

void scene_floor_init(struct IDirect3DDevice8 *dev)
{
    g_scene_floor_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B82, scene_floor_body);
}

int scene_floor_load_foreground_win32(struct IDirect3DDevice8 *dev)
{
    return scene_floor_load_with(win32_load_fn, dev, 0);
}

void scene_floor_reset(void)
{
    for (int i = 0; i < SCENE_FLOOR_COUNT; i++) {
        /* Win32: same as sprite_destroy minus the IDirect3D release.
         * Tests on Win32 don't run sprite_load so the textures are
         * always NULL; engine path uses sysassets-style release at
         * device-loss time, which we defer with the rest of the lost-
         * device handling. */
        g_scene_floor[i].tex    = 0;
        g_scene_floor[i].width  = 0;
        g_scene_floor[i].height = 0;
    }
    g_scene_floor_selector = 0;
}

#else  /* !_WIN32 — Linux test build */

void scene_floor_reset(void)
{
    g_scene_floor_selector = 0;
}

#endif /* _WIN32 */
