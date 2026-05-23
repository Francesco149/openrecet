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
    scene1_mesh_emit_trace_reset();
    g_scene1_scene_alive   = 1;
    g_scene1_camera_yaw    = 0.0f;
    g_scene1_camera_anchor[0] = 0.0f;
    g_scene1_camera_anchor[1] = 0.0f;
    g_scene1_player_ground_y  = 0.0f;
    g_scene1_scene_counter    = 0;
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

/* ─── C8h.3 — matrix transforms + trig handlers ──────────────────── */

/* type 0x92 — sinusoidal X-drift + rot triad bump. */
int test_particles_tick_type_92_rot_spin(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x92;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 0;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_ROT_X, 0.0f);

    scene1_particles_tick();

    /* rot.x bumped by π/200 ≈ 0.01571. */
    float rx = slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_X);
    if (fabsf(rx - 0.015707964f) > 1e-6f)
        T_FAIL("rot.x = %f, expected ~π/200", (double)rx);
    /* phase=0 → sin(0)=0 → perturb=0 → vel.x unchanged. */
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) != 0.0f)
        T_FAIL("vel.x should be 0 (sin(0)=0)");
    return 0;
}

int test_particles_tick_type_92_kills_at_0x100(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x92;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0xff;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x18 — vel.y replaced by sin(rot.y) * 0.03. */
int test_particles_tick_type_18_sin_drives_vy(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x18;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y, 1.5707963f);  /* π/2 → sin=1 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_SCALE, 1.0f);

    scene1_particles_tick();

    /* vel.y = sin(π/2) * 0.03 = 0.03. */
    float vy = slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y);
    if (fabsf(vy - 0.03f) > 1e-5f)
        T_FAIL("vel.y = %f, expected 0.03", (double)vy);
    /* rot.y decayed: 1.5707963 - 0.03 ≈ 1.5407963. */
    float ry = slot_read_f(slot, SCENE1_RECORDS_A_OFF_ROT_Y);
    if (fabsf(ry - 1.5407963f) > 1e-5f)
        T_FAIL("rot.y not decayed");
    return 0;
}

/* type 0x34 — orbits player, chains 0x35 on death. */
int test_particles_tick_type_34_lives_and_orbits(void)
{
    reset_world();
    g_scene1_player_pos[0] = 10.0f;
    g_scene1_player_pos[1] = 5.0f;
    g_scene1_player_pos[2] = -3.0f;

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x34;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);  /* unit dist */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z, 0.0f);

    scene1_particles_tick();

    /* age 0 → dist = (0x18 - 0) * 1.0 = 24.  vel.y=0 means no
     * RotX rotation; vel.z=0 means no RotY.  So M = T(0,0,24);
     * M[12..14] = (0, 0, 24).  pos = (10+0, 7+0, -3+24) = (10, 7, 21). */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 10.0f) > 1e-5f)
        T_FAIL("pos.x wrong");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) - 7.0f) > 1e-5f)
        T_FAIL("pos.y wrong");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) - 21.0f) > 1e-5f)
        T_FAIL("pos.z = %f, expected 21",
               (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z));

    /* vel.z incremented by 0.05. */
    float vz = slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z);
    if (fabsf(vz - 0.05f) > 1e-6f) T_FAIL("vel.z not incremented");
    return 0;
}

int test_particles_tick_type_34_chains_35_on_death(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x34;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x17;  /* will increment to 0x18 → kill+spawn */

    scene1_particles_tick();

    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    T_ASSERT(g_scene1_spawn_trace_count == 1);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0x35);
    return 0;
}

/* type 0x35 — orbit body, position snaps to player + (0,2,0). */
int test_particles_tick_type_35_pos_snaps_to_player(void)
{
    reset_world();
    g_scene1_player_pos[0] = 100.0f;
    g_scene1_player_pos[1] = 50.0f;
    g_scene1_player_pos[2] = -30.0f;

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x35;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;

    scene1_particles_tick();

    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) == 100.0f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) == 52.0f);
    T_ASSERT(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) == -30.0f);

    /* rot.y=0, rot.z=0 → M = T(0,0,1); vel = (0, 0, 1). */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X)) > 1e-6f)
        T_FAIL("vel.x not zero");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y)) > 1e-6f)
        T_FAIL("vel.y not zero");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z) - 1.0f) > 1e-6f)
        T_FAIL("vel.z = %f, expected 1.0",
               (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z));
    return 0;
}

