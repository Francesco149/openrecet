/*
 * test_tables_enemylist.c — unit tests for src/tables_enemylist.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. SJIS bytes are embedded as hex-escaped string
 * literals so this file stays ASCII-clean.
 */

#include "t.h"
#include "tables_enemylist.h"
#include "tables_enemy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SJIS shorthands. */
#define D_HEADER1 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x50" /* ダンジョン１ */
#define D_HEADER2 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x51" /* ダンジョン２ */
#define D_HEADER6 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x55" /* ダンジョン６ */

#define E_SLIME_GREEN  "\x83\x58\x83\x89\x83\x43\x83\x80\x97\xce"             /* スライム緑 */
#define E_SLIME_RED    "\x83\x58\x83\x89\x83\x43\x83\x80\x90\xd4"             /* スライム赤 */
#define E_ARRIMAN      "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93"             /* アーリマン */
#define E_ARRIMAN_GRN  "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93\x97\xce"     /* アーリマン緑 */
#define E_KOBOLD       "\x83\x52\x83\x7b\x83\x8b\x83\x68"                     /* コボルド */

/* Stub item resolver. Maps a tiny in-test name→id table. */
typedef struct { const char *name; int32_t id; } stub_item_t;

static int32_t stub_item_resolve(const char *name, void *user)
{
    const stub_item_t *items = (const stub_item_t *)user;
    for (int i = 0; items[i].name != NULL; i++) {
        if (strcmp(items[i].name, name) == 0) return items[i].id;
    }
    return -1;
}

static const stub_item_t k_stub_items[] = {
    {"Slime Fluid",          100},
    {"Worn Sword",           101},
    {"Natural Heater",       102},
    {"Chestnut",             103},
    {"Longsword",            104},
    {"Bat Wing",             105},
    {"Fin Fan",              106},
    {NULL, 0}
};

/* ─────────────────────────────────────────────────────────────────── */

int test_tables_enemylist_layout_byte_offsets(void)
{
    /* Section stride must be 752 bytes; enemy slot 12; drop slot 12.
     * Total dwords: 2 + 31×3 + 31×3 = 188 → 752 bytes. */
    T_ASSERT_EQ_U(sizeof(enemylist_enemy_t),   0x0c);
    T_ASSERT_EQ_U(sizeof(enemylist_drops_t),   0x0c);
    T_ASSERT_EQ_U(sizeof(enemylist_section_t), 0x2f0);

    T_ASSERT_EQ_U(offsetof(enemylist_section_t, floor_lo), 0x000);
    T_ASSERT_EQ_U(offsetof(enemylist_section_t, floor_hi), 0x004);
    T_ASSERT_EQ_U(offsetof(enemylist_section_t, enemies), 0x008);
    T_ASSERT_EQ_U(offsetof(enemylist_section_t, drops),   0x17c);

    /* Per-enemy: enemy_id at +0, variant +4, count +8 within the slot. */
    T_ASSERT_EQ_U(offsetof(enemylist_enemy_t, enemy_id), 0x00);
    T_ASSERT_EQ_U(offsetof(enemylist_enemy_t, variant),  0x04);
    T_ASSERT_EQ_U(offsetof(enemylist_enemy_t, count),    0x08);
    return 0;
}

