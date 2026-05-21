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

#include "sim.h"   /* g_sim_buttons for scene_title_sim_default */

scene_title_menu_t  g_scene_title_menu;
scene_title_anim_t  g_scene_title_anim;

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

/* ─── sim init + bare-path sim (FUN_0049a3a3 + FUN_0049a59e) ─────────── */

void scene_title_anim_init_fresh(scene_title_anim_t *out)
{
    /* FUN_0049a3a3 line-for-line: all counters zero, fold-out flag set. */
    memset(out, 0, sizeof *out);
    out->menu_folding_out = 1;
    /* `pending_action` lives outside the engine's BSS-zero region — our
     * own outbox field. -1 = no action pending. */
    out->pending_action   = SCENE_TITLE_ACTION_NONE;
}

/* Engine button-mask bits (see input.h "input_binding_mask" docs):
 *   0x04 = UP, 0x08 = DOWN, 0x10 = A. The sim reads UP/DOWN from
 *   `held` (auto-repeat) and A from `pressed` (rising edge). */
#define TITLE_INPUT_UP    0x04
#define TITLE_INPUT_DOWN  0x08
#define TITLE_INPUT_A     0x10

void scene_title_sim(scene_title_anim_t *anim,
                     const scene_title_menu_t *menu,
                     uint16_t pressed,
                     uint16_t held)
{
    if (!anim || !menu) return;

    /* FUN_0049a3a3 line 239-250: cursor_anim slides toward 0 when
     * `menu_folding_out` is set, toward 10 when clear. */
    if (anim->menu_folding_out) {
        if (anim->cursor_anim > 0) {
            anim->cursor_anim--;
        }
    } else {
        if (anim->cursor_anim < 10) {
            anim->cursor_anim++;
        }
    }

    /* The engine's outer `if (DAT_09643520 == 10)` arm is the main-menu
     * input handler — but only when DAT_09643524 (submenu state) != 0,
     * which never happens in the bare-path build. For the bare slice
     * we only need the else arm:
     *   - DAT_09643550 < 1 (no fade in progress) [BSS-zero — always true]
     *   - DAT_09643520 == 0  (main menu fully on-screen)
     *   - DAT_09643544 < 1  (select pulse idle) → frame_counter advance
     *                                              + input handling
     *   - otherwise           → tick select_phase up to 0xf, then reset
     */
    if (anim->cursor_anim == 0) {
        if (anim->select_phase == 0) {
            anim->frame_counter++;

            /* Past 0x1bc6 (7110) the engine stops accepting cursor
             * input — input handling sits inside this `<` gate. At
             * frame == 0x1be4 (7140) the engine would attempt to
             * play recet_op.wmv (attract loop); on success the scene
             * transitions, on failure frame_counter is reset to 0.
             * Neither path is wired in the bare slice — frame_counter
             * just keeps incrementing past the input window. */
            if (anim->frame_counter < 0x1bc6) {
                if (pressed & TITLE_INPUT_A) {
                    /* Start the select countdown. Engine plays sound
                     * 0x143 here via FUN_00499519 — stubbed. */
                    anim->select_phase = 1;
                } else if (menu->count > 0) {
                    /* UP / DOWN move the cursor with engine wrap math:
                     *   UP   → (count - 1 + cursor) % count
                     *   DOWN → (count + 1 + cursor) % count
                     * Engine plays sound 0x146 on either move — stubbed. */
                    if (held & TITLE_INPUT_UP) {
                        anim->cursor_pos = (anim->cursor_pos
                                            + (uint32_t)(menu->count - 1))
                                           % (uint32_t)menu->count;
                    } else if (held & TITLE_INPUT_DOWN) {
                        anim->cursor_pos = (anim->cursor_pos
                                            + (uint32_t)(menu->count + 1))
                                           % (uint32_t)menu->count;
                    }
                }
            }
        } else {
            /* Select-countdown branch. Engine (FUN_0049a59e L521-594):
             *   DAT_09643544 += 1;
             *   if (DAT_09643544 != 0xf) return;
             *   iVar1 = (&DAT_09643358)[DAT_09643540];   // menu item code
             *   switch (iVar1) {
             *     case 3:  PostMessageA(hwnd, WM_CLOSE, 0, 0);          // EXIT
             *     case 2:  DAT_09643524 = 2; DAT_09643528 = 0;          // OPTIONS
             *     case 7:  FUN_0049f012(1); DAT_09643524 = 3; ...       // RANKING
             *     case 8:  DAT_09643524 = 4; ...                        // HIDDEN
             *     case 0, 5: DAT_0964351c += 1; DAT_0438bed4 = 1;       // NEW (loading transition)
             *     case 1, 4: FUN_0049b537(); DAT_09643524 = 1; ...      // CONTINUE
             *     case 6:  DAT_09643550 += 1; ...                       // SURVIVAL
             *   }
             *
             * The engine does NOT reset DAT_09643544 here — it stays at
             * 0xf, but the dispatched action either closes the window
             * (EXIT) or sets `menu_folding_out = 0` so cursor_anim
             * starts incrementing, which gates this entire `cursor_anim
             * == 0` block out on subsequent frames.
             *
             * For our bare slice we cannot dispatch a scene transition
             * (none of the destination scenes have ported). Instead we
             * just publish the would-be action into `pending_action`
             * and let the consumer (main.c) decide what to do. */
            anim->select_phase++;
            if (anim->select_phase >= 0xf) {
                anim->select_phase = 0xf;          /* stay at 0xf, match engine */
                if (anim->pending_action == SCENE_TITLE_ACTION_NONE
                    && menu->count > 0
                    && anim->cursor_pos < (uint32_t)menu->count) {
                    anim->pending_action = menu->items[anim->cursor_pos];
                }
            }
        }
    }

    /* LAB_0049b415: tail. Engine increments DAT_0964352c and calls
     * FUN_004356cd (a 3-line shake-effect helper that's a no-op at
     * BSS-zero; stubbed). */
    anim->pulse_phase++;
}

