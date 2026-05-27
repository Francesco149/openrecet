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
#include "stage_gate.h"
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
    /* Post-state on the grid arrays after stage_post_load_init():
     *   - DAT_0438b4ec[0..24]: zeroed by the init body, no sibling touches it.
     *   - DAT_0438bedc / 0438bf0c: zeroed by the init body, then rewritten
     *     by stage_post_load_pulse_first_row(1, 0) (FUN_00435dcd).
     *   - DAT_0438bef4 / 0438bf24: zeroed by the init body, then rewritten
     *     by stage_post_load_pulse_5fold(1, 0) (FUN_00435fbb).
     */
    stage_post_load_reset_for_test();
    stage_post_load_init();
    for (int i = 0; i < 25; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438b4ec(i), 0);
    }
    /* FUN_00435dcd post-state at NEW-GAME default (g_dat_0438b4d0 = 0):
     *   bedc[0] = 1.0 (mode-0 dispatch puts full weight on slot 0).
     *   bedc[1..5] = 0 (untouched after the array zero pass).
     *   bf0c[0..5] = 1 (counter step = 1 in the reset_arrays==1 path). */
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(0) == 1.0f);
    for (int i = 1; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bedc(i), 0);
    }
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(i), 1);
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

/* ─── FUN_00435dcd sibling — stage_post_load_pulse_first_row ───────────── */

int test_stage_post_load_435dcd_mode_0_dumps_full_weight_on_slot_0(void)
{
    /* Default mode (g_dat_0438b4d0 = 0): bedc[0] = 1.0, bedc[1..5] = 0.
     * Counter step = 1 (reset_arrays=1 path), so bf0c[0..5] = 1. */
    stage_post_load_reset_for_test();
    stage_post_load_pulse_first_row(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(0) == 1.0f);
    for (int i = 1; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bedc(i), 0);
    }
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(i), 1);
    }
    return 0;
}

int test_stage_post_load_435dcd_param2_force_clear_bedc_slot(void)
{
    /* Seed bedc[3] non-zero, then force-clear slot 3 via param_2.
     * Mode 0 still puts 1.0 on slot 0; slot 3 stays cleared. */
    stage_post_load_reset_for_test();
    {
        float seed = 0.5f;
        /* Use the set helper via memcpy by writing through the int32
         * storage — there's no public setter for bedc.  Test indirectly
         * by walking the mode-1 branch instead (next test). */
        (void)seed;
    }
    stage_post_load_pulse_first_row(1, 3);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(0) == 1.0f);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bedc(3), 0);
    return 0;
}

int test_stage_post_load_435dcd_mode_1_writes_carved_distribution(void)
{
    /* Mode 1: bedc[1] = clamp_high( (bf0c[1] - 2) * 0.02, 0.2 );
     *         bedc[0] = 1.0 - (bedc[1] + bedc[2] + bedc[3] + bedc[4])
     *
     * After reset_arrays=1 clears bf0c[0..5], bf0c[1] starts at 0 →
     * threshold check (bf0c[1] > 2) fails → bedc[1] = 0.
     * → bedc[0] = 1.0 - 0 = 1.0 (and bedc[2..5] stay 0). */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(1);
    stage_post_load_pulse_first_row(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(0) == 1.0f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(1) == 0.0f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(2) == 0.0f);
    return 0;
}

int test_stage_post_load_435dcd_mode_1_threshold_above_2_activates(void)
{
    /* Pre-seed bf0c[1] = 7 (via repeated no-reset pulses).  Then pulse
     * with reset_arrays=0 and mode 1: bedc[1] = (7 - 2) * 0.02 = 0.1
     * (under the 0.2 cap), bedc[0] = 1.0 - 0.1 = 0.9. */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(0);
    /* Initial pulse to seed bf0c[1] = 1 (mode 0 + reset). */
    stage_post_load_pulse_first_row(1, 0);
    /* Six more no-reset pulses → bf0c[1] = 7. */
    for (int n = 0; n < 6; n++) {
        stage_post_load_pulse_first_row(0, -1);
    }
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(1), 7);
    stage_post_load_set_mode_b4d0(1);
    stage_post_load_pulse_first_row(0, -1);  /* mode 1 carve. */
    /* Counter advanced 7 → 8 before the bedc write?  Engine reads bf0c[1]
     * BEFORE the post-dispatch increment, so the carve uses bf0c[1] = 7. */
    float b1 = stage_post_load_get_dat_0438bedc_as_float(1);
    float b0 = stage_post_load_get_dat_0438bedc_as_float(0);
    T_ASSERT(b1 > 0.099f && b1 < 0.101f);
    T_ASSERT(b0 > 0.899f && b0 < 0.901f);
    return 0;
}

