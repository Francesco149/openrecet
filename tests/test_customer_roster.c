/*
 * test_customer_roster.c — the roster-scan helpers (customer_roster.c).
 *
 * These are the pure/RNG-only building blocks of the customer-eligibility
 * scan (FUN_0045edaa).  Every helper was transcribed against the objdump
 * (the Ghidra decompile dropped e55c's tier multiplier + folded a68f's
 * band classifier out of the function — gotcha #1), so the tests pin the
 * exact behaviour: the band thresholds, the front-row ×3 / tier scaling of
 * the weight, and the shuffle's RNG-consumption count (load-bearing for
 * parity).  Data-dependent absolute weights are cross-checked live against
 * retail (call_function); here we verify the algorithm on synthetic data.
 */
#include "t.h"
#include "../src/customer_roster.h"
#include "../src/tables_item.h"
#include "../src/tables_kyaku.h"
#include "../src/save_bank.h"
#include "../src/scene1_shop_display.h"
#include "../src/rng.h"

#include <stdlib.h>
#include <string.h>

/* ── synthetic-bank helpers ───────────────────────────────────────────── */

/* Allocate a zeroed working-slot arena with an all-empty (-1) display grid. */
static int32_t *make_bank(void)
{
    int32_t *b = calloc(SAVE_BANK_STRIDE_DWORDS, sizeof(int32_t));
    for (int i = 0; i < SAVE_BANK_DISPLAY_GRID_CELLS; i++)
        b[SAVE_BANK_FIELD_DISPLAY_GRID + i] = -1;
    return b;
}

static void grid_put(int32_t *b, int row, int col, int32_t item_id)
{
    b[SAVE_BANK_FIELD_DISPLAY_GRID + row * SAVE_BANK_DISPLAY_GRID_COLS + col] =
        item_id << 6;   /* engine display cell encodes id<<6 */
}

/* Populate g_item with a handful of records (item_id / category / attr). */
static void set_item(int slot, int32_t id, int32_t category, uint32_t attr)
{
    g_item.records[slot].item_id   = id;
    g_item.records[slot].category  = category;
    g_item.records[slot].attr_mask = attr;
    if (slot + 1 > g_item.count)
        g_item.count = slot + 1;
}

/* ── FUN_0045e505 — roster_shuffle ────────────────────────────────────── */

int test_roster_shuffle_draw_count_and_permutation(void)
{
    /* n passes ⇒ exactly n LCG draws (the FIXED-n index quirk). */
    int32_t a[6] = { 0, 1, 2, 3, 4, 5 };
    rng_seed(19937);
    unsigned long before = rng_call_count();
    roster_shuffle(a, 6);
    T_ASSERT_EQ_U(rng_call_count() - before, 6);

    /* Result is a permutation of the input (every value 0..5 present once). */
    int seen[6] = { 0 };
    for (int i = 0; i < 6; i++) {
        T_ASSERT(a[i] >= 0 && a[i] < 6);
        seen[a[i]]++;
    }
    for (int i = 0; i < 6; i++)
        T_ASSERT_EQ_I(seen[i], 1);
    return 0;
}

int test_roster_shuffle_matches_hand_trace(void)
{
    /* n=3, seeded: reproduce the swaps by stepping the LCG by hand.
     * pass p swaps arr[p] with arr[rng15()%3]. */
    int32_t a[3] = { 10, 20, 30 };
    rng_seed(1);
    /* mirror rng_next15 to predict the three indices */
    uint32_t s = 1;
    int idx[3];
    for (int p = 0; p < 3; p++) {
        s = s * 0x343fdu + 0x269ec3u;
        idx[p] = (int)(((s >> 16) & 0x7fffu) % 3u);
    }
    int32_t expect[3] = { 10, 20, 30 };
    for (int p = 0; p < 3; p++) {
        int32_t t = expect[idx[p]];
        expect[idx[p]] = expect[p];
        expect[p] = t;
    }
    rng_seed(1);
    roster_shuffle(a, 3);
    T_ASSERT_EQ_I(a[0], expect[0]);
    T_ASSERT_EQ_I(a[1], expect[1]);
    T_ASSERT_EQ_I(a[2], expect[2]);
    return 0;
}

