/*
 * test_scene1_records_c_tick.c — unit tests for the C8j.1 integrator.
 *
 * Covers FUN_0044284b semantics:
 *   - sentinel-empty slots stay empty
 *   - overflow eviction (DEAD-CODE branch — verify it stays dormant)
 *   - aux flag (state-1 fast-path) net-zeros age per tick
 *   - state-2 pickup-bob: sparkle spawn at age==10, pos.y lift in
 *     [0x14..0x4f], commit + kill at age==0x78
 *   - state-0 world-drop physics: drag, gravity, attract-to-player,
 *     ground clamp + bounce, kill at age==0xf0, kill at pos.y<-1
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "scene1_particles_tick.h"   /* g_scene1_player_pos */
#include "scene1_records.h"
#include "scene1_records_c_tick.h"
#include "scene1_spawn.h"

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    scene1_records_counter_scan();
    scene1_spawn_trace_reset();
    g_scene1_records_c_count = 0;
    for (int i = 0; i < 3; i++) g_scene1_player_pos[i] = 0.0f;
    /* Revert hooks to defaults. */
    scene1_records_c_set_ground_query(NULL);
    scene1_records_c_set_raycast(NULL);
    scene1_records_c_set_commit_pickup(NULL);
}

static void slot_set_i(int slot, int off, int32_t v)
{
    g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off] = v;
}
static int32_t slot_get_i(int slot, int off)
{
    return g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
}
static void slot_set_f(int slot, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off] = v;
}
static float slot_get_f(int slot, int off)
{
    int32_t v = g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

/* Stage a single state-2 (pickup-bob) world-drop slot at (px,py,pz). */
static void stage_pickup(int slot, int type, float px, float py, float pz,
                         int extra1, int extra2)
{
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_POS_X, px);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_POS_Y, py);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_POS_Z, pz);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_TYPE, type);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_AGE, 0);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_SCALE, 1.0f);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_PICKUP_E1, extra1);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_PICKUP_E2, extra2);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_STATE, 2);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_AUX, 0);
    if (slot >= g_scene1_records_c_count) {
        g_scene1_records_c_count = slot + 1;
    }
}

static void stage_world_drop(int slot, int type, float px, float py,
                             float pz, float vx, float vy, float vz)
{
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_POS_X, px);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_POS_Y, py);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_POS_Z, pz);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_VEL_X, vx);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_VEL_Y, vy);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_VEL_Z, vz);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_TYPE, type);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_AGE, 0);
    slot_set_f(slot, SCENE1_RECORDS_C_OFF_SCALE, 1.0f);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_STATE, 0);
    slot_set_i(slot, SCENE1_RECORDS_C_OFF_AUX, 0);
    if (slot >= g_scene1_records_c_count) {
        g_scene1_records_c_count = slot + 1;
    }
}

/* Test-side hook state. */
static int s_commit_calls;
static int s_commit_last_type;
static int s_commit_last_e1;
static int s_commit_last_e2;
static void capture_commit(int t, int e1, int e2)
{
    s_commit_calls++;
    s_commit_last_type = t;
    s_commit_last_e1   = e1;
    s_commit_last_e2   = e2;
}

static int s_ground_y_returns;
static float s_ground_y_value;
static int capture_ground(float x, float z, float *out_y)
{
    (void)x; (void)z;
    *out_y = s_ground_y_value;
    return s_ground_y_returns;
}

/* ─── tests ───────────────────────────────────────────────────────── */

int test_records_c_tick_empty_table_is_noop(void)
{
    reset_world();
    scene1_records_c_tick();    /* count==0; phase1+phase2 skip */
    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE), -1);
    }
    return 0;
}

int test_records_c_tick_sentinel_slots_skipped(void)
{
    reset_world();
    /* Force count to 5 with all slots sentinel; tick should leave them. */
    g_scene1_records_c_count = 5;
    scene1_records_c_tick();
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE), -1);
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_AGE), 0);
    }
    return 0;
}