int test_tables_enemylist_empty(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    tables_parse_enemylist((const unsigned char *)"", 0, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    /* All sections have floor_lo = -1 and every enemy slot -1. */
    T_ASSERT_EQ_I(out.sections[0][0].floor_lo, -1);
    T_ASSERT_EQ_I(out.sections[0][0].enemies[0].enemy_id, -1);
    T_ASSERT_EQ_I(out.sections[5][59].floor_lo, -1);
    T_ASSERT_EQ_I(out.sections[9][30].enemies[15].enemy_id, -1);

    /* All wisps -1. */
    for (int w = 0; w < ENEMYLIST_WISP_SLOTS; w++) {
        T_ASSERT_EQ_I(out.wisp_drops[w], -1);
    }

    for (int d = 0; d < ENEMYLIST_DUNGEON_SLOTS; d++) {
        T_ASSERT_EQ_I(out.section_counts[d], 0);
    }
    return 0;
}

int test_tables_enemylist_comments_and_blanks_skipped(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        "// a comment\r\n"
        "\r\n"
        "\n"
        "/leading slash also comment\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    /* No state changes. */
    T_ASSERT_EQ_I(out.sections[0][0].floor_lo, -1);
    T_ASSERT_EQ_I(out.wisp_drops[0], -1);
    return 0;
}

int test_tables_enemylist_wisp_basic(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        "wisp1:Natural Heater\r\n"
        "wisp3:Slime Fluid\r\n"
        "wisp6:Worn Sword\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    T_ASSERT_EQ_I(out.wisp_drops[0], 102);  /* Natural Heater */
    T_ASSERT_EQ_I(out.wisp_drops[1], -1);
    T_ASSERT_EQ_I(out.wisp_drops[2], 100);  /* Slime Fluid */
    T_ASSERT_EQ_I(out.wisp_drops[5], 101);  /* Worn Sword */
    return 0;
}

int test_tables_enemylist_wisp_empty_value(void)
{
    /* `wisp1:` with no item name → skip the lookup, slot stays at -1. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] = "wisp1:\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    T_ASSERT_EQ_I(out.wisp_drops[0], -1);
    return 0;
}

int test_tables_enemylist_wisp10_silent_drop(void)
{
    /* Engine quirk #32: `wisp10:` reads the item name from line[6]
     * which is `:`, so the lookup never runs and slot 9 stays at -1. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] = "wisp10:Natural Heater\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    /* Slot 9 stays at -1 — name copy hit the ':' immediately. */
    T_ASSERT_EQ_I(out.wisp_drops[9], -1);
    return 0;
}

int test_tables_enemylist_wisp_unknown_resolves_minus_one(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] = "wisp2:NotARealItem\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    T_ASSERT_EQ_I(out.wisp_drops[1], -1);
    return 0;
}

int test_tables_enemylist_dungeon_header_resets_section(void)
{
    /* dungeon header must reset section_idx to 0, AND moving to a new
     * dungeon writes f-lines into THAT dungeon's slots. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        D_HEADER2 "\r\n"
        "f:5-7\r\n"
        D_HEADER6 "\r\n"
        "f:30\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    /* Dungeon 0, section 0: floors 0..0 (1-1 stored as N-1). */
    T_ASSERT_EQ_I(out.sections[0][0].floor_lo, 0);
    T_ASSERT_EQ_I(out.sections[0][0].floor_hi, 0);
    T_ASSERT_EQ_I(out.section_counts[0], 1);

    /* Dungeon 1, section 0: floors 4..6. */
    T_ASSERT_EQ_I(out.sections[1][0].floor_lo, 4);
    T_ASSERT_EQ_I(out.sections[1][0].floor_hi, 6);
    T_ASSERT_EQ_I(out.section_counts[1], 1);

    /* Dungeon 5, section 0: floor 29 (single floor; hi=lo). */
    T_ASSERT_EQ_I(out.sections[5][0].floor_lo, 29);
    T_ASSERT_EQ_I(out.sections[5][0].floor_hi, 29);
    T_ASSERT_EQ_I(out.section_counts[5], 1);

    /* Dungeons 2, 3, 4 untouched. */
    T_ASSERT_EQ_I(out.sections[2][0].floor_lo, -1);
    T_ASSERT_EQ_I(out.sections[3][0].floor_lo, -1);
    T_ASSERT_EQ_I(out.sections[4][0].floor_lo, -1);
    return 0;
}

int test_tables_enemylist_f_no_dash_single_floor(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:5\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    T_ASSERT_EQ_I(out.sections[0][0].floor_lo, 4);
    T_ASSERT_EQ_I(out.sections[0][0].floor_hi, 4);
    return 0;
}

