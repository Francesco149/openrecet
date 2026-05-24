/*
 * test_scene1_per_frame_open.c — unit tests for the per-frame open
 * chip ladder (scene1_per_frame_open.{c,h}):
 *   PFO.1 — Table A storage + sentinel init.
 *   PFO.2 — Parent template table storage + default-fill init.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "scene1_overlay.h"
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

/* ===== PFO.2.1 — overlay slot sentinel-init wired into records_reset ===== */

int test_scene1_records_reset_sentinel_inits_overlay_slots(void)
{
    /* Engine FUN_00414902's FIRST loop sentinel-inits the 4096 overlay
     * slots' ACTIVE field to -1.  scene1_records_reset must wire the
     * Table B (overlay) half alongside the Table A half (PFO.1).
     * Wipe overlay slots first; if the reset doesn't call
     * scene1_overlay_reset, every slot's ACTIVE stays at 0. */
    memset(g_scene1_overlay_slots, 0, sizeof g_scene1_overlay_slots);
    scene1_records_reset(/*reset_c=*/1);

    /* Spot-check first, middle, and last slots. */
    int sample[] = { 0, 1, 2047, 2048, 4094, 4095 };
    for (size_t i = 0; i < sizeof sample / sizeof sample[0]; i++) {
        int s = sample[i];
        int32_t v = g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE +
                                            SCENE1_OVERLAY_OFF_ACTIVE];
        if (v != -1) {
            T_FAIL("overlay slot %d ACTIVE = %d, want -1", s, v);
        }
    }
    return 0;
}

int test_scene1_records_reset_with_reset_c_zero_still_inits_overlay(void)
{
    /* The reset_c arg gates the records C reset but NOT the overlay
     * slot reset — engine FUN_0040f64b unconditionally calls
     * FUN_00414902 regardless of its param_1. */
    memset(g_scene1_overlay_slots, 0, sizeof g_scene1_overlay_slots);
    scene1_records_reset(/*reset_c=*/0);

    int32_t v = g_scene1_overlay_slots[42 * SCENE1_OVERLAY_SLOT_STRIDE +
                                        SCENE1_OVERLAY_OFF_ACTIVE];
    T_ASSERT_EQ_I(v, -1);
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

/* ===== PFO.3 — Table B per-tick body tests ============================ */

static int32_t pfo_f_to_bits(float f)
{
    int32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return bits;
}
static float pfo_bits_to_f(int32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Setup a single overlay slot at index `s` with the engine's "live"
 * default state: ACTIVE != -1, AGE = 0, TEXTURE_TYPE = 0, and no kill
 * gate (FADE_OUT_OFFSET = -1).  Other consumers seed their own fields.
 * Also resets the shape table to BSS-zero (any prior test's
 * shapes[0][FRAME_PERIOD] mutation would otherwise leak into the anim
 * cell tick and surprise downstream tests). */
static void setup_live_slot(int s, int32_t shape_mode, int32_t type_shape)
{
    scene1_overlay_reset();
    scene1_overlay_shapes_reset();
    int base = s * SCENE1_OVERLAY_SLOT_STRIDE;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ACTIVE]            = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE]               = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_TEXTURE_TYPE]      = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SHAPE_MODE]        = shape_mode;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_TYPE_SHAPE]        = type_shape;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET]   = -1;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE_BIRTH]         = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SCALE_X]           = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_TEMPLATE5_COPY]    = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_TEMPLATE11_COPY]   = pfo_f_to_bits(0.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_UNK_48]            = pfo_f_to_bits(0.0f);
}

int test_pfo_b_tick_skips_inactive_slot(void)
{
    scene1_overlay_reset();
    /* Slot 0's ACTIVE is -1 by default — verify the tick leaves
     * everything else untouched (no anim-counter increment, no AGE++). */
    g_scene1_overlay_slots[0 * SCENE1_OVERLAY_SLOT_STRIDE +
                           SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER] = 0;

    scene1_pfo_table_b_tick();

    int32_t fc = g_scene1_overlay_slots[0 * SCENE1_OVERLAY_SLOT_STRIDE +
                                        SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER];
    T_ASSERT_EQ_I(fc, 0);
    int32_t age = g_scene1_overlay_slots[0 * SCENE1_OVERLAY_SLOT_STRIDE +
                                         SCENE1_OVERLAY_OFF_AGE];
    T_ASSERT_EQ_I(age, 0);
    return 0;
}

int test_pfo_b_tick_anim_frame_counter_increments(void)
{
    setup_live_slot(0, /*shape_mode=*/0, /*type_shape=*/0);
    /* Shape entry left BSS-zero → FRAME_PERIOD == 0 → no cell advance,
     * but the frame counter still increments unconditionally each tick. */
    scene1_pfo_table_b_tick();
    int32_t fc = g_scene1_overlay_slots[0 * SCENE1_OVERLAY_SLOT_STRIDE +
                                        SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER];
    T_ASSERT_EQ_I(fc, 1);

    scene1_pfo_table_b_tick();
    fc = g_scene1_overlay_slots[0 * SCENE1_OVERLAY_SLOT_STRIDE +
                                SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER];
    T_ASSERT_EQ_I(fc, 2);
    return 0;
}

int test_pfo_b_tick_anim_cell_advances_at_period_and_clamps(void)
{
    setup_live_slot(0, /*shape_mode=*/0, /*type_shape=*/0);
    /* Shape 0 with FRAME_PERIOD=3, FRAME_COUNT=4, LOOP_MODE=0 (clamp). */
    g_scene1_overlay_shapes[0 * SCENE1_OVERLAY_SHAPE_STRIDE +
                            SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD] = 3;
    g_scene1_overlay_shapes[0 * SCENE1_OVERLAY_SHAPE_STRIDE +
                            SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT]  = 4;
    g_scene1_overlay_shapes[0 * SCENE1_OVERLAY_SHAPE_STRIDE +
                            SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE]    = 0;

    /* Tick 3 times: each tick fc++, at fc==3 reset to 0 and bump cell. */
    scene1_pfo_table_b_tick(); /* fc 1 */
    scene1_pfo_table_b_tick(); /* fc 2 */
    scene1_pfo_table_b_tick(); /* fc==3 → reset to 0; cell 0→1 */
    int base = 0;
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER], 0);
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX], 1);

    /* 9 more ticks total → cell 1→2→3→clamp at 3. */
    for (int i = 0; i < 9; i++) scene1_pfo_table_b_tick();
    /* After 9 more ticks: 3+9 = 12 ticks total since start.  Frame
     * resets at 3,6,9,12 → 4 cell advances total → cell would be 4,
     * clamped to FRAME_COUNT-1 = 3. */
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX], 3);
    return 0;
}

