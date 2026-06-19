/*
 * customer_service_render.c — the cc08==4 CUSTOMER-SERVICE scene render.
 *
 * Ports the two retail 2D-overlay functions that draw the customer-service /
 * haggle stage on top of the (still-3D) HOUSE scene:
 *
 *   FUN_0046602e (0x46602e) → customer_service_render_chars()
 *       The 2D character art (the big Recette/Tear sprites), the letterbox
 *       curtain bars, the "!" surprise bubble, and the offer/button panel.
 *       Retail calls it at the TOP of the merchant-HUD aggregator FUN_00409925.
 *
 *   FUN_00466b7b (0x466b7b) → customer_service_render_overlay()
 *       The haggle dialogue box + typewriter line ("Tear" + the scripted
 *       text), the arrival/price labels, and the BARGAIN!! price layout.
 *       Retail calls it from the 2D-UI overlay render FUN_0040a765.
 *
 * Both read a once-per-frame snapshot of the cc08==4 state
 * (customer_service_get_render_state) — the engine's render reads those
 * DAT_0730bXXX / DAT_005c6bXX globals directly.  The textures come from the
 * scene_buy loaders (chrname.tga / shopmode.tga / the per-page character
 * sprites) + the sysasset atlases (system / hpmp_base / item_win / data_win).
 *
 * Geometry/colour/COLOROP transcribed 1:1 from the objdump (the decompile drops
 * FPU consts + texture-stage state).  Full spec: docs/findings/
 * customer-service-haggle-RE.md §8.6.
 *
 * Incremental port (this chip = Chip 3a): FUN_0046602e sections (a) letterbox /
 * (b) character sprites / (c) bubble + FUN_00466b7b section 6 (the dialogue box
 * + typewriter line).  The offer/button panel (FUN_0046602e d/e), the
 * pose/arrival/price/BARGAIN/button sections (FUN_00466b7b 1-5) + the
 * arrival-banner ticker (6d) are PORT-DEBT(cs-render-rest) — added next chip.
 */

#include "customer_service.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "render_quad.h"   /* render_quad_bind / _add / _add_mirrored / _flush */
#include "font_draw.h"     /* font_draw_text_box (FUN_00465db4) */
#include "sprite.h"
#include "scene_buy.h"     /* g_scene_buy_sprites/_chrname/_shopmode (texs) */
#include "sysassets.h"     /* g_sysassets.system_bmp / hpmp_base_tga / item_win */

/* ive_box_scale == FUN_0046c86f (the open/close scale+alpha wobble) — already a
 * confirmed-1:1 port in scene1_dialogue_run.c (sin-not-cos fix applied). */
extern void ive_box_scale(int n, float *sx, float *sy, int *alpha, int closing);

/* Bind + draw one quad (the engine's SetTexture(0,tex) → FUN_00404efc/e61 →
 * FUN_00405354 idiom).  Skips the draw when the texture is unloaded (tex==0) so
 * an unpopulated per-page sprite slot doesn't paint an untextured white square. */
static void cs_quad(IDirect3DDevice8 *dev, const sprite_t *spr,
                    const float dst[4], const float src[4],
                    uint32_t diffuse, int mirror)
{
    if (!spr || !spr->tex) return;
    render_quad_bind(dev, spr);
    if (mirror) render_quad_add_mirrored(dst, src, spr->width, spr->height, diffuse);
    else        render_quad_add(dst, src, spr->width, spr->height, diffuse);
    render_quad_flush(dev);
}

/* ── FUN_0046602e — character art + letterbox + bubble + offer panel ──────────
 * all.c:62872; gate DAT_0438b1cc==1 && DAT_0438b7b0!=0. */
