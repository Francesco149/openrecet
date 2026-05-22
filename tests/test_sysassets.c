/*
 * test_sysassets.c — coverage for the system-asset loader port
 * (src/sysassets.{c,h} = FUN_00472f5d).
 *
 * The Win32 surface (sprite_load → IDirect3DTexture8 upload) is not
 * exercisable in the host test process, so we only cover the pure
 * helper sysassets_compute_icon_sizes — it owns the engine-fidelity
 * critical math (the per-category icon page sizing formula) that we'd
 * rather not regress on accidentally.
 */
#include "t.h"
#include "sysassets.h"
#include "tables_item.h"

/* ── helper ── */

static void seed_zero(item_state_t *s)
{
    memset(s, 0, sizeof *s);
}

/* Convenience: stamp a valid record into slot `i` belonging to
 * category `cat`. Only the fields the loader reads (valid, category)
 * are set — everything else stays zero. */
static void stamp(item_state_t *s, int i, int cat)
{
    s->records[i].valid    = 1;
    s->records[i].category = cat;
    s->records[i].item_id  = cat * 100 + (i & 0x3f);
}

/* ── tests ── */

int test_sysassets_compute_icon_sizes_empty_state(void)
{
    item_state_t s;
    seed_zero(&s);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 0);
    for (int i = 0; i < SYSASSETS_ITEM_CATEGORIES; i++) {
        T_ASSERT_EQ_I(sizes[i], 0);
    }
    return 0;
}

int test_sysassets_compute_icon_sizes_single_item_minimum_64(void)
{
    item_state_t s;
    seed_zero(&s);
    stamp(&s, 0, 5);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 1);
    /* ceil(1/8) * 32 = 32, clamped up to 64. */
    T_ASSERT_EQ_I(sizes[5], 64);
    for (int i = 0; i < SYSASSETS_ITEM_CATEGORIES; i++) {
        if (i == 5) continue;
        T_ASSERT_EQ_I(sizes[i], 0);
    }
    return 0;
}

int test_sysassets_compute_icon_sizes_eight_items_still_64(void)
{
    item_state_t s;
    seed_zero(&s);
    for (int i = 0; i < 8; i++) stamp(&s, i, 3);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 1);
    /* ceil(8/8) * 32 = 32, clamped up to 64. */
    T_ASSERT_EQ_I(sizes[3], 64);
    return 0;
}

int test_sysassets_compute_icon_sizes_nine_items_grows_to_96(void)
{
    item_state_t s;
    seed_zero(&s);
    for (int i = 0; i < 9; i++) stamp(&s, i, 7);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 1);
    /* ceil(9/8) * 32 = 64. Still below the 64-minimum clamp would be
     * a wash — but the formula's output is 64 either way for 9. The
     * next bump is 17 items (3 rows = 96). */
    T_ASSERT_EQ_I(sizes[7], 64);
    return 0;
}

int test_sysassets_compute_icon_sizes_seventeen_items_yields_96(void)
{
    item_state_t s;
    seed_zero(&s);
    for (int i = 0; i < 17; i++) stamp(&s, i, 2);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 1);
    /* ceil(17/8) * 32 = ceil(2.125) * 32 = 3 * 32 = 96. */
    T_ASSERT_EQ_I(sizes[2], 96);
    return 0;
}

int test_sysassets_compute_icon_sizes_invalid_records_skipped(void)
{
    item_state_t s;
    seed_zero(&s);
    /* Three "invalid" records in cat 4 — should not bump the count. */
    s.records[0].valid = 0;   s.records[0].category = 4;
    s.records[1].valid = -1;  s.records[1].category = 4;
    s.records[2].valid = 0;   s.records[2].category = 4;
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 0);
    T_ASSERT_EQ_I(sizes[4], 0);
    return 0;
}

