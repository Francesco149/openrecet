/*
 * test_scene_title_sim.c — unit tests for scene_title_sim() and
 * scene_title_anim_init_fresh() (the bare path of FUN_0049a59e and
 * the FUN_0049a3a3 bootstrap-state seeder).
 *
 * Pure-C only: no D3D, no globals. The dispatcher wrapper
 * `scene_title_sim_default()` is exercised in test_sim.c.
 */

#include "t.h"
#include "scene_title.h"

#define INPUT_UP    0x0004
#define INPUT_DOWN  0x0008
#define INPUT_A     0x0010

/* Convenience: build a 4-slot fresh menu (the boot default — NEW_GAME,
 * RANKING, OPTIONS, EXIT) for tests that need a menu shape. */
static void mk_menu(scene_title_menu_t *m)
{
    scene_title_menu_init_fresh(m);
}

int test_scene_title_anim_init_fresh_seeds_folding_out(void)
{
    scene_title_anim_t a = {.frame_counter = 999, .cursor_pos = 7,
                            .cursor_anim = 4, .select_phase = 3,
                            .pulse_phase = 11, .menu_folding_out = 0};
    scene_title_anim_init_fresh(&a);
    T_ASSERT_EQ_U(a.frame_counter,   0u);
    T_ASSERT_EQ_U(a.cursor_pos,      0u);
    T_ASSERT_EQ_U(a.cursor_anim,     0u);
    T_ASSERT_EQ_U(a.select_phase,    0u);
    T_ASSERT_EQ_U(a.pulse_phase,     0u);
    T_ASSERT_EQ_I(a.menu_folding_out, 1);
    return 0;
}

int test_scene_title_sim_frame_counter_advances_on_idle(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* No input, cursor_anim already at 0 → frame_counter ticks. */
    for (int i = 0; i < 5; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.frame_counter, 5u);
    T_ASSERT_EQ_U(a.cursor_anim,   0u);    /* folding_out keeps it at 0 */
    T_ASSERT_EQ_U(a.cursor_pos,    0u);
    T_ASSERT_EQ_U(a.select_phase,  0u);
    T_ASSERT_EQ_U(a.pulse_phase,   5u);    /* tail tick every frame */
    return 0;
}

int test_scene_title_sim_pulse_phase_ticks_every_frame(void)
{
    /* pulse_phase advances even outside the `cursor_anim == 0` block.
     * Set menu_folding_out=0 so cursor_anim climbs 0..10 and we briefly
     * stop incrementing the frame counter, but pulse_phase should keep
     * going on every call. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    a.menu_folding_out = 0;     /* slide off-screen path */
    mk_menu(&m);

    for (int i = 0; i < 20; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.pulse_phase,  20u);
    T_ASSERT_EQ_U(a.cursor_anim,  10u);     /* clamped */
    /* While cursor_anim is non-zero, frame_counter doesn't tick. So
     * after 20 frames it's still 0 (folding ate them all). */
    T_ASSERT_EQ_U(a.frame_counter, 0u);
    return 0;
}

int test_scene_title_sim_cursor_anim_clamps_at_zero(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    a.cursor_anim = 3;            /* mid-fold */
    mk_menu(&m);

    /* menu_folding_out=1 → decrement; 3 ticks to reach 0, then sticks. */
    for (int i = 0; i < 10; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.cursor_anim, 0u);
    return 0;
}

int test_scene_title_sim_down_held_wraps_cursor(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);
    /* Fresh boot menu has 4 entries. */
    T_ASSERT_EQ_I(m.count, 4);

    /* Hold DOWN for `count` frames → cursor walks through every slot
     * and wraps back to 0. */
    for (int i = 0; i < m.count; i++) {
        scene_title_sim(&a, &m, 0, INPUT_DOWN);
    }
    T_ASSERT_EQ_U(a.cursor_pos, 0u);

    /* One more frame: cursor moves to 1. */
    scene_title_sim(&a, &m, 0, INPUT_DOWN);
    T_ASSERT_EQ_U(a.cursor_pos, 1u);
    return 0;
}

