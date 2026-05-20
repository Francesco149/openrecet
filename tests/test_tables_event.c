/*
 * test_tables_event.c — unit tests for src/tables_event.c.
 *
 * Pure-C tests, runnable under host gcc + ASan/UBSan via
 * `make -C tests run`. Fixtures use SJIS bytes spelled as hex escapes
 * so the source stays ASCII-clean.
 */

#include "t.h"
#include "tables_event.h"

#include <stdlib.h>
#include <string.h>

/* ── SJIS shorthands ─────────────────────────────────────────────────── */
#define H_HIROBA "\x8d\x4c\x8f\xea"      /* 広場 */
#define H_ICHIBA "\x8e\x73\x8f\xea"      /* 市場 */
#define H_KYOKAI "\x8b\xb3\x89\xef"      /* 教会 */
#define H_SAKABA "\x8e\xf0\x8f\xea"      /* 酒場 */

#define T_MORN "\x92\xa9"  /* 朝 */
#define T_NOON "\x92\x8b"  /* 昼 */
#define T_EVEN "\x97\x5b"  /* 夕 */
#define T_NIGT "\x96\xe9"  /* 夜 */

/* Full-width space, used as padding in vendor files. */
#define ZSP "\x81\x40"

/* ── Tests ─────────────────────────────────────────────────────────── */

int test_tables_event_empty_seeds_default(void)
{
    /* Empty input → only the pre-baked record 0 of category 0 (広場)
     * survives; the other categories stay at count=0. */
    event_state_t out;
    tables_parse_event((const unsigned char *)"", 0, &out);

    T_ASSERT_EQ_I(out.counts[EVENT_CAT_HIROBA], 1);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_ICHIBA], 0);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_KYOKAI], 0);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_SAKABA], 0);

    event_record_t *seed = &out.records[EVENT_CAT_HIROBA][0];
    T_ASSERT_EQ_I(seed->id,              0xb);
    T_ASSERT_EQ_I(seed->flag_on_trigger, 1);
    T_ASSERT_EQ_I(seed->prereq[0],       0xa3);
    T_ASSERT_EQ_I(seed->prereq[1],       -1);
    T_ASSERT_EQ_I(seed->prereq[2],       -1);
    T_ASSERT_EQ_I(seed->prereq[3],       -1);
    T_ASSERT_EQ_I(seed->time_first,      0);
    T_ASSERT_EQ_I(seed->time_max,        1);
    T_ASSERT_EQ_I(seed->day_pairs[0][0], 0);
    T_ASSERT_EQ_I(seed->day_pairs[0][1], 40);
    T_ASSERT_EQ_I(seed->day_pairs[1][0], -1);
    T_ASSERT_EQ_I(seed->day_pairs[1][1], -1);
    T_ASSERT_EQ_I(seed->loop_min,        0);
    T_ASSERT_EQ_I(seed->decay_or_max,    100000);

    /* End-of-list sentinel: the slot one past `counts` has id=-1. */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][1].id, -1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_ICHIBA][0].id, -1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].id, -1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].id, -1);
    return 0;
}

int test_tables_event_layout_byte_offsets(void)
{
    /* Belt-and-suspenders runtime check on top of the _Static_assert —
     * mostly here as a regression tripwire if anyone re-orders fields. */
    T_ASSERT_EQ_U(sizeof(event_record_t), 200);
    return 0;
}

int test_tables_event_comments_and_blanks_skipped(void)
{
    static const unsigned char input[] =
        "/leading comment line\r\n"
        "\r\n"
        "// also a comment (only leading '/' matters)\r\n"
        "\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_HIROBA], 1);  /* seed only */
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_ICHIBA], 0);
    return 0;
}

