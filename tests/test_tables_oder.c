/*
 * test_tables_oder.c — unit tests for `data/oder.txt` parsing.
 *
 * Coverage:
 *   1. Empty input                  (zero-init everything; count = 0)
 *   2. Comments + LV + one entry    (basic dispatch + record fields)
 *   3. LV updates between entries   (pending_level threads through)
 *   4. SJIS attribute → mask        (each of the 16 4-byte tags)
 *   5. English attr → mask=0, idx=-1 (fallback path is suppressed)
 *   6. Tabs skipped within fields   (engine: cVar11 == '\t' branch)
 *   7. 100-char inner-loop cap      (excess bytes truncated)
 *   8. First-field column-pos write (in-place vs sequential)
 *   9. Vendor-shape end-to-end      (real LV groups + counts)
 */
#include "t.h"
#include "tables_oder.h"

#include <stdint.h>
#include <string.h>

static void run_parse(const char *literal, size_t size, struct oder_table *out)
{
    tables_parse_oder((const unsigned char *)literal, size, out);
}

/* Toy category resolver: "Treasures" → 7, "Rings" → 12, else -1.  Mirrors
 * resolve_via_item_category (exact singular match, \0/\r/\n terminated). */
static int32_t toy_cat_resolve(const char *name, void *user)
{
    (void)user;
    if (strncmp(name, "Treasures", 9) == 0 &&
        (name[9] == '\0' || name[9] == '\r' || name[9] == '\n')) return 7;
    if (strncmp(name, "Rings", 5) == 0 &&
        (name[5] == '\0' || name[5] == '\r' || name[5] == '\n')) return 12;
    return -1;
}

int test_tables_oder_empty(void)
{
    struct oder_table tbl;
    memset(&tbl, 0xCC, sizeof tbl);
    run_parse("", 0, &tbl);

    T_ASSERT_EQ_I(tbl.count, 0);
    return 0;
}

int test_tables_oder_one_record(void)
{
    const char input[] =
        "// comment\r\n"
        "LV:1\r\n"
        "a treasure,treasures,Treasures\r\n";

    struct oder_table tbl;
    run_parse(input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 1);
    T_ASSERT(strcmp(tbl.entries[0].name_singular, "a treasure") == 0);
    T_ASSERT(strcmp(tbl.entries[0].name_plural,   "treasures")  == 0);
    /* "Treasures" — English, no SJIS attribute match. */
    T_ASSERT_EQ_U(tbl.entries[0].attr_mask, 0);
    T_ASSERT_EQ_I(tbl.entries[0].attr_index, -1);
    T_ASSERT_EQ_I(tbl.entries[0].level_minus_1, 0);  /* LV:1 -> 1-1=0 */
    return 0;
}

int test_tables_oder_resolved_category(void)
{
    /* With a resolver injected, a category-named (mask==0) oder gets its
     * attr_index set; SJIS-attribute oders (mask!=0) stay -1; unknown
     * category names stay -1. (M2a' — load-bearing for roster_pick_item.) */
    const char input[] =
        "LV:1\r\n"
        "a treasure,treasures,Treasures\r\n"   /* mask 0 → resolve 7 */
        "a sword,swords,\x95\x90\x8a\xed\r\n"   /* 武器 → mask!=0, idx stays -1 */
        "a ring,rings,Unknowns\r\n";            /* mask 0, unknown → -1 */

    struct oder_table tbl;
    tables_parse_oder_resolved((const unsigned char *)input, sizeof input - 1,
                               &tbl, toy_cat_resolve, NULL);

    T_ASSERT_EQ_I(tbl.count, 3);
    T_ASSERT_EQ_U(tbl.entries[0].attr_mask, 0);
    T_ASSERT_EQ_I(tbl.entries[0].attr_index, 7);      /* resolved */
    T_ASSERT(tbl.entries[1].attr_mask != 0);
    T_ASSERT_EQ_I(tbl.entries[1].attr_index, -1);     /* mask!=0 → not resolved */
    T_ASSERT_EQ_U(tbl.entries[2].attr_mask, 0);
    T_ASSERT_EQ_I(tbl.entries[2].attr_index, -1);     /* unknown name → -1 */

    /* NULL resolver ⇒ the plain path leaves every attr_index at -1. */
    struct oder_table tbl2;
    tables_parse_oder_resolved((const unsigned char *)input, sizeof input - 1,
                               &tbl2, NULL, NULL);
    T_ASSERT_EQ_I(tbl2.entries[0].attr_index, -1);
    return 0;
}

