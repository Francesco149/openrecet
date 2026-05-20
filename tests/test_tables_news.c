/*
 * test_tables_news.c — unit tests for src/tables_news.c.
 *
 * Pure-C tests under host gcc + ASan/UBSan via `make -C tests run`.
 * Fixtures use SJIS bytes as hex escapes so the source stays ASCII.
 */

#include "t.h"
#include "tables_news.h"

#include <stdlib.h>
#include <string.h>

/* ── SJIS shorthands ─────────────────────────────────────────────────── */
#define K_TARGET "\x91\xce\x8f\xdb\x8e\xd2"  /* 対象者 */
#define K_PERIOD "\x8e\x9e\x8a\xfa"          /* 時期   */
#define K_SPECIAL "\x93\xc1\x8e\xea"         /* 特殊   */

/* Attribute tags from oder_attr_hash (subset, in tag-index order). */
#define A_BUKI "\x95\x90\x8a\xed"  /* 武器 bit 0x0001 */
#define A_BOGU "\x96\x68\x8b\xef"  /* 防具 bit 0x0002 */
#define A_HOUSHOKU "\x95\xf3\x90\xce"  /* 宝石 bit 0x0008 (lol no — see oder) */

/* The kyaku-style category-resolver pretends `categories[i].singular`
 * holds the i'th candidate; the item-resolver pretends
 * `records[j].singular` holds the j'th item-name and `records[j].item_id`
 * holds its id. We use a flat array stub for the unit tests. */
typedef struct {
    const char *name;
    int32_t     id;
} stub_entry_t;

typedef struct {
    const stub_entry_t *categories;
    int                 cat_count;
    const stub_entry_t *items;
    int                 item_count;
} stub_state_t;

static int32_t stub_resolve_cat(const char *name, size_t name_len, void *user)
{
    const stub_state_t *s = (const stub_state_t *)user;
    for (int i = 0; i < s->cat_count; i++) {
        const char *cand = s->categories[i].name;
        size_t clen = strlen(cand);
        if (clen >= name_len && memcmp(name, cand, name_len) == 0) {
            return s->categories[i].id;
        }
    }
    return -1;
}

static int32_t stub_resolve_item(const char *name, size_t name_len, void *user)
{
    const stub_state_t *s = (const stub_state_t *)user;
    for (int i = 0; i < s->item_count; i++) {
        const char *cand = s->items[i].name;
        size_t clen = strlen(cand);
        if (clen >= name_len && memcmp(name, cand, name_len) == 0) {
            return s->items[i].id;
        }
    }
    return -1;
}

/* ── Tests ─────────────────────────────────────────────────────────── */