/* ── FUN_0040a68f — roster_dist_band ──────────────────────────────────── */

int test_roster_dist_band_thresholds(void)
{
    /* Centroid at origin; band by sqrt distance to (px,py).
     * thresholds: <1.153846→4, <3.461999→3, <9.230769→2, <12.692307→1,
     *             <=15.0→0, else −1. */
    roster_set_centroid(0, 0);
    T_ASSERT_EQ_I(roster_dist_band(0, 0), 4);    /* dist 0        */
    T_ASSERT_EQ_I(roster_dist_band(1, 0), 4);    /* dist 1.0  <1.15 */
    T_ASSERT_EQ_I(roster_dist_band(2, 0), 3);    /* dist 2.0        */
    T_ASSERT_EQ_I(roster_dist_band(3, 0), 3);    /* dist 3.0  <3.46 */
    T_ASSERT_EQ_I(roster_dist_band(5, 0), 2);    /* dist 5.0        */
    T_ASSERT_EQ_I(roster_dist_band(9, 0), 2);    /* dist 9.0  <9.23 */
    T_ASSERT_EQ_I(roster_dist_band(10, 0), 1);   /* dist 10.0       */
    T_ASSERT_EQ_I(roster_dist_band(12, 0), 1);   /* dist 12.0 <12.69*/
    T_ASSERT_EQ_I(roster_dist_band(13, 0), 0);   /* dist 13.0       */
    T_ASSERT_EQ_I(roster_dist_band(15, 0), 0);   /* dist 15.0 ==15  */
    T_ASSERT_EQ_I(roster_dist_band(16, 0), -1);  /* dist 16.0 >15   */
    return 0;
}

int test_roster_dist_band_diagonal(void)
{
    /* 3-4-5 triangle → dist exactly 5.0 → band 2; sqrt(2)=1.414 → band 3. */
    roster_set_centroid(0, 0);
    T_ASSERT_EQ_I(roster_dist_band(3, 4), 2);
    T_ASSERT_EQ_I(roster_dist_band(1, 1), 3);
    /* non-zero centroid: centroid(5,5), point(5,5) → dist 0 → band 4. */
    roster_set_centroid(5, 5);
    T_ASSERT_EQ_I(roster_dist_band(5, 5), 4);    /* dist 0 */
    T_ASSERT_EQ_I(roster_dist_band(5, 0), 2);    /* dy=5 → dist 5.0 → band 2 */
    return 0;
}

/* ── FUN_0045e55c — roster_customer_weight ────────────────────────────── */

int test_roster_customer_weight_scoring_and_front_bonus(void)
{
    memset(&g_item, 0, sizeof g_item);
    /* slot0: id 100, category 5, attr 0x1; slot1: id 200, category 9, attr 0x2. */
    set_item(0, 100, 5, 0x1);
    set_item(1, 200, 9, 0x2);

    kyaku_record_t kr;
    memset(&kr, 0, sizeof kr);
    kr.like_attr_mask = 0x1;        /* likes attr-0x1 items → +2 each */
    kr.like_count = 1;
    kr.like_kinds[0] = 9;           /* likes category 9 → +4 each */

    int32_t *b = make_bank();
    b[SHOP_DISPLAY_TIER_SELECTOR] = 0;          /* tier 0: no scaling */
    grid_put(b, 2, 5, 100);   /* not front row: attr match → +2 */
    grid_put(b, 0, 2, 200);   /* row0 col2 (front): cat match +4, ×3 = 12 */

    /* total = 2 + 12 = 14. */
    T_ASSERT_EQ_I(roster_customer_weight((const uint8_t *)b, &kr), 14);
    free(b);
    return 0;
}

