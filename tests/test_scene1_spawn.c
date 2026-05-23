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

/* ─── C8i.3a: world-anchored radial variants ──────────────────────── */

int test_scene1_spawn_type_69_commits_128(void)  { return spawn_burst_count_is(0x69, 128); }
int test_scene1_spawn_type_68_commits_1(void)    { return spawn_burst_count_is(0x68, 1);   }
int test_scene1_spawn_type_73_commits_2(void)    { return spawn_burst_count_is(0x73, 2);   }
int test_scene1_spawn_type_77_commits_2(void)    { return spawn_burst_count_is(0x77, 2);   }
int test_scene1_spawn_type_99_commits_1(void)    { return spawn_burst_count_is(99,   1);   }
int test_scene1_spawn_type_78_commits_1(void)    { return spawn_burst_count_is(0x78, 1);   }

int test_scene1_spawn_type_69_age_stagger(void)
{
    /* Engine line 140 (shared LAB_004481fa): AGE = local_8 / -2 → same
     * stagger as 0x79.  Verify across all 128 spawns. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x69, 1.0f, 0);
    for (int k = 0; k < 128; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE), k / -2);
    }
    return 0;
}

int test_scene1_spawn_type_69_pos_uses_3x_velocity(void)
{
    /* Engine line 134-136: pos = vel * 3 + (x,y,z).  Sample slot 0. */
    reset_records_and_trace();
    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x69, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float py = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y);
    float pz = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);
    T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ vx * 3.0f + 1.0f });
    T_ASSERT_EQ_I(*(int32_t *)&py, *(int32_t *)&(float){ vy * 3.0f + 2.0f });
    T_ASSERT_EQ_I(*(int32_t *)&pz, *(int32_t *)&(float){ vz * 3.0f + 3.0f });
    return 0;
}

int test_scene1_spawn_type_68_anchor_back_48x(void)
{
    /* Engine line 172-174: pos = (x,y,z) - vel * 48. */
    reset_records_and_trace();
    scene1_spawn(0, 10.0f, 20.0f, 30.0f, 0x68, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float py = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y);
    float pz = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);
    T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ 10.0f - vx * 48.0f });
    T_ASSERT_EQ_I(*(int32_t *)&py, *(int32_t *)&(float){ 20.0f - vy * 48.0f });
    T_ASSERT_EQ_I(*(int32_t *)&pz, *(int32_t *)&(float){ 30.0f - vz * 48.0f });
    return 0;
}

int test_scene1_spawn_type_73_param7_drives_angle(void)
{
    /* Engine line 192: angle = (float)param_7 / 65536.0.  Two calls with
     * param_7 == 0 and param_7 == 0x10000 (= 1.0 radian after divide)
     * should produce different vel.x (sin) and vel.z (cos). */
    extern uint32_t g_rng_seed;
    uint32_t saved = g_rng_seed;

    reset_records_and_trace();
    g_rng_seed = 0xc0ffee;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x73, 1.0f, 0);
    float vx_a = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vz_a = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);

    reset_records_and_trace();
    g_rng_seed = 0xc0ffee;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x73, 1.0f, 0x10000);
    float vx_b = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vz_b = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);

    g_rng_seed = saved;

    /* Different angle → different sin and cos. */
    T_ASSERT(vx_a != vx_b);
    T_ASSERT(vz_a != vz_b);
    /* vel.y is always 0 (engine line 195). */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y) == 0.0f);
    return 0;
}

int test_scene1_spawn_type_77_same_body_as_73(void)
{
    /* Engine lines 186-215: 0x73 and 0x77 share the same handler.  Seed
     * identically, spawn once each into a clean table — slot 0 should
     * match across both types. */
    extern uint32_t g_rng_seed;
    uint32_t saved = g_rng_seed;

    reset_records_and_trace();
    g_rng_seed = 0xfeedface;
    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x73, 0.5f, 0x8000);
    int32_t snap_73[19];
    for (int j = 0; j < 19; j++) snap_73[j] = slot_read_i(0, j);

    reset_records_and_trace();
    g_rng_seed = 0xfeedface;
    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x77, 0.5f, 0x8000);
    int32_t snap_77[19];
    for (int j = 0; j < 19; j++) snap_77[j] = slot_read_i(0, j);

    g_rng_seed = saved;

    /* All fields except TYPE (offset 12) must match. */
    for (int j = 0; j < 19; j++) {
        if (j == SCENE1_RECORDS_A_OFF_TYPE) continue;
        T_ASSERT_EQ_I(snap_73[j], snap_77[j]);
    }
    return 0;
}

int test_scene1_spawn_type_99_anchor_back_40x_and_base(void)
{
    /* Engine lines 232-237: pos = param - vel*40; base = vel*-40 (= pos
     * displacement, used by the integrator's recovery handler). */
    reset_records_and_trace();
    scene1_spawn(0, 100.0f, 200.0f, 300.0f, 99, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float py = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y);
    float pz = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);
    float bx = slot_read_f(0, SCENE1_RECORDS_A_OFF_BASE_X);
    float by = slot_read_f(0, SCENE1_RECORDS_A_OFF_BASE_Y);
    float bz = slot_read_f(0, SCENE1_RECORDS_A_OFF_BASE_Z);
    T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ 100.0f - vx * 40.0f });
    T_ASSERT_EQ_I(*(int32_t *)&py, *(int32_t *)&(float){ 200.0f - vy * 40.0f });
    T_ASSERT_EQ_I(*(int32_t *)&pz, *(int32_t *)&(float){ 300.0f - vz * 40.0f });
    T_ASSERT_EQ_I(*(int32_t *)&bx, *(int32_t *)&(float){ vx * -40.0f });
    T_ASSERT_EQ_I(*(int32_t *)&by, *(int32_t *)&(float){ vy * -40.0f });
    T_ASSERT_EQ_I(*(int32_t *)&bz, *(int32_t *)&(float){ vz * -40.0f });
    /* PARAM1 (random life cap) in [20, 119]; AGE = 0. */
    int32_t life = slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1);
    T_ASSERT(life >= 20 && life <= 119);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE) == 0);
    /* PARAM2 stays at preamble's 0. */
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2) == 0);
    return 0;
}

int test_scene1_spawn_type_78_sets_param2_param7(void)
{
    /* Engine line 263: (DAT_069b2fc4)[slot * 0x25] = param_7  →  PARAM2. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x78, 1.0f, 0xdeadbeef);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2),
                  (int32_t)0xdeadbeef);
    return 0;
}

int test_scene1_spawn_type_78_matches_99_when_param7_zero(void)
{
    /* 0x78 is 99's body + PARAM2 = param_7.  With param_7 = 0 the slot
     * should be byte-equal modulo TYPE.  Seed identical RNG. */
    extern uint32_t g_rng_seed;
    uint32_t saved = g_rng_seed;

    reset_records_and_trace();
    g_rng_seed = 0xa5a5a5a5;
    scene1_spawn(0, 5.0f, 10.0f, 15.0f, 99, 2.0f, 0);
    int32_t snap_99[19];
    for (int j = 0; j < 19; j++) snap_99[j] = slot_read_i(0, j);

    reset_records_and_trace();
    g_rng_seed = 0xa5a5a5a5;
    scene1_spawn(0, 5.0f, 10.0f, 15.0f, 0x78, 2.0f, 0);
    int32_t snap_78[19];
    for (int j = 0; j < 19; j++) snap_78[j] = slot_read_i(0, j);

    g_rng_seed = saved;

    for (int j = 0; j < 19; j++) {
        if (j == SCENE1_RECORDS_A_OFF_TYPE) continue;
        T_ASSERT_EQ_I(snap_99[j], snap_78[j]);
    }
    return 0;
}

