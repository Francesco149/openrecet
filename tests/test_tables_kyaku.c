/*
 * test_tables_kyaku.c — unit tests for src/tables_kyaku.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. Inputs use SJIS bytes spelt as hex escapes so
 * the source stays ASCII-clean.
 */

#include "t.h"
#include "tables_kyaku.h"
#include "tables_item.h"

#include <stdlib.h>
#include <string.h>

/* ── SJIS key shorthands (match the engine's interned strings) ──────── */
#define K_NAMEIDX "\x96\xbc\x91\x4f\x94\xd4\x8d\x86:"                 /* 名前番号: */
#define K_ATTR    "\x91\xae\x90\xab:"                                  /* 属性:     */
#define K_BUDGET  "\x97\x5c\x8e\x5a:"                                  /* 予算:     */
#define K_LKIND   "\x8d\x44\x82\xab\x8e\xed\x97\xde:"                  /* 好き種類: */
#define K_LATTR   "\x8d\x44\x82\xab\x91\xae\x90\xab:"                  /* 好き属性: */
#define K_DISLIKE "\x8c\x99\x82\xa2:"                                  /* 嫌い:     */
#define K_TIME    "\x8a\x88\x93\xae\x8e\x9e\x8a\xd4:"                  /* 活動時間: */
#define K_SUSP    "\x8b\x5e:"                                          /* 疑:       */
#define K_GULL    "\xe9\x78:"                                          /* 騙:       */
#define K_RISE1   "\x8f\xe3\x8f\xb8\x82\x50:"                          /* 上昇１:   */
#define K_RISE2   "\x8f\xe3\x8f\xb8\x82\x51:"                          /* 上昇２:   */
#define K_INIT    "\x8f\x89\x89\xf1:"                                  /* 初回:     */
#define K_RANDOM  "\x83\x89\x83\x93\x83\x5f\x83\x80:"                  /* ランダム: */

#define T_MORNING "\x92\xa9"  /* 朝 */
#define T_NOON    "\x92\x8b"  /* 昼 */
#define T_EVENING "\x97\x5b"  /* 夕 */
#define T_NIGHT   "\x96\xe9"  /* 夜 */

/* ── Fake resolver for 好き種類: lookups ────────────────────────────── */
static int32_t fake_kind_resolve(const char *name, void *user)
{
    (void)user;
    if (strcmp(name, "Daggers")   == 0) return 1;
    if (strcmp(name, "Swords")    == 0) return 0;
    if (strcmp(name, "Medicines") == 0) return 28;
    if (strcmp(name, "Books")     == 0) return 24;
    if (strcmp(name, "Rings")     == 0) return 21;
    return -1;
}

/* ── Tests ─────────────────────────────────────────────────────────── */

int test_tables_kyaku_empty(void)
{
    kyaku_state_t out;
    tables_parse_kyaku((const unsigned char *)"", 0, &out, NULL, NULL);
    for (int i = 0; i < KYAKU_COUNT; i++) {
        T_ASSERT_EQ_I(out.records[i].active, 0);
        T_ASSERT_EQ_I(out.records[i].like_count, 0);
        T_ASSERT_EQ_I(out.records[i].name_index, 0);
    }
    return 0;
}

int test_tables_kyaku_comments_and_blanks_skipped(void)
{
    static const unsigned char input[] =
        "/comment\r\n"
        "\r\n"
        "// not a real comment but treated like one (leading '/')\r\n"
        "\n"
        "/another\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    for (int i = 0; i < KYAKU_COUNT; i++) {
        T_ASSERT_EQ_I(out.records[i].active, 0);
    }
    return 0;
}

int test_tables_kyaku_header_singular_only(void)
{
    static const unsigned char input[] =
        "000:Recette\r\n"
        K_NAMEIDX "1\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[0].active, 1);
    T_ASSERT(strcmp(out.records[0].singular, "Recette") == 0);
    /* No '#', so joint == singular. */
    T_ASSERT(strcmp(out.records[0].joint, "Recette") == 0);
    T_ASSERT_EQ_I(out.records[0].name_index, 1);
    return 0;
}

int test_tables_kyaku_header_with_plural(void)
{
    /* 013:Woman#Women — joint write-position resets at '#' and the
     * plural overwrites joint[0..]. Singular stays at "Woman". */
    static const unsigned char input[] =
        "013:Woman#Women\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[13].active, 1);
    T_ASSERT(strcmp(out.records[13].singular, "Woman") == 0);
    T_ASSERT(strcmp(out.records[13].joint,    "Women") == 0);
    /* Other slots untouched. */
    T_ASSERT_EQ_I(out.records[0].active, 0);
    T_ASSERT_EQ_I(out.records[12].active, 0);
    T_ASSERT_EQ_I(out.records[14].active, 0);
    return 0;
}

