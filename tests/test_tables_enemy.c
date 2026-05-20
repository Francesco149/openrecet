/*
 * test_tables_enemy.c — unit tests for src/tables_enemy.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. SJIS names are embedded as hex-escaped string
 * literals so this file stays ASCII-clean.
 */

#include "t.h"
#include "tables_enemy.h"

#include <string.h>

/* SJIS bytes for a handful of vendor enemy names, used across tests. */
#define E_SLIME_GREEN  "\x83\x58\x83\x89\x83\x43\x83\x80\x97\xce"             /* スライム緑 */
#define E_SLIME_RED    "\x83\x58\x83\x89\x83\x43\x83\x80\x90\xd4"             /* スライム赤 */
#define E_ARRIMAN      "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93"             /* アーリマン */
#define E_ARRIMAN_GRN  "\x83\x41\x81\x5b\x83\x8a\x83\x7d\x83\x93\x97\xce"     /* アーリマン緑 */
#define E_USAGI        "\x93\x65"                                             /* 兎 */
#define E_NEZUMI_BARU  "\x82\xcb\x82\xb8\x82\xdd\x83\x6f\x81\x5b\x83\x8b"     /* ねずみバール */

/* ─────────────────────────────────────────────────────────────────── */

int test_tables_enemy_init_pre_baked_names(void)
{
    /* tables_enemy_init must populate the canonical 64 names + boss
     * flags. We spot-check a few records against the .data layout. */
    enemy_record_t recs[ENEMY_COUNT];
    memset(recs, 0xCC, sizeof recs);
    tables_enemy_init(recs);

    /* Record 0: スライム緑, normal. */
    T_ASSERT_EQ_I(memcmp(recs[0].name, E_SLIME_GREEN, 10), 0);
    T_ASSERT_EQ_I(recs[0].name[10], '\0');
    T_ASSERT_EQ_I(recs[0].flags, 0);

    /* Record 24: ねずみバール (boss). */
    T_ASSERT_EQ_I(memcmp(recs[24].name, E_NEZUMI_BARU, 12), 0);
    T_ASSERT_EQ_I(recs[24].flags, 1);

    /* Record 29: " " placeholder. */
    T_ASSERT_EQ_I(recs[29].name[0], ' ');
    T_ASSERT_EQ_I(recs[29].name[1], '\0');
    T_ASSERT_EQ_I(recs[29].flags, 0);

    /* Record 63: ゴーレム左 (last boss-class). */
    T_ASSERT_EQ_I(memcmp(recs[63].name, "\x83\x53\x81\x5b\x83\x8c\x83\x80\x8d\xb6", 10), 0);
    T_ASSERT_EQ_I(recs[63].flags, 1);

    /* Stats start at zero before any parse. */
    T_ASSERT_EQ_I(recs[0].hp, 0);
    T_ASSERT_EQ_I(recs[0].drop_common, 0);  /* not yet set to -1 */
    return 0;
}

int test_tables_enemy_basic_record(void)
{
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    /* Vendor's first data line, byte-for-byte. */
    static const unsigned char input[] =
        E_SLIME_GREEN "     :15# 1# 25# 4# 0# 10# Slime Fluid#Worn Sword\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    T_ASSERT_EQ_I(recs[0].hp,          15);
    T_ASSERT_EQ_I(recs[0].exp_reward,   1);
    T_ASSERT_EQ_I(recs[0].at,          25);
    T_ASSERT_EQ_I(recs[0].df,           4);
    T_ASSERT_EQ_I(recs[0].ma,           0);
    T_ASSERT_EQ_I(recs[0].md,          10);
    /* Drops resolve to -1 until item.txt parser lands. */
    T_ASSERT_EQ_I(recs[0].drop_common, -1);
    T_ASSERT_EQ_I(recs[0].drop_rare,   -1);

    /* Other records untouched. */
    T_ASSERT_EQ_I(recs[1].hp,           0);
    T_ASSERT_EQ_I(recs[6].hp,           0);
    return 0;
}

int test_tables_enemy_longest_prefix_wins(void)
{
    /* Two records share a prefix ("アーリマン" vs "アーリマン緑").
     * A line beginning with the longer name must update the LONGER
     * record, not the shorter one. */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        E_ARRIMAN_GRN "   :99#88#77#66#55#44# foo#bar\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    /* Record 7 = アーリマン緑 — should be populated. */
    T_ASSERT_EQ_I(recs[7].hp, 99);
    T_ASSERT_EQ_I(recs[7].exp_reward, 88);
    T_ASSERT_EQ_I(recs[7].at, 77);
    /* Record 6 = アーリマン (the shorter prefix) — must stay zero. */
    T_ASSERT_EQ_I(recs[6].hp, 0);
    T_ASSERT_EQ_I(recs[6].at, 0);
    return 0;
}