int test_roster_customer_weight_tier_multiplier(void)
{
    memset(&g_item, 0, sizeof g_item);
    set_item(0, 100, 5, 0x1);       /* attr-0x1, category 5 */

    kyaku_record_t kr;
    memset(&kr, 0, sizeof kr);
    kr.like_attr_mask = 0x1;        /* +2 per matching item, no front bonus */
    kr.like_count = 0;

    int32_t *b = make_bank();
    /* 5 attr-matching items in non-front cells → base total 10. */
    grid_put(b, 3, 5, 100);
    grid_put(b, 4, 5, 100);
    grid_put(b, 5, 5, 100);
    grid_put(b, 6, 5, 100);
    grid_put(b, 7, 5, 100);

    b[SHOP_DISPLAY_TIER_SELECTOR] = 0;
    T_ASSERT_EQ_I(roster_customer_weight((const uint8_t *)b, &kr), 10);   /* tier0: 10 */

    /* tier1 ×6/7: ftol(10·0.857142866)=8, +3 = 11. */
    b[SHOP_DISPLAY_TIER_SELECTOR] = 1;
    T_ASSERT_EQ_I(roster_customer_weight((const uint8_t *)b, &kr), 11);
    /* tier2 ×2/3: ftol(10·0.666666687)=6, +6 = 12. */
    b[SHOP_DISPLAY_TIER_SELECTOR] = 2;
    T_ASSERT_EQ_I(roster_customer_weight((const uint8_t *)b, &kr), 12);
    /* tier3 ×3/7: ftol(10·0.428571433)=4, +10 = 14. */
    b[SHOP_DISPLAY_TIER_SELECTOR] = 3;
    T_ASSERT_EQ_I(roster_customer_weight((const uint8_t *)b, &kr), 14);

    free(b);
    return 0;
}

/* ── FUN_0048439a — roster_compute_centroid ───────────────────────────── */

int test_roster_centroid_decoration_seed(void)
{
    memset(&g_item, 0, sizeof g_item);
    int32_t *b = make_bank();
    /* all decoration selectors 0 → seed = floor[0]+wall[0]+carpet[0]+table[0].
     * floor[0]=(-4,0) wall[0]=(0,0) carpet[0]=(-2,0) table[0]=(2,0)
     * ⇒ X = -4+0-2+2 = -4 ; Y = 0. */
    b[SAVE_BANK_FIELD_DECO_FLOOR]  = 0;
    b[SAVE_BANK_FIELD_DECO_WALL]   = 0;
    b[SAVE_BANK_FIELD_DECO_CARPET] = 0;
    b[SAVE_BANK_FIELD_DECO_TABLE]  = 0;
    roster_compute_centroid((const uint8_t *)b);
    T_ASSERT_EQ_I(roster_centroid_x(), -4);
    T_ASSERT_EQ_I(roster_centroid_y(), 0);
    free(b);
    return 0;
}

int test_roster_centroid_item_nudge_and_clamp(void)
{
    memset(&g_item, 0, sizeof g_item);
    /* id 100 (< 3000 ⇒ step 1), attr 0x200 (X += step). */
    set_item(0, 100, 0, 0x200);
    /* id 3050 (3000..0xc1b=3099 ⇒ step 3), attr 0x4000 (Y += step). */
    set_item(1, 3050, 0, 0x4000);

    int32_t *b = make_bank();
    /* seed X=-4 Y=0 (all deco 0). */
    grid_put(b, 5, 5, 100);    /* X += 1  → -3 */
    grid_put(b, 6, 6, 3050);   /* Y += 3  →  3 */
    roster_compute_centroid((const uint8_t *)b);
    T_ASSERT_EQ_I(roster_centroid_x(), -3);
    T_ASSERT_EQ_I(roster_centroid_y(), 3);

    /* Clamp: pile 20 step-3 X+ items ⇒ far past +0xd, clamps to 13. */
    memset(&g_item, 0, sizeof g_item);
    set_item(0, 3050, 0, 0x200);   /* id 3000..3099 ⇒ step 3, X+ */
    int32_t *b2 = make_bank();
    for (int i = 0; i < 20; i++)
        grid_put(b2, 1, i, 3050);   /* row 1, all 20 cols: id 3050 (step 3, X+) */
    roster_compute_centroid((const uint8_t *)b2);
    T_ASSERT_EQ_I(roster_centroid_x(), 0xd);
    free(b);
    free(b2);
    return 0;
}