int test_tables_kyaku_attr_x_y(void)
{
    static const unsigned char input[] =
        "001:Tear\r\n"
        K_ATTR "-3,4\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[1].active, 1);
    T_ASSERT_EQ_I(out.records[1].attr_x, -3);
    T_ASSERT_EQ_I(out.records[1].attr_y,  4);
    return 0;
}

int test_tables_kyaku_attr_empty_value_keeps_defaults(void)
{
    /* Empty 属性: line — engine skips when first byte after key is
     * EOL/NUL. attr_x/attr_y stay at 0 (memset default). */
    static const unsigned char input[] =
        "001:Tear\r\n"
        K_ATTR "\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[1].attr_x, 0);
    T_ASSERT_EQ_I(out.records[1].attr_y, 0);
    return 0;
}

int test_tables_kyaku_budget_range(void)
{
    static const unsigned char input[] =
        "002:Louie\r\n"
        K_BUDGET "1000-50000\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[2].budget_low,   1000);
    T_ASSERT_EQ_I(out.records[2].budget_high, 50000);
    return 0;
}

int test_tables_kyaku_budget_empty_no_write(void)
{
    static const unsigned char input[] =
        "000:Recette\r\n"
        K_BUDGET "\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[0].budget_low,  0);
    T_ASSERT_EQ_I(out.records[0].budget_high, 0);
    return 0;
}

int test_tables_kyaku_like_kind_resolver_hit(void)
{
    /* 好き種類:Daggers — resolver returns 1; record's like_kinds[0]
     * gets 1 and like_count increments. */
    static const unsigned char input[] =
        "003:Charme\r\n"
        K_LKIND "Daggers\r\n"
        K_LKIND "Rings\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, fake_kind_resolve, NULL);
    T_ASSERT_EQ_I(out.records[3].like_count, 2);
    T_ASSERT_EQ_I(out.records[3].like_kinds[0], 1);   /* Daggers */
    T_ASSERT_EQ_I(out.records[3].like_kinds[1], 21);  /* Rings */
    return 0;
}

int test_tables_kyaku_like_kind_null_resolver_skips(void)
{
    /* With NULL resolver, every 好き種類: line resolves to -1 and is
     * NOT appended (port treats -1 as "miss"). */
    static const unsigned char input[] =
        "003:Charme\r\n"
        K_LKIND "Daggers\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[3].like_count, 0);
    return 0;
}

int test_tables_kyaku_like_kind_cap_at_20(void)
{
    /* Engine caps at 0x14 = 20 entries; the 21st should be silently
     * dropped (port logs to stderr instead of MessageBoxA). */
    char buf[8192];
    int n = snprintf(buf, sizeof buf, "000:Cappy\r\n");
    for (int i = 0; i < 25; i++) {
        n += snprintf(buf + n, sizeof buf - n, K_LKIND "Daggers\r\n");
    }
    kyaku_state_t out;
    tables_parse_kyaku((const unsigned char *)buf, (size_t)n, &out,
                       fake_kind_resolve, NULL);
    T_ASSERT_EQ_I(out.records[0].like_count, 20);
    /* First 20 entries are all Daggers (id 1). */
    for (int i = 0; i < 20; i++) {
        T_ASSERT_EQ_I(out.records[0].like_kinds[i], 1);
    }
    return 0;
}

int test_tables_kyaku_like_attr_mask_sjis_tokens(void)
{
    /* 好き属性:武器防具 should OR in bits 0x0001 and 0x0002. */
    static const unsigned char input[] =
        "002:Louie\r\n"
        K_LATTR "\x95\xbe\x8a\xed\x96\x68\x8b\xef\r\n";  /* 武器防具 */
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_U(out.records[2].like_attr_mask, 0x0003u);
    return 0;
}

int test_tables_kyaku_dislikes_orphan_match_is_noop(void)
{
    /* The 嫌い: key matches but the engine does nothing with the body —
     * confirm that a 嫌い: line doesn't disturb any neighbouring
     * fields. */
    static const unsigned char input[] =
        "000:Recette\r\n"
        K_NAMEIDX "1\r\n"
        K_DISLIKE "anything goes here\r\n"
        K_BUDGET "100-200\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[0].active, 1);
    T_ASSERT_EQ_I(out.records[0].name_index, 1);
    T_ASSERT_EQ_I(out.records[0].budget_low,  100);
    T_ASSERT_EQ_I(out.records[0].budget_high, 200);
    return 0;
}

int test_tables_kyaku_file_path(void)
{
    static const unsigned char input[] =
        "005:Tielle\r\n"
        "file:kyaku/tiel.txt\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT(strcmp(out.records[5].file_path, "kyaku/tiel.txt") == 0);
    return 0;
}

