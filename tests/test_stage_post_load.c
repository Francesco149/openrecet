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
    /* Post-state should match BSS-zero on all four scratch arrays plus
     * DAT_0438b4ec.  Tested via BSS-zero starting state — the function
     * either writes 0 explicitly or leaves them alone; either way the
     * post-state matches the requirement. */
    stage_post_load_reset_for_test();
    stage_post_load_init();
    for (int i = 0; i < 25; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438b4ec(i), 0);
    }
    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bedc(i), 0);
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bef4(i), 0);
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf0c(i), 0);
        T_ASSERT_EQ_I(stage_post_load_get_dat_0438bf24(i), 0);
    }
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