void customer_service_render_chars(IDirect3DDevice8 *dev)
{
    struct cs_render_state s;
    customer_service_get_render_state(&s);

    if (s.b1cc != 1 || s.cs_active == 0) return;   /* 0x466052/60 */

    /* ── (a) letterbox / curtain bars (DAT_0730b530 → height, ≤32) ──────────
     * Two black bars (top y=0, bottom y=480-h) from the system atlas' 8x8
     * solid-black corner.  Batched: one bind, two quads, one flush. */
    int bar = s.b530 * 4;                          /* 0x466069 */
    if (bar > 0) {
        if (bar > 0x20) bar = 0x20;                /* clamp 32 */
        const sprite_t *sys = &g_sysassets.system_bmp;  /* DAT_073aa188 */
        if (sys->tex) {
            const float src[4] = { 0.0f, 0.0f, 8.0f, 8.0f };
            render_quad_bind(dev, sys);
            const float top[4] = { 0.0f, 0.0f, 640.0f, (float)bar };
            render_quad_add_mirrored(top, src, sys->width, sys->height, 0xff000000u);
            const float bot[4] = { 0.0f, (float)(0x1e0 - bar), 640.0f, (float)bar };
            render_quad_add_mirrored(bot, src, sys->width, sys->height, 0xff000000u);
            render_quad_flush(dev);
        }
    }

    /* ── (b) the two large 512x512 character sprites ────────────────────────
     * LEFT speaker (mirrored), slides in from the left to x≤-128, slot=b54c.
     * RIGHT speaker, slides in from the right to x≥256, slot=b550+b56c*10.
     * The per-page sprite array is g_scene_buy_sprites[page][slot] (flat index
     * /10, %10); unloaded slots (tex==0) draw nothing (cs_quad guard). */
    {
        const float src[4] = { 0.0f, 0.0f, 511.0f, 511.0f };

        float lx = (float)(s.b530 * 0xc) - 524.0f;     /* 0x466136 */
        if (lx > -128.0f) lx = -128.0f;
        int lflat = s.b54c;
        if (lflat >= 0 && lflat < SCENE_BUY_PAGE_COUNT * SCENE_BUY_SLOT_COUNT) {
            const sprite_t *spr = &g_scene_buy_sprites[lflat / 10][lflat % 10];
            const float dst[4] = { lx, 0.0f, 512.0f, 512.0f };
            cs_quad(dev, spr, dst, src, 0xffffffffu, /*mirror=*/1);
        }

        float rx = 640.0f - (float)(s.b52c * 0xc);     /* 0x4661b6 */
        if (rx < 256.0f) rx = 256.0f;
        int rflat = s.b550 + s.b56c * 10;
        if (rflat >= 0 && rflat < SCENE_BUY_PAGE_COUNT * SCENE_BUY_SLOT_COUNT) {
            const sprite_t *spr = &g_scene_buy_sprites[rflat / 10][rflat % 10];
            const float dst[4] = { rx, 0.0f, 512.0f, 512.0f };
            cs_quad(dev, spr, dst, src, 0xffffffffu, /*mirror=*/0);
        }

        /* ── (c) the "!" surprise bubble (above the right speaker) ───────────
         * b53c>0: wobble pos/scale/alpha via FUN_0046c86f, fade past frame 60.
         * Texture = hpmp_base.tga (DAT_073cc920), cell (368,48)-(416,96). */
        if (s.b53c > 0) {
            float bx, by; int balpha;
            ive_box_scale(s.b53c, &bx, &by, &balpha, 0);   /* FUN_0046c86f */
            float yoff = 0.0f;
            if (s.b53c > 0x3c) {
                yoff   = (float)(0x3c - s.b53c);
                balpha = balpha + (0x3c - s.b53c) * 0x10;
            }
            if (balpha > 0) {
                const sprite_t *hp = &g_sysassets.hpmp_base_tga;  /* DAT_073cc920 */
                const float bsrc[4] = { 368.0f, 48.0f, 416.0f, 96.0f };
                const float bdst[4] = {
                    (rx + 160.0f) - bx * 32.0f,
                    (yoff + 72.0f) - by * 32.0f,
                    bx * 64.0f,
                    by * 64.0f,
                };
                cs_quad(dev, hp, bdst, bsrc, ((uint32_t)balpha << 24) | 0xffffffu, 0);
            }
        }
    }

    /* (d) offer panel (b56c∈[2,9]) + (e) button row (b5d0!=0):
     * PORT-DEBT(cs-render-rest) — the item-want panel + action-button list.
     * Inert on the tutorial sell path (b56c==1); added with the haggle UI. */
}

/* ── FUN_00466b7b — haggle dialogue box + typewriter + BARGAIN!! price ────────
 * all.c:63270; gate DAT_0438b7b0!=0 (the caller adds DAT_0438b1cc==1). */
