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

#include <math.h>
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

/* ─── render (FUN_0049c644 — bare path only) ─────────────────────────── */

#include "render_quad.h"

/* Lookup table at PE 0x005d1cd4 — 9 dwords mapping menu-code →
 * "cursor tile index" in fuki.tga. Extracted via
 *   tools/analyze/pe.py bytes 0x005d1cd4 36
 * Indexing is (row = idx / 4, col = idx % 4). Each tile is 224x128
 * pixels in fuki.tga; rows lay out vertically at 128px stride from
 * top y = 336 (=0x150). */
static const int title_cursor_glyph_lut[9] = {
    0, 1, 2, 3, 4, 0, 7, 6, 5,
};

/* Helper — bind one of our 7 textures and forward to render_quad_add. */
static void title_quad(IDirect3DDevice8 *dev, int slot,
                       float dx, float dy, float dw, float dh,
                       float sx0, float sy0, float sx1, float sy1,
                       uint32_t color)
{
    const sprite_t *s = &g_tex[slot];
    if (!s->tex) return;
    /* The engine sets the texture *before* each quad-add — every
     * quad in this scene rebinds. We follow the same pattern so
     * the flush at the end emits all quads with their last-bound
     * texture (which the engine relies on too — DrawPrimitiveUP
     * with a single stage 0 binding per flush window). */
    render_quad_bind(dev, s);
    const float dst[4] = { dx, dy, dw, dh };
    const float src[4] = { sx0, sy0, sx1, sy1 };
    render_quad_add(dst, src, s->width, s->height, color);
    render_quad_flush(dev);
}

