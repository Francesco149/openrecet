/*
 * scene_day_end.c — see scene_day_end.h.  Engine FUN_0048da71 (init),
 * FUN_0048dbfb (tick), FUN_0048ef00 (render) + FUN_0048edee / FUN_0048ee77
 * (scroll arrows), FUN_00473618 (assets), FUN_0048d954 (day-start reset),
 * FUN_0045e67e.
 */
#include "scene_day_end.h"

#include <stdio.h>
#include <stdlib.h>

#include "audio.h"                /* audio_play_se_by_id / _file */
#include "fade.h"                 /* FUN_004526f5 / FUN_004528b3 / FUN_0045281c */
#include "rng.h"                  /* rng_next15 (thunk 0x471084), rng_next_unit (FUN_00471089) */
#include "save_work.h"
#include "scene.h"                /* g_scene_state */
#include "scene1_day_close.h"     /* day_close_clear, the morning re-seat */
#include "scene1_intro_dialogue.h"/* the dialogue-box gate (DAT_0438b1c8) */
#include "scene1_records.h"       /* g_scene1_records_a_count, scene1_records_reset */
#include "scene1_particles_tick.h"/* scene1_particles_tick (FUN_0040fb3a) */
#include "scene1_spawn.h"         /* scene1_spawn (FUN_00447f4f) */
#include "scene1_top_hud.h"       /* scene1_top_hud_set_clock_phase (DAT_0438b7d4) */
#include "scene1_tutorial_dispatch.h" /* the morning beat (DAT_0438b928/b924) */
#include "scene_pause.h"          /* pause_dispatch, pause_weekly_quota, pause_day_index, pause_period_end */
#include "speech_bubble.h"        /* FUN_0040622f */
#include "worker_load.h"           /* FUN_00452cde */
#include "sim.h"                   /* FUN_00453373 (DAT_06a499a0) */
#include "scene_gameover.h"        /* FUN_0049d2e5 */

/* work-record dwords */
#define W_GOLD          3
#define W_LISTS         44999     /* 3 × 100 item codes (Sold / Bought / Found) */
#define W_COUNTS        45299     /* 3 counts */
#define W_DAY_START_GOLD 45302
#define W_SCORE         45303     /* the running score (SAVE_BANK_FIELD_SCORE) */
#define W_SCORE_TODAY   45304
#define W_DAY           45307
#define W_SHOPTIME      45308
#define W_TIER          45944
#define W_SEAT_AT_DOOR  45949     /* +0x2cdf4 */
#define W_MODE          46937
#define W_PAY_FORCED_B  0x2bc4b   /* byte: a debt-payment day regardless of the calendar */

static int s_scroll[6];     /* DAT_074b2ef0..f04 ([0..2] the panels) */
static int s_exit;          /* DAT_074b2f08 — the next-day fade (1 = start) */
static int s_pay;           /* DAT_074b2f0c — the payment screen */
static int s_pay_short;     /* DAT_074b2f10 — gold < the quota */
static int s_day;           /* DAT_074b2f14 — the day shown */
static int s_panel;         /* DAT_074b2f18 */
static int s_profit_shown;  /* DAT_074b2f1c */
static int s_profit;        /* DAT_074b2f20 */
static int s_score_shown;   /* DAT_074b2f24 */
static int s_score_base;    /* DAT_074b2f28 */
static int s_after;         /* DAT_074b2f2c — frames since the count finished */
static int s_frames;        /* DAT_074b2f30 */
static int s_rating;        /* DAT_074b2f34 — 0 best … 4 */
static int s_shake;         /* DAT_074b2f38 */
static int s_shake_x, s_shake_y;  /* DAT_074b2f3c / f40 */
static int s_pay_t;         /* DAT_074b2f44 */
static int s_paid;          /* DAT_074b2f48 */
static int s_quota;         /* DAT_074b2f4c */
static int s_pay_end;       /* DAT_074b2f50 */
static int s_pay_broke;     /* DAT_074b2f54 — ran out of gold */
static int s_t;             /* DAT_074b2f58 */
static int s_coin_flag;     /* DAT_0964312c */

int day_end_frame(void)        { return s_t; }
int day_end_profit_shown(void) { return s_profit_shown; }
int day_end_profit(void)       { return s_profit; }
int day_end_score_shown(void)  { return s_score_shown; }
int day_end_after(void)        { return s_after; }
int day_end_rating(void)       { return s_rating; }
int day_end_exit_phase(void)   { return s_exit; }
int day_end_paying(void)       { return s_pay; }

