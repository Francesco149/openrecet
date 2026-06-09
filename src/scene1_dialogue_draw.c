/*
 * scene1_dialogue_draw.c — opening-prologue dialogue RENDER pass. See the
 * header. Port of the DRAW body of FUN_0046c9a2.
 *
 * Win32-only: the body is D3D8 draw calls (via render_quad + font_draw). The
 * pure-C helpers it depends on (the mirror quad, the box open/close wobble, the
 * reveal-truncation char count) live in render_quad.c / scene1_dialogue_run.c /
 * font_draw.c so the unit suite exercises them on the host.
 *
 * Built incrementally (docs/plans/vectorized-scribbling-backus.md):
 *   Layer 1 (here): the painted 2D background. Standees / box / nameplate / text
 *   land in Layers 2-4. Assets load on the first draw of each script (the sim
 *   tick has no D3D device), keyed by the driver's script generation counter.
 */
#include "scene1_dialogue_draw.h"

#ifdef _WIN32

#include "render_quad.h"   /* render_quad_* + <d3d8.h> (IDirect3DDevice8) */
#include "sprite.h"        /* sprite_t, sprite_load, sprite_destroy        */
#include "font_draw.h"     /* font_draw_text (glyph blit)                  */
#include "sysassets.h"     /* g_sysassets.data_win_tga — the skip-tip atlas */
#include "scene1_intro_dialogue.h"
#include "scene1_dialogue_run.h"
#include "skip_event.h"    /* skip_event_open() — gate the choice-box draw  */
#include "choice_box.h"    /* choice_box_draw() — the ESC skip prompt       */
#include "rng.h"           /* rng_next15() — chr/bg-shake jitter (rmb)      */

#include <string.h>

/* ─── loaded assets (one set per running script) ─────────────────────────
 * Reloaded when the driver's script generation changes (iv1_1 → iv1_2). */
static sprite_t  g_bg[IVE_MAX_NAMES];   /* DAT_073571f0 — bg images (by slot)   */
static int       g_bg_count   = 0;
static sprite_t  g_chr[IVE_MAX_NAMES];  /* DAT_0734f9b0 — chr graphics (by slot) */
static int       g_chr_count  = 0;
static sprite_t  g_window;              /* DAT_0735dc30 — bmp/ivent/ive_window.tga */
static sprite_t  g_nameplate;           /* DAT_073a3dd8 — bmp/ivent/chrname.tga    */
static int       g_ui_loaded  = 0;      /* window+nameplate are script-independent */
static unsigned  g_loaded_gen = (unsigned)-1;

static void free_assets(void)
{
    for (int i = 0; i < g_bg_count; i++)
        sprite_destroy(&g_bg[i]);
    memset(g_bg, 0, sizeof g_bg);
    g_bg_count = 0;
    for (int i = 0; i < g_chr_count; i++)
        sprite_destroy(&g_chr[i]);
    memset(g_chr, 0, sizeof g_chr);
    g_chr_count = 0;
}

/* FUN_0046bf38 (bg subset): load each parsed bg name into its slot. The engine
 * loads ive_window.tga / chrname.tga / chr graphics here too — added with the
 * box (Layer 3) and standee (Layer 2) passes. Proprietary art, loaded from the
 * user's install via the storage layer; never redistributed. */
static void ensure_assets(IDirect3DDevice8 *dev, const struct ive_program *prog)
{
    /* The window-frame + nameplate textures are the same for every script
     * (FUN_0046bf38 loads them unconditionally); load once. */
    if (!g_ui_loaded) {
        sprite_load(dev, "bmp/ivent/ive_window.tga", 0x200, 0x200, &g_window);
        sprite_load(dev, "bmp/ivent/chrname.tga",    0x200, 0x200, &g_nameplate);
        g_ui_loaded = 1;
    }

    unsigned gen = scene1_intro_dialogue_generation();
    if (gen == g_loaded_gen)
        return;
    free_assets();

    int n = prog->n_bg;
    if (n > IVE_MAX_NAMES) n = IVE_MAX_NAMES;
    for (int i = 0; i < n; i++) {
        /* Engine expects 0x400×0x200 (1024×512); sprite_load keeps the decoded
         * native size for UV normalisation (src is divided by s->width/height). */
        if (sprite_load(dev, prog->bg[i], 0x400, 0x200, &g_bg[i]))
            g_bg_count = i + 1;
    }

    /* chr standee graphics (FUN_0046bf38 third loop): each registered grp name
     * → its slot, with the script-declared W,H as the expected dims. */
    int nc = prog->n_chrname;
    if (nc > IVE_MAX_NAMES) nc = IVE_MAX_NAMES;
    for (int i = 0; i < nc; i++) {
        if (prog->chrname[i][0] == '\0') continue;
        if (sprite_load(dev, prog->chrname[i],
                        (uint32_t)prog->chr_w[i], (uint32_t)prog->chr_h[i], &g_chr[i]))
            g_chr_count = i + 1;
    }
    g_loaded_gen = gen;
}

