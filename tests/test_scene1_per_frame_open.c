/*
 * test_scene1_per_frame_open.c — unit tests for the per-frame open
 * chip ladder (scene1_per_frame_open.{c,h}):
 *   PFO.1 — Table A storage + sentinel init.
 *   PFO.2 — Parent template table storage + default-fill init.
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

/* ===== PFO.2 — parent template table tests ===== */

static void wipe_parent_table(void)
{
    memset(g_scene1_pfo_parent_table, 0, sizeof g_scene1_pfo_parent_table);
}

int test_pfo_parent_table_storage_size_matches_engine(void)
{
    /* Engine table spans 0x744580..0x769740 = 0x251c0 bytes = 152000 B
     * = 400 entries × 95 dw × 4 B.  (Anchored at DAT_007444e0 with the
     * init walk starting +0xa0 into the first entry.) */
    T_ASSERT_EQ_U(sizeof g_scene1_pfo_parent_table,
                  (size_t)(400 * 95 * sizeof(int32_t)));
    T_ASSERT_EQ_U(sizeof g_scene1_pfo_parent_table, (size_t)152000);
    return 0;
}

int test_pfo_parent_init_sentinel_minus_one_every_sub_record(void)
{
    /* Engine FUN_00412a89 L25: `puVar2[-7] = 0xffffffff;` writes -1 to
     * sub_rec[k].sentinel for k=0..6 across all 400 entries. */
    wipe_parent_table();
    scene1_pfo_parent_table_init();

    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        const int32_t *e = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            int32_t s = e[SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0 + k];
            if (s != -1) {
                T_FAIL("entry %d sub %d sentinel = %d, want -1", i, k, s);
            }
        }
    }
    return 0;
}

int test_pfo_parent_init_age_match_zero(void)
{
    /* `*puVar2 = 0;` writes 0 to sub_rec[k].age_match for k=0..6. */
    wipe_parent_table();
    /* Seed age_match to a non-zero value to prove init zeroes it back. */
    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            g_scene1_pfo_parent_table[
                i * SCENE1_PFO_PARENT_TABLE_STRIDE +
                SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0 + k] = 0x12345678;
        }
    }
    scene1_pfo_parent_table_init();

    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        const int32_t *e = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            int32_t v = e[SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0 + k];
            if (v != 0) {
                T_FAIL("entry %d sub %d age_match = 0x%08x, want 0",
                       i, k, (unsigned)v);
            }
        }
    }
    return 0;
}

int test_pfo_parent_init_rgba_quartet_is_100s(void)
{
    /* Engine writes (100, 100, 100, 100) to the 4-dw rgba quartet for
     * each sub-record (puVar1[-1] = 100; *puVar1 = 100; puVar1[1] = 100;
     * puVar1[2] = 100). */
    wipe_parent_table();
    scene1_pfo_parent_table_init();

    /* Spot-check a few entries across the table. */
    int sample_entries[] = { 0, 1, 99, 200, 399 };
    for (size_t s = 0; s < sizeof sample_entries / sizeof sample_entries[0];
         s++) {
        int i = sample_entries[s];
        const int32_t *e = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            for (int c = 0; c < 4; c++) {
                int32_t v = e[SCENE1_PFO_PARENT_OFF_SUB_RGBA_0 + k * 4 + c];
                if (v != 100) {
                    T_FAIL("entry %d sub %d rgba[%d] = %d, want 100",
                           i, k, c, v);
                }
            }
        }
    }
    return 0;
}

int test_pfo_parent_init_scale_mul_is_one_f(void)
{
    /* `puVar2[0x23] = 0x3f800000;` (= 1.0f) per sub-record. */
    wipe_parent_table();
    scene1_pfo_parent_table_init();

    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        const int32_t *e = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            int32_t v = e[SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0 + k];
            if (v != (int32_t)0x3f800000) {
                T_FAIL("entry %d sub %d scale_mul = 0x%08x, want 0x3f800000",
                       i, k, (unsigned)v);
            }
        }
    }
    return 0;
}

