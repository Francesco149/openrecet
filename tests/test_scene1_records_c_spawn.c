/*
 * test_scene1_records_c_spawn.c — unit tests for the C8j.2 table C
 * allocators (FUN_0044aef0 + FUN_0044af50 + 2 wrappers).
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "rng.h"
#include "scene1_records.h"
#include "scene1_records_c_spawn.h"
#include "scene1_records_c_tick.h"

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    memset(g_scene1_records_c, 0, sizeof g_scene1_records_c);
    scene1_records_reset(1);
    g_scene1_records_c_count = 0;
}

static int32_t slot_get_i(int slot, int off)
{
    return g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
}
static float slot_get_f(int slot, int off)
{
    int32_t v = g_scene1_records_c[slot * SCENE1_RECORDS_C_STRIDE + off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static int count_live(void)
{
    int n = 0;
    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        if (slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE) != -1) n++;
    }
    return n;
}

/* ─── pickup-spawn (FUN_0044aef0) ─────────────────────────────────── */

int test_records_c_spawn_pickup_writes_first_free_slot(void)
{
    reset_world();
    scene1_records_c_spawn_pickup(0, 1.5f, 2.5f, 3.5f, 0x42);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), 0x42);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_X) - 1.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Y) - 2.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_POS_Z) - 3.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Y) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_VEL_Z) - 0.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AGE), 0);
    T_ASSERT(fabsf(slot_get_f(0, SCENE1_RECORDS_C_OFF_SCALE) - 1.0f) < 1e-6f);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_PICKUP_E2), 0);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_STATE), 2);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AUX), 0);
    return 0;
}

int test_records_c_spawn_pickup_skips_alive_slots(void)
{
    reset_world();
    /* Stamp slot 0 as alive (type=99). */
    g_scene1_records_c[0 * SCENE1_RECORDS_C_STRIDE + SCENE1_RECORDS_C_OFF_TYPE] = 99;
    scene1_records_c_spawn_pickup(0, 0.0f, 0.0f, 0.0f, 0x42);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_TYPE), 99);
    T_ASSERT_EQ_I(slot_get_i(1, SCENE1_RECORDS_C_OFF_TYPE), 0x42);
    return 0;
}

int test_records_c_spawn_pickup_table_full_is_noop(void)
{
    reset_world();
    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + SCENE1_RECORDS_C_OFF_TYPE] = 1;
    }
    scene1_records_c_spawn_pickup(0, 0.0f, 0.0f, 0.0f, 0x42);
    /* All slots still type=1; no new slot found. */
    for (int i = 0; i < SCENE1_RECORDS_C_COUNT; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE), 1);
    }
    return 0;
}

int test_records_c_spawn_pickup_does_not_clobber_e1(void)
{
    /* Engine quirk: FUN_0044aef0 doesn't touch slot[14] (PICKUP_E1).
     * A test slot pre-stamped at slot[14]=0xBEEF, then reset to
     * sentinel TYPE, then spawned should retain 0xBEEF in slot[14]. */
    reset_world();
    g_scene1_records_c[0 * SCENE1_RECORDS_C_STRIDE + SCENE1_RECORDS_C_OFF_PICKUP_E1] = 0xBEEF;
    scene1_records_c_spawn_pickup(0, 0.0f, 0.0f, 0.0f, 0x42);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_PICKUP_E1), 0xBEEF);
    return 0;
}

/* ─── world-drop spawn (FUN_0044af50) ─────────────────────────────── */

int test_records_c_spawn_world_drop_count_zero_is_noop(void)
{
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, 0x42, 0, 1.0f, 0,0, 0, -1);
    T_ASSERT_EQ_I(count_live(), 0);
    return 0;
}

