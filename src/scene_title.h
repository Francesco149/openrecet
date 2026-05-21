/*
 * scene_title.h — title-screen scene module.
 *
 * Today: just the texture loader (FUN_004733d5). Holds 7 sprite_t
 * slots — 4 title-specific BG/menu textures + 3 cross-scene ones
 * (pause/result/dungeon) that the engine batches into the same
 * load call.
 *
 * Later commits add: menu items init (FUN_0049a324 + FUN_0049a43d)
 * and the bare-path render (FUN_0049c644).
 *
 * Two-layer split, same as render_quad.{c,h}: a pure-C constant
 * asset table + accessors at the top; D3D-touching loader under
 * `#ifdef _WIN32` at the bottom.
 */
#ifndef OPENRECET_SCENE_TITLE_H
#define OPENRECET_SCENE_TITLE_H

#include <stdint.h>

/* Slot indices for the texture array (same order as FUN_004733d5). */
enum {
    SCENE_TITLE_TEX_BG2       = 0,  /* bmp/title_bg2.bmp     1024x1024 */
    SCENE_TITLE_TEX_01        = 1,  /* bmp/title01.tga        512x256  */
    SCENE_TITLE_TEX_FUKI      = 2,  /* bmp/title_fuki.tga     512x1024 */
    SCENE_TITLE_TEX_WAKU      = 3,  /* bmp/title_waku.tga    1024x512  */
    SCENE_TITLE_TEX_PAUSE     = 4,  /* bmp/pause.tga         1024x512  */
    SCENE_TITLE_TEX_RESULT    = 5,  /* bmp/result_bord01.tga  512x256  */
    SCENE_TITLE_TEX_DUNGEON   = 6,  /* bmp/dungeonbord.tga   1024x512  */
    SCENE_TITLE_TEX_COUNT     = 7,
};

/* One entry of the asset table at PE 0x005c8688..0x005c86fc, paired
 * with the (expected_w, expected_h) arguments FUN_0047193c was
 * called with. Used by the loader; exposed for tests. */
typedef struct {
    const char *path;
    uint32_t    expected_w;
    uint32_t    expected_h;
} scene_title_asset_t;

extern const scene_title_asset_t scene_title_assets[SCENE_TITLE_TEX_COUNT];

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "sprite.h"

/* Load the 7 title-scene textures via sprite_load (FUN_0047193c).
 * Returns the number of slots that loaded successfully (== 7 on
 * full success). On partial failure the unloaded slots are left
 * zeroed; render code must NULL-guard before drawing. */
int scene_title_load_assets(IDirect3DDevice8 *dev);

/* Read access to one loaded texture by slot index. Returns a
 * pointer to the static slot — never NULL — but slot.tex may be
 * NULL if the load failed or it hasn't run yet. */
const sprite_t *scene_title_get(int slot);

/* Free all 7 textures + zero the slots. Idempotent. */
void scene_title_unload_assets(void);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_TITLE_H */
