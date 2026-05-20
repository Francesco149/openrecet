/*
 * test_tables_model.c — unit tests for `data/model.txt` parsing.
 *
 * Coverage:
 *   1. Empty input                   (zero-init everything; count=0 everywhere)
 *   2. Basic one-record              (no:, fname:, two slots)
 *   3. Index threading               (two records, record 0 and record 5)
 *   4. Comments / blanks skipped     (//, /dead, CRLF, LF-only)
 *   5. fname before any no:          (writes to default record 0)
 *   6. Repeated slot increments count (engine: count++ unconditional)
 *   7. Overlong fname truncates      (no count corruption)
 *   8. Out-of-range no: skipped      (no OOB write, all records zero)
 *   9. Vendor shape end-to-end       (17 models, max 8 points, spot checks)
 */
#include "t.h"
#include "tables_model.h"

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */

int test_tables_model_empty(void)
{
    tables_model_t out[MODEL_DEF_COUNT];
    memset(out, 0xCC, sizeof out);

    tables_parse_model((const unsigned char *)"", 0, out);

    for (int i = 0; i < MODEL_DEF_COUNT; i++) {
        T_ASSERT_EQ_U(out[i].count, 0);
        for (int s = 0; s < MODEL_DEF_POINT_SLOTS; s++) {
            T_ASSERT_EQ_U(out[i].used[s], 0);
        }
    }
    return 0;
}

int test_tables_model_basic_one_record(void)
{
    const char input[] =
        "no:0\r\n"
        "fname:foo.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    T_ASSERT(strcmp(out[0].fname, "foo.x") == 0);
    T_ASSERT(strcmp(out[0].point[0], "point_01") == 0);
    T_ASSERT(strcmp(out[0].point[1], "point_02") == 0);
    T_ASSERT_EQ_U(out[0].used[0], 1);
    T_ASSERT_EQ_U(out[0].used[1], 1);
    T_ASSERT_EQ_U(out[0].count, 2);
    /* All other slots zero. */
    for (int s = 2; s < MODEL_DEF_POINT_SLOTS; s++) {
        T_ASSERT_EQ_U(out[0].used[s], 0);
        T_ASSERT_EQ_I(out[0].point[s][0], '\0');
    }
    /* All other records zero. */
    for (int i = 1; i < MODEL_DEF_COUNT; i++) {
        T_ASSERT_EQ_U(out[i].count, 0);
    }
    return 0;
}

int test_tables_model_no_index_threads(void)
{
    const char input[] =
        "no:0\r\n"
        "fname:first.x\r\n"
        "00:pt_a\r\n"
        "no:5\r\n"
        "fname:fifth.x\r\n"
        "00:pt_b\r\n"
        "01:pt_c\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    /* Record 0 */
    T_ASSERT(strcmp(out[0].fname, "first.x") == 0);
    T_ASSERT(strcmp(out[0].point[0], "pt_a") == 0);
    T_ASSERT_EQ_U(out[0].count, 1);

    /* Record 5 */
    T_ASSERT(strcmp(out[5].fname, "fifth.x") == 0);
    T_ASSERT(strcmp(out[5].point[0], "pt_b") == 0);
    T_ASSERT(strcmp(out[5].point[1], "pt_c") == 0);
    T_ASSERT_EQ_U(out[5].count, 2);

    /* Records 1-4, 6-19 untouched. */
    for (int i = 1; i <= 4; i++)   T_ASSERT_EQ_U(out[i].count, 0);
    for (int i = 6; i < MODEL_DEF_COUNT; i++) T_ASSERT_EQ_U(out[i].count, 0);
    return 0;
}

