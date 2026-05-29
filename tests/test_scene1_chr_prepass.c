/*
 * test_scene1_chr_prepass.c — Cchr.2e coverage.
 *
 * The records / people sprite pre-pass (engine FUN_0045672a) is dormant on
 * HOUSE entry (all three record tables empty) and its render body is Win32-
 * only, so the host-testable surface is the one genuinely non-trivial helper:
 * the index co-sort (engine FUN_0045526a) that depth-orders the people
 * billboards before they are drawn back-to-front.
 */
#include "t.h"

#include <stdint.h>

#include "scene1_chr_prepass.h"

/* ── chr_prepass_sort ───────────────────────────────────────────────────── */

int test_chr_prepass_sort_basic(void)
{
    int32_t keys[5] = { 5, 3, 4, 1, 2 };
    int32_t idx[5]  = { 0, 1, 2, 3, 4 };
    chr_prepass_sort(keys, idx, 5);
    /* keys ascending. */
    T_ASSERT_EQ_I(keys[0], 1);
    T_ASSERT_EQ_I(keys[1], 2);
    T_ASSERT_EQ_I(keys[2], 3);
    T_ASSERT_EQ_I(keys[3], 4);
    T_ASSERT_EQ_I(keys[4], 5);
    /* idx carried in lockstep (original positions of the sorted keys). */
    T_ASSERT_EQ_I(idx[0], 3);   /* key 1 was at index 3 */
    T_ASSERT_EQ_I(idx[1], 4);   /* key 2 was at index 4 */
    T_ASSERT_EQ_I(idx[2], 1);   /* key 3 was at index 1 */
    T_ASSERT_EQ_I(idx[3], 2);   /* key 4 was at index 2 */
    T_ASSERT_EQ_I(idx[4], 0);   /* key 5 was at index 0 */
    return 0;
}

int test_chr_prepass_sort_already_sorted(void)
{
    int32_t keys[4] = { -10, 0, 7, 99 };
    int32_t idx[4]  = { 0, 1, 2, 3 };
    chr_prepass_sort(keys, idx, 4);
    for (int i = 0; i < 4; i++)
        T_ASSERT_EQ_I(idx[i], i);
    T_ASSERT_EQ_I(keys[0], -10);
    T_ASSERT_EQ_I(keys[3], 99);
    return 0;
}

int test_chr_prepass_sort_negative_keys(void)
{
    /* Depth keys are signed (the engine __ftol-truncates a float depth). */
    int32_t keys[3] = { 2, -5, -1 };
    int32_t idx[3]  = { 10, 11, 12 };
    chr_prepass_sort(keys, idx, 3);
    T_ASSERT_EQ_I(keys[0], -5);
    T_ASSERT_EQ_I(keys[1], -1);
    T_ASSERT_EQ_I(keys[2], 2);
    T_ASSERT_EQ_I(idx[0], 11);
    T_ASSERT_EQ_I(idx[1], 12);
    T_ASSERT_EQ_I(idx[2], 10);
    return 0;
}

int test_chr_prepass_sort_stable_equal_keys(void)
{
    /* Equal keys: the engine's `<` compare never swaps equals, so their
     * relative order (hence carried indices) is preserved. */
    int32_t keys[4] = { 7, 7, 3, 7 };
    int32_t idx[4]  = { 0, 1, 2, 3 };
    chr_prepass_sort(keys, idx, 4);
    T_ASSERT_EQ_I(keys[0], 3);
    T_ASSERT_EQ_I(idx[0], 2);
    /* the three 7s keep their input order 0,1,3. */
    T_ASSERT_EQ_I(idx[1], 0);
    T_ASSERT_EQ_I(idx[2], 1);
    T_ASSERT_EQ_I(idx[3], 3);
    return 0;
}

int test_chr_prepass_sort_trivial_sizes(void)
{
    /* n <= 1 is a no-op (engine guards `0 < count-1`). */
    int32_t k1[1] = { 42 }, i1[1] = { 9 };
    chr_prepass_sort(k1, i1, 1);
    T_ASSERT_EQ_I(k1[0], 42);
    T_ASSERT_EQ_I(i1[0], 9);

    int32_t k0[1] = { 0 }, i0[1] = { 0 };
    chr_prepass_sort(k0, i0, 0);   /* must not touch memory */
    T_ASSERT_EQ_I(k0[0], 0);
    return 0;
}
