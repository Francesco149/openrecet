/*
 * scene_title.c — title-screen scene module.
 *
 * Engine source: FUN_004733d5 (texture loader).
 *
 * Asset paths extracted from the unpacked binary's .rdata at
 *   VA 0x005c8688..0x005c86fc
 * via `tools/analyze/pe.py str <VA>`; the (w, h) pairs are the
 * literal arguments FUN_0047193c was called with — they match
 * each file's native resolution (as confirmed by spot-decoding
 * with `sprite_load` against `vendor/original`).
 *
 * The engine's first argument to FUN_0047193c (a "slot/category"
 * tag — 2 for these 7 entries) is the unload-grouping key. We do
 * not need it: our `scene_title_unload_assets` simply releases
 * every slot owned by this module.
 */

#include "scene_title.h"

const scene_title_asset_t scene_title_assets[SCENE_TITLE_TEX_COUNT] = {
    [SCENE_TITLE_TEX_BG2]     = { "bmp/title_bg2.bmp",     1024, 1024 },
    [SCENE_TITLE_TEX_01]      = { "bmp/title01.tga",        512,  256 },
    [SCENE_TITLE_TEX_FUKI]    = { "bmp/title_fuki.tga",     512, 1024 },
    [SCENE_TITLE_TEX_WAKU]    = { "bmp/title_waku.tga",    1024,  512 },
    [SCENE_TITLE_TEX_PAUSE]   = { "bmp/pause.tga",         1024,  512 },
    [SCENE_TITLE_TEX_RESULT]  = { "bmp/result_bord01.tga",  512,  256 },
    [SCENE_TITLE_TEX_DUNGEON] = { "bmp/dungeonbord.tga",   1024,  512 },
};

#ifdef _WIN32

static sprite_t g_tex[SCENE_TITLE_TEX_COUNT];

int scene_title_load_assets(IDirect3DDevice8 *dev)
{
    int loaded = 0;
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        const scene_title_asset_t *a = &scene_title_assets[i];
        if (sprite_load(dev, a->path, a->expected_w, a->expected_h,
                        &g_tex[i])) {
            loaded++;
        }
    }
    return loaded;
}

const sprite_t *scene_title_get(int slot)
{
    if (slot < 0 || slot >= SCENE_TITLE_TEX_COUNT) {
        static const sprite_t empty = {0};
        return &empty;
    }
    return &g_tex[slot];
}

void scene_title_unload_assets(void)
{
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        sprite_destroy(&g_tex[i]);
    }
}

#endif /* _WIN32 */
