/*
 * test_scene1_records_b_tick.c — unit tests for the C8j-tick.1
 * skeleton (engine FUN_0043ae20 outer loop + preamble).
 *
 * Scope: skeleton-only behavior — preamble pos += vel + age++, dead
 * slot skip, kill helper, hook installation.  Per-type behaviors
 * land in sub-chip ladder C8j-tick.2+ with their own tests.
 */

#include "t.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "scene1_records.h"
#include "scene1_records_b_tick.h"

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    scene1_records_reset(1);
    g_scene1_records_b_count = 0;
    g_scene1_records_b_tick_flag = 0;
    scene1_records_b_set_per_type_body(NULL);
    scene1_records_b_set_state_machine_hook(NULL);
}

static int32_t *bslot(int i)
{
    return &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
}

static void slot_set_i(int i, int off, int32_t v)
{
    bslot(i)[off] = v;
}

static int32_t slot_get_i(int i, int off)
{
    return bslot(i)[off];
}

static void slot_set_f(int i, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    bslot(i)[off] = v;
}

static float slot_get_f(int i, int off)
{
    int32_t v = bslot(i)[off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static void stage_live(int slot, int32_t type, float px, float py, float pz,
                       float vx, float vy, float vz, int32_t age)
{
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_TYPE, type);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_POS_X, px);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_POS_Y, py);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_POS_Z, pz);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_VEL_X, vx);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
    slot_set_f(slot, SCENE1_RECORDS_B_OFF_VEL_Z, vz);
    slot_set_i(slot, SCENE1_RECORDS_B_OFF_AGE, age);
}

/* ─── tests ───────────────────────────────────────────────────────── */

int test_records_b_tick_empty_table_is_noop(void)
{
    reset_world();
    scene1_records_b_tick();
    /* Nothing got mutated: all slots remain TYPE=0. */
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE), 0);
    }
    return 0;
}

int test_records_b_tick_skips_dead_slots(void)
{
    /* Dead-slot's pos must NOT see vel-integration even if dirty. */
    reset_world();
    slot_set_f(0, SCENE1_RECORDS_B_OFF_POS_X, 100.0f);
    slot_set_f(0, SCENE1_RECORDS_B_OFF_VEL_X, 5.0f);
    /* TYPE stays 0 → slot is dead. */
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_B_OFF_POS_X) - 100.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_B_OFF_AGE), 0);
    return 0;
}

int test_records_b_tick_preamble_integrates_pos(void)
{
    reset_world();
    /* slot 5 alive with non-zero vel + non-zero pos. */
    stage_live(5, /*type=*/0x10, 1.0f, 2.0f, 3.0f, 0.25f, -0.5f, 1.5f, /*age=*/7);
    scene1_records_b_tick();
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_POS_X) - 1.25f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_POS_Y) - 1.5f)  < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_POS_Z) - 4.5f)  < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(5, SCENE1_RECORDS_B_OFF_AGE), 8);
    /* Vel untouched. */
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_VEL_X) - 0.25f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_VEL_Y) - (-0.5f)) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(5, SCENE1_RECORDS_B_OFF_VEL_Z) - 1.5f) < 1e-6f);
    /* TYPE preserved. */
    T_ASSERT_EQ_I(slot_get_i(5, SCENE1_RECORDS_B_OFF_TYPE), 0x10);
    return 0;
}

int test_records_b_tick_preamble_clears_per_tick_flag(void)
{
    reset_world();
    g_scene1_records_b_tick_flag = 42;  /* dirty */
    stage_live(0, 1, 0, 0, 0, 0, 0, 0, 0);
    scene1_records_b_tick();
    T_ASSERT_EQ_I(g_scene1_records_b_tick_flag, 0);
    return 0;
}

int test_records_b_tick_kill_slot_sets_type_zero(void)
{
    reset_world();
    stage_live(10, /*type=*/0x42, 0, 0, 0, 0, 0, 0, 0);
    T_ASSERT_EQ_I(slot_get_i(10, SCENE1_RECORDS_B_OFF_TYPE), 0x42);
    scene1_records_b_tick_kill_slot(10);
    T_ASSERT_EQ_I(slot_get_i(10, SCENE1_RECORDS_B_OFF_TYPE), 0);
    return 0;
}

