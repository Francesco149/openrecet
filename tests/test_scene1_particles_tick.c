/*
 * test_scene1_particles_tick.c — unit tests for the C8h.1 integrator.
 *
 * Covers the 4 type handlers that landed in C8h.1:
 *   - types 6, 7, 8, 9 — camera-orbit attract
 *   - type 0x20        — player-snap with every-4-tick chain-spawn
 *   - type 0x21        — cone-spread velocity sampling
 *
 * Sentinel-empty slots stay sentinel-empty.  Age progresses.  Kills
 * happen at the right gates.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "scene1_particles_tick.h"
#include "scene1_records.h"
#include "scene1_spawn.h"

/* Reset everything: tables fully zeroed (engine reset only touches TYPE,
 * but tests cross-pollinate via leftover pos/vel/rot fields when slot 0
 * is reused — full memset gives every test a clean canvas).  Then
 * sentinel-init via scene1_records_reset so consumers see the expected
 * TYPE=-1 (table A and C) and field-0=0 (table B). */
static void reset_world(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    memset(g_scene1_records_b, 0, sizeof g_scene1_records_b);
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    scene1_spawn_trace_reset();
    g_scene1_scene_alive   = 1;
    g_scene1_camera_yaw    = 0.0f;
    g_scene1_camera_anchor[0] = 0.0f;
    g_scene1_camera_anchor[1] = 0.0f;
    for (int i = 0; i < 3; i++) {
        g_scene1_player_pos[i]    = 0.0f;
        g_scene1_spawn_origin[i]  = 0.0f;
    }
}

/* Write float bits into the record table at slot/off. */
static void slot_write_f(int slot, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off] = v;
}

static float slot_read_f(int slot, int off)
{
    int32_t v = g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof v);
    return f;
}

static int slot_read_i(int slot, int off)
{
    return g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE + off];
}

/* ─── outer loop / sentinel behavior ────────────────────────────────── */

int test_particles_tick_empty_tables_noop(void)
{
    reset_world();
    /* Every slot starts at TYPE = -1.  A full tick should leave them
     * sentinel-empty and not call into any spawn. */
    scene1_particles_tick();
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        T_ASSERT(slot_read_i(i, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    }
    T_ASSERT(g_scene1_spawn_trace_count == 0);
    return 0;
}

int test_particles_tick_ignores_unported_types(void)
{
    reset_world();
    /* Type 0x4a (matrix-transform — lands in C8h.3) is unported.
     * C8h.1/.2 must NOT touch the slot — leave TYPE / pos / age all
     * unchanged. */
    int slot = 17;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x4a;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 5.0f);
    r[SCENE1_RECORDS_A_OFF_AGE] = 7;

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == 0x4a);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) == 5.0f);
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE)  == 7);
    return 0;
}

/* ─── types 6..9 — camera-orbit attract ─────────────────────────────── */

int test_particles_tick_type_6_initial_position(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 6;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    /* PARAM2 = 0 (orbit sector 0). */
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 0;

    scene1_particles_tick();

    /* At age 0 → t = 0 → pos = anchor A (camera-orbit start).
     *   ax = sin(2.3876104) * 6 + 0 ≈ 0.6816 * 6 = 4.090
     *   az = cos(2.3876104) * 6 + 0 ≈ -0.7317 * 6 = -4.390
     *   ay = player_y + 1 = 1.0
     * Plus 2 * sin(age=0) = 0 → pos.y = 1.0. */
    float pos_x = slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X);
    float pos_y = slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y);
    float pos_z = slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z);

    float expected_x = sinf(2.3876104f) * 6.0f;
    float expected_z = cosf(2.3876104f) * 6.0f;
    if (fabsf(pos_x - expected_x) > 1e-4f)
        T_FAIL("pos.x = %f, expected %f", (double)pos_x, (double)expected_x);
    if (fabsf(pos_y - 1.0f) > 1e-4f)
        T_FAIL("pos.y = %f, expected 1.0", (double)pos_y);
    if (fabsf(pos_z - expected_z) > 1e-4f)
        T_FAIL("pos.z = %f, expected %f", (double)pos_z, (double)expected_z);

    /* Age advanced. */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);
    /* Not killed (scene_alive == 1). */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == 6);
    return 0;
}