int test_tables_event_basic_hiroba_record(void)
{
    /* A single 広場 line lands at slot 1 (slot 0 is the seed). */
    static const unsigned char input[] =
        H_HIROBA "\r\n"
        "14- 4:  100:-1:-1:-1  :" T_MORN T_NOON ZSP ZSP ":  0:  3-9,36-999:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.counts[EVENT_CAT_HIROBA], 2);
    event_record_t *r = &out.records[EVENT_CAT_HIROBA][1];
    T_ASSERT_EQ_I(r->id,               14);
    T_ASSERT_EQ_I(r->flag_on_trigger,   4);
    T_ASSERT_EQ_I(r->prereq[0],       0x100);  /* hex "100" = 256 */
    T_ASSERT_EQ_I(r->prereq[1],        -1);
    T_ASSERT_EQ_I(r->prereq[2],        -1);
    T_ASSERT_EQ_I(r->prereq[3],        -1);
    T_ASSERT_EQ_I(r->time_first,        0);    /* 朝 */
    T_ASSERT_EQ_I(r->time_max,          1);    /* 昼 */
    T_ASSERT_EQ_I(r->loop_min,          0);
    T_ASSERT_EQ_I(r->day_pairs[0][0],   3);
    T_ASSERT_EQ_I(r->day_pairs[0][1],   9);
    T_ASSERT_EQ_I(r->day_pairs[1][0],  36);
    T_ASSERT_EQ_I(r->day_pairs[1][1], 999);
    T_ASSERT_EQ_I(r->day_pairs[2][0],  -1);

    /* Sentinel rolls forward by one. */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][2].id, -1);
    return 0;
}

int test_tables_event_prereq_hex_and_minus(void)
{
    /* Prereqs `39`, `1b5`, `-2`, `-` should commit (0x39, 0x1b5, -1, -1).
     * The "-anywhere = -1" promotion is engine-faithful. */
    static const unsigned char input[] =
        H_ICHIBA "\r\n"
        "1- 2:  39:1b5:-2:- :" T_EVEN T_NIGT ":0:5-35:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.counts[EVENT_CAT_ICHIBA], 1);
    event_record_t *r = &out.records[EVENT_CAT_ICHIBA][0];
    T_ASSERT_EQ_I(r->prereq[0], 0x39);
    T_ASSERT_EQ_I(r->prereq[1], 0x1b5);
    T_ASSERT_EQ_I(r->prereq[2], -1);
    T_ASSERT_EQ_I(r->prereq[3], -1);
    return 0;
}

int test_tables_event_time_first_and_max(void)
{
    /* Tag sequence 　　夕夜 (two full-width spaces then 夕夜) ->
     *   - 　 (no match, advance 1)
     *   - 　 (no match, advance 1)
     *   ...four single-byte advances total (two full-width spaces = 4 bytes)
     *   - 夕 → matched_idx=2, time_first=2, time_max=2
     *   - 夜 → matched_idx=3, time_max=3 (time_first stays 2). */
    static const unsigned char input[] =
        H_SAKABA "\r\n"
        "14- 2: 190:-1:-1:-1:" ZSP ZSP T_EVEN T_NIGT ":0:4-5,36-999:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.counts[EVENT_CAT_SAKABA], 1);
    event_record_t *r = &out.records[EVENT_CAT_SAKABA][0];
    T_ASSERT_EQ_I(r->time_first, 2);
    T_ASSERT_EQ_I(r->time_max,   3);
    return 0;
}

int test_tables_event_time_max_clamps_to_first_no_higher(void)
{
    /* 夜朝 — 夜 (idx 3) first, then 朝 (idx 0). time_first stays at 3,
     * time_max stays at 3 (engine writes only when new index > current). */
    static const unsigned char input[] =
        H_KYOKAI "\r\n"
        "1- 1: 1:-1:-1:-1:" T_NIGT T_MORN ":0:1-2:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    event_record_t *r = &out.records[EVENT_CAT_KYOKAI][0];
    T_ASSERT_EQ_I(r->time_first, 3);
    T_ASSERT_EQ_I(r->time_max,   3);
    return 0;
}

int test_tables_event_time_unknown_tokens_only(void)
{
    /* Tag block with no known tokens: 4 full-width spaces. time_first
     * and time_max stay at their memset-zero defaults. */
    static const unsigned char input[] =
        H_HIROBA "\r\n"
        "1- 1: 1:-1:-1:-1:" ZSP ZSP ZSP ZSP ":0:1-2:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    event_record_t *r = &out.records[EVENT_CAT_HIROBA][1];
    T_ASSERT_EQ_I(r->time_first, 0);
    T_ASSERT_EQ_I(r->time_max,   0);
    return 0;
}