/* ─── background (FUN_0046c9a2 lines 75-127) ─────────────────────────────── */
static void draw_background(IDirect3DDevice8 *dev,
                            const struct ive_runtime *rt,
                            const struct ive_program *prog)
{
    if (prog->n_bg <= 0)              /* DAT_073a3df0 — no bg set */
        return;
    int idx = rt->scene.bg_index;    /* DAT_073a6d90 */
    if (idx < 0 || idx >= g_bg_count || g_bg[idx].tex == NULL)
        return;
    const sprite_t *bg = &g_bg[idx];
    render_quad_bind(dev, bg);

    if (rt->scene.bg_scroll == 0 && rt->scene.bg_mode == 0) {
        /* Static: one full-screen quad, sampling the left 640×480. */
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_add(dst, src, bg->width, bg->height, 0xffffffffu);
    } else {
        /* Scroll: three 1024-wide tiles offset by bg_scroll/1000. bg-shake
         * (rmb:a,_): one RNG draw (NOT per-tile) jitters the shared tile Y by
         * (rng_next15()&0x1f)-16 while shake_bg is live (engine FUN_0046c9a2
         * L67507). Only the scroll path shakes — the static bg above does not.
         * PORT-DEBT(deferred): the scroll path is untested until a scrolling
         * prologue bg is exercised; the prologue caps are static. */
        int sx = rt->scene.bg_scroll / 1000;
        float sy = 0.0f;
        if (rt->scene.shake_bg != 0)
            sy = (float)((int)(rng_next15() & 0x1f) - 16);
        for (int t = -1; t <= 1; t++) {
            const float x = (float)(sx + t * 0x400);
            const float dst[4] = { x, sy, 1024.0f, 480.0f };
            const float src[4] = { 0.0f, 0.0f, 1024.0f, 480.0f };
            render_quad_add(dst, src, bg->width, bg->height, 0xffffffffu);
        }
    }
    render_quad_flush(dev);
}

/* ─── character standees (FUN_0046c9a2 lines 153-209) ────────────────────── */
static void draw_standees(IDirect3DDevice8 *dev,
                          const struct ive_runtime *rt,
                          const struct ive_program *prog)
{
    for (int i = 0; i < IVE_STANDEE_COUNT; i++) {
        const struct ive_standee *s = &rt->scene.standees[i];
        if (s->field[IVE_ST_ACTIVE] == 0)              /* not displayed */
            continue;
        int g = s->field[IVE_ST_GRAPHIC];
        if (g < 0 || g >= prog->n_chrname || prog->chrname[g][0] == '\0')
            continue;                                  /* no graphic registered */
        if (g >= g_chr_count || g_chr[g].tex == NULL)
            continue;                                  /* texture failed to load */
        const sprite_t *tex = &g_chr[g];
        float w = (float)prog->chr_w[g];
        float h = (float)prog->chr_h[g];

        render_quad_bind(dev, tex);

        /* Colour: ftol the 4 channel floats, repack exactly as the engine does
         * (color = ch18<<24 | ch15<<16 | ch16<<8 | ch17). */
        int c15 = (int)ive_word_f(s->field[15]);
        int c16 = (int)ive_word_f(s->field[16]);
        int c17 = (int)ive_word_f(s->field[17]);
        int c18 = (int)ive_word_f(s->field[18]);
        uint32_t color = ((uint32_t)c18 << 24) | ((uint32_t)c15 << 16)
                       | ((uint32_t)c16 << 8)  |  (uint32_t)c17;

        /* Blend mode (field 27): >=2 = additive (SRCALPHA/ONE), else normal
         * (SRCALPHA/INVSRCALPHA); bit0 picks COLOROP ADD vs MODULATE. */
        int blend = s->field[IVE_ST_BLEND];
        if (blend >= 2) {
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
        } else {
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        }
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
            (blend & 1) ? D3DTOP_ADD : D3DTOP_MODULATE);

        /* dst (x = field1, y = field2). chr-shake (rmb:_,b): while the
         * countdown is live, jitter Y by (rng_next15()&0x1f)-16 — one RNG draw
         * per DRAWN standee, in draw order (engine FUN_0046c9a2 L67606). X is
         * NOT jittered. engine-quirks §105. */
        float y = (float)(int)ive_word_f(s->field[2]);
        if (rt->scene.shake_chr != 0)
            y += (float)((int)(rng_next15() & 0x1f) - 16);
        const float dst[4] = { ive_word_f(s->field[1]), y, w + 0.5f, h + 0.5f };
        const float src[4] = { 0.0f, 0.0f, w, h };
        if (s->field[IVE_ST_MIRROR] == 1)
            render_quad_add_mirrored(dst, src, tex->width, tex->height, color);
        else
            render_quad_add(dst, src, tex->width, tex->height, color);
        render_quad_flush(dev);
    }
}

