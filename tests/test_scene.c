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
#include "scene.h"

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
