/*
 * test_scene.c — tests for the scene-state machine (src/scene.{c,h}).
 *
 * Coverage:
 *   - scene_state_set_title() resets to (TITLE, sub=0)
 *   - scene_post_fade_init() collapses LOADING→INGAME in one call
 *     (the engine writes both consecutively in one sim tick) and kicks
 *     the phase-(-1) fade-IN so the alpha quad reveals the destination
 *     scene.
 */

#include "t.h"
#include "fade.h"
#include "save_bank.h"
#include "save_work.h"
#include "scene.h"
#include "scene_title.h"

int test_scene_set_title_writes_title_and_zero_substate(void)
{
    g_scene_state    = 99;
    g_scene_substate = 7;
    scene_state_set_title();
    T_ASSERT_EQ_I(g_scene_state,    (int)SCENE_STATE_TITLE);
    T_ASSERT_EQ_I(g_scene_substate, 0);
    return 0;
}

int test_scene_post_fade_init_lands_in_ingame(void)
{
    g_scene_state    = SCENE_STATE_TITLE;
    g_scene_substate = 0;
    fade_reset();

    scene_post_fade_init();

    T_ASSERT_EQ_I(g_scene_state,    (int)SCENE_STATE_INGAME);
    T_ASSERT_EQ_I(g_scene_substate, 0);
    return 0;
}

int test_scene_post_fade_init_clears_substate(void)
{
    g_scene_state    = SCENE_STATE_TITLE;
    g_scene_substate = 5;     /* stale value from prior scene */
    fade_reset();

    scene_post_fade_init();

    T_ASSERT_EQ_I(g_scene_substate, 0);
    return 0;
}

int test_scene_post_fade_new_game_resets_and_seeds_working(void)
{
    /* NEW GAME branch (continue_mode == 0): bank 0 is reset to fresh
     * (gold = 1000) AND working slot 0 is seeded from it. */
    save_bank_arena_clear();
    save_work_clear();
    g_scene_title_anim.continue_mode = 0;
    /* Stale pre-state that the reset must clobber. */
    save_bank_dwords_at(0)[SAVE_BANK_FIELD_GOLD] = 5000;

    g_scene_state = SCENE_STATE_TITLE;
    fade_reset();
    scene_post_fade_init();

    T_ASSERT_EQ_U(save_bank_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 1000u);
    T_ASSERT_EQ_U(save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 1000u);
    return 0;
}

int test_scene_post_fade_continue_preserves_loaded_save(void)
{
    /* CONTINUE branch (continue_mode == 1): the picker already loaded a
     * save into working slot 0; post-fade must NOT reset the save bank
     * nor clobber the working slot. */
    save_bank_arena_clear();
    save_work_clear();
    /* Simulate a played save in bank 7 + the picker having loaded it. */
    save_bank_dwords_at(0)[SAVE_BANK_FIELD_GOLD] = 5000;   /* must survive */
    save_work_set_active_slot(0);
    save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD] = 8888;   /* loaded state */
    g_scene_title_anim.continue_mode = 1;

    g_scene_state = SCENE_STATE_TITLE;
    fade_reset();
    scene_post_fade_init();

    T_ASSERT_EQ_I(g_scene_state, (int)SCENE_STATE_INGAME);
    /* Save bank NOT reset (still 5000, not 1000). */
    T_ASSERT_EQ_U(save_bank_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 5000u);
    /* Working slot preserved (loaded value intact). */
    T_ASSERT_EQ_U(save_work_dwords_at(0)[SAVE_BANK_FIELD_GOLD], 8888u);

    g_scene_title_anim.continue_mode = 0;   /* restore for later tests */
    return 0;
}

int test_scene_post_fade_init_starts_fade_in(void)
{
    /* Pre-state: title fade-OUT just completed — counter pinned at
     * duration+1, phase still 1. scene_post_fade_init should flip the
     * fade to phase -1 with counter=0 so the alpha quad ramps OUT over
     * the next 17 ticks (engine FUN_0049a59e L235). */
    fade_phase1_start(0, 0x11);
    /* simulate fade reaching done */
    g_fade_counter = g_fade_duration + 1;
    T_ASSERT_EQ_I(g_fade_phase, 1);

    scene_post_fade_init();

    T_ASSERT_EQ_I(g_fade_phase,    -1);
    T_ASSERT_EQ_I(g_fade_mode,      0);
    T_ASSERT_EQ_I(g_fade_duration,  0x11);
    T_ASSERT_EQ_I(g_fade_counter,   0);
    return 0;
}
