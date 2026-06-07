/*
 * test_esc_dispatch.c — context-sensitive ESC routing (src/esc_dispatch.c).
 *
 * Mirrors the engine WndProc ESC arm (FUN_0047b2e7): only the title screen
 * with no overlay open quits; every in-game sub-mode swallows ESC; the
 * esc-disabled flag swallows in all contexts.
 */
#include "t.h"
#include "esc_dispatch.h"
#include "scene.h"

int test_esc_quit_only_at_title(void)
{
    g_esc_disabled = 0;
    g_scene_state  = SCENE_STATE_TITLE;
    T_ASSERT_EQ_I((int)esc_pressed(), (int)ESC_RESULT_QUIT);
    return 0;
}

int test_esc_swallow_in_game(void)
{
    g_esc_disabled = 0;
    g_scene_state  = SCENE_STATE_INGAME;
    T_ASSERT_EQ_I((int)esc_pressed(), (int)ESC_RESULT_SWALLOW);
    g_scene_state  = SCENE_STATE_WORLDMAP;
    T_ASSERT_EQ_I((int)esc_pressed(), (int)ESC_RESULT_SWALLOW);
    g_scene_state  = SCENE_STATE_TITLE;   /* restore */
    return 0;
}

int test_esc_disabled_swallows_everywhere(void)
{
    g_esc_disabled = 1;
    g_scene_state  = SCENE_STATE_TITLE;   /* would quit if enabled */
    T_ASSERT_EQ_I((int)esc_pressed(), (int)ESC_RESULT_SWALLOW);
    g_esc_disabled = 0;                   /* restore */
    return 0;
}
