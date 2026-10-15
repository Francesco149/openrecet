/*
 * scene1_tutorial_dispatch.c — see the header.  The iv1_5/iv1_6 branches of
 * FUN_0044bd0d (all.c:45664-45688).
 */
#include "scene1_tutorial_dispatch.h"
#include "story_visitor.h"

#include "audio.h"              /* the morning greetings (FUN_0049933c) */

#include "scene1_intro_dialogue.h"   /* _busy / _start_single */
#include "scene1_postload.h"         /* scene1_postload_day2_actor_replace */
#include "save_work.h"               /* save_work_dwords_at / _active_slot  */
#include "save_bank.h"               /* SAVE_BANK_FIELD_GAME_MODE           */
#include "news_daily.h"              /* news_daily_update, g_news_ticker_timer */
#include "npc_schedule.h"            /* npc_schedule_apply (FUN_00490e56)   */
#include "scene.h"                   /* g_scene_state (DAT_0438b1c0)        */
#include "scene_pause.h"             /* pause_period_end (FUN_00482059)     */
#include "scene1_day_close.h"        /* day_close_arm (DAT_0438b4e0 = 1)    */
#include "stage_palette.h"           /* g_stage_palette->mode (*DAT_068dd2f0) */

#include <stdint.h>
#include <stddef.h>   /* NULL */

/* Shop-display tutorial flags, bank-byte offsets relative to the working record
 * base DAT_044e3798 (same base + scheme the player controller's PC_SHOP_DISPLAY_*
 * bytes use: DAT_0450f3f8 → 0x2bc60, so each +1 byte is the next DAT_0450f3xx). */
#define TUT_BACKROW_OFF        0x2bc63   /* DAT_0450f3fb — placed in row 0 (cond)   */
#define TUT_BACKROW_DONE_OFF   0x2bc64   /* DAT_0450f3fc — iv1_5 fired (done)        */
#define TUT_ALLFILLED_OFF      0x2bc65   /* DAT_0450f3fd — all stands filled (cond)  */
#define TUT_ALLFILLED_DONE_OFF 0x2bc66   /* DAT_0450f3fe — iv1_6 fired (done)        */
#define TUT_ALLFILLED_DONE2_OFF 0x2bc67  /* DAT_0450f3ff — iv1_6 fired (done, pair)  */
#define TUT_IV1_7_TRIG_OFF     0x2bc68   /* DAT_0450f400 — cs-close sets it → iv1_7  */
#define TUT_IV1_7_DONE_OFF     0x2bc69   /* DAT_0450f401 — iv1_7 fired (done)        */
#define TUT_IV1_7_F406_OFF     0x2bc6e   /* DAT_0450f406 — → iv1_8 trigger (P3)      */

/* The post-first-sale story chain (all.c:45726-45813) — each entry fires when
 * its predecessor's flag pair unlocks and the dialogue runtime is idle, and
 * arms the NEXT via the trailing flag write.  f4XX → bank byte 0x2bc68 +
 * (XX − 0x400). */
#define TUT_IV1_8_TRIG_OFF     0x2bc6a   /* DAT_0450f402 — cs LEAVE sets it (f406→f402) */
#define TUT_IV1_8_DONE_OFF     0x2bc6b   /* DAT_0450f403 — iv1_8 fired (done)        */
#define TUT_F407_OFF           0x2bc6f   /* DAT_0450f407 — set with iv1_8            */
#define TUT_F408_OFF           0x2bc70   /* DAT_0450f408 — cleared at the day advance */
#define TUT_IV2_1_TRIG_OFF     0x2bc71   /* DAT_0450f409 — set with iv1_8            */
#define TUT_IV2_1_DONE_OFF     0x2bc72   /* DAT_0450f40a — iv2_1 fired (done)        */
#define TUT_IV2_2_TRIG_OFF     0x2bc73   /* DAT_0450f40b — set with iv2_1            */
#define TUT_IV2_2_DONE_OFF     0x2bc74   /* DAT_0450f40c — iv2_2 fired (done)        */
#define TUT_IV2_3_TRIG_OFF     0x2bc75   /* DAT_0450f40d — set with iv2_2            */
#define TUT_IV2_3_DONE_OFF     0x2bc76   /* DAT_0450f40e — iv2_3 fired (done)        */
#define TUT_IV2_5_TRIG_OFF     0x2bc78   /* DAT_0450f410 — set at the day advance    */
#define TUT_IV2_5_DONE_OFF     0x2bc79   /* DAT_0450f411 — iv2_5 fired (done)        */
#define TUT_IV2_6_TRIG_OFF     0x2bc7a   /* DAT_0450f412 — set with iv2_5            */
#define TUT_IV2_6_DONE_OFF     0x2bc7b   /* DAT_0450f413 — iv2_6 fired (done)        */
#define TUT_F414_OFF           0x2bc7c   /* DAT_0450f414 — set with iv2_6            */
#define TUT_F488_OFF           0x2bcf0   /* DAT_0450f488 — day-9 morning-cutscene arm */
#define TUT_F3F2_OFF           0x2bc5a   /* DAT_0450f3f2 — set at the day advance    */
#define TUT_F3F7_OFF           0x2bc5f   /* DAT_0450f3f7 — cleared at the day advance */
#define TUT_F3F9_OFF           0x2bc61   /* DAT_0450f3f9 — cleared at the day advance */

