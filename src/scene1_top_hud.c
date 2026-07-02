/*
 * scene1_top_hud.c — see scene1_top_hud.h.
 *
 * FUN_00406d50 (assembler) + FUN_00406a60 (number-row digit drawer).
 * The rotated clock hand (FUN_00406241) lives in render_quad.c as
 * render_quad_draw_rotated_rect.
 */

#include "scene1_top_hud.h"

#include <stdint.h>
#include "rng.h"       /* rng_next15 — the money-roll step (FUN_005041f6 / rand) */
#include "save_bank.h" /* SAVE_BANK_FIELD_MERCHANT_* — the XP-bar animator fields */
#include "audio.h"     /* audio_play_se_file — the level-up 00re_sys03a (FUN_0049933c) */

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

/* ─── screen-shake jitter (FUN_0040656e + FUN_00406584 asm 0x406762-0x4067c8) ─
 * FUN_0040656e arms DAT_00648280=4 (+ SE 0x29d at its caller-facing shell) —
 * fired per LANDING coin by the Table-B type-4 terminal kill (FUN_00414929
 * all.c:12732; RE §21.31.2).  The FUN_00406584 block then, while timer > 0:
 * timer--, then
 *   DAT_00648284 = ±ftol((u + 0.5)·2)   (magnitude 1..2, random sign)
 *   DAT_00648288 = ±ftol((u + 0.5)·2)
 * = FOUR LCG draws per shake frame (2 unit + 2 parity; retail callsites
 * u:0x406775 / u:0x40678c / 0x4067a3 / 0x4067b8), rng-load-bearing.
 * Timer expired → both offsets reset to 0 (asm 0x4067ca). */
static int32_t g_hud_shake_timer;   /* DAT_00648280 */
static int32_t g_hud_shake_x;       /* DAT_00648284 */
static int32_t g_hud_shake_y;       /* DAT_00648288 */

int32_t scene1_top_hud_shake_x(void)     { return g_hud_shake_x; }
int32_t scene1_top_hud_shake_y(void)     { return g_hud_shake_y; }
int32_t scene1_top_hud_shake_timer(void) { return g_hud_shake_timer; }

void scene1_top_hud_shake_pulse(void)   /* FUN_0040656e timer half; the SE
                                         * 0x29d plays at the kill default
                                         * (scene1_per_frame_open.c) */
{
    g_hud_shake_timer = 4;              /* DAT_00648280 = 4 */
}

void scene1_top_hud_shake_tick(void)
{
    if (g_hud_shake_timer > 0) {
        g_hud_shake_timer -= 1;
        float ux = rng_next_unit();                    /* FUN_00471089 */
        g_hud_shake_x = (int32_t)((ux + 0.5f) * 2.0f);
        float uy = rng_next_unit();
        g_hud_shake_y = (int32_t)((uy + 0.5f) * 2.0f);
        if (rng_next15() & 1) g_hud_shake_x = -g_hud_shake_x;
        if (rng_next15() & 1) g_hud_shake_y = -g_hud_shake_y;
    } else {
        g_hud_shake_x = 0;                             /* asm 0x4067ca */
        g_hud_shake_y = 0;
    }
}

/* ─── merchant-XP bar animator (FUN_00406584 @ all.c:4799-4848) ──────────────
 * The live bar value _DAT_0438b91c eases toward the bank's merchant EXP
 * (0xb0fd, DAT_0450fb8c) by (level_end − level_start)·0.01 per frame; the
 * glow-flash counter DAT_0064827c runs while easing (wraps at 0x1e); when the
 * eased value reaches level_end (0xb0ff) the merchant LEVELS UP: level (0xb100)
 * ++, level_start (0xb0fe) = level_end, level_end += (level+2)·0x32, the
 * level-up banner timer DAT_0438b920 arms (wraps at 100 — its FUN_00407ab4
 * pop render is still unported, PORT-DEBT(merchant-levelup-pop)), SE
 * 00re_sys03a plays; at the level cap (0x62) exp clamps to level_end.
 * Zero LCG draws (rng-neutral).  Engine block order inside FUN_00406584:
 * ease(4799) → shake(4812) → level-up(4831) → money roll(4849); the shake
 * block shares no state with this one, so the port runs ease+level-up
 * together before shake_tick.  On loads the engine SNAPS b91c to the bank
 * value (all.c:33136/100698/100768) — mirrored by the first-tick latch. */
static float   g_hud_xp_anim;      /* _DAT_0438b91c */
static int32_t g_hud_xp_flash;     /* DAT_0064827c (0..0x1d glow phase) */
static int32_t g_hud_levelup_t;    /* DAT_0438b920 (0..99 level-up banner) */
static int     g_hud_xp_snapped;   /* load-snap latch (engine load sites) */

