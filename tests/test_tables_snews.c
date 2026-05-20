/*
 * test_tables_snews.c — unit tests for src/tables_snews.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. No vendor file required — the vendor shape is
 * reproduced inline with SJIS dungeon keys spelt as hex escapes so the
 * source stays ASCII-clean.
 */

#include "t.h"
#include "tables_snews.h"

/* SJIS bytes for the six dungeon keys ダンジョン1..ダンジョン6.
 * Decoded: \x83\x5f=ダ \x83\x93=ン \x83\x57=ジ \x83\x87=ョ \x83\x93=ン
 *          \x82\x50..\x82\x55 = full-width 1..6.
 * Each key is 12 bytes (5 katakana × 2 + 1 full-width digit × 2). */
#define DUN1 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x50"
#define DUN2 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x51"
#define DUN3 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x52"
#define DUN4 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x53"
#define DUN5 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x54"
#define DUN6 "\x83\x5f\x83\x93\x83\x57\x83\x87\x83\x93\x82\x55"

int test_tables_snews_empty(void)
{
    snews_state_t out;
    tables_parse_snews((const unsigned char *)"", 0, &out);

    /* No names populated. */
    T_ASSERT_EQ_I(out.name_count, 0);
    for (int i = 0; i < SNEWS_NAME_COUNT; i++) {
        T_ASSERT_EQ_I(out.names[i].active, 0);
    }

    /* Every section in every dungeon is empty (floor_start = -1). */
    for (int d = 0; d < SNEWS_DUNGEON_SLOT_COUNT; d++) {
        for (int s = 0; s < SNEWS_SECTION_COUNT; s++) {
            T_ASSERT_EQ_I(out.sections[d][s].floor_start, -1);
            T_ASSERT_EQ_I(out.sections[d][s].floor_end,   -1);
            for (int e = 0; e < SNEWS_ENTRY_COUNT; e++) {
                T_ASSERT_EQ_I(out.sections[d][s].entries[e].id, -1);
            }
        }
    }
    return 0;
}

int test_tables_snews_name_table_basic(void)
{
    /* Three named entries. Ordering: write names BEFORE any dungeon key
     * (matches vendor file layout). */
    static const unsigned char input[] =
        "001:Hello world\r\n"
        "002:SP halved\r\n"
        "025:Last entry\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.name_count, 3);

    T_ASSERT_EQ_I(out.names[1].active, 1);
    T_ASSERT_EQ_I(strcmp(out.names[1].name, "Hello world"), 0);

    T_ASSERT_EQ_I(out.names[2].active, 1);
    T_ASSERT_EQ_I(strcmp(out.names[2].name, "SP halved"), 0);

    T_ASSERT_EQ_I(out.names[25].active, 1);
    T_ASSERT_EQ_I(strcmp(out.names[25].name, "Last entry"), 0);

    /* Unwritten slots stay inactive. */
    T_ASSERT_EQ_I(out.names[0].active, 0);
    T_ASSERT_EQ_I(out.names[3].active, 0);
    T_ASSERT_EQ_I(out.names[63].active, 0);
    return 0;
}

int test_tables_snews_comments_and_blanks_skipped(void)
{
    /* `/` comments, blank lines, and SJIS comments all get dropped. */
    static const unsigned char input[] =
        "//\xb0\xbd\xc4\xde\xc1\xb6\xb4\xa1\xab\xa6\xb6\r\n"
        "\r\n"
        "/ダンジョン1\tダンジョン番号\r\n"
        "/f:1-4\t\t\t設定するフロア番号\r\n"
        "001:Real entry\r\n"
        "\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.name_count, 1);
    T_ASSERT_EQ_I(out.names[1].active, 1);
    T_ASSERT_EQ_I(strcmp(out.names[1].name, "Real entry"), 0);
    return 0;
}

