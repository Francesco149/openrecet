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
#define INPUT_B     0x0020

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

int test_scene_title_sim_select_phase_pins_at_fifteen(void)
{
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* Kick off select pulse. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    T_ASSERT_EQ_U(a.select_phase, 1u);

    /* From 1, we increment 14 more frames. Engine pins at 0xf and
     * leaves the dispatch responsibility to the consumer (the engine
     * relies on cursor_anim slide / window destruction to break out
     * of the block on subsequent frames). */
    for (int i = 0; i < 14; i++) {
        scene_title_sim(&a, &m, 0, 0);
    }
    T_ASSERT_EQ_U(a.select_phase, 0xfu);

    /* Stays at 0xf for additional frames. */
    scene_title_sim(&a, &m, 0, 0);
    scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.select_phase, 0xfu);
    return 0;
}

int test_scene_title_sim_pending_action_default_is_none(void)
{
    scene_title_anim_t a;
    scene_title_anim_init_fresh(&a);
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_ACTION_NONE);
    return 0;
}

int test_scene_title_sim_fade_counter_set_on_new_game(void)
{
    /* Pressing A on a fresh-boot menu cycles through select_phase 1..15.
     * Fresh boot's default cursor is on NEW_GAME (item code 0). NEW
     * GAME / NEW_HAS_SAVE / CONT_HAS_SAVE route through the engine's
     * `fade_counter` (DAT_0964351c) instead of `pending_action` — at
     * select_phase=0xf the counter latches to 1 and the title freezes
     * for the engine's pre-fade countdown. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    scene_title_sim(&a, &m, INPUT_A, 0);          /* select_phase=1 */
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_ACTION_NONE);
    T_ASSERT_EQ_U(a.fade_counter,   0u);

    /* 14 more frames to reach 0xf. */
    for (int i = 0; i < 14; i++) {
        scene_title_sim(&a, &m, 0, 0);
    }
    T_ASSERT_EQ_U(a.select_phase,   0xfu);
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_ACTION_NONE);
    T_ASSERT_EQ_U(a.fade_counter,   1u);
    return 0;
}

int test_scene_title_sim_pending_action_exit_on_exit_item(void)
{
    /* Move cursor onto EXIT (item 3) then trigger select pulse. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);   /* fresh boot: NEW_GAME, OPTIONS, RANKING, EXIT (4 items) */

    /* Move to EXIT (index 3 on the fresh menu — last entry). DOWN three
     * times. Cursor anim is 0 → can move; menu->count is 4. */
    scene_title_sim(&a, &m, 0, INPUT_DOWN);  /* 0 → 1 */
    scene_title_sim(&a, &m, 0, INPUT_DOWN);  /* 1 → 2 */
    scene_title_sim(&a, &m, 0, INPUT_DOWN);  /* 2 → 3 (EXIT) */
    T_ASSERT_EQ_U(a.cursor_pos, 3u);
    T_ASSERT_EQ_I(m.items[3], SCENE_TITLE_MENU_EXIT);

    /* Press A and let the select pulse run to completion. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    for (int i = 0; i < 14; i++) {
        scene_title_sim(&a, &m, 0, 0);
    }
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_MENU_EXIT);
    return 0;
}

int test_scene_title_sim_ranking_opens_encyclopedia(void)
{
    /* Code 7 (RANKING — the all-banks 図鑑) on the fresh menu opens the
     * encyclopedia submenu in place: submenu_state → 3 + slide-in, NOT the
     * fade / pending_action routes the other codes take. Fresh menu order is
     * NEW_GAME, RANKING, OPTIONS, EXIT (index 1 = RANKING). The dispatch's
     * encyclopedia_setup(1) side effect builds an (empty, no-save) catalog —
     * the assertions are on the title state machine only. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* DOWN once → cursor on RANKING (index 1). */
    scene_title_sim(&a, &m, 0, INPUT_DOWN);
    T_ASSERT_EQ_U(a.cursor_pos, 1u);
    T_ASSERT_EQ_I(m.items[1], SCENE_TITLE_MENU_RANKING);

    /* A + 14 frames → select_phase latches 0xf → encyclopedia dispatch. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    for (int i = 0; i < 14; i++) {
        scene_title_sim(&a, &m, 0, 0);
    }
    T_ASSERT_EQ_U(a.select_phase,     0xfu);
    T_ASSERT_EQ_I(a.submenu_state,    3);   /* DAT_09643524 = 3 (encyclopedia) */
    T_ASSERT_EQ_I(a.menu_folding_out, 0);   /* DAT_09643528 = 0 → slide in     */
    /* The 図鑑 does NOT use the fade / pending_action paths. */
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_ACTION_NONE);
    T_ASSERT_EQ_U(a.fade_counter,   0u);
    return 0;
}