int test_pfo_b_tick_anim_cell_wraps_when_loop_mode_1(void)
{
    setup_live_slot(0, /*shape_mode=*/0, /*type_shape=*/0);
    g_scene1_overlay_shapes[0 * SCENE1_OVERLAY_SHAPE_STRIDE +
                            SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD] = 1;
    g_scene1_overlay_shapes[0 * SCENE1_OVERLAY_SHAPE_STRIDE +
                            SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT]  = 3;
    g_scene1_overlay_shapes[0 * SCENE1_OVERLAY_SHAPE_STRIDE +
                            SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE]    = 1;

    /* 3 ticks → cell goes 0→1→2→wrap to 0 (loop mode 1 wraps). */
    scene1_pfo_table_b_tick();
    scene1_pfo_table_b_tick();
    scene1_pfo_table_b_tick();
    int base = 0;
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX], 0);
    return 0;
}

int test_pfo_b_tick_anim_runs_when_age_negative(void)
{
    setup_live_slot(0, /*shape_mode=*/0, /*type_shape=*/0);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE] = -5;
    scene1_pfo_table_b_tick();
    /* Anim frame counter increments even for AGE < 0. */
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER], 1);
    /* But integrator was skipped — pos/bend remain BSS-zero. */
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X], 0);
    /* AGE still increments. */
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE], -4);
    return 0;
}

int test_pfo_b_tick_default_integrator_pos_plus_vel(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]  = pfo_f_to_bits(10.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Y]  = pfo_f_to_bits(20.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Z]  = pfo_f_to_bits(30.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X] = pfo_f_to_bits(1.5f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y] = pfo_f_to_bits(-2.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z] = pfo_f_to_bits(0.25f);

    scene1_pfo_table_b_tick();

    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]) - 11.5f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Y]) - 18.0f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Z]) - 30.25f) < 1e-5f);
    return 0;
}

int test_pfo_b_tick_type_8_9_10_advances_rot_y(void)
{
    int base = 0;
    for (int ts = 8; ts <= 10; ts++) {
        setup_live_slot(0, /*shape_mode=*/2 /* not 1/6 */, /*type_shape=*/ts);
        g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]  = pfo_f_to_bits(5.0f);
        g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X] = pfo_f_to_bits(1.0f);
        g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y] = pfo_f_to_bits(0.5f);
        g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ROT_Y]  = pfo_f_to_bits(10.0f);

        scene1_pfo_table_b_tick();

        /* ROT_Y += BEND_Y. */
        T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ROT_Y]) - 10.5f) < 1e-5f);
        /* POS unchanged (no default add path for 8/9/10). */
        T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]) - 5.0f) < 1e-5f);
    }
    return 0;
}

int test_pfo_b_tick_type_1_null_owner_uses_zero_matrix(void)
{
    setup_live_slot(0, /*shape_mode=*/1, /*type_shape=*/0);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_OWNER_A]    = 0;   /* null */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X_COPY] = pfo_f_to_bits(7.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Y_COPY] = pfo_f_to_bits(8.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Z_COPY] = pfo_f_to_bits(9.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_X]      = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_Y]      = pfo_f_to_bits(2.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_Z]      = pfo_f_to_bits(3.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]     = pfo_f_to_bits(0.1f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y]     = pfo_f_to_bits(0.2f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z]     = pfo_f_to_bits(0.3f);

    scene1_pfo_table_b_tick();

    /* accum (= VEL) += vel (= BEND).  accum = (1.1, 2.2, 3.3). */
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_X]) - 1.1f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_Y]) - 2.2f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_Z]) - 3.3f) < 1e-5f);

    /* pos = COPY + matrix(0) + accum_NEW = (7+1.1, 8+2.2, 9+3.3). */
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]) - 8.1f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Y]) - 10.2f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Z]) - 12.3f) < 1e-5f);
    return 0;
}

int test_pfo_b_tick_type_6_null_owner_uses_zero_matrix(void)
{
    /* Same as type 1 but shape_mode==6 reads OWNER_B instead.  Verify
     * a null OWNER_B short-circuits the matrix add the same way. */
    setup_live_slot(0, /*shape_mode=*/6, /*type_shape=*/0);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_OWNER_B]    = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X_COPY] = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_X]      = pfo_f_to_bits(2.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]     = pfo_f_to_bits(3.0f);

    scene1_pfo_table_b_tick();

    /* accum_x_new = 2 + 3 = 5; pos_x = 1 + 0 + 5 = 6. */
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_VEL_X]) - 5.0f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]) - 6.0f) < 1e-5f);
    return 0;
}

int test_pfo_b_tick_drag_and_gravity_modify_bend(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]          = pfo_f_to_bits(2.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y]          = pfo_f_to_bits(2.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z]          = pfo_f_to_bits(2.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_TEMPLATE5_COPY]  = pfo_f_to_bits(0.5f);   /* drag */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_UNK_48]          = pfo_f_to_bits(-0.1f);  /* gravity */

    scene1_pfo_table_b_tick();

    /* Drag is applied first: BEND *= 0.5 → (1, 1, 1).
     * Then gravity: BEND_Y += -0.1 → (1, 0.9, 1). */
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]) - 1.0f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y]) - 0.9f) < 1e-5f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z]) - 1.0f) < 1e-5f);
    return 0;
}

int test_pfo_b_tick_energy_decay(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SCALE_X]         = pfo_f_to_bits(10.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_TEMPLATE11_COPY] = pfo_f_to_bits(-1.5f);

    scene1_pfo_table_b_tick();

    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SCALE_X]) - 8.5f) < 1e-5f);
    return 0;
}

int test_pfo_b_tick_age_increments_always(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE] = -3;
    scene1_pfo_table_b_tick();
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE], -2);

    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE] = 7;
    scene1_pfo_table_b_tick();
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE], 8);
    return 0;
}

int test_pfo_b_tick_kill_when_age_exceeds_fade_off(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 5;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE_BIRTH]       = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE]             = 4;   /* AGE+1=5, hits gate */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SCALE_X]         = pfo_f_to_bits(1.0f);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ACTIVE], -1);
    return 0;
}

int test_pfo_b_tick_kill_when_energy_zero_or_below(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 1000;   /* effectively no age gate */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SCALE_X]         = pfo_f_to_bits(-0.1f);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ACTIVE], -1);
    return 0;
}

int test_pfo_b_tick_no_kill_when_fade_off_minus_one(void)
{
    setup_live_slot(0, /*shape_mode=*/2, /*type_shape=*/2);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = -1;     /* immortal */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_SCALE_X]         = pfo_f_to_bits(-1.0f);  /* but negative energy */

    scene1_pfo_table_b_tick();

    /* FADE_OUT_OFFSET==-1 unconditionally bypasses kill, even with
     * negative SCALE_X. */
    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ACTIVE], 0);
    return 0;
}

