/*
 * test_tables_item.c — unit tests for src/tables_item.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. No vendor file required for the synthetic
 * tests; one vendor-shape integration test runs against the
 * extracted `/tmp/openrecet-extract/data/item.txt` when present and
 * skips cleanly otherwise.
 */

#include "t.h"
#include "tables_item.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SJIS bytes for attribute tags and audience markers used inline. */
#define KINZOKU "\x8b\xe0\x91\xae"  /* 金属 (attr bit 0x40)      */
#define JIMI    "\x92\x6e\x96\xa1"  /* 地味 (attr bit 0x400)     */
#define ZEN     "\x91\x53"          /* 全 audience = 0xff       */
#define OTOKO   "\x92\x6a"          /* 男 audience = 0x55       */
#define RI      "\x83\x8a"          /* リ audience = 0x01       */
#define ZAIKO   "\x8d\xdd\x8c\xc9(" /* 在庫(                    */
#define DA      "\x83\x5f("         /* ダ(                      */

/* ── Tests ─────────────────────────────────────────────────────────── */

int test_tables_item_empty(void)
{
    item_state_t out;
    tables_parse_item((const unsigned char *)"", 0, &out);
    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_item_comments_and_blanks_skipped(void)
{
    static const unsigned char input[] =
        "/header comment\r\n"
        "\r\n"
        "/another comment\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_item_basic_record_no_plural(void)
{
    /* item 0 — minimal "Sword" with no '+' (singular == plural).
     * No rank digit at start of post-prefix content, so rank stays 0. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].item_id, 0);
    T_ASSERT_EQ_I(out.records[0].category, 0);
    T_ASSERT_EQ_I(out.records[0].subindex, 0);
    T_ASSERT_EQ_I(out.records[0].rank, 0);
    T_ASSERT_EQ_I(out.records[0].price, -1);
    T_ASSERT_EQ_I(out.records[0].valid, 1);
    T_ASSERT(strcmp(out.records[0].singular, "Sword") == 0);
    T_ASSERT(strcmp(out.records[0].plural, "Sword") == 0);
    return 0;
}

int test_tables_item_basic_record_with_plural(void)
{
    /* Rank-1 record with name+plural split on '+'. */
    static const unsigned char input[] =
        "0001:1#Worn Sword+Worn Swords#200#8#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].rank, 1);
    T_ASSERT_EQ_I(out.records[0].price, 200);
    T_ASSERT_EQ_I(out.records[0].attack, 8);
    T_ASSERT(strcmp(out.records[0].singular, "Worn Sword") == 0);
    T_ASSERT(strcmp(out.records[0].plural, "Worn Swords") == 0);
    return 0;
}

int test_tables_item_full_stat_fields(void)
{
    static const unsigned char input[] =
        "0007:7#Crystal Sword+Crystal Swords#28000#58#0#7#8# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].rank, 7);
    T_ASSERT_EQ_I(out.records[0].price, 28000);
    T_ASSERT_EQ_I(out.records[0].attack, 58);
    T_ASSERT_EQ_I(out.records[0].defense, 0);
    T_ASSERT_EQ_I(out.records[0].magic_attack, 7);
    T_ASSERT_EQ_I(out.records[0].magic_defense, 8);
    return 0;
}

int test_tables_item_category_header_then_record(void)
{
    /* A category header sets the per-category name. The next record's
     * category (item_id/100) gets populated. */
    static const unsigned char input[] =
        ":Swords#(Equippable)\r\n"
        "0000:Sword#-1#0#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT(strcmp(out.categories[0].singular, "Swords") == 0);
    T_ASSERT(strcmp(out.categories[0].tag, "(Equippable)") == 0);
    /* equip_class lookup: "Swords" → 1. */
    T_ASSERT_EQ_I(out.records[0].equip_class, 1);
    return 0;
}

int test_tables_item_category_threads_to_correct_index(void)
{
    /* Two category headers each routed to different item_id/100. */
    static const unsigned char input[] =
        ":Swords#(Equippable)\r\n"
        "0000:Sword#-1#0#0#0#0# # ##\r\n"
        ":Daggers#(Equippable)\r\n"
        "0100:Knife+Knives#100#5#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT(strcmp(out.categories[0].singular, "Swords") == 0);
    T_ASSERT(strcmp(out.categories[1].singular, "Daggers") == 0);
    T_ASSERT_EQ_I(out.records[0].equip_class, 1);
    T_ASSERT_EQ_I(out.records[1].equip_class, 2);
    T_ASSERT_EQ_I(out.records[1].item_id, 100);
    T_ASSERT_EQ_I(out.records[1].category, 1);
    return 0;
}

int test_tables_item_attr_mask_with_category(void)
{
    /* Category "Swords" supplies the weapon class bit (0x01). Per-line
     * attr tags add 金属 (0x40) and 地味 (0x400). Expected mask: 0x441. */
    static const unsigned char input[] =
        ":Swords#(Equippable)\r\n"
        "0002:2#Longsword+Longswords#1200#14#0#0#0# " KINZOKU JIMI " # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_U(out.records[0].attr_mask, 0x441u);
    return 0;
}

/*
 * Audience-mask tests. The record line has 10 '#' separators in
 * canonical form:
 *   name # price # atk # def # mt # mf # attr # stock # aud # # desc1 # desc2
 * Phase 0 consumes 9 separators (the last one is AUD→DESC1). The
 * `##` between stock and desc means AUD is empty between the two
 * `#`s.
 */
int test_tables_item_audience_all_via_zen(void)
{
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # #" ZEN "##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_U(out.records[0].aud_mask, 0xffu);
    return 0;
}

int test_tables_item_audience_male_composite(void)
{
    /* 男 (otoko) sets bit pattern 0x55. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # #" OTOKO "##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_U(out.records[0].aud_mask, 0x55u);
    return 0;
}

int test_tables_item_audience_recette_only(void)
{
    /* リ alone sets bit 0x01. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # #" RI "##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_U(out.records[0].aud_mask, 0x01u);
    return 0;
}

int test_tables_item_audience_empty_field_is_all(void)
{
    /* `##` between stock and desc1 means AUD is empty. Engine OR's
     * 0xff on first-char-is-'#'. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_U(out.records[0].aud_mask, 0xffu);
    return 0;
}

int test_tables_item_stock_zaiko_basic(void)
{
    /* Single 在庫(N) tag stored at stock_info[0]. Defaults still apply
     * to other slots (wholesale = 200). */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# #" ZAIKO "5) ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_U(out.records[0].stock_info[0], 5);
    T_ASSERT_EQ_U(out.records[0].stock_info[7], ITEM_STOCK_WHOLESALE_DEFAULT);
    return 0;
}

int test_tables_item_stock_da_x10_quirk(void)
{
    /* ダ(N) where N < 10: stored as N*10. ダ(11) where N ≥ 10:
     * stored as 11. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# #" DA "5) ##\r\n"
        "0001:0#Knife+Knives#10#1#0#0#0# #" DA "11) ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT_EQ_U(out.records[0].stock_info[4], 50);  /* 5 * 10 */
    T_ASSERT_EQ_U(out.records[1].stock_info[4], 11);  /* no multiplier */
    return 0;
}

int test_tables_item_indent_space_line_skipped(void)
{
    /* A line starting with ' ' is silently dropped (engine
     * DAT_005cacf4 sentinel skip). */
    static const unsigned char input[] =
        " a stray indented line\r\n"
        "0000:Sword#-1#0#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    return 0;
}

int test_tables_item_unknown_line_skipped(void)
{
    /* Non-':', non-digit, non-space, non-comment line → engine pops
     * MessageBoxA "不明な行" (unknown line). Port skips and emits a
     * stderr warning. */
    static const unsigned char input[] =
        "garbage prefix line\r\n"
        "0000:Sword#-1#0#0#0#0# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].item_id, 0);
    return 0;
}

int test_tables_item_out_of_range_id_dropped(void)
{
    /* Engine bounds: 0 <= item_id < 10000. ID >= 10000 → skip. */
    static const unsigned char input[] =
        "9999:Foo#1#0#0#0#0# # ##\r\n"   /* boundary: 9999 admitted */
        "10000:Bar#1#0#0#0#0# # ##\r\n"; /* 10000 → first byte '1' is digit,
                                          * id parsed as 10000, dropped.
                                          * But line[5] is ':'! Wait: line
                                          * is "10000:Bar..." so line[4]
                                          * is '0' and line[5] is ':'.
                                          * atoi(line) reads through ':' as
                                          * stop, so id = 10000. Dropped
                                          * by `id >= 10000` check. */
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].item_id, 9999);
    return 0;
}

