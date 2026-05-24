/*
 * sysassets.c — system overlay/UI texture loader.
 *
 * Engine source: FUN_00472f5d. See sysassets.h for the full RE notes
 * and quirks.
 */

#include "sysassets.h"

#include <stdio.h>
#include <string.h>

int sysassets_compute_icon_sizes(const item_state_t *items,
                                 int *out_size_per_category)
{
    int counts[SYSASSETS_ITEM_CATEGORIES] = {0};

    /* L62..L72 of FUN_00472f5d — first pass: count valid records per
     * category. The engine's `piVar3[-0xe] > 0` test reads the `valid`
     * field at offset 0 (piVar3 points at the `category` field at
     * +0x38; piVar3[-0xe] = piVar3 - 56 = offset 0). */
    for (int i = 0; i < ITEM_MAX_RECORDS; i++) {
        const item_record_t *r = &items->records[i];
        if (r->valid <= 0) continue;
        int cat = r->category;
        if (cat < 0 || cat >= SYSASSETS_ITEM_CATEGORIES) continue;
        counts[cat]++;
    }

    /* L85..L99 — second pass: load each category's icon page exactly
     * once with size based on its count. The engine uses a single
     * register (iVar5) as both the "max category seen so far" tracker
     * and the temporary that holds the computed page height; we use
     * two variables for clarity (semantically identical). Since
     * records are sorted by item_id within each category, scanning
     * sequentially and gating on "cat > max_cat_seen" hits each
     * category exactly once on its first record. */
    int max_cat = -1;
    int distinct = 0;

    for (int i = 0; i < SYSASSETS_ITEM_CATEGORIES; i++) {
        out_size_per_category[i] = 0;
    }

    for (int i = 0; i < ITEM_MAX_RECORDS; i++) {
        const item_record_t *r = &items->records[i];
        if (r->valid <= 0) continue;
        int cat = r->category;
        if (cat < 0 || cat >= SYSASSETS_ITEM_CATEGORIES) continue;
        if (cat <= max_cat) continue;

        /* Engine formula: ((count + 7) / 8) * 0x20, clamped to >= 0x40.
         * 8 icons per row (each 32×32), so this is "rows needed × row
         * height", with a minimum page height of 64. */
        int h = ((counts[cat] + 7) / 8) * 32;
        if (h < 64) h = 64;
        out_size_per_category[cat] = h;
        max_cat = cat;
        distinct++;
    }

    return distinct;
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "scene1_overlay.h"

sysassets_t g_sysassets;

/* tiny wrapper so the loader loop reads cleanly */
static int load_one(IDirect3DDevice8 *dev, const char *name,
                    uint32_t w, uint32_t h, sprite_t *out)
{
    sprite_destroy(out);
    return sprite_load(dev, name, w, h, out);
}

int sysassets_load_all(IDirect3DDevice8 *dev)
{
    int loaded = 0;

    /* L27..L33 — seven non-loop textures. */
    loaded += load_one(dev, "bmp/system.bmp",     0x80,  0x80,  &g_sysassets.system_bmp);
    loaded += load_one(dev, "bmp/fps2.tga",       0x100, 0x20,  &g_sysassets.fps2_tga);
    loaded += load_one(dev, "bmp/nowloading.tga", 0x100, 0x40,  &g_sysassets.nowloading_tga);
    loaded += load_one(dev, "bmp/savewindow.tga", 0x200, 0x80,  &g_sysassets.savewindow_tga);
    loaded += load_one(dev, "bmp/window.tga",     0x40,  0x40,  &g_sysassets.window_tga);
    loaded += load_one(dev, "bmp/item_win.tga",   0x400, 0x400, &g_sysassets.item_win_tga);
    loaded += load_one(dev, "bmp/data_win.tga",   0x200, 0x200, &g_sysassets.data_win_tga);

    /* L39..L49 — chara portrait loop. The engine reads w/h from a
     * BSS-zero table at &DAT_0438cec8 (populated later by the chara
     * selection scene); on a fresh boot every load passes (0, 0),
     * which sprite_load tolerates by loading at native resolution. */
    for (int i = 0; i < SYSASSETS_CHARA_VARIANTS; i++) {
        char name[64];
        snprintf(name, sizeof name, "bmp/chr/chr%02d.bmp", i);
        if (load_one(dev, name, 0, 0, &g_sysassets.chara_variants[i])) {
            loaded++;
        }
    }

    /* L50..L61 — thirteen single-load textures after the chara loop. */
    loaded += load_one(dev, "bmp/pc_gage.tga",     0x200, 0x200, &g_sysassets.pc_gage_tga);
    loaded += load_one(dev, "bmp/hpmp_base.tga",   0x200, 0x200, &g_sysassets.hpmp_base_tga);
    loaded += load_one(dev, "bmp/hpmp_exp.tga",    0x80,  0x80,  &g_sysassets.hpmp_exp_tga);
    loaded += load_one(dev, "bmp/ene_hp2.tga",     0x200, 0x100, &g_sysassets.ene_hp2_tga);
    loaded += load_one(dev, "bmp/magicjem.tga",    0x200, 0x100, &g_sysassets.magicjem_tga);
    loaded += load_one(dev, "bmp/effect.bmp",      0x100, 0x100, &g_sysassets.effect_bmp);
    loaded += load_one(dev, "bmp/kumonosu.tga",    0x80,  0x80,  &g_sysassets.kumonosu_tga);
    loaded += load_one(dev, "bmp/katter.tga",      0x40,  0x40,  &g_sysassets.katter_tga);
    loaded += load_one(dev, "bmp/effect_mahi.bmp", 0x100, 0x40,  &g_sysassets.effect_mahi_bmp);
    loaded += load_one(dev, "bmp/snow.bmp",        0x80,  0x80,  &g_sysassets.snow_bmp);
    loaded += load_one(dev, "bmp/effect_shot.bmp", 0x100, 0x100, &g_sysassets.effect_shot_bmp);
    loaded += load_one(dev, "bmp/shade.bmp",       0x100, 0x100, &g_sysassets.shade_bmp);

    /* L73..L99 — per-category item icon pages. */
    int icon_sizes[SYSASSETS_ITEM_CATEGORIES];
    int distinct = sysassets_compute_icon_sizes(&g_item, icon_sizes);
    int icons_loaded = 0;
    for (int cat = 0; cat < SYSASSETS_ITEM_CATEGORIES; cat++) {
        if (icon_sizes[cat] == 0) continue;
        char name[64];
        snprintf(name, sizeof name, "bmp/item/item%02d.bmp", cat);
        if (load_one(dev, name, 0x100, (uint32_t)icon_sizes[cat],
                     &g_sysassets.item_icons[cat])) {
            icons_loaded++;
        }
    }
    loaded += icons_loaded;

    /* L73..L83 of engine all.c: DAT_0076b948-gated per-layer sprite
     * loader, populated by FUN_00474f4f (O.10).  Each layer's filename
     * came from `GRPNN:filename` in `ef/grpN.idx`. */
    int overlay_layers_loaded = 0;
    int overlay_layer_count = g_scene1_overlay_layer_count;
    if (overlay_layer_count > SCENE1_OVERLAY_LAYER_COUNT_MAX) {
        overlay_layer_count = SCENE1_OVERLAY_LAYER_COUNT_MAX;
    }
    for (int i = 0; i < overlay_layer_count; i++) {
        const char *name = g_scene1_overlay_layer_filenames[i];
        if (name[0] == '\0') continue;
        if (load_one(dev, name, 0x100, 0x100, &g_sysassets.overlay_layers[i])) {
            g_scene1_overlay_layer_textures[i] = g_sysassets.overlay_layers[i].tex;
            overlay_layers_loaded++;
        } else {
            g_scene1_overlay_layer_textures[i] = NULL;
        }
    }
    loaded += overlay_layers_loaded;

    fprintf(stderr,
            "sysassets: %d textures loaded "
            "(static=20 chara=%d item_categories=%d/%d overlay_layers=%d/%d)\n",
            loaded, SYSASSETS_CHARA_VARIANTS, icons_loaded, distinct,
            overlay_layers_loaded, overlay_layer_count);

    return loaded;
}

void sysassets_unload_all(void)
{
    sprite_destroy(&g_sysassets.system_bmp);
    sprite_destroy(&g_sysassets.fps2_tga);
    sprite_destroy(&g_sysassets.nowloading_tga);
    sprite_destroy(&g_sysassets.savewindow_tga);
    sprite_destroy(&g_sysassets.window_tga);
    sprite_destroy(&g_sysassets.item_win_tga);
    sprite_destroy(&g_sysassets.data_win_tga);
    for (int i = 0; i < SYSASSETS_CHARA_VARIANTS; i++) {
        sprite_destroy(&g_sysassets.chara_variants[i]);
    }
    sprite_destroy(&g_sysassets.pc_gage_tga);
    sprite_destroy(&g_sysassets.hpmp_base_tga);
    sprite_destroy(&g_sysassets.hpmp_exp_tga);
    sprite_destroy(&g_sysassets.ene_hp2_tga);
    sprite_destroy(&g_sysassets.magicjem_tga);
    sprite_destroy(&g_sysassets.effect_bmp);
    sprite_destroy(&g_sysassets.kumonosu_tga);
    sprite_destroy(&g_sysassets.katter_tga);
    sprite_destroy(&g_sysassets.effect_mahi_bmp);
    sprite_destroy(&g_sysassets.snow_bmp);
    sprite_destroy(&g_sysassets.effect_shot_bmp);
    sprite_destroy(&g_sysassets.shade_bmp);
    for (int i = 0; i < SYSASSETS_ITEM_CATEGORIES; i++) {
        sprite_destroy(&g_sysassets.item_icons[i]);
    }
    for (int i = 0; i < SCENE1_OVERLAY_LAYER_COUNT_MAX; i++) {
        sprite_destroy(&g_sysassets.overlay_layers[i]);
        g_scene1_overlay_layer_textures[i] = NULL;
    }
}

#endif /* _WIN32 */