int test_pfo_b_tick_type_4_with_unk_48_bypasses_age_kill(void)
{
    /* The age-kill check's `!(shape_mode==4 && UNK_48!=0)` gate routes
     * kill responsibility to the PFO.4 type-4 body (which only kills on
     * terminal-distance conditions).  Place the slot far from the
     * (13.2, -10.8, -520) target so the terminal check doesn't fire,
     * verify FADE_OUT_OFFSET-based age-kill is bypassed. */
    setup_live_slot(0, /*shape_mode=*/4, /*type_shape=*/0);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_UNK_48]          = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 1;     /* would normally kill */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE]             = 100;
    /* pos=(0,0,0): |target-pos|≈520, pos.y=0 > target.y=-10.8, no
     * terminal kill. */

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ACTIVE], 0);
    return 0;
}

int test_pfo_b_tick_field_renames_match_offsets(void)
{
    /* PFO.3 renamed two O.2 field-name labels.  The numeric offsets
     * must stay the same so the on-disk slot layout is unchanged. */
    T_ASSERT_EQ_I(SCENE1_OVERLAY_OFF_ANIM_FRAME_COUNTER, 31);
    T_ASSERT_EQ_I(SCENE1_OVERLAY_OFF_ANIM_CELL_INDEX,    32);
    return 0;
}

/* ===== PFO.4 — type-4 shop-walker physics body ======================== */

/* Per-slot type-4 setup: SHAPE_MODE=4, UNK_48 non-zero, AGE past the
 * stagger gate, pos at origin, vel zero, TEMPLATE5_COPY=1 (no drag).
 * UNK_48 must be non-zero else the type-4 body's outer gate skips it. */
static void setup_type_4_slot(int s, int32_t age, float unk_48,
                              float pos_x, float pos_y, float pos_z)
{
    setup_live_slot(s, /*shape_mode=*/4, /*type_shape=*/0);
    int base = s * SCENE1_OVERLAY_SLOT_STRIDE;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_UNK_48] = pfo_f_to_bits(unk_48);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE]    = age;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_X]  = pfo_f_to_bits(pos_x);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Y]  = pfo_f_to_bits(pos_y);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_POS_Z]  = pfo_f_to_bits(pos_z);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X] = pfo_f_to_bits(0.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y] = pfo_f_to_bits(0.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z] = pfo_f_to_bits(0.0f);
    /* FADE_OUT_OFFSET = -1 from setup_live_slot disables age-kill. */
}

static int g_pfo_type_4_kill_count;
static int g_pfo_type_4_last_kill_slot;
static void pfo_type_4_kill_recorder(int slot_idx)
{
    g_pfo_type_4_kill_count++;
    g_pfo_type_4_last_kill_slot = slot_idx;
}

int test_pfo_b_tick_type_4_skipped_when_unk_48_zero(void)
{
    /* SHAPE_MODE==4 alone isn't enough — UNK_48 must also be non-zero.
     * Place at the terminal-kill spot and confirm no kill fires. */
    setup_type_4_slot(0, /*age=*/100, /*unk_48=*/0.0f,
                      /*pos=*/13.2f, -10.8f, -520.0f);
    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 0);
    /* The age-kill bypass also requires UNK_48!=0; with UNK_48=0,
     * fade_off=-1 still keeps it alive, but the type-4 body never
     * runs.  ACTIVE stays 0. */
    T_ASSERT_EQ_I(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_ACTIVE], 0);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

int test_pfo_b_tick_type_4_skipped_below_gate(void)
{
    /* Gate is `AGE > 30 + (slot_idx % 4)`.  For slot 0, gate opens at
     * AGE > 30.  AGE = 30 should NOT trigger the body — verify by
     * placing at terminal-kill spot and confirming no kill. */
    setup_type_4_slot(0, /*age=*/30, /*unk_48=*/1.0f,
                      /*pos=*/13.2f, -10.8f, -520.0f);
    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 0);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

int test_pfo_b_tick_type_4_per_slot_stagger(void)
{
    /* Gate is AGE > 30 + (slot_idx % 4).  Slot 0 opens at AGE=31, slot
     * 1 at 32, slot 2 at 33, slot 3 at 34.  Verify slot 1 with AGE=31
     * does NOT trigger but slot 0 does.
     *
     * setup_type_4_slot calls scene1_overlay_reset internally so it
     * wipes all slots.  Set up slot 0 via the helper, then inline-set
     * slot 1's fields without wiping. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/13.2f, -10.8f, -520.0f);
    int base1 = 1 * SCENE1_OVERLAY_SLOT_STRIDE;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_ACTIVE]          = 0;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_AGE]             = 31;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_TEXTURE_TYPE]    = 0;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_SHAPE_MODE]      = 4;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_TYPE_SHAPE]      = 0;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = -1;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_AGE_BIRTH]       = 0;
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_SCALE_X]         = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_TEMPLATE5_COPY]  = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_TEMPLATE11_COPY] = pfo_f_to_bits(0.0f);
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_UNK_48]          = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_POS_X] = pfo_f_to_bits(13.2f);
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_POS_Y] = pfo_f_to_bits(-10.8f);
    g_scene1_overlay_slots[base1 + SCENE1_OVERLAY_OFF_POS_Z] = pfo_f_to_bits(-520.0f);

    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    scene1_pfo_table_b_tick();

    /* slot 0: gate 30, AGE=31 → open → terminal kill fires.
     * slot 1: gate 31, AGE=31 → NOT open → no kill. */
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 1);
    T_ASSERT_EQ_I(g_pfo_type_4_last_kill_slot, 0);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

int test_pfo_b_tick_type_4_unk_48_decays(void)
{
    /* UNK_48 *= 0.8 every tick the type-4 body fires.  Start at 1.0,
     * AGE=31 (slot 0 gate open).  Place far from target so terminal
     * kill doesn't fire; only the decay matters. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/0.0f, 100.0f, 0.0f);

    scene1_pfo_table_b_tick();

    float u = pfo_bits_to_f(g_scene1_overlay_slots[
        0 + SCENE1_OVERLAY_OFF_UNK_48]);
    T_ASSERT(fabsf(u - 0.8f) < 1e-6f);
    return 0;
}

int test_pfo_b_tick_type_4_terminal_kill_at_target(void)
{
    /* When |target - pos| < 0.5 the slot self-kills.  Target is
     * (13.2, -10.8, -520).  Place pos AT the target so distance is 0. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/13.2f, -10.8f, -520.0f);
    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_ACTIVE], -1);
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 1);
    T_ASSERT_EQ_I(g_pfo_type_4_last_kill_slot, 0);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

int test_pfo_b_tick_type_4_terminal_kill_when_below_target_y(void)
{
    /* The terminal check fires on `|dist|<0.5 OR pos.y<target.y`.
     * Place far from target horizontally but with pos.y just below
     * target.y (-10.8). */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/0.0f, /*pos.y=*/-20.0f, 0.0f);
    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_ACTIVE], -1);
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 1);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

