/*
 * scene_buy.c — see scene_buy.h.
 *
 * Engine source: FUN_0047329b @ 0x47329b (151 bytes).
 */

#include "scene_buy.h"

#include <stdio.h>
#include <string.h>

#include "worker_load.h"

/* ─── module state ────────────────────────────────────────────────────── */

int32_t g_scene_buy_page0_valid = 0;
int32_t g_scene_buy_page0_count = 0;
char    g_scene_buy_page0_names[SCENE_BUY_SLOT_COUNT][256];

/* Engine s_bmp__s_005c864c (and the B13-side s_bmp__s_005c8680 — same
 * literal at a different .rdata address; B13 will reuse this getter). */
static const char *const g_scene_buy_path_fmt = "bmp/%s";

const char *scene_buy_format_string(void)
{
    return g_scene_buy_path_fmt;
}

/* ─── pure-C body ─────────────────────────────────────────────────────── */

int scene_buy_ae8_load_with(scene_buy_load_fn load_fn, void *userdata)
{
    int loads = 0;

    /* Phase 1 — dynamic per-item icon loop (page 0).
     *
     * Engine: `if ((DAT_06a63bdc != 0) && (DAT_06a63bd4 != 0))`. Both
     * gates must be non-zero. We then iterate `count` times, reading
     * each name from the per-page name buffer.
     *
     * Clamp: the engine has no cap, but the destination sprite array's
     * per-page stride is 0xa0 = 10 slots. Counts above 10 overflow
     * into adjacent pages' sprite memory engine-side; we clamp at
     * SCENE_BUY_SLOT_COUNT so the name-buffer read stays in bounds.
     * Tests can observe the full clamped sequence. */
    if (g_scene_buy_page0_valid != 0 && g_scene_buy_page0_count != 0) {
        int n = g_scene_buy_page0_count;
        if (n > SCENE_BUY_SLOT_COUNT) n = SCENE_BUY_SLOT_COUNT;
        for (int i = 0; i < n; i++) {
            char path[256];
            /* Engine: FUN_005038ff = unbounded sprintf-with-NUL. The
             * snprintf form is safer; longest formatted path is
             * "bmp/" (4) + 255-byte name + NUL = 260, hits the buffer
             * boundary but truncation matches engine semantics for
             * over-long names (engine would write past the local
             * 256-byte scratch). */
            snprintf(path, sizeof(path), g_scene_buy_path_fmt,
                     g_scene_buy_page0_names[i]);
            if (load_fn) load_fn(path, i, 0x200, 0x200, userdata);
            loads++;
        }
    }

    /* Phase 2 — fixed chrname.tga (always fires).
     * Engine: FUN_0047193c(0x10, &DAT_073cc8d0,
     *                       s_bmp_ivent_chrname_tga_005c8654,
     *                       0x200, 0x200). */
    if (load_fn) load_fn("bmp/ivent/chrname.tga",
                          SCENE_BUY_AE8_SLOT_CHRNAME,
                          0x200, 0x200, userdata);
    loads++;

    /* Phase 3 — fixed shopmode.tga (always fires).
     * Engine: FUN_0047193c(0x10, &DAT_073a9580,
     *                       s_bmp_shopmode_tga_005c866c,
     *                       0x400, 0x200). */
    if (load_fn) load_fn("bmp/shopmode.tga",
                          SCENE_BUY_AE8_SLOT_SHOPMODE,
                          0x400, 0x200, userdata);
    loads++;

    return loads;
}

/* ─── reset ──────────────────────────────────────────────────────────── */

static void scene_buy_state_clear(void)
{
    g_scene_buy_page0_valid = 0;
    g_scene_buy_page0_count = 0;
    memset(g_scene_buy_page0_names, 0, sizeof(g_scene_buy_page0_names));
}

/* ─── Win32 worker_load wiring + sprite storage ─────────────────────── */

#ifdef _WIN32

#include <d3d8.h>

sprite_t g_scene_buy_page0_sprites[SCENE_BUY_SLOT_COUNT];
sprite_t g_scene_buy_chrname;
sprite_t g_scene_buy_shopmode;

static IDirect3DDevice8 *g_scene_buy_dev = 0;

static sprite_t *scene_buy_slot_dest(int slot)
{
    if (slot >= 0 && slot < SCENE_BUY_SLOT_COUNT) {
        return &g_scene_buy_page0_sprites[slot];
    }
    if (slot == SCENE_BUY_AE8_SLOT_CHRNAME)  return &g_scene_buy_chrname;
    if (slot == SCENE_BUY_AE8_SLOT_SHOPMODE) return &g_scene_buy_shopmode;
    return 0;
}

static int win32_load_fn(const char *path, int slot, int w, int h,
                          void *userdata)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)userdata;
    sprite_t *dst = scene_buy_slot_dest(slot);
    if (!dst) return 0;
    return sprite_load(dev, path, (uint32_t)w, (uint32_t)h, dst);
}

static void scene_buy_ae8_body(void)
{
    scene_buy_ae8_load_with(win32_load_fn, g_scene_buy_dev);
}

void scene_buy_init(struct IDirect3DDevice8 *dev)
{
    g_scene_buy_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AE8, scene_buy_ae8_body);
}

void scene_buy_reset(void)
{
    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        g_scene_buy_page0_sprites[i].tex    = 0;
        g_scene_buy_page0_sprites[i].width  = 0;
        g_scene_buy_page0_sprites[i].height = 0;
    }
    g_scene_buy_chrname.tex     = 0;
    g_scene_buy_chrname.width   = 0;
    g_scene_buy_chrname.height  = 0;
    g_scene_buy_shopmode.tex    = 0;
    g_scene_buy_shopmode.width  = 0;
    g_scene_buy_shopmode.height = 0;
    scene_buy_state_clear();
}

#else  /* !_WIN32 — Linux test build */

void scene_buy_reset(void)
{
    scene_buy_state_clear();
}

#endif /* _WIN32 */
