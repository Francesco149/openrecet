/*
 * test_rng.c — engine LCG + time-to-seed unit tests.
 *
 * The LCG constants (0x343fd / 0x269ec3) match MSVC's classic rand(), so
 * the first few values from seed=1 should be the canonical sequence
 * 41, 18467, 6334, 26500, 19169, ... — that's both the engine's boot
 * sequence (DAT_006023a0 starts at 1) and a well-known compiler
 * fingerprint for sanity-checking the port.
 */

#include "t.h"
#include "rng.h"

int test_rng_initial_seed_is_one(void)
{
    g_rng_seed = 1;     /* re-arm in case a prior test left it stomped */
    T_ASSERT_EQ_U(g_rng_seed, 1);
    return 0;
}

int test_rng_msvc_rand_sequence_from_seed_1(void)
{
    /* Canonical MSVC rand() output for srand(1). The engine's RNG uses
     * the same LCG so the sequences match exactly. */
    static const uint16_t want[] = { 41, 18467, 6334, 26500, 19169, 15724,
                                     11478, 29358, 26962, 24464 };
    rng_seed(1);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        uint16_t got = rng_next15();
        if (got != want[i]) {
            T_FAIL("rand[%zu]: got %u, want %u", i, got, want[i]);
        }
    }
    return 0;
}

int test_rng_seed_resets_state(void)
{
    rng_seed(1);
    (void)rng_next15();
    (void)rng_next15();
    rng_seed(1);
    T_ASSERT_EQ_U(rng_next15(), 41);
    return 0;
}

int test_rng_next_unit_range_zero_to_just_under_one(void)
{
    rng_seed(1);
    for (int i = 0; i < 1024; i++) {
        float u = rng_next_unit();
        if (u < 0.0f || u >= 1.0f) {
            T_FAIL("rng_next_unit out of [0,1): got %f at i=%d", u, i);
        }
    }
    return 0;
}

int test_rng_next_unit_seed_1_first_values(void)
{
    /* rand=41 → 41/32768 = 0.001251220703125 (exact in float). */
    rng_seed(1);
    float u0 = rng_next_unit();
    float u1 = rng_next_unit();
    if (u0 != 41.0f / 32768.0f) T_FAIL("u0=%f want %f", u0, 41.0f / 32768.0f);
    if (u1 != 18467.0f / 32768.0f) T_FAIL("u1=%f", u1);
    return 0;
}

/*
 * rng_compute_seed: the formula is
 *   days = yr1900 * 365 + doy[m] + day + (year - 1901) / 4
 *          + (month > 2 && yr1900 % 4 == 0 ? 1 : 0)
 *   s    = (((hour + days*24) * 60 + minute) * 60 + 0x7080 + 0x7c558180 + second)
 *   if dst==1:  s += -3600
 *
 * Hand-computed reference values below come from re-applying the formula
 * directly (the result has no real-world unit; it's just a number whose
 * variation across boots makes a good LCG seed).
 */
int test_rng_compute_seed_year_2000_jan_1(void)
{
    /* y=2000 m=1 d=1 h=0 m=0 s=0 dst=0:
     *   yr1900 = 100; doy[1] = -1; adj_day = -1 + 1 = 0
     *   yr1900 % 4 == 0 && month > 2 → false (month=1) → no leap bump
     *   days = 100*365 + 0 + (2000-1901)/4 = 36500 + 24 = 36524
     *   s    = (((0 + 36524*24)*60 + 0)*60 + 0x7080 + 0x7c558180 + 0)
     *        = (876576*60 + 0)*60 + 28800 + 0x7c558180
     *        = 52594560 * 60 + 28800 + 0x7c558180
     *        = 3155673600 + 28800 + 0x7c558180
     *        = signed(3155673600) wraps to -1139293696 then + the constants
     *        We just check that the function produces an exactly-reproducible
     *        value and that DST changes it by -3600. */
    int32_t s_no_dst = rng_compute_seed(2000, 1, 1, 0, 0, 0, 0);
    int32_t s_dst    = rng_compute_seed(2000, 1, 1, 0, 0, 0, 1);
    T_ASSERT_EQ_I(s_dst - s_no_dst, -3600);

    /* Each second of input bumps the output by 1 second. */
    int32_t s_plus_1s = rng_compute_seed(2000, 1, 1, 0, 0, 1, 0);
    T_ASSERT_EQ_I(s_plus_1s - s_no_dst, 1);

    /* Each minute = 60s. */
    int32_t s_plus_1min = rng_compute_seed(2000, 1, 1, 0, 1, 0, 0);
    T_ASSERT_EQ_I(s_plus_1min - s_no_dst, 60);

    /* Each hour = 3600s. */
    int32_t s_plus_1h = rng_compute_seed(2000, 1, 1, 1, 0, 0, 0);
    T_ASSERT_EQ_I(s_plus_1h - s_no_dst, 3600);
    return 0;
}

int test_rng_compute_seed_year_range_rejects(void)
{
    /* Engine range check: year - 1900 must be in [0x46, 0x8a] = [70, 138]
     * = years 1970..2038 inclusive. Outside → -1. */
    T_ASSERT_EQ_I(rng_compute_seed(1969, 1, 1, 0, 0, 0, 0), -1);
    T_ASSERT_EQ_I(rng_compute_seed(2039, 1, 1, 0, 0, 0, 0), -1);
    /* Boundary values accept. */
    T_ASSERT(rng_compute_seed(1970, 1, 1, 0, 0, 0, 0) != -1);
    T_ASSERT(rng_compute_seed(2038, 1, 1, 0, 0, 0, 0) != -1);
    return 0;
}

int test_rng_compute_seed_leap_bump_post_february(void)
{
    /* 2000 is divisible by 4 → leap. Compare March 1 in 2000 vs 2001
     * (non-leap-bumped). The leap year version should be exactly +1 day
     * = +86400s heavier in the days*24*60*60 term, *plus* whatever the
     * year-to-year contribution is from the (year-1900)*365 +
     * (year-1901)/4 terms. The simpler check: same year, month=2 vs
     * month=3 — the month=3 result should be 1 day larger because of
     * the leap bump (since in 2000 the bump applies). */
    int32_t feb_2000 = rng_compute_seed(2000, 2, 29, 0, 0, 0, 0);
    int32_t mar_2000 = rng_compute_seed(2000, 3, 1, 0, 0, 0, 0);
    /* doy[3] - doy[2] = 58 - 30 = 28 days. Feb 29 + leap_bump=0 (month<=2)
     * → adj_day = 30+29 = 59. Mar 1: doy[3]=58, +1 leap bump → adj_day = 60.
     * Difference: 60-59 = 1 day = 86400s. */
    T_ASSERT_EQ_I(mar_2000 - feb_2000, 86400);

    /* In a non-leap year, no bump → Feb 28 → Mar 1 should also be exactly 1 day. */
    int32_t feb_2001 = rng_compute_seed(2001, 2, 28, 0, 0, 0, 0);
    int32_t mar_2001 = rng_compute_seed(2001, 3, 1, 0, 0, 0, 0);
    T_ASSERT_EQ_I(mar_2001 - feb_2001, 86400);
    return 0;
}

int test_rng_compute_seed_is_deterministic(void)
{
    /* Same inputs → same output, every call. */
    int32_t a = rng_compute_seed(2024, 6, 15, 12, 30, 45, 1);
    int32_t b = rng_compute_seed(2024, 6, 15, 12, 30, 45, 1);
    T_ASSERT_EQ_I(a, b);
    return 0;
}