int32_t g_day_end_short;     /* DAT_0438b798 — the payment fell short (the game over) */
int32_t g_day_end_ending;    /* DAT_0438b79c — the day-36 payment made (the ending) */

void day_end_init(void)
{
    int32_t *w = (int32_t *)save_work_dwords_at(save_work_active_slot());
    speech_bubble_hide();                                /* FUN_0040622f */
    for (int i = 0; i < 6; i++)
        s_scroll[i] = 0;
    s_quota = 10000;
    s_exit = 0;
    s_panel = 0;
    s_pay = 0;
    s_pay_short = 0;
    s_pay_t = 0;
    s_shake = s_shake_x = s_shake_y = 0;
    s_paid = 0;
    if (w == NULL)
        return;
    int q = pause_weekly_quota();                        /* FUN_0048d997 */
    w[W_SCORE] += w[W_SCORE_TODAY];
    s_quota = q;
    s_pay_end = 0;
    s_pay_broke = 0;
    s_profit_shown = 0;
    s_score_shown = 0;
    s_score_base = w[W_SCORE] - w[W_SCORE_TODAY];
    s_after = 0;
    s_frames = 0;
    s_profit = w[W_GOLD] - w[W_DAY_START_GOLD];
    int p = s_profit < 0 ? 0 : s_profit;
    int v = 30 * w[W_COUNTS + 2] / 5 + 100 * p / (pause_weekly_quota() / 3)
          + 30 * w[W_COUNTS + 1] / 4 + 30 * w[W_COUNTS] / 3;
    if (v > 80)      s_rating = 0;
    else if (v > 60) s_rating = 1;
    else if (v > 40) s_rating = 2;
    else             s_rating = (v <= 20) + 3;
    s_day = w[W_DAY];
    scene1_tutorial_dispatch_arm_beat();                 /* DAT_0438b928 = 1, b924 = 0 */
    s_t = 0;
    scene1_records_reset(1);                             /* FUN_0040f64b(1) */
}

/* FUN_0048d954 — start a new day: the day-start gold, today's score, the three
 * lists (-1) and their counts. */
void day_end_day_start_reset(int32_t *w)
{
    w[W_SCORE_TODAY] = 0;
    w[W_DAY_START_GOLD] = w[W_GOLD];
    for (int k = 0; k < 3; k++) {
        w[W_COUNTS + k] = 0;
        for (int i = 0; i < 100; i++)
            w[W_LISTS + 100 * k + i] = -1;
    }
}

/* FUN_0045e67e — tick down the 10 work-record timers at +0x2a600 (stride 20
 * bytes) whose flag two dwords earlier is 1. */
static void day_timers_tick(int32_t *w)
{
    int32_t *e = w + 0x2a600 / 4;
    for (int i = 0; i < 10; i++, e += 5)
        if (e[-2] == 1)
            --*e;
}

/* The payment coin shower (the dialogue-box arm and the payment tail). */
static void coin_shower(void)
{
    s_coin_flag = 1;
    float x = (rng_next_unit() - 0.5f) * 20.0f;
    scene1_spawn(0, x, 10.0f, 20.0f, 146, 0.2f, 1);
}

