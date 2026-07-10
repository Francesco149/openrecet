/*
 * test_news_daily.c — the daily-news subsystem (src/news_daily.c):
 * generator FUN_00436623, picker FUN_004363c6, reset FUN_00436180,
 * price-trend classifier FUN_004361b2.
 *
 * Pins the objdump-exact behaviour on synthetic g_news/g_item data:
 * eligibility windows, the dedup/eligibility reroll predicate, the
 * '<'-splice, the boom thresholds + pair TTLs, expiry message
 * branches (incl. engine quirk #132's raw-slot lookup), the scroll
 * offsets, and — load-bearing for parity — the exact RNG draw COUNT
 * of every phase (asserted via rng_call_count()).
 * RE: docs/findings/news-daily-RE.md.
 */
#include "t.h"
#include "../src/news_daily.h"
#include "../src/tables_news.h"
#include "../src/tables_item.h"
#include "../src/save_bank.h"
#include "../src/rng.h"

#include <stdlib.h>
#include <string.h>

/* ── synthetic fixtures ──────────────────────────────────────────────── */

static uint8_t *nd_make_bank(int day, int rank)
{
    uint8_t *b = calloc(1, SAVE_BANK_STRIDE_BYTES);
    ((int32_t *)b)[SAVE_BANK_FIELD_SHOP_DAY]  = day;
    ((int32_t *)b)[SAVE_BANK_FIELD_SHOP_RANK] = rank;
    return b;
}

static news_record_t *nd_row(int id)   /* 1-based, like the engine */
{
    return &g_news.records[id - 1];
}

/* One plain always-eligible news row: rate/attr/category/item as given,
 * evergreen period, lifetime dur_base..+dur_range, no target-item pick. */
static void nd_set_row(int id, const char *body, int rate, int attr,
                       int category, int item_id)
{
    news_record_t *r = nd_row(id);
    memset(r, 0, sizeof(*r));
    snprintf(r->body, sizeof(r->body), "%s", body);
    r->rate = rate;
    r->attr_mask = attr;
    r->category = category;
    r->item_id = item_id;
    r->dur_base = 3;
    r->dur_range = 0;          /* no lifetime rng draw by default */
    r->price_lo = -1;          /* no target-item scan by default */
    r->price_hi = -1;
    r->period_start = 0;
    r->period_end = 999;
    if (g_news.count < id)
        g_news.count = id;
}

static void nd_set_item(int slot, int item_id, int price, uint32_t attr,
                        int category, const char *plural)
{
    item_record_t *it = &g_item.records[slot];
    memset(it, 0, sizeof(*it));
    it->valid = 1;
    it->item_id = item_id;
    it->price = price;
    it->attr_mask = attr;
    it->category = category;
    snprintf(it->plural, sizeof(it->plural), "%s", plural);
    if (g_item.count <= slot)
        g_item.count = slot + 1;
}

static void nd_reset_tables(void)
{
    memset(&g_news, 0, sizeof(g_news));
    memset(&g_item, 0, sizeof(g_item));
}

static int32_t *nd_entry_id(uint8_t *b, int i)
{
    return (int32_t *)(b + SAVE_BANK_NEWS_ENTRY_BYTE_OFF + i * 0xc) + 1;
}
static int32_t *nd_entry_target(uint8_t *b, int i)
{
    return (int32_t *)(b + SAVE_BANK_NEWS_ENTRY_BYTE_OFF + i * 0xc);
}
static uint8_t *nd_entry(uint8_t *b, int i)
{
    return b + SAVE_BANK_NEWS_ENTRY_BYTE_OFF + i * 0xc;
}
static const char *nd_headline(uint8_t *b, int n)
{
    return (const char *)b + SAVE_BANK_NEWS_HL_TEXT_BYTE_OFF
           + n * SAVE_BANK_NEWS_HL_ROW_BYTES;
}
static int16_t *nd_pairs(uint8_t *b)
{
    return (int16_t *)(b + SAVE_BANK_NEWS_PAIRS_BYTE_OFF);
}

/* ── picker ─────────────────────────────────────────────────────────── */