int test_tables_news_empty(void)
{
    news_state_t out;
    tables_parse_news((const unsigned char *)"", 0, &out,
                      NULL, NULL, NULL);
    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_news_layout_byte_offsets(void)
{
    /* Belt-and-suspenders runtime check on top of _Static_assert. */
    T_ASSERT_EQ_U(sizeof(news_record_t), 0xbc);
    T_ASSERT_EQ_U(offsetof(news_record_t, body),         0x00);
    T_ASSERT_EQ_U(offsetof(news_record_t, name),         0x80);
    T_ASSERT_EQ_U(offsetof(news_record_t, rate),         0x90);
    T_ASSERT_EQ_U(offsetof(news_record_t, price_lo),     0x94);
    T_ASSERT_EQ_U(offsetof(news_record_t, price_hi),     0x98);
    T_ASSERT_EQ_U(offsetof(news_record_t, attr_mask),    0x9c);
    T_ASSERT_EQ_U(offsetof(news_record_t, category),     0xa0);
    T_ASSERT_EQ_U(offsetof(news_record_t, item_id),      0xa4);
    T_ASSERT_EQ_U(offsetof(news_record_t, target_group), 0xa8);
    T_ASSERT_EQ_U(offsetof(news_record_t, days_lo),      0xac);
    T_ASSERT_EQ_U(offsetof(news_record_t, days_hi),      0xb0);
    T_ASSERT_EQ_U(offsetof(news_record_t, period_start), 0xb4);
    T_ASSERT_EQ_U(offsetof(news_record_t, period_end),   0xb8);
    return 0;
}

int test_tables_news_comments_and_blanks_skipped(void)
{
    static const unsigned char input[] =
        "/lead comment\r\n"
        "\r\n"
        "// also a comment (only line[0]=='/' matters)\r\n"
        "\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);
    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_news_special_attr_basic(void)
{
    /* "特殊,-1,3-1,body\r\n" — attr_mask = -1 (sentinel), no resolver call. */
    static const unsigned char input[] =
        K_SPECIAL ",-1,3-1,Prices for <I> are falling rapidly!\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    news_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->attr_mask,    -1);
    T_ASSERT_EQ_I(r->rate,         -1);
    T_ASSERT_EQ_I(r->price_lo,      3);
    T_ASSERT_EQ_I(r->price_hi,      1);
    T_ASSERT_EQ_I(r->category,     -1);
    T_ASSERT_EQ_I(r->item_id,      -1);
    T_ASSERT_EQ_I(r->days_lo,      -1);
    T_ASSERT_EQ_I(r->days_hi,      -1);
    T_ASSERT_EQ_I(r->target_group,  0);  /* default */
    T_ASSERT_EQ_I(r->period_start,  0);  /* default */
    T_ASSERT_EQ_I(r->period_end,  100);  /* default */

    /* Quirk #30: CRLF source leaves a trailing '\r' in the body. */
    T_ASSERT(strlen(r->body) > 0);
    T_ASSERT_EQ_I((unsigned char)r->body[strlen(r->body) - 1], '\r');
    /* And the body starts with "Prices ...". */
    T_ASSERT(memcmp(r->body, "Prices ", 7) == 0);
    return 0;
}

int test_tables_news_sjis_attr_mask(void)
{
    /* Name "武器" matches the SJIS attr table → attr_mask = 0x0001 (bit 0).
     * No category/item resolvers needed (engine doesn't try them once
     * attr_mask is non-zero). */
    static const unsigned char input[] =
        A_BUKI ",1,3-1,Weapons up.\r\n"
        A_BOGU ",-1,3-1,Armor down.\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT_EQ_I(out.records[0].attr_mask, 0x0001);  /* 武器 = bit 0 */
    T_ASSERT_EQ_I(out.records[1].attr_mask, 0x0002);  /* 防具 = bit 1 */
    /* category/item still at -1. */
    T_ASSERT_EQ_I(out.records[0].category, -1);
    T_ASSERT_EQ_I(out.records[0].item_id,  -1);
    return 0;
}

int test_tables_news_category_resolver_hit(void)
{
    static const stub_entry_t cats[] = { {"Daggers", 1}, {"Swords", 2} };
    stub_state_t state = { cats, 2, NULL, 0 };
    static const unsigned char input[] =
        "Daggers,1,3-1,The price of daggers has increased.\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out,
                      stub_resolve_cat, stub_resolve_item, &state);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].attr_mask, 0);
    T_ASSERT_EQ_I(out.records[0].category, 1);
    T_ASSERT_EQ_I(out.records[0].item_id, -1);
    return 0;
}

int test_tables_news_item_resolver_hit(void)
{
    /* Name "Candy" doesn't match any SJIS attr or category — falls through
     * to the item resolver, which returns the item_id. */
    static const stub_entry_t cats[]  = { {"Daggers", 1} };
    static const stub_entry_t items[] = { {"Candy", 42} };
    stub_state_t state = { cats, 1, items, 1 };
    static const unsigned char input[] =
        "Candy,1,3-1,The price of Candy has increased.\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out,
                      stub_resolve_cat, stub_resolve_item, &state);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].attr_mask, 0);
    T_ASSERT_EQ_I(out.records[0].category, -1);
    T_ASSERT_EQ_I(out.records[0].item_id,  42);
    return 0;
}

