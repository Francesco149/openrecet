/*
 * test_scene1_day_close.c — the shop day close (scene1_day_close.c), the
 * actor-say driver (scene1_actor_say.c), the speech bubble (speech_bubble.c)
 * and the day-end summary count-up (scene_day_end.c).
 * Expected timings = rhost retail, buy-counter-raise-accept: b4e0 1 @f7156,
 * variant 1, msg 302 @b4e0 10 (age 2 the same frame), 303 @90, the summary
 * opened at b4e0 160 (docs/findings/day-close-RE.md).
 */

#include "t.h"

#include <string.h>

#include "rng.h"
#include "save_bank.h"
#include "save_work.h"
#include "scene1_actor_say.h"
#include "scene1_day_close.h"
#include "scene1_player_ctrl.h"
#include "scene_day_end.h"
#include "scene_pause.h"
#include "speech_bubble.h"

int test_speech_bubble_splits_br_lines(void)
{
    speech_bubble_reset();
    speech_bubble_open(412.0f, 112.0f, "Go rest, then.<BR>Tomorrow will<BR>be busy.");
    T_ASSERT_EQ_I(speech_bubble_line_count(), 3);
    T_ASSERT(strcmp(speech_bubble_line(0), "Go rest, then.") == 0);
    T_ASSERT(strcmp(speech_bubble_line(1), "Tomorrow will") == 0);
    T_ASSERT(strcmp(speech_bubble_line(2), "be busy.") == 0);
    T_ASSERT_EQ_I(speech_bubble_age(), 1);
    T_ASSERT_EQ_I(speech_bubble_pop(), 1);
    return 0;
}

int test_speech_bubble_close_runs_ten_ticks(void)
{
    speech_bubble_reset();
    speech_bubble_open(0.0f, 0.0f, "G'night!");
    for (int i = 0; i < 5; i++)
        speech_bubble_tick();
    T_ASSERT_EQ_I(speech_bubble_age(), 6);
    speech_bubble_close(0);
    for (int i = 0; i < 8; i++)
        speech_bubble_tick();
    T_ASSERT_EQ_I(speech_bubble_close_ctr(), 9);
    T_ASSERT(speech_bubble_age() != 0);
    speech_bubble_tick();                            /* the 10th: hidden */
    T_ASSERT_EQ_I(speech_bubble_close_ctr(), 0);
    T_ASSERT_EQ_I(speech_bubble_age(), 0);
    T_ASSERT_EQ_I(speech_bubble_pop(), 0);
    return 0;
}

int test_actor_say_day_close_lines(void)
{
    actor_say_set(ACTOR_SAY_DAY_CLOSE_1R, 0, 0, 1, 100);
    T_ASSERT(strcmp(actor_say_text(), "Thanks for all your<BR>help today, Tear!") == 0);
    actor_say_set(ACTOR_SAY_DAY_CLOSE_2T, 0, 2, 0, 100);
    T_ASSERT(strcmp(actor_say_text(), "Good night.") == 0);
    actor_say_set(999, 0, 0, 0, 100);
    T_ASSERT(strcmp(actor_say_text(), "") == 0);
    actor_say_stage_reset();
    return 0;
}

int test_actor_say_ages_and_closes_at_duration(void)
{
    speech_bubble_reset();
    actor_say_set(ACTOR_SAY_DAY_CLOSE_0R, 0, 0, 1, 5);
    actor_say_tick();                                /* opens the bubble, age 2 */
    T_ASSERT_EQ_I(actor_say_age(), 2);
    T_ASSERT_EQ_I(speech_bubble_age(), 1);
    T_ASSERT(strcmp(speech_bubble_line(0), "I'm tiiiired...") == 0);
    T_ASSERT_EQ_I(speech_bubble_mirror(), 1);        /* side 1: the mirrored tail */
    for (int i = 0; i < 3; i++)
        actor_say_tick();
    T_ASSERT_EQ_I(actor_say_age(), 0);               /* 5 reached: closed */
    T_ASSERT_EQ_I(speech_bubble_close_ctr(), 1);
    return 0;
}