void customer_service_render_overlay(IDirect3DDevice8 *dev)
{
    struct cs_render_state s;
    customer_service_get_render_state(&s);

    if (s.cs_active == 0) return;   /* 0x466b87 */

    /* Sections 1-5 (pose panel + speech line / arrival panel + price labels /
     * BARGAIN!! price / choice buttons): PORT-DEBT(cs-render-rest) — next chip
     * (the user ordered the dialogue text first, the price layout after). */

    /* ── Section 6 — offer cards + the TYPEWRITER dialogue line ──────────────
     * Loop the 2 on-screen speaker slots; for each with a live pose-in timer
     * (b278[i]>0) draw the offer-card backdrop (shopmode.tga), the speaker
     * name plate (chrname.tga), the Z-advance prompt, and the typewriter line
     * via the existing font_draw_text_box (FUN_00465db4). */
    for (int slot = 0; slot < 2; slot++) {
        if (s.pose_timer[slot] <= 0) continue;     /* 0x46790c gate */

        float sx, sy; int alpha;
        ive_box_scale(s.pose_timer[slot], &sx, &sy, &alpha,
                      (s.cust_active[slot] == 0));         /* FUN_0046c86f */
        int nplate = s.pose_timer[slot] * 0x20 - 0xe1;    /* name-plate alpha */

        /* 6a — offer-card backdrop (shopmode.tga), slot 0 mirrored. */
        {
            const sprite_t *sm = &g_scene_buy_shopmode;       /* DAT_073a9580 */
            const float src[4] = { 0.5f, 176.0f, 448.0f, 351.0f };
            const uint32_t col = ((uint32_t)alpha << 24) | 0xffffffu;
            float ox = (slot == 0) ? 352.0f : 288.0f;
            const float dst[4] = {
                ox - sx * 224.0f, 376.0f - sy * 88.0f, sx * 448.0f, sy * 176.0f,
            };
            cs_quad(dev, sm, dst, src, col, /*mirror=*/(slot == 0));

            /* speaker name plate (chrname.tga) when this slot is active. */
            if (s.cust_active[slot] != 0 && nplate > 0) {
                const sprite_t *cn = &g_scene_buy_chrname;    /* DAT_073cc8d0 */
                const uint32_t ncol = ((uint32_t)nplate << 24) | 0xffffffu;
                if (slot == 0) {
                    const float nsrc[4] = { 0.0f, 32.0f, 128.0f, 64.0f };
                    const float ndst[4] = { 308.0f, 300.0f, 128.0f, 32.0f };
                    cs_quad(dev, cn, ndst, nsrc, ncol, 0);
                } else {
                    /* slot 1: the customer's name cell, indexed by the kyaku
                     * record's name_index (all.c:63 466b7b lines 427-443).  The
                     * chrname.tga atlas is 4 cols × 16 rows of 128×32 cells; the
                     * low range (≤0x15) packs 7 rows/col, the high range starts
                     * 8 rows/col at src-y +256. */
                    int ni  = s.cust_name_index;
                    int col = ni / 7;
                    float st = (float)((ni % 7) * 32);
                    if (ni > 0x15) {
                        col = (ni - 0x16) / 8;
                        st  = (float)(((ni - 0x16) % 8) * 32 + 256);
                    }
                    float sl = (float)(col * 128);
                    const float nsrc[4] = { sl, st, sl + 128.0f, st + 32.0f };
                    const float ndst[4] = { 204.0f, 300.0f, 128.0f, 32.0f };
                    cs_quad(dev, cn, ndst, nsrc, ncol, 0);
                }
            }
        }

        /* 6b — Z-advance / continue prompt (shopmode.tga), gated b55c!=0. */
        if (s.b55c != 0) {
            const sprite_t *sm = &g_scene_buy_shopmode;
            int blink = (s.b5b4 / 5) % 0x14;
            if (blink > 4) blink = 0;
            const float src[4] = { 736.0f, (float)(blink << 6),
                                   800.0f, (float)((blink + 1) * 0x40) };
            const float dst[4] = { (slot == 0) ? 468.0f : 368.0f,
                                   404.0f, 64.0f, 64.0f };
            cs_quad(dev, sm, dst, src, 0xffffffffu, 0);
        }

        /* 6c — the typewriter dialogue line (FUN_00465db4 == font_draw_text_box).
         * Text = the active visible line (b270), reveal budget = b548; x=250
         * (slot 0) / 130 (slot 1), y=346, scale 1.0 (the BOX scale). */
        if (s.cust_active[slot] != 0 && s.line) {
            float tx = (slot == 0) ? 250.0f : 130.0f;
            font_draw_text_box(dev, tx, 346.0f, s.line, 0xffffffffu, 1.0f, s.b548);
        }
    }

    /* 6d — item-detail overlay + customer-arrival banner + "great numbers"
     * ticker (b5c0>0 / b5e8): PORT-DEBT(cs-render-rest). */
}

#endif /* _WIN32 */