void scene_title_render(IDirect3DDevice8 *dev,
                        const scene_title_menu_t *menu,
                        const scene_title_anim_t *anim)
{
    if (!dev || !menu || !anim) return;

    render_quad_state_setup(dev);

    /* ── background bg2.bmp ────────────────────────────────────────────
     * Vertical scroll: src.top_y = 360 - (frame * 360 / 7140). On a
     * fresh boot (frame_counter == 0) that's exactly 360, so we sample
     * the 640x480 window at (0, 360)..(640, 840) of the 1024x1024
     * texture. */
    const float scroll_y = 360.0f
        - ((float)anim->frame_counter * 360.0f) / 7140.0f;
    title_quad(dev, SCENE_TITLE_TEX_BG2,
               0.0f, 0.0f, 640.0f, 480.0f,
               0.0f, scroll_y, 640.0f, scroll_y + 480.0f,
               0xFFFFFFFF);

    /* ── waku frame overlay (full screen, opaque tex with alpha cutouts) */
    title_quad(dev, SCENE_TITLE_TEX_WAKU,
               0.0f, 0.0f, 640.0f, 480.0f,
               0.0f, 0.0f, 640.0f, 480.0f,
               0xFFFFFFFF);

    /* ── fuki corner element ──────────────────────────────────────────
     * 416x32 strip pulled from (0, 992)..(416, 1024) of fuki.tga. */
    title_quad(dev, SCENE_TITLE_TEX_FUKI,
               112.0f, 448.0f, 416.0f, 32.0f,
               0.0f, 992.0f, 416.0f, 1024.0f,
               0xFFFFFFFF);

    /* ── title01 animated band ────────────────────────────────────────
     * The engine derives a base offset `local_14 = -cursor_anim * 64`
     * once and reuses it for the band, the menu items, and the
     * selected-row decoration tiles. At fresh boot (cursor_anim == 0)
     * `slide` is 0. */
    const float slide = -(float)(int)anim->cursor_anim * 64.0f;
    title_quad(dev, SCENE_TITLE_TEX_01,
               slide + 64.0f, 0.0f, 512.0f, 256.0f,
               0.0f, 0.0f, 512.0f, 256.0f,
               0xFFFFFFFF);

    /* ── menu items loop ──────────────────────────────────────────────
     * Each item is a 160x32 tile (scale 1.0 selected, 0.8 not) pulled
     * from fuki.tga at (224, code*32)..(384, (code+1)*32). Y position
     * is `index * y_stride + y_origin + 288 - 16*scale`. Colour for
     * the selected slot pulses via sin(select_phase*pi/15); other
     * items use the bit-pattern 0x95 (= 149) as a flat grey. Both
     * paths build a `0xFF | r<<16 | g<<8 | b` greyscale. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADD);

    const int selected_idx = (int)anim->cursor_pos;
    for (int i = 0; i < menu->count; i++) {
        const int code  = menu->items[i];
        float scale     = 0.8f;
        uint32_t bright = 0x95;  /* engine "1.33123e-43" denormal trick — */
                                 /*   bit pattern 0x95 = 149 */

        if (i == selected_idx) {
            scale = 1.0f;
            /* Two sin pulses modulate brightness. Ghidra hides the
             * FPU scale factor inside __ftol; reconstructed scales
             * are 127 (centered on 0x7f) and 32 (centered on 0x20).
             * On a fresh boot (select_phase == pulse_phase == 0)
             * both sines return 0, yielding 0x7f + 0x20 = 0x9f. */
            const float a = (float)anim->select_phase * 3.1415927f / 15.0f;
            const float b = (float)(anim->pulse_phase % 0x2d)
                          * 6.2831855f / 45.0f;
            int v = 0x7f - (int)(sinf(a) * 127.0f);
            v +=    0x20 - (int)(sinf(b) *  32.0f);
            if (v > 0xff) v = 0xff;
            if (v < 0)    v = 0;
            bright = (uint32_t)v;
        }

        const uint32_t color = 0xff000000u
                             | (bright << 16) | (bright << 8) | bright;
        const float dst_x = slide + 320.0f - scale * 80.0f;
        const float dst_y = (float)i * menu->y_stride + menu->y_origin
                          + 288.0f - scale * 16.0f;
        title_quad(dev, SCENE_TITLE_TEX_FUKI,
                   dst_x, dst_y, scale * 160.0f, scale * 32.0f,
                   224.0f, (float)(code * 32),
                   384.0f, (float)((code + 1) * 32),
                   color);
    }

    /* ── selected-item highlight overlay ──────────────────────────────
     * Restores MODULATE. Then draws three decoration tiles on top of
     * the selected menu row:
     *   1. Top-corner strip at (224, 32)..(384, 64) of fuki.tga, sized
     *      224x112 dst, positioned at selected_y + 216.
     *   2. The big cursor-glyph (224x128) pulled from a 4-col grid at
     *      (lut/4 * 224, lut%4 * 128 + 336) of fuki.tga.
     *   3. A small label strip (192x16) from (0, 144)..(192, 160). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    if (menu->count > 0) {
        const int sel = selected_idx;
        const int code = menu->items[sel];
        const float sy = (float)sel * menu->y_stride + menu->y_origin;
        const int lut = (code >= 0 && code < 9)
                          ? title_cursor_glyph_lut[code] : 0;

        /* Tile 1 — decorative outline frame (224×112) pulled from
         * (0, 0)..(224, 112) of fuki. dst.x = slide + 32. */
        title_quad(dev, SCENE_TITLE_TEX_FUKI,
                   slide + 32.0f, sy + 216.0f, 224.0f, 112.0f,
                    0.0f,  0.0f, 224.0f, 112.0f,
                   0xFFFFFFFF);

        /* Tile 2 — the BIG label glyph (e.g. "New Game") via the
         * 9-entry LUT at PE 0x005d1cd4. fuki has a 4-column grid of
         * 224×128 tiles starting at y = 0x150 (336). dst overlaps
         * tile 1 at the same (x, y) — modulate blend stacks them. */
        const float glyph_x0 = (float)((lut / 4) * 0xe0);
        const float glyph_y0 = (float)((lut % 4) * 0x80 + 0x150);
        const float glyph_x1 = (float)(((lut / 4) + 1) * 0xe0);
        const float glyph_y1 = (float)((lut % 4) * 0x80 + 0x1d0);
        title_quad(dev, SCENE_TITLE_TEX_FUKI,
                   slide + 32.0f, sy + 216.0f, 224.0f, 128.0f,
                   glyph_x0, glyph_y0, glyph_x1, glyph_y1,
                   0xFFFFFFFF);

        /* Tile 3 — small 192×16 ribbon below the label at
         * (slide + 224, sy + 296). Source (0, 144)..(192, 160). */
        title_quad(dev, SCENE_TITLE_TEX_FUKI,
                   slide + 224.0f, sy + 296.0f, 192.0f, 16.0f,
                   0.0f, 144.0f, 192.0f, 160.0f,
                   0xFFFFFFFF);
    }

    /* Sub-menu sub-screens (DAT_09643524 != 0) and fade-in
     * (DAT_09643518 > 0x1bc6) are intentionally not ported here —
     * both are gated on counters that stay at BSS-zero until the
     * sim port lands. Tracked in PROGRESS as deferred. */

    /* Final flush guard — restore additive→modulate already done. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

#endif /* _WIN32 */