int test_tables_oder_level_threads_through(void)
{
    /* The LV: value must persist across multiple data lines and
     * update when a new LV: appears. */
    const char input[] =
        "LV:1\r\n"
        "alpha,alphas,Alphas\r\n"
        "beta,betas,Betas\r\n"
        "LV:7\r\n"
        "gamma,gammas,Gammas\r\n";

    struct oder_table tbl;
    run_parse(input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 3);
    T_ASSERT_EQ_I(tbl.entries[0].level_minus_1, 0);
    T_ASSERT_EQ_I(tbl.entries[1].level_minus_1, 0);
    T_ASSERT_EQ_I(tbl.entries[2].level_minus_1, 6);  /* LV:7 -> 6 */
    return 0;
}

int test_tables_oder_sjis_attrs_all_16(void)
{
    /* Every attribute tag maps to its index bit, in declaration order. */
    static const unsigned char input[] =
        "LV:1\r\n"
        "x,y,\x95\x90\x8a\xed\r\n"   /* 武器  -> 1<<0  */
        "x,y,\x96\x68\x8b\xef\r\n"   /* 防具  -> 1<<1  */
        "x,y,\x92\xb2\x93\x78\r\n"   /* 調度  -> 1<<2  */
        "x,y,\x95\x9e\x8f\xfc\r\n"   /* 服飾  -> 1<<3  */
        "x,y,\x83\x41\x83\x4e\r\n"   /* アク  -> 1<<4  */
        "x,y,\x8b\x4d\x8b\xe0\r\n"   /* 貴金  -> 1<<5  */
        "x,y,\x8b\xe0\x91\xae\r\n"   /* 金属  -> 1<<6  */
        "x,y,\x97\x5b\x94\xd1\r\n"   /* 夕飯  -> 1<<7  */
        "x,y,\x8a\xc3\x82\xa2\r\n"   /* 甘い  -> 1<<8  */
        "x,y,\x94\x68\x8e\xe8\r\n"   /* 派手  -> 1<<9  */
        "x,y,\x92\x6e\x96\xa1\r\n"   /* 地味  -> 1<<10 */
        "x,y,\x92\xbf\x95\x69\r\n"   /* 珍品  -> 1<<11 */
        "x,y,\x96\x68\x8a\xa6\r\n"   /* 防寒  -> 1<<12 */
        "x,y,\x90\x48\x95\x69\r\n"   /* 食品  -> 1<<13 */
        "x,y,\x90\xb9\x91\xae\r\n"   /* 聖属  -> 1<<14 */
        "x,y,\x96\x82\x91\xae\r\n";  /* 魔属  -> 1<<15 */

    struct oder_table tbl;
    tables_parse_oder(input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 16);
    for (int i = 0; i < 16; i++) {
        T_ASSERT_EQ_U(tbl.entries[i].attr_mask, (uint32_t)(1u << i));
        T_ASSERT_EQ_I(tbl.entries[i].attr_index, -1);
    }
    return 0;
}

int test_tables_oder_english_attr_falls_through(void)
{
    /* "Bracelets" is English (not in the 16-tag SJIS set), so the
     * engine would linear-search the item-name table; our port
     * suppresses that and leaves attr_index = -1. */
    const char input[] =
        "LV:2\r\n"
        "a bracelet,bracelets,Bracelets\r\n";

    struct oder_table tbl;
    run_parse(input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 1);
    T_ASSERT_EQ_U(tbl.entries[0].attr_mask, 0);
    T_ASSERT_EQ_I(tbl.entries[0].attr_index, -1);
    return 0;
}

int test_tables_oder_tabs_are_skipped(void)
{
    /* Engine quirk: tabs do not advance any field position. So a
     * row with tabs between the commas should still produce a
     * clean record with no tab bytes embedded. */
    const char input[] =
        "LV:1\r\n"
        "a\tring,\trings,\x8b\xe0\x91\xae\r\n";  /* attr 金属 -> 1<<6 */

    struct oder_table tbl;
    tables_parse_oder((const unsigned char *)input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 1);
    /* "a\tring" → "aring" (tab dropped, in-place column writes leave
     * a hole at the tab's column, but the next byte 'r' lands at
     * column 2, so we get 'a' at col 0, [skipped] at col 1, 'r' at
     * col 2, etc.). */
    /* Column positions: 'a'(0), '\t'(skip), 'r'(2), 'i'(3), 'n'(4),
     * 'g'(5), ','(6 -> \0). The record's name_singular has \0 at
     * col 1 (memset) and "ring" at col 2-5, \0 at col 6. */
    T_ASSERT_EQ_I(tbl.entries[0].name_singular[0], 'a');
    T_ASSERT_EQ_I(tbl.entries[0].name_singular[1], '\0');
    T_ASSERT_EQ_I(tbl.entries[0].name_singular[2], 'r');
    T_ASSERT_EQ_I(tbl.entries[0].name_singular[5], 'g');
    T_ASSERT_EQ_I(tbl.entries[0].name_singular[6], '\0');
    /* Plural is sequential — tab simply omitted: "rings". */
    T_ASSERT(strcmp(tbl.entries[0].name_plural, "rings") == 0);
    T_ASSERT_EQ_U(tbl.entries[0].attr_mask, 1u << 6);
    return 0;
}