/* ─── C8i.3b: mixed-shape multi-particle radials ──────────────────── */

int test_scene1_spawn_type_53_commits_1(void)   { return spawn_burst_count_is(0x53, 1);  }
int test_scene1_spawn_type_4a_commits_8(void)   { return spawn_burst_count_is(0x4a, 8);  }
int test_scene1_spawn_type_43_commits_24(void)  { return spawn_burst_count_is(0x43, 24); }
int test_scene1_spawn_type_97_commits_64(void)  { return spawn_burst_count_is(0x97, 64); }
int test_scene1_spawn_type_96_commits_64(void)  { return spawn_burst_count_is(0x96, 64); }
int test_scene1_spawn_type_40_commits_8(void)   { return spawn_burst_count_is(0x40, 8);  }
int test_scene1_spawn_type_4e_commits_3(void)   { return spawn_burst_count_is(0x4e, 3);  }

int test_scene1_spawn_type_36_param7_drives_count(void)
{
    /* Engine LAB_0044aa47: local_8 + 1 == param_7 → exactly param_7
     * particles.  Try a few different values. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x36, 1.0f, 5);
    T_ASSERT_EQ_I(count_committed_slots(0x36), 5);

    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x36, 1.0f, 17);
    T_ASSERT_EQ_I(count_committed_slots(0x36), 17);
    return 0;
}

int test_scene1_spawn_type_74_param7_drives_count(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x74, 1.0f, 9);
    T_ASSERT_EQ_I(count_committed_slots(0x74), 9);
    return 0;
}

int test_scene1_spawn_type_36_param7_zero_clamps_to_one(void)
{
    /* param_7 <= 0 hits a signed-wrap path on the engine.  Our clamp
     * spawns exactly 1 to keep the loop bounded. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x36, 1.0f, 0);
    T_ASSERT_EQ_I(count_committed_slots(0x36), 1);
    return 0;
}

int test_scene1_spawn_type_53_positive_vy(void)
{
    /* Engine: vy = (u + 1.5) * scale * 3.0 → always >= 1.5 * scale * 3
     * = 4.5 * scale (with u >= 0). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x53, 1.0f, 0);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    T_ASSERT(vy >= 4.5f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z) == 0.0f);
    return 0;
}

int test_scene1_spawn_type_4a_param1_eq_param7(void)
{
    /* Engine line 331: PARAM1 = param_7 (across all 8 spawns). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4a, 1.0f, 0xc0debabe);
    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM1),
                      (int32_t)0xc0debabe);
    }
    return 0;
}

int test_scene1_spawn_type_4a_age_stagger(void)
{
    /* Engine line 332: AGE = local_8 * -4 → 0, -4, -8, ..., -28. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4a, 1.0f, 0);
    for (int k = 0; k < 8; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE), k * -4);
    }
    return 0;
}

int test_scene1_spawn_type_4a_pos_exact(void)
{
    /* Engine lines 328-330: pos = (param_2, param_3, param_4) exact (no
     * vel nudge).  Verify across all 8 spawns. */
    reset_records_and_trace();
    scene1_spawn(0, 12.5f, 34.5f, 56.5f, 0x4a, 1.0f, 0);
    for (int k = 0; k < 8; k++) {
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_X) == 12.5f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Y) == 34.5f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Z) == 56.5f);
    }
    return 0;
}

int test_scene1_spawn_type_43_pos_y_anchored_below(void)
{
    /* Engine line 351: pos.y = y - scale * 8. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 100.0f, 0.0f, 0x43, 2.0f, 0);
    /* For all 24 spawns, pos.y == 100.0 - 2*8 == 84.0. */
    for (int k = 0; k < 24; k++) {
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Y) == 84.0f);
    }
    return 0;
}

int test_scene1_spawn_type_43_rot_y_matches_angle(void)
{
    /* Engine line 347 + 349: rot.y = angle (= u2*2π), and pos uses
     * sin(angle)*fVar1 + x.  Verify rot.y is what makes pos.x consistent. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x43, 1.0f, 0);
    for (int k = 0; k < 24; k++) {
        float angle = slot_read_f(k, SCENE1_RECORDS_A_OFF_ROT_Y);
        T_ASSERT(angle >= 0.0f);
        T_ASSERT(angle <= 6.2831856f);
    }
    /* rot.x = rot.z = 0 (engine lines 346 + 348). */
    for (int k = 0; k < 24; k++) {
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_ROT_Z) == 0.0f);
    }
    return 0;
}

int test_scene1_spawn_type_97_scale_halved(void)
{
    /* Engine: scale *= (u+0.5)/2 → range [0.25, 0.75] × original (= 1).
     * With many samples, all slots' SCALE must lie in that range. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x97, 1.0f, 0);
    for (int k = 0; k < 64; k++) {
        float s = slot_read_f(k, SCENE1_RECORDS_A_OFF_SCALE);
        T_ASSERT(s >= 0.25f);
        T_ASSERT(s <= 0.75f);
    }
    return 0;
}

int test_scene1_spawn_type_96_camera_drives_xz_bend(void)
{
    /* Engine: vel.x += sin(c*2π/8)*0.2;  vel.z += cos(c*2π/8)*0.2.
     * Compare two spawns with the same RNG but different camera
     * counters; verify vel.x and vel.z change. */
    extern uint32_t g_rng_seed;
    uint32_t saved = g_rng_seed;
    int saved_c = g_scene1_spawn_camera_counter_948;

    reset_records_and_trace();
    g_rng_seed = 0xb16b00b5;
    g_scene1_spawn_camera_counter_948 = 0;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x96, 1.0f, 0);
    float vx_0 = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vz_0 = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);

    reset_records_and_trace();
    g_rng_seed = 0xb16b00b5;
    g_scene1_spawn_camera_counter_948 = 2;  /* 2/8 turn → 90° rotation */
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x96, 1.0f, 0);
    float vx_2 = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vz_2 = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);

    g_rng_seed = saved;
    g_scene1_spawn_camera_counter_948 = saved_c;

    T_ASSERT(vx_0 != vx_2);
    T_ASSERT(vz_0 != vz_2);
    return 0;
}