int test_tables_news_lookup_chain_precedence(void)
{
    /* If attr matches, neither category nor item resolver fires.
     * If category matches, item resolver doesn't fire. */
    static const stub_entry_t cats[]  = { {A_BUKI, 99} };  /* 武器 as category? */
    static const stub_entry_t items[] = { {A_BUKI, 88} };
    stub_state_t state = { cats, 1, items, 1 };
    static const unsigned char input[] =
        A_BUKI ",1,3-1,attr beats cat.\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out,
                      stub_resolve_cat, stub_resolve_item, &state);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].attr_mask, 0x0001);
    T_ASSERT_EQ_I(out.records[0].category, -1);  /* not consulted */
    T_ASSERT_EQ_I(out.records[0].item_id,  -1);
    return 0;
}

int test_tables_news_days_range_optional(void)
{
    /* Vendor mix: lines with and without days_lo-days_hi between the
     * price range and the body. */
    static const unsigned char input[] =
        K_SPECIAL ",-1,3-1,No days here.\r\n"
        K_SPECIAL ",1,3-1,0-1000,Days 0..1000 here.\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT_EQ_I(out.records[0].days_lo, -1);
    T_ASSERT_EQ_I(out.records[0].days_hi, -1);
    T_ASSERT_EQ_I(out.records[1].days_lo,  0);
    T_ASSERT_EQ_I(out.records[1].days_hi, 1000);
    return 0;
}

int test_tables_news_dash_row(void)
{
    /* "-,-,body\r\n" — generic news; both lookups skipped. Engine quirks:
     * category=-100, attr_mask=0, target_group=0, days_lo=days_hi=0
     * (NOT -1 like non-"-" rows). */
    static const unsigned char input[] =
        K_TARGET ",14\r\n"
        "-,-,Fluffles the cat is missing.\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    news_record_t *r = &out.records[0];
    T_ASSERT_EQ_I(r->category,     NEWS_CATEGORY_DASH);
    T_ASSERT_EQ_I(r->attr_mask,    0);
    T_ASSERT_EQ_I(r->item_id,      0);  /* BSS-zero, NOT -1 */
    T_ASSERT_EQ_I(r->days_lo,      0);  /* BSS-zero, NOT -1 */
    T_ASSERT_EQ_I(r->days_hi,      0);
    T_ASSERT_EQ_I(r->target_group, 0);  /* quirk #29: not set for "-" rows */
    T_ASSERT(memcmp(r->body, "Fluffles ", 9) == 0);
    return 0;
}

int test_tables_news_target_group_sticky(void)
{
    /* The "対象者:" header sets a sticky target_group applied to all
     * subsequent non-"-" data rows. */
    static const unsigned char input[] =
        K_TARGET ",14\r\n"
        K_SPECIAL ",-1,3-1,first message\r\n"
        K_TARGET ",17\r\n"
        K_SPECIAL ",-1,3-1,second message\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT_EQ_I(out.records[0].target_group, 14);
    T_ASSERT_EQ_I(out.records[1].target_group, 17);
    return 0;
}

int test_tables_news_period_sticky(void)
{
    /* The "時期:" header sets period_start/period_end. Sticky across
     * data rows. Both "-" and non-"-" rows pick up the current values. */
    static const unsigned char input[] =
        K_PERIOD ",1-8\r\n"
        K_SPECIAL ",-1,3-1,early\r\n"
        K_PERIOD ",100-999\r\n"
        "-,-,generic news\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT_EQ_I(out.records[0].period_start, 1);
    T_ASSERT_EQ_I(out.records[0].period_end,   8);
    T_ASSERT_EQ_I(out.records[1].period_start, 100);
    T_ASSERT_EQ_I(out.records[1].period_end,   999);
    return 0;
}

