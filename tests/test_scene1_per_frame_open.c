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

int test_pfo_b_tick_type_4_with_unk_48_bypasses_kill(void)
{
    /* The shop-walker body is skipped in PFO.3, but the kill check's
     * `!(shape_mode==4 && UNK_48!=0)` gate is preserved.  A slot in this
     * state should never age-kill, even when FADE_OUT_OFFSET <= age. */
    setup_live_slot(0, /*shape_mode=*/4, /*type_shape=*/0);
    int base = 0;
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_UNK_48]          = pfo_f_to_bits(1.0f);
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 1;     /* would normally kill */
    g_scene1_overlay_slots[base + SCENE1_OVERLAY_OFF_AGE]             = 100;

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
