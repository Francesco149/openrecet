/*
 * scene1_top_hud.c — see scene1_top_hud.h.
 *
 * FUN_00406d50 (assembler) + FUN_00406a60 (number-row digit drawer).
 * The rotated clock hand (FUN_00406241) lives in render_quad.c as
 * render_quad_draw_rotated_rect.
 */

#include "scene1_top_hud.h"

/* ─── game-state inputs (see header) ───────────────────────────────────── */

static int   g_hud_day         = 0;       /* DAT_0450fb84[slot]; shown as +1 */
static int   g_hud_money        = 1000;   /* DAT_0438b918 (new-game start) */
static float g_hud_clock_phase  = 0.0f;   /* DAT_0438b7d4 */

void  scene1_top_hud_set_day(int day)            { g_hud_day = day; }
void  scene1_top_hud_set_money(int money)        { g_hud_money = money; }
void  scene1_top_hud_set_clock_phase(float p)    { g_hud_clock_phase = p; }
int   scene1_top_hud_day(void)                   { return g_hud_day; }
int   scene1_top_hud_money(void)                 { return g_hud_money; }
float scene1_top_hud_clock_phase(void)           { return g_hud_clock_phase; }

/* ─── Win32 render body ────────────────────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <math.h>
#include <stdio.h>

#include "render_quad.h"
#include "sysassets.h"
#include "scene1_intro_dialogue.h"   /* dialogue-active gate for the camera hint */
#include "call_trace.h"

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif

/* FUN_00406a60 — draw a number as digit-glyph sprites from item_win.tga.
 *
 *   x, y     — right anchor / baseline (640-relative pixels).
 *   value    — the integer to render ("%d").
 *   icon     — non-zero: draw the leading "pix" icon sprite (src 776,144-
 *              830,174) at (x-54, y) 43.2x24, then resume at x-47.6.
 *   color    — D3DCOLOR for every glyph.
 *   comma    — non-zero: insert a thousands-comma sprite (src 752,144-
 *              776,176) before every third digit from the right.
 *
 * Digits are laid out right-to-left at a 12.8 px pitch; digit `d` samples
 * src (d*24+512,144)-(d*24+536,168) and draws 19.2x19.2.  One flush at the
 * end (all glyphs share item_win.tga).  Verbatim from the engine. */
void scene1_top_hud_draw_number(IDirect3DDevice8 *dev,
                                float x, float y,
                                int value, int icon,
                                uint32_t color, int comma)
{
    const sprite_t *tex = &g_sysassets.item_win_tga;
    char buf[256];
    snprintf(buf, sizeof buf, "%d", value);

    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex->tex);

    if (icon != 0) {
        const float dst[4] = { x - 54.0f, y, 43.2f, 24.0f };
        const float src[4] = { 776.0f, 144.0f, 830.0f, 174.0f };
        render_quad_add(dst, src, tex->width, tex->height, color);
        x = (x - 54.0f) + 6.4f;
    }

    if (buf[0] != '\0') {
        /* index of the last character (engine walks to the NUL). */
        int last = 0;
        while (buf[last + 1] != '\0') last++;

        int pos = 0;   /* 1-based digit position from the right */
        for (int i = last; i >= 0 && buf[i] != '\0'; i--) {
            pos++;
            if (comma != 0 && pos != 1 && (pos % 3) == 1) {
                const float dst[4] = { x - 12.8f, y, 19.2f, 25.6f };
                const float src[4] = { 752.0f, 144.0f, 776.0f, 176.0f };
                render_quad_add(dst, src, tex->width, tex->height, color);
                x -= 3.2f;
            }
            x -= 12.8f;
            int d = buf[i] - '0';
            const float dst[4] = { x, y, 19.2f, 19.2f };
            const float src[4] = { (float)(d * 0x18 + 0x200), 144.0f,
                                   (float)(d * 0x18 + 0x218), 168.0f };
            render_quad_add(dst, src, tex->width, tex->height, color);
        }
    }

    render_quad_flush(dev);
}