/* The text-speed table value DAT_005c78dc (px per reveal tick). The fresh-config
 * default is "normal" = 32 (same as ive_completion's IVE_REVEAL_SPEED). The
 * speed setting (DAT_056e5784 0..2) isn't wired port-side yet — PORT-DEBT. */
#define DLG_REVEAL_SPEED 32

/* FUN_00405a52 — draw one text row truncated to `max_chars` logical (SJIS-aware)
 * characters; returns the engine's char count (iVar3: chars before the final).
 * The truncated row is rendered via font_draw_text at scale 1.0 — which equals
 * FUN_0047d464's 0.65·(76/100) glyph scale (the font-size global _DAT_0052912c
 * defaults to 76, the same 0.76 baked into font_draw_text). */
static int dialogue_draw_row(IDirect3DDevice8 *dev, float x, float y,
                             const char *row, uint32_t color, int max_chars)
{
    char buf[256];
    int i1 = 0, i2 = 0, i3 = 0;
    if (max_chars != 0 && row[0] != '\0') {
        for (;;) {
            if (i3 < max_chars) buf[i2] = row[i1];
            if ((signed char)row[i1] < 1) {          /* SJIS lead / control → 2 bytes */
                if (i3 < max_chars) { buf[i2 + 1] = row[i1 + 1]; i2 += 2; }
                i1 += 2;
            } else {
                i1 += 1;
                if (i3 < max_chars) i2 += 1;
            }
            if (row[i1] == '\0') break;
            i3 += 1;
        }
    }
    buf[i2] = '\0';
    font_draw_text(dev, x, y, buf, color, 1.0f);
    return i3;
}

/* ─── dialogue window box + text (FUN_0046c9a2 lines 210-388) ───────────────
 * The box and the glyph text share `local_c` (the speaker-relative centre), so
 * they're ported together to mirror the engine's flow. */