int test_records_c_tick_aux_flag_net_zero_age(void)
{
    /* aux==1 → age -= 1 in substate, then trailing age++ → net zero. */
    reset_world();
    slot_set_i(0, SCENE1_RECORDS_C_OFF_TYPE, 0x10);   /* non-world-drop */
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 50);
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AUX, 1);
    g_scene1_records_c_count = 1;
    scene1_records_c_tick();
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 50);
    /* Trailing kill check only fires for world-drop types; 0x10 is not. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), 0x10);
    return 0;
}

int test_records_c_tick_pickup_bob_sparkle_at_age_10(void)
{
    reset_world();
    stage_pickup(0, 2, 1.0f, 2.0f, 3.0f, 0, 0);
    /* Bump to age 10 (engine integrator will see age==10 just BEFORE
     * its trailing age++; so it fires sparkle, then advances to 11). */
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 10);

    scene1_records_c_tick();

    /* Pos unchanged (age 10 is NOT in the 0x14..0x4f lift range). */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Y) - 2.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 11);
    /* Sparkle spawn: scene1_spawn type 0x2d at slot pos. */
    T_ASSERT(g_scene1_spawn_trace_count >= 1);
    scene1_spawn_call_t e = g_scene1_spawn_trace[g_scene1_spawn_trace_count - 1];
    T_ASSERT_EQ_I(e.type, 0x2d);
    T_ASSERT(fabsf(e.x - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(e.y - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(e.z - 3.0f) < 1e-6f);
    return 0;
}

int test_records_c_tick_pickup_bob_lifts_in_range(void)
{
    reset_world();
    stage_pickup(0, 2, 0.0f, 0.0f, 0.0f, 0, 0);
    /* age=0x20: in (0x14, 0x50) → pos.y += 0.05f. */
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 0x20);

    scene1_records_c_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Y) - 0.05f) < 1e-6f);
    return 0;
}

int test_records_c_tick_pickup_bob_no_lift_outside_range(void)
{
    reset_world();
    stage_pickup(0, 2, 0.0f, 0.0f, 0.0f, 0, 0);
    /* age=0x50: NOT < 0x50 → no lift. */
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 0x50);

    scene1_records_c_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Y) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_c_tick_pickup_commit_kills_at_age_0x78(void)
{
    reset_world();
    s_commit_calls = 0;
    scene1_records_c_set_commit_pickup(capture_commit);

    stage_pickup(0, 5, 0.0f, 0.0f, 0.0f, 123, 456);
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 0x78);

    scene1_records_c_tick();

    T_ASSERT_EQ_I(s_commit_calls, 1);
    T_ASSERT_EQ_I(s_commit_last_type, 5);
    T_ASSERT_EQ_I(s_commit_last_e1, 123);
    T_ASSERT_EQ_I(s_commit_last_e2, 456);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), -1);

    scene1_records_c_set_commit_pickup(NULL);
    return 0;
}

int test_records_c_tick_world_drop_drag_and_gravity(void)
{
    /* No ground hook → ground_query returns 0 → physics applies drag +
     * gravity only.  Type 0 (world-drop) at age 0, vel (1, 0, 0). */
    reset_world();
    stage_world_drop(0, 0, 0.0f, 10.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    /* age=0 (< 60) → no attract-to-player branch. */
    scene1_records_c_tick();

    /* pos.x += 1.0 (scale=1.0, vel.x=1.0).  vel.x *= 0.97. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_X) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_X) - 0.97f) < 1e-6f);
    /* pos.y += 0 (vel.y=0).  vel.y *= 0.97 → 0, then gravity -0.05. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Y) - 10.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Y) - (-0.05f)) < 1e-6f);
    return 0;
}

int test_records_c_tick_world_drop_attract_to_player(void)
{
    /* age=70 (> 60), type=1 → attract toward player. */
    reset_world();
    g_scene1_player_pos[0] = 5.0f;
    g_scene1_player_pos[2] = 0.0f;
    stage_world_drop(0, 1, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 70);

    scene1_records_c_tick();

    /* vel.x = 0*0.97 + (5-0)*0.2 = 1.0.  Speed sqrt(1²+0²)=1.0; cap kicks
     * in only when speed > 1.0 → exactly 1.0 stays uncapped. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_X) - 1.0f) < 1e-6f);
    return 0;
}

int test_records_c_tick_world_drop_speed_cap(void)
{
    /* Place player far enough that attraction pushes speed > 1.0; cap
     * normalizes to 0.5. */
    reset_world();
    g_scene1_player_pos[0] = 50.0f;
    g_scene1_player_pos[2] = 0.0f;
    stage_world_drop(0, 1, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 70);

    scene1_records_c_tick();

    /* vel.x after engine math: vx = (50-0)*0.2 = 10; speed = 10; cap →
     * vx = 10*0.5/10 = 0.5. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_X) - 0.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Z) - 0.0f) < 1e-6f);
    return 0;
}

int test_records_c_tick_world_drop_ground_bounce(void)
{
    /* Hook says ground at y=0; slot starts at y=-0.5 with vel.y=-0.5
     * (strong downward) → bounce to vy *= -0.7 → +0.35. */
    reset_world();
    s_ground_y_returns = 1;
    s_ground_y_value = 0.0f;
    scene1_records_c_set_ground_query(capture_ground);

    stage_world_drop(0, 0, 0.0f, -0.5f, 0.0f, 0.0f, -0.5f, 0.0f);

    scene1_records_c_tick();

    /* Pos.y clamps to ground (0.0).  Vel.y: after drag/gravity (-0.5 *
     * 0.97 - 0.05 = -0.535), <= -0.3 → bounce: 0.535 * 0.7 = 0.3745. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Y) - 0.3745f) < 1e-5f);
    /* ground_y cached into slot[22] + slot[9]. */
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_GROUND_Y) - 0.0f) < 1e-6f);

    scene1_records_c_set_ground_query(NULL);
    return 0;
}

