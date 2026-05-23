/*
 * test_scene1_spawn.c — unit tests for the C8i.1 spawn skeleton.
 *
 * Covers:
 *   - Trace ring (kept as opt-in instrumentation from the pre-C8i stub)
 *   - Outer slot-scan: first-free, busy-skip, table-full
 *   - Common preamble: pos / vel=0 / rot=0 / age=0 / param2=0 / type / scale
 *   - Per-type init for the 3 C8i.1 anchor types: 0x60, 0x20, 0x66
 *   - Unimplemented types record trace but do not commit a slot
 *
 * Resets g_scene1_records_a between tests so slot state is clean.
 */

#include "t.h"

#include <string.h>

#include "rng.h"
#include "scene1_records.h"
#include "scene1_spawn.h"

static void reset_records_and_trace(void)
{
    memset(g_scene1_records_a, 0, sizeof g_scene1_records_a);
    scene1_records_reset(1);
    scene1_spawn_trace_reset();
    scene1_mesh_emit_trace_reset();
}

static float slot_read_f(int i, int off)
{
    int32_t v = g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int32_t slot_read_i(int i, int off)
{
    return g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE + off];
}

/* ─── trace API (pre-C8i instrumentation that survives the port) ──── */

int test_scene1_spawn_first_call_recorded(void)
{
    reset_records_and_trace();
    T_ASSERT(g_scene1_spawn_trace_count == 0);

    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x21, 1.0f, 0);

    T_ASSERT(g_scene1_spawn_trace_count == 1);
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == 0);
    T_ASSERT(g_scene1_spawn_trace[0].x == 1.0f);
    T_ASSERT(g_scene1_spawn_trace[0].y == 2.0f);
    T_ASSERT(g_scene1_spawn_trace[0].z == 3.0f);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0x21);
    T_ASSERT(g_scene1_spawn_trace[0].scale == 1.0f);
    T_ASSERT(g_scene1_spawn_trace[0].param7 == 0);
    return 0;
}

int test_scene1_spawn_ring_wraps(void)
{
    reset_records_and_trace();
    /* Fill the ring + one extra; the extra overwrites slot 0. */
    for (int i = 0; i < SCENE1_SPAWN_TRACE_CAPACITY + 1; i++) {
        scene1_spawn(i, (float)i, 0.0f, 0.0f, i, 1.0f, 0);
    }
    T_ASSERT(g_scene1_spawn_trace_count == SCENE1_SPAWN_TRACE_CAPACITY + 1);
    /* Slot 0 was overwritten by the wrapping call (i == capacity). */
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == SCENE1_SPAWN_TRACE_CAPACITY);
    /* Slot 1 still holds the original i==1 call. */
    T_ASSERT(g_scene1_spawn_trace[1].slot_hint == 1);
    return 0;
}

int test_scene1_spawn_reset_clears(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 99.0f, 99.0f, 99.0f, 0x99, 9.0f, 9);
    scene1_spawn_trace_reset();
    T_ASSERT(g_scene1_spawn_trace_count == 0);
    T_ASSERT(g_scene1_spawn_trace[0].slot_hint == 0);
    T_ASSERT(g_scene1_spawn_trace[0].x == 0.0f);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0);
    return 0;
}

/* ─── C8i.1: common preamble + outer slot scan ────────────────────── */

int test_scene1_spawn_type_60_commits_slot_zero(void)
{
    reset_records_and_trace();

    scene1_spawn(7, 1.5f, 2.5f, 3.5f, 0x60, 0.25f, 0);

    /* Slot 0 was sentinel-empty → first-fit lands here. */
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE) == 0x60);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AUX_18) == 7);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X) == 1.5f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y) == 2.5f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z) == 3.5f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z) == 0.0f);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE) == 0);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2) == 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_SCALE) == 0.25f);

    /* Subsequent slots remain sentinel-empty — type 0x60 spawns 1 only. */
    T_ASSERT(slot_read_i(1, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    return 0;
}

int test_scene1_spawn_busy_slots_skipped(void)
{
    reset_records_and_trace();
    /* Pre-commit slots 0..4 with arbitrary live type. */
    for (int i = 0; i < 5; i++) {
        g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE
                           + SCENE1_RECORDS_A_OFF_TYPE] = 0x99;
    }

    scene1_spawn(0, 10.0f, 20.0f, 30.0f, 0x60, 1.0f, 0);

    /* Should land on slot 5. */
    T_ASSERT(slot_read_i(5, SCENE1_RECORDS_A_OFF_TYPE) == 0x60);
    T_ASSERT(slot_read_f(5, SCENE1_RECORDS_A_OFF_POS_X) == 10.0f);
    /* Slots 0..4 untouched. */
    for (int i = 0; i < 5; i++) {
        T_ASSERT(slot_read_i(i, SCENE1_RECORDS_A_OFF_TYPE) == 0x99);
    }
    return 0;
}