int test_scene_title_sim_records_opens_on_code8(void)
{
    /* Code 8 (the title's "Survival Score" row — the port's misnamed
     * HIDDEN_CHAR) opens the Records / high-score screen in place:
     * submenu_state → 4 + slide-in, submenu_cursor → 0, and NOT the fade /
     * pending_action routes the New/Continue/Survival/Exit codes take. Build a
     * menu with hidden_char set so code 8 is present (it sits right after
     * RANKING): NEW_GAME, RANKING, RECORDS(8), OPTIONS, EXIT. */
    scene_title_save_t save = { .hidden_char_unlocked = 1 };
    scene_title_menu_t m;
    scene_title_menu_init(&save, &m);

    int ridx = -1;
    for (int i = 0; i < m.count; i++)
        if (m.items[i] == SCENE_TITLE_MENU_HIDDEN_CHAR) ridx = i;
    T_ASSERT(ridx >= 0);

    scene_title_anim_t a;
    scene_title_anim_init_fresh(&a);

    /* DOWN to the RECORDS row (one move per held frame). */
    for (int i = 0; i < ridx; i++) scene_title_sim(&a, &m, 0, INPUT_DOWN);
    T_ASSERT_EQ_U(a.cursor_pos, (unsigned)ridx);
    T_ASSERT_EQ_I(m.items[a.cursor_pos], SCENE_TITLE_MENU_HIDDEN_CHAR);

    /* A + 14 frames → select_phase latches 0xf → records dispatch. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.select_phase,     0xfu);
    T_ASSERT_EQ_I(a.submenu_state,    4);   /* DAT_09643524 = 4 (records) */
    T_ASSERT_EQ_I(a.menu_folding_out, 0);   /* DAT_09643528 = 0 → slide in   */
    T_ASSERT_EQ_I(a.submenu_cursor,   0);   /* DAT_09643530 = 0              */
    /* Records uses neither the fade nor pending_action. */
    T_ASSERT_EQ_I(a.pending_action, SCENE_TITLE_ACTION_NONE);
    T_ASSERT_EQ_U(a.fade_counter,   0u);
    return 0;
}

int test_scene_title_sim_records_closes_on_ab(void)
{
    /* Once the Records screen is fully open (cursor_anim == 10), A or B folds
     * it out: select_phase → 0, menu_folding_out → 1 (submenu_state stays 4
     * until the fold-out ramp reaches cursor_anim == 0). Engine FUN_0049a59e
     * state-4 close (LAB_0049aaff: DAT_09643544 = 0; DAT_09643528 = 1). */
    scene_title_save_t save = { .hidden_char_unlocked = 1 };
    scene_title_menu_t m;
    scene_title_menu_init(&save, &m);
    int ridx = -1;
    for (int i = 0; i < m.count; i++)
        if (m.items[i] == SCENE_TITLE_MENU_HIDDEN_CHAR) ridx = i;
    T_ASSERT(ridx >= 0);

    scene_title_anim_t a;
    scene_title_anim_init_fresh(&a);
    for (int i = 0; i < ridx; i++) scene_title_sim(&a, &m, 0, INPUT_DOWN);
    scene_title_sim(&a, &m, INPUT_A, 0);
    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_I(a.submenu_state, 4);

    /* Ramp cursor_anim 0→10 (menu_folding_out == 0 climbs it). B is ignored
     * until fully open. */
    for (int i = 0; i < 12; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.cursor_anim, 10u);
    T_ASSERT_EQ_I(a.menu_folding_out, 0);

    /* B closes: fold out + reset the select countdown, submenu_state held. */
    scene_title_sim(&a, &m, INPUT_B, 0);
    T_ASSERT_EQ_I(a.menu_folding_out, 1);   /* DAT_09643528 = 1 */
    T_ASSERT_EQ_U(a.select_phase,     0u);  /* DAT_09643544 = 0 */
    T_ASSERT_EQ_I(a.submenu_state,    4);   /* still 4 until cursor_anim → 0 */
    return 0;
}