int test_particles_tick_type_35_kills_at_0x30(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x35;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x2f;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
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

/* ─── C8h.4b ─────────────────────────────────────────────────────────
 *
 * 14 handler tests for the no-table-dep types: 99, 0x23, 0x22, 0x3c,
 * 0x5a, 0x98, 0x2c, 0x41/0x61/0x62/0x72, 0x3d, 0x6e, 0x6d, 0x6c, 0x1d,
 * 0x2d.  Each handler gets one basic-behavior test + one kill test;
 * a couple of more-complex ones (0x98 distance kill, 0x6e mesh emit)
 * get a third.
 */

/* type 99 — baseline drift + player anchor (Y+2.0).  Engine L91-L104. */
int test_particles_tick_type_99_baseline_drift_anchor(void)
{
    reset_world();
    g_scene1_player_pos[0] = 10.0f;
    g_scene1_player_pos[1] = 20.0f;
    g_scene1_player_pos[2] = 30.0f;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 99;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 0.5f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z, -1.0f);

    scene1_particles_tick();

    /* baseline += vel, then pos = player + baseline (+Y 2.0). */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 11.0f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) - 22.5f) > 1e-6f)
        T_FAIL("pos.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) - 29.0f) > 1e-6f)
        T_FAIL("pos.z = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z));
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);
    return 0;
}

int test_particles_tick_type_99_kills_at_0x18(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 99;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x17;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x23 — hard snap to (player.x, player.y+0.1, player.z).
 * No body gating; baseline/vel ignored.  Engine L690-L698. */
int test_particles_tick_type_23_hard_snap(void)
{
    reset_world();
    g_scene1_player_pos[0] = 5.0f;
    g_scene1_player_pos[1] = 10.0f;
    g_scene1_player_pos[2] = -3.0f;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x23;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 999.0f);

    scene1_particles_tick();

    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 5.0f)
        T_FAIL("not snapped to player.x");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) - 10.1f) > 1e-6f)
        T_FAIL("not snapped to player.y + 0.1");
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) != -3.0f)
        T_FAIL("not snapped to player.z");
    return 0;
}

int test_particles_tick_type_23_kills_at_0x30(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x23;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x2f;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x22 — baseline drift + buoyancy (+0.002) + anchor + damp.
 * Engine L699-L719.  Same body as 0x3c with opposite gravity sign. */
int test_particles_tick_type_22_drift_and_damp(void)
{
    reset_world();
    g_scene1_player_pos[0] = 100.0f;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x22;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);

    scene1_particles_tick();

    /* baseline.x = 0 + 1.0 = 1.0; pos.x = 100 + 1 = 101. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 101.0f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    /* vel.x: 1.0 * 0.97 = 0.97. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) - 0.97f) > 1e-6f)
        T_FAIL("vel.x not damped");
    /* vel.y: (0.0 + 0.002) * 0.97 = 0.00194. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) - 0.00194f) > 1e-6f)
        T_FAIL("vel.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y));
    return 0;
}

int test_particles_tick_type_22_kills_at_0x20(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x22;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x1f;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x3c — like 0x22 but gravity (-0.002) and kill 0x30. */
int test_particles_tick_type_3c_gravity_and_kill(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x3c;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);

    scene1_particles_tick();

    /* vel.y: (0.0 - 0.002) * 0.97 = -0.00194. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) + 0.00194f) > 1e-6f)
        T_FAIL("vel.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y));

    /* Verify kill at 0x30 (not 0x20). */
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x2f;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x5a — y-ONLY baseline drift; baseline.x/z stay frozen.
 * Engine L741-L757. */
int test_particles_tick_type_5a_yonly_baseline_drift(void)
{
    reset_world();
    g_scene1_player_pos[0] = 5.0f;
    g_scene1_player_pos[1] = 6.0f;
    g_scene1_player_pos[2] = 7.0f;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x5a;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_X, 100.0f);  /* should stay 100 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_Y, 0.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_Z, 200.0f);  /* should stay 200 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 999.0f);   /* not integrated into baseline */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 1.0f);

    scene1_particles_tick();

    /* baseline.x/z untouched. */
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_BASE_X) != 100.0f)
        T_FAIL("baseline.x must not drift for type 0x5a");
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_BASE_Z) != 200.0f)
        T_FAIL("baseline.z must not drift for type 0x5a");
    /* baseline.y advanced by vel.y. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_BASE_Y) - 1.0f) > 1e-6f)
        T_FAIL("baseline.y must drift for type 0x5a");
    /* pos = player + (frozen baseline.x, drifted baseline.y, frozen baseline.z) */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 105.0f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) - 207.0f) > 1e-6f)
        T_FAIL("pos.z = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z));
    return 0;
}

