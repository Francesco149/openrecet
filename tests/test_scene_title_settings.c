/*
 * test_scene_title_settings.c — title scene's settings submenu
 * (FUN_0049a59e state==2 producer + main→settings transition + exit).
 *
 * Builds on the existing test_scene_title_sim.c which covers the main
 * menu path. Here we exercise:
 *   - A-press on the OPTIONS menu item transitions to the settings
 *     submenu (state=2, cursor=0, menu_folding_out=0 for slide-in).
 *   - cursor_anim ramps 0→10 over 10 frames after entry; settings
 *     input is gated until then.
 *   - UP/DOWN cycle the 6-row settings cursor mod 6.
 *   - LEFT/RIGHT adjust the slider at the current row, clamped to
 *     row-specific bounds; row 0 (BGM) drives audio_fade_apply on
 *     every change.
 *   - A or B in settings exits — dirty=1→2 (save) or 0→3 (no-save).
 *   - The exit handler at next sim-top folds back to main menu with
 *     cursor on OPTIONS row.
 *   - The SE feedback hook captures call counts (we don't assert
 *     specific IDs here — those are covered by the audio_play_se
 *     unit tests).
 */
#include "t.h"

#include "scene_title.h"
#include "audio_fade.h"
#include "settings.h"

/* Convenience: fresh-boot 4-slot menu (NEW_GAME, RANKING, OPTIONS, EXIT). */
static void mk_menu(scene_title_menu_t *m) { scene_title_menu_init_fresh(m); }

/* Each test starts from a known baseline: audio_fade reset (9/9/9),
 * settings reset (default 1/0), fresh anim. */
static void setup(scene_title_anim_t *a, scene_title_menu_t *m)
{
    audio_fade_reset();
    settings_reset();
    scene_title_anim_init_fresh(a);
    mk_menu(m);
}

/* Drive the sim until the user lands in the settings submenu with
 * cursor_anim==10 (settings input active). Asserts state along the
 * way so a regression breaks here cleanly. */
static int enter_settings(scene_title_anim_t *a, scene_title_menu_t *m)
{
    /* Move cursor to OPTIONS (index 2 in the 4-slot fresh menu). */
    scene_title_sim(a, m, 0, SCENE_TITLE_INPUT_DOWN);  /* 0 → 1 */
    scene_title_sim(a, m, 0, SCENE_TITLE_INPUT_DOWN);  /* 1 → 2 */
    T_ASSERT_EQ_U(a->cursor_pos, 2u);
    T_ASSERT_EQ_I(m->items[2], SCENE_TITLE_MENU_OPTIONS);

    /* A press → select pulse starts. 15 frames later it completes and
     * the OPTIONS dispatch transitions to state 2. */
    scene_title_sim(a, m, SCENE_TITLE_INPUT_A, 0);
    T_ASSERT_EQ_U(a->select_phase, 1u);

    for (int i = 0; i < 14; i++) scene_title_sim(a, m, 0, 0);
    /* At frame 15, dispatch ran:
     *   submenu_state = 2
     *   submenu_cursor = 0
     *   menu_folding_out = 0  (start slide-in)
     * pending_action stays NONE — OPTIONS is handled inline. */
    T_ASSERT_EQ_I(a->submenu_state,    2);
    T_ASSERT_EQ_U(a->submenu_cursor,   0u);
    T_ASSERT_EQ_I(a->menu_folding_out, 0);
    T_ASSERT_EQ_I(a->pending_action,   SCENE_TITLE_ACTION_NONE);

    /* cursor_anim was 0 when the dispatch ran. The ramp runs at the
     * top of EACH sim call (including the dispatching frame), but the
     * dispatch occurred AFTER the ramp this frame. So cursor_anim is
     * still 0 immediately after frame 15. */
    T_ASSERT_EQ_U(a->cursor_anim, 0u);

    /* 10 more frames to ramp cursor_anim 0 → 10. */
    for (int i = 0; i < 10; i++) scene_title_sim(a, m, 0, 0);
    T_ASSERT_EQ_U(a->cursor_anim, 10u);
    T_ASSERT_EQ_I(a->submenu_state, 2);
    return 0;
}