static void draw_box_and_text(IDirect3DDevice8 *dev, const struct ive_runtime *rt,
                              const struct ive_program *prog)
{
    if (g_window.tex == NULL)
        return;

    /* Top banner (windowset; DAT_005c797c >= 0). Rare in the prologue. */
    int wctr = rt->scene.window_open_ctr;
    if (wctr >= 0) {
        render_quad_bind(dev, &g_window);
        const float dst[4] = { 0.0f, (float)((0x78 - wctr) * 4), 640.0f, 160.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 160.0f };
        render_quad_add(dst, src, g_window.width, g_window.height, 0xffffffffu);
        render_quad_flush(dev);
    }

    float local_c = 16.0f;                     /* engine default (line 224) */
    int box_open  = rt->box_open;              /* DAT_073a3e14 */
    int mode      = rt->scene.box_pos_mode;    /* DAT_005c7984 */

    if (box_open >= 1) {
        float sx = 1.0f, sy = 1.0f;
        int alpha = 0xff;
        ive_box_scale(box_open, &sx, &sy, &alpha, rt->line_row < 0);

        /* Centre reference: half the speaker standee's graphic width + its x ±
         * its centre offset (field 7). */
        int spk = rt->speaker;
        if (spk >= 0 && spk < IVE_STANDEE_COUNT) {
            const struct ive_standee *s = &rt->scene.standees[spk];
            int g = s->field[IVE_ST_GRAPHIC];
            float halfw = (g >= 0 && g < prog->n_chrname) ? (float)(prog->chr_w[g] / 2) : 0.0f;
            float x   = ive_word_f(s->field[1]);
            float ctr = ive_word_f(s->field[7]);
            local_c = (s->field[12] == 0) ? (halfw + x + ctr) : (halfw + x - ctr);
        } else {
            local_c = 0.0f;
        }

        float off    = (float)rt->scene.box_pos_off;
        uint32_t col = ((uint32_t)alpha << 24) | 0xffffffu;
        float dy     = (off + 88.0f) - sy * 88.0f;
        render_quad_bind(dev, &g_window);

        if (mode == 2) {
            /* text-only narration box — no frame body */
        } else if (mode == -1) {                  /* left-aligned */
            local_c = (local_c - 416.0f) + 32.0f;
            const float dst[4] = { (local_c + 208.0f) - sx * 208.0f, dy, sx * 416.0f, sy * 176.0f };
            const float src[4] = { 0.0f, 0.0f, 416.0f, 176.0f };
            render_quad_add(dst, src, g_window.width, g_window.height, col);
            render_quad_flush(dev);
        } else if (mode != 0) {                   /* mirrored frame */
            local_c -= 32.0f;
            const float dst[4] = { (local_c + 208.0f) - sx * 208.0f, dy, sx * 416.0f, sy * 176.0f };
            const float src[4] = { 0.0f, 0.0f, 416.0f, 176.0f };
            render_quad_add_mirrored(dst, src, g_window.width, g_window.height, col);
            render_quad_flush(dev);               /* MUST flush — else the next bind
                                                   * (nameplate) draws this box quad
                                                   * with the wrong texture. */
        } else {                                  /* mode 0 — centre box, lower strip */
            local_c -= 64.0f;
            const float dst[4] = { (local_c + 208.0f) - sx * 208.0f, dy, sx * 416.0f, sy * 176.0f };
            const float src[4] = { 0.0f, 176.0f, 416.0f, 352.0f };
            render_quad_add(dst, src, g_window.width, g_window.height, col);
            render_quad_flush(dev);
        }
        local_c += 80.0f;                         /* LAB_0046d30c */

        /* ── speaker nameplate + next-line arrow (lines 283-345) ── */
        int nalpha = (box_open - 4) * 0x3c;       /* (box_open-4)*60, clamp 255 */
        if (nalpha > 0xff) nalpha = 0xff;
        /* bVar1: mode 0 + non-choice draws NO nameplate (the centre box has no
         * name tab); every other mode does. */
        int draw_plate = !(mode == 0 && rt->scene.choice_mode < 0);
        if (mode != 2 && rt->line_row >= 0 && nalpha > 0) {
            uint32_t ncol = ((uint32_t)nalpha << 24) | 0xffffffu;

            if (draw_plate && g_nameplate.tex) {
                float sl, st, sr, sb;
                if (rt->scene.choice_mode < 0) {  /* name image from the 7-tall grid */
                    int idx = rt->portrait;       /* DAT_073a3e10 */
                    int col = idx / 7;
                    float ty = (float)((idx % 7) * 32);
                    if (idx > 0x15) {
                        col = (idx - 0x16) / 8;
                        ty  = (float)(((idx - 0x16) % 8) << 5) + 256.0f;
                    }
                    float tx = (float)(col * 128);
                    sl = tx; st = ty; sr = tx + 128.0f; sb = ty + 32.0f;
                } else {                          /* choice prompt cell */
                    sl = 640.0f; st = 224.0f; sr = 768.0f; sb = 256.0f;
                }
                float nx = (mode == 1) ? (local_c + 72.0f) : (local_c + 56.0f);
                float ny = (float)(rt->scene.box_pos_off + 0xc);
                const float dst[4] = { nx, ny, 128.0f, 32.0f };
                const float src[4] = { sl, st, sr, sb };
                render_quad_bind(dev, &g_nameplate);
                render_quad_add(dst, src, g_nameplate.width, g_nameplate.height, ncol);
                render_quad_flush(dev);
            }

            if (rt->revealed != 0 && g_window.tex) {  /* blinking next-line arrow */
                static int s_blink = 0;            /* DAT_073a3e0c (draw-only cosmetic) */
                int cell = (s_blink / 5) % 0x14;
                if (cell > 4) cell = 0;
                s_blink++;
                float nx = local_c + 256.0f;
                float ny = (float)(rt->scene.box_pos_off + 0x68);
                const float dst[4] = { nx, ny, 64.0f, 64.0f };
                const float src[4] = { 416.0f, (float)(cell << 6), 480.0f, (float)((cell + 1) * 0x40) };
                render_quad_bind(dev, &g_window);
                render_quad_add(dst, src, g_window.width, g_window.height, ncol);
                render_quad_flush(dev);
            }
        }
    }

    /* ── glyph text (lines 350-388) ── */
    if (rt->line_row < 0)
        return;
    float budget = (float)(rt->reveal - 4) * (float)DLG_REVEAL_SPEED / 32.0f;
    if (budget <= 0.0f)
        return;
    float base_y = (float)rt->scene.box_pos_off + 48.0f;
    float text_x = local_c - 16.0f;
    uint32_t color = (rt->scene.choice_mode >= 0) ? 0xffffff00u : 0xffffffffu;
    float row_y = 0.0f;
    for (int r = 0; r < rt->line_rows; r++) {
        int gi = rt->line_row + r;
        if (gi < 0 || gi >= IVE_MAX_ROWS) break;
        int max_chars = (int)budget;
        /* Per-row reveal fade-in (FUN_0047d464: glyph alpha = input_alpha *
         * clamp(param_6 * DAT_5198d8, 1.0), DAT_5198d8 = 0.2, ceil DAT_519364 =
         * 1.0; param_6 = this row's char budget).  A newly-revealing row ramps
         * from transparent to opaque over its first 5 revealed chars — the
         * dialogue "gradient to transparent".  A settled row's budget is large
         * (reveal climbs to 0x800) so it sits at full alpha. */
        float fade = (float)max_chars * 0.2f;
        if (fade > 1.0f) fade = 1.0f;
        uint32_t a = (uint32_t)((float)(color >> 24) * fade);   /* input alpha · fade */
        uint32_t rcol = (a << 24) | (color & 0xffffffu);
        int consumed = dialogue_draw_row(dev, text_x, row_y + base_y + 8.0f,
                                         prog->glyph[gi], rcol, max_chars);
        budget -= (float)consumed;
        if (budget <= 0.0f) break;
        row_y += 30.0f;                           /* 0x1e */
    }
}

