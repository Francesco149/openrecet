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