void scene1_top_hud_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* E.2 probe — FUN_00406d50 @ 0x406d50. */
    CALL_TRACE_ENTER(0x406d50u);

    const sprite_t *tex = &g_sysassets.item_win_tga;
    if (!tex->tex) return;

    /* L: FUN_0049065b sub-init (2D-overlay camera feed; no-op in HOUSE,
     * see scene1_hud.c) — skipped here, already a stub at the aggregator. */

    /* L: letterbox offset.  Engine: local_c = DAT_0438b1dc * -128.0.  The
     * cinema-bar animator (DAT_0438b1dc) is unported (BSS-zero), so the
     * offset is 0 — the HUD sits at the top edge. */
    const float yoff = 0.0f;

    /* MIN/MAGFILTER = LINEAR (engine SetTextureStageState 0x11/0x10 = 2);
     * alpha-blend state was set by the aggregator's render_quad_state_setup. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

    /* ── Draw 1: the gold frame (clock ring + banner + Day-badge disc). ──
     * src (480,0)-(768,128) → dst (0, yoff, 230.4, 102.4). */
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex->tex);
    {
        const float dst[4] = { 0.0f, yoff, 230.40001f, 102.4f };
        const float src[4] = { 480.0f, 0.0f, 768.0f, 128.0f };
        render_quad_add(dst, src, tex->width, tex->height, 0xffffffffu);
    }

    /* Draw 2 — the new-event notification icon (gated on DAT_00529704>0 +
     * the FUN_0046c86f scale/alpha animator).  Both unported / BSS-zero,
     * so dormant; deferred. */

    render_quad_flush(dev);

    /* ── Draw 3: the clock hand (rotated). ──
     * Engine angle = π/2 - (DAT_0438b7d4 * π/3); centre (41.6, yoff+57.6);
     * dst rect (-12.8,-43.2)-(12.8,8.0); normalised UVs from item_win. */
    {
        const float angle = M_PI_F / 2.0f - (g_hud_clock_phase * M_PI_F) / 3.0f;
        const float dst[4] = { -12.8f, -43.2f, 12.8f, 8.0f };
        const float uv[4]  = { 0.4541015625f, 0.12597656f,
                               0.4833984375f, 0.18652344f };
        render_quad_draw_rotated_rect(dev, 41.6f, yoff + 57.6f, angle,
                                      dst, uv, 0xffffffffu);
    }

    /* ── Draw 4: the Day number (DAT_0450fb84[slot] + 1, capped 9999). ──
     * x picked by digit count; y = yoff + 60.8.  No icon, no comma. */
    {
        int day = g_hud_day + 1;
        if (day > 9999) day = 9999;
        float dx;
        if (day < 10)        dx = 89.6f;   /* 0x42b33333 */
        else if (day < 100)  dx = 92.8f;   /* 0x42b9999a */
        else if (day < 1000) dx = 96.0f;   /* 0x42c00000 */
        else                 dx = 104.0f;  /* 0x42d00000 */
        scene1_top_hud_draw_number(dev, dx, yoff + 60.8f, day,
                                   /*icon=*/0, 0xffffffffu, /*comma=*/0);
    }

    /* ── Draw 5: the money "N,NNNpix" (DAT_0438b918). ──
     * x = 244.8, y = yoff + 22.4; pix icon + thousands commas. */
    scene1_top_hud_draw_number(dev, 244.8f, yoff + 22.4f, g_hud_money,
                               /*icon=*/1, 0xffffffffu, /*comma=*/1);

    /* The DUNGEON minimap block (gated *DAT_068dd2f0 > 0) is dormant in
     * HOUSE (maptype 0) and deferred. */
}

/* The bottom-right "Button 4: Change Camera" control hint — the tail of
 * FUN_00409925 (decomp LAB_0040a5fd, the only part of that 3.4 KB HOUSE-town
 * HUD function that draws in free-roam; the rest is shop/stocking UI).  A
 * single baked sprite from bmp/data_win.tga (g_sysassets.data_win_tga,
 * DAT_073d8678): src (288,352)-(488,384), dst (440,440) size 200x32.
 *
 * Gate: `DAT_0438b1c8 == 0 && DAT_0438b4e8 == 0` — drawn only when NO dialogue
 * is active (so it's hidden during the iv1_1/iv1_2 cutscenes, which show the
 * "[ESC] Event Skip" hint instead, and appears in free-roam).  DAT_0438b4e8 is
 * a transient menu/transition flag (BSS-zero in free-roam), treated as 0. */
void scene1_top_hud_camera_hint(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* DAT_0438b1c8 == 0: no dialogue active. */
    if (scene1_intro_dialogue_active()) return;

    const sprite_t *tex = &g_sysassets.data_win_tga;
    if (!tex->tex) return;

    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tex->tex);
    {
        const float dst[4] = { 440.0f, 440.0f, 200.0f, 32.0f };
        const float src[4] = { 288.0f, 352.0f, 488.0f, 384.0f };
        render_quad_add(dst, src, tex->width, tex->height, 0xffffffffu);
    }
    render_quad_flush(dev);
}

#endif /* _WIN32 */
