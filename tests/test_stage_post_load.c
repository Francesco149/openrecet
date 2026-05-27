/*
 * test_stage_post_load.c — tests for src/stage_post_load.{c,h}.
 *
 * Covers the FUN_00435c98 body via stage_post_load_init():
 *   - scratch reset triple (0, 3, 1)
 *   - XP threshold writes at level 0 → (0, 300)
 *   - XP threshold writes at level 5 → (4500, 6300)
 *   - xp_value clamping (below xp_curr, above xp_next, in-range pass)
 *   - chara position carry-forward (lo + hi int16 sum, mirrored to
 *     the backup pair)
 *   - DAT_056dae44 → -1 sentinel block
 *   - DAT_0438b4c4 = 1 single-int write
 *   - Idempotency across two consecutive init calls
 */

#include "t.h"
#include "stage_post_load.h"

/* ─── scratch triple at DAT_056da1cc/d0/d4 ─────────────────────────────── */

int test_stage_post_load_init_writes_da1xx_triple(void)
{
    stage_post_load_reset_for_test();
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_get_dat_056da1cc(), 0);
    T_ASSERT_EQ_I(stage_post_load_get_dat_056da1d0(), 3);
    T_ASSERT_EQ_I(stage_post_load_get_dat_056da1d4(), 1);
    return 0;
}

/* ─── XP threshold writes via xp_curve ─────────────────────────────────── */

int test_stage_post_load_init_xp_thresholds_at_level_0(void)
{
    /* Fresh BSS: chara 0 level = 0 → xp_curve(0)=0, xp_curve(1)=300. */
    stage_post_load_reset_for_test();
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x12), 0);
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x13), 300);
    return 0;
}

int test_stage_post_load_init_xp_thresholds_at_level_5(void)
{
    /* xp_curve(5) = 5*6*150 = 4500; xp_curve(6) = 6*7*150 = 6300. */
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_field(0, 0, 5);
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x12), 4500);
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x13), 6300);
    return 0;
}

/* ─── xp_value clamping into [xp_curr, xp_next] ────────────────────────── */

int test_stage_post_load_init_clamps_xp_value_below_curr(void)
{
    /* Level=3 → xp_curr=3*4*150=1800, xp_next=4*5*150=3000.
     * Seed xp_value=100 → should clamp up to 1800. */
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_field(0, 0, 3);
    stage_post_load_set_chara_field(0, 0x11, 100);
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x11), 1800);
    return 0;
}

int test_stage_post_load_init_clamps_xp_value_above_next(void)
{
    /* Level=2 → xp_curr=2*3*150=900, xp_next=3*4*150=1800.
     * Seed xp_value=5000 → should clamp down to 1800. */
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_field(0, 0, 2);
    stage_post_load_set_chara_field(0, 0x11, 5000);
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x11), 1800);
    return 0;
}

int test_stage_post_load_init_preserves_xp_value_in_range(void)
{
    /* Level=2 → range [900, 1800].  xp_value=1200 stays 1200. */
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_field(0, 0, 2);
    stage_post_load_set_chara_field(0, 0x11, 1200);
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x11), 1200);
    return 0;
}

/* ─── position carry-forward ───────────────────────────────────────────── */

int test_stage_post_load_init_carries_position_from_chara_bytes(void)
{
    /* Seed pos_x_lo=10, pos_x_hi=5 → X = 15
     *      pos_y_lo=20, pos_y_hi=7 → Y = 27
     * Both also mirrored into the backup pair DAT_056db0c4 / c8. */
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_short(0, 0x3c, 10);
    stage_post_load_set_chara_short(0, 0x3e, 5);
    stage_post_load_set_chara_short(0, 0x40, 20);
    stage_post_load_set_chara_short(0, 0x42, 7);
    stage_post_load_init();
    T_ASSERT(stage_post_load_get_dat_056db0bc() == 15.0f);
    T_ASSERT(stage_post_load_get_dat_056db0c0() == 27.0f);
    T_ASSERT(stage_post_load_get_dat_056db0c4() == 15.0f);
    T_ASSERT(stage_post_load_get_dat_056db0c8() == 27.0f);
    return 0;
}