int test_tables_kyaku_activity_time_mask_four_tokens(void)
{
    /* 朝昼夕夜 → all 4 bits set. */
    static const unsigned char input[] =
        "004:Caillou\r\n"
        K_TIME T_MORNING T_NOON T_EVENING T_NIGHT "\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_U(out.records[4].activity_time_mask,
                  KYAKU_TIME_MORNING | KYAKU_TIME_NOON |
                  KYAKU_TIME_EVENING | KYAKU_TIME_NIGHT);
    return 0;
}

int test_tables_kyaku_activity_time_partial_tokens(void)
{
    /* 夕夜 → only evening + night bits. */
    static const unsigned char input[] =
        "013:Woman#Women\r\n"
        K_TIME T_EVENING T_NIGHT "\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_U(out.records[13].activity_time_mask,
                  KYAKU_TIME_EVENING | KYAKU_TIME_NIGHT);
    return 0;
}

int test_tables_kyaku_activity_time_unknown_token_ignored(void)
{
    /* `本人` (本=0x96 0x7B, 人=0x90 0x6C) is not in the 4-token set —
     * the parser still iterates 2 chars but matches nothing. mask=0. */
    static const unsigned char input[] =
        "000:Recette\r\n"
        K_TIME "\x96\x7b\x90\x6c\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_U(out.records[0].activity_time_mask, 0u);
    return 0;
}

int test_tables_kyaku_atoi_scalars(void)
{
    static const unsigned char input[] =
        "003:Charme\r\n"
        K_SUSP   "\r\n"        /* empty -> 0 */
        K_GULL   "20\r\n"
        K_RISE1  "5\r\n"
        K_RISE2  "7\r\n"
        K_INIT   "125\r\n"
        K_RANDOM "2\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[3].suspicion,   0);
    T_ASSERT_EQ_I(out.records[3].gullibility, 20);
    T_ASSERT_EQ_I(out.records[3].rise1,        5);
    T_ASSERT_EQ_I(out.records[3].rise2,        7);
    T_ASSERT_EQ_I(out.records[3].initial,    125);
    T_ASSERT_EQ_I(out.records[3].random,       2);
    return 0;
}

int test_tables_kyaku_lines_before_header_dropped(void)
{
    /* Fields before any header have nowhere to land (engine local_14
     * == -1 → log + skip). All slots should remain inactive/zero. */
    static const unsigned char input[] =
        K_NAMEIDX "42\r\n"
        K_ATTR    "1,2\r\n"
        "000:Recette\r\n"
        K_NAMEIDX "1\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[0].active, 1);
    T_ASSERT_EQ_I(out.records[0].name_index, 1);
    T_ASSERT_EQ_I(out.records[0].attr_x, 0);
    T_ASSERT_EQ_I(out.records[0].attr_y, 0);
    return 0;
}

int test_tables_kyaku_no_trailing_newline(void)
{
    /* Last line has no \r\n — content past the last field still parses. */
    static const unsigned char input[] =
        "000:Recette\r\n"
        K_NAMEIDX "1";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[0].active, 1);
    T_ASSERT_EQ_I(out.records[0].name_index, 1);
    return 0;
}

int test_tables_kyaku_multi_customer_threading(void)
{
    /* Two customers; per-key lines land in the most recently
     * declared record (current_id threads through). */
    static const unsigned char input[] =
        "002:Louie\r\n"
        K_BUDGET "1000-50000\r\n"
        "003:Charme\r\n"
        K_BUDGET "2000-300000\r\n";
    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out, NULL, NULL);
    T_ASSERT_EQ_I(out.records[2].budget_low,   1000);
    T_ASSERT_EQ_I(out.records[2].budget_high, 50000);
    T_ASSERT_EQ_I(out.records[3].budget_low,   2000);
    T_ASSERT_EQ_I(out.records[3].budget_high, 300000);
    return 0;
}

/* Adapter mirroring tables.c's `resolve_via_item_category` — exercises
 * the actual item_state_t lookup that production wires up at boot. */
static int32_t resolve_via_item_category(const char *name, void *user)
{
    const item_state_t *state = (const item_state_t *)user;
    for (int c = 0; c < ITEM_CATEGORY_COUNT; c++) {
        if (state->categories[c].singular[0] == '\0') continue;
        size_t nlen = strlen(state->categories[c].singular);
        if (memcmp(name, state->categories[c].singular, nlen) == 0
            && (name[nlen] == '\0' || name[nlen] == '\r'
                || name[nlen] == '\n')) {
            return (int32_t)c;
        }
    }
    return -1;
}

