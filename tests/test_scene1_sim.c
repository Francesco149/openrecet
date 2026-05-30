/*
 * test_scene1_sim.c — Cs1 wiring tests for scene1_ingame_tick + the
 * SCENE_STATE_INGAME arm of sim_step_a.
 *
 * The behavior these tests pin down:
 *
 *   1. `scene1_ingame_tick()` calls `scene1_particles_tick()`.
 *   2. `sim_step_a()` with `g_scene_state == SCENE_STATE_INGAME` runs
 *      the integrator.
 *   3. `sim_step_a()` with `g_scene_state == SCENE_STATE_TITLE` does
 *      NOT run the integrator (smoke check the switch doesn't fall
 *      through into INGAME by accident).
 *
 * Behavior is observed via a hand-injected type-0x92 particle (same
 * helper as `--show-pass-f-test`): after one tick the AGE field bumps
 * from 0 → 1 and ROT_X bumps by π/200 (≈ 0.015708).
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "input.h"
#include "scene.h"
#include "scene1_particles_tick.h"
#include "scene1_records.h"
#include "scene1_records_c_tick.h"
#include "scene1_sim.h"
#include "scene1_spawn.h"
#include "sim.h"
#include "worker_load.h"
#include "scene1_intro_events.h"

static void reset_world(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    scene1_spawn_trace_reset();
    g_scene1_records_c_count = 0;
    /* Revert C-tick hooks to defaults (other tests may have installed
     * captures; we don't want them firing here). */
    scene1_records_c_set_ground_query(NULL);
    scene1_records_c_set_raycast(NULL);
    scene1_records_c_set_commit_pickup(NULL);

    /* C8j.3 state-1 sub-dispatch flags — BSS-zero = default arm. */
    g_scene1_ingame_transition_flag = 0;
    g_scene1_ingame_skip_flag       = 0;
    g_scene1_ingame_paused_flag     = 0;

    sim_init();
    worker_load_reset();
    /* The new-game intro-event stub (src/scene1_intro_events.c) is armed by
     * scene_post_fade_init; sim_step_a ticks it and short-circuits while it
     * holds the load gate. A prior test may have left it armed, so reset it
     * here for a hermetic sim_step_a aging count. */
    scene1_intro_events_reset();
    g_input_state[0].buttons = 0;
    g_input_state[1].buttons = 0;
}

/* Stage a STATE=2 (pickup-bob) table-C record at the given slot, with
 * AGE preset (so the trailing age++ advances visibly).  Tracks
 * g_scene1_records_c_count so the integrator's outer loop reaches the
 * slot. */
static void stage_c_pickup(int slot, int type, int age)
{
    int base = slot * SCENE1_RECORDS_C_STRIDE;
    float zero = 0.0f, one = 1.0f;
    int32_t bz, bo;
    memcpy(&bz, &zero, sizeof bz);
    memcpy(&bo, &one,  sizeof bo);
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_POS_X]   = bz;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_POS_Y]   = bz;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_POS_Z]   = bz;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_TYPE]    = type;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_AGE]     = age;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_SCALE]   = bo;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_STATE]   = 2;
    g_scene1_records_c[base + SCENE1_RECORDS_C_OFF_AUX]     = 0;
    if (slot >= g_scene1_records_c_count)
        g_scene1_records_c_count = slot + 1;
}

static int c_slot_get_i(int slot, int off)
{
    return g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
}

static int slot_read_i(int slot, int off)
{
    return g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off];
}

static float slot_read_f(int slot, int off)
{
    int32_t v = g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof v);
    return f;
}

int test_scene1_ingame_tick_runs_integrator(void)
{
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE) == 0x92);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE)  == 0);

    scene1_ingame_tick();

    /* Type-0x92 integrator bumps AGE by 1 and ROT_X by π/200 per tick. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);

    float rot_x = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X);
    if (fabsf(rot_x - 0.015707964f) > 1e-6f)
        T_FAIL("rot.x = %f, expected ~0.015708 (π/200)", (double)rot_x);
    return 0;
}

int test_sim_step_a_state_ingame_runs_integrator(void)
{
    reset_world();
    g_scene_state = SCENE_STATE_INGAME;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    sim_step_a();

    /* The INGAME switch arm should have called scene1_ingame_tick,
     * which calls scene1_particles_tick, which bumps AGE. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    return 0;
}

int test_sim_step_a_state_title_skips_integrator(void)
{
    reset_world();
    g_scene_state = SCENE_STATE_TITLE;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    sim_step_a();

    /* TITLE arm runs scene_title_sim_default; the integrator should
     * be untouched. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 0);
    return 0;
}

int test_sim_step_a_state_ingame_100_ticks(void)
{
    /* Drive 100 ticks and verify AGE accumulates + ROT_X reaches the
     * expected sum of 100 * π/200 = π/2 ≈ 1.5708.  This is the
     * smoke-test for "the integrator runs every frame", not just once. */
    reset_world();
    g_scene_state = SCENE_STATE_INGAME;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    for (int i = 0; i < 100; i++) sim_step_a();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 100);

    float rot_x = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X);
    float expected = 100.0f * 0.015707964f;
    if (fabsf(rot_x - expected) > 1e-4f)
        T_FAIL("rot.x = %f after 100 ticks, expected %f",
               (double)rot_x, (double)expected);
    return 0;
}