/* Day-block dwords (bank dword index; the fb7x/fb8x per-save stats block). */
#define TUT_DAY_DWORD          (0x2c3ec / 4)   /* DAT_0450fb84 — day counter (HUD shows +1) */
#define TUT_SHOPTIME_DWORD     (0x2c3f0 / 4)   /* DAT_0450fb88 — shop-time/activity counter */

/* iv2_5→iv2_6 idle-beat length: retail gates the cascade behind
 * `DAT_0438b924 < 0xbe` (all.c:45489), and b924 counts one per free-roam frame from
 * the iv2_5 arm (b924=0) — so iv2_6 fires the frame b924 reaches 0xbe.  Measured:
 * b924 0@15470 → 189@15659 (retail drive 161316Z), iv2_6 load @15659. */
#define IV2_BEAT_FRAMES 0xbe   /* 190 */

/* The transient "Recette looks up at Tear" beat (retail DAT_0438b928 / DAT_0438b924).
 * Armed at the iv2_5 fire, counted once per free-roam dispatch tick (= retail's
 * master-tick b924++, which only runs in the default/free-roam arm — during a
 * dialogue the sim takes the event arm and neither this tick nor b924 advances). */
static int g_iv2_beat_active = 0;   /* DAT_0438b928 (the beat flag; stays 1 after
                                     * the summary's arm until a game load) */
static int g_iv2_beat_ctr    = 0;   /* DAT_0438b924       */

/* Retail holds the free-roam arm / Tear's face branch while b928 == 1 && b924 < 200
 * (FUN_0048670f 0x4880xx LABEL_448, FUN_0048a833 v39); the cascade gate is 0xbe. */
#define BEAT_HOLD_FRAMES 200

/* One-shot: armed when iv2_5 arms the beat, consumed on the FIRST free-roam
 * frame of the beat (the default-arm top, before the pose derives facing) to run
 * the DAY-2 actor re-place (retail's FUN_0048526d scene-entry re-seat the port's
 * dialogue-load iv2 model skips).  See scene1_postload_day2_actor_replace. */
static int g_day2_replace_pending = 0;

/* The opening-scene selector the dispatcher writes (DAT_005c7a2c = 1). */
#define TUT_SCENE 1
#define TUT_SCENE_IV2 2
#define TUT_SUB_IV1_5 5
#define TUT_SUB_IV1_6 6
#define TUT_SUB_IV1_7 7
#define TUT_SUB_IV1_8 8