int test_scene1_spawn_full_table_no_crash(void)
{
    reset_records_and_trace();
    /* Mark every slot busy. */
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE
                           + SCENE1_RECORDS_A_OFF_TYPE] = 0x42;
    }

    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x60, 1.0f, 0);

    /* No slot was overwritten. */
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        T_ASSERT(slot_read_i(i, SCENE1_RECORDS_A_OFF_TYPE) == 0x42);
    }
    /* But trace still recorded. */
    T_ASSERT(g_scene1_spawn_trace_count == 1);
    return 0;
}

int test_scene1_spawn_unimplemented_type_no_commit(void)
{
    reset_records_and_trace();

    scene1_spawn(0, 5.0f, 5.0f, 5.0f, 0x21, 1.0f, 0);

    /* No slot committed — slot 0 still sentinel-empty. */
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE) == -1);
    /* Trace still records the call. */
    T_ASSERT(g_scene1_spawn_trace_count == 1);
    T_ASSERT(g_scene1_spawn_trace[0].type == 0x21);
    return 0;
}

/* ─── C8i.1: per-type init bodies ─────────────────────────────────── */

int test_scene1_spawn_type_20_age_zero(void)
{
    reset_records_and_trace();
    /* Pre-poison age to detect that init_type_20 writes it. */
    g_scene1_records_a[0 * SCENE1_RECORDS_A_STRIDE
                       + SCENE1_RECORDS_A_OFF_AGE] = 0x7777;
    /* Re-arm the sentinel since the poison overwrote nothing relevant
     * but make extra sure slot 0 is free for the spawn. */
    g_scene1_records_a[0 * SCENE1_RECORDS_A_STRIDE
                       + SCENE1_RECORDS_A_OFF_TYPE] = -1;

    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x20, 1.0f, 0);

    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE) == 0x20);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE) == 0);
    return 0;
}

int test_scene1_spawn_type_66_velocity_down(void)
{
    reset_records_and_trace();

    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x66, 1.0f, 0);

    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE) == 0x66);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z) == -1.0f);
    /* PARAM1 (random life cap) lands in [20, 119]. */
    int32_t life = slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1);
    T_ASSERT(life >= 20);
    T_ASSERT(life <= 119);
    return 0;
}

int test_scene1_spawn_type_66_param1_distribution(void)
{
    /* Spawn many type-0x66 particles into a clean table; verify PARAM1
     * actually varies (i.e. the PRNG is hooked up).  Not testing
     * uniformity — just that we see at least two distinct values. */
    reset_records_and_trace();
    int values[16];
    for (int i = 0; i < 16; i++) {
        /* Re-sentinel slot 0 between calls so each spawn lands there. */
        g_scene1_records_a[0 * SCENE1_RECORDS_A_STRIDE
                           + SCENE1_RECORDS_A_OFF_TYPE] = -1;
        scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x66, 1.0f, 0);
        values[i] = slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1);
    }
    int distinct = 0;
    for (int i = 0; i < 16; i++) {
        int dup = 0;
        for (int j = 0; j < i; j++) if (values[j] == values[i]) dup = 1;
        if (!dup) distinct++;
    }
    T_ASSERT(distinct >= 2);
    return 0;
}

/* ─── C8i.2: radial-burst family ──────────────────────────────────── */

/* Counts how many slots have TYPE == want_type after a single spawn
 * call into a clean table. */
static int count_committed_slots(int want_type)
{
    int n = 0;
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        if (slot_read_i(i, SCENE1_RECORDS_A_OFF_TYPE) == want_type) n++;
    }
    return n;
}

/* Returns the index of the first slot whose TYPE != want_type, i.e.
 * the first "unused" slot after a burst.  Used to verify the burst
 * stopped at the expected count and didn't overflow further. */
static int first_unused_after(int want_type)
{
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        if (slot_read_i(i, SCENE1_RECORDS_A_OFF_TYPE) != want_type) return i;
    }
    return SCENE1_RECORDS_A_COUNT;
}

