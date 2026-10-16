/*
 * scene1_day_close.c — see scene1_day_close.h.
 */
#include "scene1_day_close.h"
#include "story_visitor.h"

#include "audio.h"                /* audio_play_se_file (FUN_0049933c) */
#include "news_daily.h"           /* news_daily_update, g_news_ticker_timer */
#include "rng.h"                  /* rng_next15 (the shared LCG) */
#include "save_bank.h"
#include "save_work.h"
#include "scene.h"                /* g_scene_state (DAT_0438b1c0) */
#include "scene1_actor_say.h"
#include "scene1_camera.h"         /* DAT_0438cc68 */
#include "scene1_chr_sprite.h"    /* CHR_ACTOR_* */
#include "scene1_particles_tick.h"/* g_scene1_actor_pos */
#include "scene1_player_ctrl.h"
#include "scene1_postload.h"      /* the in-scene FUN_00436f97, FUN_004851e2 */
#include "scene1_tutorial_dispatch.h" /* DAT_0438b924 */
#include "scene_pause.h"          /* pause_dispatch (FUN_00453384) */
#include "speech_bubble.h"

/* work-record dwords */
#define WORK_SHOPTIME     (0x2c3f0 / 4)   /* DAT_0450fb88 — periods passed today */
#define WORK_DAY          (0x2c3ec / 4)   /* DAT_0450fb84 */
#define WORK_SEAT_AT_DOOR (0x2cdf4 / 4)   /* DAT_0451058c — FUN_00436f97's seat pick */
#define WORK_PERIOD_FLAG  (0x2ce08 / 4)   /* DAT_045105a0 — a period passed away from the shop */
#define WORK_PERIOD_EVENTS_OFF 0x2bca8    /* 4 bytes, FUN_0045e028 */

static int s_b4e0;      /* DAT_0438b4e0 */
static int s_variant;   /* DAT_074b2eec (BSS; +1 mod 3 per close) */

int day_close_b4e0(void)    { return s_b4e0; }
int day_close_variant(void) { return s_variant; }

void day_close_reset(void)
{
    s_b4e0 = 0;
    s_variant = 0;
}

void shop_period_events_clear(uint8_t *bank)
{
    if (bank == NULL)
        return;
    for (int i = 0; i < 4; i++)
        bank[WORK_PERIOD_EVENTS_OFF + i] = 0;
}

void scene1_house_relayout(void)
{
    scene1_postload_stage_entry_init();          /* FUN_00436f97 */
    scene1_postload_place_actors(1);             /* FUN_004851e2(1): the front door */

    /* Recette faces Tear: left (octant 2, −π/2) when Tear is on her left. */
    int32_t *p0 = player_ctrl_actor_record_mut(0);
    if (g_scene1_actor_pos[2][0] <= g_scene1_actor_pos[0][0]) {
        player_ctrl_set_facing_angle(-1.5707964f);
        p0[CHR_ACTOR_FACING] = 2;
    } else {
        player_ctrl_set_facing_angle(1.5707964f);
        p0[CHR_ACTOR_FACING] = 6;
    }
    /* the tired pose (state/anim 6), restarted unless already held. */
    if (p0[CHR_ACTOR_STATE] != 6) {
        p0[CHR_ACTOR_FRAME]   = 0;
        p0[CHR_ACTOR_COUNTER] = 0;
        p0[CHR_ACTOR_STATE]   = 6;
        p0[CHR_ACTOR_ANIM]    = 6;
        p0[CHR_ACTOR_TIMER]   = 0;               /* 0.0f */
    }
    player_ctrl_cc08_enter_freeroam();           /* DAT_0438cc08 = 1 */
    player_ctrl_set_db048(15);                   /* DAT_056db048 = 15 */
    scene1_beat_set_counter(300);                /* DAT_0438b924 = 300: past any b928 hold */
}

void day_close_clear(void) { s_b4e0 = 0; }