void day_end_tick(uint16_t pressed, uint16_t held)
{
    int32_t *w = (int32_t *)save_work_dwords_at(save_work_active_slot());
    const uint32_t in = (uint32_t)pressed | ((uint32_t)held << 16);   /* DAT_073dddd4 */

    /* L5-17: the payment camera (DAT_0438b774..77c, DAT_0438cc38..58) +
     * FUN_00441c3e — PORT-DEBT(stub, FUN_0048e49c): the payment-screen 3D shop
     * render is unported, so its camera is not driven here. */
    g_scene1_records_a_count = SCENE1_RECORDS_A_COUNT;   /* DAT_0076b960 = 0x1000 */
    scene1_particles_tick();                             /* FUN_0040fb3a */
    s_coin_flag = 0;
    if (s_shake <= 0) {
        s_shake_x = 0;
        s_shake_y = 0;
    } else {
        --s_shake;
        s_shake_x = (int)(rng_next15() & 7) - 4;
        s_shake_y = (int)(rng_next15() & 7) - 4;
    }
    if (w == NULL)
        return;

    /* L335-340: a dialogue box is up (the payment event script) — only the
     * coin shower runs. */
    /* DAT_0438b1c8 itself (the gate): the mode-9 arm cleared it this frame when
     * the script ended, before this tick — not busy()'s settle frame */
    if (scene1_intro_dialogue_gate() != 0) {
        if (s_pay_t > s_pay_end && s_pay_broke == 0)
            coin_shower();
        return;
    }

    if (s_pay_t == 4)
        audio_play_se_by_id(0x144);

    if (s_exit != 0) {
        if (s_exit == 1)
            fade_phase1_start(0, 0x11);                  /* FUN_004526f5(0, 17) */
        ++s_exit;
        if (fade_is_done()) {
            day_end_day_start_reset(w);                  /* FUN_0048d954 */
            w[W_SHOPTIME] = 0;
            day_close_clear();                           /* DAT_0438b4e0 = 0 */
            scene1_top_hud_set_clock_phase(0.0f);        /* DAT_0438b7d4 = 0 */
            /* DAT_0438b4dc = w[W_TIER] — the stage index; PORT-DEBT(hardcode,
             * FUN_00436f97): no port mirror (scene1_preload_floor_layout). */
            day_timers_tick(w);                          /* FUN_0045e67e */
            g_day_end_ending = 0;                        /* DAT_0438b79c */
            if (s_pay != 0 && s_pay_short == 0)
                g_day_end_ending = w[W_DAY] == 36;
            int story = 0;
            if (w[W_DAY] == 8 || w[W_DAY] == 36) {
                story = 1;
                if (w[W_MODE] == 3 || w[W_MODE] == 2)
                    story = 0;
            }
            if ((s_pay != 0 && s_pay_short != 0) || story != 0) {
                /* the game over, the ending, the week-end story: mode 2
                 * (FUN_0049d2e5) or 7 (FUN_0049da9e) and its scene load */
                g_day_end_short = s_pay_short;           /* DAT_0438b798 */
                sim_set_mode_9a0(0);                     /* FUN_00453373: the pause closes */
                if (g_day_end_ending != 0) {
                    g_scene_state = 7;
                    /* PORT-DEBT(stub, FUN_0049da9e): the ending's entry + mode 7 FUN_0049db8a */
                } else {
                    g_scene_state = 2;
                    scene_gameover_enter();              /* FUN_0049d2e5 */
                }
                /* FUN_00474d92 (the D3D teardown) + FUN_00473668 (the scene's
                 * textures): render side */
                fade_phase_out_start(0, 0x11);           /* FUN_0045281c(0, 17) */
                day_end_init();                          /* FUN_0048da71 */
                worker_load_spawn();                     /* FUN_00452cde */
            } else {
                pause_dispatch(1);                       /* FUN_004536b9: close */
                fade_phase_out_start(0, 0x11);           /* FUN_0045281c(0, 17) */
                day_end_init();                          /* FUN_0048da71 */
                w[W_SEAT_AT_DOOR] = 0;
                g_scene_state = 1;
                scene1_house_morning_reseat();           /* FUN_004852fb */
            }
        }
        goto tail;
    }

    if (s_pay != 0) {
        /* the debt payment: the gold flows into the payment until the quota
         * (or the gold) runs out, then Tear's verdict + Z. */
        if (s_pay_end != 0) {
            if (s_pay_t == s_pay_end + 90) {
                s_shake = 16;
                audio_play_se_by_id(0x29f);
            }
            if (s_pay_t == s_pay_end && s_pay_broke == 0) {
                static const char *const k_paid_se[3] = {
                    "bin/se/01ti/system/01ti_sys13.bin",
                    "bin/se/01ti/system/01ti_sys14.bin",
                    "bin/se/01ti/system/01ti_sys15.bin",
                };
                audio_play_se_file(k_paid_se[rng_next15() % 3u]);
            }
            if (s_pay_t > s_pay_end && s_pay_broke == 0)
                coin_shower();
            if (s_pay_t > s_pay_end + 100 && (in & 0x10)) {
                s_exit = 1;
                /* Tear's verdict: broke (0, 5), the day-36 payment (1, 19), paid (0, 4) */
                if (s_pay_broke != 0)
                    scene1_intro_dialogue_start_single(0, 5);   /* FUN_0044ba2c(0, 5, 0) */
                else if (w[W_DAY] == 36)
                    scene1_intro_dialogue_start_single(1, 19);  /* FUN_0044ba2c(1, 19, 0) */
                else
                    scene1_intro_dialogue_start_single(0, 4);   /* FUN_0044ba2c(0, 4, 0) */
            }
        }
        ++s_pay_t;
        if (s_pay_end != 0 || s_pay_t <= 60 || s_paid >= s_quota)
            goto tail;
        int step = s_paid / 50;
        if (s_paid / 50 < 10)
            step = 10;
        int amt = s_paid / 50 + (int)((uint32_t)rng_next15() % (uint32_t)step);
        if (amt > s_quota - s_paid)
            amt = s_quota - s_paid;
        if (amt > w[W_GOLD])
            amt = w[W_GOLD];
        if (s_t % 6 == 0)
            audio_play_se_by_id(0x29d);
        w[W_GOLD] -= amt;
        s_paid += amt;
        if (s_paid < s_quota) {
            if (w[W_GOLD] != 0)
                goto tail;
            s_pay_broke = 1;
        } else {
            s_paid = s_quota;
        }
        s_pay_end = s_pay_t + 10;
        audio_play_se_by_id(0x14d);
        goto tail;
    }

    ++s_frames;
    if (s_after != 0) {
        if (s_after == 70)
            audio_play_se_by_id(0x29e);
        if (s_after == 60) {
            static const char *const k_rating_se[5] = {
                "bin/se/01ti/system/01ti_sys05.bin",
                "bin/se/01ti/system/01ti_sys06.bin",
                "bin/se/01ti/system/01ti_sys07.bin",
                "bin/se/01ti/system/01ti_sys08.bin",
                "bin/se/01ti/system/01ti_sys09.bin",
            };
            if (s_rating >= 0 && s_rating <= 4)
                audio_play_se_file(k_rating_se[s_rating]);
        }
        if (s_frames >= 46)
            ++s_after;
        if (s_after > 130 && (in & 0x10)) {
            if (w[W_DAY] > 9999)
                w[W_DAY] = 9999;
            /* the HENSAI debug rows (FUN_00451874 text grid, not drawn) call
             * FUN_00482059, which caches the period end. */
            (void)pause_period_end();
            if (w[W_MODE] != 3 &&
                (((const uint8_t *)w)[W_PAY_FORCED_B] != 0 ||
                 pause_day_index((const uint32_t *)w) == pause_period_end())) {
                audio_play_se_file("bin/se/01ti/system/01ti_sys12.bin");
                s_pay = 1;
                if (w[W_GOLD] < s_quota)
                    s_pay_short = 1;
            } else {
                s_exit = 1;
            }
            ++w[W_DAY];
        }
        goto counted;
    }
    if (s_frames >= 46) {
        if (s_profit_shown == s_profit) {
            /* the score difference counts up (or down) to today's score */
            int target = w[W_SCORE_TODAY];
            if (s_score_shown < target) {
                int step = target / 100;
                if (step < 10)
                    step = 10;
                s_score_shown += (int)((uint32_t)rng_next15() % (uint32_t)step) + target / 100;
                if (s_score_shown > target)
                    s_score_shown = target;
            } else if (s_score_shown > target) {
                int step = target / -100;
                if (step < 10)
                    step = 10;
                s_score_shown -= (int)((uint32_t)rng_next15() % (uint32_t)step) - target / 100;
                if (s_score_shown < target)
                    s_score_shown = target;
            } else {
                s_after = 1;
            }
        } else {
            int base = abs(s_profit) / 100;
            int step = base < 10 ? 10 : base;
            int d = base + (int)((uint32_t)rng_next15() % (uint32_t)step);
            if (s_profit <= 0) {
                s_profit_shown -= d;
                if (s_profit_shown < s_profit)
                    s_profit_shown = s_profit;
            } else {
                s_profit_shown += d;
                if (s_profit_shown > s_profit)
                    s_profit_shown = s_profit;
            }
        }
    }

counted:
    if (s_t == 4 || s_t == 12 || s_t == 20)
        audio_play_se_by_id(0x174);
    if (s_t > 46) {
        if (in & 0x20000) {
            s_panel = (s_panel + 2) % 3;
            audio_play_se_by_id(0x146);
        }
        if (in & 0x10000) {
            s_panel = (s_panel + 4) % 3;
            audio_play_se_by_id(0x146);
        }
        int lim = w[W_COUNTS + s_panel] - 6;
        if (lim < 0)
            lim = 0;
        if ((in & 0x40000) && s_scroll[s_panel] > 0) {
            audio_play_se_by_id(0x146);
            --s_scroll[s_panel];
        }
        if ((in & 0x80000) && s_scroll[s_panel] < lim - 1) {
            audio_play_se_by_id(0x146);
            ++s_scroll[s_panel];
        }
    }

tail:
    ++s_t;
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>
#include <math.h>

#include "font_draw.h"
#include "render_quad.h"
#include "sprite.h"
#include "sysassets.h"            /* item_win.tga (DAT_073d8748) */
#include "tables_item.h"          /* g_item, tables_item_find_slot_by_id (FUN_004681f6) */

static sprite_t s_tex_choushuu;   /* DAT_073aa7d8 — the payment screen (FUN_0048e49c) */
static sprite_t s_tex_board;      /* DAT_073da000 — result_bord01.tga */
static sprite_t s_tex_hanko;      /* DAT_073da010 — hanko.tga, 5 stamps stacked */

void day_end_load_assets(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (dev == NULL)
        return;
    sprite_destroy(&s_tex_choushuu);
    sprite_destroy(&s_tex_board);
    sprite_destroy(&s_tex_hanko);
    sprite_load(dev, "bmp/choushuu.tga",      1024, 1024, &s_tex_choushuu);
    sprite_load(dev, "bmp/result_bord01.tga",  512,  256, &s_tex_board);
    sprite_load(dev, "bmp/hanko.tga",          128, 1024, &s_tex_hanko);
}

static void quad(const sprite_t *t, float x, float y, float w, float h,
                 float u0, float v0, float u1, float v1, uint32_t col, int mirrored)
{
    const float dst[4] = { x, y, w, h };
    const float src[4] = { u0, v0, u1, v1 };
    if (mirrored)
        render_quad_add_mirrored(dst, src, t->width, t->height, col);
    else
        render_quad_add(dst, src, t->width, t->height, col);
}

/* FUN_0048edee / FUN_0048ee77 — the list's up / down scroll arrows
 * (item_win.tga (448,896)-(512,944) / (512,896)-(576,944), 64×48). */
static void scroll_arrow(IDirect3DDevice8 *dev, float x, float y, uint32_t col, int down)
{
    const sprite_t *iw = &g_sysassets.item_win_tga;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)iw->tex);
    const float u0 = down ? 512.0f : 448.0f;
    quad(iw, x - 32.0f, y - 24.0f, 64.0f, 48.0f, u0, 896.0f, u0 + 64.0f, 944.0f, col, 0);
    render_quad_flush(dev);
}

