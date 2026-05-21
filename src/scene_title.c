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

#include <string.h>

const scene_title_asset_t scene_title_assets[SCENE_TITLE_TEX_COUNT] = {
    [SCENE_TITLE_TEX_BG2]     = { "bmp/title_bg2.bmp",     1024, 1024 },
    [SCENE_TITLE_TEX_01]      = { "bmp/title01.tga",        512,  256 },
    [SCENE_TITLE_TEX_FUKI]    = { "bmp/title_fuki.tga",     512, 1024 },
    [SCENE_TITLE_TEX_WAKU]    = { "bmp/title_waku.tga",    1024,  512 },
    [SCENE_TITLE_TEX_PAUSE]   = { "bmp/pause.tga",         1024,  512 },
    [SCENE_TITLE_TEX_RESULT]  = { "bmp/result_bord01.tga",  512,  256 },
    [SCENE_TITLE_TEX_DUNGEON] = { "bmp/dungeonbord.tga",   1024,  512 },
};

/* ─── menu init (FUN_0049a43d) ───────────────────────────────────────── */

void scene_title_menu_init(const scene_title_save_t *save,
                           scene_title_menu_t *out)
{
    memset(out, 0, sizeof *out);

    /* The engine's literal "uVar1" bitmask. bit 0 = any cleared adv;
     * bit 1 = any bank has adv8 cleared. uVar1 == 3 ↔ both set. */
    const int uVar1 = (save->has_any_adv_cleared ? 1 : 0)
                    | (save->has_any_adv8_cleared ? 2 : 0);

    int count = 0;
    int *m    = out->items;

    /* Slot 0: New Game vs. (Continue + New). Engine flips between
     * the two flavours via item-code 0 vs. 5+4. */
    if ((uVar1 & 1) == 0) {
        m[count++] = SCENE_TITLE_MENU_NEW_GAME;          /* 0 */
    } else {
        m[count++] = SCENE_TITLE_MENU_CONT_HAS_SAVE;     /* 5 */
        m[count++] = SCENE_TITLE_MENU_NEW_HAS_SAVE;      /* 4 */
    }

    /* Survival unlock — engine literally checks `uVar1 == 3`, not
     * a bit test. Both flags must be set (Adventure 2 cleared on a
     * bank that also has Adventure 8 cleared). */
    if (uVar1 == 3) {
        m[count++] = SCENE_TITLE_MENU_SURVIVAL;          /* 6 */
    }

    /* Quick-Continue: scan-for-any-populated-bank result. Adds
     * item 1 once and sets the default cursor to it. */
    if (save->has_any_score) {
        out->default_cursor = count;
        m[count++] = SCENE_TITLE_MENU_CONTINUE_ANY;      /* 1 */
    }

    /* Ranking is always present. */
    m[count++] = SCENE_TITLE_MENU_RANKING;               /* 7 */

    /* Hidden character (DAT_056e5788) — note the engine *also*
     * unlocks this when (uVar1 & 1) is set, not only via the
     * dedicated flag. Engine quirk; reproduced. */
    if (save->hidden_char_unlocked || (uVar1 & 1)) {
        m[count++] = SCENE_TITLE_MENU_HIDDEN_CHAR;       /* 8 */
    }

    m[count++] = SCENE_TITLE_MENU_OPTIONS;               /* 2 */
    m[count++] = SCENE_TITLE_MENU_EXIT;                  /* 3 */

    out->count = count;

    /* Y-stride / Y-origin table from the count-based switch at the
     * tail of FUN_0049a43d:
     *
     *   count == 8 → stride 27, origin -36
     *   count == 7 → stride 30, origin -36
     *   count == 6 → stride 33, origin -30
     *   else       → stride 33, origin -16
     */
    if (count == 8) {
        out->y_stride = 27.0f;
        out->y_origin = -36.0f;
    } else if (count == 7) {
        out->y_stride = 30.0f;
        out->y_origin = -36.0f;
    } else if (count == 6) {
        out->y_stride = 33.0f;
        out->y_origin = -30.0f;
    } else {
        out->y_stride = 33.0f;
        out->y_origin = -16.0f;
    }
}

void scene_title_menu_init_fresh(scene_title_menu_t *out)
{
    const scene_title_save_t empty = {0};
    scene_title_menu_init(&empty, out);
}

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