int test_pfo_b_tick_type_4_no_kill_when_far_from_target(void)
{
    /* Place pos at origin: dist≈520, pos.y=0>target.y=-10.8.  Neither
     * terminal condition fires.  Verify slot survives and ACTIVE
     * stays 0. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/0.0f, 0.0f, 0.0f);
    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_ACTIVE], 0);
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 0);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

int test_pfo_b_tick_type_4_vel_normalized_toward_target(void)
{
    /* When |target-pos| > 0.1 (always true at origin with target ≈ 520
     * away), the scaled delta gets normalized to length 0.1.  Verify
     * the post-tick BEND has |BEND|≈0.1 from origin.  (Note: gravity
     * adds UNK_48 to BEND_Y first, so we set UNK_48 small to keep the
     * |vel|>1 normalize path quiet — direction is what we're measuring.) */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/0.001f,
                      /*pos=*/0.0f, 0.0f, 0.0f);
    /* AGE=31 with slot 0 gate=30 → body fires.  No drag2 (AGE not > 40). */

    scene1_pfo_table_b_tick();

    float bx = pfo_bits_to_f(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_BEND_X]);
    float by = pfo_bits_to_f(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_BEND_Y]);
    float bz = pfo_bits_to_f(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_BEND_Z]);
    /* After gravity: by += UNK_48 = 0.0008 (after *0.8 decay it's 0.0008,
     * but gravity uses the PRE-decay 0.001).  Pre-PFO.4: by≈0.001.
     * Then PFO.4 adds normalized delta of magnitude 0.1 in direction
     * (target-pos)/dist.  target-pos = (13.2, -10.8, -520),
     * dist≈520.36; dz dominates.
     * dx = 13.2*0.1/520.36 ≈ 0.00254
     * dy = -10.8*0.1/520.36 ≈ -0.00208
     * dz = -520*0.1/520.36 ≈ -0.0999  (z uses *0.2 raw scale before
     *   normalize; raw delta is dx_raw*0.1, dy_raw*0.1, dz_raw*0.2)
     *
     * Hmm — the raw deltas in the engine are dx*0.1, dy*0.1, dz*0.2
     * BEFORE normalization.  Normalize uses sqrt of (dx_scaled² +
     * dy_scaled² + dz_scaled²) which weights z more.  Then divides
     * each by that magnitude * 0.1.  Net: vel direction matches the
     * SCALED (not raw) delta direction.
     *
     * Just verify |vel| ≈ 0.1 (modulo the tiny gravity addition). */
    float vmag = sqrtf(bx*bx + by*by + bz*bz);
    T_ASSERT(fabsf(vmag - 0.1f) < 0.005f);
    /* And direction is mostly -Z. */
    T_ASSERT(bz < -0.05f);
    return 0;
}

int test_pfo_b_tick_type_4_vel_clamped_to_unit(void)
{
    /* When |vel| > 1.0, vel gets normalized to unit.  Pre-set BEND to
     * (5, 5, 5) which has magnitude ≈ 8.66.  After type-4 body's delta
     * add (tiny ~0.1 step) and no drag2 (AGE=31), the vel-clamp must
     * normalize to unit. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/0.001f,
                      /*pos=*/0.0f, 0.0f, 0.0f);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X] = pfo_f_to_bits(5.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y] = pfo_f_to_bits(5.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z] = pfo_f_to_bits(5.0f);

    scene1_pfo_table_b_tick();

    float bx = pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]);
    float by = pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y]);
    float bz = pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z]);
    float vmag = sqrtf(bx*bx + by*by + bz*bz);
    T_ASSERT(fabsf(vmag - 1.0f) < 0.01f);
    return 0;
}

int test_pfo_b_tick_type_4_drag2_kicks_in_above_age_40(void)
{
    /* AGE > 40 applies a gradual drag scale: max(0.97, 1.0 -
     * (AGE-40)*0.002).  Set BEND to a known unit-length vector (so the
     * |vel|>1 normalize doesn't fire) and AGE=50 → drag2 = 1 - 10*0.002
     * = 0.98 (above 0.97 floor).  Post-tick BEND magnitude should be
     * ~0.98 (after the small delta add, the drag scale rules). */
    setup_type_4_slot(0, /*age=*/50, /*unk_48=*/0.001f,
                      /*pos=*/0.0f, 0.0f, 0.0f);
    int base = 0;
    /* Pre-set BEND to (1, 0, 0); after gravity by += 0.001, then the
     * PFO.4 delta gets added (~0.1 length), then *0.98 drag2, then
     * |vel|>1 normalize might fire depending on magnitudes. */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X] = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y] = pfo_f_to_bits(0.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z] = pfo_f_to_bits(0.0f);

    scene1_pfo_table_b_tick();

    /* Sanity: BEND mutated.  Detailed magnitude depends on interaction
     * with normalize step; the precise expected value is what the
     * engine computes.  Verify BEND_X dropped from 1.0 (drag * 0.98
     * applied) — even with the small delta-add boost, |vel| stays
     * close to 1, so the unit-normalize might or might not fire.  Just
     * confirm vel is not still 1.0 exactly (drag2 has applied). */
    float bx = pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]);
    /* bx is roughly: ((1.0 + 0.00254_delta) * 0.98) ≈ 0.982, then
     * possibly /|vel| if |vel|>1.  Either way bx < 1.0. */
    T_ASSERT(bx < 1.0f);
    T_ASSERT(bx > 0.95f);
    return 0;
}

int test_pfo_b_tick_type_4_drag2_clamped_at_0_97(void)
{
    /* AGE very high → (AGE-40)*0.002 grows large → 1.0 - x goes below
     * 0.97 → clamp at 0.97.  AGE=1000 → 1 - 960*0.002 = -0.92 → clamp
     * to 0.97. */
    setup_type_4_slot(0, /*age=*/1000, /*unk_48=*/0.001f,
                      /*pos=*/0.0f, 0.0f, 0.0f);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X] = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y] = pfo_f_to_bits(0.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Z] = pfo_f_to_bits(0.0f);

    scene1_pfo_table_b_tick();

    float bx = pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_X]);
    /* drag2 = 0.97.  bx_post_drag ≈ (1 + small_delta) * 0.97 ≈ 0.97.
     * Then |vel| normalize may fire — but |vel| ≈ 0.97 after drag, not
     * > 1, so no normalize.  Final bx ≈ 0.97. */
    T_ASSERT(fabsf(bx - 0.97f) < 0.02f);
    return 0;
}

