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

#include <math.h>          /* sinf — the panel slide / flash / cursor pulse */
#include <stdio.h>         /* snprintf — the price / label format strings */

#include "render_quad.h"   /* render_quad_bind / _add / _add_mirrored / _flush */
#include "font_draw.h"     /* font_draw_text / _centered / _right / _box       */
#include "sprite.h"
#include "scene_buy.h"     /* g_scene_buy_sprites/_chrname/_shopmode (texs) */
#include "sysassets.h"     /* g_sysassets.* (item_win/data_win/item_icons[]) + g_item */

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

/* ── FUN_00468034 — the big right-justified haggle PRICE NUMBER ───────────────
 * all.c:63 0x468034.  Draws a width-7 ("%7d", space-padded) number from the
 * shopmode.tga digit row (src-y 352..392): each digit a 32x40 cell at column
 * digit*0x20, on screen 32x40 at a 36px pitch with a +8px group gap after cell
 * indices 0/3/6.  Leading spaces are skipped (the field is right-aligned). */
static void cs_draw_price_number(IDirect3DDevice8 *dev, float x, float y,
                                 int value, uint32_t argb)
{
    const sprite_t *sm = &g_scene_buy_shopmode;        /* DAT_073a9580 */
    if (!sm->tex) return;
    char buf[16];
    snprintf(buf, sizeof buf, "%7d", value);           /* PTR_DAT_005c6da8 = "%7d" */
    render_quad_bind(dev, sm);
    for (int i = 0; i < 7; i++) {
        char c = buf[i];
        if (c != ' ') {
            int d = c - '0';
            const float src[4] = { (float)(d * 0x20),   352.0f,
                                   (float)((d + 1) * 0x20), 392.0f };
            const float dst[4] = { x, y, 32.0f, 40.0f };
            render_quad_add(dst, src, sm->width, sm->height, argb);
        }
        x += 36.0f;
        if (i == 0 || i == 3 || i == 6) x += 8.0f;
    }
    render_quad_flush(dev);
}

/* ── FUN_00469abb — comma-group a number (no unit suffix) ─────────────────────
 * <1000 → "%d"; <1e6 → "%d,%03d"; else "%d,%03d,%03d". */