int test_news_pick_day9_scripted(void)
{
    nd_reset_tables();
    nd_set_row(1, "first", 1, 1, -1, -1);
    nd_set_row(2, "second", 1, 2, -1, -1);
    rng_seed(1);
    unsigned long before = rng_call_count();
    T_ASSERT_EQ_I(news_pick_def(9), 0);       /* scripted, id 1 */
    T_ASSERT_EQ_U(rng_call_count() - before, 0);
    return 0;
}

int test_news_pick_period_window(void)
{
    nd_reset_tables();
    /* row 1 stale (period 0-10), row 2 evergreen (999), row 3 day-range
     * pool (category -100, excluded), row 4 future (20-30). */
    nd_set_row(1, "stale", 1, 1, -1, -1);
    nd_row(1)->period_end = 10;
    nd_set_row(2, "evergreen", 1, 2, -1, -1);
    nd_row(2)->period_start = 0;
    nd_row(2)->period_end = 999;
    nd_set_row(3, "dayrange", 0, 0, NEWS_CATEGORY_DASH, -1);
    nd_set_row(4, "future", 1, 4, -1, -1);
    nd_row(4)->period_start = 20;
    nd_row(4)->period_end = 30;

    rng_seed(1);
    unsigned long before = rng_call_count();
    int pick = news_pick_def(15);   /* only row 2 eligible */
    T_ASSERT_EQ_I(pick, 1);         /* 0-based index of row id 2 */
    T_ASSERT_EQ_U(rng_call_count() - before, 1);

    /* no eligible rows at all → -1, no draw (cap the evergreen row) */
    nd_row(2)->period_end = 10;
    before = rng_call_count();
    T_ASSERT_EQ_I(news_pick_def(15), -1);
    T_ASSERT_EQ_U(rng_call_count() - before, 0);
    return 0;
}

/* ── reset ──────────────────────────────────────────────────────────── */

int test_news_reset_marks_entries_minus1(void)
{
    uint8_t *b = nd_make_bank(9, 1);
    *nd_entry_id(b, 3) = 7;
    nd_entry(b, 3)[8] = 'd';
    nd_entry(b, 3)[9] = 5;
    *nd_entry_target(b, 3) = 42;
    news_list_reset(b);
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        T_ASSERT_EQ_I(*nd_entry_id(b, i), -1);
        T_ASSERT_EQ_I(nd_entry(b, i)[8], 0);
        T_ASSERT_EQ_I(nd_entry(b, i)[9], 0);
    }
    T_ASSERT_EQ_I(*nd_entry_target(b, 3), 42);   /* target untouched */
    /* a reset list has NO id==0 slot → the generator never adds news */
    nd_reset_tables();
    nd_set_row(1, "x", 1, 1, -1, -1);
    rng_seed(1);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    T_ASSERT_EQ_U(rng_call_count() - before, 0);
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_COUNT], 0);
    free(b);
    return 0;
}

/* ── generator: accept + dedup + lifetime ───────────────────────────── */

int test_news_gen_accepts_one_row(void)
{
    nd_reset_tables();
    nd_set_row(1, "Big weapon sale!", 2, 0x1, -1, -1);
    uint8_t *b = nd_make_bank(10, 1);
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    /* draws: 1 pick (row 1 the only random-pool row… day-range pool empty)
     * + 0 dur (dur_range 0) + 0 item scan (price_lo -1) + 0 boom (no
     * pairs) + 0 day-range (no -100 rows) = 1 */
    T_ASSERT_EQ_U(rng_call_count() - before, 1);
    T_ASSERT_EQ_I(*nd_entry_id(b, 0), 1);
    T_ASSERT_EQ_I(nd_entry(b, 0)[8], 2);            /* trend = rate byte */
    /* dur = base 3 + 1 = 4, then the same call's expiry pass -1 → 3 */
    T_ASSERT_EQ_I(nd_entry(b, 0)[9], 3);
    T_ASSERT_EQ_I(*nd_entry_target(b, 0), -1);
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_COUNT], 1);
    T_ASSERT(strcmp(nd_headline(b, 0), "Big weapon sale!") == 0);
    /* offsets: one row at 0; total = strlen+4 */
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_OFFS], 0);
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_TOTAL],
                  (int32_t)strlen("Big weapon sale!") + 4);
    free(b);
    return 0;
}