int test_scene1_spawn_type_96_scale_full_range(void)
{
    /* Engine: scale *= (u+0.5) → range [0.5, 1.5] × original. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x96, 1.0f, 0);
    for (int k = 0; k < 64; k++) {
        float s = slot_read_f(k, SCENE1_RECORDS_A_OFF_SCALE);
        T_ASSERT(s >= 0.5f);
        T_ASSERT(s <= 1.5f);
    }
    return 0;
}

int test_scene1_spawn_type_40_ring_angle_steps(void)
{
    /* Engine: angle = local_8 * 2π/8.
     *   k=0 → angle 0:    sin = 0,        cos = +1
     *   k=2 → angle π/2:  sin = +1,       cos = 0
     *   k=4 → angle π:    sin ≈ 0,        cos = -1
     *   k=6 → angle 3π/2: sin = -1,       cos = 0
     *
     * Since u1 varies per-slot, we can't pin exact values, but the SIGN
     * of vx/vz is deterministic for each k (modulo near-zero noise from
     * sinf(π) etc., which we tolerate via a small-epsilon "near zero"
     * test). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x40, 1.0f, 0);
    const float eps = 1e-4f;
    /* k=0: vx == sinf(0)*... = 0.0f exactly; vz > 0. */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z) > 0.0f);
    /* k=2: vx > 0 (sin(π/2)=1), vz ≈ 0 (cosf(π/2) is tiny). */
    T_ASSERT(slot_read_f(2, SCENE1_RECORDS_A_OFF_VEL_X) > 0.0f);
    {
        float vz = slot_read_f(2, SCENE1_RECORDS_A_OFF_VEL_Z);
        T_ASSERT(vz > -eps && vz < eps);
    }
    /* k=4: vx ≈ 0 (sinf(π) is tiny but nonzero); vz < 0. */
    {
        float vx = slot_read_f(4, SCENE1_RECORDS_A_OFF_VEL_X);
        T_ASSERT(vx > -eps && vx < eps);
    }
    T_ASSERT(slot_read_f(4, SCENE1_RECORDS_A_OFF_VEL_Z) < 0.0f);
    /* k=6: vx < 0 (sin(3π/2)=-1), vz ≈ 0. */
    T_ASSERT(slot_read_f(6, SCENE1_RECORDS_A_OFF_VEL_X) < 0.0f);
    {
        float vz = slot_read_f(6, SCENE1_RECORDS_A_OFF_VEL_Z);
        T_ASSERT(vz > -eps && vz < eps);
    }
    return 0;
}

int test_scene1_spawn_type_40_vy_positive(void)
{
    /* Engine: vy = u * scale * 1.5 (always >= 0). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x40, 1.0f, 0);
    for (int k = 0; k < 8; k++) {
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_VEL_Y) >= 0.0f);
    }
    return 0;
}

int test_scene1_spawn_type_36_rescales_slot_scale(void)
{
    /* Engine line 496: slot.scale = (u + 1.0) * param_6 → [1.0, 2.0]
     * × param_6 = 2.0 → final scale in [2.0, 4.0]. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x36, 2.0f, 4);
    for (int k = 0; k < 4; k++) {
        float s = slot_read_f(k, SCENE1_RECORDS_A_OFF_SCALE);
        T_ASSERT(s >= 2.0f);
        T_ASSERT(s <= 4.0f);
    }
    /* PARAM1 in [0x10, 0x1f]. */
    for (int k = 0; k < 4; k++) {
        int32_t p = slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM1);
        T_ASSERT(p >= 0x10);
        T_ASSERT(p <= 0x1f);
    }
    return 0;
}

int test_scene1_spawn_type_4e_vel_xz_narrowed_vs_pos(void)
{
    /* Engine: vel.x = sin(angle)*scale*fVar1*0.1; pos.x = sin(angle)*
     * scale*fVar1 + x.  Ratio pos.x / vel.x should be ~10 (for the same
     * angle/u1), and pos.x - x should equal vel.x * 10 exactly modulo
     * fp rounding. */
    reset_records_and_trace();
    scene1_spawn(0, 100.0f, 200.0f, 300.0f, 0x4e, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float pz = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);
    T_ASSERT_EQ_I(*(int32_t *)&px,
                  *(int32_t *)&(float){ vx * 10.0f + 100.0f });
    T_ASSERT_EQ_I(*(int32_t *)&pz,
                  *(int32_t *)&(float){ vz * 10.0f + 300.0f });
    /* pos.y = y exactly. */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y) == 200.0f);
    return 0;
}

/* ─── C8i.3c: local_8-azimuth + chain pair ────────────────────────── */

int test_scene1_spawn_type_34_commits_24(void)  { return spawn_burst_count_is(0x34, 24); }
int test_scene1_spawn_type_35_commits_1(void)   { return spawn_burst_count_is(0x35, 1);  }
int test_scene1_spawn_type_2c_commits_32(void)  { return spawn_burst_count_is(0x2c, 32); }
int test_scene1_spawn_type_29_commits_14(void)  { return spawn_burst_count_is(0x29, 14); }
int test_scene1_spawn_type_32_commits_2(void)   { return spawn_burst_count_is(0x32, 2);  }
int test_scene1_spawn_type_4c_commits_1(void)   { return spawn_burst_count_is(0x4c, 1);  }
int test_scene1_spawn_type_55_commits_1(void)   { return spawn_burst_count_is(0x55, 1);  }
int test_scene1_spawn_type_4b_commits_3(void)   { return spawn_burst_count_is(0x4b, 3);  }
int test_scene1_spawn_type_57_commits_1(void)   { return spawn_burst_count_is(0x57, 1);  }
int test_scene1_spawn_type_3e_commits_4(void)   { return spawn_burst_count_is(0x3e, 4);  }

int test_scene1_spawn_type_33_param7_drives_count(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x33, 1.0f, 7);
    T_ASSERT_EQ_I(count_committed_slots(0x33), 7);
    return 0;
}

int test_scene1_spawn_type_4d_param7_drives_count(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4d, 1.0f, 11);
    T_ASSERT_EQ_I(count_committed_slots(0x4d), 11);
    return 0;
}

int test_scene1_spawn_type_51_param7_drives_count(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x51, 1.0f, 3);
    T_ASSERT_EQ_I(count_committed_slots(0x51), 3);
    return 0;
}

int test_scene1_spawn_type_34_age_stagger(void)
{
    /* Engine line 550: AGE = -count_index → 0, -1, ..., -23. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x34, 1.0f, 0);
    for (int k = 0; k < 24; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE), -k);
    }
    return 0;
}

int test_scene1_spawn_type_34_vel_x_equals_mag_times_scale(void)
{
    /* Engine line 542: vel.x = fVar1 * scale = (u1 + 1.2) * scale.
     * With scale=2.0, u1 in [0,1), vel.x in [2.4, 4.4). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x34, 2.0f, 0);
    for (int k = 0; k < 24; k++) {
        float vx = slot_read_f(k, SCENE1_RECORDS_A_OFF_VEL_X);
        T_ASSERT(vx >= 2.4f);
        T_ASSERT(vx < 4.4f);
    }
    return 0;
}

int test_scene1_spawn_type_34_pos_uses_anchor_back_24x(void)
{
    /* Engine lines 547-549: pos = (x,y,z) - vel * 24. */
    reset_records_and_trace();
    scene1_spawn(0, 100.0f, 200.0f, 300.0f, 0x34, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float py = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y);
    float pz = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);
    T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ 100.0f - vx * 24.0f });
    T_ASSERT_EQ_I(*(int32_t *)&py, *(int32_t *)&(float){ 200.0f - vy * 24.0f });
    T_ASSERT_EQ_I(*(int32_t *)&pz, *(int32_t *)&(float){ 300.0f - vz * 24.0f });
    return 0;
}