int test_pfo_parent_init_xyz_is_zero(void)
{
    /* `puVar3[-1] = 0; *puVar3 = 0; puVar3[1] = 0;` per sub-record xyz. */
    wipe_parent_table();
    /* Seed xyz with non-zero to prove init zeroes it. */
    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            for (int c = 0; c < 3; c++) {
                g_scene1_pfo_parent_table[
                    i * SCENE1_PFO_PARENT_TABLE_STRIDE +
                    SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + k * 3 + c] = 0x7fffffff;
            }
        }
    }
    scene1_pfo_parent_table_init();

    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        const int32_t *e = &g_scene1_pfo_parent_table[
            i * SCENE1_PFO_PARENT_TABLE_STRIDE];
        for (int k = 0; k < SCENE1_PFO_PARENT_TABLE_SUB_COUNT; k++) {
            for (int c = 0; c < 3; c++) {
                int32_t v = e[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + k * 3 + c];
                if (v != 0) {
                    T_FAIL("entry %d sub %d xyz[%d] = 0x%08x, want 0",
                           i, k, c, (unsigned)v);
                }
            }
        }
    }
    return 0;
}

int test_pfo_parent_init_preserves_preamble(void)
{
    /* Engine init writes the "<unknown>" name string into dw 0 via
     * FUN_005038ff but otherwise leaves the dw 0..24 preamble alone.
     * Our port skips the name string entirely (tick doesn't read it
     * and PFO.7 parser overwrites it from file).  Verify init does NOT
     * touch the preamble dwords. */
    wipe_parent_table();
    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        for (int f = 0; f < SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0; f++) {
            g_scene1_pfo_parent_table[
                i * SCENE1_PFO_PARENT_TABLE_STRIDE + f] =
                0x20000000 + i * 0x100 + f;
        }
    }

    scene1_pfo_parent_table_init();

    for (int i = 0; i < SCENE1_PFO_PARENT_TABLE_COUNT; i++) {
        for (int f = 0; f < SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0; f++) {
            int32_t got = g_scene1_pfo_parent_table[
                i * SCENE1_PFO_PARENT_TABLE_STRIDE + f];
            int32_t want = 0x20000000 + i * 0x100 + f;
            if (got != want) {
                T_FAIL("entry %d preamble dw %d: got 0x%08x want 0x%08x",
                       i, f, (unsigned)got, (unsigned)want);
            }
        }
    }
    return 0;
}

int test_pfo_parent_init_is_idempotent(void)
{
    /* Calling init twice produces the same state as calling it once. */
    wipe_parent_table();
    scene1_pfo_parent_table_init();

    /* Snapshot. */
    static int32_t snap[SCENE1_PFO_PARENT_TABLE_COUNT *
                        SCENE1_PFO_PARENT_TABLE_STRIDE];
    memcpy(snap, g_scene1_pfo_parent_table, sizeof snap);

    scene1_pfo_parent_table_init();

    if (memcmp(snap, g_scene1_pfo_parent_table, sizeof snap) != 0) {
        T_FAIL("second init produced different state from first");
    }
    return 0;
}

int test_pfo_parent_field_offsets_match_engine_layout(void)
{
    /* Sanity-check the per-entry layout used by FUN_00414929:
     *   piVar1 = &DAT_00744544 + iVar10 * 0x5f;
     *   pfVar3 = &DAT_0074460c + iVar10 * 0x5f;
     *
     * DAT_00744544 - DAT_007444e0 = 0x64 bytes = 25 dw → piVar1 anchors
     *                                                   at the sentinel block.
     * DAT_0074460c - DAT_007444e0 = 0x12c bytes = 75 dw → pfVar3 anchors
     *                                                    one dw past the
     *                                                    XYZ block start
     *                                                    (XYZ starts at
     *                                                    dw 74); the tick
     *                                                    indexes pfVar3[-1].
     *
     * Encode these invariants in the test. */
    T_ASSERT_EQ_I(SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0, 25);
    T_ASSERT_EQ_I(SCENE1_PFO_PARENT_OFF_SUB_XYZ_0,     74);

    /* piVar1[7]    = age_match → 25 + 7 = 32 ✓ */
    T_ASSERT_EQ_I(SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0 + 7,
                  SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0);

    /* piVar1[0x2a] = scale_mul → 25 + 0x2a = 67 ✓ */
    T_ASSERT_EQ_I(SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0 + 0x2a,
                  SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0);

    /* pfVar3[-1]   = xyz.x of sub 0 → 75 - 1 = 74 ✓ */
    T_ASSERT_EQ_I(SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + 1, 75);

    /* Stride covers everything up to but not past dw 95. */
    T_ASSERT_EQ_I(SCENE1_PFO_PARENT_OFF_SUB_XYZ_0 + 6 * 3 + 2,
                  SCENE1_PFO_PARENT_TABLE_STRIDE - 1);

    return 0;
}