int test_records_c_spawn_world_drop_low_type_caps_at_136(void)
{
    /* type <= 6 → scan cap is 136 (0x88).  Pre-stamp slots 0..135 as
     * alive; spawn 1 record; should NOT find a slot (cap reached at 136). */
    reset_world();
    for (int i = 0; i < 136; i++) {
        g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + SCENE1_RECORDS_C_OFF_TYPE] = 99;
    }
    scene1_records_c_spawn_world_drop(0, 0,0,0, /*type=*/3, 1, 1.0f, 0,0, 0, -1);
    /* No new slot — slots 136+ are still sentinel. */
    T_ASSERT_EQ_I(slot_get_i(136, SCENE1_RECORDS_C_OFF_TYPE), -1);
    return 0;
}

int test_records_c_spawn_world_drop_high_type_scans_all_200(void)
{
    /* type > 6 → scan cap is 200.  Pre-stamp slots 0..135 alive; spawn
     * should land at slot 136. */
    reset_world();
    for (int i = 0; i < 136; i++) {
        g_scene1_records_c[i * SCENE1_RECORDS_C_STRIDE + SCENE1_RECORDS_C_OFF_TYPE] = 99;
    }
    scene1_records_c_spawn_world_drop(0, 1.0f, 2.0f, 3.0f, /*type=*/0x42,
                                      1, 1.0f, 0,0, 0, -1);
    T_ASSERT_EQ_I(slot_get_i(136, SCENE1_RECORDS_C_OFF_TYPE), 0x42);
    T_ASSERT_EQ_I(slot_get_i(136, SCENE1_RECORDS_C_OFF_STATE), 0);
    return 0;
}

int test_records_c_spawn_world_drop_commits_count_slots(void)
{
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, 0x42, 4, 1.0f, 0,0, 0, -1);
    T_ASSERT_EQ_I(count_live(), 4);
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_TYPE), 0x42);
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_STATE), 0);
    }
    return 0;
}

int test_records_c_spawn_world_drop_age_in_0_to_7(void)
{
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, 0x42, 16, 1.0f, 0,0, 0, -1);
    for (int i = 0; i < 16; i++) {
        int age = slot_get_i(i, SCENE1_RECORDS_C_OFF_AGE);
        T_ASSERT(age >= 0 && age <= 7);
    }
    return 0;
}

int test_records_c_spawn_world_drop_vel_xz_paired_trig(void)
{
    /* vel.x = sin(angle) * (u+0.2)*0.5 * mag
     * vel.z = cos(angle) * (u+0.2)*0.5 * mag
     * → vel.x² + vel.z² = ((u+0.2)*0.5)² * mag² */
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, 0x42, 4, 2.0f, 0,0, 0, -1);
    for (int i = 0; i < 4; i++) {
        float vx = slot_get_f(i, SCENE1_RECORDS_C_OFF_VEL_X);
        float vy = slot_get_f(i, SCENE1_RECORDS_C_OFF_VEL_Y);
        float vz = slot_get_f(i, SCENE1_RECORDS_C_OFF_VEL_Z);
        float xz_mag = sqrtf(vx * vx + vz * vz);
        /* fVar1 = (u+0.2)*0.5; u ∈ [0,1) → fVar1 ∈ [0.1, 0.6);
         * xz_mag = fVar1 * mag = fVar1 * 2 → ∈ [0.2, 1.2). */
        T_ASSERT(xz_mag >= 0.2f - 1e-5f);
        T_ASSERT(xz_mag <  1.2f + 1e-5f);
        /* vy = (u+0.2) * mag * 0.5 = (u+0.2); ∈ [0.2, 1.2). */
        T_ASSERT(vy >= 0.2f - 1e-5f);
        T_ASSERT(vy <  1.2f + 1e-5f);
    }
    return 0;
}

int test_records_c_spawn_world_drop_low_type_skips_e1_write(void)
{
    /* type <= 6 → pickup_e1 stays 0 even when e1 != 0. */
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, /*type=*/3, 1, 1.0f, /*e1=*/0xABCD,
                                      0, 0, -1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_PICKUP_E1), 0);
    return 0;
}

int test_records_c_spawn_world_drop_high_type_writes_e1(void)
{
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, /*type=*/0x42, 1, 1.0f,
                                      /*e1=*/0xABCD, 0, 0, -1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_PICKUP_E1), 0xABCD);
    return 0;
}