int test_pfo_b_tick_type_4_gravity_cancel_when_below_target_y(void)
{
    /* When pos.y < target.y (-10.8), BEND_Y -= UNK_48 (decayed value).
     * Net effect: gravity added UNK_48 (1.0), then type-4 body
     * subtracts decayed UNK_48 (0.8).  Net BEND_Y = 0 (initial) + 1.0
     * - 0.8 + delta_y ≈ 0.2 + tiny.
     *
     * But pos.y < target.y is also a terminal-kill condition!  So the
     * slot dies after this tick.  We can still verify the post-tick
     * BEND_Y state. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/0.0f, /*pos.y=*/-20.0f, 0.0f);
    int base = 0;
    /* Verify state right after the body runs.  Post-tick: ACTIVE=-1
     * (terminal kill), BEND_Y = 0.0 (initial) + 1.0 (gravity) +
     * delta_y (~-0.002) - 0.8 (gravity cancel decayed UNK_48)
     * ≈ 0.198. */
    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_ACTIVE], -1);
    float by = pfo_bits_to_f(g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_BEND_Y]);
    /* Approximately: 1.0 - 0.8 + small_delta = 0.2. */
    T_ASSERT(fabsf(by - 0.2f) < 0.05f);
    return 0;
}

int test_pfo_b_tick_type_4_hook_not_fired_without_install(void)
{
    /* No hook installed → no fire.  Terminal kill still happens
     * (ACTIVE=-1) but the host-observable side-effect counter stays 0. */
    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      /*pos=*/13.2f, -10.8f, -520.0f);
    scene1_pfo_clear_type_4_terminal_kill_hook();
    g_pfo_type_4_kill_count = 0;

    scene1_pfo_table_b_tick();

    T_ASSERT_EQ_I(g_scene1_overlay_slots[0 + SCENE1_OVERLAY_OFF_ACTIVE], -1);
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 0);
    return 0;
}

int test_pfo_b_tick_type_4_factor_always_1_2_quirk_50(void)
{
    /* Engine quirk #50: factor = (AGE-30)*0.4 + 1.2 → clamps at 1.2.
     * AGE>30 → factor ALWAYS = 1.2 in this branch (formula≥1.6 always).
     * Therefore target = (13.2, -10.8, -520) regardless of AGE.
     *
     * Verify: place pos at (13.2, -10.8, -520) for AGE=31 AND AGE=100;
     * both should terminal-kill (distance to target ≈ 0). */
    g_pfo_type_4_kill_count = 0;
    scene1_pfo_set_type_4_terminal_kill_hook(pfo_type_4_kill_recorder);

    setup_type_4_slot(0, /*age=*/31, /*unk_48=*/1.0f,
                      13.2f, -10.8f, -520.0f);
    scene1_pfo_table_b_tick();
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 1);

    setup_type_4_slot(0, /*age=*/100, /*unk_48=*/1.0f,
                      13.2f, -10.8f, -520.0f);
    scene1_pfo_table_b_tick();
    T_ASSERT_EQ_I(g_pfo_type_4_kill_count, 2);

    scene1_pfo_clear_type_4_terminal_kill_hook();
    return 0;
}

/* ===== PFO.5a — Table A per-tick body =============================== */

typedef struct {
    const void *template_owner;
    float pos_x, pos_y, pos_z;
    int   template_id;
    float scale_base;
    int   override_dur;
    int   override_rot_y_bits;
    int   shape_mode;
    int   mode;
} pfo_spawn_record_t;

#define PFO_SPAWN_LOG_CAP 16
static int                 g_pfo_spawn_log_count;
static pfo_spawn_record_t  g_pfo_spawn_log[PFO_SPAWN_LOG_CAP];

static void pfo_spawn_recorder(const void *template_owner,
                               float pos_x, float pos_y, float pos_z,
                               int   template_id,
                               float scale_base,
                               int   override_dur,
                               int   override_rot_y,
                               int   shape_mode,
                               int   mode)
{
    if (g_pfo_spawn_log_count >= PFO_SPAWN_LOG_CAP) return;
    pfo_spawn_record_t *r = &g_pfo_spawn_log[g_pfo_spawn_log_count++];
    r->template_owner    = template_owner;
    r->pos_x             = pos_x;
    r->pos_y             = pos_y;
    r->pos_z             = pos_z;
    r->template_id       = template_id;
    r->scale_base        = scale_base;
    r->override_dur      = override_dur;
    r->override_rot_y_bits = override_rot_y;
    r->shape_mode        = shape_mode;
    r->mode              = mode;
}

static void pfo_spawn_log_reset(void)
{
    g_pfo_spawn_log_count = 0;
    memset(g_pfo_spawn_log, 0, sizeof g_pfo_spawn_log);
}

/* Seed a Table A slot to a "live" baseline with predictable param
 * values; AGE = `age`, MODE = `mode`.  All param dws are set so each
 * test can pull a recognizable value out of the spawn record. */
static void setup_pfo_a_slot(int s, int32_t age, int32_t mode)
{
    int base = s * SCENE1_PFO_TABLE_A_STRIDE;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM0]   = 0;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM1]   = pfo_f_to_bits(1.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM2]   = pfo_f_to_bits(2.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM3]   = pfo_f_to_bits(3.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_SENTINEL] = 0;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM5]   = pfo_f_to_bits(1.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM6]   = 0;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM7]   = 0;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM8]   = 0;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_AGE]      = age;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_MODE]     = mode;
}

/* Seed parent_table[parent_id] sub_rec[k] with explicit fields.  Other
 * sub_recs stay at "default empty" (sentinel=-1) per
 * scene1_pfo_parent_table_init. */
static void seed_parent_sub_rec(int parent_id, int k,
                                int32_t sentinel, int32_t age_match,
                                float scale_mul,
                                float xyz_x, float xyz_y, float xyz_z)
{
    int32_t *entry = &g_scene1_pfo_parent_table[
        parent_id * SCENE1_PFO_PARENT_TABLE_STRIDE];
    entry[SCENE1_PFO_PARENT_OFF_SUB_SENTINEL_0  + k]          = sentinel;
    entry[SCENE1_PFO_PARENT_OFF_SUB_AGE_MATCH_0 + k]          = age_match;
    entry[SCENE1_PFO_PARENT_OFF_SUB_SCALE_MUL_0 + k]          = pfo_f_to_bits(scale_mul);
    entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 0]  = pfo_f_to_bits(xyz_x);
    entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 1]  = pfo_f_to_bits(xyz_y);
    entry[SCENE1_PFO_PARENT_OFF_SUB_XYZ_0       + k * 3 + 2]  = pfo_f_to_bits(xyz_z);
}

