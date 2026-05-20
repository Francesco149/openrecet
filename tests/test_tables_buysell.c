/*
 * test_tables_buysell.c — unit tests for `data/buysell.txt` parsing.
 *
 * Coverage:
 *   1. Empty input            (zero-init everything)
 *   2. Comment / blank lines  (skipped, no globals touched)
 *   3. `ok:` toggle           (sets debug flag, no other state)
 *   4. SJIS scalar keys       (客番号 + 種類 — the engine's two Japanese keys)
 *   5. msg/rmsg arrays        (20 entries each, parsed via printf'd keys)
 *   6. CRLF line endings      (vendor file format)
 *   7. End-of-buffer w/o newline (last line still parsed)
 *   8. Vendor-shape synthetic (mirrors the actual vendor file's content
 *                              order, exercising the full happy path
 *                              with SJIS keys + CRLF + comment lines).
 *
 * No vendor file dependency: buysell.txt content in the shipped game
 * has all msg/rmsg = 0, so a real-vendor cross-check would mostly
 * validate zero-init. The synthetic fixture #8 reproduces the exact
 * byte structure (SJIS keys, CRLF, leading slash for comments) at
 * non-trivial values.
 */
#include "t.h"
#include "tables_buysell.h"

#include <stdint.h>
#include <string.h>

/* Shift-JIS key bytes — same as the engine's compare strings. */
#define SJIS_KYAKU "\x8B\x71\x94\xD4\x8D\x86"  /* 客番号 */
#define SJIS_KIND  "\x94\x84\x94\x83"          /* 種類   */

static void run_parse(const char *literal, size_t size,
                      struct buysell_config *out)
{
    tables_parse_buysell((const unsigned char *)literal, size, out);
}

int test_tables_buysell_empty(void)
{
    struct buysell_config cfg;
    /* Pre-poison the struct so we catch any field the parser fails to
     * zero. (The parser memset()s up front; this verifies that.) */
    memset(&cfg, 0xCC, sizeof cfg);
    run_parse("", 0, &cfg);

    T_ASSERT_EQ_I(cfg.debug_mode,   0);
    T_ASSERT_EQ_I(cfg.kyaku_number, 0);
    T_ASSERT_EQ_I(cfg.kind,         0);
    for (int i = 0; i < BUYSELL_MSG_COUNT; i++) {
        T_ASSERT_EQ_I(cfg.msg[i],  0);
        T_ASSERT_EQ_I(cfg.rmsg[i], 0);
    }
    return 0;
}

int test_tables_buysell_comments_only(void)
{
    /* All lines start with '/', \r, or \n — every key check skipped. */
    const char input[] =
        "/ok:\tcomment about the ok flag\r\n"
        "/\r\n"
        "\r\n"
        "/random text that mentions msg00 but is commented\r\n";

    struct buysell_config cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.debug_mode,   0);
    T_ASSERT_EQ_I(cfg.kyaku_number, 0);
    T_ASSERT_EQ_I(cfg.kind,         0);
    T_ASSERT_EQ_I(cfg.msg[0],       0);
    return 0;
}

int test_tables_buysell_ok_toggle(void)
{
    const char input[] = "ok:\r\n";

    struct buysell_config cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.debug_mode,   1);
    /* Other globals untouched. */
    T_ASSERT_EQ_I(cfg.kyaku_number, 0);
    T_ASSERT_EQ_I(cfg.kind,         0);
    return 0;
}

int test_tables_buysell_sjis_scalars(void)
{
    const char input[] =
        SJIS_KYAKU ":14\r\n"
        SJIS_KIND  ":2\r\n";

    struct buysell_config cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.kyaku_number, 14);
    T_ASSERT_EQ_I(cfg.kind,         2);
    T_ASSERT_EQ_I(cfg.debug_mode,   0);
    return 0;
}

int test_tables_buysell_msg_arrays(void)
{
    /* Populate a handful of msg / rmsg slots, including the boundary
     * indices 0 and 19 (the engine loops over 0..19 inclusive). */
    const char input[] =
        "msg00:7\r\n"
        "msg05:42\r\n"
        "msg19:99\r\n"
        "rmsg00:1\r\n"
        "rmsg10:50\r\n"
        "rmsg19:123\r\n";

    struct buysell_config cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.msg[0],   7);
    T_ASSERT_EQ_I(cfg.msg[5],   42);
    T_ASSERT_EQ_I(cfg.msg[19],  99);
    T_ASSERT_EQ_I(cfg.msg[6],   0);   /* unset stays zero */

    T_ASSERT_EQ_I(cfg.rmsg[0],  1);
    T_ASSERT_EQ_I(cfg.rmsg[10], 50);
    T_ASSERT_EQ_I(cfg.rmsg[19], 123);
    T_ASSERT_EQ_I(cfg.rmsg[1],  0);   /* unset stays zero */
    return 0;
}

int test_tables_buysell_no_trailing_newline(void)
{
    /* Last line lacks any \r\n — verifies our EOF handling. */
    const char input[] = "msg00:5";

    struct buysell_config cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.msg[0], 5);
    return 0;
}

int test_tables_buysell_embedded_null_terminates(void)
{
    /* Engine stops on \0 mid-buffer. msg05 after the \0 must not parse. */
    const char input[] =
        "msg00:11\r\n"
        "\0"
        "msg05:22\r\n";

    struct buysell_config cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.msg[0], 11);
    T_ASSERT_EQ_I(cfg.msg[5], 0);
    return 0;
}

int test_tables_buysell_vendor_shape(void)
{
    /* Reproduces the structure of the actual vendor `data/buysell.txt`
     * (CRLF, SJIS keys, leading-'/' comments), but with non-zero values
     * so we can tell parsed fields from default-zero fields. */
    static const unsigned char vendor_like[] =
        "/ok:\t//\tdebug mode toggle (comment line — ignored)\r\n"
        "\r\n"
        "/" SJIS_KYAKU ":kyaku.txt entry number\r\n"
        "/" SJIS_KIND  ":0..sell, 1..buy, 2..about\r\n"
        "/msg00:dialogue branch override\r\n"
        "/\r\n"
        SJIS_KYAKU ":14\r\n"
        SJIS_KIND  ":2\r\n"
        "msg00:0\r\n"
        "msg01:3\r\n"
        "msg12:7\r\n"
        "\r\n"
        "/ rmsg block — reset-time overrides\r\n"
        "rmsg00:0\r\n"
        "rmsg05:1\r\n"
        "rmsg12:9\r\n";

    struct buysell_config cfg;
    run_parse((const char *)vendor_like, sizeof vendor_like - 1, &cfg);

    T_ASSERT_EQ_I(cfg.debug_mode,   0);   /* `ok:` line is commented   */
    T_ASSERT_EQ_I(cfg.kyaku_number, 14);
    T_ASSERT_EQ_I(cfg.kind,         2);
    T_ASSERT_EQ_I(cfg.msg[0],       0);
    T_ASSERT_EQ_I(cfg.msg[1],       3);
    T_ASSERT_EQ_I(cfg.msg[12],      7);
    T_ASSERT_EQ_I(cfg.rmsg[0],      0);
    T_ASSERT_EQ_I(cfg.rmsg[5],      1);
    T_ASSERT_EQ_I(cfg.rmsg[12],     9);
    return 0;
}