int test_settings_a_on_options_transitions_to_state_2(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    int r = enter_settings(&a, &m);
    if (r) return r;
    /* Dirty starts at 0 — no slider has moved. */
    T_ASSERT_EQ_I(a.settings_dirty, 0);
    return 0;
}

int test_settings_input_gated_during_slide_in(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);

    /* Move cursor to OPTIONS + A-press; let the pulse complete. */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.submenu_state, 2);

    /* During the slide-in (cursor_anim 1..9), DOWN input is ignored
     * — the cursor must stay at row 0. */
    const uint32_t cur_before = a.submenu_cursor;
    for (int i = 0; i < 9; i++) {
        scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
        T_ASSERT_EQ_U(a.submenu_cursor, cur_before);
    }

    /* cursor_anim should now be 9; one more frame puts it at 10. */
    T_ASSERT_EQ_U(a.cursor_anim, 9u);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    /* Now cursor_anim == 10 *during* this call → settings input
     * activates this very frame, DOWN registers, cursor advances. */
    T_ASSERT_EQ_U(a.cursor_anim,    10u);
    T_ASSERT_EQ_U(a.submenu_cursor, 1u);
    return 0;
}

int test_settings_down_wraps_cursor_mod_six(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* Hold DOWN for 6 frames → cursor walks 0..5 and wraps back. */
    for (int i = 0; i < 6; i++) scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    T_ASSERT_EQ_U(a.submenu_cursor, 0u);

    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    T_ASSERT_EQ_U(a.submenu_cursor, 1u);
    return 0;
}

int test_settings_up_wraps_cursor_backwards(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* From cursor=0, UP wraps to 5. */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_UP);
    T_ASSERT_EQ_U(a.submenu_cursor, 5u);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_UP);
    T_ASSERT_EQ_U(a.submenu_cursor, 4u);
    return 0;
}

int test_settings_left_decrements_bgm(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    /* Cursor is on BGM (row 0) right after enter. Default slider
     * value (audio_fade_reset) is 9. */
    if (enter_settings(&a, &m)) return 1;
    T_ASSERT_EQ_U(a.submenu_cursor, 0u);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);

    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 8);
    T_ASSERT_EQ_I(a.settings_dirty, 1);
    return 0;
}

int test_settings_left_clamps_bgm_floor(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* Set BGM to 0 via the API (bypass the slow drive-down). */
    audio_fade_set_slider(AUDIO_FADE_CHANNEL_BGM, 0);
    /* LEFT must not underflow. settings_dirty should stay 0 (the
     * frame attempted a change but skipped → no actual change). */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 0);
    T_ASSERT_EQ_I(a.settings_dirty, 0);
    return 0;
}

int test_settings_right_clamps_bgm_ceiling(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* BGM at 9 (default after reset). RIGHT should not push past 9. */
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_RIGHT);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 9);
    T_ASSERT_EQ_I(a.settings_dirty, 0);
    return 0;
}

int test_settings_each_row_targets_correct_state(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* Move to row 1 (SE-A) via DOWN. LEFT → SE-A slider drops 9→8. */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    T_ASSERT_EQ_U(a.submenu_cursor, 1u);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A), 8);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM),  9);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 9);

    /* Row 2 (SE-B). */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_B), 8);
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_SE_A), 8);

    /* Row 3 (slider3 / non-audio, default 1, range [0,2]). */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    T_ASSERT_EQ_U(a.submenu_cursor, 3u);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_RIGHT);
    T_ASSERT_EQ_I(settings_get_slider3(), 2);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_RIGHT);
    /* Already at max — stays at 2. */
    T_ASSERT_EQ_I(settings_get_slider3(), 2);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(settings_get_slider3(), 1);

    /* Row 4 (slider4 / boolean, default 0, range [0,1]). */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    T_ASSERT_EQ_U(a.submenu_cursor, 4u);
    T_ASSERT_EQ_I(settings_get_slider4(), 0);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_RIGHT);
    T_ASSERT_EQ_I(settings_get_slider4(), 1);
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_RIGHT);
    /* At max. */
    T_ASSERT_EQ_I(settings_get_slider4(), 1);
    return 0;
}