int test_particles_tick_type_5a_kills_at_0x30(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x5a;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x2f;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x98 — drift + damp; after age >= PARAM1 steer toward player,
 * kill if close.  Engine L276-L305. */
int test_particles_tick_type_98_drift_and_damp(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x98;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 100;  /* steer-toward gate way out */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);

    scene1_particles_tick();

    /* age 0 < PARAM1 100 → just drift + single damp. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 1.0f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) - 0.97f) > 1e-6f)
        T_FAIL("vel.x not damped");
    return 0;
}

int test_particles_tick_type_98_kill_on_close_distance(void)
{
    reset_world();
    g_scene1_player_pos[0] = 0.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 0.0f;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x98;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 0;     /* immediately steer toward player */
    r[SCENE1_RECORDS_A_OFF_AGE]    = 0;
    /* Pos very close to player (with the +2.0 y offset, distance is ~2). */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 0.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_Y, 1.0f);  /* dy = 1, len ~ 1 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_Z, 0.0f);

    scene1_particles_tick();

    /* len < 3 → kill. */
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

int test_particles_tick_type_98_kills_at_0x40(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x98;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 9999;   /* keep steer-toward off */
    r[SCENE1_RECORDS_A_OFF_AGE]    = 0x3f;
    /* Pos far from player so the close-distance kill doesn't fire. */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 1000.0f);
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x2c — reverse drift gated on age > 0 (first tick is free).
 * Engine L416-L427. */
int test_particles_tick_type_2c_first_tick_no_move(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x2c;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 50.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 10.0f);

    scene1_particles_tick();

    /* age was 0, so no body; pos unchanged.  Age now 1. */
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 50.0f)
        T_FAIL("pos.x changed on age-0 tick");
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);
    return 0;
}

int test_particles_tick_type_2c_reverse_drift(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x2c;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 5;  /* > 0 → body fires */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 50.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 10.0f);

    scene1_particles_tick();

    /* pos.x -= vel.x. */
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 40.0f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    return 0;
}

/* type 0x41 — snap to (player.x, ground_y, player.z); kill at age 100. */
int test_particles_tick_type_41_snap_to_ground(void)
{
    reset_world();
    g_scene1_player_pos[0] = 7.0f;
    g_scene1_player_pos[1] = 99.0f;          /* player.y is animated; should be ignored */
    g_scene1_player_pos[2] = -2.0f;
    g_scene1_player_ground_y = 0.5f;          /* floor the player is standing on */
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x41;

    scene1_particles_tick();

    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 7.0f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) != 0.5f)
        T_FAIL("pos.y must use ground_y, not player.y");
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) != -2.0f)
        T_FAIL("pos.z = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z));
    return 0;
}

int test_particles_tick_type_41_kills_at_100(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x41;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 99;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x62 — kill gate on g_scene1_scene_counter.  Kills iff
 * counter <= 0x2c (44).  Stays alive when counter > 44. */
int test_particles_tick_type_62_lives_when_counter_high(void)
{
    reset_world();
    g_scene1_scene_counter = 0x2d;  /* > 44 */
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x62;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == 0x62);
    return 0;
}