/* ─── Cs2: LAB_00453bed mass dispatch ────────────────────────────────── */

static int ticks_advance_age(int state)
{
    reset_world();
    g_scene_state = state;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    sim_step_a();
    return slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE);
}

int test_sim_step_a_state_2_runs_integrator(void)
{
    /* State 2 (cutscene) hits engine LAB_00453bed → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(2), 1);
    return 0;
}

int test_sim_step_a_state_3_runs_integrator(void)
{
    /* State 3 (dialog) hits engine LAB_00453bed → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(3), 1);
    return 0;
}

int test_sim_step_a_state_b_runs_integrator(void)
{
    /* State 0xb (the largest >=9 LAB_00453bed entry) → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(0xb), 1);
    return 0;
}

int test_sim_step_a_state_10_runs_integrator(void)
{
    /* State 0x10 (last ending arm) → particle tick. */
    T_ASSERT_EQ_I(ticks_advance_age(0x10), 1);
    return 0;
}

int test_sim_step_a_state_4_skips_integrator(void)
{
    /* State 4 — engine block 21 goto LAB_00453cfb directly, no tick. */
    T_ASSERT_EQ_I(ticks_advance_age(4), 0);
    return 0;
}

int test_sim_step_a_state_5_skips_integrator(void)
{
    /* State 5 (worldmap) — engine calls FUN_0046c039 then LAB_00453cfb. */
    T_ASSERT_EQ_I(ticks_advance_age(5), 0);
    return 0;
}

int test_sim_step_a_state_a_skips_integrator(void)
{
    /* State 0xa — engine calls FUN_0047e711 then LAB_00453cfb. */
    T_ASSERT_EQ_I(ticks_advance_age(0xa), 0);
    return 0;
}

int test_sim_step_a_state_c_skips_integrator(void)
{
    /* State 0xc — engine falls into the (>=9 && !=0xb && (<0xd || >0x10))
     * exclusion in block 21; goto LAB_00453cfb directly. */
    T_ASSERT_EQ_I(ticks_advance_age(0xc), 0);
    return 0;
}

/* ─── C8j.3: state-1 sub-dispatch + default-arm wiring ──────────────── */

int test_scene1_ingame_default_arm_ticks_particles_and_c(void)
{
    /* All gate flags BSS-zero → scene1_ingame_tick routes through the
     * default-running arm, which must tick BOTH tables. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);  /* table A */
    stage_c_pickup(0, /*type*/ 2, /*age*/ 10);             /* table C */

    scene1_ingame_tick();

    /* Particle (table A) tick fired → AGE 0→1. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    /* Table C tick fired → pickup-bob AGE 10→11. */
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    return 0;
}

int test_scene1_ingame_transition_flag_picks_transition_arm(void)
{
    /* Transition flag != 0 → routes to transition arm, which ticks
     * particles but NOT table C. */
    reset_world();
    g_scene1_ingame_transition_flag = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_skip_flag_skips_all_ticks(void)
{
    /* Skip flag != 0 (transition flag stays 0) → no sim call at all. */
    reset_world();
    g_scene1_ingame_skip_flag = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 0);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_paused_flag_picks_transition_arm(void)
{
    /* Paused flag != 0 (transition+skip both 0) → paused arm body,
     * which is the transition-arm body in the engine.  Particles tick,
     * table C does not. */
    reset_world();
    g_scene1_ingame_paused_flag = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_transition_arm_tick_direct(void)
{
    /* Direct call to transition arm — ticks particles, not C. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_transition_arm_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 10);
    return 0;
}

int test_scene1_ingame_default_arm_tick_direct(void)
{
    /* Direct call to default arm — ticks BOTH particles and C. */
    reset_world();
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);
    stage_c_pickup(0, 2, 10);

    scene1_ingame_default_arm_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    return 0;
}

int test_scene1_ingame_transition_flag_takes_precedence_over_skip(void)
{
    /* Engine order: transition checked first, then skip.  With both
     * set, transition wins → particle tick fires. */
    reset_world();
    g_scene1_ingame_transition_flag = 1;
    g_scene1_ingame_skip_flag       = 1;
    scene1_records_inject_test_type92(0.0f, 0.0f, 0.0f);

    scene1_ingame_tick();

    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 1);
    return 0;
}

int test_sim_step_a_state_ingame_default_arm_also_ticks_c(void)
{
    /* End-to-end: sim_step_a(SCENE_STATE_INGAME) with default flags
     * routes through scene1_ingame_tick → default arm → C tick fires. */
    reset_world();
    g_scene_state = SCENE_STATE_INGAME;
    stage_c_pickup(0, 2, 10);

    sim_step_a();

    T_ASSERT_EQ_I(c_slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    return 0;
}
