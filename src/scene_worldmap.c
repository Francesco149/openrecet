/*
 * scene_worldmap.c — see scene_worldmap.h.
 *
 * Engine source: FUN_004735ad @ 0x4735ad (98 bytes). Four fixed
 * sprite_load calls — no loop, no selector. Strings extracted via
 *   tools/analyze/pe.py str 0x005c87f4 0x005c880c 0x005c8824 0x005c883c
 */

#include "scene_worldmap.h"

#include "worker_load.h"

/* ─── pre-baked asset table ──────────────────────────────────────────── */

const scene_worldmap_asset_t scene_worldmap_assets[SCENE_WORLDMAP_COUNT] = {
    [SCENE_WORLDMAP_TEX_NOMAL]    = { "bmp/worldmap_nomal.bmp",  0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_YUGATA]   = { "bmp/worldmap_yugata.bmp", 0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_NIGHT]    = { "bmp/worldmap_night.bmp",  0x400, 0x200 },
    [SCENE_WORLDMAP_TEX_MAPPOINT] = { "bmp/mappoint.tga",        0x100, 0x400 },
};

const char *scene_worldmap_filename(int slot)
{
    if (slot < 0 || slot >= SCENE_WORLDMAP_COUNT) return 0;
    return scene_worldmap_assets[slot].path;
}

int scene_worldmap_dims(int slot, uint32_t *w, uint32_t *h)
{
    if (slot < 0 || slot >= SCENE_WORLDMAP_COUNT) {
        if (w) *w = 0;
        if (h) *h = 0;
        return 0;
    }
    if (w) *w = scene_worldmap_assets[slot].expected_w;
    if (h) *h = scene_worldmap_assets[slot].expected_h;
    return 1;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_worldmap_load_with(scene_worldmap_load_fn load_fn,
                             void *userdata)
{
    /* Engine FUN_004735ad: 4 unconditional sprite_load calls in fixed
     * order. The first arg (kind = 10) is the engine's per-scene
     * texture-group flag, which openrecet's sprite_load drops. */
    int loads = 0;
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        const scene_worldmap_asset_t *a = &scene_worldmap_assets[i];
        if (load_fn) {
            load_fn(a->path, a->expected_w, a->expected_h, i, userdata);
        }
        loads++;
    }
    return loads;
}

/* ─── Win32 worker_load wiring + sprite storage ──────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

#include "sprite.h"

sprite_t g_scene_worldmap[SCENE_WORLDMAP_COUNT];

static IDirect3DDevice8 *g_scene_worldmap_dev = 0;

/* Win32 load_fn: drives sprite_load against the per-slot sprite_t. */
static int win32_load_fn(const char *path,
                         uint32_t w, uint32_t h,
                         int slot, void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    /* Engine: FUN_0047193c(10, dst, name, w, h). The kind=10 is the
     * scene-1 texture-group flag; sprite_load drops it (same as the
     * rest of the openrecet call sites). */
    return sprite_load(dev, path, w, h, &g_scene_worldmap[slot]);
}

static void scene_worldmap_body(void)
{
    /* Engine LAB_00452c96's inner-body calls (objdump @ 0x452c96..):
     *     call 0x49de20    ; FUN_0049de20  — world-map state machine
     *     call 0x4735ad    ; FUN_004735ad  — world-map BMP loader  (THIS)
     *
     * The state-machine half is deferred — see header banner. This
     * body runs only the BMP loader; when the state machine ports,
     * the body becomes a sequential 2-call wrapper. */
    scene_worldmap_load_with(win32_load_fn, g_scene_worldmap_dev);
}

void scene_worldmap_init(struct IDirect3DDevice8 *dev)
{
    g_scene_worldmap_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C96, scene_worldmap_body);
}

void scene_worldmap_reset(void)
{
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        /* Same lazy-reset shape as scene_floor/jutan — leaves the
         * IDirect3D resource leaked, which is fine because tests on
         * Win32 don't run sprite_load, and the engine's lost-device
         * handling (not yet ported) will own the real teardown. */
        g_scene_worldmap[i].tex    = 0;
        g_scene_worldmap[i].width  = 0;
        g_scene_worldmap[i].height = 0;
    }
}

#else  /* !_WIN32 — Linux test build */

void scene_worldmap_reset(void)
{
    /* Nothing to reset on the test build — no sprite_t storage. */
}

#endif /* _WIN32 */