int test_tables_enemylist_f_empty_skips(void)
{
    /* `f:` with no digits → engine prints "loop err 16" and skips.
     * Port advances neither section_idx nor enemy_slot. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:\r\n"
        "f:5-10\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    /* Engine writes floor_lo=-1 (atoi("")-1) on the empty f: line —
     * the port mirrors that.  Then the legitimate f:5-10 follows in
     * section[0][0] (the empty f: did NOT advance section_idx, but
     * it DID write into section[0][0]'s floor_lo). The 5-10 write
     * lands on section[0][0] too, overwriting the partial state. */
    T_ASSERT_EQ_I(out.sections[0][0].floor_lo, 4);
    T_ASSERT_EQ_I(out.sections[0][0].floor_hi, 9);
    T_ASSERT_EQ_I(out.section_counts[0], 1);
    return 0;
}

int test_tables_enemylist_multiple_f_lines_thread(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        "f:2-3\r\n"
        "f:4-4\r\n"
        "f:7-12\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    T_ASSERT_EQ_I(out.sections[0][0].floor_lo, 0);
    T_ASSERT_EQ_I(out.sections[0][0].floor_hi, 0);
    T_ASSERT_EQ_I(out.sections[0][1].floor_lo, 1);
    T_ASSERT_EQ_I(out.sections[0][1].floor_hi, 2);
    T_ASSERT_EQ_I(out.sections[0][2].floor_lo, 3);
    T_ASSERT_EQ_I(out.sections[0][2].floor_hi, 3);
    T_ASSERT_EQ_I(out.sections[0][3].floor_lo, 6);
    T_ASSERT_EQ_I(out.sections[0][3].floor_hi, 11);
    T_ASSERT_EQ_I(out.section_counts[0], 4);
    return 0;
}

int test_tables_enemylist_enemy_basic_one_drop(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_SLIME_GREEN "\t:Slime Fluid\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    /* Record 0 is スライム緑 in the pre-baked table. */
    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->enemies[0].variant, 0);
    T_ASSERT_EQ_I(sec->enemies[0].count,   1);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], 100);  /* Slime Fluid */
    T_ASSERT_EQ_I(sec->drops[0].item_id[1], -1);
    T_ASSERT_EQ_I(sec->drops[0].item_id[2], -1);

    /* Next slot still -1 (terminator). */
    T_ASSERT_EQ_I(sec->enemies[1].enemy_id, -1);
    return 0;
}

int test_tables_enemylist_enemy_multi_drops(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:3-3\r\n"
        E_SLIME_GREEN "\t:Slime Fluid#Worn Sword\r\n"
        E_KOBOLD "\t:Chestnut#Longsword#Slime Fluid\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    enemylist_section_t *sec = &out.sections[0][0];
    /* Slot 0 = スライム緑. */
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], 100);
    T_ASSERT_EQ_I(sec->drops[0].item_id[1], 101);
    T_ASSERT_EQ_I(sec->drops[0].item_id[2], -1);
    /* Slot 1 = コボルド (record 16). */
    T_ASSERT_EQ_I(sec->enemies[1].enemy_id, 16);
    T_ASSERT_EQ_I(sec->drops[1].item_id[0], 103);  /* Chestnut */
    T_ASSERT_EQ_I(sec->drops[1].item_id[1], 104);  /* Longsword */
    T_ASSERT_EQ_I(sec->drops[1].item_id[2], 100);  /* Slime Fluid */
    /* Slot 2 still empty. */
    T_ASSERT_EQ_I(sec->enemies[2].enemy_id, -1);
    return 0;
}

int test_tables_enemylist_variant_suffix(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_KOBOLD "(1)\t:Chestnut#Longsword\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 16);    /* コボルド */
    T_ASSERT_EQ_I(sec->enemies[0].variant, 1);
    T_ASSERT_EQ_I(sec->enemies[0].count,   1);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], 103);
    return 0;
}