float   scene1_top_hud_xp_anim(void)       { return g_hud_xp_anim; }
int32_t scene1_top_hud_xp_flash(void)      { return g_hud_xp_flash; }
int32_t scene1_top_hud_levelup_timer(void) { return g_hud_levelup_t; }
void    scene1_top_hud_xp_snap(int bank_exp)
{
    g_hud_xp_anim    = (float)bank_exp;
    g_hud_xp_snapped = 1;
}

void scene1_top_hud_xp_tick(uint32_t *bankw)
{
    if (!bankw) return;
    if (!g_hud_xp_snapped)
        scene1_top_hud_xp_snap((int)bankw[SAVE_BANK_FIELD_MERCHANT_EXP]);

    int32_t xp_bank  = (int32_t)bankw[SAVE_BANK_FIELD_MERCHANT_EXP];
    int32_t xp_start = (int32_t)bankw[SAVE_BANK_FIELD_MERCHANT_XP_START];
    int32_t xp_end   = (int32_t)bankw[SAVE_BANK_FIELD_MERCHANT_XP_END];

    /* all.c:4799-4811 — ease + flash. */
    if ((float)xp_bank <= g_hud_xp_anim) {
        if (g_hud_xp_flash != 0)
            g_hud_xp_flash += 1;
    } else {
        g_hud_xp_anim += (float)(xp_end - xp_start) * 0.01f;
        g_hud_xp_flash += 1;
    }
    if (g_hud_xp_flash == 0x1e)
        g_hud_xp_flash = 0;

    /* all.c:4831-4833 — the level-up banner timer. */
    if (g_hud_levelup_t > 0 && ++g_hud_levelup_t == 100)
        g_hud_levelup_t = 0;

    /* all.c:4834-4848 — level-up when the eased bar reaches level_end. */
    if ((float)xp_end <= g_hud_xp_anim) {
        int32_t level = (int32_t)bankw[SAVE_BANK_FIELD_MERCHANT_LEVEL];
        if (level < 0x62) {
            bankw[SAVE_BANK_FIELD_MERCHANT_LEVEL]    = (uint32_t)(level + 1);
            bankw[SAVE_BANK_FIELD_MERCHANT_XP_START] = (uint32_t)xp_end;
            g_hud_levelup_t = 1;
            bankw[SAVE_BANK_FIELD_MERCHANT_XP_END]   =
                (uint32_t)(xp_end + (level + 1 + 2) * 0x32);
            audio_play_se_file("bin/se/00re/system/00re_sys03a.bin");
        } else {
            bankw[SAVE_BANK_FIELD_MERCHANT_EXP] = (uint32_t)xp_end;
            g_hud_xp_anim = (float)xp_end;
        }
    }
}

/* ─── money rolling-counter (FUN_00406584 @ all.c:4849-4870) ─────────────────
 * The displayed money (DAT_0438b918) eases toward the working-bank gold by a
 * per-frame step `rand() % max(|Δ|/25, 10) + |Δ|/100` — the digits roll down
 * after a purchase / up after a sale, rather than snapping.  Consumes one
 * rng_next15 (the LCG `rand`, FUN_005041f6) per rolling frame, exactly as the
 * engine; at rest (displayed == bank) it is a no-op and burns no RNG.  Called
 * each frame a scene shows the HUD (the engine runs it for every scene_state>5
 * via the FUN_00453xxx scene dispatch). */
void scene1_top_hud_money_tick(int bank_gold)
{
    int delta = bank_gold - g_hud_money;
    if (delta == 0)
        return;
    int ad = (delta < 0) ? -delta : delta;
    int step_mod = ad / 0x19;                 /* |Δ| / 25 */
    if (step_mod < 10)
        step_mod = 10;
    int step = (int)((uint32_t)rng_next15() % (uint32_t)step_mod) + ad / 100;
    if (delta < 0) {                          /* bank < displayed → roll down */
        g_hud_money -= step;
        if (g_hud_money < bank_gold)
            g_hud_money = bank_gold;
    } else {                                  /* bank > displayed → roll up   */
        g_hud_money += step;
        if (g_hud_money > bank_gold)
            g_hud_money = bank_gold;
    }
}

/* ─── world-map travel-time tooltip (FUN_00406d50 Draw-2 + the FUN_00406584
 *     mode-8 band selector) ───────────────────────────────────────────────
 * The world map shows a baked help box in the top-left whose text depends on
 * the destination under the cursor:
 *   band 0  "Going to a dungeon will take 2 periods of time"   (dest 6)
 *   band 1  "Returning to the shop will take 1 period of time" (dest 0, DAT_045105a0!=0)
 *   band 3  "If you return now no time will pass"              (dest 0, DAT_045105a0==0)
 *   band 4  "This action will not take any time"               (any other dest)
 * (band 2 is the mode-1 "Opening the shop will take 1 period of time" counter
 * hint — a separate, still-unported path.)  All five bands are baked 120x80
 * into item_win.tga stacked from (832,0); the render lives in
 * scene1_top_hud_render.  DAT_00529708 = band id (-1 = hidden); DAT_00529704 =
 * the 0->15 slide-in counter consumed by the FUN_0046c86f scale animator. */