/* ─── "ESC Key Event Skip" tip (FUN_0046c9a2 lines 67831-67843, the draw tail) ─
 * The very last quad of the draw: a fixed bottom strip from data_win.tga,
 * gated on `DAT_073a3e18 > 1` (≥2 dialogue frames elapsed) AND
 * `DAT_073a6db0 == 0` (the skip-disable flag, only ever 0 → always true). The
 * texture is the boot-time system atlas g_sysassets.data_win_tga, so no
 * per-script load is needed. src (288,384)-(488,416) → dst (440,440) 200×32. */
static void draw_skip_tip(IDirect3DDevice8 *dev, const struct ive_runtime *rt)
{
    if (rt->scene.skip_prompt <= 1)
        return;
    sprite_t *tip = &g_sysassets.data_win_tga;
    if (tip->tex == NULL)
        return;
    const float dst[4] = { 440.0f, 440.0f, 200.0f, 32.0f };
    const float src[4] = { 288.0f, 384.0f, 488.0f, 416.0f };
    render_quad_bind(dev, tip);
    render_quad_add(dst, src, tip->width, tip->height, 0xffffffffu);
    render_quad_flush(dev);
}

void scene1_dialogue_draw(IDirect3DDevice8 *dev)
{
    const struct ive_runtime *rt = scene1_intro_dialogue_runtime();
    if (rt == NULL)
        return;   /* no active script — nothing to draw */
    const struct ive_program *prog = scene1_intro_dialogue_program();
    if (prog == NULL)
        return;

    ensure_assets(dev, prog);

    render_quad_state_setup(dev);   /* FUN_0049b425 — 2D alpha-blend preset */
    draw_background(dev, rt, prog);
    draw_standees(dev, rt, prog);
    draw_box_and_text(dev, rt, prog);
    draw_skip_tip(dev, rt);

    /* FUN_0046c090 tail: when the ESC skip prompt is open (DAT_073a3dec==1),
     * the engine draws the choice box (FUN_0043537e + FUN_00435747) over the
     * dialogue. The box freezes the dialogue tick (sim.c), so `rt` is the
     * settled line underneath. */
    if (skip_event_open())
        choice_box_draw(dev);

    /* Remaining Layer 4: rmb screen-shake RNG reads + the choice/menu fade
     * overlay (DAT_073a6da4; no choices in the prologue). */
}

#endif /* _WIN32 */
