/*
 * scene_jutan.c — see scene_jutan.h.
 *
 * Engine source: FUN_0047486a @ 0x47486a (142 bytes). Filename table
 * at 0x5c7ff4..0x5c8014 (8 entries). Sprintf format at 0x5ca3d8.
 * Per-stage selector at offset 0x584 of the 0x2dfc8-byte stage record.
 */

#include "scene_jutan.h"

#include <stdio.h>

#include "worker_load.h"

/* ─── pre-baked filename table ────────────────────────────────────────── */

/* Extracted from vendor/unpacked/recettear.unpacked.exe via
 *   tools/analyze/pe.py str 0x005ca334 0x005ca348 … 0x005ca3c4
 * Order matches the pointer table at 0x5c7ff4. The engine never
 * mutates these strings — they live in .rdata.
 *
 * Note slot 0 is the only .tga in the set — `shop_jutan01.tga`. The
 * remaining 7 are .bmp. sprite_load sniffs the magic so the extension
 * is just convention. */
static const char *const g_jutan_filenames[SCENE_JUTAN_COUNT] = {
    "shop_jutan01.tga",
    "shop_jutan03.bmp",
    "capet_tora.bmp",
    "shop_jutan_umi.bmp",
    "shop_jutan_check.bmp",
    "shop_jutan_hade.bmp",
    "shop_jutan_kawai.bmp",
    "shop_jutan_jya.bmp",
};

/* Engine s_xfile_jutan__s_005ca3d8. */
static const char *const g_jutan_path_fmt = "xfile/jutan/%s";

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_jutan_selector = 0;

const char *scene_jutan_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_JUTAN_COUNT) return 0;
    return g_jutan_filenames[slot];
}

const char *scene_jutan_format_string(void)
{
    return g_jutan_path_fmt;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_jutan_load_with(scene_jutan_load_fn load_fn,
                          void *userdata,
                          int param)
{
    /* Engine FUN_0047486a bodies an 8-iteration loop with the same
     * 1-bit predicate as the wall + floor siblings:
     *   param == 0  →  load i where i == selector
     *   param != 0  →  load i where i != selector
     *
     * Same shape as FUN_0047474e / FUN_004747dc; see scene_walls.c for
     * the analogous comment. */
    const int32_t selector = g_scene_jutan_selector;
    int loads = 0;

    for (int i = 0; i < SCENE_JUTAN_COUNT; i++) {
        int match = (i == selector);
        int do_load = (param == 0) ? match : !match;
        if (!do_load) continue;

        char path[256];
        /* Engine: FUN_005038ff = unbounded sprintf-with-NUL. Longest
         * possible path here: "xfile/jutan/" (12) +
         * "shop_jutan_check.bmp" (20) + NUL = 33. Well under 256. */
        snprintf(path, sizeof(path), g_jutan_path_fmt, g_jutan_filenames[i]);

        if (load_fn) load_fn(path, i, userdata);
        loads++;
    }
    return loads;
}

/* ─── Win32 worker_load wiring + sprite storage ──────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

sprite_t g_scene_jutan[SCENE_JUTAN_COUNT];

static IDirect3DDevice8 *g_scene_jutan_dev = 0;

static int win32_load_fn(const char *path, int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    /* Engine: FUN_0047193c(3, dst, name, 0x100, 0x100). Same as the
     * other sibling loaders. */
    return sprite_load(dev, path, 0x100, 0x100, &g_scene_jutan[slot]);
}

static void scene_jutan_body(void)
{
    /* Engine LAB_00452bc6's inner-body call:
     *     FUN_0047486a(1)
     * objdump @ 0x452bc6..0x452bcb:
     *     push esi ; push $0x1 ; pop esi ; push esi ; call 0x47486a
     * — literal `1` (same push-esi=1 pattern as B3E walls + B82 floor;
     * esi=1 is reused for the post-body fade-kick gate). */
    scene_jutan_load_with(win32_load_fn, g_scene_jutan_dev, 1);
}

void scene_jutan_init(struct IDirect3DDevice8 *dev)
{
    g_scene_jutan_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_BC6, scene_jutan_body);
}

int scene_jutan_load_foreground_win32(struct IDirect3DDevice8 *dev)
{
    return scene_jutan_load_with(win32_load_fn, dev, 0);
}

void scene_jutan_reset(void)
{
    for (int i = 0; i < SCENE_JUTAN_COUNT; i++) {
        g_scene_jutan[i].tex    = 0;
        g_scene_jutan[i].width  = 0;
        g_scene_jutan[i].height = 0;
    }
    g_scene_jutan_selector = 0;
}

#else  /* !_WIN32 — Linux test build */

void scene_jutan_reset(void)
{
    g_scene_jutan_selector = 0;
}

#endif /* _WIN32 */
