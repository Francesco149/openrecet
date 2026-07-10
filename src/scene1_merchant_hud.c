/*
 * scene1_merchant_hud.c — see scene1_merchant_hud.h.
 *
 * Port of FUN_00409925's body (decomp L124-L179 / asm 0x409cf0-0x409f6x):
 * the bottom-left "Merchant Level" badge + experience bar, plus its level-
 * number sub-helper FUN_00481ec3 (0x481ec3).
 */

#include "scene1_merchant_hud.h"
#include "customer_service.h"   /* customer_service_render_chars (FUN_0046602e) */

/* ─── game-state inputs (see header; defaults = new-game HOUSE) ─────────── */

static int g_level     = 0;     /* DAT_0450fb98[slot]; shown as +1            */
static int g_xp_cur    = 0;     /* _DAT_0438b91c                               */
static int g_xp_start  = 0;     /* DAT_0450fb90[slot]                          */
static int g_xp_end    = 100;   /* DAT_0450fb94[slot]; >start so fill is sane  */

void scene1_merchant_hud_set_level(int level) { g_level = level; }
void scene1_merchant_hud_set_xp(int current, int level_start, int level_end)
{
    g_xp_cur   = current;
    g_xp_start = level_start;
    g_xp_end   = level_end;
}
int scene1_merchant_hud_level(void) { return g_level; }

/* ─── Win32 render body ────────────────────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <math.h>
#include <stdio.h>

#include "render_quad.h"
#include "sysassets.h"
#include "call_trace.h"
#include "font_draw.h"           /* font_draw_text_centered (FUN_0047d14c)   */
#include "scene1_render.h"       /* scene1_project_world (FUN_00490c78)      */
#include "scene1_shop_display.h" /* cbfc / cc00 / bf68 + grid stride          */
#include "scene1_player_ctrl.h"  /* player_ctrl_cc08 (DAT_0438cc08)          */
#include "save_work.h"           /* working-bank dwords (the live item grid) */
#include "save_bank.h"           /* SAVE_BANK_FIELD_DISPLAY_GRID (DAT_044f7030) */
#include "scene1_maplight.h"     /* scene1_current_stage_record (*068dd2f0)  */
#include "scene.h"               /* g_scene_state (DAT_0438b1c0)             */
#include "tables_item.h"         /* g_item DB record + name lookup           */

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif

/* Engine HUD-animation globals — LIVE since §21.31.5: the screen-shake,
 * glow-pulse and XP/level-up animators (decomp L4795-L4848) are ported in
 * scene1_top_hud.c (xp_tick/shake_tick, driven from the sim INGAME arm).
 * Read through their accessors at draw time. */
#include "scene1_top_hud.h"
#define g_pulse_phase  (scene1_top_hud_xp_flash())        /* DAT_0064827c */
#define g_shake_x      ((float)scene1_top_hud_shake_x())  /* DAT_00648284 */
#define g_shake_y      ((float)scene1_top_hud_shake_y())  /* DAT_00648288 */
#define g_levelup_anim (scene1_top_hud_levelup_timer())   /* DAT_0438b920 */

/* FUN_00481ec3 — draw the merchant level as large badge digits from
 * item_win.tga's glyph row (src y 848-888, 32 px/digit).
 *
 *   x, y   — badge anchor (640-relative).
 *   value  — 0-based level; drawn as value+1 ("%d", max 2 digits).
 *   color  — D3DCOLOR for the glyphs.
 *
 * COLOROP is MODULATE here (set by the caller).  The level-up bounce
 * (g_levelup_anim 1..10) scales the digits 1x..2x and lifts them; dormant
 * at rest (scale 1.0). */