int test_tables_model_comments_skipped(void)
{
    /* All of these should be skipped: '//' line, '/dead', blank CRLF,
     * blank LF-only. */
    const char input[] =
        "// this is a comment\r\n"
        "/dead line\r\n"
        "\r\n"
        "\n"
        "no:2\r\n"
        "fname:bar.x\r\n"
        "00:bone1\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    T_ASSERT(strcmp(out[2].fname, "bar.x") == 0);
    T_ASSERT(strcmp(out[2].point[0], "bone1") == 0);
    T_ASSERT_EQ_U(out[2].count, 1);
    /* Others untouched. */
    for (int i = 0; i < MODEL_DEF_COUNT; i++) {
        if (i == 2) continue;
        T_ASSERT_EQ_U(out[i].count, 0);
    }
    return 0;
}

int test_tables_model_fname_default_record_zero(void)
{
    /* fname: before any no: → writes to record 0 (engine: local_c
     * initialised to 0 at L1429). */
    const char input[] =
        "fname:foo.x\r\n"
        "00:point_01\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    T_ASSERT(strcmp(out[0].fname, "foo.x") == 0);
    T_ASSERT(strcmp(out[0].point[0], "point_01") == 0);
    T_ASSERT_EQ_U(out[0].count, 1);
    return 0;
}

int test_tables_model_repeated_slot_increments_count(void)
{
    /* Redefining slot 00 twice: last write wins for the name, but count
     * increments both times (engine: unconditional count++ per matched
     * NN: line, no gate on !used[slot]). */
    const char input[] =
        "no:0\r\n"
        "00:foo\r\n"
        "00:bar\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    T_ASSERT(strcmp(out[0].point[0], "bar") == 0);  /* last write wins */
    T_ASSERT_EQ_U(out[0].used[0], 1);
    T_ASSERT_EQ_U(out[0].count, 2);  /* incremented twice */
    return 0;
}

int test_tables_model_overlong_fname_truncates(void)
{
    /* A 50-character fname should be truncated cleanly at
     * MODEL_DEF_NAME_MAX-1 chars with a NUL at fname[MODEL_DEF_NAME_MAX-1].
     * Critical: count must NOT be corrupted (must remain 0 with no NN:
     * lines). */
    const char input[] =
        "no:0\r\n"
        "fname:AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDDDDDDDEEEEEEEEEE\r\n";
    /* 50 A-E chars above */

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    /* fname must be truncated — exactly MODEL_DEF_NAME_MAX-1 chars of
     * data then a NUL at the last byte of the field. */
    T_ASSERT_EQ_I((int)(unsigned char)out[0].fname[MODEL_DEF_NAME_MAX - 1], '\0');
    /* The length should be exactly MODEL_DEF_NAME_MAX - 1. */
    T_ASSERT_EQ_U(strlen(out[0].fname), (size_t)(MODEL_DEF_NAME_MAX - 1));
    /* count must not have been corrupted by the overlong write. */
    T_ASSERT_EQ_U(out[0].count, 0);
    return 0;
}

int test_tables_model_out_of_range_index_skipped(void)
{
    /* no:25 is out of range (>= MODEL_DEF_COUNT=20). The port must
     * skip subsequent fname: and NN: writes without an OOB access.
     * All records must remain zero. */
    const char input[] =
        "no:25\r\n"
        "fname:foo.x\r\n"
        "00:bar\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model((const unsigned char *)input, sizeof input - 1, out);

    for (int i = 0; i < MODEL_DEF_COUNT; i++) {
        T_ASSERT_EQ_U(out[i].count, 0);
        T_ASSERT_EQ_I(out[i].fname[0], '\0');
    }
    return 0;
}