int test_tables_enemy_shorter_prefix_when_no_longer_match(void)
{
    /* Line beginning with the SHORTER name "アーリマン" — followed by
     * padding spaces, not the suffix char. Should match record 6
     * (アーリマン), not records 7/8/9 (which have suffix bytes that
     * don't equal ' '). */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        E_ARRIMAN "     :10#20#30#40#50#60# X#Y\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    T_ASSERT_EQ_I(recs[6].hp, 10);
    T_ASSERT_EQ_I(recs[6].md, 60);
    /* Suffix-records untouched. */
    T_ASSERT_EQ_I(recs[7].hp, 0);
    T_ASSERT_EQ_I(recs[8].hp, 0);
    T_ASSERT_EQ_I(recs[9].hp, 0);
    return 0;
}

int test_tables_enemy_comments_and_blanks_skipped(void)
{
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        "// header comment\r\n"
        "\r\n"
        "\n"
        "  / indented (also skipped via leading-space rule)\r\n"
        E_USAGI "             :42# 9# 8# 7# 6# 5# Fur Ball\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    /* Record 22 = 兎 — should be the only one populated. */
    T_ASSERT_EQ_I(recs[22].hp, 42);
    T_ASSERT_EQ_I(recs[22].exp_reward, 9);
    T_ASSERT_EQ_I(recs[22].md, 5);
    /* Rare drop omitted from input → stays -1 (reset at line start). */
    T_ASSERT_EQ_I(recs[22].drop_rare, -1);
    T_ASSERT_EQ_I(recs[22].drop_common, -1);

    /* Records on either side stay zeroed. */
    T_ASSERT_EQ_I(recs[21].hp, 0);
    T_ASSERT_EQ_I(recs[23].hp, 0);
    return 0;
}

int test_tables_enemy_per_line_drop_reset(void)
{
    /* Parse line A with both drops, then line B without — line B's
     * drops must come out as -1 (engine resets at L925..L926). */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        E_SLIME_GREEN "     :15# 1# 25# 4# 0# 10# Slime Fluid#Worn Sword\r\n"
        E_SLIME_RED   "     :20# 4# 30#16# 0# 10# Slime Fluid\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    /* Record 0 — has both drops (resolved to -1 until item.txt). */
    T_ASSERT_EQ_I(recs[0].drop_common, -1);
    T_ASSERT_EQ_I(recs[0].drop_rare,   -1);
    /* Record 1 — Slime Fluid only; drop_rare stays at the per-line
     * reset value (-1), NOT bleed-through from record 0. */
    T_ASSERT_EQ_I(recs[1].hp, 20);
    T_ASSERT_EQ_I(recs[1].drop_common, -1);
    T_ASSERT_EQ_I(recs[1].drop_rare,   -1);
    return 0;
}

int test_tables_enemy_unknown_name_silently_skipped(void)
{
    /* A line beginning with a name that matches no record must NOT
     * crash or corrupt other records (engine pops MessageBoxA; port
     * silently skips). */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        "TotallyMadeUpName:99#99#99#99#99#99# Stuff#MoreStuff\r\n"
        E_USAGI "             :42# 9# 8# 7# 6# 5# Fur Ball\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    /* 兎 still parses normally despite the preceding bogus line. */
    T_ASSERT_EQ_I(recs[22].hp, 42);
    T_ASSERT_EQ_I(recs[22].md,  5);
    return 0;
}

int test_tables_enemy_placeholder_records_skip_match(void)
{
    /* Records 29/31/32/33/56/57/58 are " " (single space). A real
     * data line starts with a non-space byte. Even if a contrived
     * line started with a single space, it'd be filtered earlier by
     * the comment/blank rule (line[0] == ' '). Either way: nothing
     * routes into the placeholder records. */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        E_SLIME_GREEN "     :15# 1# 25# 4# 0# 10# x#y\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    T_ASSERT_EQ_I(recs[29].hp, 0);
    T_ASSERT_EQ_I(recs[31].hp, 0);
    T_ASSERT_EQ_I(recs[32].hp, 0);
    T_ASSERT_EQ_I(recs[33].hp, 0);
    T_ASSERT_EQ_I(recs[56].hp, 0);
    T_ASSERT_EQ_I(recs[57].hp, 0);
    T_ASSERT_EQ_I(recs[58].hp, 0);
    /* Sanity: real record DID populate. */
    T_ASSERT_EQ_I(recs[0].hp, 15);
    return 0;
}