int test_stage_post_load_init_carries_negative_position(void)
{
    /* Signed-short sign-extension: lo=-100, hi=-50 → sum=-150. */
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_short(0, 0x3c, -100);
    stage_post_load_set_chara_short(0, 0x3e, -50);
    stage_post_load_init();
    T_ASSERT(stage_post_load_get_dat_056db0bc() == -150.0f);
    return 0;
}

int test_stage_post_load_init_zeroes_db0d8_and_bea0(void)
{
    stage_post_load_reset_for_test();
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_get_dat_056db0d8(), 0);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bea0(), 0);
    return 0;
}

/* ─── sentinel + zero scratch arrays ───────────────────────────────────── */

int test_stage_post_load_init_writes_minus_one_sentinels_at_dae44(void)
{
    stage_post_load_reset_for_test();
    stage_post_load_init();
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_056dae44(i), -1);
    }
    return 0;
}

int test_stage_post_load_init_writes_one_at_b4c4(void)
{
    stage_post_load_reset_for_test();
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438b4c4(), 1);
    return 0;
}

int test_stage_post_load_init_zeroes_b4ec_and_grid(void)
{
    /* Post-state on the grid arrays: FUN_00435c98's body zeros all
     * 6 entries of each of the 4 rows; FUN_00435fbb (also called from
     * the same init) then processes bef4[0..4] (default → 0 for all
     * five since counter[i] == threshold[i] check fails) and
     * increments bf24[0..4] by 1.  bef4[5] and bf24[5] remain at the
     * FUN_00435c98 zero. */
    stage_post_load_reset_for_test();
    stage_post_load_init();
    for (int i = 0; i < 25; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438b4ec(i), 0);
    }
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bedc(i), 0);
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(i), 0);
    }
    /* FUN_00435fbb post-state at indices [0..4]:
     *   bef4 — counter=0 means threshold check fails for all five →
     *           bef4 starts at 0 then the low-clamp pulls indices with
     *           nonzero low up to their low.
     *           Lows: {0, 0, 0.02, 0.008, 0}
     *           No override (gate returns 0 at default state).
     *   bf24 — counters all incremented from 0 to 1.
     * Index 5 is outside FUN_00435fbb's range; both bef4[5] and
     * bf24[5] remain at the FUN_00435c98-seeded 0. */
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(0) == 0.0f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(1) == 0.0f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(2) == 0.02f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(3) == 0.008f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(4) == 0.0f);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bef4(5), 0);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(i), 1);
    }
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(5), 0);
    return 0;
}

/* ─── FUN_00435fbb sibling — driven directly via stage_post_load_pulse_5fold ── */

int test_stage_post_load_435fbb_default_values_are_low_clamps(void)
{
    /* First call from reset state: counter[i] starts at 0 (reset by
     * param_1=1), so threshold check fails for all five.  bef4 lands
     * at low[i] via the low clamp:
     *   {0, 0, 0.02, 0.008, 0} */
    stage_post_load_reset_for_test();
    stage_post_load_pulse_5fold(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(0) == 0.0f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(1) == 0.0f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(2) == 0.02f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(3) == 0.008f);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(4) == 0.0f);
    return 0;
}

int test_stage_post_load_435fbb_counter_advances_with_no_reset(void)
{
    /* With param_1=0, each call preserves counters and just increments.
     * After 4 calls counters reach 4. */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 4; n++) stage_post_load_pulse_5fold(0, -1);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(i), 4);
    }
    return 0;
}

int test_stage_post_load_435fbb_param1_one_resets_arrays(void)
{
    /* Pulse several times without reset, then once with reset.  Final
     * state: counters back to 1 (after the single increment past 0). */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 5; n++) stage_post_load_pulse_5fold(0, -1);
    stage_post_load_pulse_5fold(1, -1);
    for (int i = 0; i < 5; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(i), 1);
    }
    return 0;
}