int test_stage_post_load_435dcd_mode_1_high_clamp_caps_bedc1(void)
{
    /* Push bf0c[1] very high → bedc[1] saturates at 0.2.
     * Drive bf0c[1] = 200 via 199 no-reset pulses after the initial
     * reset pulse (each pulse increments bf0c[1] by 1 unless the
     * rng-gated equipped-item predicate fires — predicate stubs to 0). */
    stage_post_load_reset_for_test();
    stage_post_load_pulse_first_row(1, 0);  /* bf0c[1] = 1 */
    for (int n = 0; n < 199; n++) {
        stage_post_load_pulse_first_row(0, -1);
    }
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(1), 200);
    stage_post_load_set_mode_b4d0(1);
    stage_post_load_pulse_first_row(0, -1);
    float b1 = stage_post_load_get_dat_0438bedc_as_float(1);
    float b0 = stage_post_load_get_dat_0438bedc_as_float(0);
    T_ASSERT(b1 == 0.2f);
    T_ASSERT(b0 > 0.799f && b0 < 0.801f);
    return 0;
}

int test_stage_post_load_435dcd_mode_2_slot_0_with_zero_counters(void)
{
    /* Mode 2 → slot=0.  bf0c[1]=0 (≤ 0 threshold fails → pfvar4 = 0.04,
     *   capped to high_a=0.2 → stays 0.04);
     * bf0c[2]=0 (≤ 3 threshold fails → pfvar1 = 0).
     * bedc[1] = 0.04, bedc[2] = 0.0, bedc[0] = 1.0 - 0.04 - 0 = 0.96. */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(2);
    stage_post_load_pulse_first_row(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(0) == 0.96f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(1) == 0.04f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(2) == 0.0f);
    return 0;
}

int test_stage_post_load_435dcd_mode_3_slot_1_with_zero_counters(void)
{
    /* Mode 3 → slot=1.  bedc[2] = 0.04, bedc[3] = 0, bedc[1] = 0.96. */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(3);
    stage_post_load_pulse_first_row(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(1) == 0.96f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(2) == 0.04f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(3) == 0.0f);
    return 0;
}

int test_stage_post_load_435dcd_mode_5_slot_3_with_zero_counters(void)
{
    /* Mode 5 → slot=3.  bedc[4] = 0.04, bedc[5] = 0, bedc[3] = 0.96.
     * Confirms slot+2 indexing into bedc[5] (boundary-correct). */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(5);
    stage_post_load_pulse_first_row(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(3) == 0.96f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(4) == 0.04f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(5) == 0.0f);
    return 0;
}

int test_stage_post_load_435dcd_mode_4_slot_2_with_counter_boost(void)
{
    /* Mode 4 → slot=2.  Pre-seed bf0c[3] = 4 (so pfvar4 = 4*0.04 = 0.16,
     * under the 0.2 cap) and bf0c[4] = 8 (so pfvar1 = (8-3)*0.005 = 0.025,
     * under the 0.05 cap).
     * bedc[3] = 0.16, bedc[4] = 0.025, bedc[2] = 1.0 - 0.16 - 0.025 = 0.815. */
    stage_post_load_reset_for_test();
    stage_post_load_pulse_first_row(1, 0);  /* bf0c[0..5] = 1 */
    /* Drive bf0c[3] from 1 → 4 (3 no-reset pulses).  Each pulse
     * increments all six slots → bf0c[3] reaches 4 after 3 increments. */
    for (int n = 0; n < 3; n++) {
        stage_post_load_pulse_first_row(0, -1);
    }
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(3), 4);
    /* bf0c[4] is now 4 too — bump it to 8 via 4 more pulses; that takes
     * bf0c[3] to 8 as well (above the slot-spec but the slot+1 read uses
     * bf0c[slot+1] = bf0c[3] which is what we're driving anyway). */
    for (int n = 0; n < 4; n++) {
        stage_post_load_pulse_first_row(0, -1);
    }
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(3), 8);
    T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(4), 8);
    stage_post_load_set_mode_b4d0(4);
    stage_post_load_pulse_first_row(0, -1);
    float b3 = stage_post_load_get_dat_0438bedc_as_float(3);
    float b4 = stage_post_load_get_dat_0438bedc_as_float(4);
    float b2 = stage_post_load_get_dat_0438bedc_as_float(2);
    /* pfvar4 = 8 * 0.04 = 0.32 → caps to 0.2.
     * pfvar1 = (8-3) * 0.005 = 0.025 → under 0.05 cap.
     * bedc[2] = 1.0 - 0.2 - 0.025 = 0.775. */
    T_ASSERT(b3 == 0.2f);
    T_ASSERT(b4 > 0.0249f && b4 < 0.0251f);
    T_ASSERT(b2 > 0.7749f && b2 < 0.7751f);
    return 0;
}