int test_tables_snews_dungeon_and_section(void)
{
    /* One dungeon, one section, two NON-and-named entries. */
    static const unsigned char input[] =
        DUN3 "\r\n"
        "f:5-9\r\n"
        "001,30\r\n"
        "002,10\r\n"
        "NON,400\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    /* Dungeon 3 → index 2. First f: in dungeon writes to OLD section
     * pointer (section [0][0] from init), then advances to section
     * [2][0]. So [0][0] gets the floor info BUT no entries; [2][0]
     * gets the entries BUT no floor info (stays at the post-init
     * sentinel -1, -1). This is the documented engine off-by-one. */
    T_ASSERT_EQ_I(out.sections[0][0].floor_start, 5);
    T_ASSERT_EQ_I(out.sections[0][0].floor_end,   9);
    T_ASSERT_EQ_I(out.sections[0][0].entries[0].id, -1);   /* no entries */

    T_ASSERT_EQ_I(out.sections[2][0].floor_start, -1);     /* no floor info */
    T_ASSERT_EQ_I(out.sections[2][0].floor_end,   -1);
    T_ASSERT_EQ_I(out.sections[2][0].entries[0].id, 1);
    T_ASSERT_EQ_I(out.sections[2][0].entries[0].weight, 30);
    T_ASSERT_EQ_I(out.sections[2][0].entries[1].id, 2);
    T_ASSERT_EQ_I(out.sections[2][0].entries[1].weight, 10);
    T_ASSERT_EQ_I(out.sections[2][0].entries[2].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[2][0].entries[2].weight, 400);
    T_ASSERT_EQ_I(out.sections[2][0].entries[3].id, -1);   /* unused */
    return 0;
}

int test_tables_snews_multiple_sections_in_dungeon(void)
{
    /* Within a single dungeon, the SECOND and later f: lines land
     * correctly: they write the floor info to the section that the
     * previous f: line advanced into. */
    static const unsigned char input[] =
        DUN2 "\r\n"
        "f:1-4\r\n"
        "NON,300\r\n"
        "f:6-9\r\n"
        "001,30\r\n"
        "NON,400\r\n"
        "f:11-14\r\n"
        "001,30\r\n"
        "NON,300\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    /* The off-by-one means f:1-4 → section [0][0] floor info (with
     * entries landing in [1][0]); f:6-9 → section [1][0] floor info
     * (with entries in [1][1]); f:11-14 → section [1][1] floor info
     * (with entries in [1][2]). */
    T_ASSERT_EQ_I(out.sections[0][0].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[0][0].floor_end,   4);

    T_ASSERT_EQ_I(out.sections[1][0].floor_start, 6);
    T_ASSERT_EQ_I(out.sections[1][0].floor_end,   9);
    T_ASSERT_EQ_I(out.sections[1][0].entries[0].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[1][0].entries[0].weight, 300);

    T_ASSERT_EQ_I(out.sections[1][1].floor_start, 11);
    T_ASSERT_EQ_I(out.sections[1][1].floor_end,   14);
    T_ASSERT_EQ_I(out.sections[1][1].entries[0].id, 1);
    T_ASSERT_EQ_I(out.sections[1][1].entries[0].weight, 30);
    T_ASSERT_EQ_I(out.sections[1][1].entries[1].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[1][1].entries[1].weight, 400);

    /* The last section of the dungeon (with entries 001,30 + NON,300)
     * lands at [1][2] but its floor info stays at the post-init
     * sentinel because no subsequent f: line wrote to it. */
    T_ASSERT_EQ_I(out.sections[1][2].floor_start, -1);
    T_ASSERT_EQ_I(out.sections[1][2].floor_end,   -1);
    T_ASSERT_EQ_I(out.sections[1][2].entries[0].id, 1);
    T_ASSERT_EQ_I(out.sections[1][2].entries[0].weight, 30);
    T_ASSERT_EQ_I(out.sections[1][2].entries[1].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[1][2].entries[1].weight, 300);
    return 0;
}