int test_particles_tick_type_7_8_9_share_body(void)
{
    /* Confirm types 7, 8, 9 take the same path as 6 by checking the age
     * advances on each.  All four use the same anchor/snap math. */
    for (int type = 7; type <= 9; type++) {
        reset_world();
        int slot = 3;
        int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
        r[SCENE1_RECORDS_A_OFF_TYPE] = type;
        r[SCENE1_RECORDS_A_OFF_AGE]  = 5;
        r[SCENE1_RECORDS_A_OFF_PARAM2] = 1;

        scene1_particles_tick();

        if (slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) != 6)
            T_FAIL("type %d: age did not advance", type);
        if (slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) != type)
            T_FAIL("type %d: slot killed unexpectedly", type);
    }
    return 0;
}

int test_particles_tick_type_6_kills_on_scene_dead(void)
{
    reset_world();
    g_scene1_scene_alive = 0;
    int slot = 7;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 6;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 0;

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

int test_particles_tick_type_6_interp_saturates(void)
{
    /* At age = 12, t = 12 * 0.08 = 0.96 → near full-interp toward
     * anchor B (player snap).  At age = 13, t clamps to 1.0. */
    reset_world();
    g_scene1_player_pos[0] = 100.0f;
    g_scene1_player_pos[1] = 50.0f;
    g_scene1_player_pos[2] = -30.0f;

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 6;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 13;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 0;

    scene1_particles_tick();

    /* t saturated; pos.y = player_y + 0.5 + 2*sin(14) — within reach.
     * Without computing sin(14) here, just sanity-check pos.x is near
     * the orbit-B target sinf(sector_angle - yaw) * 1.5 + player_x. */
    float sector_angle = (0.0f * 6.2831855f) / 3.0f + 3.1415927f;
    float bx = sinf(sector_angle) * 1.5f + 100.0f;
    float pos_x = slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X);
    if (fabsf(pos_x - bx) > 1e-3f)
        T_FAIL("pos.x = %f, expected near %f", (double)pos_x, (double)bx);
    return 0;
}

/* ─── type 0x20 — player-snap with every-4-tick spawn ───────────────── */

int test_particles_tick_type_20_snaps_to_origin(void)
{
    reset_world();
    g_scene1_spawn_origin[0] = 10.0f;
    g_scene1_spawn_origin[1] = 20.0f;
    g_scene1_spawn_origin[2] = 30.0f;

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x20;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;

    scene1_particles_tick();

    /* pos = (origin.x, origin.y + 2.5, origin.z). */
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) == 10.0f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) == 22.5f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) == 30.0f);
    /* Age 0 → 1; (1 & 3) != 0 → no chain spawn. */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);
    T_ASSERT(g_scene1_spawn_trace_count == 0);
    return 0;
}

int test_particles_tick_type_20_spawns_every_4_ticks(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x20;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 3;  /* age++ → 4; (4 & 3) == 0 → spawn */

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 4);
    T_ASSERT(g_scene1_spawn_trace_count == 1);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0x21);
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == 0);
    return 0;
}

int test_particles_tick_type_20_kills_on_scene_dead(void)
{
    reset_world();
    g_scene1_scene_alive = 0;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x20;

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* ─── type 0x21 — cone-spread velocity sampling ─────────────────────── */

int test_particles_tick_type_21_basic_velocity_and_rotation(void)
{
    reset_world();
    g_scene1_spawn_origin[0] = 0.0f;
    g_scene1_spawn_origin[1] = 0.0f;
    g_scene1_spawn_origin[2] = 0.0f;

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x21;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 0x7f;  /* 127 — neutral spread offset */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_ROT_X, 0.0f);

    scene1_particles_tick();

    /* rot.x bumped by 0.15. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_X) - 0.15f) > 1e-5f)
        T_FAIL("rot.x not bumped");

    /* vel.x = 0, vel.z = 0. */
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z) == 0.0f);

    /* vel.y = (127-127)*0.002 + cos(0)*0.2 - 0*0.005 = 0.2. */
    float vy = slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y);
    if (fabsf(vy - 0.2f) > 1e-5f)
        T_FAIL("vel.y = %f, expected 0.2", (double)vy);

    /* Snapped to spawn origin + 2.5 on Y. */
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) == 2.5f);
    /* Age incremented. */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);
    return 0;
}