int test_records_c_tick_world_drop_kill_at_age_0xf0(void)
{
    reset_world();
    stage_world_drop(0, 0, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 0xef);

    scene1_records_c_tick();

    /* Age 0xef → integrator runs world-drop body → trailing age++ → 0xf0
     * → kill via the type-{0,1,2,3} age==0xf0 gate. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), -1);
    return 0;
}

int test_records_c_tick_world_drop_kill_at_pos_y_below_minus_1(void)
{
    reset_world();
    stage_world_drop(0, 0, 0.0f, -1.5f, 0.0f, 0.0f, 0.0f, 0.0f);

    scene1_records_c_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), -1);
    return 0;
}

int test_records_c_tick_overflow_eviction_is_dormant(void)
{
    /* Stage 8 world-drop type-0 slots (> 6 threshold) but with ages
     * well under the 1200 dead-bound.  Eviction should NOT fire. */
    reset_world();
    for (int i = 0; i < 8; i++) {
        stage_world_drop(i, 0, 0.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        slot_set_i(i, SCENE1_RECORDS_C_OFF_AGE, 100);
    }
    /* Disable ground/raycast so types stay alive and ages just increment. */
    scene1_records_c_tick();
    /* All 8 should still be alive; eviction never selects (max_age=1200
     * threshold never crossed). */
    int alive = 0;
    for (int i = 0; i < 8; i++) {
        if (slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE) == 0) alive++;
    }
    T_ASSERT_EQ_I(alive, 8);
    return 0;
}

int test_records_c_tick_non_world_drop_type_no_kill_check(void)
{
    /* Type 0x10 with age=0xef → trailing age++ → 0xf0, but kill check
     * gates on world-drop types only.  Slot stays alive. */
    reset_world();
    stage_world_drop(0, 0x10, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    slot_set_i(0, SCENE1_RECORDS_C_OFF_AGE, 0xef);

    scene1_records_c_tick();

    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), 0x10);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 0xf0);
    return 0;
}

int test_records_c_tick_ground_clamp_non_world_drop_advances_state(void)
{
    /* Non-world-drop type with vel.y in [-0.3, 0) on ground hit → vy = 0,
     * vx = vz = 0, state advances to 1.  vy=-0.1 + drag/gravity:
     * -0.1 * 0.97 - 0.05 = -0.147 (in [-0.3, 0)). */
    reset_world();
    s_ground_y_returns = 1;
    s_ground_y_value = 0.0f;
    scene1_records_c_set_ground_query(capture_ground);

    stage_world_drop(0, 0x10, 0.0f, -0.5f, 0.0f, 1.0f, -0.1f, 2.0f);

    scene1_records_c_tick();

    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Z) - 0.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_STATE), 1);

    scene1_records_c_set_ground_query(NULL);
    return 0;
}

int test_records_c_offset_type_is_10(void)
{
    /* Regression: scene1_records.h used to mis-define table C's TYPE at
     * offset 12 (table A's offset).  Confirm it's 10 (the engine's
     * actual layout per FUN_0044aef0 allocator at puVar1[10] = type). */
    T_ASSERT_EQ_I(SCENE1_RECORDS_C_OFF_TYPE, 10);
    return 0;
}

int test_records_c_reset_sentinels_at_offset_10(void)
{
    /* Regression for the offset fix in scene1_records.c. */
    reset_world();
    /* Stamp non-sentinel at offset 10 (the new correct location). */
    g_scene1_records_c[3 * SCENE1_RECORDS_C_STRIDE + 10] = 42;
    g_scene1_records_c[3 * SCENE1_RECORDS_C_STRIDE + 12] = 99;
    scene1_records_reset(1);
    T_ASSERT_EQ_I(g_scene1_records_c[3 * SCENE1_RECORDS_C_STRIDE + 10], -1);
    /* Offset 12 should be left untouched (it's an arbitrary scratch
     * field, not the type sentinel). */
    T_ASSERT_EQ_I(g_scene1_records_c[3 * SCENE1_RECORDS_C_STRIDE + 12], 99);
    return 0;
}
