/*
 * scene_town_event.c — engine modes 13..16, the town-visit scenes.
 * See scene_town_event.h (all_ida 61144-61400, FUN_00473972..FUN_004739fb).
 */
#include "scene_town_event.h"

#include <stdint.h>
#include <stddef.h>   /* NULL */

#include "event_probe.h"            /* FUN_0045de68                               */
#include "fade.h"                   /* FUN_004526f5 / 0045281c / 004528b3         */
#include "save_bank.h"              /* SAVE_BANK_FIELD_CLOCK_TARGET               */
#include "save_work.h"              /* save_work_dwords_at / _active_slot         */
#include "scene.h"                  /* g_scene_state (DAT_0438b1c0)               */
#include "scene1_intro_dialogue.h"  /* FUN_0044ba2c                               */
#include "sim.h"                    /* g_sim_buttons (DAT_073dddd4)               */
#include "worker_load.h"            /* worker_load_spawn / _set_cb (FUN_00452cde) */

#define W_PERIOD_FLAG 0xb382u       /* DAT_045105a0 (work +0x2ce08) */

static int32_t s_leaving;           /* DAT_06a5d400 */
static int32_t s_ticks;             /* DAT_06a5d404 */

void scene_town_event_reset(void)
{
    s_leaving = 0;
    s_ticks   = 0;
}

static void town_event_release(void);

/* The shared leave arm: once the fade-out is done, the trip costs a period,
 * mode 8, FUN_00473969 (the group-7 release), the fade-in, the load. */
static void town_event_leave(uint32_t *w)
{
    if (!fade_is_done())                         /* FUN_004528b3 */
        return;
    if (w != NULL)
        w[W_PERIOD_FLAG] = 1;
    g_scene_state = 8;
    town_event_release();                        /* FUN_00473969 */
    fade_phase_out_start(0, 0x11);               /* FUN_0045281c(0,17) */
    worker_load_spawn();                         /* FUN_00452cde */
}

/* FUN_0045e3dc / 053 / 1a5: tick 0 starts the place's script, tick 1 fades. */
static void town_event_script(uint32_t *w, int loc, int idle_idx)
{
    if (s_ticks == 0) {
        int grp, idx;
        if (w != NULL && event_probe(w, loc, &grp, &idx, 1))   /* FUN_0045de68 */
            scene1_intro_dialogue_start_single(grp, idx);   /* FUN_0044ba2c(grp,idx,0) */
        else
            scene1_intro_dialogue_start_single(16, idle_idx); /* FUN_0044ba2c(16,n,0) */
    }
    if (s_ticks == 1) {
        s_leaving = 1;
        fade_phase1_start(0, 0x11);              /* FUN_004526f5(0,17) */
    }
}

void scene_town_event_sim(int mode)
{
    uint32_t *w = save_work_dwords_at(save_work_active_slot());
    if (s_leaving != 0) {
        town_event_leave(w);
    } else {
        switch (mode) {
        case 13:                                 /* 教会 — FUN_0045e3dc */
            town_event_script(w, 5, 5);
            break;
        case 14: {                               /* 広場 — FUN_0045e053 */
            const int32_t period = w != NULL ? (int32_t)w[SAVE_BANK_FIELD_CLOCK_TARGET] : 0;
            town_event_script(w, 2, period < 2 ? 1 : period == 2 ? 2 : 3);
            break;
        }
        case 15:                                 /* 酒場 — FUN_0045e1a5 */
            town_event_script(w, 4, 4);
            break;
        case 16:                                 /* FUN_0045e2dd: Z leaves */
            if (g_sim_buttons[0].pressed & 0x10) {   /* DAT_073dddd4 */
                s_leaving = 1;
                fade_phase1_start(0, 0x11);
            }
            break;
        default:
            return;
        }
    }
    s_ticks++;
}

int scene_town_event_dbg(uint32_t va, int32_t *out)
{
    switch (va) {
    case 0x6a5d400u: *out = s_leaving; return 1;
    case 0x6a5d404u: *out = s_ticks;   return 1;
    default:         return 0;
    }
}

#ifndef _WIN32
static void town_event_release(void) {}
void scene_town_event_init(struct IDirect3DDevice8 *dev) { (void)dev; }
void scene_town_event_render(struct IDirect3DDevice8 *dev) { (void)dev; }
#else

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "render_quad.h"   /* render_quad_state_setup/bind/add/flush */
#include "sprite.h"        /* sprite_t, sprite_load */

/* DAT_073da000 / 010 / 020 — the texture group 7 slots these scenes load. */
static sprite_t          s_tex[3];
static IDirect3DDevice8 *s_dev = NULL;

static void town_event_release(void)
{
    for (int i = 0; i < 3; i++)
        sprite_destroy(&s_tex[i]);
}

static void town_event_load(const char *a, const char *b, const char *c)
{
    town_event_release();
    if (s_dev == NULL)
        return;
    const char *p[3] = { a, b, c };
    for (int i = 0; i < 3; i++)
        if (p[i] != NULL)
            sprite_load(s_dev, p[i], 0x400, 0x200, &s_tex[i]);   /* FUN_0047193c(7, …) */
}

static void load_13(void) { town_event_load("bmp/ivent/bg_kyoukai.bmp", NULL, NULL); }   /* FUN_00473972 */
static void load_14(void)                                                                 /* FUN_00473991 */
{
    town_event_load("bmp/ivent/bg_hiro1.bmp", "bmp/ivent/bg_hiro2.bmp", "bmp/ivent/bg_hiro3.bmp");
}
static void load_15(void) { town_event_load("bmp/ivent/bg_sakaba.bmp", NULL, NULL); }    /* FUN_004739fb */
static void load_16(void) { town_event_load("bmp/ivent/bg_hiro1.bmp", NULL, NULL); }     /* FUN_004739dc */

void scene_town_event_init(struct IDirect3DDevice8 *dev)
{
    s_dev = (IDirect3DDevice8 *)dev;
    worker_load_set_cb(13, load_13);
    worker_load_set_cb(14, load_14);
    worker_load_set_cb(15, load_15);
    worker_load_set_cb(16, load_16);
}

/* FUN_0045e48b / 11c / 254 / 353 (identical): FUN_0049b425, DAT_073da000 bound,
 * one 640x480 quad in diffuse 0xff000000 (MODULATE → black), flush. */
void scene_town_event_render(struct IDirect3DDevice8 *dev)
{
    IDirect3DDevice8 *d = (IDirect3DDevice8 *)dev;
    render_quad_state_setup(d);                  /* FUN_0049b425 */
    if (s_tex[0].tex == NULL)
        return;
    render_quad_bind(d, &s_tex[0]);
    const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
    const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
    render_quad_add(dst, src, s_tex[0].width, s_tex[0].height, 0xff000000u);   /* FUN_00404efc */
    render_quad_flush(d);                        /* FUN_00405354 */
}

#endif
