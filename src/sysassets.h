/*
 * sysassets.h — system overlay/UI texture loader.
 *
 * Engine source: FUN_00472f5d ("init systextures") at 0x472f5d. Loads
 * the ~30 textures that the post-title UI overlays consume: the
 * "Now Loading…" panel, the save window, item/data inventory windows,
 * character portraits, HP/MP gauges, status effects, etc. Called once
 * at title bootstrap (FUN_0047b29e L233) and again after every D3D8
 * lost-device recovery (FUN_004547ab L231) — we only wire the first
 * site today, since openrecet doesn't trigger device loss yet.
 *
 * Asset filenames extracted via
 *   tools/analyze/pe.py str <VA>
 * at the VAs listed in 0x472f5d.c. Each FUN_0047193c call passes
 * (slot_tag=1, &dst_sprite, filename, expected_w, expected_h). Our
 * sprite_load doesn't yet resample to (expected_w, expected_h) so the
 * sizes are stored on the sprite for future use but the texture loads
 * at native resolution.
 *
 * Sub-loops faithfully reproduced:
 *
 *   1. 20-dword zeroing loop at &DAT_068dccc4 (stride 40 bytes, 20
 *      iters). Whatever this clears is BSS-zero in a clean process and
 *      we haven't wired the consumer yet — the engine only needs the
 *      reset on the device-loss reload path, so for the boot call our
 *      first-touch-is-zero semantics suffice. Tracked as deferred work.
 *
 *   2. Character portrait loop — 3 variants from `bmp/chr/chr%02d.bmp`,
 *      width/height read from a size table at &DAT_0438cec8 with stride
 *      0x5058. That table is BSS-zero at boot (populated later by the
 *      character selection screen), so we load each portrait at
 *      (expected_w=0, expected_h=0) — same as the engine does on a
 *      fresh boot.
 *
 *   3. Item icon loop — for each item category C present in g_item,
 *      load `bmp/item/item%02d.bmp` once with height =
 *      ceil(count_in_category / 8) * 32 (min 64). The icon page is
 *      always 256 wide; 8 icons per row at 32×32 each. Item records
 *      are iterated in g_item.records[] order; categories are loaded
 *      in ascending order (each only once, gated by max_cat tracker).
 *
 *   4. DAT_0076b948-gated array load — per-layer sprite loader for the
 *      2D-overlay dispatcher; populated by the FUN_00474f4f parser
 *      (chip O.10, `src/scene1_overlay_table.{c,h}`).  Engine all.c
 *      L71673-71683: for i in 0..DAT_0076b948, sprite_load(1,
 *      DAT_073cc780 + i*0x10, DAT_0072a820 + i*0x100, 0x100, 0x100).
 *      In our port the filename table is
 *      `g_scene1_overlay_layer_filenames[i]` and the texture slot is
 *      `g_scene1_overlay_layers[i]` (the sprite_t backing
 *      `g_scene1_overlay_layer_textures[i]`).  Loop dormant until
 *      scene1_overlay_table_load_all writes the table (main.c boot
 *      sequencing).
 */

#ifndef OPENRECET_SYSASSETS_H
#define OPENRECET_SYSASSETS_H

#include <stdint.h>

#include "tables_item.h"

/* Number of `bmp/chr/chr%02d.bmp` variants the engine loads (see the
 * chara loop in 472f5d.c at lines 39..49: end pointer DAT_073a9b48
 * minus start DAT_073a9b18 = 0x30 bytes / 0x10 stride = 3). */
#define SYSASSETS_CHARA_VARIANTS  3

/* Item icon page slot count. Mirrors the engine's per-category array
 * at DAT_073d8778 + cat * 0x10. Bounded by ITEM_CATEGORY_COUNT. */
#define SYSASSETS_ITEM_CATEGORIES ITEM_CATEGORY_COUNT

#ifdef _WIN32
/* sprite_t is a thin wrapper around IDirect3DTexture8, so we keep the
 * struct + global gated to the Win32 build. The pure helper below
 * uses neither and is available unconditionally for host tests. */
#include "scene1_overlay.h"
#include "sprite.h"

