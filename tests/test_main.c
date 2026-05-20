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
 *
 * Registration: add one X(name) line per test to TESTS below. The X-macro
 * expands twice — once to emit the extern decl, once to populate the
 * dispatch table — so the test name appears exactly once in this file
 * and the registry can't drift from the declarations.
 */
#include "t.h"

typedef int (*test_fn)(void);

struct test_case {
    const char *name;
    test_fn     fn;
};

/* ─── test list ────────────────────────────────────────────────────────────
 * Each X(n) declares `extern int test_##n(void);` and registers it as
 * {"n", test_##n}. Group blank lines are cosmetic only.
 */
#define TESTS(X) \
    X(bmp_basic_24bit) \
    X(bmp_color_key) \
    X(bmp_color_key_disabled) \
    X(bmp_top_down) \
    X(bmp_32bit) \
    X(bmp_reject_bad_magic) \
    X(bmp_reject_truncated) \
    X(bmp_reject_unsupported_compression) \
    X(bmp_reject_palettized) \
    \
    X(tga_type2_24bit_bottom_up) \
    X(tga_type2_32bit_top_down) \
    X(tga_type10_rle_single_run) \
    X(tga_type10_rle_mixed) \
    X(tga_type10_rle_split_pixel) \
    X(tga_reject_unsupported_type) \
    X(tga_reject_truncated_uncompressed) \
    X(tga_reject_truncated_rle) \
    \
    X(bmp_lzw_round_trip_vendor) \
    \
    X(lnkdatas_hash_empty) \
    X(lnkdatas_hash_test_vector) \
    X(lnkdatas_hash_vendor) \
    \
    X(lnk_lzss_single_literal) \
    X(lnk_lzss_back_reference_short) \
    X(lnk_lzss_back_reference_extended) \
    X(lnk_lzss_self_overlap) \
    X(lnk_lzss_end_of_stream_mid_ctrl) \
    X(lnk_lzss_back_high_bits) \
    X(lnk_lzss_mixed_flags) \
    X(lnk_lzss_vendor_round_trip) \
    \
    X(tables_buysell_empty) \
    X(tables_buysell_comments_only) \
    X(tables_buysell_ok_toggle) \
    X(tables_buysell_sjis_scalars) \
    X(tables_buysell_msg_arrays) \
    X(tables_buysell_no_trailing_newline) \
    X(tables_buysell_embedded_null_terminates) \
    X(tables_buysell_vendor_shape) \
    \
    X(tables_chara_empty) \
    X(tables_chara_defaults_bit_exact) \
    X(tables_chara_basic_record) \
    X(tables_chara_lv100_alone) \
    X(tables_chara_both_blocks_combined) \
    X(tables_chara_comments_skipped) \
    X(tables_chara_out_of_range_index_guarded) \
    X(tables_chara_lv100_field_permutation) \
    X(tables_chara_vendor_shape) \
    \
    X(tables_enemy_init_pre_baked_names) \
    X(tables_enemy_basic_record) \
    X(tables_enemy_longest_prefix_wins) \
    X(tables_enemy_shorter_prefix_when_no_longer_match) \
    X(tables_enemy_comments_and_blanks_skipped) \
    X(tables_enemy_per_line_drop_reset) \
    X(tables_enemy_unknown_name_silently_skipped) \
    X(tables_enemy_placeholder_records_skip_match) \
    X(tables_enemy_no_trailing_newline) \
    X(tables_enemy_vendor_shape) \
    \
    X(tables_config_empty) \
    X(tables_config_all_live_keys) \
    X(tables_config_makefont_is_noop) \
    X(tables_config_font_sjis) \
    X(tables_config_font_overlong_truncates) \
    X(tables_config_commented_lines_ignored) \
    X(tables_config_vendor_shape) \
    \
    X(tables_model_empty) \
    X(tables_model_basic_one_record) \
    X(tables_model_no_index_threads) \
    X(tables_model_comments_skipped) \
    X(tables_model_fname_default_record_zero) \
    X(tables_model_repeated_slot_increments_count) \
    X(tables_model_overlong_fname_truncates) \
    X(tables_model_out_of_range_index_skipped) \
    X(tables_model_vendor_shape) \
    \
    X(tables_oder_empty) \
    X(tables_oder_one_record) \
    X(tables_oder_level_threads_through) \
    X(tables_oder_sjis_attrs_all_16) \
    X(tables_oder_english_attr_falls_through) \
    X(tables_oder_tabs_are_skipped) \
    X(tables_oder_line_cap_truncates) \
    X(tables_oder_no_trailing_newline) \
    X(tables_oder_vendor_shape) \
    \
    X(tables_snews_empty) \
    X(tables_snews_name_table_basic) \
    X(tables_snews_comments_and_blanks_skipped) \
    X(tables_snews_dungeon_and_section) \
    X(tables_snews_multiple_sections_in_dungeon) \
    X(tables_snews_dungeon_transition_corrupts_prev) \
    X(tables_snews_name_empty_value) \
    X(tables_snews_name_overlong_truncates) \
    X(tables_snews_entry_slot_overflow_dropped) \
    X(tables_snews_vendor_shape) \
    \
    X(tables_tuto_empty) \
    X(tables_tuto_blanks_and_comments) \
    X(tables_tuto_chr0_basic) \
    X(tables_tuto_chr1_basic) \
    X(tables_tuto_no_arg_opcodes) \
    X(tables_tuto_goto_7_ints) \
    X(tables_tuto_goto_short_args_zero) \
    X(tables_tuto_bun0_7_ints) \
    X(tables_tuto_nedan_alias_takaku) \
    X(tables_tuto_nebiki_neage) \
    X(tables_tuto_shoki_kingaku_kettei) \
    X(tables_tuto_aitemu) \
    X(tables_tuto_kensen_7_ints) \
    X(tables_tuto_id_minus_one_sentinel) \
    X(tables_tuto_id_below_minus_one_text_only) \
    X(tables_tuto_file_index_stride) \
    X(tables_tuto_overflows_cap) \
    X(tables_tuto_vendor_like_shape) \
    \
    X(tables_item_empty) \
    X(tables_item_comments_and_blanks_skipped) \
    X(tables_item_basic_record_no_plural) \
    X(tables_item_basic_record_with_plural) \
    X(tables_item_full_stat_fields) \
    X(tables_item_category_header_then_record) \
    X(tables_item_category_threads_to_correct_index) \
    X(tables_item_attr_mask_with_category) \
    X(tables_item_audience_all_via_zen) \
    X(tables_item_audience_male_composite) \
    X(tables_item_audience_recette_only) \
    X(tables_item_audience_empty_field_is_all) \
    X(tables_item_stock_zaiko_basic) \
    X(tables_item_stock_da_x10_quirk) \
    X(tables_item_indent_space_line_skipped) \
    X(tables_item_unknown_line_skipped) \
    X(tables_item_out_of_range_id_dropped) \
    X(tables_item_no_trailing_newline) \
    X(tables_item_description_in_line1_and_line2) \
    X(tables_item_description_slash_terminates_line2) \
    X(tables_item_resolver_finds_singular) \
    X(tables_item_max_records_cap) \
    X(tables_item_vendor_shape) \
    \
    X(tables_gousei_empty) \
    X(tables_gousei_comments_and_blanks_skipped) \
    X(tables_gousei_basic_recipe) \
    X(tables_gousei_rank_header) \
    X(tables_gousei_recipe_before_rank_is_rank_zero) \
    X(tables_gousei_prefix_discarded) \
    X(tables_gousei_three_ingredients) \
    X(tables_gousei_five_ingredients) \
    X(tables_gousei_null_resolver_yields_minus_one) \
    X(tables_gousei_unknown_name_resolves_to_empty) \
    X(tables_gousei_count_at_eol_no_trailing_colon) \
    X(tables_gousei_no_trailing_newline) \
    X(tables_gousei_max_records_cap) \
    X(tables_gousei_embedded_nul_early_exit) \
    X(tables_gousei_vendor_shape)

#define T_DECL(n) extern int test_##n(void);
TESTS(T_DECL)
#undef T_DECL

static struct test_case g_tests[] = {
#define T_ENTRY(n) { #n, test_##n },
    TESTS(T_ENTRY)
#undef T_ENTRY
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