int test_news_gen_rate0_trend_d_and_generic_dedup(void)
{
    nd_reset_tables();
    nd_set_row(1, "generic one", 0, 0, NEWS_CATEGORY_DASH, -1);
    /* -100 rows are EXCLUDED from the random pool → use a 0-rate row with
     * a real category instead */
    nd_set_row(2, "generic news", 0, 0, 3, -1);
    nd_set_row(3, "weapon news", 1, 0x1, -1, -1);
    uint8_t *b = nd_make_bank(10, 1);
    /* seed an ACTIVE generic (rate 0) entry: id 2 in slot 1; the free
     * slot is 0.  Candidate id 2 → same-id conflict; candidate id 2's
     * sibling generic → rate0-vs-rate0 conflict; only row 3 accepted. */
    *nd_entry_id(b, 1) = 2;
    nd_entry(b, 1)[8] = 'd';
    nd_entry(b, 1)[9] = 9;
    /* force the pick sequence: try seeds until the first pick is row 2
     * (index 1), proving the reroll lands on row 3. */
    int seeded = 0;
    for (uint32_t s = 1; s < 5000; s++) {
        rng_seed(s);
        if (news_pick_def(10) == 1) { seeded = 1; rng_seed(s); break; }
    }
    T_ASSERT(seeded);
    news_daily_update(b);
    T_ASSERT_EQ_I(*nd_entry_id(b, 0), 3);   /* rerolled off the generic */
    T_ASSERT_EQ_I(nd_entry(b, 0)[8], 1);
    free(b);
    return 0;
}

int test_news_gen_attr_dedup_and_special_reroll(void)
{
    nd_reset_tables();
    nd_set_row(1, "sp", 1, -1, -1, -1);       /* 特殊 — never picked */
    nd_set_row(2, "armor A", 1, 0x2, -1, -1);
    nd_set_row(3, "armor B", 2, 0x2, -1, -1); /* same attr as 2 → dedup */
    nd_set_row(4, "sweets", 1, 0x100, -1, -1);
    uint8_t *b = nd_make_bank(10, 1);
    *nd_entry_id(b, 1) = 2;                   /* active armor news */
    nd_entry(b, 1)[8] = 1;
    nd_entry(b, 1)[9] = 9;
    /* whatever the pick order, only row 4 can be accepted */
    rng_seed(19937);
    news_daily_update(b);
    T_ASSERT_EQ_I(*nd_entry_id(b, 0), 4);
    free(b);
    return 0;
}

int test_news_gen_lifetime_rng_and_min2(void)
{
    nd_reset_tables();
    nd_set_row(1, "x", 1, 1, -1, -1);
    nd_row(1)->dur_base = 0;
    nd_row(1)->dur_range = 3;    /* dur = 0 + rng%3 + 1 ∈ 1..3, min 2 */
    uint8_t *b = nd_make_bank(10, 1);
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    T_ASSERT_EQ_U(rng_call_count() - before, 2);   /* pick + dur */
    /* post-expiry-decrement value ∈ {1, 2} and ≥ (min2 − 1) */
    int8_t d = (int8_t)nd_entry(b, 0)[9];
    T_ASSERT(d >= 1 && d <= 2);
    free(b);
    return 0;
}

/* ── generator: target-item scan + '<'-splice ───────────────────────── */

int test_news_gen_item_target_and_splice(void)
{
    nd_reset_tables();
    /* body carries the 3-byte marker '<' + 2 filler bytes (one SJIS char
     * in vendor data) */
    nd_set_row(1, "Get your <XX today!", 1, 0x1, -1, -1);
    nd_row(1)->price_lo = 100;
    nd_row(1)->price_hi = 200;
    /* items: slot 0 out-of-window, slot 1 the only match (attr 0x1),
     * slot 2 wrong attr */
    nd_set_item(0, 900, 999, 0x1, 9, "Costly Swords");
    nd_set_item(1, 901, 150, 0x1, 9, "Iron Swords");
    nd_set_item(2, 902, 150, 0x2, 9, "Shields");
    uint8_t *b = nd_make_bank(10, 1);
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    /* draws: pick(1) + item-scan(1; one match) = 2 */
    T_ASSERT_EQ_U(rng_call_count() - before, 2);
    T_ASSERT_EQ_I(*nd_entry_target(b, 0), 901);
    T_ASSERT(strcmp(nd_headline(b, 0), "Get your Iron Swords today!") == 0);
    free(b);
    return 0;
}

