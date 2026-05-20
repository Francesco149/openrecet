/*
 * test_tables_gousei.c — unit tests for src/tables_gousei.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. No vendor file required — the vendor shape is
 * reproduced inline with the SJIS `ランク:` header spelt as a hex
 * escape so the source stays ASCII-clean.
 */

#include "t.h"
#include "tables_gousei.h"
#include "tables_item.h"

#include <stdlib.h>
#include <string.h>

/* SJIS bytes for the "ランク:" header prefix.
 * Decoded: \x83\x89=ラ \x83\x93=ン \x83\x4e=ク + ':'. */
#define RANK "\x83\x89\x83\x93\x83\x4e:"

/* ── Resolver fixtures ─────────────────────────────────────────────── */

/* Static name→id table for resolver-using tests. */
struct fake_item {
    const char *name;
    int32_t     id;
};

static const struct fake_item g_fake_items[] = {
    /* id 0 is intentionally a real item — exercises the "engine bug
     * with index-0 lookup" code path (which the port does NOT replicate
     * — index-0 hits are silent in our port). */
    { "Gilded Sword",   0 },
    { "Longsword",      1 },
    { "Water Crystal",  2 },
    { "Survival Knife", 3 },
    { "Crafter's Knife",4 },
    { "Fin Fan",        5 },
    { "Cloth Beater",   6 },
    { "Foo",            7 },
    { "Bar",            8 },
    { "Baz",            9 },
    { NULL,             0 },
};

static int32_t fake_resolve(const char *name, void *user)
{
    (void)user;
    for (size_t i = 0; g_fake_items[i].name != NULL; i++) {
        if (strcmp(g_fake_items[i].name, name) == 0) {
            return g_fake_items[i].id;
        }
    }
    return -1;
}

/* ── Tests ─────────────────────────────────────────────────────────── */

int test_tables_gousei_empty(void)
{
    gousei_state_t out;
    tables_parse_gousei((const unsigned char *)"", 0, &out, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 0);
    /* All records are zero-initialised — output_id, rank, all
     * ingredient slots. */
    for (int i = 0; i < GOUSEI_MAX_RECORDS; i++) {
        T_ASSERT_EQ_I(out.records[i].output_id, 0);
        T_ASSERT_EQ_I(out.records[i].rank, 0);
        for (int k = 0; k < GOUSEI_INGREDIENT_COUNT; k++) {
            T_ASSERT_EQ_I(out.records[i].ingredient_id[k], 0);
            T_ASSERT_EQ_I(out.records[i].ingredient_count[k], 0);
        }
    }
    return 0;
}

int test_tables_gousei_comments_and_blanks_skipped(void)
{
    static const unsigned char input[] =
        "/header comment\r\n"
        "\r\n"
        "/another comment\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 0);
    return 0;
}

int test_tables_gousei_basic_recipe(void)
{
    /* One simple recipe with two ingredients. No rank header — so
     * rank stays at 0. */
    static const unsigned char input[] =
        "0004:Gilded Sword:Longsword#1:Water Crystal#1:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 0);   /* Gilded Sword → 0 */
    T_ASSERT_EQ_I(out.records[0].rank, 0);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 1);  /* Longsword → 1 */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], 2);  /* Water Crystal → 2 */
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 1);
    /* ing3..5 ID slots stamped to -1 by the ing1-write quirk. */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[2], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[3], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[4], -1);
    /* ing3..5 counts stay at 0 (no #count, no pre-init). */
    T_ASSERT_EQ_I(out.records[0].ingredient_count[2], 0);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[3], 0);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[4], 0);
    return 0;
}

int test_tables_gousei_rank_header(void)
{
    /* Three recipes across two rank blocks. */
    static const unsigned char input[] =
        RANK "1\r\n"
        "0004:Foo:Bar#1:\r\n"
        "0104:Baz:Foo#2:\r\n"
        RANK "2\r\n"
        "0009:Bar:Baz#3:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 3);
    T_ASSERT_EQ_I(out.records[0].rank, 1);
    T_ASSERT_EQ_I(out.records[1].rank, 1);
    T_ASSERT_EQ_I(out.records[2].rank, 2);

    /* Verify resolver ordering. */
    T_ASSERT_EQ_I(out.records[0].output_id, 7);  /* Foo */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 8);  /* Bar */
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);

    T_ASSERT_EQ_I(out.records[1].output_id, 9);  /* Baz */
    T_ASSERT_EQ_I(out.records[1].ingredient_id[0], 7);  /* Foo */
    T_ASSERT_EQ_I(out.records[1].ingredient_count[0], 2);

    T_ASSERT_EQ_I(out.records[2].output_id, 8);  /* Bar */
    T_ASSERT_EQ_I(out.records[2].ingredient_id[0], 9);  /* Baz */
    T_ASSERT_EQ_I(out.records[2].ingredient_count[0], 3);
    return 0;
}