void scene1_merchant_hud_draw_level(IDirect3DDevice8 *dev,
                                    float x, float y,
                                    int value, uint32_t color)
{
    /* L: scale = 1.0, bumped to sin(anim*pi/10)+1 during the level-up
     * bounce (anim frames 1..10). */
    float scale = 1.0f;
    if (g_levelup_anim > 0 && g_levelup_anim < 0xb) {
        scale = sinf((float)g_levelup_anim * M_PI_F / 10.0f) + 1.0f;
    }

    int disp = value + 1;          /* engine: param_3 + 1 (1-based level) */
    char buf[256];
    snprintf(buf, sizeof buf, "%d", disp);

    /* x pre-shift by digit count (engine: param_1 -= disp<10 ? 8 : 20). */
    x -= (disp < 10) ? 8.0f : 20.0f;

    const sprite_t *tex = &g_sysassets.item_win_tga;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex->tex);

    /* At most two glyphs (engine caps the loop at iVar3 != 2). */
    for (int i = 0; i < 2; i++) {
        char ch = buf[i];
        if (ch == '\0') break;
        int d = ch - '0';

        const float src[4] = { (float)(d * 0x20),  848.0f,
                               (float)((d + 1) * 0x20), 888.0f };

        float dx = x;
        if (disp < 10) dx = (x + 16.0f) - scale * 16.0f;   /* single-digit centring */
        const float dst[4] = {
            dx,
            (y + 20.0f) - scale * 20.0f,
            scale * 32.0f,
            scale * 40.0f,
        };
        render_quad_add(dst, src, tex->width, tex->height, color);

        x += 20.0f;
    }
    render_quad_flush(dev);
}

/* ── FUN_00409925 front (asm 0x409925-0x409cf0) — C3b ──────────────────────
 * The world-anchored item tooltip over the faced display stand.  Runs BEFORE
 * the level-badge body, matching the engine's intra-function order.
 *
 * Gate (asm 0x40994a-0x4099dd): scene_state==1 (HOUSE/INGAME) && stage
 * maptype==0 (*DAT_068dd2f0) && (cc08==1 || 0x32) && cbfc!=-1 && cc00!=-1 &&
 * (grid[cell]!=-1 || bf68!=0).  Item branch (bf68==0): the name tooltip.
 * Furniture branch (bf68!=0): name + "%d/%d" slot count — deferred (the bench
 * faces a sword cell, bf68==0).
 *
 * The faced cell is projected to screen at world (2·cbfc-9, 1.9, 2·cc00-6.5)
 * — note Z = the cell render_z + 0.5 (asm 0x4099e3-0x409a27).  Parchment
 * bubble = item_win src(832,480)-(959,559) dst(sx-26, sy-16, 164, 80), diffuse
 * 0xffffffff (the arg Ghidra dropped, asm 0x409a32). */