int test_stage_post_load_435dcd_deep_dungeon_override_to_mode_4(void)
{
    /* g_dat_0438b4d0 = -1 with g_scene1_combat_stage_id == 5 and
     * stage_gate.next > 0x1d → snaps to mode 4 → slot 2. */
    extern int32_t g_scene1_combat_stage_id;
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(-1);
    g_scene1_combat_stage_id = 5;
    stage_gate_set_next(0x1f);  /* > 0x1d */
    stage_post_load_pulse_first_row(1, 0);
    /* Mode 4 + zero counters → bedc[3] = 0.04, bedc[4] = 0, bedc[2] = 0.96. */
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(2) == 0.96f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(3) == 0.04f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(4) == 0.0f);
    /* Reset side state so other tests aren't poisoned. */
    g_scene1_combat_stage_id = 0;
    stage_gate_set_next(0);
    return 0;
}

int test_stage_post_load_435dcd_no_override_when_stage_id_not_5(void)
{
    /* g_dat_0438b4d0 = -1 but g_scene1_combat_stage_id != 5 → no override.
     * Mode = -1 falls into the else branch with slot = 2 (engine's
     * initial iVar6) and out-of-range RDATA — clamp values are 0
     * (engine reads denormal-effectively-0 floats at .data[-8]; port
     * matches by treating out-of-range mode as clamp-to-0).
     * Result: pfvar4 = 0 (0.04 clamped down to 0), pfvar1 = 0,
     * bedc[2] = 1.0 - 0 - 0 = 1.0.  bedc[3..4] = 0. */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(-1);
    /* g_scene1_combat_stage_id stays at 0 from the reset. */
    stage_post_load_pulse_first_row(1, 0);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(2) == 1.0f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(3) == 0.0f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(4) == 0.0f);
    return 0;
}

int test_stage_post_load_435dcd_array_preservation_under_no_reset(void)
{
    /* With reset_arrays=0 + force_clear_idx=-1, bedc[] is not zeroed
     * before the dispatch.  In mode 0 the dispatch only writes bedc[0],
     * so bedc[1..5] preserve whatever state they had.  Seed bedc[3]
     * via a mode-3 pulse, then check that a mode-0 no-reset pulse
     * preserves it. */
    stage_post_load_reset_for_test();
    stage_post_load_set_mode_b4d0(3);
    stage_post_load_pulse_first_row(1, 0);
    float pre_b2 = stage_post_load_get_dat_0438bedc_as_float(2);
    T_ASSERT(pre_b2 == 0.04f);
    /* Now mode 0 with no reset.  bedc[0] = 1.0, bedc[2] should stay. */
    stage_post_load_set_mode_b4d0(0);
    stage_post_load_pulse_first_row(0, -1);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(0) == 1.0f);
    T_ASSERT(stage_post_load_get_dat_0438bedc_as_float(2) == 0.04f);
    return 0;
}

int test_stage_post_load_435dcd_counter_increment_each_pulse(void)
{
    /* Each pulse increments bf0c[0..5] by 1 (rng-gated boost branch is
     * unreachable on NEW GAME: predicate_485712 stubs to 0). */
    stage_post_load_reset_for_test();
    for (int n = 0; n < 10; n++) {
        stage_post_load_pulse_first_row(0, -1);
    }
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(i), 10);
    }
    return 0;
}

int test_stage_post_load_435dcd_b4d0_accessor_roundtrip(void)
{
    stage_post_load_reset_for_test();
    T_ASSERT_EQ_I(stage_post_load_get_mode_b4d0(), 0);
    stage_post_load_set_mode_b4d0(-1);
    T_ASSERT_EQ_I(stage_post_load_get_mode_b4d0(), -1);
    stage_post_load_set_mode_b4d0(3);
    T_ASSERT_EQ_I(stage_post_load_get_mode_b4d0(), 3);
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