int test_tables_event_loop_min_atoi(void)
{
    static const unsigned char input[] =
        H_SAKABA "\r\n"
        "17- 2: 201:197:-1:47:" ZSP ZSP ZSP T_EVEN T_NIGT ":42:50-999:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    event_record_t *r = &out.records[EVENT_CAT_SAKABA][0];
    T_ASSERT_EQ_I(r->loop_min, 42);
    T_ASSERT_EQ_I(r->day_pairs[0][0], 50);
    T_ASSERT_EQ_I(r->day_pairs[0][1], 999);
    T_ASSERT_EQ_I(r->day_pairs[1][0], -1);
    return 0;
}

int test_tables_event_day_pairs_up_to_20(void)
{
    /* 21 declared pairs — only the first 20 should land. */
    char buf[2048];
    int n = 0;
    n += snprintf(buf + n, sizeof buf - n, H_KYOKAI "\r\n");
    n += snprintf(buf + n, sizeof buf - n, "1- 1: 1:-1:-1:-1:" T_MORN ":0:");
    for (int i = 0; i < 21; i++) {
        n += snprintf(buf + n, sizeof buf - n,
                      "%d-%d%s", i * 10 + 1, i * 10 + 5,
                      (i == 20) ? ":\r\n" : ",");
    }

    event_state_t out;
    tables_parse_event((const unsigned char *)buf, (size_t)n, &out);
    event_record_t *r = &out.records[EVENT_CAT_KYOKAI][0];
    T_ASSERT_EQ_I(r->day_pairs[0][0],   1);
    T_ASSERT_EQ_I(r->day_pairs[0][1],   5);
    T_ASSERT_EQ_I(r->day_pairs[19][0], 191);
    T_ASSERT_EQ_I(r->day_pairs[19][1], 195);
    /* No 21st slot exists in the struct (cap is 20); the parser stops
     * after committing pair 19 and never reads past index 19. */
    return 0;
}

int test_tables_event_category_dispatch(void)
{
    /* One record per category. 広場 lands at slot 1 (slot 0 = seed). */
    static const unsigned char input[] =
        H_HIROBA "\r\n"
        "10- 1: 1:-1:-1:-1:" T_MORN ":0:1-2:\r\n"
        H_ICHIBA "\r\n"
        "10- 2: 2:-1:-1:-1:" T_NOON ":0:3-4:\r\n"
        H_KYOKAI "\r\n"
        "10- 3: 3:-1:-1:-1:" T_EVEN ":0:5-6:\r\n"
        H_SAKABA "\r\n"
        "10- 4: 4:-1:-1:-1:" T_NIGT ":0:7-8:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.counts[EVENT_CAT_HIROBA], 2);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_ICHIBA], 1);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_KYOKAI], 1);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_SAKABA], 1);

    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][1].id, 10);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][1].flag_on_trigger, 1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_ICHIBA][0].id, 10);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_ICHIBA][0].flag_on_trigger, 2);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].id, 10);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].flag_on_trigger, 3);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].id, 10);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].flag_on_trigger, 4);
    return 0;
}

int test_tables_event_data_line_before_header_goes_to_hiroba(void)
{
    /* No header in the file — data line falls into category 0 (engine
     * default). Appended at slot 1 because seed is at slot 0. */
    static const unsigned char input[] =
        "9-9: 999:-1:-1:-1:" T_MORN ":0:1-1:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_HIROBA], 2);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][1].id, 9);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][1].flag_on_trigger, 9);
    return 0;
}

int test_tables_event_decay_or_max_zero_for_parsed(void)
{
    /* Every parsed record explicitly gets decay_or_max=0; the seed
     * keeps its 100000. */
    static const unsigned char input[] =
        H_ICHIBA "\r\n"
        "1- 1: 1:-1:-1:-1:" T_MORN ":0:1-2:\r\n";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][0].decay_or_max, 100000);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_ICHIBA][0].decay_or_max, 0);
    return 0;
}

int test_tables_event_no_trailing_newline(void)
{
    /* File ends without CRLF — last line still parses. */
    static const unsigned char input[] =
        H_KYOKAI "\r\n"
        "5- 5: 5:-1:-1:-1:" T_NOON ":0:1-1:";
    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_KYOKAI], 1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].id, 5);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].day_pairs[0][0], 1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].day_pairs[0][1], 1);
    return 0;
}