static void cs_format_grouped(char *out, size_t n, int v)
{
    if (v < 1000)            snprintf(out, n, "%d", v);
    else if (v < 1000000)    snprintf(out, n, "%d,%03d", v / 1000, v % 1000);
    else                     snprintf(out, n, "%d,%03d,%03d",
                                      v / 1000000, (v / 1000) % 1000, v % 1000);
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

    /* PORT-DEBT(cs-render-priceinput): section 1 (b5d0 != 0) — the autonomous
     * "name a price" digit-entry panel (466b7b lines 40-130 + FUN_0046602e e).
     * b5d0 is never set on the scripted tutorial sell path (it uses the b598
     * BARGAIN banner below instead), so it is inert here. */

    /* ── Section 2 — the price-INFO panel (b5a0): the shopmode reference panel
     * + "Base Price NNN" + the showcase item name/icon (466b7b lines 131-250).
     * b5a0 is armed by the script's price-set op (op 2) and ramped to 0x3c by
     * the master tick. */
    if (s.b5a0 != 0) {
        /* slide-in factor (sin ramp over b5a0 0..0xf) + the arrival flash. */
        float slide = 1.0f;
        if (s.b5a0 < 0xf)
            slide = sinf((float)s.b5a0 * 2.5132742f / 15.0f) / sinf(2.5132742f);
        int pa = 0x7f;
        if (s.b5a0 > 0x1e && s.b5a0 < 0x2e)
            pa = 0x7f - (int)(sinf((float)(s.b5a0 - 0x1e) * 3.1415927f / 15.0f)
                              * -128.0f);
        /* the shopmode price-panel backdrop (the "TARGET!" panel).  Grey 0x7f
         * diffuse under ADDSIGNED (asm 0x466f7e→0x46702a) passes the texture at
         * full brightness (and the pa flash brightens it); the default MODULATE
         * would HALVE it → the dim panel (note #14). */
        {
            const sprite_t *sm = &g_scene_buy_shopmode;          /* DAT_073a9580 */
            const float src[4] = { 432.0f, 0.0f, 607.0f, 175.0f };
            const float dst[4] = { 304.0f - slide * 88.0f, 120.0f - slide * 88.0f,
                                   slide * 176.0f, slide * 176.0f };
            uint32_t col = 0xff000000u | ((uint32_t)pa << 16)
                         | ((uint32_t)pa << 8) | (uint32_t)pa;
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_ADDSIGNED);
            cs_quad(dev, sm, dst, src, col, 0);
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_MODULATE);
        }
        /* once mostly arrived (b5a0 >= 0x26): the price text + item name/icon. */
        if (s.b5a0 >= 0x26) {
            int islot = tables_item_find_slot_by_id(&g_item, s.b5a4 >> 6);
            const char *iname = (islot >= 0) ? g_item.records[islot].singular : "";
            int count = s.price_count, low4 = s.b5a4 & 0xf;
            /* PORT-DEBT(cs-price-trend): FUN_004361b2 (the market trend) → 0, so
             * the label is always "Base Price" + the grey (trend-0) tint.  trend
             * >0 → "High Price"/red, <0 → "Low Price"/blue (466b7b 184-217). */
            uint32_t tcol = 0xff7f7f7fu;
            char namebuf[256], labelbuf[256], numbuf[64];
            /* item-name line (304,80) scale 0.8.  The +N (low4>0) enchant forms
             * never appear in the haggle tutorial (b5a4 low nibble == 0). */
            if (count > 1) {
                if (low4 > 0) snprintf(namebuf, sizeof namebuf, "%d %ses+%d", count, iname, low4);
                else          snprintf(namebuf, sizeof namebuf, "%d %ses", count, iname);
            } else {
                if (low4 > 0) snprintf(namebuf, sizeof namebuf, "%s+%d", iname, low4);
                else          snprintf(namebuf, sizeof namebuf, "%s", iname);
            }
            /* COLOROP=ADDSIGNED for the trend-tinted text (asm 0x467143):
             * under ADDSIGNED the grey 0x7f7f7f diffuse passes the texture at
             * full brightness; the port's default render_quad MODULATE
             * (render_quad.c:269) would HALVE it (the dim-text bug). */
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_ADDSIGNED);
            font_draw_text_centered(dev, 304.0f, 80.0f, namebuf, tcol, 0.8f);
            /* "Base Price NNN" (304,168) scale 0.6. */
            cs_format_grouped(numbuf, sizeof numbuf, s.price_base);
            snprintf(labelbuf, sizeof labelbuf, "Base Price %s", numbuf);
            font_draw_text_centered(dev, 304.0f, 168.0f, labelbuf, tcol, 0.6f);
            /* "Showcase Item" (304,64) scale 0.8 when b564. */
            if (s.b564 != 0)
                font_draw_text_centered(dev, 304.0f, 64.0f, "Showcase Item", tcol, 0.8f);
            /* reset to MODULATE before the icon (asm 0x467233) — the item icon
             * + data_win frame are MODULATE on both sides. */
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_MODULATE);
            /* the item icon (280,96,48,48) from the category atlas, cell=subindex
             * (cleared to 0 when the b5a4 detail bit 0x10 is set). */
            if (islot >= 0) {
                int cat  = g_item.records[islot].category;
                int cell = (s.b5a4 & 0x10) ? 0 : g_item.records[islot].subindex;
                if (cat >= 0 && cat < SYSASSETS_ITEM_CATEGORIES &&
                    g_sysassets.item_icons[cat].tex) {
                    const sprite_t *ic = &g_sysassets.item_icons[cat];
                    float sl = (float)((cell % 8) * 32), st = (float)((cell / 8) * 32);
                    const float src[4] = { sl, st, sl + 32.0f, st + 32.0f };
                    const float dst[4] = { 280.0f, 96.0f, 48.0f, 48.0f };
                    cs_quad(dev, ic, dst, src, 0xffffffffu, 0);
                }
            }
            /* the data_win label frame (440,440,192,32). */
            {
                const sprite_t *dw = &g_sysassets.data_win_tga;  /* DAT_073d8678 */
                const float src[4] = { 288.0f, 320.0f, 480.0f, 352.0f };
                const float dst[4] = { 440.0f, 440.0f, 192.0f, 32.0f };
                cs_quad(dev, dw, dst, src, 0xffffffffu, 0);
            }
        }
    }

    /* ── Section 3 — the BARGAIN!! banner (b598): the shopmode banner panel +
     * the player's asking-price number + the digit cursor + the "name a price"
     * prompt + the markup % (466b7b lines 251-311).  Armed via b59c (PRID/PRIA)
     * and ramped by the master tick. */
    if (s.b598 != 0) {
        float bx, by; int balpha;
        /* closing flag = (b59c == 0): the banner pops open while PRID/PRIA holds
         * b59c (objdump 0x4673ae `cmp b59c; sete cl`). */
        ive_box_scale(s.b598, &bx, &by, &balpha, (s.b59c == 0)); /* FUN_0046c86f */
        int alpha = (s.pose_timer[0] > 0 || s.pose_timer[1] > 0) ? 0x7f : 0xff;
        /* the banner panel (shopmode src {0,0,432,176}). */
        {
            const sprite_t *sm = &g_scene_buy_shopmode;
            const float src[4] = { 0.0f, 0.0f, 432.0f, 176.0f };
            const float dst[4] = { 284.0f - bx * 220.0f, 306.0f - by * 88.0f,
                                   bx * 432.0f, by * 176.0f };
            cs_quad(dev, sm, dst, src, ((uint32_t)alpha << 24) | 0xffffffu, 0);
        }
        if (s.b598 >= 0xa) {
            /* COLOROP=ADDSIGNED for the NUMBER + cursor ONLY (asm 0x467493→
             * 0x4675d3): the grey 0x7f7f7f number + cursor pass the texture at
             * full brightness; the default MODULATE halved them (the dim bug).
             * The banner panel above AND the prompt/markup below stay MODULATE —
             * retail resets at 0x4675d3 right after the cursor (note #15: the
             * white/yellow prompt+markup go OVERBRIGHT under ADDSIGNED). */
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_ADDSIGNED);
            /* the big asking-price number (176,290), grey 0x7f7f7f under ADDSIGNED. */
            cs_draw_price_number(dev, 176.0f, 290.0f, s.price_ask,
                                 ((uint32_t)alpha << 24) | 0x7f7f7fu);
            /* the digit cursor (shopmode cell 448,176-496,224), x by digit count. */
            float cx = 409.0f - (float)(s.b560 * 0x24);
            if (s.b560 > 2) cx -= 8.0f;
            if (s.b560 > 5) cx -= 8.0f;
            if (s.b560 > 8) cx -= 8.0f;
            int cv = 0x7f - (int)((sinf((float)s.b5b4 * 0.2f) + 1.0f) * -32.0f);
            {
                const sprite_t *sm = &g_scene_buy_shopmode;
                const float src[4] = { 448.0f, 176.0f, 496.0f, 224.0f };
                const float dst[4] = { cx, 288.0f, 32.0f, 48.0f };
                uint32_t col = ((uint32_t)alpha << 24) | ((uint32_t)cv << 16)
                             | ((uint32_t)cv << 8) | (uint32_t)cv;
                cs_quad(dev, sm, dst, src, col, 0);
            }
            /* reset to MODULATE before the prompt + markup (asm 0x4675d3): they
             * use white/yellow diffuse, which ADDSIGNED would push OVERBRIGHT. */
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_MODULATE);
            /* the "name a price" prompt (312,250) scale 1.0, gated b598==0xf||b59c. */
            if (s.b598 == 0xf || s.b59c > 0) {
                if (s.b51c != 0) {
                    const char *t; uint32_t pc;
                    if (s.price_fileidx == 1) {
                        t = "What should I pay?";
                        pc = ((uint32_t)alpha << 24) | 0xffff37u;   /* yellow */
                    } else {
                        t = "How much should I?";
                        pc = ((uint32_t)alpha << 24) | 0xffffffu;   /* white */
                    }
                    font_draw_text_centered(dev, 312.0f, 250.0f, t, pc, 1.0f);
                }
                /* PORT-DEBT(cs-haggle-prompt-live): the b51c==0 live-customer
                 * response line (the non-scripted-tutorial machines). */
            }
            /* the markup "NN% Of Base Price" (400,342) right-aligned scale 0.8. */
            {
                int pct = (s.price_base != 0)
                        ? (int)(((float)s.price_ask / (float)s.price_base) * 100.0f) : 0;
                char pbuf[64], full[128];
                cs_format_grouped(pbuf, sizeof pbuf, pct);
                snprintf(full, sizeof full, "%s%% Of Base Price", pbuf);
                font_draw_text_right(dev, 400.0f, 342.0f, full,
                                     ((uint32_t)alpha << 24) | 0xffffffu, 0.8f);
            }
        }
    }

    /* ── Section 4 — the choice BUTTONS (b58c): 2 item_win panels + labels
     * (466b7b lines 312-381).  b58c climbs to 5 during the PRIA confirm poll. */
    if (s.b58c > 0) {
        float ybase = (s.b5a8 == 3) ? 186.0f : 362.0f;
        for (int i = 0; i < 2; i++) {
            uint32_t col = (s.b540 == i) ? 0xff7f7f7fu : 0xb97f7f7fu;
            float scale = (float)s.b58c * 0.2f;
            if (s.b590 >= 1) {
                if (s.b540 == i) {
                    /* selected button pulses grey while the commit countdown runs. */
                    int g = 0x7f - (int)(sinf((float)s.b590 * 3.1415927f / 15.0f)
                                         * -128.0f);
                    col = 0xff000000u | ((uint32_t)g << 16)
                        | ((uint32_t)g << 8) | (uint32_t)g;
                } else {
                    /* the other button shrinks + fades out. */
                    scale = 1.0f - (float)s.b590 * 0.1f;
                    int a = s.b590 * -0x19 + 0xff;
                    if (a < 0) continue;                  /* faded fully → gone */
                    col = ((uint32_t)a << 24) | 0x7f7f7fu;
                }
            }
            /* COLOROP=ADDSIGNED for the panel + label (asm 0x4677d6, set
             * per-button AFTER the faded-out `continue` so a culled button
             * skips the bracket like retail's js skip): both the grey item_win
             * panel and the label render at full brightness. */
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_ADDSIGNED);
            /* the item_win button panel (src {400,240,640,304}). */
            {
                const sprite_t *iw = &g_sysassets.item_win_tga;  /* DAT_073d8748 */
                const float src[4] = { 400.0f, 240.0f, 640.0f, 304.0f };
                const float dst[4] = { 312.0f - scale * 96.0f,
                                       (float)(i * 0x30) + ybase + 24.0f - scale * 24.0f,
                                       scale * 192.0f, scale * 48.0f };
                cs_quad(dev, iw, dst, src, col, 0);
            }
            /* the button label, centered at x=312 scale 1.0. */
            const char *label;
            if (s.b5a8 == 3) label = (i == 0) ? "Accept Order" : "Refuse";
            else             label = (i == 0) ? "Okay!"        : "Start Again";
            float ly = ybase + (i == 0 ? 12.0f : 60.0f);
            font_draw_text_centered(dev, 312.0f, ly, label, col, 1.0f);
            /* reset to MODULATE (asm 0x4678f3, per button). */
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                                  D3DTOP_MODULATE);
        }
    }

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