/* The exchange from the arm: variant 0 → 1 at b4e0 2; Recette's line at 10,
 * the close at 79, Tear's reply at 90, the summary + b4e0 = 0 at 160. */
int test_day_close_exchange_timeline(void)
{
    save_work_set_active_slot(0);
    uint32_t *bank = save_work_dwords_at(0);
    T_ASSERT(bank != NULL);
    memset(bank, 0, SAVE_BANK_STRIDE_BYTES);
    day_close_reset();
    actor_say_stage_reset();
    pause_sm_reset();

    day_close_begin();                               /* b4e0 = 1 + FUN_0048526d */
    T_ASSERT_EQ_I(day_close_b4e0(), 1);
    T_ASSERT_EQ_I(player_ctrl_cc08(), 1);
    T_ASSERT_EQ_I(player_ctrl_db048(), 15);
    T_ASSERT_EQ_I(player_ctrl_actor_record(0)[5], 6);   /* the tired pose */

    int saw_302 = 0, saw_303 = 0;
    for (int f = 2; f <= 160; f++) {
        T_ASSERT_EQ_I(day_close_machine_tick(), 1);
        if (f < 160)
            T_ASSERT_EQ_I(day_close_b4e0(), f);
        if (f == 2)
            T_ASSERT_EQ_I(day_close_variant(), 1);
        if (f == 10) {
            T_ASSERT_EQ_I(actor_say_msg(), ACTOR_SAY_DAY_CLOSE_1R);
            saw_302 = 1;
        }
        if (f == 90) {
            T_ASSERT_EQ_I(actor_say_msg(), ACTOR_SAY_DAY_CLOSE_1T);
            saw_303 = 1;
        }
    }
    T_ASSERT(saw_302 && saw_303);
    T_ASSERT_EQ_I(day_close_b4e0(), 0);
    T_ASSERT_EQ_I(g_pause_action, 1);                /* FUN_004536b9 → FUN_00453384(1) */
    T_ASSERT_EQ_I(day_close_machine_tick(), 0);
    day_close_reset();
    pause_sm_reset();
    return 0;
}

/* FUN_0048da71 targets + the FUN_0048dbfb count-up: profit counts to
 * gold − day-start gold from frame 46, then the score difference, then the
 * after-count starts. */
int test_day_end_counts_up_to_the_targets(void)
{
    save_work_set_active_slot(0);
    int32_t *w = (int32_t *)save_work_dwords_at(0);
    T_ASSERT(w != NULL);
    memset(w, 0, SAVE_BANK_STRIDE_BYTES);
    w[3] = 5200;                                     /* gold */
    w[45302] = 3000;                                 /* day-start gold */
    w[45304] = 340;                                  /* today's score */
    w[45303] = 1000;                                 /* the score before today */
    w[0xb0fb] = 5;                                   /* day */
    rng_seed(12345);
    day_end_init();
    T_ASSERT_EQ_I(day_end_profit(), 2200);
    T_ASSERT_EQ_I(w[45303], 1340);                   /* the total takes today's score */
    int prev = 0, f;
    for (f = 0; f < 600 && day_end_after() == 0; f++) {
        day_end_tick(0, 0);
        T_ASSERT(day_end_profit_shown() >= prev);
        T_ASSERT(day_end_profit_shown() <= 2200);
        prev = day_end_profit_shown();
        if (f < 45)
            T_ASSERT_EQ_I(day_end_profit_shown(), 0);
    }
    T_ASSERT_EQ_I(day_end_profit_shown(), 2200);
    T_ASSERT_EQ_I(day_end_score_shown(), 340);
    T_ASSERT_EQ_I(day_end_after(), 1);
    return 0;
}