int test_tables_news_period_defaults_apply_before_header(void)
{
    /* No "時期:" header before the data row → defaults (0, 100). */
    static const unsigned char input[] =
        K_SPECIAL ",-1,3-1,no period header\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].period_start, NEWS_PERIOD_START_DEFAULT);
    T_ASSERT_EQ_I(out.records[0].period_end,   NEWS_PERIOD_END_DEFAULT);
    return 0;
}

int test_tables_news_period_missing_dash_leaves_end_unchanged(void)
{
    /* Engine "loop err 6": "時期,A" with no '-' leaves period_end at
     * its previous value (the default 100 on first occurrence). */
    static const unsigned char input[] =
        K_PERIOD ",1-8\r\n"
        K_PERIOD ",42\r\n"                    /* malformed — no '-' */
        K_SPECIAL ",-1,3-1,after malformed\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    /* period_start got bumped to 42 from the malformed header; period_end
     * stayed at the previous value (8). */
    T_ASSERT_EQ_I(out.records[0].period_start, 42);
    T_ASSERT_EQ_I(out.records[0].period_end,   8);
    return 0;
}

int test_tables_news_no_trailing_newline(void)
{
    /* Last line lacks a trailing \r\n — body terminates on the buffer-end
     * NUL. No '\r' suffix in the body (quirk #30 is dormant here). */
    static const unsigned char input[] =
        K_SPECIAL ",-1,3-1,no eol";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT(strcmp(out.records[0].body, "no eol") == 0);
    return 0;
}

int test_tables_news_body_keeps_trailing_cr_on_crlf(void)
{
    static const unsigned char input[] =
        K_SPECIAL ",-1,3-1,crlf body\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    size_t blen = strlen(out.records[0].body);
    T_ASSERT(blen > 0);
    T_ASSERT_EQ_I((unsigned char)out.records[0].body[blen - 1], '\r');
    return 0;
}

int test_tables_news_body_strips_on_lf_only(void)
{
    /* LF-only line terminator → body copy stops AT the '\n', no '\r'
     * suffix. */
    static const unsigned char input[] =
        K_SPECIAL ",-1,3-1,lf only\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT(strcmp(out.records[0].body, "lf only") == 0);
    return 0;
}

int test_tables_news_no_resolver_misses_silently(void)
{
    /* Name that matches nothing — all fields stay at -1, parser still
     * counts the record (engine MessageBoxA's "syn error" then continues
     * incrementing count). */
    static const unsigned char input[] =
        "NoSuchThing,1,3-1,nothing matches\r\n";
    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out, NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].attr_mask, 0);
    T_ASSERT_EQ_I(out.records[0].category, -1);
    T_ASSERT_EQ_I(out.records[0].item_id,  -1);
    return 0;
}

int test_tables_news_max_records_cap(void)
{
    /* More records than NEWS_MAX_RECORDS → excess silently dropped. */
    static char buf[64 * 1024];
    size_t len = 0;
    int rows = NEWS_MAX_RECORDS + 10;
    for (int i = 0; i < rows; i++) {
        len += (size_t)snprintf(buf + len, sizeof buf - len,
                                K_SPECIAL ",%d,3-1,row %d\r\n", i, i);
    }
    news_state_t out;
    tables_parse_news((const unsigned char *)buf, len, &out,
                      NULL, NULL, NULL);

    T_ASSERT_EQ_I(out.count, NEWS_MAX_RECORDS);
    return 0;
}