int test_tables_enemy_no_trailing_newline(void)
{
    /* Last line missing CRLF should still parse. */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        E_USAGI "             :42# 9# 8# 7# 6# 5# Fur Ball";  /* no \r\n */

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    T_ASSERT_EQ_I(recs[22].hp, 42);
    T_ASSERT_EQ_I(recs[22].md,  5);
    return 0;
}

int test_tables_enemy_vendor_shape(void)
{
    /* End-to-end vendor enemy.txt smoke shape. Reproduces the first
     * dozen vendor lines + the boss header section. Validates that
     * the parser routes lines to the correct records under realistic
     * conditions (mixed prefix lengths, blank line, SJIS comment). */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        "// HP,EXP,AT,DF,MA,MD, drop, raredrop\r\n"
        E_SLIME_GREEN "     :15# 1# 25# 4# 0# 10# Slime Fluid#Worn Sword\r\n"
        E_SLIME_RED   "     :20# 4# 30#16# 0# 10# Slime Fluid\r\n"
        E_ARRIMAN     "     :15# 1# 25# 4# 0# 10# Bat Wing\r\n"
        E_ARRIMAN_GRN "   :15# 1# 25# 4# 0# 10# Bat Wing\r\n"
        E_USAGI       "             :15# 5# 20# 8# 0# 10# Fur Ball\r\n"
        "\r\n"
        "// ---- ボス ----\r\n"
        E_NEZUMI_BARU "   : 150#25# 20#30# 0# 10# Tail\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs, NULL, NULL);

    /* Spot-check stats. */
    T_ASSERT_EQ_I(recs[ 0].hp, 15);  T_ASSERT_EQ_I(recs[ 0].at, 25);
    T_ASSERT_EQ_I(recs[ 1].hp, 20);  T_ASSERT_EQ_I(recs[ 1].df, 16);
    T_ASSERT_EQ_I(recs[ 6].hp, 15);  T_ASSERT_EQ_I(recs[ 6].at, 25);
    T_ASSERT_EQ_I(recs[ 7].hp, 15);  /* アーリマン緑 — longer prefix win */
    T_ASSERT_EQ_I(recs[22].hp, 15);  T_ASSERT_EQ_I(recs[22].exp_reward, 5);
    T_ASSERT_EQ_I(recs[24].hp, 150); T_ASSERT_EQ_I(recs[24].at, 20);

    /* Boss flag preserved on the boss record. */
    T_ASSERT_EQ_I(recs[24].flags, 1);

    /* All drops resolve to -1 (no item.txt yet). */
    T_ASSERT_EQ_I(recs[ 0].drop_common, -1);
    T_ASSERT_EQ_I(recs[24].drop_common, -1);
    return 0;
}

/* Stub resolver used by the resolver-wiring test below. Mirrors the
 * shape of tables_item_resolve but against a small in-test name → id
 * map. Trailing/leading spaces on the drop name aren't trimmed by the
 * parser, so the test inputs use bare names without padding. */
static int32_t stub_drop_resolve(const char *name, void *user)
{
    (void)user;
    if (strcmp(name, "Slime Fluid") == 0) return 100;
    if (strcmp(name, "Worn Sword")  == 0) return 200;
    if (strcmp(name, "Fur Ball")    == 0) return 300;
    return -1;
}

int test_tables_enemy_drop_resolves_via_callback(void)
{
    /* When a non-NULL resolver is wired up, drop names get translated
     * to item ids; an unknown name still resolves to -1. */
    enemy_record_t recs[ENEMY_COUNT];
    tables_enemy_init(recs);

    static const unsigned char input[] =
        E_SLIME_GREEN "     :15# 1# 25# 4# 0# 10#Slime Fluid#Worn Sword\r\n"
        E_USAGI       "             :42# 9# 8# 7# 6# 5#Fur Ball#NotAnItem\r\n";

    tables_parse_enemy(input, sizeof input - 1, recs,
                       stub_drop_resolve, NULL);

    /* Resolved hits land on drop_common / drop_rare. */
    T_ASSERT_EQ_I(recs[ 0].drop_common, 100);  /* Slime Fluid */
    T_ASSERT_EQ_I(recs[ 0].drop_rare,   200);  /* Worn Sword  */
    T_ASSERT_EQ_I(recs[22].drop_common, 300);  /* Fur Ball    */
    T_ASSERT_EQ_I(recs[22].drop_rare,   -1);   /* NotAnItem   */
    return 0;
}