int test_particles_tick_type_62_dies_when_counter_low(void)
{
    reset_world();
    g_scene1_scene_counter = 10;  /* <= 44 */
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x62;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* types 0x61 / 0x72 — no snap, just age++ and kill at 300. */
int test_particles_tick_type_61_no_snap_kill_at_300(void)
{
    reset_world();
    g_scene1_player_pos[0] = 99.0f;
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x61;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 1.0f);
    scene1_particles_tick();
    /* No snap — pos unchanged. */
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 1.0f)
        T_FAIL("type 0x61 must not snap");
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_AGE) == 1);

    /* Kill at 300. */
    r[SCENE1_RECORDS_A_OFF_AGE] = 299;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x3d — trig orbit; sin/cos around baseline.  Engine L561-L586.
 * Kill at age == (PARAM1 * 270) / 100. */
int test_particles_tick_type_3d_orbit_basic(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x3d;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 100;     /* kill_age = 270 */
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 0;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_SCALE, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_X, 10.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_Y, 20.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_Z, 30.0f);

    scene1_particles_tick();

    /* age becomes 1, age >= 0 so body fires.
     * angle = (1 + 0) * 0.04 = 0.04
     * radius = (0 * 0.2 + 2.0) * 1.0 = 2.0
     * pos.x = sin(0.04) * 2 + 10  ≈ 0.03999 * 2 + 10 = 10.07999
     * pos.z = cos(0.04) * 2 + 30  ≈ 0.99920 * 2 + 30 = 31.9984 */
    float px = slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X);
    float pz = slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z);
    if (fabsf(px - 10.07999f) > 1e-3f)
        T_FAIL("pos.x = %f (expected ~10.08)", (double)px);
    if (fabsf(pz - 31.9984f) > 1e-3f)
        T_FAIL("pos.z = %f (expected ~31.998)", (double)pz);
    return 0;
}

int test_particles_tick_type_3d_kill_from_param1(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x3d;
    r[SCENE1_RECORDS_A_OFF_PARAM1] = 10;       /* kill_age = (10 * 270)/100 = 27 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_SCALE, 1.0f);
    r[SCENE1_RECORDS_A_OFF_AGE]    = 26;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x6e — drift; emit mesh at age 100; kill 0x74.  Engine L610-L626. */
int test_particles_tick_type_6e_drifts_then_emits_mesh(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x6e;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 99;  /* age++ → 100 → mesh emit */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 0.5f);

    scene1_particles_tick();

    /* age 100 is still < 0x65 (101), so pos += vel fires; pos.x = 1.5. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 1.5f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    /* Mesh emit fired at the post-drift pos. */
    T_ASSERT(g_scene1_mesh_emit_trace_count == 1);
    if (fabsf(g_scene1_mesh_emit_trace[0].x - 1.5f) > 1e-6f)
        T_FAIL("mesh emit.x = %f", (double)g_scene1_mesh_emit_trace[0].x);
    T_ASSERT(g_scene1_mesh_emit_trace[0].slot == 1);
    return 0;
}

int test_particles_tick_type_6e_no_drift_past_101(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x6e;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 101;  /* age++ → 102 → drift OFF */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_POS_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 0.5f);

    scene1_particles_tick();

    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 1.0f)
        T_FAIL("pos.x must not drift when age >= 101");
    T_ASSERT(g_scene1_mesh_emit_trace_count == 0);
    return 0;
}