int test_tables_snews_dungeon_transition_corrupts_prev(void)
{
    /* Faithful reproduction of the dungeon-transition off-by-one:
     * dungeon 1's f:1-5 writes floor info to [0][0]; dungeon 2's f:1-4
     * then OVERWRITES [0][0]'s floor info with (1, 4) before advancing
     * to [1][0]. The entries of [0][0] are preserved. */
    static const unsigned char input[] =
        DUN1 "\r\n"
        "f:1-5\r\n"
        "NON,300\r\n"
        DUN2 "\r\n"
        "f:1-4\r\n"
        "NON,400\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    /* [0][0] = floor (1, 4) — corrupted by dungeon 2's first f:.
     * Entries from dungeon 1 are still there. */
    T_ASSERT_EQ_I(out.sections[0][0].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[0][0].floor_end,   4);   /* was 5 before corruption */
    T_ASSERT_EQ_I(out.sections[0][0].entries[0].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[0][0].entries[0].weight, 300);

    /* Dungeon 2 entries land at [1][0]; floor info stays at -1
     * (no subsequent f: wrote to [1][0]). */
    T_ASSERT_EQ_I(out.sections[1][0].floor_start, -1);
    T_ASSERT_EQ_I(out.sections[1][0].floor_end,   -1);
    T_ASSERT_EQ_I(out.sections[1][0].entries[0].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[1][0].entries[0].weight, 400);
    return 0;
}

int test_tables_snews_name_empty_value(void)
{
    /* "NNN:" with nothing after the colon. Engine writes NUL at
     * name[0] (Branch A); active still gets set to 1. */
    static const unsigned char input[] = "007:\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.name_count, 1);
    T_ASSERT_EQ_I(out.names[7].active, 1);
    T_ASSERT_EQ_I(out.names[7].name[0], '\0');
    return 0;
}

int test_tables_snews_name_overlong_truncates(void)
{
    /* Name longer than SNEWS_NAME_LEN-1: port truncates and NUL-terms;
     * the truncated content matches the first 63 chars of the input. */
    static const char long_name[] =
        "0123456789012345678901234567890123456789012345678901234567890123456789";
    char input_buf[256];
    int n = snprintf(input_buf, sizeof input_buf,
                     "012:%s\r\n", long_name);
    T_ASSERT(n > 0 && n < (int)sizeof input_buf);

    snews_state_t out;
    tables_parse_snews((const unsigned char *)input_buf, (size_t)n, &out);

    T_ASSERT_EQ_I(out.names[12].active, 1);
    /* Port cap: SNEWS_NAME_LEN-1 = 63 chars written, then NUL. */
    T_ASSERT_EQ_I((int)strlen(out.names[12].name), SNEWS_NAME_LEN - 1);
    T_ASSERT_EQ_I(memcmp(out.names[12].name, long_name, SNEWS_NAME_LEN - 1), 0);
    /* Next entry's active flag must NOT have been clobbered (port
     * divergence from the engine's 1-byte overrun bug). */
    T_ASSERT_EQ_I(out.names[13].active, 0);
    return 0;
}

int test_tables_snews_entry_slot_overflow_dropped(void)
{
    /* Engine's local_18 keeps incrementing past 20 and writes OOB;
     * port silently drops entries past slot 19. */
    char buf[8192];
    size_t off = 0;
    off += (size_t)snprintf(buf + off, sizeof buf - off,
                            DUN1 "\r\nf:1-10\r\n");
    /* 25 entries → 5 past the 20-slot cap. */
    for (int i = 1; i <= 25; i++) {
        off += (size_t)snprintf(buf + off, sizeof buf - off,
                                "%03d,%d\r\n", i, i * 10);
    }

    snews_state_t out;
    tables_parse_snews((const unsigned char *)buf, off, &out);

    /* All 20 slots written, none corrupted past the cap. */
    for (int k = 0; k < SNEWS_ENTRY_COUNT; k++) {
        T_ASSERT_EQ_I(out.sections[0][0].entries[k].id,     k + 1);
        T_ASSERT_EQ_I(out.sections[0][0].entries[k].weight, (k + 1) * 10);
    }
    return 0;
}