int test_records_b_tick_kill_slot_oob_is_safe(void)
{
    reset_world();
    /* Should not crash / corrupt anything. */
    scene1_records_b_tick_kill_slot(-1);
    scene1_records_b_tick_kill_slot(SCENE1_RECORDS_B_COUNT);
    scene1_records_b_tick_kill_slot(SCENE1_RECORDS_B_COUNT + 100);
    return 0;
}

/* Per-type-body hook capture state. */
static int s_dispatch_calls;
static int s_dispatch_last_slot;
static int32_t s_dispatch_last_type;
static void capture_dispatch(int slot_idx, int32_t type)
{
    s_dispatch_calls++;
    s_dispatch_last_slot = slot_idx;
    s_dispatch_last_type = type;
}

int test_records_b_tick_per_type_hook_fires_after_preamble(void)
{
    reset_world();
    s_dispatch_calls      = 0;
    s_dispatch_last_slot  = -1;
    s_dispatch_last_type  = 0;
    scene1_b_per_type_body_fn prev =
        scene1_records_b_set_per_type_body(capture_dispatch);
    T_ASSERT(prev == NULL);

    stage_live(3, /*type=*/0x77, 1, 2, 3, 0, 0, 0, 0);
    scene1_records_b_tick();

    T_ASSERT_EQ_I(s_dispatch_calls, 1);
    T_ASSERT_EQ_I(s_dispatch_last_slot, 3);
    T_ASSERT_EQ_I(s_dispatch_last_type, 0x77);

    /* Confirm preamble fired before dispatch — age already 1 if hook
     * had read the slot.  We didn't capture state; just confirm
     * post-call slot has age=1 + pos unchanged (vel=0). */
    T_ASSERT_EQ_I(slot_get_i(3, SCENE1_RECORDS_B_OFF_AGE), 1);

    /* Restore. */
    scene1_records_b_set_per_type_body(NULL);
    return 0;
}

int test_records_b_tick_per_type_hook_not_called_for_dead_slots(void)
{
    reset_world();
    s_dispatch_calls = 0;
    scene1_records_b_set_per_type_body(capture_dispatch);
    /* All 512 slots dead. */
    scene1_records_b_tick();
    T_ASSERT_EQ_I(s_dispatch_calls, 0);
    scene1_records_b_set_per_type_body(NULL);
    return 0;
}

/* State-machine hook setter is wired but not invoked from skeleton.
 * Confirm setter round-trips. */
static void noop_state_machine(int32_t *slot) { (void)slot; }

int test_records_b_tick_state_machine_setter_round_trips(void)
{
    reset_world();
    scene1_b_state_machine_fn prev =
        scene1_records_b_set_state_machine_hook(noop_state_machine);
    T_ASSERT(prev == NULL);
    prev = scene1_records_b_set_state_machine_hook(NULL);
    T_ASSERT(prev == noop_state_machine);
    return 0;
}

int test_records_b_tick_walks_all_512_slots(void)
{
    reset_world();
    /* Mark slots 0, 100, 256, 511 alive — confirm preamble fires on
     * the outer extremes. */
    stage_live(0,   1, 0, 0, 0, 1.0f, 0, 0, 0);
    stage_live(100, 1, 0, 0, 0, 0, 1.0f, 0, 0);
    stage_live(256, 1, 0, 0, 0, 0, 0, 1.0f, 0);
    stage_live(511, 1, 0, 0, 0, 0.5f, 0.5f, 0.5f, 0);

    scene1_records_b_tick();

    T_ASSERT(fabsf(slot_get_f(0,   SCENE1_RECORDS_B_OFF_POS_X) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(100, SCENE1_RECORDS_B_OFF_POS_Y) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(256, SCENE1_RECORDS_B_OFF_POS_Z) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(511, SCENE1_RECORDS_B_OFF_POS_X) - 0.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(511, SCENE1_RECORDS_B_OFF_POS_Y) - 0.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(511, SCENE1_RECORDS_B_OFF_POS_Z) - 0.5f) < 1e-6f);

    /* All four had age++. */
    T_ASSERT_EQ_I(slot_get_i(0,   SCENE1_RECORDS_B_OFF_AGE), 1);
    T_ASSERT_EQ_I(slot_get_i(100, SCENE1_RECORDS_B_OFF_AGE), 1);
    T_ASSERT_EQ_I(slot_get_i(256, SCENE1_RECORDS_B_OFF_AGE), 1);
    T_ASSERT_EQ_I(slot_get_i(511, SCENE1_RECORDS_B_OFF_AGE), 1);
    return 0;
}