int test_tables_event_vendor_shape(void)
{
    /* Replays the vendor file's "introductory 広場 block + section
     * transitions" in miniature, plus a 酒場 entry the consumer would
     * see when 酒場 is the active in-town location. Confirms the
     * dispatcher correctly threads the current category through
     * interleaved comments. */
    static const unsigned char input[] =
        "/header comment\r\n"
        "\r\n"
        H_HIROBA "\r\n"
        "14- 4:  100:-1:-1:-1  :" T_MORN T_NOON ZSP ZSP ":  0:  3-9,36-999:   //plaza day comment\r\n"
        "14- 5:  106:-1:-1:-1  :" T_EVEN ZSP ZSP ZSP ":  0:  20-30,36-999:\r\n"
        "\r\n"
        H_ICHIBA "\r\n"
        "15- 9: 130:-1:-1:-1  :" T_EVEN ZSP ZSP ":0 : 4-5,36-999:\r\n"
        "\r\n"
        "/comment under 市場\r\n"
        H_SAKABA "\r\n"
        "14- 2: 190:-1:-1:-1:" ZSP ZSP T_EVEN T_NIGT ":0:4-5,36-999:\r\n"
        "14- 3: 191:-1:-1:-1:" ZSP ZSP T_EVEN T_NIGT ":0:5-9,36-999:\r\n";

    event_state_t out;
    tables_parse_event(input, sizeof input - 1, &out);

    T_ASSERT_EQ_I(out.counts[EVENT_CAT_HIROBA], 3);  /* seed + 2 parsed */
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_ICHIBA], 1);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_KYOKAI], 0);
    T_ASSERT_EQ_I(out.counts[EVENT_CAT_SAKABA], 2);

    /* First 広場 parsed record. */
    event_record_t *h1 = &out.records[EVENT_CAT_HIROBA][1];
    T_ASSERT_EQ_I(h1->id,             14);
    T_ASSERT_EQ_I(h1->flag_on_trigger, 4);
    T_ASSERT_EQ_I(h1->prereq[0],     0x100);
    T_ASSERT_EQ_I(h1->time_first,     0);
    T_ASSERT_EQ_I(h1->time_max,       1);
    T_ASSERT_EQ_I(h1->loop_min,       0);
    T_ASSERT_EQ_I(h1->day_pairs[0][0],  3);
    T_ASSERT_EQ_I(h1->day_pairs[0][1],  9);
    T_ASSERT_EQ_I(h1->day_pairs[1][0], 36);
    T_ASSERT_EQ_I(h1->day_pairs[1][1], 999);

    /* Second 広場 parsed record — evening-only. prereq[0]=0x106 (hex). */
    event_record_t *h2 = &out.records[EVENT_CAT_HIROBA][2];
    T_ASSERT_EQ_I(h2->id,             14);
    T_ASSERT_EQ_I(h2->flag_on_trigger, 5);
    T_ASSERT_EQ_I(h2->prereq[0],     0x106);
    T_ASSERT_EQ_I(h2->prereq[1],      -1);
    T_ASSERT_EQ_I(h2->time_first,      2);  /* 夕 */
    T_ASSERT_EQ_I(h2->time_max,        2);

    /* 市場 single. */
    event_record_t *i0 = &out.records[EVENT_CAT_ICHIBA][0];
    T_ASSERT_EQ_I(i0->id,             15);
    T_ASSERT_EQ_I(i0->flag_on_trigger, 9);
    T_ASSERT_EQ_I(i0->day_pairs[0][0], 4);
    T_ASSERT_EQ_I(i0->day_pairs[0][1], 5);

    /* 酒場 pair. */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].id, 14);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].flag_on_trigger, 2);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].time_first, 2);  /* 夕 */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].time_max,   3);  /* 夜 */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][1].id, 14);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][1].flag_on_trigger, 3);

    /* Sentinels in each category. */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][3].id, -1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_ICHIBA][1].id, -1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_KYOKAI][0].id, -1);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][2].id, -1);

    /* Decay_or_max is 100000 only for the seed. */
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][0].decay_or_max, 100000);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_HIROBA][1].decay_or_max, 0);
    T_ASSERT_EQ_I(out.records[EVENT_CAT_SAKABA][0].decay_or_max, 0);
    return 0;
}