int test_news_gen_item_scan_no_match_no_draw(void)
{
    nd_reset_tables();
    nd_set_row(1, "No stock news", 1, 0x1, -1, -1);
    nd_row(1)->price_lo = 100;
    nd_row(1)->price_hi = 200;   /* window matches nothing */
    nd_set_item(0, 900, 999, 0x1, 9, "Costly Swords");
    uint8_t *b = nd_make_bank(10, 1);
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    T_ASSERT_EQ_U(rng_call_count() - before, 1);   /* pick only */
    T_ASSERT_EQ_I(*nd_entry_target(b, 0), -1);
    free(b);
    return 0;
}

/* ── generator: boom news + pair TTLs ───────────────────────────────── */

int test_news_gen_boom_and_pairs(void)
{
    nd_reset_tables();
    /* every row OUT of its 時期 window so the random-pick pool is empty
     * (news_pick_def → -1, zero draws; the id-0 sentinel is accepted
     * draw-free and not counted) — isolates the boom-phase draws. */
    nd_set_row(1, "sp", 1, -1, -1, -1);
    nd_set_row(NEWS_BOOM_ID,     "The <XX boom is on!", 3, 0, -1, -1);
    nd_set_row(NEWS_BOOM_ALT_ID, "<XX are hot but fading!", -2, 0, -1, -1);
    nd_row(1)->period_start = 999;             nd_row(1)->period_end = 0;
    nd_row(NEWS_BOOM_ID)->period_start = 999;  nd_row(NEWS_BOOM_ID)->period_end = 0;
    nd_row(NEWS_BOOM_ALT_ID)->period_start = 999; nd_row(NEWS_BOOM_ALT_ID)->period_end = 0;
    nd_set_item(0, 12, 100, 0x1, 0, "Walnut Breads");
    uint8_t *b = nd_make_bank(10, 9);   /* rank 9 = boom-eligible */
    int16_t *pairs = nd_pairs(b);
    for (int i = 0; i < 8; i++) {       /* mult 8 ⇒ p=100: always booms */
        pairs[i * 2] = 12;
        pairs[i * 2 + 1] = 3;
    }
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    /* draws: new-news 0 (empty pool → -1 → sentinel accept, draw-free);
     * boom: threshold(1) + variant(1) + duration(1) = 3. */
    T_ASSERT_EQ_U(rng_call_count() - before, 3);
    int32_t id = *nd_entry_id(b, 0);
    T_ASSERT(id == NEWS_BOOM_ID || id == NEWS_BOOM_ALT_ID);
    T_ASSERT_EQ_I(*nd_entry_target(b, 0), 12);
    /* trend byte = the boom row's rate (NO 0→'d' fixup on this path) */
    T_ASSERT_EQ_I((int8_t)nd_entry(b, 0)[8], nd_row(id)->rate);
    /* the hot pairs are cleared */
    for (int i = 0; i < 8; i++)
        T_ASSERT_EQ_I(pairs[i * 2], 0);
    /* headline spliced with the plural (target 12 ≤ 10 is false → 12>10
     * ⇒ slot conversion; synthetic slot 0 carries id 12) */
    T_ASSERT(strstr(nd_headline(b, 0), "Walnut Breads") != NULL);
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_COUNT], 1);
    free(b);
    return 0;
}

int test_news_gen_boom_needs_rank9(void)
{
    nd_reset_tables();
    nd_set_row(1, "sp", 1, -1, -1, -1);
    nd_set_row(NEWS_BOOM_ID, "boom", 3, 0, -1, -1);
    nd_row(1)->period_start = 999;            nd_row(1)->period_end = 0;
    nd_row(NEWS_BOOM_ID)->period_start = 999; nd_row(NEWS_BOOM_ID)->period_end = 0;
    uint8_t *b = nd_make_bank(10, 8);   /* rank 8 < 9: no boom news */
    int16_t *pairs = nd_pairs(b);
    for (int i = 0; i < 8; i++) { pairs[i*2] = 12; pairs[i*2+1] = 3; }
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    /* the threshold draw STILL happens (asm 436a90 precedes the rank
     * gate) — load-bearing LCG count */
    T_ASSERT_EQ_U(rng_call_count() - before, 1);
    T_ASSERT_EQ_I(*nd_entry_id(b, 0), 0);
    /* TTLs still decremented */
    T_ASSERT_EQ_I(pairs[1], 2);
    free(b);
    return 0;
}