int test_scene1_spawn_type_35_pos_exact(void)
{
    /* Engine 0x35: pos = param - vel*24 with vel=0 (preamble). */
    reset_records_and_trace();
    scene1_spawn(0, 12.5f, 34.5f, 56.5f, 0x35, 1.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X) == 12.5f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Y) == 34.5f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z) == 56.5f);
    /* rot.x = 0 (engine line 554). */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
    /* rot.y, rot.z each in [0, 2π]. */
    float ry = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y);
    float rz = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z);
    T_ASSERT(ry >= 0.0f && ry <= 6.2831856f);
    T_ASSERT(rz >= 0.0f && rz <= 6.2831856f);
    return 0;
}

int test_scene1_spawn_type_2c_age_stagger(void)
{
    /* Engine line 584: AGE = -count_index. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x2c, 1.0f, 0);
    for (int k = 0; k < 32; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE), -k);
    }
    return 0;
}

int test_scene1_spawn_type_2c_pos_uses_20x_velocity(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x2c, 1.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float px = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    T_ASSERT_EQ_I(*(int32_t *)&px, *(int32_t *)&(float){ vx * 20.0f + 1.0f });
    return 0;
}

int test_scene1_spawn_type_29_pos_stays_at_param(void)
{
    /* Engine: 0x29 doesn't write pos — preamble pos = (param_2,3,4) stands. */
    reset_records_and_trace();
    scene1_spawn(0, 7.5f, 8.5f, 9.5f, 0x29, 1.0f, 0);
    /* Across all 14 spawns. */
    for (int k = 0; k < 14; k++) {
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_X) == 7.5f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Y) == 8.5f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Z) == 9.5f);
        /* AGE stays at preamble's 0. */
        T_ASSERT(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE) == 0);
    }
    return 0;
}

int test_scene1_spawn_type_29_vel_no_scale(void)
{
    /* Engine: vel.y = (u - 0.2) * 0.8 → range [-0.16, 0.64].  No scale
     * factor — passing scale=100 should not change the range. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x29, 100.0f, 0);
    for (int k = 0; k < 14; k++) {
        float vy = slot_read_f(k, SCENE1_RECORDS_A_OFF_VEL_Y);
        T_ASSERT(vy >= -0.16f);
        T_ASSERT(vy <= 0.64f);
    }
    return 0;
}

int test_scene1_spawn_type_32_rot_angle_steps(void)
{
    /* Engine: rot.x = count_index * π + π/2 → π/2 then 3π/2. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x32, 1.0f, 0);
    float r0 = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X);
    float r1 = slot_read_f(1, SCENE1_RECORDS_A_OFF_ROT_X);
    T_ASSERT(r0 == 1.5707964f);
    T_ASSERT(r1 == 3.1415927f + 1.5707964f);
    /* rot.z = π/2 for both. */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z) == 1.5707964f);
    T_ASSERT(slot_read_f(1, SCENE1_RECORDS_A_OFF_ROT_Z) == 1.5707964f);
    return 0;
}

int test_scene1_spawn_type_4c_vel_x_sign_alternates(void)
{
    /* Engine: if (count_index & 1) vel.x = -vel.x.  Only 1 particle per
     * spawn call, so verify by spawning twice (clearing slot in between). */
    reset_records_and_trace();
    /* Slot 0 → count_index 0 → positive. */
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4c, 1.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) > 0.0f);
    /* The 0x4c spawn count is 1, so the sign-alt logic isn't actually
     * exercised within a single call.  Test through 0x4b instead which
     * spawns 3. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4b, 1.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) > 0.0f);  /* k=0: +0.1 */
    T_ASSERT(slot_read_f(1, SCENE1_RECORDS_A_OFF_VEL_X) < 0.0f);  /* k=1: -0.2 */
    T_ASSERT(slot_read_f(2, SCENE1_RECORDS_A_OFF_VEL_X) > 0.0f);  /* k=2: +0.3 */
    return 0;
}

int test_scene1_spawn_type_4b_vel_x_count_driven(void)
{
    /* Engine line 666: fVar1 = (count_index + 1) * 0.1.
     *   k=0: |vel.x| = 0.1
     *   k=1: |vel.x| = 0.2
     *   k=2: |vel.x| = 0.3
     * Signs alternate. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4b, 1.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.1f);
    T_ASSERT(slot_read_f(1, SCENE1_RECORDS_A_OFF_VEL_X) == -0.2f);
    T_ASSERT(slot_read_f(2, SCENE1_RECORDS_A_OFF_VEL_X) == 0.3f);
    /* PARAM2 = count_index * 2 (engine line 674). */
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2) == 0);
    T_ASSERT(slot_read_i(1, SCENE1_RECORDS_A_OFF_PARAM2) == 2);
    T_ASSERT(slot_read_i(2, SCENE1_RECORDS_A_OFF_PARAM2) == 4);
    return 0;
}

int test_scene1_spawn_type_4b_scale_decays_per_particle(void)
{
    /* Engine line 671-672: slot.scale *= (1 - count_index * 0.2).
     *   k=0: scale *= 1.0    → 2.0
     *   k=1: scale *= 0.8    → 1.6
     *   k=2: scale *= 0.6    → 1.2  */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x4b, 2.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_SCALE) == 2.0f);
    T_ASSERT(slot_read_f(1, SCENE1_RECORDS_A_OFF_SCALE) == 1.6f);
    T_ASSERT(slot_read_f(2, SCENE1_RECORDS_A_OFF_SCALE) ==
             (float)(1.0f - 2 * 0.2f) * 2.0f);  /* 0.6 * 2.0 */
    return 0;
}

int test_scene1_spawn_type_57_param2_eq_param7(void)
{
    /* Engine via LAB_00448f57: PARAM2 = param_7. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x57, 1.0f, 0x12345678);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2),
                  (int32_t)0x12345678);
    return 0;
}

int test_scene1_spawn_type_57_rot_z_sign_on_param7(void)
{
    /* Engine: rot.z = (param_7 == 0) ? π/2 : -π/2. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x57, 1.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z) == 1.5707964f);

    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x57, 1.0f, 99);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z) == -1.5707964f);
    return 0;
}

int test_scene1_spawn_type_3e_rot_x_steps(void)
{
    /* Engine: rot.x = count_index * π + π/4 → π/4, 5π/4, 9π/4, 13π/4. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x3e, 1.0f, 0);
    for (int k = 0; k < 4; k++) {
        float want = (float)k * 3.1415927f + 0.7853982f;
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_ROT_X) == want);
    }
    return 0;
}

/* ─── C8i.3d: orbit/fountain/world-jitter exotics ─────────────────── */

int test_scene1_spawn_type_3d_commits_20(void)  { return spawn_burst_count_is(0x3d, 20); }
int test_scene1_spawn_type_6c_commits_1(void)   { return spawn_burst_count_is(0x6c, 1);  }
int test_scene1_spawn_type_6e_commits_1(void)   { return spawn_burst_count_is(0x6e, 1);  }
int test_scene1_spawn_type_1f_commits_1(void)   { return spawn_burst_count_is(0x1f, 1);  }
int test_scene1_spawn_type_100_commits_1(void)  { return spawn_burst_count_is(100,  1);  }
int test_scene1_spawn_type_23_commits_1(void)   { return spawn_burst_count_is(0x23, 1);  }
int test_scene1_spawn_type_22_commits_20(void)  { return spawn_burst_count_is(0x22, 20); }
int test_scene1_spawn_type_3c_commits_20(void)  { return spawn_burst_count_is(0x3c, 20); }
int test_scene1_spawn_type_5a_commits_20(void)  { return spawn_burst_count_is(0x5a, 20); }
int test_scene1_spawn_type_2d_commits_20(void)  { return spawn_burst_count_is(0x2d, 20); }
int test_scene1_spawn_type_1d_commits_1(void)   { return spawn_burst_count_is(0x1d, 1);  }

int test_scene1_spawn_type_6d_param7_drives_count(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x6d, 1.0f, 6);
    T_ASSERT_EQ_I(count_committed_slots(0x6d), 6);
    return 0;
}