void scene1_house_morning_reseat(void)
{
    uint8_t *wb = (uint8_t *)save_work_dwords_at(save_work_active_slot());
    const int32_t *w = (const int32_t *)wb;
    actor_say_stage_reset_age();                 /* DAT_0438b764 = 0 */
    player_ctrl_tear_reach_reset();              /* DAT_0438be80 = 1, DAT_0438be7c = 0 */
    player_ctrl_emote_reset();                   /* DAT_056db000/004 = 0 */
    scene1_postload_place_actors(0);             /* FUN_004851e2(0): the counter spot */
    int32_t *p0 = player_ctrl_actor_record_mut(0);
    if (g_scene1_actor_pos[2][0] <= g_scene1_actor_pos[0][0]) {
        player_ctrl_set_facing_angle(-1.5707964f);
        p0[CHR_ACTOR_FACING] = 2;
    } else {
        player_ctrl_set_facing_angle(1.5707964f);
        p0[CHR_ACTOR_FACING] = 6;
    }
    if (p0[CHR_ACTOR_STATE] != 6) {
        p0[CHR_ACTOR_STATE]   = 6;
        p0[CHR_ACTOR_FRAME]   = 0;
        p0[CHR_ACTOR_TIMER]   = 0;
        p0[CHR_ACTOR_COUNTER] = 0;
        p0[CHR_ACTOR_ANIM]    = 6;
    }
    player_ctrl_set_db048(0);
    g_scene1_camera_stage_class = 0;             /* DAT_0438b4e8 = 0 (0x4853a7) */
    if (wb == NULL)
        return;
    /* Day 8 (shop-time 0, not survival mode, work byte +0x2bce8 clear): the
     * visitor's scene FUN_004706ea, +0x2bcd8 = 1, DAT_0438cc68 = 1. */
    if (w[0x2c3ec / 4] == 8 && w[0x2c3f0 / 4] == 0 && w[0x2dd64 / 4] != 2 && wb[0x2bce8] == 0) {
        story_visitor_start();                   /* FUN_004706ea */
        wb[STORY_SCENE_OFF] = 1;
        scene1_camera_snap_next();               /* DAT_0438cc68 = 1 */
    }
    /* Day 15 (shop-time 0, not survival mode, work byte +0x2bcee clear): the
     * day-15 scene (+0x2bced, FUN_0048670f's block), DAT_0438cc68 = 1. */
    if (w[0x2c3ec / 4] == 15 && w[0x2c3f0 / 4] == 0 && w[0x2dd64 / 4] != 2 && wb[0x2bcee] == 0) {
        wb[0x2bced] = 1;
        scene1_camera_snap_next();               /* DAT_0438cc68 = 1 */
    }
    wb[0x2bd07] = 0;
}

void day_close_arm(void) { s_b4e0 = 1; }

void day_close_begin(void)
{
    s_b4e0 = 1;
    scene1_house_relayout();
}

void day_close_relayout_tick(void)
{
    if (s_b4e0 == 1)
        scene1_house_relayout();
}

/* The exchange lines (asm 0x486e6e-0x48702e): [variant] = Recette's line at
 * b4e0 10, the close at `shut`, Tear's reply at `reply`, with their voices. */
static const struct {
    int r_msg, t_msg, shut, reply;
    const char *r_se, *t_se;
} k_lines[3] = {
    { ACTOR_SAY_DAY_CLOSE_0R, ACTOR_SAY_DAY_CLOSE_0T, 79, 90,
      "bin/se/00re/system/00re_sys02a.bin", "bin/se/01ti/system/01ti_sys02.bin" },
    { ACTOR_SAY_DAY_CLOSE_1R, ACTOR_SAY_DAY_CLOSE_1T, 79, 90,
      "bin/se/00re/system/00re_sys02b.bin", "bin/se/01ti/system/01ti_sys02.bin" },
    { ACTOR_SAY_DAY_CLOSE_2R, ACTOR_SAY_DAY_CLOSE_2T, 69, 80,
      "bin/se/00re/system/00re_sys02c.bin", "bin/se/01ti/system/01ti_sys03.bin" },
};

int day_close_machine_tick(void)
{
    if (s_b4e0 <= 0)
        return 0;
    /* The count holds while DAT_0438b928 == 2 && DAT_0438b924 < 200 (the first
     * 200 ticks after a world-map return, FUN_0049e163's home arm): v6 keeps
     * the un-incremented b4e0, so the v == N beats below repeat while held. */
    int v = (scene1_beat_flag() != 2 || scene1_beat_counter() >= 200) ? ++s_b4e0 : s_b4e0;
    if (v == 2)
        s_variant = (s_variant + 1) % 3;
    const int k = s_variant;
    if (v == 10) {
        actor_say_set(k_lines[k].r_msg, 0, 0, 1, 100);   /* Recette, bubble to her left */
        audio_play_se_file(k_lines[k].r_se);
    }
    if (s_b4e0 == k_lines[k].shut)
        speech_bubble_close(0);
    if (s_b4e0 == k_lines[k].reply) {
        actor_say_set(k_lines[k].t_msg, 0, 2, 0, 100);   /* Tear, to her right */
        audio_play_se_file(k_lines[k].t_se);
    }
    if (s_b4e0 == 160) {
        pause_dispatch(1);                               /* FUN_004536b9: the day-end summary */
        s_b4e0 = 0;
        player_ctrl_cc08_enter_freeroam();
        speech_bubble_close(0);
    }
    return 1;
}

void shop_period_return_tick(int cc08, int cc04)
{
    uint32_t *w = save_work_dwords_at(save_work_active_slot());
    if (w == NULL || cc08 != 1 || cc04 != 0 || w[WORK_PERIOD_FLAG] != 1)
        return;
    w[WORK_PERIOD_FLAG] = 0;
    w[WORK_SHOPTIME]++;
    /* the rand is drawn first, every time (asm 0x486cae). */
    if (rng_next15() % 5u == 0 && (int32_t)w[WORK_DAY] >= 9) {
        news_daily_update((uint8_t *)w);
        g_news_ticker_timer = 1;
    }
    shop_period_events_clear((uint8_t *)w);              /* FUN_0045e028 */
    if ((int32_t)w[WORK_SHOPTIME] >= 4) {
        w[WORK_SHOPTIME] = 4;
        g_scene_state = 1;                               /* DAT_0438b1c0 = 1 */
        w[WORK_SEAT_AT_DOOR] = 0;
        day_close_begin();
    }
}