int test_news_gen_pair_ttl_expires(void)
{
    nd_reset_tables();
    uint8_t *b = nd_make_bank(10, 1);   /* rank 1: no boom */
    int16_t *pairs = nd_pairs(b);
    pairs[0] = 7;  pairs[1] = 1;        /* expires this update */
    pairs[2] = 8;  pairs[3] = 2;
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    T_ASSERT_EQ_U(rng_call_count() - before, 1);  /* threshold draw only */
    T_ASSERT_EQ_I(pairs[0], 0);
    T_ASSERT_EQ_I(pairs[1], 0);
    T_ASSERT_EQ_I(pairs[2], 8);
    T_ASSERT_EQ_I(pairs[3], 1);
    free(b);
    return 0;
}

/* ── generator: expiry messages ─────────────────────────────────────── */

int test_news_gen_expiry_branches(void)
{
    nd_reset_tables();
    nd_set_row(1, "w", 5, 0x1, -1, -1);       /* attr row */
    nd_set_row(2, "n", 5, 0, 3, -1);          /* named row */
    snprintf(nd_row(2)->name, NEWS_NAME_LEN, "%s", "Karagances");
    nd_set_item(0, 12, 100, 0x1, 0, "Walnut Breads");
    nd_set_item(3, 900, 100, 0x1, 9, "Quirk Slot Threes");
    uint8_t *b = nd_make_bank(10, 1);
    /* the random pool has pickable rows — park an active generic in the
     * free slot?  Simpler: fill ALL slots so no new news is added, with
     * four expiring entries covering the branches. */
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++)
        *nd_entry_id(b, i) = -1;              /* occupied (reset-style) */

    /* [0] trend 'd', target 12 (>10 ⇒ slot conv → synthetic slot 0) */
    *nd_entry_id(b, 0) = 1; *nd_entry_target(b, 0) = 12;
    nd_entry(b, 0)[8] = 'd'; nd_entry(b, 0)[9] = 1;
    /* [1] trend 5 (≠0,≠'d'), target -1, attr row ⇒ attr display name */
    *nd_entry_id(b, 1) = 1; *nd_entry_target(b, 1) = -1;
    nd_entry(b, 1)[8] = 5; nd_entry(b, 1)[9] = 1;
    /* [2] trend 0, target 3 ⇒ QUIRK #132: raw 3 used as SLOT directly */
    *nd_entry_id(b, 2) = 2; *nd_entry_target(b, 2) = 3;
    nd_entry(b, 2)[8] = 0; nd_entry(b, 2)[9] = 1;
    /* [3] trend 5, target -1, row 2 attr 0 ⇒ def name */
    *nd_entry_id(b, 3) = 2; *nd_entry_target(b, 3) = -1;
    nd_entry(b, 3)[8] = 5; nd_entry(b, 3)[9] = 1;

    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    T_ASSERT_EQ_U(rng_call_count() - before, 0);  /* expiry draws nothing */
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_COUNT], 4);
    T_ASSERT(strcmp(nd_headline(b, 0),
                    "The Walnut Breads boom has ended.") == 0);
    T_ASSERT(strcmp(nd_headline(b, 1),
                    "The price of weapons has normalized.") == 0);
    T_ASSERT(strcmp(nd_headline(b, 2),
                    "The Quirk Slot Threes boom has ended.") == 0);
    T_ASSERT(strcmp(nd_headline(b, 3),
                    "The price of Karagances has normalized.") == 0);
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_I(*nd_entry_id(b, i), 0);     /* freed */
        T_ASSERT_EQ_I(nd_entry(b, i)[9], 0);
    }
    /* offsets accumulate strlen+4 */
    int32_t *w = (int32_t *)b;
    T_ASSERT_EQ_I(w[SAVE_BANK_FIELD_NEWS_HL_OFFS + 1],
                  (int32_t)strlen(nd_headline(b, 0)) + 4);
    T_ASSERT_EQ_I(w[SAVE_BANK_FIELD_NEWS_HL_TOTAL],
                  w[SAVE_BANK_FIELD_NEWS_HL_OFFS + 3] +
                  (int32_t)strlen(nd_headline(b, 3)) + 4);
    free(b);
    return 0;
}