int test_scene1_spawn_type_45_param7_drives_count(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x45, 1.0f, 13);
    T_ASSERT_EQ_I(count_committed_slots(0x45), 13);
    return 0;
}

int test_scene1_spawn_type_3d_age_stagger(void)
{
    /* Engine: AGE = (-30 - count_index) * 2 → -60, -62, ..., -98. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x3d, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE),
                      (-30 - k) * 2);
    }
    return 0;
}

int test_scene1_spawn_type_3d_param1_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x3d, 1.0f, 0xfeedbeef);
    for (int k = 0; k < 20; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM1),
                      (int32_t)0xfeedbeef);
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM2), k);
    }
    return 0;
}

int test_scene1_spawn_type_3d_base_eq_param(void)
{
    /* Engine: base = (x,y,z) as recovery target. */
    reset_records_and_trace();
    scene1_spawn(0, 11.0f, 22.0f, 33.0f, 0x3d, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_BASE_X) == 11.0f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_BASE_Y) == 22.0f);
        T_ASSERT(slot_read_f(k, SCENE1_RECORDS_A_OFF_BASE_Z) == 33.0f);
    }
    return 0;
}

int test_scene1_spawn_type_6d_camera_yaw_drives_pos(void)
{
    /* Body reads g_scene1_camera_yaw_alt; vary it and see pos change. */
    extern uint32_t g_rng_seed;
    extern float g_scene1_camera_yaw_alt;
    uint32_t saved = g_rng_seed;
    float saved_yaw = g_scene1_camera_yaw_alt;

    reset_records_and_trace();
    g_rng_seed = 0xfacefeed;
    g_scene1_camera_yaw_alt = 0.0f;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x6d, 1.0f, 1);
    float px_a = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);

    reset_records_and_trace();
    g_rng_seed = 0xfacefeed;
    g_scene1_camera_yaw_alt = 1.5707964f;   /* π/2 */
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x6d, 1.0f, 1);
    float px_b = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);

    g_rng_seed = saved;
    g_scene1_camera_yaw_alt = saved_yaw;

    T_ASSERT(px_a != px_b);
    return 0;
}

int test_scene1_spawn_type_6d_param2_in_range(void)
{
    /* Engine: PARAM2 = (u & 0x1f) + 0x41 → in [0x41, 0x60]. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x6d, 1.0f, 5);
    for (int k = 0; k < 5; k++) {
        int32_t p2 = slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM2);
        T_ASSERT(p2 >= 0x41);
        T_ASSERT(p2 <= 0x60);
    }
    return 0;
}

int test_scene1_spawn_type_45_no_param2_tag(void)
{
    /* Engine 0x45 (vs 0x6d) skips the PARAM2 write — preamble's 0 stands. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x45, 1.0f, 3);
    for (int k = 0; k < 3; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM2), 0);
    }
    return 0;
}

int test_scene1_spawn_type_6c_vel_zero(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 1.0f, 2.0f, 3.0f, 0x6c, 1.0f, 0);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z) == 0.0f);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE)   == 0);
    /* pos retained from preamble = param. */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X) == 1.0f);
    return 0;
}

int test_scene1_spawn_type_6e_vel_targets_base(void)
{
    /* Engine: vel = (base - (x,y,z)) / 100.  After spawn, base.{x,z}
     * lie on a 15-unit circle and base.y = 1.0.  Verify
     * pos + vel*100 == base (within fp tolerance). */
    reset_records_and_trace();
    scene1_spawn(0, 100.0f, 200.0f, 300.0f, 0x6e, 1.0f, 0);
    float bx = slot_read_f(0, SCENE1_RECORDS_A_OFF_BASE_X);
    float by = slot_read_f(0, SCENE1_RECORDS_A_OFF_BASE_Y);
    float bz = slot_read_f(0, SCENE1_RECORDS_A_OFF_BASE_Z);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);

    T_ASSERT(by == 1.0f);
    /* Within 15-unit circle. */
    T_ASSERT(bx >= -15.0f && bx <= 15.0f);
    T_ASSERT(bz >= -15.0f && bz <= 15.0f);
    /* vel reaches base in 100 ticks (exact match modulo fp). */
    T_ASSERT_EQ_I(*(int32_t *)&vx,
                  *(int32_t *)&(float){ (bx - 100.0f) / 100.0f });
    T_ASSERT_EQ_I(*(int32_t *)&vy,
                  *(int32_t *)&(float){ (by - 200.0f) / 100.0f });
    T_ASSERT_EQ_I(*(int32_t *)&vz,
                  *(int32_t *)&(float){ (bz - 300.0f) / 100.0f });
    return 0;
}

int test_scene1_spawn_type_1f_scene_counter_drives_pos(void)
{
    /* Body reads g_scene1_spawn_scene_counter_dab58 — vary and check. */
    extern uint32_t g_rng_seed;
    extern int g_scene1_spawn_scene_counter_dab58;
    uint32_t saved = g_rng_seed;
    int saved_c = g_scene1_spawn_scene_counter_dab58;

    reset_records_and_trace();
    g_rng_seed = 0xbeefcafe;
    g_scene1_spawn_scene_counter_dab58 = 0;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x1f, 1.0f, 0);
    float px_a = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float pz_a = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);

    reset_records_and_trace();
    g_rng_seed = 0xbeefcafe;
    g_scene1_spawn_scene_counter_dab58 = 2;   /* π/2 angle */
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x1f, 1.0f, 0);
    float px_b = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X);
    float pz_b = slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_Z);

    g_rng_seed = saved;
    g_scene1_spawn_scene_counter_dab58 = saved_c;

    T_ASSERT(px_a != px_b);
    T_ASSERT(pz_a != pz_b);
    return 0;
}

