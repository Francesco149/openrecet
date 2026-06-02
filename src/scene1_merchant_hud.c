/*
 * scene1_merchant_hud.c — see scene1_merchant_hud.h.
 *
 * Port of FUN_00409925's body (decomp L124-L179 / asm 0x409cf0-0x409f6x):
 * the bottom-left "Merchant Level" badge + experience bar, plus its level-
 * number sub-helper FUN_00481ec3 (0x481ec3).
 */

#include "scene1_merchant_hud.h"

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

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif

/* Engine HUD-animation globals.  All BSS-zero / steady-state in free-roam
 * (their per-frame updaters — the screen-shake, glow-pulse and level-up
 * animators at decomp L4795-L4848 — are unported), so reproduced as the
 * constants they hold at rest.  Promoted to named statics so the wiring is
 * obvious if/when those animators land. */
static const int   g_pulse_phase = 0;     /* DAT_0064827c (0..29, glow pulse) */
static const float g_shake_x     = 0.0f;  /* DAT_00648284 (screen-shake off)  */
static const float g_shake_y     = 0.0f;  /* DAT_00648288 (screen-shake off)  */
static const int   g_levelup_anim = 0;    /* DAT_0438b920 (level-up bounce)   */

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
static void scene1_merchant_hud_draw_level(IDirect3DDevice8 *dev,
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

void scene1_merchant_hud_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* E.2 probe — FUN_00409925 @ 0x409925 (this is its body, L124+). */
    CALL_TRACE_ENTER(0x409925u);

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

    /* Draw 2 — experience-bar fill.  Width = clamp(progress,0,1) * 142, at
     * dst x = ox+39; the source grows rightward from x=679 so the bar is
     * sampled from the gold gradient strip (row y 592-632). */
    {
        float range = (float)(g_xp_end - g_xp_start);
        if (range < 1.0f) range = 1.0f;                 /* engine: max(range,1) */
        float fill = ((float)(g_xp_cur - g_xp_start) / range) * 142.0f;
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

    /* Draw 4 — the level number, anchored at (ox+16, oy). */
    scene1_merchant_hud_draw_level(dev, ox + 16.0f, oy, g_level, 0xffffffffu);

    /* The "LEVEL UP!" pop (FUN_00407ab4, gated 0 < DAT_0438b920) is dormant
     * at rest and deferred — it needs the level-up event subsystem to drive
     * DAT_0438b920.  No quad is emitted while g_levelup_anim == 0. */
}

#endif /* _WIN32 */