static uint32_t argb(int a) { return 0x00ffffffu | ((uint32_t)a << 24); }

/* ftol of an alpha the engine scales by the fade (x87 → int, truncation). */
static int fade_alpha(int a, float fade) { return (int)((float)a * fade); }

void day_end_render(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    const int32_t *w = (const int32_t *)save_work_dwords_at(save_work_active_slot());
    if (dev == NULL || w == NULL)
        return;
    static const char *const k_titles[3] = { "Sold", "Bough\x82\x94", "Found" };   /* off_005cf554 */
    char buf[256];

    render_quad_state_setup(dev);                          /* FUN_0049b425 */
    const float fade = 1.0f - (float)s_pay_t * 0.05f;      /* the payment screen fades it out */
    if (fade > 0.0f) {
        const uint32_t tcol = argb((int)(fade * 255.0f));
        for (int k = 0; k < 3; k++) {
            /* each panel drops in 8 frames after the previous one, bouncing */
            const int d = s_t - 8 * k;
            const float px = (float)k * 208.0f;
            float py = (float)(30 * d) - 180.0f;
            if (py > 120.0f)
                py = 120.0f;
            if (d < 30)
                py += sinf((float)d * 6.2831855f / 30.0f) * (64.0f - (float)(2 * d));
            const int sel = (k == s_panel);
            int a = 200;
            if (sel) {
                a = 255;
                py -= 32.0f;
            }
            const uint32_t col = argb(fade_alpha(a, fade));
            IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)s_tex_board.tex);
            quad(&s_tex_board, px, py + 40.0f, 224.0f, 224.0f, 0.0f, 0.0f, 224.0f, 224.0f, col, 0);
            quad(&s_tex_board, px + 18.0f, py, 188.0f, 64.0f,
                 224.0f, sel ? 0.0f : 64.0f, 412.0f, sel ? 64.0f : 128.0f, col, 0);
            render_quad_flush(dev);
            font_draw_text_centered(dev_in, px + 112.0f, py + 20.0f, k_titles[k], tcol, 1.0f);

            const int count = w[W_COUNTS + k];
            const int shown = count < 7 ? count : 7;
            for (int i = 0; i < shown; i++) {
                const int32_t code = w[W_LISTS + 100 * k + i + s_scroll[k]];
                if (code == -1)
                    continue;
                const int slot = tables_item_find_slot_by_id(&g_item, code >> 6);
                snprintf(buf, sizeof buf, "%s",
                         (slot >= 0 && slot < g_item.count) ? g_item.records[slot].singular : "");
                font_draw_text(dev_in, px + 48.0f, (float)(26 * i) + py + 68.0f, buf, tcol, 0.7f);
            }
            if (s_scroll[k] > 0)
                scroll_arrow(dev, px + 40.0f, py + 56.0f, tcol, 0);
            if (count > shown && s_scroll[k] < count - shown)
                scroll_arrow(dev, px + 40.0f, py + 248.0f, tcol, 1);
            render_quad_flush(dev);
        }

        /* the "Daily Profits / Day N" banner zooms in from frame 20 */
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)s_tex_board.tex);
        if (s_t > 20) {
            const int e = s_t - 20;
            float sc = 2.0f - (float)e * 0.1f;
            if (sc < 1.0f)
                sc = 1.0f;
            int a = 25 * e;
            if (a > 255)
                a = 255;
            const uint32_t col = argb(fade_alpha(a, fade));
            const float bw = sc * 224.0f, bh = sc * 64.0f;
            quad(&s_tex_board, 288.0f - bw, 16.0f, bw, bh, 224.0f, 128.0f, 384.0f, 192.0f, col, 0);
            quad(&s_tex_board, 352.0f, 16.0f, bw, bh, 224.0f, 128.0f, 384.0f, 192.0f, col, 1);
            render_quad_flush(dev);
            font_draw_text_centered(dev_in, 320.0f, 8.0f, "Daily Profits", col, sc * 1.2f);
            snprintf(buf, sizeof buf, "Day %d", s_day + 1);
            font_draw_text_centered(dev_in, 320.0f, 48.0f, buf, col, sc);
        }

        /* "Profits" slides in from frame 8, "Score Difference" from 20 */
        {
            const int e = s_t - 8;
            float off = 32.0f - (float)(2 * e);
            if (off < 0.0f)
                off = 0.0f;
            int a = (e & 0x8000000) ? 0 : 16 * e;
            if (a > 255)
                a = 255;
            const uint32_t col = argb(fade_alpha(a, fade));
            font_draw_text(dev_in, off + 288.0f, 408.0f, "Profits", col, 1.0f);
            snprintf(buf, sizeof buf, s_profit_shown > 0 ? "+%d" : "%d", s_profit_shown);
            font_draw_text_right(dev_in, off + 576.0f, 408.0f, buf, col, 1.0f);
        }
        {
            const int e = s_t - 20;
            float off = 32.0f - (float)(2 * e);
            if (off < 0.0f)
                off = 0.0f;
            int a = (e & 0x8000000) ? 0 : 16 * e;
            if (a > 255)
                a = 255;
            const uint32_t col = argb(a);
            font_draw_text(dev_in, off + 288.0f, 440.0f, "Score Difference", col, 1.0f);
            snprintf(buf, sizeof buf, w[W_SCORE_TODAY] <= 0 ? "%8d(%d)" : "%8d(+%d)",
                     s_score_shown + s_score_base, s_score_shown);
            font_draw_text_right(dev_in, off + 632.0f, 440.0f, buf, col, 1.0f);
        }

        /* Tear's rating stamp lands 60 frames after the count */
        if (s_after > 60) {
            const int e = s_after - 60;
            float sc = 3.0f - (float)e * 0.2f;
            if (sc < 1.0f)
                sc = 1.0f;
            int a = 25 * e;
            if (a > 255)
                a = 255;
            IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)s_tex_hanko.tex);
            const float half = sc * 64.0f, size = sc * 128.0f;
            quad(&s_tex_hanko, 544.0f - half, 96.0f - half, size, size,
                 0.0f, (float)(s_rating << 7), 128.0f, (float)((s_rating + 1) << 7),
                 argb(fade_alpha(a, fade)), 0);
            render_quad_flush(dev);
        }
    }
    /* s_pay: FUN_0048e49c (the choushuu.tga payment board) + the 3D shop
     * render FUN_004161c7 under its own projection —
     * PORT-DEBT(stub, FUN_0048e49c). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
}

#endif /* _WIN32 */