int test_scene1_spawn_type_100_same_body_as_1f(void)
{
    /* Engine treats 0x1f and 100 (decimal) identically. */
    extern uint32_t g_rng_seed;
    uint32_t saved = g_rng_seed;

    reset_records_and_trace();
    g_rng_seed = 0xa1b2c3d4;
    scene1_spawn(0, 1.5f, 2.5f, 3.5f, 0x1f, 0.7f, 0);
    int32_t snap_1f[19];
    for (int j = 0; j < 19; j++) snap_1f[j] = slot_read_i(0, j);

    reset_records_and_trace();
    g_rng_seed = 0xa1b2c3d4;
    scene1_spawn(0, 1.5f, 2.5f, 3.5f, 100, 0.7f, 0);
    int32_t snap_100[19];
    for (int j = 0; j < 19; j++) snap_100[j] = slot_read_i(0, j);

    g_rng_seed = saved;

    /* All fields except TYPE must match. */
    for (int j = 0; j < 19; j++) {
        if (j == SCENE1_RECORDS_A_OFF_TYPE) continue;
        T_ASSERT_EQ_I(snap_1f[j], snap_100[j]);
    }
    return 0;
}

int test_scene1_spawn_type_23_preamble_only(void)
{
    /* Engine just returns — slot has preamble defaults (TYPE=0x23,
     * pos=param, vel=0, age=0, etc.). */
    reset_records_and_trace();
    scene1_spawn(0, 7.0f, 8.0f, 9.0f, 0x23, 1.0f, 0);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE), 0x23);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X) == 7.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE) == 0);
    return 0;
}

int test_scene1_spawn_type_22_age_stagger(void)
{
    /* Engine: AGE = -4 - count_index → -4, -5, ..., -23. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x22, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE), -4 - k);
    }
    return 0;
}

int test_scene1_spawn_type_3c_base_y_lifted(void)
{
    /* Engine: base.y = u * 0.2 + 4.0 → in [4.0, 4.2]. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x3c, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        float by = slot_read_f(k, SCENE1_RECORDS_A_OFF_BASE_Y);
        T_ASSERT(by >= 4.0f);
        T_ASSERT(by <= 4.2f);
    }
    return 0;
}

int test_scene1_spawn_type_5a_age_stagger(void)
{
    /* Engine: AGE = (-8 - count_index) * 2 → -16, -18, ..., -54. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x5a, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE),
                      (-8 - k) * 2);
    }
    return 0;
}

int test_scene1_spawn_type_2d_age_stagger(void)
{
    /* Engine: AGE = (-2 - count_index) * 2 → -4, -6, ..., -42. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x2d, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        T_ASSERT_EQ_I(slot_read_i(k, SCENE1_RECORDS_A_OFF_AGE),
                      (-2 - k) * 2);
    }
    return 0;
}

int test_scene1_spawn_type_2d_pos_relative_to_param(void)
{
    /* Engine: pos += offset (pos was preamble's param).  pos.y was just
     * y, so after += u, pos.y is in [y, y+1). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 100.0f, 0.0f, 0x2d, 1.0f, 0);
    for (int k = 0; k < 20; k++) {
        float py = slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Y);
        T_ASSERT(py >= 100.0f);
        T_ASSERT(py < 101.0f);
    }
    return 0;
}

int test_scene1_spawn_type_1d_vel_scale_12(void)
{
    /* Engine: vel = (u - 0.5) * scale * 12 → in [-6*scale, +6*scale]. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x1d, 2.0f, 0);
    float vx = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X);
    float vy = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Y);
    float vz = slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_Z);
    T_ASSERT(vx >= -12.0f && vx <= 12.0f);
    T_ASSERT(vy >= -12.0f && vy <= 12.0f);
    T_ASSERT(vz >= -12.0f && vz <= 12.0f);
    return 0;
}

int test_scene1_spawn_type_1d_param2_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x1d, 1.0f, 0xa5b6c7d8);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2),
                  (int32_t)0xa5b6c7d8);
    /* rot.x, rot.y stay at preamble (0).  Only rot.z is set. */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y) == 0.0f);
    float rz = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z);
    T_ASSERT(rz >= 0.0f && rz <= 6.2831856f);
    return 0;
}

/* ─── C8i.4: line-1240 mega-group (34 types share one body) ──────────
 *
 * Predicate match (kept here as a literal — independent of the impl
 * in scene1_spawn.c so a typo in either fails loudly): types
 *   0x25..0x28, 0x37..0x3a, 0x46..0x49, 0x7a..0x84, 0x86..0x90.
 * Each commits 12 particles per call. */

int test_scene1_spawn_mega_group_type_25_commits_12(void) { return spawn_burst_count_is(0x25, 12); }
int test_scene1_spawn_mega_group_type_37_commits_12(void) { return spawn_burst_count_is(0x37, 12); }
int test_scene1_spawn_mega_group_type_46_commits_12(void) { return spawn_burst_count_is(0x46, 12); }
int test_scene1_spawn_mega_group_type_7a_commits_12(void) { return spawn_burst_count_is(0x7a, 12); }
int test_scene1_spawn_mega_group_type_84_commits_12(void) { return spawn_burst_count_is(0x84, 12); }
int test_scene1_spawn_mega_group_type_86_commits_12(void) { return spawn_burst_count_is(0x86, 12); }
int test_scene1_spawn_mega_group_type_90_commits_12(void) { return spawn_burst_count_is(0x90, 12); }

int test_scene1_spawn_mega_group_type_85_not_member(void)
{
    /* 0x85 sits between 0x7a-0x84 and 0x86-0x90 but is NOT in the
     * mega-group predicate — must remain unimplemented (no commits). */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x85, 1.0f, 0);
    T_ASSERT_EQ_I(count_committed_slots(0x85), 0);
    return 0;
}

int test_scene1_spawn_mega_group_type_29_not_member(void)
{
    /* 0x29 is a real type (C8i.3c, 14 particles) and would falsely
     * fall into the >=0x25 prefix if the upper bound were sloppy.
     * Verify it commits its own count (14), not the mega-group's 12. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x29, 1.0f, 0);
    T_ASSERT_EQ_I(count_committed_slots(0x29), 14);
    return 0;
}

int test_scene1_spawn_mega_group_param2_color_in_range(void)
{
    /* Engine: PARAM2 = rng_next15() % 10 → 0..9 color cycle. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x25, 1.0f, 0);
    for (int k = 0; k < 12; k++) {
        int32_t c = slot_read_i(k, SCENE1_RECORDS_A_OFF_PARAM2);
        T_ASSERT(c >= 0);
        T_ASSERT(c < 10);
    }
    return 0;
}

int test_scene1_spawn_mega_group_rot_y_sign_alternates(void)
{
    /* Engine L1282-1284: if (local_8 & 1) rot.y = -rot.y.
     * Even-index slots store +|fVar1|, odd-index store -|fVar1|.
     * The MAGNITUDES come from independent RNG draws, but the SIGN is
     * fully determined by the slot index parity. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x37, 1.0f, 0);
    for (int k = 0; k < 12; k++) {
        float ry = slot_read_f(k, SCENE1_RECORDS_A_OFF_ROT_Y);
        if ((k & 1) == 0) {
            T_ASSERT(ry >= 0.0f);
        } else {
            T_ASSERT(ry <= 0.0f);
        }
    }
    return 0;
}

int test_scene1_spawn_mega_group_rot_x_raw_unit(void)
{
    /* Engine L1277-1278: rot.x = u (raw [0,1)), NOT u*2π like most
     * other handlers.  Sanity-bound here — a mis-port that wrote
     * u*2π would put values up to ~6.28. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x7d, 1.0f, 0);
    for (int k = 0; k < 12; k++) {
        float rx = slot_read_f(k, SCENE1_RECORDS_A_OFF_ROT_X);
        T_ASSERT(rx >= 0.0f);
        T_ASSERT(rx < 1.0f);
    }
    return 0;
}

int test_scene1_spawn_mega_group_pos_offsets_param(void)
{
    /* Engine writes pos.{x,z} as (param + sin/cos*amp) and pos.y as
     * (param + u*0.5).  amp = (u+0.2)*0.5 with u in [0,1) → amp in
     * [0.1, 0.6).  Worst-case xz offset is ±0.6, so pos.{x,z} stays
     * within [param-0.6, param+0.6].  pos.y is in [param, param+0.5). */
    reset_records_and_trace();
    scene1_spawn(0, 100.0f, 200.0f, 300.0f, 0x88, 1.0f, 0);
    for (int k = 0; k < 12; k++) {
        float px = slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_X);
        float py = slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Y);
        float pz = slot_read_f(k, SCENE1_RECORDS_A_OFF_POS_Z);
        T_ASSERT(px >= 100.0f - 0.6f && px <= 100.0f + 0.6f);
        T_ASSERT(py >= 200.0f && py < 200.5f);
        T_ASSERT(pz >= 300.0f - 0.6f && pz <= 300.0f + 0.6f);
    }
    return 0;
}

