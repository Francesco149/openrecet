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

extern int test_tables_buysell_empty(void);
extern int test_tables_buysell_comments_only(void);
extern int test_tables_buysell_ok_toggle(void);
extern int test_tables_buysell_sjis_scalars(void);
extern int test_tables_buysell_msg_arrays(void);
extern int test_tables_buysell_no_trailing_newline(void);
extern int test_tables_buysell_embedded_null_terminates(void);
extern int test_tables_buysell_vendor_shape(void);

extern int test_tables_chara_empty(void);
extern int test_tables_chara_defaults_bit_exact(void);
extern int test_tables_chara_basic_record(void);
extern int test_tables_chara_lv100_alone(void);
extern int test_tables_chara_both_blocks_combined(void);
extern int test_tables_chara_comments_skipped(void);
extern int test_tables_chara_out_of_range_index_guarded(void);
extern int test_tables_chara_lv100_field_permutation(void);
extern int test_tables_chara_vendor_shape(void);

extern int test_tables_config_empty(void);
extern int test_tables_config_all_live_keys(void);
extern int test_tables_config_makefont_is_noop(void);
extern int test_tables_config_font_sjis(void);
extern int test_tables_config_font_overlong_truncates(void);
extern int test_tables_config_commented_lines_ignored(void);
extern int test_tables_config_vendor_shape(void);

extern int test_tables_model_basic_one_record(void);
extern int test_tables_model_comments_skipped(void);
extern int test_tables_model_empty(void);
extern int test_tables_model_fname_default_record_zero(void);
extern int test_tables_model_no_index_threads(void);
extern int test_tables_model_out_of_range_index_skipped(void);
extern int test_tables_model_overlong_fname_truncates(void);
extern int test_tables_model_repeated_slot_increments_count(void);
extern int test_tables_model_vendor_shape(void);

extern int test_tables_oder_empty(void);
extern int test_tables_oder_one_record(void);
extern int test_tables_oder_level_threads_through(void);
extern int test_tables_oder_sjis_attrs_all_16(void);
extern int test_tables_oder_english_attr_falls_through(void);
extern int test_tables_oder_tabs_are_skipped(void);
extern int test_tables_oder_line_cap_truncates(void);
extern int test_tables_oder_no_trailing_newline(void);
extern int test_tables_oder_vendor_shape(void);

extern int test_tables_snews_empty(void);
extern int test_tables_snews_name_table_basic(void);
extern int test_tables_snews_comments_and_blanks_skipped(void);
extern int test_tables_snews_dungeon_and_section(void);
extern int test_tables_snews_multiple_sections_in_dungeon(void);
extern int test_tables_snews_dungeon_transition_corrupts_prev(void);
extern int test_tables_snews_name_empty_value(void);
extern int test_tables_snews_name_overlong_truncates(void);
extern int test_tables_snews_entry_slot_overflow_dropped(void);
extern int test_tables_snews_vendor_shape(void);

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

    {"tables_buysell_empty",                  test_tables_buysell_empty},
    {"tables_buysell_comments_only",          test_tables_buysell_comments_only},
    {"tables_buysell_ok_toggle",              test_tables_buysell_ok_toggle},
    {"tables_buysell_sjis_scalars",           test_tables_buysell_sjis_scalars},
    {"tables_buysell_msg_arrays",             test_tables_buysell_msg_arrays},
    {"tables_buysell_no_trailing_newline",    test_tables_buysell_no_trailing_newline},
    {"tables_buysell_embedded_null_terminates", test_tables_buysell_embedded_null_terminates},
    {"tables_buysell_vendor_shape",           test_tables_buysell_vendor_shape},

    {"tables_chara_empty",                       test_tables_chara_empty},
    {"tables_chara_defaults_bit_exact",          test_tables_chara_defaults_bit_exact},
    {"tables_chara_basic_record",                test_tables_chara_basic_record},
    {"tables_chara_lv100_alone",                 test_tables_chara_lv100_alone},
    {"tables_chara_both_blocks_combined",        test_tables_chara_both_blocks_combined},
    {"tables_chara_comments_skipped",            test_tables_chara_comments_skipped},
    {"tables_chara_out_of_range_index_guarded",  test_tables_chara_out_of_range_index_guarded},
    {"tables_chara_lv100_field_permutation",     test_tables_chara_lv100_field_permutation},
    {"tables_chara_vendor_shape",                test_tables_chara_vendor_shape},

    {"tables_config_empty",                   test_tables_config_empty},
    {"tables_config_all_live_keys",           test_tables_config_all_live_keys},
    {"tables_config_makefont_is_noop",        test_tables_config_makefont_is_noop},
    {"tables_config_font_sjis",               test_tables_config_font_sjis},
    {"tables_config_font_overlong_truncates", test_tables_config_font_overlong_truncates},
    {"tables_config_commented_lines_ignored", test_tables_config_commented_lines_ignored},
    {"tables_config_vendor_shape",            test_tables_config_vendor_shape},

    {"tables_model_empty",                        test_tables_model_empty},
    {"tables_model_basic_one_record",             test_tables_model_basic_one_record},
    {"tables_model_no_index_threads",             test_tables_model_no_index_threads},
    {"tables_model_comments_skipped",             test_tables_model_comments_skipped},
    {"tables_model_fname_default_record_zero",    test_tables_model_fname_default_record_zero},
    {"tables_model_repeated_slot_increments_count", test_tables_model_repeated_slot_increments_count},
    {"tables_model_overlong_fname_truncates",     test_tables_model_overlong_fname_truncates},
    {"tables_model_out_of_range_index_skipped",   test_tables_model_out_of_range_index_skipped},
    {"tables_model_vendor_shape",                 test_tables_model_vendor_shape},

    {"tables_oder_empty",                     test_tables_oder_empty},
    {"tables_oder_one_record",                test_tables_oder_one_record},
    {"tables_oder_level_threads_through",     test_tables_oder_level_threads_through},
    {"tables_oder_sjis_attrs_all_16",         test_tables_oder_sjis_attrs_all_16},
    {"tables_oder_english_attr_falls_through", test_tables_oder_english_attr_falls_through},
    {"tables_oder_tabs_are_skipped",          test_tables_oder_tabs_are_skipped},
    {"tables_oder_line_cap_truncates",        test_tables_oder_line_cap_truncates},
    {"tables_oder_no_trailing_newline",       test_tables_oder_no_trailing_newline},
    {"tables_oder_vendor_shape",              test_tables_oder_vendor_shape},

    {"tables_snews_empty",                              test_tables_snews_empty},
    {"tables_snews_name_table_basic",                   test_tables_snews_name_table_basic},
    {"tables_snews_comments_and_blanks_skipped",        test_tables_snews_comments_and_blanks_skipped},
    {"tables_snews_dungeon_and_section",                test_tables_snews_dungeon_and_section},
    {"tables_snews_multiple_sections_in_dungeon",       test_tables_snews_multiple_sections_in_dungeon},
    {"tables_snews_dungeon_transition_corrupts_prev",   test_tables_snews_dungeon_transition_corrupts_prev},
    {"tables_snews_name_empty_value",                   test_tables_snews_name_empty_value},
    {"tables_snews_name_overlong_truncates",            test_tables_snews_name_overlong_truncates},
    {"tables_snews_entry_slot_overflow_dropped",        test_tables_snews_entry_slot_overflow_dropped},
    {"tables_snews_vendor_shape",                       test_tables_snews_vendor_shape},
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
