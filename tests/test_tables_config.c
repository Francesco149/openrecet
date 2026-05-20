/*
 * test_tables_config.c — unit tests for `data/config.idx` parsing.
 *
 * Coverage:
 *   1. Empty input              (zero-init everything)
 *   2. All five live keys       (kanjioff, edgewi, edgedel, effectmode, font)
 *   3. `makefont` no-op         (engine quirk — matches but assigns nothing)
 *   4. Font name w/ Shift-JIS   (vendor file's "ＭＳ Ｐゴシック" form)
 *   5. Font over-length truncation (engine writes into a fixed 256-byte buffer)
 *   6. Commented lines ignored  (vendor file ships nearly every key commented)
 *   7. Vendor-shape end-to-end  (mirrors the actual file's active-key set:
 *                                only edgewi/edgedel; font/kanjioff/effectmode
 *                                are all `/`-prefixed in the shipping file).
 */
#include "t.h"
#include "tables_config.h"

#include <stdint.h>
#include <string.h>

static void run_parse(const char *literal, size_t size,
                      struct config_idx *out)
{
    tables_parse_config((const unsigned char *)literal, size, out);
}

int test_tables_config_empty(void)
{
    struct config_idx cfg;
    memset(&cfg, 0xCC, sizeof cfg);
    run_parse("", 0, &cfg);

    T_ASSERT_EQ_I(cfg.kanjioff,   0);
    T_ASSERT_EQ_I(cfg.edgewi,     0);
    T_ASSERT_EQ_I(cfg.edgedel,    0);
    T_ASSERT_EQ_I(cfg.effectmode, 0);
    T_ASSERT_EQ_I(cfg.font_set,   0);
    T_ASSERT_EQ_I(cfg.font_name[0], '\0');
    return 0;
}

int test_tables_config_all_live_keys(void)
{
    const char input[] =
        "kanjioff:\r\n"
        "edgewi:3\r\n"
        "edgedel:10\r\n"
        "effectmode:\r\n"
        "font:Arial\r\n";

    struct config_idx cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.kanjioff,   1);
    T_ASSERT_EQ_I(cfg.edgewi,     3);
    T_ASSERT_EQ_I(cfg.edgedel,    10);
    T_ASSERT_EQ_I(cfg.effectmode, 1);
    T_ASSERT_EQ_I(cfg.font_set,   1);
    T_ASSERT(strcmp(cfg.font_name, "Arial") == 0);
    return 0;
}

int test_tables_config_makefont_is_noop(void)
{
    /* Engine matches 8 bytes against "makefont" but assigns to no
     * global. A `makefont:...` line must leave everything at default. */
    const char input[] = "makefont:something\r\n";

    struct config_idx cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.kanjioff,   0);
    T_ASSERT_EQ_I(cfg.edgewi,     0);
    T_ASSERT_EQ_I(cfg.edgedel,    0);
    T_ASSERT_EQ_I(cfg.effectmode, 0);
    T_ASSERT_EQ_I(cfg.font_set,   0);
    return 0;
}

int test_tables_config_font_sjis(void)
{
    /* Shift-JIS bytes for "ＭＳ Ｐゴシック" — the default font name
     * shown in the vendor `config.idx`. */
    static const unsigned char input[] =
        "font:\x82\x6C\x82\x72\x20\x82\x6F\x83\x53\x83\x56\x83\x62\x83\x4E\r\n";

    struct config_idx cfg;
    tables_parse_config(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.font_set, 1);
    /* The buffer should contain the SJIS bytes verbatim, NUL-terminated. */
    T_ASSERT_MEM_EQ(cfg.font_name,
                    "\x82\x6C\x82\x72\x20\x82\x6F\x83\x53\x83\x56\x83\x62\x83\x4E",
                    15);
    T_ASSERT_EQ_I(cfg.font_name[15], '\0');
    return 0;
}

int test_tables_config_font_overlong_truncates(void)
{
    /* Build a font: line whose name exceeds CONFIG_FONT_NAME_CAP-1 chars.
     * Engine would over-write past its 256-byte buffer; our port
     * truncates safely to N-1 chars + NUL. */
    char input[CONFIG_FONT_NAME_CAP + 64];
    int offset = 0;
    memcpy(input + offset, "font:", 5); offset += 5;
    /* Fill with 'A' up to well past the cap. */
    int fill_len = CONFIG_FONT_NAME_CAP + 32;
    memset(input + offset, 'A', fill_len); offset += fill_len;
    input[offset++] = '\r';
    input[offset++] = '\n';

    struct config_idx cfg;
    tables_parse_config((const unsigned char *)input, (size_t)offset, &cfg);

    T_ASSERT_EQ_I(cfg.font_set, 1);
    T_ASSERT_EQ_I(cfg.font_name[CONFIG_FONT_NAME_CAP - 1], '\0');
    /* All bytes before the terminator should be 'A'. */
    for (int i = 0; i < CONFIG_FONT_NAME_CAP - 1; i++) {
        T_ASSERT_EQ_I(cfg.font_name[i], 'A');
    }
    return 0;
}

int test_tables_config_commented_lines_ignored(void)
{
    /* Vendor file's actual top: every interesting line commented. */
    const char input[] =
        "/font:Foo\r\n"
        "/kanjioff:\r\n"
        "/edgewi:9\r\n"
        "/edgedel:9\r\n"
        "/effectmode:\r\n";

    struct config_idx cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.kanjioff,   0);
    T_ASSERT_EQ_I(cfg.edgewi,     0);
    T_ASSERT_EQ_I(cfg.edgedel,    0);
    T_ASSERT_EQ_I(cfg.effectmode, 0);
    T_ASSERT_EQ_I(cfg.font_set,   0);
    return 0;
}

int test_tables_config_vendor_shape(void)
{
    /* Approximates the shipping `data/config.idx`: font/kanjioff/
     * effectmode commented out, edgewi=2 + edgedel=6 active, plus
     * trailing helper-text comments that the parser should skip. */
    const char input[] =
        "/font:\x82\x6C\x82\x72\x20\x82\x6F\x83\x53\x83\x56\x83\x62\x83\x4E\r\n"
        "/kanjioff:\r\n"
        "edgewi:2\r\n"
        "edgedel:6\r\n"
        "/effectmode:\r\n"
        "\r\n"
        "\r\n"
        "\r\n"
        "/random Japanese helper text would go here\r\n"
        "/edgewi:2, 0..4 \r\n"
        "/edgedel:6, 0..15\r\n";

    struct config_idx cfg;
    run_parse(input, sizeof input - 1, &cfg);

    T_ASSERT_EQ_I(cfg.kanjioff,   0);
    T_ASSERT_EQ_I(cfg.edgewi,     2);
    T_ASSERT_EQ_I(cfg.edgedel,    6);
    T_ASSERT_EQ_I(cfg.effectmode, 0);
    T_ASSERT_EQ_I(cfg.font_set,   0);
    return 0;
}