int test_scene_title_sim_up_held_wraps_cursor_backwards(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* From cursor=0, one UP → wraps to count-1. */
    scene_title_sim(&a, &m, 0, INPUT_UP);
    T_ASSERT_EQ_U(a.cursor_pos, (uint32_t)(m.count - 1));

    scene_title_sim(&a, &m, 0, INPUT_UP);
    T_ASSERT_EQ_U(a.cursor_pos, (uint32_t)(m.count - 2));
    return 0;
}

int test_scene_title_sim_a_pressed_starts_select_phase(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* `pressed` carries the A bit. A rising edge starts the
     * 15-frame select pulse. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    T_ASSERT_EQ_U(a.select_phase, 1u);
    /* Frame counter did NOT advance once select_phase fired — engine
     * order is: `frame_counter++; if (frame_counter < 0x1bc6) { ... A? }`
     * So frame_counter is at 1 (we did increment), select_phase at 1. */
    T_ASSERT_EQ_U(a.frame_counter, 1u);
    return 0;
}

int test_scene_title_sim_select_phase_resets_at_fifteen(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* Kick off select pulse. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    T_ASSERT_EQ_U(a.select_phase, 1u);

    /* From 1, we increment 14 more frames before it hits 15 and resets.
     * Engine triggers scene-transition at 15; bare slice resets to 0. */
    for (int i = 0; i < 14; i++) {
        scene_title_sim(&a, &m, 0, 0);
    }
    T_ASSERT_EQ_U(a.select_phase, 0u);
    return 0;
}

int test_scene_title_sim_cursor_input_ignored_while_select_pending(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* A press starts pulse. select_phase = 1. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    T_ASSERT_EQ_U(a.select_phase, 1u);
    T_ASSERT_EQ_U(a.cursor_pos,   0u);

    /* Now hold DOWN while pulse is active. cursor must NOT move
     * (engine guards cursor input behind `select_phase < 1`). */
    scene_title_sim(&a, &m, 0, INPUT_DOWN);
    T_ASSERT_EQ_U(a.cursor_pos,   0u);
    T_ASSERT_EQ_U(a.select_phase, 2u);
    return 0;
}

int test_scene_title_sim_cursor_pressed_only_no_held(void)
{
    /* If A appears in `held` but not `pressed`, the sim should not
     * fire the select pulse — A is read off `pressed` (rising edge). */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    scene_title_sim(&a, &m, 0, INPUT_A);   /* A held, not pressed */
    T_ASSERT_EQ_U(a.select_phase, 0u);
    T_ASSERT_EQ_U(a.cursor_pos,   0u);     /* A doesn't move cursor */
    return 0;
}

int test_scene_title_sim_frame_counter_clamps_at_pre_movie_window(void)
{
    /* Engine's intro-movie window: frame_counter < 0x1bc6 is the
     * "menu live" range. We assert the counter is allowed past 0x1bc6
     * (we don't actually clamp — we just stop reacting to input)
     * but cursor/select are gated when it's past 0x1bc6. The bare
     * slice keeps incrementing pulse_phase regardless. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    a.frame_counter = 0x1bc6;     /* sit at the boundary */
    /* Press A — engine ignores it when frame_counter >= 0x1bc6 */
    scene_title_sim(&a, &m, INPUT_A, 0);
    T_ASSERT_EQ_U(a.select_phase, 0u);
    T_ASSERT_EQ_U(a.frame_counter, 0x1bc7u);
    return 0;
}

int test_scene_title_sim_null_guards(void)
{
    /* Mirror the existing render's defensive NULL guard. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    scene_title_sim(NULL,  &m,    0, 0);   /* no crash */
    scene_title_sim(&a,    NULL,  0, 0);   /* no crash */
    return 0;
}
