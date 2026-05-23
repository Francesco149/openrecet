/*
 * scene_sc1.c — see scene_sc1.h.
 *
 * Engine source: FUN_0046bf38 @ 0x46bf38 (230 bytes).
 *   Fixed sprite #1 dest: DAT_0735dc30, name "bmp/ivent/ive_window.tga"
 *   Fixed sprite #2 dest: DAT_073a3dd8, name "bmp/ivent/chrname.tga"
 *   Variable mesh: count DAT_073a3dfc, names DAT_0734fff0 (0x100),
 *                  dest DAT_0735dd88 (0x28)
 *   Variable sprite: count DAT_073a3df0, names DAT_07350df0 (0x100),
 *                    dest DAT_073571f0 (0x10), dims 0x400×0x200
 *   Fixed 100-slot sprite: dest DAT_0734f9b0 (0x10), names DAT_07357830
 *                          (0x100), size pairs DAT_073a3ab8 (8 bytes,
 *                          ranges to DAT_073a3dd8 — i.e. 100 pairs).
 */

#include "scene_sc1.h"

#include <stdio.h>
#include <string.h>

#include "worker_load.h"

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_sc1_mesh_count   = 0;
char    g_scene_sc1_mesh_names  [SCENE_SC1_VAR_MESH_CAP][SCENE_SC1_NAME_MAX];

int32_t g_scene_sc1_sprite_count = 0;
char    g_scene_sc1_sprite_names[SCENE_SC1_VAR_SPRITE_CAP][SCENE_SC1_NAME_MAX];

char     g_scene_sc1_item_names[SCENE_SC1_ITEM_CAP][SCENE_SC1_NAME_MAX];
uint32_t g_scene_sc1_item_sizes[SCENE_SC1_ITEM_CAP][2];

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_sc1_load_with(scene_sc1_sprite_load_fn load_fn,
                        scene_sc1_mesh_load_fn   mesh_fn,
                        void *userdata)
{
    int loads = 0;

    /* Bucket (1): 2 fixed sprites. Always run. */
    if (load_fn) {
        load_fn("bmp/ivent/ive_window.tga",
                SCENE_SC1_KIND_IVE_WINDOW, 0, 0x200, 0x200, userdata);
    }
    loads++;
    if (load_fn) {
        load_fn("bmp/ivent/chrname.tga",
                SCENE_SC1_KIND_CHRNAME, 0, 0x200, 0x200, userdata);
    }
    loads++;

    /* Bucket (2): variable mesh loop. Engine `if (DAT_073a3dfc != 0)`. */
    int32_t mc = g_scene_sc1_mesh_count;
    if (mc < 0) mc = 0;
    if (mc > SCENE_SC1_VAR_MESH_CAP) mc = SCENE_SC1_VAR_MESH_CAP;
    for (int32_t i = 0; i < mc; i++) {
        const char *nm = g_scene_sc1_mesh_names[i];
        if (mesh_fn) mesh_fn(nm, i, userdata);
        loads++;
    }

    /* Bucket (3): variable sprite loop. */
    int32_t sc = g_scene_sc1_sprite_count;
    if (sc < 0) sc = 0;
    if (sc > SCENE_SC1_VAR_SPRITE_CAP) sc = SCENE_SC1_VAR_SPRITE_CAP;
    for (int32_t i = 0; i < sc; i++) {
        const char *nm = g_scene_sc1_sprite_names[i];
        if (load_fn) load_fn(nm, SCENE_SC1_KIND_VAR_SPRITE, (int)i,
                             0x400, 0x200, userdata);
        loads++;
    }

    /* Bucket (4): fixed 100-slot, name-gated. */
    for (int i = 0; i < SCENE_SC1_ITEM_CAP; i++) {
        if (g_scene_sc1_item_names[i][0] == '\0') continue;
        if (load_fn) load_fn(g_scene_sc1_item_names[i],
                             SCENE_SC1_KIND_ITEM, i,
                             g_scene_sc1_item_sizes[i][0],
                             g_scene_sc1_item_sizes[i][1],
                             userdata);
        loads++;
    }

    return loads;
}