/* The other FUN_0044bd0d flags (work-record bytes; DAT_0450f3f0 → 0x2bc58). */
#define TUT_IV1_1_DONE_OFF     0x2bc58   /* DAT_0450f3f0 — the opening iv1_1 played  */
#define TUT_IV1_2_DONE_OFF     0x2bc59   /* DAT_0450f3f1 — iv1_2 played              */
#define TUT_GUILD_SEEN_OFF     0x2bc5c   /* DAT_0450f3f4 — the guild's first visit   */
#define TUT_IV1_4_DONE_OFF     0x2bc5e   /* DAT_0450f3f6                             */
#define TUT_IV0_6_DONE_OFF     0x2bc89   /* DAT_0450f421 — the first payment-day talk */
#define TUT_IV2_15_DONE_OFF    0x2bc8a   /* DAT_0450f422 — day ≥ 2 morning          */
#define TUT_IV2_14_DONE_OFF    0x2bc8b   /* DAT_0450f423 — day ≥ 2, the evening      */
#define TUT_IV1_17_DONE_OFF    0x2bc93   /* DAT_0450f42b — day 31                    */
#define TUT_IV1_18_DONE_OFF    0x2bc94   /* DAT_0450f42c — day 35                    */
#define TUT_IV4_4_TRIG_OFF     0x2bcbc   /* DAT_0450f454                             */
#define TUT_IV4_5_TRIG_OFF     0x2bcbd   /* DAT_0450f455                             */
#define TUT_IV7_1_TRIG_OFF     0x2bcbe   /* DAT_0450f456                             */
#define TUT_STORY_SCENE_OFF    0x2bcd8   /* DAT_0450f470 — FUN_00470a46's scene      */
#define TUT_IV7_1_DONE_OFF     0x2bcdd   /* DAT_0450f475                             */
#define TUT_MODE2_DONE_OFF     0x2bcec   /* DAT_0450f484 — survival mode's opener    */
#define TUT_IV9_1_ARM_OFF      0x2bced   /* DAT_0450f485                             */
#define TUT_IV9_1_DONE_OFF     0x2bcee   /* DAT_0450f486                             */
#define TUT_IV12_2_DONE_OFF    0x2bcf2   /* DAT_0450f48a                             */
#define TUT_IV12_3_DONE_OFF    0x2bcf3   /* DAT_0450f48b                             */
#define TUT_IV12_1_TRIG_OFF    0x2bcfc   /* DAT_0450f494                             */
#define TUT_NO_CASCADE_OFF     0x2bcfd   /* DAT_0450f495 — the cascade is off        */
#define TUT_IV0_17_TRIG_OFF    0x2bd10   /* DAT_0450f4a8                             */
#define TUT_IV0_17_DONE_OFF    0x2bd11   /* DAT_0450f4a9                             */
#define TUT_IV12_A_OFF         0x2bc4c   /* DAT_0450f3e4..ea, paired with …          */
#define TUT_IV12_B_OFF         0x2bd18   /* DAT_0450f4b0..b6                         */
#define TUT_MODE2_ALT_DWORD    (0x2de34 / 4)   /* dword 46989 — survival opener 19 vs 18 */

/* FUN_0044bd0d's dispatch: DAT_005c7a2c/30 = grp/idx, DAT_0438b1c8 = 2,
 * FUN_00452d07(mode).  The port's start_single owns the load bracket; the
 * worker `mode` (what runs at the dialogue's end) and the FUN_00452809
 * blackout arms are PORT-DEBT(blackout-tut-dispatch) as before. */
static int tut_idle(void) { return !scene1_intro_dialogue_busy(); }

/* FUN_0044bd0d (0x44bd0d, 2723 B, all_ida 49399): the house story / tutorial
 * cascade, one dialogue at a time while none runs (DAT_0438b1c8 == 0). */