int test_tables_item_no_trailing_newline(void)
{
    /* Last line without CRLF — engine reads to NUL/EOF. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # ##";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT(strcmp(out.records[0].singular, "Sword") == 0);
    return 0;
}

int test_tables_item_description_in_line1_and_line2(void)
{
    /* The `##` between stock and desc1 makes AUD empty; phase 1
     * starts AT the second `#`, then immediately transitions to
     * phase 2 — no, wait: phase 0 consumes 9 # advances ending past
     * the SECOND `#` of `##`, so phase 1 reads the first byte of
     * DESC1. desc_line1 then captures up to the DESC1→DESC2 `#`. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # ##A worn-out sword.#Better than nothing.\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT(strcmp(out.records[0].desc_line1, "A worn-out sword.") == 0);
    T_ASSERT(strcmp(out.records[0].desc_line2, "Better than nothing.") == 0);
    return 0;
}

int test_tables_item_description_slash_terminates_line2(void)
{
    /* Phase 2 breaks on '/'. We need to actually reach phase 2 — so
     * embed a `#` in the description to trigger the phase 1 → 2
     * transition first. */
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # ##desc1#desc2 body /trailing comment\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT(strcmp(out.records[0].desc_line1, "desc1") == 0);
    T_ASSERT(strcmp(out.records[0].desc_line2, "desc2 body ") == 0);
    return 0;
}