int test_scene_title_sim_fade_counter_advances_after_set(void)
{
    /* Once `fade_counter` latches, subsequent frames advance it one
     * tick per frame. The cursor_anim ramp and menu input are gated
     * out — only `pulse_phase` keeps ticking (BG scroll continues
     * during the freeze). */
    scene_title_anim_t a;
    scene_title_menu_t m;
    scene_title_anim_init_fresh(&a);
    mk_menu(&m);

    /* Run the full pulse to latch fade_counter. */
    scene_title_sim(&a, &m, INPUT_A, 0);
    for (int i = 0; i < 14; i++) {
        scene_title_sim(&a, &m, 0, 0);
    }
    T_ASSERT_EQ_U(a.fade_counter, 1u);

    /* 10 more frames — counter should be at 11, regardless of input. */
    for (int i = 0; i < 10; i++) {
        scene_title_sim(&a, &m, INPUT_DOWN, INPUT_DOWN);
    }
    T_ASSERT_EQ_U(a.fade_counter, 11u);
    T_ASSERT_EQ_U(a.cursor_pos,    0u);   /* cursor frozen */
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

/* ── Survival difficulty selector (title item code 6) ──────────────────
 *
 * Build a menu with the Survival row unlocked (FUN_0049a324 uVar1 == 3:
 * a save bank with adv-2 cleared AND an adv-8 item) and return its index.
 * Shape: CONT_HAS_SAVE, NEW_HAS_SAVE, SURVIVAL(2), CONTINUE_ANY,
 * RANKING, HIDDEN_CHAR, OPTIONS, EXIT. */
static int mk_survival_menu(scene_title_menu_t *m)
{
    scene_title_save_t save = { .has_any_adv_cleared  = 1,
                                .has_any_adv8_cleared = 1,
                                .has_any_score        = 1 };
    scene_title_menu_init(&save, m);
    for (int i = 0; i < m->count; i++)
        if (m->items[i] == SCENE_TITLE_MENU_SURVIVAL) return i;
    return -1;
}

/* Open the selector + ramp it to its at-rest state (survival_state == 8). */
static void open_survival_to_rest(scene_title_anim_t *a, scene_title_menu_t *m)
{
    int sidx = mk_survival_menu(m);
    scene_title_anim_init_fresh(a);
    for (int i = 0; i < sidx; i++) scene_title_sim(a, m, 0, INPUT_DOWN);
    scene_title_sim(a, m, INPUT_A, 0);                 /* select_phase = 1 */
    for (int i = 0; i < 14; i++) scene_title_sim(a, m, 0, 0); /* dispatch → state 1 */
    for (int i = 0; i < 10; i++) scene_title_sim(a, m, 0, 0); /* ramp 1 → 8 (pinned) */
}

int test_scene_title_sim_survival_opens_on_code6(void)
{
    /* Code 6 (SURVIVAL) does NOT open a submenu_state — it sets
     * survival_state = 1 (the selector slide-in begins) + survival_option
     * = 0, and takes neither the fade nor pending_action routes. */
    scene_title_anim_t a;
    scene_title_menu_t m;
    int sidx = mk_survival_menu(&m);
    T_ASSERT(sidx >= 0);
    T_ASSERT_EQ_I(m.items[sidx], SCENE_TITLE_MENU_SURVIVAL);

    scene_title_anim_init_fresh(&a);
    for (int i = 0; i < sidx; i++) scene_title_sim(&a, &m, 0, INPUT_DOWN);
    T_ASSERT_EQ_U(a.cursor_pos, (unsigned)sidx);

    scene_title_sim(&a, &m, INPUT_A, 0);
    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.select_phase,   0xfu);
    T_ASSERT_EQ_U(a.survival_state,  1u);  /* DAT_09643550 = 1 */
    T_ASSERT_EQ_U(a.survival_option, 0u);  /* DAT_09643558 = 0 */
    T_ASSERT_EQ_I(a.submenu_state,   0);   /* NOT a submenu */
    T_ASSERT_EQ_I(a.pending_action,  SCENE_TITLE_ACTION_NONE);
    T_ASSERT_EQ_U(a.fade_counter,    0u);
    return 0;
}