int test_pfo_a_tick_skips_empty_slot(void)
{
    /* All slots sentinel=-1 → no spawns, no age changes. */
    scene1_pfo_table_a_init();
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 0);
    /* Sentinel-empty slot 0's AGE stays at its BSS default (0 here). */
    int32_t age0 = g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE];
    T_ASSERT_EQ_I(age0, 0);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_no_spawn_when_parent_table_default(void)
{
    /* Default-fill parent table → all sub_rec[k].sentinel == -1 → gate
     * never opens → no spawns, but age still increments. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 0);
    /* Age incremented from 0 → 1. */
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE], 1);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_spawn_fires_on_age_match(void)
{
    /* sub_rec[0] sentinel=42, age_match=0; slot age=0 → fires. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    seed_parent_sub_rec(/*parent_id=*/0, /*k=*/0,
                        /*sentinel=*/42, /*age_match=*/0,
                        /*scale_mul=*/1.0f,
                        0.0f, 0.0f, 0.0f);
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].template_id, 42);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_spawn_skipped_when_age_match_differs(void)
{
    /* sub_rec[0] age_match=5, slot age=0 → gate closed. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    seed_parent_sub_rec(0, 0, /*sentinel=*/42, /*age_match=*/5,
                        1.0f, 0.0f, 0.0f, 0.0f);
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 0);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_spawn_skipped_when_sub_sentinel_minus_one(void)
{
    /* Engine init leaves sub_sentinel=-1; even if age_match matches the
     * slot age, the sentinel==-1 gate keeps it shut. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    /* Don't seed sub_rec[0] — leave at default (sentinel=-1, age_match=0,
     * which DOES match the slot's age=0, but sentinel guard wins). */
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 0);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_multiple_sub_records_fire_same_tick(void)
{
    /* Three sub_recs match → three spawns this tick. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/3, /*mode=*/0);
    seed_parent_sub_rec(0, 0, 10, 3, 1.0f, 0,0,0);
    seed_parent_sub_rec(0, 1, 20, 3, 1.0f, 0,0,0);
    seed_parent_sub_rec(0, 4, 30, 3, 1.0f, 0,0,0);  /* skip k=2,3 */
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 3);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].template_id, 10);
    T_ASSERT_EQ_I(g_pfo_spawn_log[1].template_id, 20);
    T_ASSERT_EQ_I(g_pfo_spawn_log[2].template_id, 30);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_passthrough_mode_pos_and_args(void)
{
    /* Mode 0 path.  Slot pos=(1,2,3), sub.xyz=(10,20,30) → passthrough
     * pos = sub + slot = (11, 22, 33).
     * Slot PARAM5=5.0 (scale), sub.scale_mul=2.0 → scale_base=10.0.
     * Slot PARAM0=0x12345 → template_owner cast.
     * Slot PARAM6=7 → override_dur=7.
     * Slot PARAM7=0xCAFEBABE → override_rot_y bits passed through.
     * Mode flag=0 → shape_mode arg=0, mode arg=0. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    int base = 0 * SCENE1_PFO_TABLE_A_STRIDE;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM0] = 0x12345;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM1] = pfo_f_to_bits(1.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM2] = pfo_f_to_bits(2.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM3] = pfo_f_to_bits(3.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM5] = pfo_f_to_bits(5.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM6] = 7;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM7] = (int32_t)0xCAFEBABE;

    seed_parent_sub_rec(0, 0, /*sentinel=*/99, /*age_match=*/0,
                        /*scale_mul=*/2.0f,
                        /*xyz=*/10.0f, 20.0f, 30.0f);
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    g_scene1_pfo_alt_mode = 0;

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    pfo_spawn_record_t *r = &g_pfo_spawn_log[0];
    T_ASSERT(fabsf(r->pos_x - 11.0f) < 1e-5f);
    T_ASSERT(fabsf(r->pos_y - 22.0f) < 1e-5f);
    T_ASSERT(fabsf(r->pos_z - 33.0f) < 1e-5f);
    T_ASSERT(fabsf(r->scale_base - 10.0f) < 1e-5f);
    T_ASSERT_EQ_I(r->template_id, 99);
    T_ASSERT_EQ_I(r->override_dur, 7);
    T_ASSERT_EQ_I(r->override_rot_y_bits, (int32_t)0xCAFEBABE);
    T_ASSERT_EQ_I(r->shape_mode, 0);
    T_ASSERT_EQ_I(r->mode, 0);
    T_ASSERT_EQ_U((uintptr_t)r->template_owner, (uintptr_t)0x12345u);

    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_projected_mode_pos_and_args(void)
{
    /* Mode != 0 path.  slot pos=(1,2,3), sub.xyz=(10,20,30):
     *   pos_x = 16.5 - (1+10)/19.5 = 16.5 - 11/19.5
     *   pos_y = 12.4 - (2+20)/19.5 = 12.4 - 22/19.5
     *   pos_z = -520
     * template_owner = NULL; override_rot_y = 0.0f bits;
     * shape_mode = slot[10] (= 1 here); mode = 1. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/1);
    int base = 0 * SCENE1_PFO_TABLE_A_STRIDE;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM0] = 0xDEADBEEF;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM1] = pfo_f_to_bits(1.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM2] = pfo_f_to_bits(2.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM3] = pfo_f_to_bits(3.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM5] = pfo_f_to_bits(4.0f);
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM6] = 11;
    g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM7] = (int32_t)0x55555555;

    seed_parent_sub_rec(0, 0, /*sentinel=*/77, /*age_match=*/0,
                        /*scale_mul=*/0.5f,
                        10.0f, 20.0f, 30.0f);
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    g_scene1_pfo_alt_mode = 0;

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    pfo_spawn_record_t *r = &g_pfo_spawn_log[0];
    T_ASSERT(fabsf(r->pos_x - (16.5f - 11.0f / 19.5f)) < 1e-5f);
    T_ASSERT(fabsf(r->pos_y - (12.4f - 22.0f / 19.5f)) < 1e-5f);
    T_ASSERT(fabsf(r->pos_z - (-520.0f)) < 1e-5f);
    T_ASSERT(fabsf(r->scale_base - (4.0f * 0.5f)) < 1e-5f);
    T_ASSERT_EQ_I(r->template_id, 77);
    T_ASSERT_EQ_I(r->override_dur, 11);
    T_ASSERT_EQ_I(r->override_rot_y_bits, pfo_f_to_bits(0.0f));
    T_ASSERT_EQ_I(r->shape_mode, 1);
    T_ASSERT_EQ_I(r->mode, 1);
    /* template_owner is forced to NULL in projected mode regardless of
     * slot PARAM0. */
    T_ASSERT_EQ_U((uintptr_t)r->template_owner, (uintptr_t)0u);

    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_passthrough_alt_mode_adds_minus_520_to_z(void)
{
    /* PHC #17: when g_scene1_pfo_alt_mode != 0, passthrough adds -520
     * to pos_z (= sub.z + slot[3] + alt_offset). */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    seed_parent_sub_rec(0, 0, /*sentinel=*/1, /*age_match=*/0,
                        1.0f, 0.0f, 0.0f, 10.0f);
    /* slot PARAM3 = 3.0 by setup_pfo_a_slot default. */

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    g_scene1_pfo_alt_mode = 1;

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    /* z = sub.z + slot.PARAM3 + alt_offset = 10 + 3 - 520 = -507. */
    T_ASSERT(fabsf(g_pfo_spawn_log[0].pos_z - (-507.0f)) < 1e-5f);

    g_scene1_pfo_alt_mode = 0;
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_projected_mode_ignores_alt_mode(void)
{
    /* Projected always uses pos_z=-520 regardless of g_scene1_pfo_alt_mode. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/1);
    seed_parent_sub_rec(0, 0, 1, 0, 1.0f, 0,0, 99.0f);

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    g_scene1_pfo_alt_mode = 1;

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    T_ASSERT(fabsf(g_pfo_spawn_log[0].pos_z - (-520.0f)) < 1e-5f);

    g_scene1_pfo_alt_mode = 0;
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_age_increments_when_live(void)
{
    /* Even when no sub_rec fires, age increments. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/5, /*mode=*/0);
    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE], 6);

    scene1_pfo_table_a_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE], 7);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_self_clears_at_age_300(void)
{
    /* Engine: if (++age == 300) sentinel = -1.  age=299 → tick →
     * age=300 + sentinel=-1. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/299, /*mode=*/0);
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    pfo_spawn_log_reset();

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE], 300);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], -1);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_does_not_self_clear_below_or_above_300(void)
{
    /* age=298 → 299 (no clear); age=300 → 301 (no clear; gate is == only). */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();

    /* Case 1: age 298 → 299, no clear. */
    setup_pfo_a_slot(0, 298, /*mode=*/0);
    scene1_pfo_table_a_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE], 299);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], 0);

    /* Case 2: age 300 → 301, no clear (already past the == 300 gate). */
    setup_pfo_a_slot(0, 300, /*mode=*/0);
    scene1_pfo_table_a_tick();
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_AGE], 301);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL], 0);
    return 0;
}

