/*
 * test_scene1_per_frame_open.c — unit tests for PFO.1: Table A
 * storage + sentinel init (scene1_per_frame_open.{c,h}).
 */

#include "t.h"

#include <string.h>

#include "scene1_per_frame_open.h"
#include "scene1_records.h"

/* Fresh-storage: zero the whole table, leaving every field including
 * SENTINEL at 0 (i.e. NOT the engine's "empty" sentinel of -1). */
static void wipe_table_a(void)
{
    memset(g_scene1_pfo_table_a, 0, sizeof g_scene1_pfo_table_a);
}

int test_pfo_init_sets_sentinel_to_minus_one_on_every_slot(void)
{
    wipe_table_a();
    scene1_pfo_table_a_init();

    for (int i = 0; i < SCENE1_PFO_TABLE_A_COUNT; i++) {
        int32_t s = g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE +
                                         SCENE1_PFO_TABLE_A_OFF_SENTINEL];
        if (s != -1) {
            T_FAIL("slot %d sentinel = %d, want -1", i, s);
        }
    }
    return 0;
}

int test_pfo_init_does_not_touch_other_fields(void)
{
    /* Engine FUN_00414902 only writes the sentinel field; other dws
     * keep whatever was there.  Seed every field of every slot with
     * a recognizable value, then verify init leaves everything except
     * the sentinel intact. */
    for (int i = 0; i < SCENE1_PFO_TABLE_A_COUNT; i++) {
        for (int f = 0; f < SCENE1_PFO_TABLE_A_STRIDE; f++) {
            g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE + f] =
                0x10000000 + i * 0x100 + f;
        }
    }

    scene1_pfo_table_a_init();

    for (int i = 0; i < SCENE1_PFO_TABLE_A_COUNT; i++) {
        for (int f = 0; f < SCENE1_PFO_TABLE_A_STRIDE; f++) {
            int32_t got = g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE
                                                + f];
            int32_t want = (f == SCENE1_PFO_TABLE_A_OFF_SENTINEL)
                ? -1
                : (0x10000000 + i * 0x100 + f);
            if (got != want) {
                T_FAIL("slot %d field %d: got 0x%08x want 0x%08x",
                       i, f, (unsigned)got, (unsigned)want);
            }
        }
    }
    return 0;
}

int test_scene1_records_reset_invokes_pfo_init(void)
{
    /* scene1_records_reset() must also wire the PFO Table A init,
     * matching the engine's FUN_0040f64b → FUN_00414902 call point.
     * Wipe table A first; if scene1_records_reset doesn't call our
     * init, slot 0's sentinel stays 0 (alive) instead of going to -1. */
    wipe_table_a();
    scene1_records_reset(/*reset_c=*/1);

    int32_t s0 = g_scene1_pfo_table_a[0 * SCENE1_PFO_TABLE_A_STRIDE +
                                      SCENE1_PFO_TABLE_A_OFF_SENTINEL];
    T_ASSERT_EQ_I(s0, -1);

    int32_t s_last = g_scene1_pfo_table_a[(SCENE1_PFO_TABLE_A_COUNT - 1) *
                                          SCENE1_PFO_TABLE_A_STRIDE +
                                          SCENE1_PFO_TABLE_A_OFF_SENTINEL];
    T_ASSERT_EQ_I(s_last, -1);

    return 0;
}

int test_scene1_records_reset_with_reset_c_zero_still_inits_pfo(void)
{
    /* The reset_c arg gates the records C reset but NOT the PFO init —
     * the engine's FUN_0040f64b unconditionally calls FUN_00414902
     * regardless of its param_1.  Verify our port matches. */
    wipe_table_a();
    scene1_records_reset(/*reset_c=*/0);

    int32_t s = g_scene1_pfo_table_a[42 * SCENE1_PFO_TABLE_A_STRIDE +
                                     SCENE1_PFO_TABLE_A_OFF_SENTINEL];
    T_ASSERT_EQ_I(s, -1);
    return 0;
}

int test_pfo_storage_size_matches_engine(void)
{
    /* Engine table spans 0x730c20..0x733820 = 0x2c00 bytes = 11264 B
     * = 256 slots × 44 B.  Verify our typed array matches that exact
     * byte count. */
    T_ASSERT_EQ_U(sizeof g_scene1_pfo_table_a,
                  (size_t)(256 * 11 * sizeof(int32_t)));
    T_ASSERT_EQ_U(sizeof g_scene1_pfo_table_a, (size_t)11264);
    return 0;
}