int test_stage_post_load_435fbb_param2_zeros_specific_counter_slot(void)
{
    /* Pulse twice with no reset → counters=2.  Then pulse(0, 2) → only
     * counter[2] zeros before the loop, increments to 1.  Others go
     * from 2 to 3. */
    stage_post_load_reset_for_test();
    stage_post_load_pulse_5fold(0, -1);  /* counters 0→1 */
    stage_post_load_pulse_5fold(0, -1);  /* counters 1→2 */
    stage_post_load_pulse_5fold(0, 2);   /* counter[2] zeroed; all increment */
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(0), 3);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(1), 3);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(2), 1);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(3), 3);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(4), 3);
    return 0;
}

int test_stage_post_load_435fbb_idx0_threshold_zero_produces_slope_value(void)
{
    /* Index 0: threshold=0, slope=0.002, low=0, high=0.1.
     * Pulse 4× with no reset → counter[0]=4 entering, so reading is
     *   call 1: counter=0 → 0<0 false → bef4=0 → counter=1
     *   call 2: counter=1 → 0<1 true  → pre=(1-0)*0.002+0=0.002 → counter=2
     *   call 3: counter=2 → pre=0.004 → counter=3
     *   call 4: counter=3 → pre=0.006 → counter=4
     * Final bef4[0] = 0.006. */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 4; n++) stage_post_load_pulse_5fold(0, -1);
    float v = stage_post_load_get_dat_0438bef4_as_float(0);
    T_ASSERT(v > 0.0059f && v < 0.0061f);
    return 0;
}

int test_stage_post_load_435fbb_idx0_high_clamp_caps_at_0_1(void)
{
    /* Push counter[0] far past where slope*counter > high.  After 200
     * pulses with no reset, slope*counter = 0.4, clamps to 0.1. */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 200; n++) stage_post_load_pulse_5fold(0, -1);
    float v = stage_post_load_get_dat_0438bef4_as_float(0);
    T_ASSERT(v == 0.1f);
    return 0;
}

int test_stage_post_load_435fbb_idx1_threshold_3_below_stays_zero(void)
{
    /* Index 1: threshold=3.  After 3 pulses counter[1]=3 entering.
     * 3<3 false → bef4[1]=0.  Low=0 → no clamp change. */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 3; n++) stage_post_load_pulse_5fold(0, -1);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(1) == 0.0f);
    return 0;
}

int test_stage_post_load_435fbb_idx1_threshold_3_above_activates(void)
{
    /* Counter[1] reads its pre-increment value at the start of each
     * pulse, so after N pulses with no reset, counter[1] enters the
     * Nth call at N-1.  Sequence:
     *   pulse 1..4: counter ∈ {0,1,2,3} at entry → 3<counter false → bef4=0
     *   pulse 5:    counter=4 at entry → 3<4 true → pre=(4-3)*0.0005=0.0005
     * Final bef4[1] = 0.0005. */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 5; n++) stage_post_load_pulse_5fold(0, -1);
    float v = stage_post_load_get_dat_0438bef4_as_float(1);
    T_ASSERT(v > 0.00049f && v < 0.00051f);
    return 0;
}

int test_stage_post_load_435fbb_idx2_high_clamp_caps_at_0_2(void)
{
    /* Index 2: slope=0.04 + low=0.02 = pre grows fast.  After many
     * pulses bef4[2] saturates at high=0.2. */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 50; n++) stage_post_load_pulse_5fold(0, -1);
    T_ASSERT(stage_post_load_get_dat_0438bef4_as_float(2) == 0.2f);
    return 0;
}

/* ─── per-stage XP-base float (defaults to 0 with stage record unported) ── */

int test_stage_post_load_init_defaults_b91c_to_zero(void)
{
    stage_post_load_reset_for_test();
    stage_post_load_init();
    T_ASSERT(stage_post_load_get_dat_0438b91c() == 0.0f);
    return 0;
}

/* ─── idempotency ──────────────────────────────────────────────────────── */

int test_stage_post_load_init_idempotent(void)
{
    stage_post_load_reset_for_test();
    stage_post_load_set_chara_field(0, 0, 5);
    stage_post_load_init();
    stage_post_load_init();
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x12), 4500);
    T_ASSERT_EQ_I(stage_post_load_chara_field(0, 0x13), 6300);
    T_ASSERT_EQ_I(stage_post_load_get_dat_056da1d0(), 3);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438b4c4(), 1);
    return 0;
}