static int spawn_burst_count_is(int type, int want_count)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, type, 1.0f, 0);
    T_ASSERT_EQ_I(count_committed_slots(type), want_count);
    T_ASSERT_EQ_I(first_unused_after(type), want_count);
    return 0;
}

int test_scene1_spawn_type_1_commits_8(void)   { return spawn_burst_count_is(1,    8);   }
int test_scene1_spawn_type_2_commits_8(void)   { return spawn_burst_count_is(2,    8);   }
int test_scene1_spawn_type_3_commits_8(void)   { return spawn_burst_count_is(3,    8);   }
int test_scene1_spawn_type_52_commits_8(void)  { return spawn_burst_count_is(0x52, 8);   }
int test_scene1_spawn_type_5e_commits_8(void)  { return spawn_burst_count_is(0x5e, 8);   }
int test_scene1_spawn_type_65_commits_8(void)  { return spawn_burst_count_is(0x65, 8);   }
int test_scene1_spawn_type_92_commits_1(void)  { return spawn_burst_count_is(0x92, 1);   }
int test_scene1_spawn_type_79_commits_128(void){ return spawn_burst_count_is(0x79, 128); }
int test_scene1_spawn_type_5d_commits_45(void) { return spawn_burst_count_is(0x5d, 45);  }

int test_scene1_spawn_type_52_vy_positive_bias(void)
{
    /* Engine: vy = u * SCALE * 1.5 (always >= 0). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x52, 1.0f, 0);
    for (int i = 0; i < 8; i++) {
        T_ASSERT(slot_read_f(i, SCENE1_RECORDS_A_OFF_VEL_Y) >= 0.0f);
    }
    return 0;
}

int test_scene1_spawn_type_1_vy_can_be_negative(void)
{
    /* Engine: vy = (u - 0.5) * SCALE * 3 (signed).  Over 8 samples
     * we expect at least one negative — flaky in principle but with
     * an 8-sample sweep the false-negative odds are 2^-8 ≈ 0.4%. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 1, 1.0f, 0);
    int saw_neg = 0;
    for (int i = 0; i < 8; i++) {
        if (slot_read_f(i, SCENE1_RECORDS_A_OFF_VEL_Y) < 0.0f) saw_neg = 1;
    }
    T_ASSERT(saw_neg);
    return 0;
}

int test_scene1_spawn_type_92_vy_negative(void)
{
    /* Engine: vy = (u + 2.5) * SCALE * -0.2  → always in [-0.7, -0.5]*SCALE. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x92, 1.0f, 0);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    T_ASSERT(vy <= -0.5f);
    T_ASSERT(vy >= -0.7f);
    return 0;
}

int test_scene1_spawn_type_92_param_ranges(void)
{
    /* Engine: PARAM1 = rng%1000 (0..999); PARAM2 = rng%100+100 (100..199). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x92, 1.0f, 0);
    int32_t p1 = slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1);
    int32_t p2 = slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2);
    T_ASSERT(p1 >= 0);
    T_ASSERT(p1 <= 999);
    T_ASSERT(p2 >= 100);
    T_ASSERT(p2 <= 199);
    /* Rotation seeds should be in [0, 2pi]. */
    float rx = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X);
    float ry = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y);
    float rz = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z);
    T_ASSERT(rx >= 0.0f && rx <= 6.2831856f);
    T_ASSERT(ry >= 0.0f && ry <= 6.2831856f);
    T_ASSERT(rz >= 0.0f && rz <= 6.2831856f);
    return 0;
}

int test_scene1_spawn_type_79_age_stagger(void)
{
    /* Engine: AGE = (int)local_8 / -2  →  0,0,-1,-1,-2,-2,...,-63,-63. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x79, 1.0f, 0);
    for (int k = 0; k < 128; k++) {
        int32_t want = k / -2;
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE), want);
    }
    return 0;
}

int test_scene1_spawn_type_5d_age_and_param1_negate_index(void)
{
    /* Engine: AGE = PARAM1 = -local_8  →  slot 0:0, slot 1:-1, ..., slot 44:-44. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x5d, 1.0f, 0);
    for (int k = 0; k < 45; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE),    -k);
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM1), -k);
    }
    return 0;
}

int test_scene1_spawn_type_5d_pos_uses_4x_velocity(void)
{
    /* Engine line 282-284: pos = vel * 4.0 + (param2,3,4).  Verify the
     * multiplier matches by re-deriving from the recorded vel + spawn pos. */
    reset_records_and_trace();
    scene1_spawn(0, 10.0f, 20.0f, 30.0f, 0x5d, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float py = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y);
    float pz = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);
    T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ vx * 4.0f + 10.0f });
    T_ASSERT_EQ_I(*(int32_t *)&py, *(int32_t *)&(float){ vy * 4.0f + 20.0f });
    T_ASSERT_EQ_I(*(int32_t *)&pz, *(int32_t *)&(float){ vz * 4.0f + 30.0f });
    return 0;
}