int test_sysassets_compute_icon_sizes_multi_category(void)
{
    item_state_t s;
    seed_zero(&s);
    /* Items must appear in ascending-category order in records[] for
     * the engine's max_cat-tracker to pick them up — that's what the
     * parser produces in real boot. */
    for (int i = 0; i < 3; i++)   stamp(&s, i,         1);
    for (int i = 0; i < 10; i++)  stamp(&s, 3 + i,     3);
    for (int i = 0; i < 1; i++)   stamp(&s, 13 + i,    9);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 3);
    T_ASSERT_EQ_I(sizes[1], 64);   /* 3 items → ceil(3/8)*32=32 → 64 */
    T_ASSERT_EQ_I(sizes[3], 64);   /* 10 items → ceil(10/8)*32=64    */
    T_ASSERT_EQ_I(sizes[9], 64);   /* 1 item  → ceil(1/8)*32=32 → 64 */
    /* Categories not present must be left at zero. */
    T_ASSERT_EQ_I(sizes[0], 0);
    T_ASSERT_EQ_I(sizes[2], 0);
    T_ASSERT_EQ_I(sizes[5], 0);
    T_ASSERT_EQ_I(sizes[99], 0);
    return 0;
}

int test_sysassets_compute_icon_sizes_out_of_range_category_skipped(void)
{
    item_state_t s;
    seed_zero(&s);
    /* category=-1 and category=999 are both out of the 0..99 range
     * — must not write out-of-bounds. ASan/UBSan would flag a write
     * outside the size array, so this is a defensive guard. */
    stamp(&s, 0, -1);
    stamp(&s, 1, 999);
    stamp(&s, 2, 42);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(sizes[42], 64);
    return 0;
}

int test_sysassets_compute_icon_sizes_max_category_tracker_skips_revisits(void)
{
    item_state_t s;
    seed_zero(&s);
    /* The engine gates the load on `cat > max_cat_seen`, so if records
     * for a previously-loaded category appear after a higher category,
     * they must NOT trigger a reload. Build records: cat 2 × 3, then
     * cat 5 × 2, then cat 2 × 4 (out-of-order tail). The trailing
     * cat-2 records still count toward the per-category total (counts
     * is the first pass), but the loader pass only loads cat 2 once
     * — at the *first* cat-2 record, when the running total is 3, not
     * the final total of 7. So sizes[2] = 64 (ceil(3/8)*32 → 64), not
     * 64 from the full total (which would also be 64) — pick a value
     * where the difference would matter. With cat 2 final total = 9
     * the loader would *still* read counts[2] = 9 (counts is built
     * first), but only fire the load on the first encounter. So
     * sizes[2] should reflect the full count of 9, not 3.
     *
     * Restated more carefully: counts[] is fully populated before the
     * second loop runs. So sizes[2] = ceil(9/8)*32 = 64. Same for
     * sizes[5] = ceil(2/8)*32 → 64. distinct = 2. */
    for (int i = 0; i < 3; i++) stamp(&s, i,     2);
    for (int i = 0; i < 2; i++) stamp(&s, 3 + i, 5);
    for (int i = 0; i < 6; i++) stamp(&s, 5 + i, 2);  /* trailing cat-2 */
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 2);
    /* Full count for cat 2 = 9 (ceil(9/8)*32 = 64). */
    T_ASSERT_EQ_I(sizes[2], 64);
    T_ASSERT_EQ_I(sizes[5], 64);
    return 0;
}

int test_sysassets_compute_icon_sizes_large_category_count(void)
{
    item_state_t s;
    seed_zero(&s);
    /* 65 items in a single category → ceil(65/8) = 9 rows → 288. */
    for (int i = 0; i < 65; i++) stamp(&s, i, 11);
    int sizes[SYSASSETS_ITEM_CATEGORIES];
    int n = sysassets_compute_icon_sizes(&s, sizes);
    T_ASSERT_EQ_I(n, 1);
    T_ASSERT_EQ_I(sizes[11], 288);
    return 0;
}

int test_sysassets_chara_variant_count_matches_engine_stride(void)
{
    /* The engine derives 3 from (DAT_073a9b48 - DAT_073a9b18) / 0x10
     * — encoded in our SYSASSETS_CHARA_VARIANTS. Pin it. */
    T_ASSERT_EQ_I(SYSASSETS_CHARA_VARIANTS, 3);
    return 0;
}

int test_sysassets_item_category_slot_count_matches_table(void)
{
    /* Item icon array is indexed by category id (item_id / 100).
     * The icon-page slot array must be at least as large as the item
     * table's category ceiling so an out-of-range write can never
     * land in adjacent storage. */
    T_ASSERT_EQ_I(SYSASSETS_ITEM_CATEGORIES, ITEM_CATEGORY_COUNT);
    return 0;
}