int test_settings_clear_row_consumes_a_press_no_exit(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* Walk to row 5 (Clear all data). */
    for (int i = 0; i < 5; i++) scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);
    T_ASSERT_EQ_U(a.submenu_cursor, 5u);

    /* A press on row 5 should NOT exit. Engine opens a confirm modal;
     * we play the SE and consume the press, but state stays at 2. */
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    T_ASSERT_EQ_I(a.submenu_state, 2);
    T_ASSERT_EQ_I(a.settings_dirty, 0);
    return 0;
}

int test_settings_a_exits_clean_with_dirty_3(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* No changes → dirty stays 0. A press → dirty becomes 3
     * (exit-no-save). */
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    T_ASSERT_EQ_I(a.settings_dirty, 3);
    T_ASSERT_EQ_I(a.submenu_state,  2);   /* still in state 2 — exit
                                           * handler runs NEXT frame */

    /* Next frame: exit handler fires; state goes back to 0, cursor
     * seeks to OPTIONS row (index 2 in the fresh menu), slide-out
     * starts. */
    scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.submenu_state,    0);
    T_ASSERT_EQ_I(a.menu_folding_out, 1);
    T_ASSERT_EQ_U(a.cursor_pos,       2u);   /* OPTIONS row */
    T_ASSERT_EQ_I(a.settings_dirty,   0);
    T_ASSERT_EQ_U(a.select_phase,     0u);
    return 0;
}

int test_settings_a_exits_dirty_with_dirty_2(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* Move BGM down once to dirty the settings. */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(a.settings_dirty, 1);

    /* A press → dirty 1→2 (exit-save). */
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    T_ASSERT_EQ_I(a.settings_dirty, 2);

    /* Next frame exit handler folds back. */
    scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.submenu_state, 0);
    /* Slider state is preserved across the exit (save IO is stubbed
     * but the live audio_fade state survives). */
    T_ASSERT_EQ_I(audio_fade_get_slider(AUDIO_FADE_CHANNEL_BGM), 8);
    return 0;
}

int test_settings_b_also_exits(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* B is bit 0x20 in `pressed`. Engine treats A and B identically
     * for the exit gate (DAT_073dddd4 & 0x30). */
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_B, 0);
    T_ASSERT_EQ_I(a.settings_dirty, 3);
    return 0;
}

int test_settings_dirty_flag_cleared_on_re_entry(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;

    /* Dirty the state and exit. */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_LEFT);
    T_ASSERT_EQ_I(a.settings_dirty, 1);
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.submenu_state,  0);
    T_ASSERT_EQ_I(a.settings_dirty, 0);

    /* Cursor sits on OPTIONS; slide-out is in progress. Drive 10
     * frames to land back at main-menu cursor_anim==0. */
    for (int i = 0; i < 10; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.cursor_anim, 0u);

    /* Re-enter via A on OPTIONS — already on row 2. */
    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.submenu_state,  2);
    T_ASSERT_EQ_I(a.settings_dirty, 0);   /* clean again */
    T_ASSERT_EQ_U(a.submenu_cursor, 0u);  /* and cursor reset to row 0 */
    return 0;
}

int test_settings_options_dispatch_does_not_set_pending_action(void)
{
    /* The OPTIONS code (2) is handled inline by the sim — main.c must
     * NOT see it in pending_action. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);
    if (enter_settings(&a, &m)) return 1;
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_ACTION_NONE);
    return 0;
}

int test_settings_main_menu_other_actions_still_publish(void)
{
    /* Regression guard: with the OPTIONS branch handled inline, the
     * other menu items (EXIT, NEW_GAME) must still latch into
     * pending_action for main.c. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    setup(&a, &m);

    /* Move to EXIT (index 3) and run full pulse. */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);   /* 0 → 1 */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);   /* 1 → 2 (OPTIONS) */
    scene_title_sim(&a, &m, 0, SCENE_TITLE_INPUT_DOWN);   /* 2 → 3 (EXIT) */
    T_ASSERT_EQ_U(a.cursor_pos, 3u);

    scene_title_sim(&a, &m, SCENE_TITLE_INPUT_A, 0);
    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_MENU_EXIT);
    T_ASSERT_EQ_I(a.submenu_state,  0);   /* did NOT enter settings */
    return 0;
}