int test_particles_tick_type_21_kills_at_age_0x20(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x21;
    /* age 0x1f → after increment becomes 0x20 → kill. */
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x1f;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = -1;

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

int test_particles_tick_type_21_no_snap_when_param2_minus1(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x21;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = -1;
    /* Pre-existing position should survive. */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 99.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_Y, 88.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_Z, 77.0f);

    scene1_particles_tick();

    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) == 99.0f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) == 88.0f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) == 77.0f);
    return 0;
}

int test_particles_tick_type_21_kills_when_table_b_empty(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x21;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 5;
    /* PARAM2 = 10 → reads g_scene1_records_b[10 * 0x49].  After
     * scene1_records_reset, that field == 0 → kill. */
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 10;

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

int test_particles_tick_type_21_lives_when_table_b_active(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x21;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 5;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 10;

    /* Activate table-B slot 10 — field 0 nonzero means alive. */
    g_scene1_records_b[10 * SCENE1_RECORDS_B_STRIDE] = 1;

    scene1_particles_tick();

    /* Should age 5 → 6 and not be killed. */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 6);
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == 0x21);
    return 0;
}

/* ─── chained-spawn observed correctly ─────────────────────────────── */

/* ─── C8h.2 — decay-drift-kill / pure-age / field-decay handlers ─── */

/* type 0x43 — decay-drift-uniform, damp 0.97, kill 0x18. */
int test_particles_tick_type_43_decay_drift(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x43;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 0.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 2.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z, 3.0f);

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 1.0f) > 1e-6f)
        T_FAIL("pos.x didn't advance by vel.x");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) - 0.97f) > 1e-6f)
        T_FAIL("vel.x not damped");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) - 1.94f) > 1e-6f)
        T_FAIL("vel.y not damped");
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);
    return 0;
}

/* type 0x43 — kills at age 0x18. */
int test_particles_tick_type_43_kills_at_0x18(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x43;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x17;  /* will become 0x18 → kill */
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x68 — no damp; pos += vel; age++; kill 0x30. */
int test_particles_tick_type_68_no_damp(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x68;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 5.0f);

    scene1_particles_tick();

    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) != 5.0f)
        T_FAIL("type 0x68 should NOT damp vel");
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 5.0f)
        T_FAIL("pos didn't advance");
    return 0;
}

/* type 0x29 — pre-damp gravity, kill 0x28. */
int test_particles_tick_type_29_gravity(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x29;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 1.0f);

    scene1_particles_tick();

    /* vel.y after: (1.0 - 0.002) * 0.97 = 0.998 * 0.97 = 0.96806 */
    float vy = slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y);
    if (fabsf(vy - 0.96806f) > 1e-5f)
        T_FAIL("vel.y wrong: got %f, expected ~0.96806", (double)vy);
    return 0;
}

/* type 0x96/0x97 — post-damp gravity + rot bumps. */
int test_particles_tick_type_96_rot_bumps(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x96;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_X) - 0.1f) > 1e-6f)
        T_FAIL("rot.x not bumped");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y) - 0.03f) > 1e-6f)
        T_FAIL("rot.y not bumped");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Z) - 0.01f) > 1e-6f)
        T_FAIL("rot.z not bumped");
    /* vel.y after: 0 * 0.995 - 0.03 = -0.03 */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) + 0.03f) > 1e-6f)
        T_FAIL("vel.y wrong (post-damp gravity)");
    return 0;
}

/* type 0x60 — age caps at 400, kills at 0x960. */
int test_particles_tick_type_60_age_cap(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x60;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 400;
    scene1_particles_tick();
    /* age >= 400 → no increment. */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 400);
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == 0x60);
    return 0;
}

