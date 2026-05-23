/*
 * scene_table.c — see scene_table.h.
 *
 * Engine source: FUN_004748f8 @ 0x4748f8 (169 bytes). Pair-of-pointers
 * table at 0x5c8018..0x5c8058 (16 entries / 8 pairs). Sprintf format
 * at 0x5ca500 (and a duplicate at 0x5ca510 — same content, same call).
 * Per-stage selector at offset 0x588 of the 0x2dfc8-byte stage record
 * (DAT_04510588 + DAT_0438b1e0 * 0x2dfc8 in engine).
 */

#include "scene_table.h"

#include <stdio.h>
#include <string.h>

#include "worker_load.h"

/* ─── pre-baked filename table ────────────────────────────────────────── */

/* Order matches the pointer table at 0x5c8018; pairs are (0,1), (2,3),
 * … (14,15). All strings .rdata at 0x5ca3e8..0x5ca4ff. */
static const char *const g_table_filenames[SCENE_TABLE_SLOT_COUNT] = {
    "shop_table01.x",      "shop_table02.x",
    "shop_danbo01.x",      "shop_danbo02.x",
    "shop_desk01.x",       "shop_desk02.x",
    "shop_tarudesk01.x",   "shop_tarudesk02.x",
    "shop_shokutaku01.x",  "shop_shokutaku02.x",
    "shop_kyoudan01.x",    "shop_kyoudan02.x",
    "shop_jya01.x",        "shop_jya02.x",
    "shop_jwel01.x",       "shop_jwel02.x",
};

static const char *const g_table_path_fmt = "xfile/table/%s";

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_table_selector = 0;

const char *scene_table_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_TABLE_SLOT_COUNT) return 0;
    return g_table_filenames[slot];
}

const char *scene_table_format_string(void) { return g_table_path_fmt; }

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_table_load_with(scene_table_load_fn load_fn,
                          void *userdata,
                          int param)
{
    /* Engine FUN_004748f8: 8-pair loop, per-pair predicate identical
     * to scene_floor/jutan/walls:
     *   param == 0  →  load pairs where pair_index == selector
     *   param != 0  →  load pairs where pair_index != selector
     *
     * Each matching pair fires TWO loader calls — first to the slot
     * BEFORE local_8 (engine `local_8 + -0x28`) for filenames[pair*2],
     * then to local_8 for filenames[pair*2+1]. */
    const int32_t selector = g_scene_table_selector;
    int loads = 0;

    for (int pair = 0; pair < SCENE_TABLE_PAIR_COUNT; pair++) {
        int match = (pair == selector);
        int do_load = (param == 0) ? match : !match;
        if (!do_load) continue;

        for (int half = 0; half < 2; half++) {
            int slot = pair * 2 + half;
            char path[256];
            snprintf(path, sizeof path, g_table_path_fmt, g_table_filenames[slot]);
            if (load_fn) load_fn(path, slot, userdata);
            loads++;
        }
    }
    return loads;
}

/* ─── Win32 worker_load wiring + mesh storage ─────────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

#include "mesh_load.h"

mesh_t *g_scene_table[SCENE_TABLE_SLOT_COUNT];

static IDirect3DDevice8 *g_scene_table_dev = 0;

static int win32_load_fn(const char *path, int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    if (g_scene_table[slot]) {
        mesh_free(g_scene_table[slot]);
        g_scene_table[slot] = 0;
    }
    mesh_t *m = mesh_load(path, -1);
    if (!m) {
        fprintf(stderr, "scene_table: mesh_load failed for %s\n", path);
        return 0;
    }
    if (m->error[0]) {
        fprintf(stderr, "scene_table: %s: %s\n", path, m->error);
        mesh_free(m);
        return 0;
    }
    if (dev) {
        long hr = mesh_load_finalize_win32(m, (struct IDirect3DDevice8 *)dev);
        if (hr) fprintf(stderr,
            "scene_table: finalize_win32 failed (hr=0x%08lx) for %s\n",
            (unsigned long)hr, path);
    }
    g_scene_table[slot] = m;
    return 1;
}

static void scene_table_body(void)
{
    /* Engine LAB_00452c0a → FUN_004748f8(1). Same hard-coded-literal-1
     * shape as the wall/floor/jutan siblings. */
    scene_table_load_with(win32_load_fn, g_scene_table_dev, 1);
}

void scene_table_init(struct IDirect3DDevice8 *dev)
{
    g_scene_table_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C0A, scene_table_body);
}

void scene_table_reset(void)
{
    for (int i = 0; i < SCENE_TABLE_SLOT_COUNT; i++) {
        if (g_scene_table[i]) { mesh_free(g_scene_table[i]); g_scene_table[i] = 0; }
    }
    g_scene_table_selector = 0;
}

#else /* !_WIN32 */

void scene_table_reset(void)
{
    g_scene_table_selector = 0;
}

#endif /* _WIN32 */