int test_tables_news_vendor_shape(void)
{
    /* Mirrors the structure of the real vendor file in miniature:
     * comment header, then a period header, then a Candy-by-name row,
     * then a SJIS-attr row, then a target_group switch, then a "-" row,
     * then a "特殊" row. Verifies that ALL of these paths thread
     * sequentially through the same parser and pick up the right
     * sticky state. */
    static const stub_entry_t cats[]  = {
        {"Daggers", 1}, {"Swords", 2}
    };
    static const stub_entry_t items[] = {
        {"Candy", 42}, {"Bread", 7}
    };
    stub_state_t state = { cats, 2, items, 2 };

    static const unsigned char input[] =
        "//random event\r\n"
        K_PERIOD ",1-8\r\n"
        "Candy,1,3-1,The price of Candy has increased.\r\n"
        "\r\n"
        K_PERIOD ",1-999\r\n"
        A_BUKI ",1,3-1,The price of weapons has increased.\r\n"
        A_BUKI ",-1,3-1,The price of weapons has decreased.\r\n"
        "Daggers,1,3-1,The price of daggers has increased.\r\n"
        "\r\n"
        K_TARGET ",14\r\n"
        A_BUKI ",0,3-1,0-1000,Men of character seek <I>.\r\n"
        "\r\n"
        K_TARGET ",0\r\n"
        K_SPECIAL ",-1,3-1,Prices for <I> are falling rapidly!\r\n"
        "\r\n"
        K_PERIOD ",1-8\r\n"
        "-,-,Fluffles the cat is currently missing.\r\n"
        "-,-,Big Bash is here for your shopping needs.\r\n";

    news_state_t out;
    tables_parse_news(input, sizeof input - 1, &out,
                      stub_resolve_cat, stub_resolve_item, &state);

    T_ASSERT_EQ_I(out.count, 8);

    /* [0] Candy (item resolver hit), period 1-8. */
    T_ASSERT_EQ_I(out.records[0].item_id,  42);
    T_ASSERT_EQ_I(out.records[0].category, -1);
    T_ASSERT_EQ_I(out.records[0].attr_mask, 0);
    T_ASSERT_EQ_I(out.records[0].period_start, 1);
    T_ASSERT_EQ_I(out.records[0].period_end,   8);
    T_ASSERT_EQ_I(out.records[0].target_group, 0);

    /* [1] 武器 attr-mask up, period 1-999. */
    T_ASSERT_EQ_I(out.records[1].attr_mask, 0x0001);
    T_ASSERT_EQ_I(out.records[1].rate,      1);
    T_ASSERT_EQ_I(out.records[1].period_start, 1);
    T_ASSERT_EQ_I(out.records[1].period_end, 999);

    /* [2] 武器 attr-mask down. */
    T_ASSERT_EQ_I(out.records[2].attr_mask, 0x0001);
    T_ASSERT_EQ_I(out.records[2].rate,     -1);

    /* [3] Daggers category. */
    T_ASSERT_EQ_I(out.records[3].category, 1);
    T_ASSERT_EQ_I(out.records[3].attr_mask, 0);
    T_ASSERT_EQ_I(out.records[3].item_id, -1);

    /* [4] target_group=14 + 武器 attr + days range. */
    T_ASSERT_EQ_I(out.records[4].attr_mask, 0x0001);
    T_ASSERT_EQ_I(out.records[4].target_group, 14);
    T_ASSERT_EQ_I(out.records[4].days_lo,  0);
    T_ASSERT_EQ_I(out.records[4].days_hi, 1000);

    /* [5] 特殊 with target_group=0 reset. */
    T_ASSERT_EQ_I(out.records[5].attr_mask, -1);
    T_ASSERT_EQ_I(out.records[5].target_group, 0);

    /* [6] / [7] "-" generic rows under period 1-8. target_group stays 0
     * for "-" rows regardless of the most-recent "対象者:" header (which
     * happens to be 0 here anyway — quirk dormant). */
    T_ASSERT_EQ_I(out.records[6].category, NEWS_CATEGORY_DASH);
    T_ASSERT_EQ_I(out.records[6].attr_mask, 0);
    T_ASSERT_EQ_I(out.records[6].target_group, 0);
    T_ASSERT_EQ_I(out.records[6].period_start, 1);
    T_ASSERT_EQ_I(out.records[6].period_end,   8);
    T_ASSERT(memcmp(out.records[6].body, "Fluffles ", 9) == 0);

    T_ASSERT_EQ_I(out.records[7].category, NEWS_CATEGORY_DASH);
    T_ASSERT(memcmp(out.records[7].body, "Big Bash ", 9) == 0);
    return 0;
}