int test_records_c_spawn_world_drop_explicit_override_writes_e2(void)
{
    /* type > 6 + type_override >= 0 → pickup_e2 = type_override. */
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, 0x42, 1, 1.0f, 0,0, 0,
                                      /*type_override=*/7);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_PICKUP_E2), 7);
    return 0;
}

int test_records_c_spawn_world_drop_ramp_fires_outside_window(void)
{
    /* type > 6 + type_override < 0 + (type-7) ∉ [0xc80, 0xce3] → 4-color
     * RNG ramp.  Use a normal type like 0x42 (far below the window).
     * Statistical: with N=200 spawns and the ramp probabilities
     * {50%, 20%, 15%, 10%, 5%}, all 5 colors should appear (PMF
     * isn't degenerate).  Use small N=64 and just verify pickup_e2 ∈
     * {0..4}. */
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, /*type=*/0x42, 64, 1.0f, 0,0, 0,
                                      /*type_override=*/-1);
    for (int i = 0; i < 64; i++) {
        int e2 = slot_get_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2);
        T_ASSERT(e2 >= 0 && e2 <= 4);
    }
    return 0;
}

int test_records_c_spawn_world_drop_ramp_suppressed_inside_window(void)
{
    /* (type - 7) ∈ [0xc80, 0xce3] → ramp skipped, pickup_e2 stays 0.
     * Pick type = 7 + 0xc80 = 0xc87. */
    reset_world();
    scene1_records_c_spawn_world_drop(0, 0,0,0, /*type=*/0xc87, 8, 1.0f, 0,0, 0,
                                      /*type_override=*/-1);
    for (int i = 0; i < 8; i++) {
        T_ASSERT_EQ_I(slot_get_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2), 0);
    }
    return 0;
}

int test_records_c_spawn_world_drop_owner_extra_aux_aux_recorded(void)
{
    reset_world();
    scene1_records_c_spawn_world_drop(0xDEAD, 0,0,0, 0x42, 1, 1.0f, 0,
                                      /*extra_aux=*/0xCAFE,
                                      /*aux10=*/0xBEEF,
                                      /*type_override=*/-1);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_OWNER), (int32_t)0xDEAD);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_EXTRA_AUX), (int32_t)0xCAFE);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AUX), (int32_t)0xBEEF);
    return 0;
}

/* ─── wrappers ────────────────────────────────────────────────────── */

int test_records_c_spawn_default_wrapper_forces_ramp(void)
{
    /* FUN_0044b0f3 calls FUN_0044af50 with type_override=-1.  So
     * pickup_e2 should land in {0..4} via the ramp (assuming type
     * outside the suppress window). */
    reset_world();
    scene1_records_c_spawn_world_drop_default(0, 0,0,0, /*type=*/0x42, 8, 1.0f,
                                              0, 0);
    for (int i = 0; i < 8; i++) {
        int e2 = slot_get_i(i, SCENE1_RECORDS_C_OFF_PICKUP_E2);
        T_ASSERT(e2 >= 0 && e2 <= 4);
    }
    /* aux10 forced to 0. */
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AUX), 0);
    return 0;
}

int test_records_c_spawn_typed_wrapper_passes_override(void)
{
    /* FUN_0044b12f calls FUN_0044af50 with type_override=caller's
     * last arg.  Pickup_e2 = 5 means the override was passed through. */
    reset_world();
    scene1_records_c_spawn_world_drop_typed(0, 0,0,0, /*type=*/0x42, 1, 1.0f,
                                            0, 0, /*type_override=*/5);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_PICKUP_E2), 5);
    return 0;
}

int test_records_c_spawn_default_wrapper_aux10_zero(void)
{
    reset_world();
    scene1_records_c_spawn_world_drop_default(0, 0,0,0, 0x42, 1, 1.0f, 0,
                                              /*extra_aux=*/0x1234);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_EXTRA_AUX), 0x1234);
    T_ASSERT_EQ_I(slot_get_i(0, SCENE1_RECORDS_C_OFF_AUX), 0);
    return 0;
}