void scene1_tutorial_dispatch_tick(void)
{
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    uint8_t *bb = (uint8_t *)bank;
    const int day    = (int32_t)bank[TUT_DAY_DWORD];
    const int period = (int32_t)bank[TUT_SHOPTIME_DWORD];

    /* survival mode: its opener (iv0_19 / iv0_18), once. */
    if ((int32_t)bank[SAVE_BANK_FIELD_GAME_MODE] == 2) {
        if (bb[TUT_MODE2_DONE_OFF] != 0 || !tut_idle())
            return;
        scene1_intro_dialogue_start_single(0, bank[TUT_MODE2_ALT_DWORD] != 0 ? 19 : 18);
        bb[TUT_MODE2_DONE_OFF] = 1;
        return;
    }
    if (bb[TUT_STORY_SCENE_OFF] != 0) {
        story_visitor_tick();                    /* FUN_00470a46 */
        return;
    }
    /* sprintf("OHA:%4d", b924) → FUN_00451874: a debug text line, no state. */
    if (bb[TUT_F488_OFF] != 0) {
        if (g_iv2_beat_ctr == 630 && tut_idle()) {
            scene1_intro_dialogue_start_single(0, 8);
            bb[TUT_F488_OFF] = 0;
        }
        return;
    }
    if (bb[TUT_IV9_1_ARM_OFF] != 0) {
        if (g_iv2_beat_ctr == 300 && bb[TUT_IV9_1_DONE_OFF] == 0 && tut_idle()) {
            scene1_intro_dialogue_start_single(9, 1);
            bb[TUT_IV9_1_DONE_OFF] = 1;
        }
        return;
    }
    if (bb[TUT_NO_CASCADE_OFF] != 0)
        return;

    /* The morning beat holds the cascade: in the shop with b928 == 1, nothing
     * fires until b924 reaches 190 (b924 is bumped by the master tick before
     * this dispatch runs, so b924 = 1..189 hold). */
    int open = 1;
    if (g_scene_state == 1 && g_stage_palette != NULL && g_stage_palette->mode == 0 &&
        g_iv2_beat_active == 1)
        open = g_iv2_beat_ctr >= 190;

    /* iv7_1: the second-week morning (b924 1, before the day's first period). */
    if (bb[TUT_IV7_1_TRIG_OFF] != 0 && bb[TUT_IV7_1_DONE_OFF] == 0 && period == 0 && day != 8 &&
        g_iv2_beat_active == 1 && g_iv2_beat_ctr == 1 && tut_idle()) {
        scene1_intro_dialogue_start_single(7, 1);
        bb[TUT_IV7_1_DONE_OFF] = 1;
    }
    if (!open)
        return;

    if (bb[TUT_IV1_1_DONE_OFF] == 0) {
        /* iv1_1, the opening: the port's prologue machine plays it
         * (scene1_intro_dialogue_arm) and sets this flag. */
        if (tut_idle()) {
            scene1_intro_dialogue_start_single(1, 1);
            bb[TUT_IV1_1_DONE_OFF] = 1;
        }
    } else if (bb[TUT_IV1_2_DONE_OFF] == 0) {
        if (tut_idle()) {
            scene1_intro_dialogue_start_single(1, 2);
            bb[TUT_IV1_2_DONE_OFF] = 1;
        }
    } else if (bb[TUT_IV12_1_TRIG_OFF] == 1 && tut_idle()) {
        scene1_intro_dialogue_start_single(12, 1);
        bb[TUT_IV12_1_TRIG_OFF] = 0;
    }

    /* LABEL_44 */
    if (bb[TUT_GUILD_SEEN_OFF] != 0 && bb[TUT_IV1_4_DONE_OFF] == 0 && tut_idle()) {
        scene1_intro_dialogue_start_single(1, 4);
        bb[TUT_IV1_4_DONE_OFF] = 1;
        bb[TUT_F3F2_OFF]       = 1;
    }
    if (pause_period_end() == day && bb[TUT_IV0_6_DONE_OFF] == 0 && tut_idle()) {
        scene1_intro_dialogue_start_single(0, 6);
        bb[TUT_IV0_6_DONE_OFF] = 1;
    }
    if (day == 31 && bb[TUT_IV1_17_DONE_OFF] == 0 && tut_idle()) {
        scene1_intro_dialogue_start_single(1, 17);
        bb[TUT_IV1_17_DONE_OFF] = 1;
    }
    if (day == 35 && bb[TUT_IV1_18_DONE_OFF] == 0 && tut_idle()) {
        scene1_intro_dialogue_start_single(1, 18);
        bb[TUT_IV1_18_DONE_OFF] = 1;
    }
    if (day >= 2) {
        if (bb[TUT_IV2_15_DONE_OFF] != 0) {
            if (bb[TUT_IV2_14_DONE_OFF] == 0 && period == 2 && tut_idle()) {
                scene1_intro_dialogue_start_single(2, 14);
                bb[TUT_IV2_14_DONE_OFF] = 1;
            }
        } else if (tut_idle()) {
            scene1_intro_dialogue_start_single(2, 15);
            bb[TUT_IV2_15_DONE_OFF] = 1;
        }
    }
    if (bb[TUT_IV0_17_TRIG_OFF] != 0 && bb[TUT_IV0_17_DONE_OFF] == 0 && tut_idle()) {
        scene1_intro_dialogue_start_single(0, 17);
        bb[TUT_IV0_17_DONE_OFF] = 1;
    }
    if (day > 35 && g_iv2_beat_active == 1 && g_iv2_beat_ctr < 200) {
        /* every one of the 7 pairs (+0x2bc4c+k, +0x2bd18+k) has a side set */
        int all = 1;
        for (int k = 0; k < 7; k++)
            if (bb[TUT_IV12_A_OFF + k] == 0 && bb[TUT_IV12_B_OFF + k] == 0)
                all = 0;
        if (all) {
            if (bb[TUT_IV12_2_DONE_OFF] != 0) {
                if (bb[TUT_IV12_3_DONE_OFF] == 0 && tut_idle()) {
                    scene1_intro_dialogue_start_single(12, 3);
                    bb[TUT_IV12_3_DONE_OFF] = 1;
                }
            } else if (tut_idle()) {
                scene1_intro_dialogue_start_single(12, 2);
                bb[TUT_IV12_2_DONE_OFF] = 1;
            }
        }
    }
    if (bb[TUT_IV4_4_TRIG_OFF] != 0) {
        if (tut_idle()) {
            scene1_intro_dialogue_start_single(4, 4);
            bb[TUT_IV4_4_TRIG_OFF] = 0;
            bb[TUT_IV4_5_TRIG_OFF] = 1;
            if ((int32_t)bank[TUT_SHOPTIME_DWORD] >= 4) {
                bank[TUT_SHOPTIME_DWORD] = 4;
                day_close_arm();                     /* DAT_0438b4e0 = 1 */
            }
        }
    } else if (bb[TUT_IV4_5_TRIG_OFF] == 0 || period != 0) {
        if (bb[TUT_BACKROW_DONE_OFF] != 0 || bb[TUT_BACKROW_OFF] != 1) {
            /* iv1_6: the "all wares displayed" dialogue. */
            if (bb[TUT_ALLFILLED_DONE_OFF] == 0 && bb[TUT_ALLFILLED_OFF] == 1 && tut_idle()) {
                scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_6);
                bb[TUT_ALLFILLED_DONE_OFF]  = 1;     /* DAT_0450f3fe = 1 */
                bb[TUT_ALLFILLED_DONE2_OFF] = 1;     /* DAT_0450f3ff = 1 */
            }
        } else if (tut_idle()) {
            /* iv1_5: the window-counter placement dialogue. */
            scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_5);
            bb[TUT_BACKROW_DONE_OFF] = 1;            /* DAT_0450f3fc = 1 */
        }
    } else if (tut_idle()) {
        scene1_intro_dialogue_start_single(4, 5);
        bb[TUT_IV4_5_TRIG_OFF] = 0;
        bb[TUT_IV7_1_TRIG_OFF] = 1;
    }
    /* iv1_7: the post-sale wrap-up ("And that is, essentially, how it goes…"),
     * armed by the cs leave (f400); → f406, the iv1_8 first-customer entry. */
    if (bb[TUT_IV1_7_DONE_OFF] == 0 && bb[TUT_IV1_7_TRIG_OFF] == 1 && tut_idle()) {
        scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_7);
        bb[TUT_IV1_7_DONE_OFF] = 1;              /* DAT_0450f401 = 1 */
        bb[TUT_IV1_7_F406_OFF] = 1;              /* DAT_0450f406 = 1 */
    }

    /* ── The post-first-sale story chain: iv1_8 → iv2_1 → iv2_2 → iv2_3 (the
     * DAY ADVANCE) → iv2_5 → iv2_6, the day-1-evening → day-2 series. ── */
    if (bb[TUT_IV1_8_DONE_OFF] == 0) {
        if (bb[TUT_IV1_8_TRIG_OFF] == 1 && tut_idle()) {   /* f402: set at the cs LEAVE */
            scene1_intro_dialogue_start_single(TUT_SCENE, TUT_SUB_IV1_8);
            bb[TUT_IV1_8_DONE_OFF] = 1;      /* DAT_0450f403 = 1 */
            bb[TUT_IV2_1_TRIG_OFF] = 1;      /* DAT_0450f409 = 1 */
            bb[TUT_F407_OFF]       = 1;      /* DAT_0450f407 = 1 */
        }
    } else if (bb[TUT_IV2_1_DONE_OFF] == 0) {
        if (bb[TUT_IV2_1_TRIG_OFF] == 1 && tut_idle()) {
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 1);
            bb[TUT_IV2_1_DONE_OFF] = 1;      /* DAT_0450f40a = 1 */
            bb[TUT_IV2_2_TRIG_OFF] = 1;      /* DAT_0450f40b = 1 */
        }
    } else if (bb[TUT_IV2_2_DONE_OFF] == 0) {
        if (bb[TUT_IV2_2_TRIG_OFF] == 1 && tut_idle()) {
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 2);
            bb[TUT_IV2_2_DONE_OFF] = 1;      /* DAT_0450f40c = 1 */
            bb[TUT_IV2_3_TRIG_OFF] = 1;      /* DAT_0450f40d = 1 */
        }
    } else if (bb[TUT_IV2_3_DONE_OFF] == 0) {
        if (bb[TUT_IV2_3_TRIG_OFF] == 1 && tut_idle()) {
            /* iv2_3 = the DAY ADVANCE: day++ + the day-block resets that reopen
             * the shop for day 2. */
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 3);
            bb[TUT_IV2_3_DONE_OFF] = 1;      /* DAT_0450f40e = 1 */
            bank[TUT_DAY_DWORD] += 1;        /* DAT_0450fb84 += 1 (day counter) */
            bank[TUT_SHOPTIME_DWORD] = 0;    /* DAT_0450fb88 = 0 */
            bb[TUT_F3F9_OFF] = 0;            /* DAT_0450f3f9 = 0 */
            bb[TUT_F408_OFF] = 0;            /* DAT_0450f408 = 0 */
            bb[TUT_F3F2_OFF] = 1;            /* DAT_0450f3f2 = 1 */
            bb[TUT_F3F7_OFF] = 0;            /* DAT_0450f3f7 = 0 */
            bb[TUT_IV1_7_TRIG_OFF] = 0;      /* DAT_0450f400 = 0 */
            /* _DAT_0438b7d4 = 0 — the smoothed shop-time follower
             * (sim-step-a L304, unported cosmetic timer). */
            bb[TUT_IV2_5_TRIG_OFF] = 1;      /* DAT_0450f410 = 1 */
        }
    } else if (bb[TUT_IV2_5_DONE_OFF] == 0) {
        if (bb[TUT_IV2_5_TRIG_OFF] == 1 && tut_idle()) {
            scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 5);
            bb[TUT_IV2_5_DONE_OFF] = 1;      /* DAT_0450f411 = 1 */
            bb[TUT_IV2_6_TRIG_OFF] = 1;      /* DAT_0450f412 = 1 */
            /* FUN_004852fb (PORT-DEBT(stub, FUN_004852fb)), then the morning
             * beat: b928 = 1, b924 = 0 (the cascade holds for 190 ticks, the
             * pose driver holds the actors). */
            scene1_tutorial_dispatch_arm_beat();
            /* the DAY-2 actor re-place, consumed on the first free-roam frame
             * of the beat (retail day2 @15470: player -0.30, companion 0.6). */
            g_day2_replace_pending = 1;
        }
    } else if (bb[TUT_IV2_6_DONE_OFF] == 0 && bb[TUT_IV2_6_TRIG_OFF] == 1 && tut_idle()) {
        scene1_intro_dialogue_start_single(TUT_SCENE_IV2, 6);
        bb[TUT_IV2_6_DONE_OFF] = 1;          /* DAT_0450f413 = 1 */
        bb[TUT_F414_OFF]       = 1;          /* DAT_0450f414 = 1 */
        /* b928 = 0: retail clears it when the DAY2 load completes; the port's
         * iv2_6 is a dialogue load, so the clear lands on its start
         * (PORT-DEBT(simplified, FUN_0049a59e)). */
        g_iv2_beat_active = 0;
    }
}