typedef struct {
    /* The seven non-loop textures from FUN_00472f5d L27..L33.
     * Names mirror the original .data global names. */
    sprite_t system_bmp;          /* &DAT_073aa188 — bmp/system.bmp        */
    sprite_t fps2_tga;            /* &DAT_073d9fe0 — bmp/fps2.tga          */
    sprite_t nowloading_tga;      /* &DAT_073cc770 — bmp/nowloading.tga    */
    sprite_t savewindow_tga;      /* &DAT_073d8dc0 — bmp/savewindow.tga    */
    sprite_t window_tga;          /* &DAT_073da2e0 — bmp/window.tga        */
    sprite_t item_win_tga;        /* &DAT_073d8748 — bmp/item_win.tga      */
    sprite_t data_win_tga;        /* &DAT_073d8678 — bmp/data_win.tga      */

    /* Character portrait variants — bmp/chr/chr00..02.bmp. */
    sprite_t chara_variants[SYSASSETS_CHARA_VARIANTS];

    /* The 13 single-load textures after the chara loop (L50..L61). */
    sprite_t pc_gage_tga;         /* &DAT_073cb8f0 — bmp/pc_gage.tga       */
    sprite_t hpmp_base_tga;       /* &DAT_073cc920 — bmp/hpmp_base.tga     */
    sprite_t hpmp_exp_tga;        /* &DAT_073cc900 — bmp/hpmp_exp.tga      */
    sprite_t ene_hp2_tga;         /* &DAT_073cc910 — bmp/ene_hp2.tga       */
    sprite_t magicjem_tga;        /* &DAT_073cc930 — bmp/magicjem.tga      */
    sprite_t effect_bmp;          /* &DAT_073cc8c0 — bmp/effect.bmp        */
    sprite_t kumonosu_tga;        /* &DAT_073d8620 — bmp/kumonosu.tga      */
    sprite_t katter_tga;          /* &DAT_073cc8e0 — bmp/katter.tga        */
    sprite_t effect_mahi_bmp;     /* &DAT_073d8ed0 — bmp/effect_mahi.bmp   */
    sprite_t snow_bmp;            /* &DAT_073aa178 — bmp/snow.bmp          */
    sprite_t effect_shot_bmp;     /* &DAT_073cc940 — bmp/effect_shot.bmp   */
    sprite_t shade_bmp;           /* &DAT_073cc8f0 — bmp/shade.bmp         */

    /* Item icon pages, sparse — populated only for categories that
     * have at least one valid item record (i.e. valid > 0). */
    sprite_t item_icons[SYSASSETS_ITEM_CATEGORIES];

    /* 2D-overlay dispatcher per-layer textures (engine DAT_073cc780
     * stride 0x10).  Slot count = `g_scene1_overlay_layer_count`
     * (populated by scene1_overlay_table_load_all before this loader
     * runs).  After load, sysassets_load_all writes each sprite's
     * IDirect3DTexture8 pointer into g_scene1_overlay_layer_textures
     * so the dispatcher's sticky SetTexture has the right binding. */
    sprite_t overlay_layers[SCENE1_OVERLAY_LAYER_COUNT_MAX];
} sysassets_t;

extern sysassets_t g_sysassets;
#endif /* _WIN32 */

/* Pure helper — compute per-category icon page height the same way
 * FUN_00472f5d does at L73..L97. For each category C present in
 * `items->records[]` with at least one valid record (valid > 0):
 *
 *   out_size_per_category[C] = max(64, ((count_C + 7) / 8) * 32)
 *
 * For categories with no valid records, the slot is set to 0.
 *
 * Returns the number of distinct categories that received a non-zero
 * size (i.e. the number of icon pages that would be loaded).
 *
 * `out_size_per_category` must point to an array of at least
 * SYSASSETS_ITEM_CATEGORIES ints. */
int sysassets_compute_icon_sizes(const item_state_t *items,
                                 int *out_size_per_category);

#ifdef _WIN32
struct IDirect3DDevice8;

/* Load every system texture into g_sysassets. Returns the number of
 * sprites loaded successfully (max = the static slots + the active
 * chara variants + the active item categories — typically 7 + 3 + ~33
 * for vendor data, i.e. ~43). Idempotent: existing sprites are
 * released first.
 *
 * Currently only wired in main.c at boot, after scene_title_load_assets.
 * The device-reset reload path (FUN_004547ab L231) is deferred until
 * device-loss handling lands. */
int sysassets_load_all(struct IDirect3DDevice8 *dev);

/* Release every D3D texture held by g_sysassets. Safe to call multiple
 * times; safe to call without a prior sysassets_load_all (no-op on
 * zero-init sprites). */
void sysassets_unload_all(void);
#endif /* _WIN32 */

#endif /* OPENRECET_SYSASSETS_H */