int test_scene1_spawn_type_1_pos_uses_3x_velocity(void)
{
    /* Group A uses vel * 3.0 nudge.  Sanity check across 8 slots. */
    reset_records_and_trace();
    scene1_spawn(0, 100.0f, 200.0f, 300.0f, 1, 1.0f, 0);
    for (int i = 0; i < 8; i++) {
        float vx = slot_read_f(i, SCENE1_RECORDS_A_OFF_VEL_X);
        float vy = slot_read_f(i, SCENE1_RECORDS_A_OFF_VEL_Y);
        float vz = slot_read_f(i, SCENE1_RECORDS_A_OFF_VEL_Z);
        float px = slot_read_f(i, SCENE1_RECORDS_A_OFF_POS_X);
        float py = slot_read_f(i, SCENE1_RECORDS_A_OFF_POS_Y);
        float pz = slot_read_f(i, SCENE1_RECORDS_A_OFF_POS_Z);
        T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ vx * 3.0f + 100.0f });
        T_ASSERT_EQ_I(*(int32_t *)&py, *(int32_t *)&(float){ vy * 3.0f + 200.0f });
        T_ASSERT_EQ_I(*(int32_t *)&pz, *(int32_t *)&(float){ vz * 3.0f + 300.0f });
    }
    return 0;
}

int test_scene1_spawn_type_65_halves_magnitude(void)
{
    /* Type 0x65 multiplies mag by 0.5 vs type 1 (same body otherwise).
     * Seed RNG to identical state for both, compare vel.x magnitude:
     * type 1 should be 2x type 0x65. */
    extern uint32_t g_rng_seed;  /* from rng.h via scene1_spawn.h transitively */
    uint32_t saved = g_rng_seed;

    reset_records_and_trace();
    g_rng_seed = 0xdeadbeef;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 1, 1.0f, 0);
    float vx_1 = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);

    reset_records_and_trace();
    g_rng_seed = 0xdeadbeef;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x65, 1.0f, 0);
    float vx_65 = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);

    g_rng_seed = saved;

    /* Reproducibility check: type 1's first vel.x with seed dead =
     * sin(angle)*1.0*(u1+1.2).  Type 0x65's = sin(angle)*1.0*(u1+1.2)*0.5
     * with the same u1 and angle (same RNG sequence).  So vx_1 == 2*vx_65. */
    T_ASSERT(vx_65 != 0.0f);
    float ratio = vx_1 / vx_65;
    T_ASSERT(ratio > 1.99f);
    T_ASSERT(ratio < 2.01f);
    return 0;
}

/* ─── mesh-emit stub (FUN_0044b0f3 placeholder) ────────────────────── */

int test_scene1_mesh_emit_records_call(void)
{
    scene1_mesh_emit_trace_reset();
    T_ASSERT(g_scene1_mesh_emit_trace_count == 0);

    scene1_mesh_emit(1.0f, 2.0f, 3.0f, 42, 1, 0);

    T_ASSERT(g_scene1_mesh_emit_trace_count == 1);
    T_ASSERT(g_scene1_mesh_emit_trace[0].x == 1.0f);
    T_ASSERT(g_scene1_mesh_emit_trace[0].mesh_id == 42);
    T_ASSERT(g_scene1_mesh_emit_trace[0].slot == 1);
    return 0;
}

int test_scene1_mesh_emit_reset_clears(void)
{
    scene1_mesh_emit(99.0f, 0.0f, 0.0f, 99, 9, 9);
    scene1_mesh_emit_trace_reset();
    T_ASSERT(g_scene1_mesh_emit_trace_count == 0);
    T_ASSERT(g_scene1_mesh_emit_trace[0].x == 0.0f);
    T_ASSERT(g_scene1_mesh_emit_trace[0].mesh_id == 0);
    return 0;
}