int test_scene1_spawn_mega_group_all_types_share_rng_sequence(void)
{
    /* All 34 mega-group types must produce byte-identical slot state
     * (apart from TYPE) for the same RNG seed.  This locks in the
     * "one shared body" contract — adding a type-specific branch to
     * the handler later would break this. */
    extern uint32_t g_rng_seed;
    const int types[] = { 0x25, 0x28, 0x37, 0x3a, 0x46, 0x49,
                          0x7a, 0x7f, 0x84, 0x86, 0x8b, 0x90 };
    const int n = (int)(sizeof types / sizeof types[0]);

    /* Baseline: type 0x25 */
    uint32_t saved = g_rng_seed;
    reset_records_and_trace();
    g_rng_seed = 0xdeadbeef;
    scene1_spawn(0, 1.0f, 2.0f, 3.0f, types[0], 1.5f, 0);
    int32_t base[12][19];
    for (int s = 0; s < 12; s++)
        for (int j = 0; j < 19; j++)
            base[s][j] = slot_read_i(s, j);

    for (int t = 1; t < n; t++) {
        reset_records_and_trace();
        g_rng_seed = 0xdeadbeef;
        scene1_spawn(0, 1.0f, 2.0f, 3.0f, types[t], 1.5f, 0);
        for (int s = 0; s < 12; s++) {
            for (int j = 0; j < 19; j++) {
                if (j == SCENE1_RECORDS_A_OFF_TYPE) continue;
                T_ASSERT_EQ_I(slot_read_i(s, j), base[s][j]);
            }
        }
    }

    g_rng_seed = saved;
    return 0;
}

/* ─── C8i.5a: 23 one-particle spawn types ────────────────────────────
 *
 * Each handler is `preamble + tiny tail`.  Tests confirm the per-type
 * tail effect + that all 23 types commit exactly 1 particle. */

/* Spawn-count smoke for every new type. */
int test_scene1_spawn_type_19_commits_1(void)  { return spawn_burst_count_is(0x19, 1); }
int test_scene1_spawn_type_44_commits_1(void)  { return spawn_burst_count_is(0x44, 1); }
int test_scene1_spawn_type_94_commits_1(void)  { return spawn_burst_count_is(0x94, 1); }
int test_scene1_spawn_type_2e_commits_1(void)  { return spawn_burst_count_is(0x2e, 1); }
int test_scene1_spawn_type_1e_commits_1(void)  { return spawn_burst_count_is(0x1e, 1); }
int test_scene1_spawn_type_1a_commits_1(void)  { return spawn_burst_count_is(0x1a, 1); }
int test_scene1_spawn_type_5f_commits_1(void)  { return spawn_burst_count_is(0x5f, 1); }
int test_scene1_spawn_type_4_commits_1(void)   { return spawn_burst_count_is(4,    1); }
int test_scene1_spawn_type_70_commits_1(void)  { return spawn_burst_count_is(0x70, 1); }
int test_scene1_spawn_type_1c_commits_1(void)  { return spawn_burst_count_is(0x1c, 1); }
int test_scene1_spawn_type_42_commits_1(void)  { return spawn_burst_count_is(0x42, 1); }
int test_scene1_spawn_type_2a_commits_1(void)  { return spawn_burst_count_is(0x2a, 1); }
int test_scene1_spawn_type_13_commits_1(void)  { return spawn_burst_count_is(0x13, 1); }
int test_scene1_spawn_type_14_commits_1(void)  { return spawn_burst_count_is(0x14, 1); }
int test_scene1_spawn_type_24_commits_1(void)  { return spawn_burst_count_is(0x24, 1); }
int test_scene1_spawn_type_6_commits_1(void)   { return spawn_burst_count_is(6,    1); }
int test_scene1_spawn_type_7_commits_1(void)   { return spawn_burst_count_is(7,    1); }
int test_scene1_spawn_type_8_commits_1(void)   { return spawn_burst_count_is(8,    1); }
int test_scene1_spawn_type_9_commits_1(void)   { return spawn_burst_count_is(9,    1); }
int test_scene1_spawn_type_11_commits_1(void)  { return spawn_burst_count_is(0x11, 1); }
int test_scene1_spawn_type_12_commits_1(void)  { return spawn_burst_count_is(0x12, 1); }
int test_scene1_spawn_type_54_commits_1(void)  { return spawn_burst_count_is(0x54, 1); }
int test_scene1_spawn_type_50_commits_1(void)  { return spawn_burst_count_is(0x50, 1); }

/* Preamble-only types must leave every non-preamble field at its
 * preamble value.  We pick 0x19 as a representative — same body shape
 * as 0x44 / 0x94 / 0x2e / 0x1e. */
int test_scene1_spawn_type_19_preamble_only(void)
{
    reset_records_and_trace();
    scene1_spawn(7, 1.0f, 2.0f, 3.0f, 0x19, 0.5f, 0xdead);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE), 0x19);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AUX_18), 7);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_POS_X) == 1.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_VEL_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z) == 0.0f);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 0);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2), 0);
    /* PARAM1 is NOT touched by preamble — preamble-only types leave it
     * at whatever zero we started with after reset. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), 0);
    return 0;
}

int test_scene1_spawn_type_1a_param2_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x1a, 1.0f, 0x12345678);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2), 0x12345678);
    /* No other slot field beyond preamble touched. */
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z) == 0.0f);
    return 0;
}

/* rot.z = u*2π — same body for 0x5f, 4, 0x70, 0x1c.  Verify rot.z lands
 * in [0, 2π) and no other field is touched.  Tested via 0x5f. */
int test_scene1_spawn_type_5f_rot_z_in_range(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x5f, 1.0f, 0);
    float rz = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z);
    T_ASSERT(rz >= 0.0f);
    T_ASSERT(rz < 6.2831856f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y) == 0.0f);
    return 0;
}