void scene_title_sim_default(void)
{
    /* Dispatch off the global button ring (sim.c wrote it earlier in
     * the same sim_step_a call). The engine's button masks live at
     * DAT_073dddd4 / DAT_073dddd6 — player 0 only at this layer; the
     * second player's mask is never read by the title sim. */
    scene_title_sim(&g_scene_title_anim,
                    &g_scene_title_menu,
                    g_sim_buttons[0].pressed,
                    g_sim_buttons[0].held);
}

#ifdef _WIN32

static sprite_t g_tex[SCENE_TITLE_TEX_COUNT];

int g_scene_title_assets_loaded = 0;

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
    g_scene_title_assets_loaded = (loaded == SCENE_TITLE_TEX_COUNT) ? 1 : 0;
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
     * paths build a `0xFF | r<<16 | g<<8 | b` greyscale.
     *
     * Blend is D3DTOP_ADDSIGNED (= 8), not D3DTOP_ADD (= 7) — engine
     * FUN_0049c644 L80 passes 8 as the third arg to
     * SetTextureStageState(0, D3DTSS_COLOROP, …). D3DTOP_ADD clips
     * highlights to white and the menu items look washed out;
     * D3DTOP_ADDSIGNED subtracts 0.5 from one term first, preserving
     * the per-item contrast the engine intends. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);

    const int selected_idx = (int)anim->cursor_pos;
    for (int i = 0; i < menu->count; i++) {
        const int code  = menu->items[i];
        float scale     = 0.8f;
        uint32_t bright = 0x95;  /* engine "1.33123e-43" denormal trick — */
                                 /*   bit pattern 0x95 = 149 */

        if (i == selected_idx) {
            scale = 1.0f;
            /* Two sin pulses modulate brightness. Ghidra hides the
             * FPU scale factor inside __ftol; the engine writes
             * `0x7f - iVar1` and `0x20 - iVar1`, but the hidden
             * scales are NEGATIVE (-127 and -32) — the press pulse is
             * a half-sine on [0,π] so `0x7f - sin(a)*127` would
             * darken the text mid-press, whereas the engine clearly
             * BRIGHTENS (the engine's clamp branch even pegs to
             * 0xff). So the effective formulas are:
             *   v  = 0x7f + 127 * sin(select_phase * π / 15)
             *   v += 0x20 +  32 * sin((pulse_phase % 45) * 2π / 45)
             * On a fresh boot (both phases zero) v = 0x9f, slightly
             * brighter than the unselected 0x95 default — enough for
             * ADDSIGNED to show the item as "selected". At press
             * midpoint sin(a)=1 → v peaks near 0xff (clamped). */
            const float a = (float)anim->select_phase * 3.1415927f / 15.0f;
            const float b = (float)(anim->pulse_phase % 0x2d)
                          * 6.2831855f / 45.0f;
            int v = 0x7f + (int)(sinf(a) * 127.0f);
            v +=    0x20 + (int)(sinf(b) *  32.0f);
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