int test_pfo_a_tick_uses_parent_id_from_sentinel(void)
{
    /* The sentinel field IS the parent template id (a non-negative int).
     * Verify the tick reads parent_table[sentinel], not parent_table[0]. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    /* Re-point slot 0's parent id to parent_table[7] instead of [0]. */
    g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_SENTINEL] = 7;
    /* Seed parent 0's sub_rec[0] to FIRE — but it should be ignored. */
    seed_parent_sub_rec(0, 0, 100, 0, 1.0f, 0,0,0);
    /* Seed parent 7's sub_rec[0] to fire — that's what should win. */
    seed_parent_sub_rec(7, 0, 200, 0, 1.0f, 0,0,0);

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].template_id, 200);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_walks_all_live_slots(void)
{
    /* Two live slots both fire: slot 0 (parent 0) and slot 5 (parent 1). */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    setup_pfo_a_slot(5, /*age=*/0, /*mode=*/0);
    g_scene1_pfo_table_a[5 * SCENE1_PFO_TABLE_A_STRIDE +
                         SCENE1_PFO_TABLE_A_OFF_SENTINEL] = 1;
    seed_parent_sub_rec(0, 0, 11, 0, 1.0f, 0,0,0);
    seed_parent_sub_rec(1, 0, 22, 0, 1.0f, 0,0,0);

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 2);
    /* Order: slot 0 first (template_id 11), then slot 5 (template_id 22). */
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].template_id, 11);
    T_ASSERT_EQ_I(g_pfo_spawn_log[1].template_id, 22);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_spawn_hook_intercepts_real_overlay_spawn(void)
{
    /* When the hook is installed, scene1_overlay_spawn must NOT be
     * called.  Verify by checking that no overlay slot got claimed. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    seed_parent_sub_rec(0, 0, /*sentinel=*/0, /*age_match=*/0,
                        1.0f, 0,0,0);
    /* Wipe overlay so a real spawn would claim slot 0. */
    scene1_overlay_reset();

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);

    scene1_pfo_table_a_tick();

    /* Hook fired once. */
    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    /* No overlay slot got claimed — first slot's ACTIVE still -1. */
    int32_t s_active = g_scene1_overlay_slots[
        0 * SCENE1_OVERLAY_SLOT_STRIDE + SCENE1_OVERLAY_OFF_ACTIVE];
    T_ASSERT_EQ_I(s_active, -1);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_a_tick_default_calls_real_overlay_spawn(void)
{
    /* No hook installed → real scene1_overlay_spawn fires → claims slot
     * 0 of the overlay table (since overlay_reset leaves all -1).
     * Template 0 with all-default fields is enough to allocate. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    setup_pfo_a_slot(0, /*age=*/0, /*mode=*/0);
    seed_parent_sub_rec(0, 0, /*sentinel=*/0, /*age_match=*/0,
                        1.0f, 0,0,0);
    scene1_overlay_reset();
    scene1_pfo_clear_spawn_hook();

    scene1_pfo_table_a_tick();

    /* Overlay slot 0 must have been claimed (ACTIVE != -1). */
    int32_t s_active = g_scene1_overlay_slots[
        0 * SCENE1_OVERLAY_SLOT_STRIDE + SCENE1_OVERLAY_OFF_ACTIVE];
    if (s_active == -1) {
        T_FAIL("real scene1_overlay_spawn not called: overlay slot 0 still -1");
    }
    return 0;
}

/* ===== PFO.6 — Table A allocators =================================== */

int test_pfo_alloc_projected_claims_first_empty_slot(void)
{
    /* Sentinel-empty table → allocator claims slot 0. */
    scene1_pfo_table_a_init();
    int s = scene1_pfo_table_a_alloc_projected(
        /*pos_x=*/1.0f, /*pos_y=*/2.0f,
        /*template_id=*/42,
        /*scale_base=*/0.5f,
        /*override_dur=*/7,
        /*param_8=*/0xCAFE);
    T_ASSERT_EQ_I(s, 0);

    int base = 0 * SCENE1_PFO_TABLE_A_STRIDE;
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM0],   0);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM1]) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM2]) - 2.0f) < 1e-6f);
    /* PARAM3 = -520.0f via the .rdata 0xc4020000 bit-pattern. */
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM3], (int32_t)0xc4020000);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_SENTINEL], 42);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM5]) - 0.5f) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM6], 7);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM7], 0);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM8], 0xCAFE);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_AGE], 0);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_MODE], 1);
    return 0;
}

int test_pfo_alloc_passthrough_claims_first_empty_slot(void)
{
    scene1_pfo_table_a_init();
    int s = scene1_pfo_table_a_alloc_passthrough(
        /*template_owner=*/0xABCD,
        /*pos_x=*/1.0f, /*pos_y=*/2.0f, /*pos_z=*/3.0f,
        /*template_id=*/77,
        /*scale_base=*/4.0f,
        /*override_dur=*/9,
        /*override_rot_y_bits=*/(int32_t)0xDEADBEEF,
        /*param_8=*/0xF00D);
    T_ASSERT_EQ_I(s, 0);

    int base = 0 * SCENE1_PFO_TABLE_A_STRIDE;
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM0],   0xABCD);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM1]) - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM2]) - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM3]) - 3.0f) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_SENTINEL], 77);
    T_ASSERT(fabsf(pfo_bits_to_f(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM5]) - 4.0f) < 1e-6f);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM6], 9);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM7], (int32_t)0xDEADBEEF);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_PARAM8], 0xF00D);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_AGE], 0);
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[base + SCENE1_PFO_TABLE_A_OFF_MODE], 0);
    return 0;
}