int scene1_tutorial_dispatch_iv2_beat_active(void)
{
    return g_iv2_beat_active == 1 && g_iv2_beat_ctr < BEAT_HOLD_FRAMES;
}

int scene1_tutorial_dispatch_beat_holds_this_tick(void)
{
    return g_iv2_beat_active == 1 && g_iv2_beat_ctr + 1 < BEAT_HOLD_FRAMES;
}

void scene1_tutorial_dispatch_arm_beat(void)
{
    g_iv2_beat_active = 1;
    g_iv2_beat_ctr    = 0;
}

void scene1_beat_set_counter(int v) { g_iv2_beat_ctr = v; }
void scene1_beat_set_flag(int v)    { g_iv2_beat_active = v; }
int  scene1_beat_flag(void)         { return g_iv2_beat_active; }
int  scene1_beat_counter(void)      { return g_iv2_beat_ctr; }

static int g_greet_alt;   /* DAT_074b2ee8 — which pair greets (toggled per morning) */

void scene1_beat_master_voices(void)
{
    /* FUN_0048670f asm 0x486d75-0x486e26 (before ++b924): at b924 1 the
     * world-map return beat (b928 2, armed by FUN_0049e163's home arm: SE 0x150
     * + 00re_sys10) or the morning's pair toggle; then the morning greetings:
     * Recette at 50 + Tear at 110, or Tear at 50 + Recette at 150. */
    if (g_iv2_beat_ctr == 1 && g_iv2_beat_active == 2) {
        audio_play_se_by_id(0x150);
        audio_play_se_file("bin/se/00re/system/00re_sys10.bin");
    }
    if (g_iv2_beat_ctr == 1 && g_iv2_beat_active == 1)
        g_greet_alt ^= 1;
    if (g_iv2_beat_active != 1)
        return;
    if (g_greet_alt == 0) {
        if (g_iv2_beat_ctr == 50)
            audio_play_se_file("bin/se/00re/system/00re_sys01a.bin");
        if (g_iv2_beat_ctr == 110)
            audio_play_se_file("bin/se/01ti/system/01ti_sys01a.bin");
    } else {
        if (g_iv2_beat_ctr == 50)
            audio_play_se_file("bin/se/01ti/system/01ti_sys01b.bin");
        if (g_iv2_beat_ctr == 150)
            audio_play_se_file("bin/se/00re/system/00re_sys01b.bin");
    }
}