int test_tables_gousei_recipe_before_rank_is_rank_zero(void)
{
    /* A recipe before any "ランク:" header gets rank=0 — engine
     * `local_24` starts at 0. */
    static const unsigned char input[] =
        "0004:Foo:Bar#1:\r\n"
        RANK "1\r\n"
        "0104:Baz:Foo#2:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 2);
    T_ASSERT_EQ_I(out.records[0].rank, 0);
    T_ASSERT_EQ_I(out.records[1].rank, 1);
    return 0;
}

int test_tables_gousei_prefix_discarded(void)
{
    /* Engine skips 5 bytes regardless of content; verify "9999:" works
     * just like "0004:" and the prefix never appears in the record. */
    static const unsigned char input[] =
        "9999:Foo:Bar#1:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 7);          /* Foo */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 8);   /* Bar */
    return 0;
}

int test_tables_gousei_three_ingredients(void)
{
    /* All three ingredient slots filled. ing4 / ing5 IDs should be -1
     * (set by ing1 quirk and not overwritten). */
    static const unsigned char input[] =
        "0004:Foo:Bar#1:Baz#5:Longsword#10:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 8);   /* Bar */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], 9);   /* Baz */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[2], 1);   /* Longsword */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[3], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[4], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 5);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[2], 10);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[3], 0);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[4], 0);
    return 0;
}

int test_tables_gousei_five_ingredients(void)
{
    /* Maximum supported width — five ingredients. */
    static const unsigned char input[] =
        "0004:Foo:Bar#1:Baz#2:Longsword#3:Water Crystal#4:Fin Fan#5:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 8);   /* Bar */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], 9);   /* Baz */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[2], 1);   /* Longsword */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[3], 2);   /* Water Crystal */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[4], 5);   /* Fin Fan */
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 2);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[2], 3);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[3], 4);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[4], 5);
    return 0;
}

int test_tables_gousei_null_resolver_yields_minus_one(void)
{
    /* No resolver → all IDs are -1. ing1-quirk still stamps ing2..5
     * to -1, but ing1 itself also reads -1 (because the resolver
     * returned -1 for the lookup). */
    static const unsigned char input[] =
        "0004:Gilded Sword:Longsword#1:Water Crystal#1:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out, NULL, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 1);
    return 0;
}

int test_tables_gousei_unknown_name_resolves_to_empty(void)
{
    /* Unknown ingredient name → -1. The ing1 quirk still pre-stamps
     * ing2..5 to -1. */
    static const unsigned char input[] =
        "0004:Mystery:Phantom#1:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    return 0;
}

int test_tables_gousei_count_at_eol_no_trailing_colon(void)
{
    /* Vendor quirk: one recipe (`Master's Plate` in rank 4) ends with
     * `#1` and no trailing ':'. Engine's unbounded ':' hunt walks past
     * the line; port detects EOL inside the hunt, finalises the
     * column, and breaks. Record is still counted. */
    static const unsigned char input[] =
        "0004:Foo:Bar#1:Baz#2:Longsword#3\r\n";  /* no trailing ':' */

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 7);          /* Foo */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 8);   /* Bar */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], 9);   /* Baz */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[2], 1);   /* Longsword */
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 2);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[2], 3);
    /* ing4/ing5 stay at -1 from ing1-write quirk. */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[3], -1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[4], -1);
    return 0;
}

int test_tables_gousei_no_trailing_newline(void)
{
    /* Last line without CRLF — engine reads to NUL/EOF. */
    static const unsigned char input[] =
        "0004:Foo:Bar#1:";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 7);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 8);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    return 0;
}

int test_tables_gousei_max_records_cap(void)
{
    /* Build a string of GOUSEI_MAX_RECORDS + 5 minimal recipes and
     * verify count caps at GOUSEI_MAX_RECORDS. */
    char buf[64 * 1024];
    size_t off = 0;
    int target = GOUSEI_MAX_RECORDS + 5;
    for (int i = 0; i < target; i++) {
        int n = snprintf(buf + off, sizeof buf - off,
                         "0004:Foo:Bar#1:\r\n");
        if (n <= 0) T_FAIL("snprintf failed");
        off += (size_t)n;
        if (off >= sizeof buf - 32) T_FAIL("buffer too small");
    }

    gousei_state_t out;
    tables_parse_gousei((const unsigned char *)buf, off, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, GOUSEI_MAX_RECORDS);
    /* Spot-check that the last admitted record was parsed correctly. */
    T_ASSERT_EQ_I(out.records[GOUSEI_MAX_RECORDS - 1].output_id, 7);
    T_ASSERT_EQ_I(out.records[GOUSEI_MAX_RECORDS - 1].ingredient_id[0], 8);
    T_ASSERT_EQ_I(out.records[GOUSEI_MAX_RECORDS - 1].ingredient_count[0], 1);
    return 0;
}