/* ─── Win32 worker_load wiring ────────────────────────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

#include "mesh_load.h"

sprite_t g_scene_sc1_ive_window;
sprite_t g_scene_sc1_chrname;
sprite_t g_scene_sc1_sprites[SCENE_SC1_VAR_SPRITE_CAP];
sprite_t g_scene_sc1_items  [SCENE_SC1_ITEM_CAP];
mesh_t  *g_scene_sc1_meshes [SCENE_SC1_VAR_MESH_CAP];

static IDirect3DDevice8 *g_scene_sc1_dev = 0;

static int win32_sprite_load_fn(const char *path, int kind, int slot,
                                uint32_t w, uint32_t h, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    sprite_t *dst = 0;
    switch (kind) {
        case SCENE_SC1_KIND_IVE_WINDOW: dst = &g_scene_sc1_ive_window; break;
        case SCENE_SC1_KIND_CHRNAME:    dst = &g_scene_sc1_chrname;    break;
        case SCENE_SC1_KIND_VAR_SPRITE:
            if (slot >= 0 && slot < SCENE_SC1_VAR_SPRITE_CAP)
                dst = &g_scene_sc1_sprites[slot];
            break;
        case SCENE_SC1_KIND_ITEM:
            if (slot >= 0 && slot < SCENE_SC1_ITEM_CAP)
                dst = &g_scene_sc1_items[slot];
            break;
    }
    if (!dst) return 0;
    return sprite_load(dev, path, w, h, dst);
}

static int win32_mesh_load_fn(const char *path, int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    if (slot < 0 || slot >= SCENE_SC1_VAR_MESH_CAP) return 0;
    if (g_scene_sc1_meshes[slot]) {
        mesh_free(g_scene_sc1_meshes[slot]);
        g_scene_sc1_meshes[slot] = 0;
    }
    mesh_t *m = mesh_load(path, -1);
    if (!m) {
        fprintf(stderr, "scene_sc1: mesh_load failed for %s\n", path);
        return 0;
    }
    if (m->error[0]) {
        fprintf(stderr, "scene_sc1: %s: %s\n", path, m->error);
        mesh_free(m);
        return 0;
    }
    if (dev) {
        long hr = mesh_load_finalize_win32(m, (struct IDirect3DDevice8 *)dev);
        if (hr) fprintf(stderr,
            "scene_sc1: finalize_win32 failed (hr=0x%08lx) for %s\n",
            (unsigned long)hr, path);
    }
    g_scene_sc1_meshes[slot] = m;
    return 1;
}

static void scene_sc1_body(void)
{
    scene_sc1_load_with(win32_sprite_load_fn, win32_mesh_load_fn, g_scene_sc1_dev);
}

void scene_sc1_init(struct IDirect3DDevice8 *dev)
{
    g_scene_sc1_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, scene_sc1_body);
}

void scene_sc1_reset(void)
{
    memset(&g_scene_sc1_ive_window, 0, sizeof g_scene_sc1_ive_window);
    memset(&g_scene_sc1_chrname,    0, sizeof g_scene_sc1_chrname);
    memset(g_scene_sc1_sprites, 0, sizeof g_scene_sc1_sprites);
    memset(g_scene_sc1_items,   0, sizeof g_scene_sc1_items);
    for (int i = 0; i < SCENE_SC1_VAR_MESH_CAP; i++) {
        if (g_scene_sc1_meshes[i]) {
            mesh_free(g_scene_sc1_meshes[i]);
            g_scene_sc1_meshes[i] = 0;
        }
    }
    g_scene_sc1_mesh_count   = 0;
    g_scene_sc1_sprite_count = 0;
    memset(g_scene_sc1_mesh_names,   0, sizeof g_scene_sc1_mesh_names);
    memset(g_scene_sc1_sprite_names, 0, sizeof g_scene_sc1_sprite_names);
    memset(g_scene_sc1_item_names,   0, sizeof g_scene_sc1_item_names);
    memset(g_scene_sc1_item_sizes,   0, sizeof g_scene_sc1_item_sizes);
}

#else /* !_WIN32 */

void scene_sc1_reset(void)
{
    g_scene_sc1_mesh_count   = 0;
    g_scene_sc1_sprite_count = 0;
    memset(g_scene_sc1_mesh_names,   0, sizeof g_scene_sc1_mesh_names);
    memset(g_scene_sc1_sprite_names, 0, sizeof g_scene_sc1_sprite_names);
    memset(g_scene_sc1_item_names,   0, sizeof g_scene_sc1_item_names);
    memset(g_scene_sc1_item_sizes,   0, sizeof g_scene_sc1_item_sizes);
}

#endif /* _WIN32 */