static int g_tooltip_msg = -1;   /* DAT_00529708 */
static int g_tooltip_ctr =  0;   /* DAT_00529704 */

/* FUN_00406584 mode-8 block (all.c:4776-4790): pick the band from the selected
 * destination, then ramp the slide-in counter toward 15.  Called once per
 * world-map sim frame BEFORE the nav updates the cursor, so the box lags a
 * cursor move by one frame exactly as the engine's pre-sim FUN_00406584 does.
 * return_pending = DAT_045105a0[slot] != 0 (the dest-0 variant selector). */
void scene1_top_hud_worldmap_tooltip_tick(int sel_dest, int return_pending)
{
    if (sel_dest == 6)      g_tooltip_msg = 0;                      /* dungeon  */
    else if (sel_dest == 0) g_tooltip_msg = return_pending ? 1 : 3; /* the shop */
    else                    g_tooltip_msg = 4;                      /* other    */
    if (g_tooltip_ctr < 0xf) g_tooltip_ctr++;
}

/* FUN_004060ff resets DAT_00529704/8.  The port calls this at world-map init so
 * the box slides in fresh on every entry (retail carries a ~0 counter in from
 * the house's pre-load ramp-down; starting at 0 reproduces the same first-frame
 * slide-in and avoids a stale box lingering on re-entry). */
void scene1_top_hud_tooltip_reset(void)
{
    g_tooltip_msg = -1;
    g_tooltip_ctr =  0;
}

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
#include "scene.h"                   /* g_scene_state / SCENE_STATE_WORLDMAP — tooltip gate */
#include "customer_service.h"        /* customer_service_active — cc08==4 camera-hint gate */
#include "call_trace.h"

#ifndef M_PI_F
#define M_PI_F 3.1415927f
#endif

/* FUN_0046c86f — the shared UI box open/close scale+alpha animator (ported in
 * scene1_dialogue_run.c).  Reused for the world-map tooltip slide-in; declared
 * locally to avoid pulling the whole dialogue-run header into the HUD. */
extern void ive_box_scale(int n, float *sx, float *sy, int *alpha, int closing);

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

    /* ── Draw 2: the world-map travel-time tooltip (FUN_00406d50 L5148). ──
     * A baked help band from item_win.tga — src (832, msg*80)-(952, msg*80+80),
     * 120x80 — chosen by the destination under the cursor, slid in via the
     * FUN_0046c86f scale animator on the 0->15 counter.  Same texture as the
     * gold frame above, so it batches into the SAME flush below.  Gated to the
     * world map (g_scene_state==8): only the mode-8 sim ramps the counter, and
     * the house (mode 1) shares this render path. */
    if (g_scene_state == SCENE_STATE_WORLDMAP &&
        g_tooltip_ctr > 0 && g_tooltip_msg >= 0) {
        float sx = 1.0f, sy = 1.0f;
        int   alpha = 0xff;
        ive_box_scale(g_tooltip_ctr, &sx, &sy, &alpha, 0);   /* FUN_0046c86f */
        (void)alpha;                                          /* engine draws @0xffffffff */
        const float dst[4] = { 88.0f - sx * 60.0f,
                               (yoff + 128.0f) - sy * 48.0f,
                               sx * 120.0f, sy * 80.0f };
        const float src[4] = { 832.0f, (float)(g_tooltip_msg * 80),
                               952.0f, (float)((g_tooltip_msg * 5 + 5) * 16) };
        render_quad_add(dst, src, tex->width, tex->height, 0xffffffffu);
    }

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
 * AND no menu/overlay is active (hidden during the iv1_1/iv1_2 cutscenes, which
 * show the "[ESC] Event Skip" hint instead; appears in free-roam).  DAT_0438b4e8
 * is a transient menu/transition flag set while an overlay owns the bottom-right
 * hint slot — notably the cc08==4 customer-service haggle, whose own UI draws
 * "Button 3: Item Details" at the SAME (440,440) slot.  Without the b4e8 gate the
 * port drew BOTH and they overlapped (user-flagged). */
void scene1_top_hud_camera_hint(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* DAT_0438b1c8 == 0: no dialogue active. */
    if (scene1_intro_dialogue_active()) return;
    /* DAT_0438b4e8 == 0: no menu/overlay owns the hint slot.  The port mirrors
     * the cc08==4 customer-service case via the cs-active flag (DAT_0438b7b0):
     * its UI draws the "Button 3: Item Details" hint here instead.  PORT-DEBT
     * (camera-hint-b4e8): the OTHER b4e8 menu/transition states
     * (FUN_00423b58/FUN_004426a7) aren't tracked yet — generalise when b4e8 lands. */
    if (customer_service_active()) return;

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