static void merchant_hud_item_tooltip(IDirect3DDevice8 *dev)
{
    /* ── gate ──────────────────────────────────────────────────────────── */
    if (g_scene_state != SCENE_STATE_INGAME)             /* DAT_0438b1c0==1  */
        return;
    const stage_record_t *stage = scene1_current_stage_record();
    if (stage && stage->maptype != 0)                    /* *DAT_068dd2f0==0 */
        return;
    int cc08 = player_ctrl_cc08();
    if (cc08 != 1 && cc08 != 0x32)
        return;
    int cbfc = shop_display_cbfc();
    int cc00 = shop_display_cc00();
    if (cbfc == -1 || cc00 == -1)
        return;
    int bf68 = shop_display_bf68();
    /* Faced cell's item id (engine *(DAT_044f7030 + (cbfc+cc00·20)·4 + slot·…)):
     * the working save-bank DISPLAY grid — the SAME grid the sparkle and the A2
     * removal read — NOT the furniture-layout grid (shop_display_grid_cell). */
    int32_t itemid = -1;
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank)
        itemid = (int32_t)bank[SAVE_BANK_FIELD_DISPLAY_GRID
                               + cbfc + cc00 * SHOP_DISPLAY_GRID_STRIDE];
    if (itemid == -1 && bf68 == 0)                  /* grid[cell]!=-1 || bf68 */
        return;

    const sprite_t *win = &g_sysassets.item_win_tga; /* DAT_073d8748          */
    if (!win->tex)
        return;

    /* ── project the cell to screen (FUN_00490c78) ─────────────────────── */
    float sx, sy;
    scene1_project_world(2.0f * (float)cbfc - 9.0f, 1.9f,
                         2.0f * (float)cc00 - 6.5f, &sx, &sy, NULL);

    /* ── parchment bubble (asm 0x409a2c-0x409a99); native brightness ───── */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)win->tex);
    {
        const float dst[4] = { sx - 26.0f, sy - 16.0f, 164.0f, 80.0f };
        const float src[4] = { 832.0f, 480.0f, 959.0f, 559.0f };
        render_quad_add(dst, src, win->width, win->height, 0xffffffffu);
        render_quad_flush(dev);
    }

    if (bf68 != 0) {
        /* The bf68≠0 furniture-stand tooltip branch (name + "%d/%d" slot count,
         * asm 0x409a9e-0x409bd6) is deferred — the item-display bench faces item
         * cells (bf68==0).  Retire on a furniture bench.
         * PORT-DEBT(stub, FUN_00409925): C3b furniture-stand tooltip branch (name + "%d/%d" slot count) not rendered; item branch only. */
        return;
    }
    if (itemid == -1)
        return;

    /* ── item name (item branch, asm 0x409bdc-0x409cea) ────────────────── */
    int rec = tables_item_find_slot_by_id(&g_item, itemid >> 6);
    if (rec < 0)
        return;
    const item_record_t *r = &g_item.records[rec];

    /* Name colour = the live market price-trend (FUN_004361b2 via
     * cs_news_price_trend — classifier ported in news_daily.c; retires the
     * C3b PORT-DEBT(simplified, FUN_004361b2) neutral default).  Table per
     * asm 409c01-409c3f: ≥2 ff0000 red / 1 ff4d4d light-red / 0 7f7f7f
     * neutral / -1 4d4dff light-blue / ≤-2 0000ff blue.  Neutral whenever
     * the daily-news list is empty (all pre-day-9 traces). */
    int trend = (int)cs_news_price_trend(itemid);
    uint32_t color = 0xff7f7f7fu;
    if (trend >= 2)       color = 0xffff0000u;
    else if (trend == 1)  color = 0xffff4d4du;
    else if (trend <= -2) color = 0xff0000ffu;
    else if (trend == -1) color = 0xff4d4dffu;

    char buf[256];
    if ((itemid & 0xf) == 0)
        snprintf(buf, sizeof buf, "%s", r->singular);                 /* 0x529968 */
    else
        snprintf(buf, sizeof buf, "%s+%d", r->singular, itemid & 0xf);/* 0x529960 */

    /* COLOROP=ADDSIGNED around the name (asm 0x409c44 / 0x409ce3), then back to
     * MODULATE for the trailing level badge the body draws next. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    font_draw_text_centered(dev, sx + 52.0f, sy + 26.0f, buf, color, 0.6f);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
}

void scene1_merchant_hud_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* E.2 probe — FUN_00409925 @ 0x409925 (this is its body, L124+). */
    CALL_TRACE_ENTER(0x409925u);

    /* FUN_0046602e (all.c:6423) — the cc08==4 customer-service character art +
     * letterbox + bubble.  Called FIRST in FUN_00409925, before the b1c0-gated
     * tooltip (and before this fn's own item_win early-return); self-gated on
     * b1cc==1 && b7b0!=0 so it's inert outside the selling mode. */
    customer_service_render_chars(dev);

    /* FUN_00409925 front (asm 0x409925-0x409cf0): the item tooltip over the
     * faced display stand — drawn first, then the level badge below. */
    merchant_hud_item_tooltip(dev);

    const sprite_t *tex = &g_sysassets.item_win_tga;
    if (!tex->tex) return;

    /* Badge origin: x = shake-x (0 at rest), y = shake-y + 424. */
    const float ox = g_shake_x;
    const float oy = g_shake_y + 424.0f;

    /* Grey glow pulse (decomp L126-L141, with the *64 multiply Ghidra
     * dropped on the sin result recovered from asm 0x409d66):
     *   gray = abs((int)(sinf(phase * pi/30) * 64)) + 0x7f   (0x7f..0xbf)
     *   color = 0xff000000 | gray*0x010101.
     * Drawn with COLOROP=ADDSIGNED so at rest (gray 0x7f≈0.5) the frame
     * shows at native brightness, brightening as the pulse climbs. */
    int gray = (int)(sinf((float)g_pulse_phase * M_PI_F / 30.0f) * 64.0f);
    if (gray < 0) gray = -gray;
    gray += 0x7f;
    const uint32_t pulse = 0xff000000u | ((uint32_t)gray * 0x00010101u);

    /* COLOROP = ADDSIGNED for the three frame layers (engine STSS
     * (0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED) at asm 0x409d0b). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADDSIGNED);
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex->tex);

    /* Draw 1 — back frame layer.  dst (ox,oy,192,40) src (640,544)-(832,584). */
    {
        const float dst[4] = { ox, oy, 192.0f, 40.0f };
        const float src[4] = { 640.0f, 544.0f, 832.0f, 584.0f };
        render_quad_add(dst, src, tex->width, tex->height, pulse);
    }

    /* Draw 2 — experience-bar fill.  Width = (b91c − level_start)/range · 142
     * (engine all.c:6525-6539 — the ANIMATED float _DAT_0438b91c, eased by
     * scene1_top_hud_xp_tick, NOT the raw bank exp: the bar visibly grows
     * during the sale fanfare, viewer note #19).  Bank start/end are live;
     * the g_xp_* setter values remain the non-bank fallback (host tests /
     * picker contexts). */
    {
        float cur, start, range;
        const uint32_t *wb = save_work_dwords_at(save_work_active_slot());
        if (wb) {
            cur   = scene1_top_hud_xp_anim();
            start = (float)(int32_t)wb[SAVE_BANK_FIELD_MERCHANT_XP_START];
            range = (float)((int32_t)wb[SAVE_BANK_FIELD_MERCHANT_XP_END] -
                            (int32_t)wb[SAVE_BANK_FIELD_MERCHANT_XP_START]);
        } else {
            cur   = (float)g_xp_cur;
            start = (float)g_xp_start;
            range = (float)(g_xp_end - g_xp_start);
        }
        if (range < 1.0f) range = 1.0f;                 /* engine: max(range,1) */
        float fill = ((cur - start) / range) * 142.0f;
        const float dst[4] = { ox + 39.0f, oy, fill, 40.0f };
        const float src[4] = { 679.0f, 592.0f, 679.0f + fill, 632.0f };
        render_quad_add(dst, src, tex->width, tex->height, pulse);
    }

    /* Draw 3 — front frame layer (groove border / glass over the bar).
     * dst (ox,oy,192,40) src (640,640)-(832,680). */
    {
        const float dst[4] = { ox, oy, 192.0f, 40.0f };
        const float src[4] = { 640.0f, 640.0f, 832.0f, 680.0f };
        render_quad_add(dst, src, tex->width, tex->height, pulse);
    }

    render_quad_flush(dev);

    /* COLOROP back to MODULATE for the level digits (engine STSS
     * (0, D3DTSS_COLOROP, D3DTOP_MODULATE) at asm 0x409f3e) — also the
     * state the following camera hint / top HUD expect. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    /* Draw 4 — the level number, anchored at (ox+16, oy).  Engine reads the
     * bank level (DAT_0450fb98) directly; g_level is the non-bank fallback. */
    {
        int level = g_level;
        const uint32_t *wb = save_work_dwords_at(save_work_active_slot());
        if (wb) level = (int)wb[SAVE_BANK_FIELD_MERCHANT_LEVEL];
        scene1_merchant_hud_draw_level(dev, ox + 16.0f, oy, level, 0xffffffffu);
    }

    /* The "LEVEL UP!" pop (FUN_00407ab4, gated 0 < DAT_0438b920): the timer
     * is LIVE now (scene1_top_hud_levelup_timer, armed by the xp_tick level-
     * up) but the pop render itself is still unported —
     * PORT-DEBT(merchant-levelup-pop); not exercised in the tutorial trace
     * (exp 10 < level_end). */
}

#endif /* _WIN32 */