int test_tables_item_resolver_finds_singular(void)
{
    static const unsigned char input[] =
        "0000:Sword#-1#0#0#0#0# # ##\r\n"
        "0007:7#Crystal Sword+Crystal Swords#28000#58#0#7#8# # ##\r\n";
    item_state_t out;
    tables_parse_item(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(tables_item_resolve(&out, "Sword"), 0);
    T_ASSERT_EQ_I(tables_item_resolve(&out, "Crystal Sword"), 7);
    T_ASSERT_EQ_I(tables_item_resolve(&out, "Nonexistent"), -1);
    T_ASSERT_EQ_I(tables_item_resolve(&out, NULL), -1);
    T_ASSERT_EQ_I(tables_item_resolve(NULL, "Sword"), -1);
    return 0;
}

int test_tables_item_max_records_cap(void)
{
    /* Drop records past ITEM_MAX_RECORDS slot cap. Smoke check —
     * we don't need to actually generate 1000 unique IDs, the engine
     * indexes by slot not by id, so dupes work fine. */
    char *buf = (char *)malloc(64 * 1024);
    if (!buf) T_FAIL("alloc");
    size_t off = 0;
    int target = ITEM_MAX_RECORDS + 3;
    for (int i = 0; i < target; i++) {
        int id = i % 10000;
        int n = snprintf(buf + off, 64 * 1024 - off,
                         "%04d:Foo#1#0#0#0#0# # ##\r\n", id);
        if (n <= 0) { free(buf); T_FAIL("snprintf"); }
        off += (size_t)n;
        if (off >= 60 * 1024) { free(buf); T_FAIL("buf small"); }
    }

    item_state_t out;
    tables_parse_item((const unsigned char *)buf, off, &out);
    int saved_count = out.count;
    free(buf);
    T_ASSERT_EQ_I(saved_count, ITEM_MAX_RECORDS);
    return 0;
}

int test_tables_item_vendor_shape(void)
{
    /* Vendor-shape integration test: parse the extracted item.txt and
     * assert structural invariants. Skips if extraction is absent. */
    const char *path = "/tmp/openrecet-extract/data/item.txt";
    FILE *f = fopen(path, "rb");
    if (!f) {
        T_SKIP("vendor item.txt not at /tmp/openrecet-extract/");
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); T_FAIL("empty vendor file"); }
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) { fclose(f); T_FAIL("alloc"); }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf); fclose(f); T_FAIL("read short");
    }
    fclose(f);

    item_state_t *out = (item_state_t *)malloc(sizeof *out);
    if (!out) { free(buf); T_FAIL("alloc state"); }
    tables_parse_item(buf, (size_t)sz, out);

    /* Sanity: lots of items, "Swords" / "Daggers" / "Robes" / "Bows"
     * present as categories, "Sword" is item 0, "Longsword" is 0002. */
    T_ASSERT(out->count >= 500);
    T_ASSERT(out->count <= ITEM_MAX_RECORDS);
    T_ASSERT(strcmp(out->categories[0].singular, "Swords") == 0);
    T_ASSERT_EQ_I(tables_item_resolve(out, "Sword"), 0);
    T_ASSERT_EQ_I(tables_item_resolve(out, "Longsword"), 2);
    T_ASSERT_EQ_I(tables_item_resolve(out, "Crystal Sword"), 7);

    /* equip_class for Swords category should be 1. */
    T_ASSERT_EQ_I(out->records[0].equip_class, 1);

    free(out);
    free(buf);
    return 0;
}