/* 0x5f / 4 / 0x70 / 0x1c MUST share the same body — verify they emit
 * identical slot state for a pinned RNG seed.  Catches regressions if
 * someone splits the dispatch case by accident. */
int test_scene1_spawn_rot_z_group_shares_body(void)
{
    extern uint32_t g_rng_seed;
    const int types[] = { 0x5f, 4, 0x70, 0x1c };
    uint32_t saved = g_rng_seed;

    /* Baseline 0x5f. */
    reset_records_and_trace();
    g_rng_seed = 0xcafef00d;
    scene1_spawn(0, 5.0f, 6.0f, 7.0f, types[0], 2.0f, 0);
    int32_t base[19];
    for (int j = 0; j < 19; j++) base[j] = slot_read_i(0, j);

    for (int t = 1; t < 4; t++) {
        reset_records_and_trace();
        g_rng_seed = 0xcafef00d;
        scene1_spawn(0, 5.0f, 6.0f, 7.0f, types[t], 2.0f, 0);
        for (int j = 0; j < 19; j++) {
            if (j == SCENE1_RECORDS_A_OFF_TYPE) continue;
            T_ASSERT_EQ_I(slot_read_i(0, j), base[j]);
        }
    }
    g_rng_seed = saved;
    return 0;
}

/* 0x42: rot.y AND rot.z both random; verify both are in [0, 2π). */
int test_scene1_spawn_type_42_rot_y_and_z(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x42, 1.0f, 0);
    float ry = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Y);
    float rz = slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_Z);
    T_ASSERT(ry >= 0.0f && ry < 6.2831856f);
    T_ASSERT(rz >= 0.0f && rz < 6.2831856f);
    T_ASSERT(slot_read_f(0, SCENE1_RECORDS_A_OFF_ROT_X) == 0.0f);
    return 0;
}

/* PARAM1 = param_7 — shared by 0x2a / 0x13 / 0x14. */
int test_scene1_spawn_type_2a_param1_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x2a, 1.0f, 0x4242);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), 0x4242);
    return 0;
}

int test_scene1_spawn_type_13_param1_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x13, 1.0f, -7);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), -7);
    return 0;
}

int test_scene1_spawn_type_14_param1_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x14, 1.0f, 99);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), 99);
    return 0;
}

/* 0x24: PARAM1=param_7 + side-effect arm counter increments. */
int test_scene1_spawn_type_24_param1_and_arm(void)
{
    extern int g_scene1_spawn_type_24_arm_count;
    reset_records_and_trace();
    g_scene1_spawn_type_24_arm_count = 0;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x24, 1.0f, 0x77);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), 0x77);
    T_ASSERT_EQ_I(g_scene1_spawn_type_24_arm_count, 1);

    /* Each subsequent call increments the arm counter.  Provide a free
     * slot at index 5 via a partial reset. */
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x24, 1.0f, 0);
    T_ASSERT_EQ_I(g_scene1_spawn_type_24_arm_count, 2);
    return 0;
}

/* 6/7/8/9: PARAM2 takes a snapshot of g_scene1_spawn_global_ae84. */
int test_scene1_spawn_type_6_param2_snapshots_global(void)
{
    extern int g_scene1_spawn_global_ae84;
    reset_records_and_trace();
    g_scene1_spawn_global_ae84 = 0xabcd;
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 6, 1.0f, 0);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2), 0xabcd);
    g_scene1_spawn_global_ae84 = 0;
    return 0;
}

int test_scene1_spawn_types_6_to_9_share_body(void)
{
    extern int g_scene1_spawn_global_ae84;
    const int types[] = { 6, 7, 8, 9 };
    for (int t = 0; t < 4; t++) {
        reset_records_and_trace();
        g_scene1_spawn_global_ae84 = 0x55 + t;
        scene1_spawn(0, 0.0f, 0.0f, 0.0f, types[t], 1.0f, 0);
        T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE), types[t]);
        T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2), 0x55 + t);
    }
    g_scene1_spawn_global_ae84 = 0;
    return 0;
}

/* 0x11: const-vel int literals + AGE = -(u%24). */
int test_scene1_spawn_type_11_const_vel(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x11, 1.0f, 0);
    /* vel bits are the engine's literal int constants.  Read back via
     * slot_read_i so we compare bit-exact. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_VEL_X), 0x3ca3d70a);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_VEL_Y), 0x3b03126f);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_VEL_Z), 0);
    int32_t age = slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE);
    T_ASSERT(age <= 0);
    T_ASSERT(age > -24);
    return 0;
}

/* 0x12, 0x54: AGE=0, PARAM1=param_7, PARAM2=0.  AGE was already 0 from
 * preamble; the explicit write here is engine-side dead code but the
 * port mirrors it for cycle-accuracy.  PARAM1 must reflect param_7. */
int test_scene1_spawn_type_12_param1_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x12, 1.0f, 0x6789);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), 0x6789);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM2), 0);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE), 0);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE), 0x12);
    return 0;
}

int test_scene1_spawn_type_54_param1_eq_param7(void)
{
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x54, 1.0f, -1);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_PARAM1), -1);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_TYPE), 0x54);
    return 0;
}

/* 0x50: 17-dword scratch copy from g_scene1_spawn_50_block_d04 + rot.y
 * from g_scene1_spawn_50_rot_y_ea4 + const-vel + AGE=-(u%0x18). */
int test_scene1_spawn_type_50_copies_block(void)
{
    extern int32_t g_scene1_spawn_50_block_d04[17];
    extern int32_t g_scene1_spawn_50_rot_y_ea4;
    reset_records_and_trace();
    for (int k = 0; k < 17; k++) g_scene1_spawn_50_block_d04[k] = 0x100 + k;
    g_scene1_spawn_50_rot_y_ea4 = 0x44660088;

    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x50, 1.0f, 0);

    /* Block landed at dw 20..36. */
    for (int k = 0; k < 17; k++) {
        T_ASSERT_EQ_I(slot_read_i(0, 20 + k), 0x100 + k);
    }
    /* rot.y holds the raw int bits from the ea4 stand-in. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_ROT_Y), 0x44660088);
    /* Const vel + AGE. */
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_VEL_X), 0x3ca3d70a);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_VEL_Y), 0x3b03126f);
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_VEL_Z), 0);
    int32_t age = slot_read_i(0, SCENE1_RECORDS_A_OFF_AGE);
    T_ASSERT(age <= 0);
    T_ASSERT(age > -0x18);

    /* Clean up so other tests see zero stand-ins. */
    for (int k = 0; k < 17; k++) g_scene1_spawn_50_block_d04[k] = 0;
    g_scene1_spawn_50_rot_y_ea4 = 0;
    return 0;
}

int test_scene1_spawn_type_50_block_default_zero(void)
{
    /* With both stand-ins zero, the copy writes zeros; rot.y is zero. */
    reset_records_and_trace();
    scene1_spawn(0, 0.0f, 0.0f, 0.0f, 0x50, 1.0f, 0);
    for (int k = 0; k < 17; k++) {
        T_ASSERT_EQ_I(slot_read_i(0, 20 + k), 0);
    }
    T_ASSERT_EQ_I(slot_read_i(0, SCENE1_RECORDS_A_OFF_ROT_Y), 0);
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