int test_scene_title_sim_survival_ramps_and_pins(void)
{
    /* After opening, survival_state ramps 1 → 8 one per frame, then pins
     * at 8 (the at-rest open selector). */
    scene_title_anim_t a;
    scene_title_menu_t m;
    open_survival_to_rest(&a, &m);
    T_ASSERT_EQ_U(a.survival_state, 8u);
    /* Holding no input keeps it pinned (the engine re-clamps to 8). */
    for (int i = 0; i < 5; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.survival_state, 8u);
    /* Still the main menu underneath (submenu_state 0, cursor_anim 0). */
    T_ASSERT_EQ_I(a.submenu_state, 0);
    T_ASSERT_EQ_U(a.cursor_anim,   0u);
    return 0;
}

int test_scene_title_sim_survival_toggle_option(void)
{
    /* At rest, a pressed-edge UP/DOWN toggles Hell <-> Normal
     * (survival_option ^= 1). */
    scene_title_anim_t a;
    scene_title_menu_t m;
    open_survival_to_rest(&a, &m);
    T_ASSERT_EQ_U(a.survival_option, 0u);
    scene_title_sim(&a, &m, INPUT_DOWN, 0);   /* pressed-edge */
    T_ASSERT_EQ_U(a.survival_option, 1u);
    scene_title_sim(&a, &m, INPUT_UP, 0);
    T_ASSERT_EQ_U(a.survival_option, 0u);
    /* Toggling does not leave the selector (still state 8). */
    T_ASSERT_EQ_U(a.survival_state, 8u);
    return 0;
}

int test_scene_title_sim_survival_b_cancels(void)
{
    /* B starts the slide-out (survival_slideout = 1); survival_state then
     * counts 8 → 0 and the selector returns to the resting main menu
     * (menu_folding_out = 1, select_phase = 0). */
    scene_title_anim_t a;
    scene_title_menu_t m;
    open_survival_to_rest(&a, &m);

    scene_title_sim(&a, &m, INPUT_B, 0);
    T_ASSERT_EQ_U(a.survival_slideout, 1u);
    T_ASSERT_EQ_U(a.survival_state,    8u);   /* B frame doesn't decrement */

    for (int i = 0; i < 10; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.survival_state,    0u);
    T_ASSERT_EQ_U(a.survival_slideout, 0u);
    T_ASSERT_EQ_I(a.menu_folding_out,  1);
    T_ASSERT_EQ_U(a.select_phase,      0u);
    return 0;
}

int test_scene_title_sim_survival_a_confirms_opens_picker(void)
{
    /* A starts the closing animation (survival_anim 1 → 0xf); at 0xf it
     * hands off to the save picker (submenu_state = 1, slide in). The
     * survival bank-FILTER + game launch stay PORT-DEBT(survival-picker). */
    scene_title_anim_t a;
    scene_title_menu_t m;
    open_survival_to_rest(&a, &m);

    scene_title_sim(&a, &m, INPUT_A, 0);
    T_ASSERT_EQ_U(a.survival_anim, 1u);

    for (int i = 0; i < 14; i++) scene_title_sim(&a, &m, 0, 0);
    T_ASSERT_EQ_U(a.survival_anim,    0u);  /* reset after handoff */
    T_ASSERT_EQ_I(a.submenu_state,    1);   /* DAT_09643524 = 1 (picker) */
    T_ASSERT_EQ_I(a.menu_folding_out, 0);   /* slide in */
    return 0;
}