int test_particles_tick_type_6e_kills_at_0x74(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x6e;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0x73;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x6d — drift + per-particle gravity = baseline.y.  Kill at PARAM2. */
int test_particles_tick_type_6d_baseline_y_as_gravity(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x6d;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 50;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y,  1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_BASE_Y, -0.05f); /* "gravity" */

    scene1_particles_tick();

    /* pos.y += 1.0 (original vel); vel.y = (1.0 + -0.05) * 0.97 = 0.9215. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) - 1.0f) > 1e-6f)
        T_FAIL("pos.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) - 0.9215f) > 1e-5f)
        T_FAIL("vel.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y));
    return 0;
}

int test_particles_tick_type_6d_kills_at_param2(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x6d;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 50;
    r[SCENE1_RECORDS_A_OFF_AGE]    = 49;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x6c — two-stage trajectory.  Engine L644-L673. */
int test_particles_tick_type_6c_early_stage_buoyancy(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x6c;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 5;  /* < 0x14 */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);

    scene1_particles_tick();

    /* age in [0, 19] → pos += vel; vel.y = 0 * 0.98 + 0.0196 = 0.0196.
     * vel.x is NOT damped in the early stage. */
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 1.0f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_X) != 1.0f)
        T_FAIL("vel.x must not damp in early stage");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) - 0.0196f) > 1e-6f)
        T_FAIL("vel.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y));
    return 0;
}

int test_particles_tick_type_6c_kills_at_600(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x6c;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 599;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

/* type 0x1d — anchor to table-B record's pos; kill at 0xd.
 * Engine L1187-L1198. */
int test_particles_tick_type_1d_anchors_to_table_b(void)
{
    reset_world();
    /* Populate table-B slot 7 with a pos vector. */
    int b_slot = 7;
    int32_t *b = &g_scene1_records_b[b_slot * SCENE1_RECORDS_B_STRIDE];
    float anchor_x = 12.0f, anchor_y = 34.0f, anchor_z = 56.0f;
    memcpy(&b[SCENE1_RECORDS_B_OFF_POS_X], &anchor_x, sizeof anchor_x);
    memcpy(&b[SCENE1_RECORDS_B_OFF_POS_Y], &anchor_y, sizeof anchor_y);
    memcpy(&b[SCENE1_RECORDS_B_OFF_POS_Z], &anchor_z, sizeof anchor_z);

    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x1d;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = b_slot;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, -2.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Z, 0.5f);

    scene1_particles_tick();

    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) - 13.0f) > 1e-6f)
        T_FAIL("pos.x = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y) - 32.0f) > 1e-6f)
        T_FAIL("pos.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Y));
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z) - 56.5f) > 1e-6f)
        T_FAIL("pos.z = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_Z));
    return 0;
}

int test_particles_tick_type_1d_kills_at_0xd(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x1d;
    r[SCENE1_RECORDS_A_OFF_AGE]  = 0xc;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = -1;  /* OOB → no anchor read */
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

int test_particles_tick_type_1d_oob_param2_safe(void)
{
    /* Engine doesn't bounds-check; our port treats OOB as "no anchor".
     * Verify no crash and pos = vel-only. */
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE]   = 0x1d;
    r[SCENE1_RECORDS_A_OFF_PARAM2] = 99999;  /* OOB */
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 7.0f);
    scene1_particles_tick();
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 7.0f)
        T_FAIL("OOB PARAM2 should leave pos = vel-only");
    return 0;
}

/* type 0x2d — wraps decay_drift_grav_pre with (0.97, +0.002, 0x40). */
int test_particles_tick_type_2d_buoyant_drift(void)
{
    reset_world();
    int slot = 0;
    int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
    r[SCENE1_RECORDS_A_OFF_TYPE] = 0x2d;
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_X, 1.0f);
    slot_write_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y, 0.0f);

    scene1_particles_tick();

    /* pos.x += 1.0; vel.y = (0 + 0.002) * 0.97 = 0.00194. */
    if (slot_read_f(slot, SCENE1_RECORDS_A_OFF_POS_X) != 1.0f)
        T_FAIL("pos.x didn't advance");
    if (fabsf(slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y) - 0.00194f) > 1e-6f)
        T_FAIL("vel.y = %f", (double)slot_read_f(slot, SCENE1_RECORDS_A_OFF_VEL_Y));

    /* Kill at 0x40. */
    r[SCENE1_RECORDS_A_OFF_AGE] = 0x3f;
    scene1_particles_tick();
    T_ASSERT(slot_read_i(slot, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}