int test_pfo_alloc_skips_occupied_slots(void)
{
    /* Slot 0 occupied → allocator claims slot 1. */
    scene1_pfo_table_a_init();
    g_scene1_pfo_table_a[0 * SCENE1_PFO_TABLE_A_STRIDE +
                         SCENE1_PFO_TABLE_A_OFF_SENTINEL] = 5; /* alive */

    int s = scene1_pfo_table_a_alloc_passthrough(0, 0,0,0, 1, 1.0f, 0, 0, 0);
    T_ASSERT_EQ_I(s, 1);

    /* Slot 0 untouched. */
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[0 * SCENE1_PFO_TABLE_A_STRIDE +
                                       SCENE1_PFO_TABLE_A_OFF_SENTINEL], 5);
    return 0;
}

int test_pfo_alloc_returns_minus_one_when_full(void)
{
    /* All 256 slots alive → alloc returns -1 and writes nothing. */
    for (int i = 0; i < SCENE1_PFO_TABLE_A_COUNT; i++) {
        g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE +
                             SCENE1_PFO_TABLE_A_OFF_SENTINEL] = 99;
        /* Seed PARAM0 to a recognizable value so we can prove no
         * write happened. */
        g_scene1_pfo_table_a[i * SCENE1_PFO_TABLE_A_STRIDE +
                             SCENE1_PFO_TABLE_A_OFF_PARAM0] = 0xDEAD0000 + i;
    }

    int s1 = scene1_pfo_table_a_alloc_projected(0, 0, 1, 1.0f, 0, 0);
    int s2 = scene1_pfo_table_a_alloc_passthrough(0, 0, 0, 0, 1, 1.0f, 0, 0, 0);
    T_ASSERT_EQ_I(s1, -1);
    T_ASSERT_EQ_I(s2, -1);

    /* Slot 0's PARAM0 unchanged. */
    T_ASSERT_EQ_I(g_scene1_pfo_table_a[0 * SCENE1_PFO_TABLE_A_STRIDE +
                                       SCENE1_PFO_TABLE_A_OFF_PARAM0],
                  (int32_t)0xDEAD0000);
    return 0;
}

int test_pfo_alloc_projected_then_tick_fires_projected_spawn(void)
{
    /* End-to-end: alloc-projected then tick should produce a projected-
     * mode spawn call.  Set up parent_table[42] sub_rec[0] to match. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    seed_parent_sub_rec(/*parent_id=*/42, /*k=*/0,
                        /*sentinel=*/123, /*age_match=*/0,
                        /*scale_mul=*/1.0f, 0,0,0);

    int s = scene1_pfo_table_a_alloc_projected(
        /*pos_x=*/0.0f, /*pos_y=*/0.0f,
        /*template_id=*/42,
        /*scale_base=*/1.0f,
        /*override_dur=*/0,
        /*param_8=*/0);
    T_ASSERT_EQ_I(s, 0);

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    g_scene1_pfo_alt_mode = 0;

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    /* Projected mode → mode arg = 1, shape_mode = 1, pos_z = -520. */
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].mode, 1);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].shape_mode, 1);
    T_ASSERT(fabsf(g_pfo_spawn_log[0].pos_z - (-520.0f)) < 1e-5f);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].template_id, 123);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_alloc_passthrough_then_tick_fires_passthrough_spawn(void)
{
    /* End-to-end the other way. */
    scene1_pfo_table_a_init();
    scene1_pfo_parent_table_init();
    seed_parent_sub_rec(/*parent_id=*/7, /*k=*/0,
                        /*sentinel=*/200, /*age_match=*/0,
                        1.0f, 0,0,0);

    int s = scene1_pfo_table_a_alloc_passthrough(
        /*template_owner=*/0,  /* keep 0 so template_owner reads NULL */
        /*pos_x=*/1.0f, /*pos_y=*/2.0f, /*pos_z=*/3.0f,
        /*template_id=*/7,
        /*scale_base=*/1.0f,
        /*override_dur=*/0,
        /*override_rot_y_bits=*/0,
        /*param_8=*/0);
    T_ASSERT_EQ_I(s, 0);

    pfo_spawn_log_reset();
    scene1_pfo_set_spawn_hook(pfo_spawn_recorder);
    g_scene1_pfo_alt_mode = 0;

    scene1_pfo_table_a_tick();

    T_ASSERT_EQ_I(g_pfo_spawn_log_count, 1);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].mode, 0);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].shape_mode, 0);
    /* Passthrough: pos = slot_pos + sub_rec.xyz (=0,0,0 here). */
    T_ASSERT(fabsf(g_pfo_spawn_log[0].pos_x - 1.0f) < 1e-6f);
    T_ASSERT(fabsf(g_pfo_spawn_log[0].pos_y - 2.0f) < 1e-6f);
    T_ASSERT(fabsf(g_pfo_spawn_log[0].pos_z - 3.0f) < 1e-6f);
    T_ASSERT_EQ_I(g_pfo_spawn_log[0].template_id, 200);
    scene1_pfo_clear_spawn_hook();
    return 0;
}

int test_pfo_alloc_projected_param3_minus_520_bit_pattern(void)
{
    /* The .rdata constant 0xc4020000 maps to IEEE-754 binary32 -520.0f.
     * Verify the bit-pattern survives the int32 store. */
    scene1_pfo_table_a_init();
    scene1_pfo_table_a_alloc_projected(0, 0, 1, 1.0f, 0, 0);
    int32_t p3 = g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_OFF_PARAM3];
    T_ASSERT_EQ_I(p3, (int32_t)0xc4020000);
    T_ASSERT(fabsf(pfo_bits_to_f(p3) - (-520.0f)) < 1e-5f);
    return 0;
}

int test_pfo_alloc_repeated_calls_fill_table_in_order(void)
{
    /* Five back-to-back allocations claim slots 0..4. */
    scene1_pfo_table_a_init();
    for (int i = 0; i < 5; i++) {
        int s = scene1_pfo_table_a_alloc_projected(0, 0, /*template_id=*/i + 1,
                                                   1.0f, 0, 0);
        T_ASSERT_EQ_I(s, i);
    }
    /* Verify each got its own template_id. */
    for (int i = 0; i < 5; i++) {
        int32_t sentinel = g_scene1_pfo_table_a[
            i * SCENE1_PFO_TABLE_A_STRIDE + SCENE1_PFO_TABLE_A_OFF_SENTINEL];
        T_ASSERT_EQ_I(sentinel, i + 1);
    }
    return 0;
}