int test_tables_enemylist_count_suffix(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_SLIME_GREEN "x3\t:Slime Fluid\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->enemies[0].count, 3);
    T_ASSERT_EQ_I(sec->enemies[0].variant, 0);
    return 0;
}

int test_tables_enemylist_longest_prefix_wins(void)
{
    /* "アーリマン緑" (12 bytes) vs "アーリマン" (10 bytes) — longer
     * record name wins. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_ARRIMAN_GRN "\t:Bat Wing\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    /* Record 7 = アーリマン緑 in the pre-baked table. */
    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 7);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], 105);  /* Bat Wing */
    return 0;
}

int test_tables_enemylist_unknown_enemy_skipped(void)
{
    /* A line that doesn't match any pre-baked enemy name → silently
     * dropped (and stderr log). Subsequent slots are unaffected. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        "Zorblax\t:NotAnItem\r\n"
        E_SLIME_GREEN "\t:Slime Fluid\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    enemylist_section_t *sec = &out.sections[0][0];
    /* スライム緑 lands in slot 0 (unknown line consumed nothing). */
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], 100);
    T_ASSERT_EQ_I(sec->enemies[1].enemy_id, -1);
    return 0;
}

int test_tables_enemylist_drop_reset_per_line(void)
{
    /* A line with NO drops on an enemy slot must still leave drops
     * at -1 (per-line reset). Not what a vendor line looks like, but
     * the engine init writes -1 to drop slots at line start. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_SLIME_GREEN "\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], -1);
    T_ASSERT_EQ_I(sec->drops[0].item_id[1], -1);
    T_ASSERT_EQ_I(sec->drops[0].item_id[2], -1);
    return 0;
}

int test_tables_enemylist_no_resolver_drops_minus_one(void)
{
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_SLIME_GREEN "\t:Slime Fluid#Worn Sword\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], -1);
    T_ASSERT_EQ_I(sec->drops[0].item_id[1], -1);
    return 0;
}

int test_tables_enemylist_no_trailing_newline(void)
{
    /* Final line without CRLF still parses. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_SLIME_GREEN "\t:Slime Fluid";  /* no \r\n */

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    enemylist_section_t *sec = &out.sections[0][0];
    T_ASSERT_EQ_I(sec->enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(sec->drops[0].item_id[0], 100);
    return 0;
}

int test_tables_enemylist_enemies_thread_across_f_blocks(void)
{
    /* f:1-1 with two enemies, then f:2-2 with one enemy. enemy slot
     * counter resets on each f-line. */
    enemylist_state_t out;
    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    static const unsigned char input[] =
        D_HEADER1 "\r\n"
        "f:1-1\r\n"
        E_SLIME_GREEN "\t:Slime Fluid\r\n"
        E_SLIME_RED   "\t:Slime Fluid\r\n"
        "f:2-2\r\n"
        E_KOBOLD      "\t:Chestnut\r\n";

    tables_parse_enemylist(input, sizeof input - 1, &out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_stub_items);

    /* Section 0: 2 enemies in slots 0/1. */
    T_ASSERT_EQ_I(out.sections[0][0].enemies[0].enemy_id, 0);
    T_ASSERT_EQ_I(out.sections[0][0].enemies[1].enemy_id, 1);
    T_ASSERT_EQ_I(out.sections[0][0].enemies[2].enemy_id, -1);

    /* Section 1: 1 enemy in slot 0 (コボルド = record 16). */
    T_ASSERT_EQ_I(out.sections[0][1].enemies[0].enemy_id, 16);
    T_ASSERT_EQ_I(out.sections[0][1].enemies[1].enemy_id, -1);
    return 0;
}

int test_tables_enemylist_vendor_shape(void)
{
    /* Vendor-shape integration test: parse the extracted enemylist.txt
     * and assert structural invariants. Skips if extraction is absent. */
    const char *path = "/tmp/openrecet-extract/data/enemylist.txt";
    FILE *f = fopen(path, "rb");
    if (!f) {
        T_SKIP("vendor enemylist.txt not at /tmp/openrecet-extract/");
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

    enemy_record_t enemies[ENEMY_COUNT];
    tables_enemy_init(enemies);

    enemylist_state_t *out =
        (enemylist_state_t *)malloc(sizeof *out);
    if (!out) { free(buf); T_FAIL("alloc state"); }
    tables_parse_enemylist(buf, (size_t)sz, out,
                           enemies, ENEMY_COUNT, NULL, NULL);

    /* All 6 keyed dungeons should have at least one f-block. */
    int total_sections = 0;
    for (int d = 0; d < ENEMYLIST_DUNGEON_KEYS; d++) {
        T_ASSERT(out->section_counts[d] > 0);
        total_sections += out->section_counts[d];
    }
    /* Vendor file has 100 `f:` lines total. */
    T_ASSERT_EQ_I(total_sections, 100);

    /* Slots 6..9 still untouched (engine quirk #31). */
    for (int d = ENEMYLIST_DUNGEON_KEYS; d < ENEMYLIST_DUNGEON_SLOTS; d++) {
        T_ASSERT_EQ_I(out->section_counts[d], 0);
        T_ASSERT_EQ_I(out->sections[d][0].floor_lo, -1);
    }

    /* Wisp slots 0..5 populated, 6..9 still -1. */
    for (int w = 0; w < 6; w++) {
        /* No resolver, so the slot's a true "did we copy the name"
         * proxy only via an item resolve... we passed NULL resolver so
         * value stays -1 even when copied. Cross-check with a stub
         * below. */
        (void)out->wisp_drops[w];
    }
    for (int w = 6; w < ENEMYLIST_WISP_SLOTS; w++) {
        T_ASSERT_EQ_I(out->wisp_drops[w], -1);
    }

    /* Total enemy entries across the vendor file. Just sanity-bound. */
    int total_enemies = 0;
    for (int d = 0; d < ENEMYLIST_DUNGEON_KEYS; d++) {
        for (int s = 0; s < out->section_counts[d]; s++) {
            const enemylist_section_t *sec = &out->sections[d][s];
            for (int k = 0; k < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; k++) {
                if (sec->enemies[k].enemy_id >= 0) total_enemies++;
            }
        }
    }
    T_ASSERT(total_enemies > 100);   /* lots of enemies in vendor file */
    T_ASSERT(total_enemies < 1000);  /* but not absurdly so */

    /* Re-parse with a stub resolver and confirm wisp slot 0 lands on
     * a non-(-1) value (wisp1 in vendor maps to nothing, so try the
     * wisp3:Natural Heater line — slot 2 maps to Natural Heater). */
    static const stub_item_t k_min[] = {
        {"Natural Heater",   42},
        {"Salamander Scale", 43},
        {NULL, 0}
    };
    tables_parse_enemylist(buf, (size_t)sz, out,
                           enemies, ENEMY_COUNT,
                           stub_item_resolve, (void *)k_min);
    /* Vendor file: wisp1: (empty), wisp2: (empty), wisp3:Natural Heater,
     * wisp4:Natural Heater, wisp5:Salamander Scale, wisp6:Salamander Scale. */
    T_ASSERT_EQ_I(out->wisp_drops[0], -1);
    T_ASSERT_EQ_I(out->wisp_drops[1], -1);
    T_ASSERT_EQ_I(out->wisp_drops[2], 42);
    T_ASSERT_EQ_I(out->wisp_drops[3], 42);
    T_ASSERT_EQ_I(out->wisp_drops[4], 43);
    T_ASSERT_EQ_I(out->wisp_drops[5], 43);

    free(out);
    free(buf);
    return 0;
}