/* ── generator: day-range story news ────────────────────────────────── */

int test_news_gen_day_range_news(void)
{
    nd_reset_tables();
    nd_set_row(1, "sp", 1, -1, -1, -1);
    nd_row(1)->period_start = 999;   /* out of window: empty random pool */
    nd_row(1)->period_end = 0;
    nd_set_row(2, "Founding festival!", 0, 0, NEWS_CATEGORY_DASH, -1);
    nd_row(2)->period_start = 10;
    nd_row(2)->period_end = 12;
    uint8_t *b = nd_make_bank(11, 1);
    rng_seed(19937);
    unsigned long before = rng_call_count();
    news_daily_update(b);
    T_ASSERT_EQ_U(rng_call_count() - before, 1); /* the day-range pick */
    T_ASSERT_EQ_I(((int32_t *)b)[SAVE_BANK_FIELD_NEWS_HL_COUNT], 1);
    T_ASSERT(strcmp(nd_headline(b, 0), "Founding festival!") == 0);
    /* day 9 (or ≤9) never draws the day-range pick */
    uint8_t *b2 = nd_make_bank(9, 1);
    rng_seed(19937);
    before = rng_call_count();
    news_daily_update(b2);
    T_ASSERT_EQ_U(rng_call_count() - before, 0); /* day-9 scripted pick is
                                                  * draw-free + no pool */
    free(b); free(b2);
    return 0;
}

/* ── classifier ─────────────────────────────────────────────────────── */

int test_news_trend_classifier(void)
{
    nd_reset_tables();
    nd_set_row(1, "w", 1, 0x1, -1, -1);    /* weapons +1 */
    nd_set_row(2, "w", -2, 0x1, -1, -1);   /* weapons hard down */
    nd_set_row(3, "i", 1, 0, -1, 55);      /* direct item 55 */
    nd_set_item(0, 55, 100, 0x1, 0, "Swords");
    uint8_t *b = nd_make_bank(10, 1);

    /* empty list ⇒ 0 (live-validated §22) */
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 0), 0);
    /* tutorial gate ⇒ 0 */
    *nd_entry_id(b, 0) = 1;
    nd_entry(b, 0)[8] = 1;
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 1), 0);
    /* attr match, trend char 1 ⇒ +1 */
    *nd_entry_target(b, 0) = -1;
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 0), 1);
    /* 'd' entries are skipped */
    nd_entry(b, 0)[8] = 'd';
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 0), 0);
    /* accumulate + clamp: two +1 entries ⇒ still 1 */
    nd_entry(b, 0)[8] = 1;
    *nd_entry_id(b, 1) = 3; *nd_entry_target(b, 1) = -1;
    nd_entry(b, 1)[8] = 1;
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 0), 1);
    /* a ≤-2 trend char anywhere ⇒ -2 regardless of the sum */
    *nd_entry_id(b, 2) = 2; *nd_entry_target(b, 2) = -1;
    nd_entry(b, 2)[8] = (uint8_t)-2;
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 0), -2);
    /* direct-target entries only hit their item */
    memset(nd_entry(b, 0), 0, 0xc * 3);
    *nd_entry_id(b, 0) = 3; *nd_entry_target(b, 0) = 55;
    nd_entry(b, 0)[8] = 1;
    T_ASSERT_EQ_I(news_price_trend(b, 55 << 6, 0), 1);
    T_ASSERT_EQ_I(news_price_trend(b, 56 << 6, 0), 0);
    free(b);
    return 0;
}

int test_news_attr_names(void)
{
    T_ASSERT(strcmp(news_attr_display_name(0x1), "weapons") == 0);
    T_ASSERT(strcmp(news_attr_display_name(0x8000),
                    "sinister things") == 0);
    T_ASSERT(strcmp(news_attr_display_name(0x2000), "Food") == 0);
    /* unknown ⇒ SJIS ダミー */
    T_ASSERT(strcmp(news_attr_display_name(0x3),
                    "\x83_\x83~\x81[") == 0);
    return 0;
}