int test_tables_snews_vendor_shape(void)
{
    /* Reproduces the vendor file's overall shape: 25 named entries
     * (IDs 001..025) plus all six dungeons with their f: + entry
     * lines. Names are abbreviated; counts and weights match the real
     * file so the engine off-by-one behavior is observable. */
    static const unsigned char input[] =
        "001:SP consumption halved!\r\n"
        "002:EXP gain has doubled!\r\n"
        "003:Attack power doubled for everyone!\r\n"
        "004:Enemy attack power doubled!\r\n"
        "005:Adventurer attack power doubled!\r\n"
        "006:Defense power doubled for everyone!\r\n"
        "007:Enemy defense power doubled!\r\n"
        "008:Adventurer defense power doubled!\r\n"
        "009:The floor is now slick!\r\n"
        "010:Enemy movement speed increased!\r\n"
        "011:Adventurer movement speed increased!\r\n"
        "012:Movement speed increased for everyone!\r\n"
        "013:Consumables now twice as effective!\r\n"
        "014:Consumables now half as effective!\r\n"
        "015:If you see this message, summat is broke.\r\n"
        "016:Adventurers now nearly blind!\r\n"
        "017:This level is filled with poison gas. Enjoy!\r\n"
        "018:Strong winds are blowing!\r\n"
        "019:The map of this level has been revealed!\r\n"
        "020:A powerful foe has spawned!\r\n"
        "021:This level is full of traps. Enjoy!\r\n"
        "022:Enemies here are invisible. Enjoy!\r\n"
        "023:HP now restored based on damage done!\r\n"
        "024:The automapper will not work here. Enjoy!\r\n"
        "025:Will-o'-Wisps will appear soon. Enjoy!\r\n"
        "\r\n"
        DUN1 "\r\n"
        "f:1-5\r\n"
        "NON,300\r\n"
        "\r\n"
        DUN2 "\r\n"
        "f:1-4\r\n"
        "NON,300\r\n"
        "f:6-9\r\n"
        "001,30\r\n"
        "002,10\r\n"
        "NON,400\r\n"
        "f:11-14\r\n"
        "001,30\r\n"
        "002,10\r\n"
        "004,5\r\n"
        "005,10\r\n"
        "007,5\r\n"
        "NON,300\r\n"
        "\r\n"
        DUN3 "\r\n"
        "f:1-10\r\n"
        "NON,200\r\n"
        "f:11-20\r\n"
        "001,10\r\n"
        "NON,150\r\n"
        "f:21-30\r\n"
        "001,10\r\n"
        "NON,100\r\n"
        "\r\n"
        DUN4 "\r\n"
        "f:1-30\r\n"
        "001,10\r\n"
        "NON,50\r\n"
        "f:31-60\r\n"
        "001,10\r\n"
        "NON,50\r\n"
        "\r\n"
        DUN5 "\r\n"
        "f:1-100\r\n"
        "001,10\r\n"
        "NON,20\r\n"
        "\r\n"
        DUN6 "\r\n"
        "f:1-30\r\n"
        "004,20\r\n"
        "NON,0\r\n";

    snews_state_t out;
    tables_parse_snews(input, sizeof input - 1, &out);

    /* 25 names populated, ID 001..025. */
    T_ASSERT_EQ_I(out.name_count, 25);
    T_ASSERT_EQ_I(out.names[1].active, 1);
    T_ASSERT_EQ_I(strcmp(out.names[1].name, "SP consumption halved!"), 0);
    T_ASSERT_EQ_I(out.names[25].active, 1);
    T_ASSERT_EQ_I(strcmp(out.names[25].name, "Will-o'-Wisps will appear soon. Enjoy!"),
                  0);
    T_ASSERT_EQ_I(out.names[26].active, 0);   /* unused */

    /* Per the documented engine off-by-one: every f: line writes its
     * floor info to the section that the PREVIOUS f: line advanced
     * into. So the floor-range info follows a shifted layout: */
    /* Dungeon 1 has 1 f: line (f:1-5). It writes to [0][0] floor info
     * (and stays at [0][0] for entries). Dungeon 2's first f:1-4 then
     * overwrites [0][0] floor info with (1, 4). */
    T_ASSERT_EQ_I(out.sections[0][0].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[0][0].floor_end,   4);
    T_ASSERT_EQ_I(out.sections[0][0].entries[0].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[0][0].entries[0].weight, 300);

    /* Dungeon 2: f:1-4 writes to [0][0] (overwrites), advances to
     * [1][0]; f:6-9 writes to [1][0], advances to [1][1]; f:11-14
     * writes to [1][1], advances to [1][2]. */
    T_ASSERT_EQ_I(out.sections[1][0].floor_start, 6);
    T_ASSERT_EQ_I(out.sections[1][0].floor_end,   9);
    T_ASSERT_EQ_I(out.sections[1][1].floor_start, 11);
    T_ASSERT_EQ_I(out.sections[1][1].floor_end,   14);

    /* Dungeon 3 has 3 f: lines (1-10, 11-20, 21-30). Its first f:1-10
     * overwrites dungeon 2's last section [1][2] floor info. */
    T_ASSERT_EQ_I(out.sections[1][2].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[1][2].floor_end,   10);

    T_ASSERT_EQ_I(out.sections[2][0].floor_start, 11);
    T_ASSERT_EQ_I(out.sections[2][0].floor_end,   20);
    T_ASSERT_EQ_I(out.sections[2][1].floor_start, 21);
    T_ASSERT_EQ_I(out.sections[2][1].floor_end,   30);

    /* Dungeon 4 has 2 f: lines (1-30, 31-60). First overwrites [2][2]. */
    T_ASSERT_EQ_I(out.sections[2][2].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[2][2].floor_end,   30);
    T_ASSERT_EQ_I(out.sections[3][0].floor_start, 31);
    T_ASSERT_EQ_I(out.sections[3][0].floor_end,   60);

    /* Dungeon 5 has 1 f: line (1-100). Overwrites dungeon 4's last
     * section [3][1] floor info. Dungeon 5's lone f: line advances
     * section_idx to [4][0], where its entries (001,10 + NON,20) land. */
    T_ASSERT_EQ_I(out.sections[3][1].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[3][1].floor_end,   100);

    /* Dungeon 6's f:1-30 overwrites [4][0]'s floor info (from -1, -1
     * to 1, 30) then advances to [5][0]. So [4][0] has dungeon 5's
     * entries + dungeon 6's floor info — a perfect example of the
     * dungeon-transition off-by-one in action. */
    T_ASSERT_EQ_I(out.sections[4][0].floor_start, 1);
    T_ASSERT_EQ_I(out.sections[4][0].floor_end,   30);
    T_ASSERT_EQ_I(out.sections[4][0].entries[0].id, 1);    /* from dungeon 5 */
    T_ASSERT_EQ_I(out.sections[4][0].entries[0].weight, 10);
    T_ASSERT_EQ_I(out.sections[4][0].entries[1].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[4][0].entries[1].weight, 20);

    /* Dungeon 6's entries land at [5][0]; floor info stays at the
     * post-init sentinel because nothing writes after it (file ends). */
    T_ASSERT_EQ_I(out.sections[5][0].floor_start, -1);
    T_ASSERT_EQ_I(out.sections[5][0].floor_end,   -1);
    T_ASSERT_EQ_I(out.sections[5][0].entries[0].id, 4);
    T_ASSERT_EQ_I(out.sections[5][0].entries[0].weight, 20);
    T_ASSERT_EQ_I(out.sections[5][0].entries[1].id, SNEWS_NON_ID);
    T_ASSERT_EQ_I(out.sections[5][0].entries[1].weight, 0);
    T_ASSERT_EQ_I(out.sections[5][0].entries[2].id, -1);
    return 0;
}