int test_tables_kyaku_resolves_via_item_category(void)
{
    /* End-to-end resolver-wiring smoke: hand-populate a tiny
     * item_state_t with category names, then parse a 好き種類: line
     * through the same lookup tables.c uses. */
    item_state_t *state = (item_state_t *)calloc(1, sizeof *state);
    if (!state) T_FAIL("OOM allocating item_state_t");

    strcpy(state->categories[0].singular,  "Swords");
    strcpy(state->categories[1].singular,  "Daggers");
    strcpy(state->categories[28].singular, "Medicines");

    static const unsigned char input[] =
        "002:Louie\r\n"
        K_LKIND "Swords\r\n"
        K_LKIND "Daggers\r\n"
        K_LKIND "Medicines\r\n"
        K_LKIND "Nonexistent\r\n";

    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out,
                       resolve_via_item_category, state);

    T_ASSERT_EQ_I(out.records[2].like_count, 3);
    T_ASSERT_EQ_I(out.records[2].like_kinds[0],  0);
    T_ASSERT_EQ_I(out.records[2].like_kinds[1],  1);
    T_ASSERT_EQ_I(out.records[2].like_kinds[2], 28);

    free(state);
    return 0;
}

int test_tables_kyaku_vendor_shape(void)
{
    /* End-to-end vendor kyaku.txt smoke shape. Reproduces the
     * 013:Woman#Women block (singular/plural header + the full
     * field-key sweep) plus a comment line and a header-only entry.
     * Validates that the dispatcher correctly threads the "current
     * record" through interleaved lines. */
    static const unsigned char input[] =
        "/leading comment\r\n"
        "\r\n"
        "013:Woman#Women\r\n"
        K_NAMEIDX "13\r\n"
        K_ATTR    "0,4\r\n"
        K_LATTR   "\x95\xbe\x8a\xed\x96\x68\x8b\xef\r\n"   /* 武器防具 -> 0x3 */
        K_LKIND   "Medicines\r\n"
        K_LKIND   "Rings\r\n"
        K_LKIND   "Books\r\n"
        K_DISLIKE "\r\n"
        K_BUDGET  "3000-300000\r\n"
        K_TIME    T_EVENING T_NIGHT "\r\n"
        K_SUSP    "\r\n"
        K_RANDOM  "3\r\n"
        K_INIT    "120\r\n"
        K_GULL    "20\r\n"
        K_RISE1   "10\r\n"
        K_RISE2   "10\r\n"
        "file:kyaku/f3.txt\r\n"
        "\r\n"
        "014:Man#Men\r\n"
        K_NAMEIDX "14\r\n";

    kyaku_state_t out;
    tables_parse_kyaku(input, sizeof input - 1, &out,
                       fake_kind_resolve, NULL);

    /* 013:Woman#Women — singular/joint split. */
    T_ASSERT_EQ_I(out.records[13].active, 1);
    T_ASSERT(strcmp(out.records[13].singular, "Woman") == 0);
    T_ASSERT(strcmp(out.records[13].joint,    "Women") == 0);
    T_ASSERT_EQ_I(out.records[13].name_index, 13);
    T_ASSERT_EQ_I(out.records[13].attr_x, 0);
    T_ASSERT_EQ_I(out.records[13].attr_y, 4);
    T_ASSERT_EQ_U(out.records[13].like_attr_mask, 0x3u);
    T_ASSERT_EQ_I(out.records[13].like_count, 3);
    T_ASSERT_EQ_I(out.records[13].like_kinds[0], 28);  /* Medicines */
    T_ASSERT_EQ_I(out.records[13].like_kinds[1], 21);  /* Rings */
    T_ASSERT_EQ_I(out.records[13].like_kinds[2], 24);  /* Books */
    T_ASSERT_EQ_I(out.records[13].budget_low,    3000);
    T_ASSERT_EQ_I(out.records[13].budget_high, 300000);
    T_ASSERT_EQ_U(out.records[13].activity_time_mask,
                  KYAKU_TIME_EVENING | KYAKU_TIME_NIGHT);
    T_ASSERT_EQ_I(out.records[13].suspicion,    0);
    T_ASSERT_EQ_I(out.records[13].random,       3);
    T_ASSERT_EQ_I(out.records[13].initial,    120);
    T_ASSERT_EQ_I(out.records[13].gullibility, 20);
    T_ASSERT_EQ_I(out.records[13].rise1, 10);
    T_ASSERT_EQ_I(out.records[13].rise2, 10);
    T_ASSERT(strcmp(out.records[13].file_path, "kyaku/f3.txt") == 0);

    /* 014:Man#Men — header threads correctly. */
    T_ASSERT_EQ_I(out.records[14].active, 1);
    T_ASSERT(strcmp(out.records[14].singular, "Man") == 0);
    T_ASSERT(strcmp(out.records[14].joint,    "Men") == 0);
    T_ASSERT_EQ_I(out.records[14].name_index, 14);

    /* Untouched slots remain inactive. */
    T_ASSERT_EQ_I(out.records[12].active, 0);
    T_ASSERT_EQ_I(out.records[15].active, 0);

    return 0;
}