int test_tables_model_vendor_shape(void)
{
    /* Reproduces the actual data/model.txt shape using only ASCII bytes
     * (the SJIS comment lines start with '/' and are skipped by the
     * parser — we omit them from the fixture entirely). */
    static const unsigned char input[] =
        /* kine models 0-2 (2 points each) */
        "no:0\r\n"
        "fname:g_lat_06.x\r\n"
        "00:point_01\r\n"
        "01:bone_kine\r\n"
        "\r\n"
        "no:1\r\n"
        "fname:g_lat_07.x\r\n"
        "00:point_01\r\n"
        "01:bone_kine\r\n"
        "\r\n"
        "no:2\r\n"
        "fname:g_lat_08.x\r\n"
        "00:point_01\r\n"
        "01:bone_kine\r\n"
        "\r\n"
        /* golem models 3-8 (7 points each) */
        "no:3\r\n"
        "fname:golem_g01.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "\r\n"
        "no:4\r\n"
        "fname:golem_g02.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "\r\n"
        "no:5\r\n"
        "fname:golem_g03.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "\r\n"
        "no:6\r\n"
        "fname:golem_g01.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "\r\n"
        "no:7\r\n"
        "fname:golem_g02.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "\r\n"
        "no:8\r\n"
        "fname:golem_g03.x\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "\r\n"
        /* cyg models 15, 17, 18 */
        "no:15\r\n"
        "fname:cyg_body.X\r\n"
        "00:point_01\r\n"
        "01:point_20\r\n"
        "02:point_21\r\n"
        "\r\n"
        "no:18\r\n"
        "fname:cyg_l.X\r\n"
        "00:bone03_arm_body_l\r\n"
        "01:point_15\r\n"
        "02:point_16\r\n"
        "03:point_17\r\n"
        "04:point_18\r\n"
        "05:point_19\r\n"
        "\r\n"
        "no:17\r\n"
        "fname:cyg_r.X\r\n"
        "00:bone03_arm_body_r\r\n"
        "01:point_10\r\n"
        "02:point_11\r\n"
        "03:point_12\r\n"
        "04:point_13\r\n"
        "05:point_14\r\n"
        "\r\n"
        /* kurage 12, 13 (3 points each) */
        "no:12\r\n"
        "fname:kurage_01.x\r\n"
        "00:bone1_body\r\n"
        "01:point_01\r\n"
        "02:point_02\r\n"
        "\r\n"
        "no:13\r\n"
        "fname:kurage_01.x\r\n"
        "00:bone1_body\r\n"
        "01:point_01\r\n"
        "02:point_02\r\n"
        "\r\n"
        /* kani 10, 11 (8 points each) */
        "no:10\r\n"
        "fname:kani01.X\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "07:point_08\r\n"
        "\r\n"
        "no:11\r\n"
        "fname:kani01.X\r\n"
        "00:point_01\r\n"
        "01:point_02\r\n"
        "02:point_03\r\n"
        "03:point_04\r\n"
        "04:point_05\r\n"
        "05:point_06\r\n"
        "06:point_07\r\n"
        "07:point_08\r\n"
        "\r\n"
        /* maou 14 (6 points) */
        "no:14\r\n"
        "fname:maou_02.X\r\n"
        "00:bone03_arm_body_r\r\n"
        "01:point_10\r\n"
        "02:point_11\r\n"
        "03:point_12\r\n"
        "04:point_13\r\n"
        "05:point_14\r\n";

    tables_model_t out[MODEL_DEF_COUNT];
    tables_parse_model(input, sizeof input - 1, out);

    /* 17 models with count > 0 */
    int defined = 0;
    int max_pts = 0;
    for (int i = 0; i < MODEL_DEF_COUNT; i++) {
        if (out[i].count > 0) {
            defined++;
            if ((int)out[i].count > max_pts) max_pts = (int)out[i].count;
        }
    }
    T_ASSERT_EQ_I(defined, 17);
    T_ASSERT_EQ_I(max_pts, 8);

    /* Gaps at 9, 16, 19 remain zero. */
    T_ASSERT_EQ_U(out[9].count, 0);
    T_ASSERT_EQ_U(out[16].count, 0);
    T_ASSERT_EQ_U(out[19].count, 0);

    /* Spot checks. */
    T_ASSERT(strcmp(out[0].fname, "g_lat_06.x") == 0);
    T_ASSERT(strcmp(out[3].fname, "golem_g01.x") == 0);
    T_ASSERT(strcmp(out[15].fname, "cyg_body.X") == 0);  /* case-preserved */
    T_ASSERT(strcmp(out[10].point[7], "point_08") == 0);

    return 0;
}