void scene1_beat_master_count(void) { g_iv2_beat_ctr++; }

void scene1_beat_master_prologue(void)
{
    /* FUN_0048670f 0x486b7d-0x486c1b, all under SHOP_DAY >= 9 (dormant through
     * the tutorial days): at the morning (b928 == 1, b924 == 0) the day-9 f488
     * morning-cutscene arm (story mode only;
     * PORT-DEBT(stub, FUN_0044bd0d): its b924 == 0x276 consumer, scene0/sub8),
     * the daily NPC-schedule pass FUN_00490e56(0) and the daily news
     * FUN_00436623; at b924 191 the news ticker arms (b92c = 1). */
    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL || (int32_t)bank[TUT_DAY_DWORD] < 9 || g_iv2_beat_active != 1)
        return;
    uint8_t *bb = (uint8_t *)bank;
    if (g_iv2_beat_ctr == 0) {
        if ((int32_t)bank[SAVE_BANK_FIELD_GAME_MODE] != 2 &&
            (int32_t)bank[TUT_DAY_DWORD] == 9)
            bb[TUT_F488_OFF] = 1;            /* DAT_0450f488 = 1 */
        npc_schedule_apply(0);               /* push edi(0) @486bdc */
        news_daily_update((uint8_t *)bank);
    } else if (g_iv2_beat_ctr == 191) {
        g_news_ticker_timer = 1;             /* DAT_0438b92c = 1 (asm 0x486c09) */
    }
}

int scene1_tutorial_dispatch_iv2_beat_ctr(void)
{
    return g_iv2_beat_ctr;
}

void scene1_tutorial_dispatch_consume_day2_replace(void)
{
    /* Called from the INGAME default-running arm TOP (before the conversation
     * pose derives facing, before render).  The default arm runs only on
     * non-dialogue free-roam frames, so the pending flag armed during the iv2_5
     * dialogue is consumed on the FIRST post-iv2_5 free-roam frame = the beat's
     * first counting frame (retail day2 @15470).  One-shot. */
    if (g_day2_replace_pending) {
        g_day2_replace_pending = 0;
        scene1_postload_day2_actor_replace();
    }
}

void scene1_tutorial_dispatch_house_entry(void)
{
    /* b928 survives a house load (the world-map return's 2 rides into it);
     * FUN_0049a59e clears it at a title load (scene_post_fade_init). */
    g_iv2_beat_ctr         = 0;
    g_day2_replace_pending = 0;
}

void scene1_tutorial_dispatch_reset(void)
{
    g_iv2_beat_active      = 0;
    g_iv2_beat_ctr         = 0;
    g_day2_replace_pending = 0;
}
