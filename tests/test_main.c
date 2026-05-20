/*
 * test_main.c — unit-test driver for the OpenRecet decoders.
 *
 *   make -C tests run             # all tests
 *   make -C tests run -- bmp      # filter by name substring (TODO: passthrough)
 *   ./build/run_tests bmp         # ...or invoke directly
 *
 * Test outcomes:
 *   0 pass, 1 fail (printed message), 2 skip (missing vendor data, etc.)
 *
 * Process exit is 0 iff zero failures. Skips do not count as failures.
 */
#include "t.h"

typedef int (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn     fn;
};

/* ─── forward decls — keep alphabetized per file ───────────────────────── */
extern int test_bmp_basic_24bit(void);
extern int test_bmp_color_key(void);
extern int test_bmp_color_key_disabled(void);
extern int test_bmp_top_down(void);
extern int test_bmp_32bit(void);
extern int test_bmp_reject_bad_magic(void);
extern int test_bmp_reject_truncated(void);
extern int test_bmp_reject_unsupported_compression(void);
extern int test_bmp_reject_palettized(void);

extern int test_tga_type2_24bit_bottom_up(void);
extern int test_tga_type2_32bit_top_down(void);
extern int test_tga_type10_rle_single_run(void);
extern int test_tga_type10_rle_mixed(void);
extern int test_tga_type10_rle_split_pixel(void);
extern int test_tga_reject_unsupported_type(void);
extern int test_tga_reject_truncated_uncompressed(void);
extern int test_tga_reject_truncated_rle(void);

extern int test_bmp_lzw_round_trip_vendor(void);

extern int test_lnkdatas_hash_empty(void);
extern int test_lnkdatas_hash_test_vector(void);
extern int test_lnkdatas_hash_vendor(void);

extern int test_lnk_lzss_single_literal(void);
extern int test_lnk_lzss_back_reference_short(void);
extern int test_lnk_lzss_back_reference_extended(void);
extern int test_lnk_lzss_self_overlap(void);
extern int test_lnk_lzss_end_of_stream_mid_ctrl(void);
extern int test_lnk_lzss_back_high_bits(void);
extern int test_lnk_lzss_mixed_flags(void);
extern int test_lnk_lzss_vendor_round_trip(void);

/* ─── registry ─────────────────────────────────────────────────────────── */
static struct test_case g_tests[] = {
    {"bmp_basic_24bit",                  test_bmp_basic_24bit},
    {"bmp_color_key",                    test_bmp_color_key},
    {"bmp_color_key_disabled",           test_bmp_color_key_disabled},
    {"bmp_top_down",                     test_bmp_top_down},
    {"bmp_32bit",                        test_bmp_32bit},
    {"bmp_reject_bad_magic",             test_bmp_reject_bad_magic},
    {"bmp_reject_truncated",             test_bmp_reject_truncated},
    {"bmp_reject_unsupported_compression", test_bmp_reject_unsupported_compression},
    {"bmp_reject_palettized",            test_bmp_reject_palettized},

    {"tga_type2_24bit_bottom_up",        test_tga_type2_24bit_bottom_up},
    {"tga_type2_32bit_top_down",         test_tga_type2_32bit_top_down},
    {"tga_type10_rle_single_run",        test_tga_type10_rle_single_run},
    {"tga_type10_rle_mixed",             test_tga_type10_rle_mixed},
    {"tga_type10_rle_split_pixel",       test_tga_type10_rle_split_pixel},
    {"tga_reject_unsupported_type",      test_tga_reject_unsupported_type},
    {"tga_reject_truncated_uncompressed", test_tga_reject_truncated_uncompressed},
    {"tga_reject_truncated_rle",         test_tga_reject_truncated_rle},

    {"bmp_lzw_round_trip_vendor",        test_bmp_lzw_round_trip_vendor},

    {"lnkdatas_hash_empty",              test_lnkdatas_hash_empty},
    {"lnkdatas_hash_test_vector",        test_lnkdatas_hash_test_vector},
    {"lnkdatas_hash_vendor",             test_lnkdatas_hash_vendor},

    {"lnk_lzss_single_literal",          test_lnk_lzss_single_literal},
    {"lnk_lzss_back_reference_short",    test_lnk_lzss_back_reference_short},
    {"lnk_lzss_back_reference_extended", test_lnk_lzss_back_reference_extended},
    {"lnk_lzss_self_overlap",            test_lnk_lzss_self_overlap},
    {"lnk_lzss_end_of_stream_mid_ctrl",  test_lnk_lzss_end_of_stream_mid_ctrl},
    {"lnk_lzss_back_high_bits",          test_lnk_lzss_back_high_bits},
    {"lnk_lzss_mixed_flags",             test_lnk_lzss_mixed_flags},
    {"lnk_lzss_vendor_round_trip",       test_lnk_lzss_vendor_round_trip},
};

int main(int argc, char *argv[])
{
    int total   = (int)(sizeof(g_tests) / sizeof(g_tests[0]));
    int passed  = 0, failed = 0, skipped = 0;
    const char *filter = (argc > 1) ? argv[1] : NULL;

    for (int i = 0; i < total; i++) {
        if (filter && !strstr(g_tests[i].name, filter)) continue;
        printf("run  %s\n", g_tests[i].name);
        int rc = g_tests[i].fn();
        if      (rc == 0) { printf("pass %s\n", g_tests[i].name); passed++;  }
        else if (rc == 2) { printf("skip %s\n", g_tests[i].name); skipped++; }
        else              { printf("FAIL %s\n", g_tests[i].name); failed++;  }
    }
    int run_count = passed + failed + skipped;
    printf("\n%d passed, %d failed, %d skipped (%d run, %d registered)\n",
           passed, failed, skipped, run_count, total);
    return failed ? 1 : 0;
}