int test_particles_tick_type_60_kill_at_0x960(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x60;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x960;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x5d — kill at age == PARAM1 + 0x3e. */
int test_particles_tick_type_5d_param1_kill(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x5d;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 9;   /* will become 10 */
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 10 - 0x3e; /* kill when age == 10 */
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x36 — gravity -0.02, kill at PARAM1. */
int test_particles_tick_type_36_kill_at_param1(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x36;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 4;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 5;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x4b — rot.y += vel.x, kill at PARAM2 + 0x28. */
int test_particles_tick_type_4b_field_decay(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x4b;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 0;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 5;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 0.3f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y, 1.0f);

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y) - 1.3f) > 1e-6f)
        T_FAIL("rot.y not bumped by vel.x");
    /* Kill at age == PARAM2(5) + 0x28 == 0x2d. */
    r[SCENE1_RECORDS_A_OFF_AGE] = 0x2c;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x32 — constant rot.x += 0.2, kill 0x40. */
int test_particles_tick_type_32_constant_rot(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x32;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_X) - 0.2f) > 1e-6f)
        T_FAIL("rot.x not advanced");
    return 0;
}

/* type 0x71 — pos += 2*vel, age += 2. */
int test_particles_tick_type_71_double_step(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x71;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 2.0f) > 1e-6f)
        T_FAIL("pos.x didn't double-step");
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 2);
    return 0;
}

/* type 0x59 — EXPANDING (damp 1.05). */
int test_particles_tick_type_59_expanding(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x59;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 1;  /* > 0 → active gate */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) - 1.05f) > 1e-6f)
        T_FAIL("vel.x should grow (damp 1.05)");
    return 0;
}

/* type 0x25 — huge group: rot accumulator. */
int test_particles_tick_type_25_huge_group(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x25;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y, 0.5f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_ROT_Z, 0.0f);

    scene1_particles_tick();

    /* rot.z += rot.y (= 0.5); rot.y *= 0.97 (= 0.485). */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Z) - 0.5f) > 1e-6f)
        T_FAIL("rot.z not bumped");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y) - 0.485f) > 1e-6f)
        T_FAIL("rot.y not damped");
    return 0;
}

/* type 0x50 — pure-age, kill 300. */
int test_particles_tick_type_50_pure_age(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x50;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 299;

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x10 — PARAM1-driven gravity. */
int test_particles_tick_type_10_param1_gravity(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x10;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 0;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 10;  /* gravity = 10 * 0.003 = 0.03 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 1.0f);

    scene1_particles_tick();

    /* vel.y after: (1.0 - 0.03) * 0.92 = 0.8924 */
    float vy = slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y);
    if (fabsf(vy - 0.8924f) > 1e-5f)
        T_FAIL("vel.y wrong: got %f, expected ~0.8924", (double)vy);
    return 0;
}

/* type 4 — scaled drift + rot.z drip; kill 0x10. */
int test_particles_tick_type_4_rot_drip(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 4;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_SCALE, 2.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);

    scene1_particles_tick();

    /* pos.x += vel.x * scale = 2.0; rot.z += 0.1. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 2.0f) > 1e-6f)
        T_FAIL("pos.x not scaled-stepped");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Z) - 0.1f) > 1e-6f)
        T_FAIL("rot.z not dripped");
    return 0;
}

int test_particles_tick_chain_20_to_21_records_spawn(void)
{
    reset_world();
    g_scene1_spawn_origin[0] = 1.0f;
    g_scene1_spawn_origin[1] = 2.0f;
    g_scene1_spawn_origin[2] = 3.0f;

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x20;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 3;

    scene1_particles_tick();

    /* The chain-spawn fires at the post-snap pos. */
    T_ASSERT(g_scene1_spawn_trace_count == 1);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0x21);
    if (fabsf(g_scene1_spawn_trace[0].x - 1.0f) > 1e-6f)
        T_FAIL("spawn.x = %f", (double)g_scene1_spawn_trace[0].x);
    if (fabsf(g_scene1_spawn_trace[0].y - 4.5f) > 1e-6f)
        T_FAIL("spawn.y = %f", (double)g_scene1_spawn_trace[0].y);
    if (fabsf(g_scene1_spawn_trace[0].z - 3.0f) > 1e-6f)
        T_FAIL("spawn.z = %f", (double)g_scene1_spawn_trace[0].z);
    return 0;
}
