/*
 * test_xp_curve.c — tests for src/xp_curve.{c,h}.
 *
 * Covers the small but exact engine formula at FUN_0048a331:
 *
 *   xp_curve_threshold(level) = level * (level + 1) * 150  (level > 0)
 *                             = 0                            (level == 0)
 */

#include "t.h"
#include "xp_curve.h"

int test_xp_curve_zero_returns_zero(void)
{
    /* Engine guard: level == 0 short-circuits to 0 without the multiply. */
    T_ASSERT_EQ_I(xp_curve_threshold(0), 0);
    return 0;
}

int test_xp_curve_level_1_is_300(void)
{
    /* 1 * 2 * 150 = 300 */
    T_ASSERT_EQ_I(xp_curve_threshold(1), 300);
    return 0;
}

int test_xp_curve_low_levels_match_quadratic(void)
{
    /* Sanity check the first few levels match the formula. */
    T_ASSERT_EQ_I(xp_curve_threshold(2), 2 * 3 * 150);   /* 900 */
    T_ASSERT_EQ_I(xp_curve_threshold(3), 3 * 4 * 150);   /* 1800 */
    T_ASSERT_EQ_I(xp_curve_threshold(10), 10 * 11 * 150); /* 16500 */
    return 0;
}

int test_xp_curve_level_cap_97_no_overflow(void)
{
    /* Engine level cap is 97 (the level-up handler at FUN_0048a383
     * guards `level < 0x62 = 98`).  At level 97 the curve evaluates
     * to 97 * 98 * 150 = 1,425,900 — far inside int32 range. */
    T_ASSERT_EQ_I(xp_curve_threshold(97), 97 * 98 * 150);
    T_ASSERT_EQ_I(xp_curve_threshold(98), 98 * 99 * 150);
    return 0;
}

int test_xp_curve_negative_follows_formula(void)
{
    /* Negative input has no caller in the engine, but the pure formula
     * still yields a defined value: -1 * 0 * 150 = 0; -2 * -1 * 150 = 300. */
    T_ASSERT_EQ_I(xp_curve_threshold(-1), -1 * 0 * 150);
    T_ASSERT_EQ_I(xp_curve_threshold(-2), -2 * -1 * 150);
    return 0;
}