int test_tables_gousei_embedded_nul_early_exit(void)
{
    /* Engine's outer loop bails on NUL inside the buffer (the line-
     * collect inner loop also stops on NUL). Confirm we treat embedded
     * NUL the same. */
    static const unsigned char input[] =
        "0004:Foo:Bar#1:\r\n"
        "\0"
        "0104:Baz:Foo#2:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 7);   /* Foo */
    return 0;
}

int test_tables_gousei_vendor_shape(void)
{
    /* Vendor-like sample: two rank blocks, mix of comments and
     * recipes. Mirrors the structure of `data/gousei.txt` head. */
    static const unsigned char input[] =
        "/header line 1\r\n"
        "/header line 2\r\n"
        "/Crystal Rod\r\n"
        RANK "1\r\n"
        "\r\n"
        "0004:Gilded Sword:Longsword#1:Water Crystal#1:\r\n"
        "0104:Survival Knife:Crafter's Knife#1:Fin Fan#1:\r\n"
        "0204:Cloth Beater:Longsword#1:Water Crystal#5:Fin Fan#1:\r\n"
        "\r\n"
        RANK "2\r\n"
        "0009:Gilded Sword:Longsword#1:Water Crystal#3:Foo#2:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        fake_resolve, NULL);

    T_ASSERT_EQ_I(out.count, 4);

    /* Recipe 0: Gilded Sword from Longsword#1 + Water Crystal#1 at rank 1. */
    T_ASSERT_EQ_I(out.records[0].rank, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 0);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], 2);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 1);

    /* Recipe 1: Survival Knife from Crafter's Knife#1 + Fin Fan#1. */
    T_ASSERT_EQ_I(out.records[1].rank, 1);
    T_ASSERT_EQ_I(out.records[1].output_id, 3);
    T_ASSERT_EQ_I(out.records[1].ingredient_id[0], 4);
    T_ASSERT_EQ_I(out.records[1].ingredient_id[1], 5);

    /* Recipe 2: Cloth Beater with 3 ingredients. */
    T_ASSERT_EQ_I(out.records[2].rank, 1);
    T_ASSERT_EQ_I(out.records[2].output_id, 6);
    T_ASSERT_EQ_I(out.records[2].ingredient_id[0], 1);
    T_ASSERT_EQ_I(out.records[2].ingredient_id[1], 2);
    T_ASSERT_EQ_I(out.records[2].ingredient_id[2], 5);
    T_ASSERT_EQ_I(out.records[2].ingredient_count[1], 5);

    /* Recipe 3: rank advanced to 2. */
    T_ASSERT_EQ_I(out.records[3].rank, 2);
    T_ASSERT_EQ_I(out.records[3].output_id, 0);
    T_ASSERT_EQ_I(out.records[3].ingredient_id[2], 7);  /* Foo */
    T_ASSERT_EQ_I(out.records[3].ingredient_count[2], 2);

    return 0;
}

/* Adapter that lets tables_item_resolve satisfy the (name, user) → id
 * callback shape. Mirrors `resolve_via_item_state` in src/tables.c —
 * keeping a copy here so the unit test isn't coupled to tables.c. */
static int32_t resolve_via_item_state(const char *name, void *user)
{
    return tables_item_resolve((const item_state_t *)user, name);
}

int test_tables_gousei_resolves_via_item_state(void)
{
    /* End-to-end resolver-wiring smoke: hand-populate a tiny
     * item_state_t (as if item.txt had parsed three records), then
     * parse a recipe through the same `tables_item_resolve` callback
     * that tables.c uses at boot. */
    item_state_t *state = (item_state_t *)calloc(1, sizeof *state);
    if (!state) T_FAIL("OOM allocating item_state_t");

    /* Three items, valid=1, distinct singular + item_id. */
    state->records[0].valid   = 1;
    state->records[0].item_id = 42;
    strcpy(state->records[0].singular, "Gilded Sword");

    state->records[1].valid   = 1;
    state->records[1].item_id = 17;
    strcpy(state->records[1].singular, "Longsword");

    state->records[2].valid   = 1;
    state->records[2].item_id = 99;
    strcpy(state->records[2].singular, "Water Crystal");

    state->count = 3;

    static const unsigned char input[] =
        "0004:Gilded Sword:Longsword#1:Water Crystal#1:\r\n";

    gousei_state_t out;
    tables_parse_gousei(input, sizeof input - 1, &out,
                        resolve_via_item_state, state);

    T_ASSERT_EQ_I(out.count, 1);
    T_ASSERT_EQ_I(out.records[0].output_id, 42);          /* Gilded Sword */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[0], 17);   /* Longsword */
    T_ASSERT_EQ_I(out.records[0].ingredient_id[1], 99);   /* Water Crystal */
    T_ASSERT_EQ_I(out.records[0].ingredient_count[0], 1);
    T_ASSERT_EQ_I(out.records[0].ingredient_count[1], 1);

    free(state);
    return 0;
}
