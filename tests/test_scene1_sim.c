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
#include "scene1_sim.h"
#include "scene1_spawn.h"
#include "sim.h"
#include "worker_load.h"

static void reset_world(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    scene1_spawn_trace_reset();

    sim_init();
    worker_load_reset();
    g_input_state[0].buttons = 0;
    g_input_state[1].buttons = 0;
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