int test_tables_oder_line_cap_truncates(void)
{
    /* Inner loop caps at 100 chars per line (engine: local_14 ==
     * 0x64 break). Build a single record whose third field would
     * push past 100 bytes, and check we still parse the first two
     * fields cleanly. */
    char buf[256];
    int n = 0;
    n += snprintf(buf + n, sizeof buf - n, "LV:1\r\n");
    n += snprintf(buf + n, sizeof buf - n, "ab,cd,");  /* 6 bytes */
    /* Pad with 'X' to push past the 100-char cap of the data row
     * (which started at offset n - 6, so we add 200 'X' to be sure). */
    for (int i = 0; i < 200 && n < (int)sizeof buf - 4; i++) buf[n++] = 'X';
    buf[n++] = '\r'; buf[n++] = '\n';

    struct oder_table tbl;
    tables_parse_oder((const unsigned char *)buf, (size_t)n, &tbl);

    T_ASSERT_EQ_I(tbl.count, 1);
    T_ASSERT(strcmp(tbl.entries[0].name_singular, "ab") == 0);
    T_ASSERT(strcmp(tbl.entries[0].name_plural,   "cd") == 0);
    /* attr field is all-X — no SJIS match. */
    T_ASSERT_EQ_U(tbl.entries[0].attr_mask, 0);
    return 0;
}

int test_tables_oder_no_trailing_newline(void)
{
    /* Last line lacks a terminator (vendor file's actual EOF
     * doesn't have one either). The record should still parse. */
    const char input[] =
        "LV:3\r\n"
        "a hat,hats,Hats";

    struct oder_table tbl;
    run_parse(input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 1);
    T_ASSERT(strcmp(tbl.entries[0].name_singular, "a hat") == 0);
    T_ASSERT(strcmp(tbl.entries[0].name_plural,   "hats")  == 0);
    T_ASSERT_EQ_I(tbl.entries[0].level_minus_1, 2);  /* LV:3 -> 2 */
    return 0;
}

int test_tables_oder_vendor_shape(void)
{
    /* Reproduces the actual `data/oder.txt`'s LV-group structure
     * with a representative mix of SJIS-attr rows (mask != 0) and
     * English-name rows (mask == 0). Mirrors LV 1..5 groups with
     * realistic per-group counts. */
    static const unsigned char input[] =
        "//top comment\r\n"
        "\r\n"
        "LV:1\r\n"
        "a treasure,treasures,Treasures\r\n"
        "a weapon,weapons,\x95\x90\x8a\xed\r\n"      /* 武器 -> 1<<0 */
        "a piece of armor,pieces of armor,\x96\x68\x8b\xef\r\n"  /* 防具 -> 1<<1 */
        "\r\n"
        "LV:2\r\n"
        "some clothes,clothes,\x95\x9e\x8f\xfc\r\n"  /* 服飾 -> 1<<3 */
        "a ring,rings,Rings\r\n"
        "\r\n"
        "/dead comment\r\n"
        "LV:5\r\n"
        "a sword,swords,Swords\r\n";

    struct oder_table tbl;
    tables_parse_oder(input, sizeof input - 1, &tbl);

    T_ASSERT_EQ_I(tbl.count, 6);

    /* Spot-check a few entries. */
    T_ASSERT(strcmp(tbl.entries[0].name_singular, "a treasure") == 0);
    T_ASSERT(strcmp(tbl.entries[0].name_plural,   "treasures")  == 0);
    T_ASSERT_EQ_U(tbl.entries[0].attr_mask, 0);
    T_ASSERT_EQ_I(tbl.entries[0].level_minus_1, 0);

    T_ASSERT_EQ_U(tbl.entries[1].attr_mask, 1u << 0);  /* 武器 */
    T_ASSERT_EQ_I(tbl.entries[1].level_minus_1, 0);

    T_ASSERT_EQ_U(tbl.entries[2].attr_mask, 1u << 1);  /* 防具 */
    T_ASSERT_EQ_I(tbl.entries[2].level_minus_1, 0);

    T_ASSERT_EQ_U(tbl.entries[3].attr_mask, 1u << 3);  /* 服飾 */
    T_ASSERT_EQ_I(tbl.entries[3].level_minus_1, 1);

    /* Rings — English fallback. */
    T_ASSERT_EQ_U(tbl.entries[4].attr_mask, 0);
    T_ASSERT_EQ_I(tbl.entries[4].level_minus_1, 1);

    /* LV:5 group skipped LV:3,4 entirely. */
    T_ASSERT_EQ_I(tbl.entries[5].level_minus_1, 4);
    T_ASSERT(strcmp(tbl.entries[5].name_singular, "a sword") == 0);
    return 0;
}
