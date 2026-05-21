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
    X(tables_enemy_drop_resolves_via_callback) \
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
    X(tables_gousei_vendor_shape) \
    X(tables_gousei_resolves_via_item_state) \
    \
    X(tables_kyaku_empty) \
    X(tables_kyaku_comments_and_blanks_skipped) \
    X(tables_kyaku_header_singular_only) \
    X(tables_kyaku_header_with_plural) \
    X(tables_kyaku_attr_x_y) \
    X(tables_kyaku_attr_empty_value_keeps_defaults) \
    X(tables_kyaku_budget_range) \
    X(tables_kyaku_budget_empty_no_write) \
    X(tables_kyaku_like_kind_resolver_hit) \
    X(tables_kyaku_like_kind_null_resolver_skips) \
    X(tables_kyaku_like_kind_cap_at_20) \
    X(tables_kyaku_like_attr_mask_sjis_tokens) \
    X(tables_kyaku_dislikes_orphan_match_is_noop) \
    X(tables_kyaku_file_path) \
    X(tables_kyaku_activity_time_mask_four_tokens) \
    X(tables_kyaku_activity_time_partial_tokens) \
    X(tables_kyaku_activity_time_unknown_token_ignored) \
    X(tables_kyaku_atoi_scalars) \
    X(tables_kyaku_lines_before_header_dropped) \
    X(tables_kyaku_no_trailing_newline) \
    X(tables_kyaku_multi_customer_threading) \
    X(tables_kyaku_resolves_via_item_category) \
    X(tables_kyaku_vendor_shape) \
    \
    X(tables_event_empty_seeds_default) \
    X(tables_event_layout_byte_offsets) \
    X(tables_event_comments_and_blanks_skipped) \
    X(tables_event_basic_hiroba_record) \
    X(tables_event_prereq_hex_and_minus) \
    X(tables_event_time_first_and_max) \
    X(tables_event_time_max_clamps_to_first_no_higher) \
    X(tables_event_time_unknown_tokens_only) \
    X(tables_event_loop_min_atoi) \
    X(tables_event_day_pairs_up_to_20) \
    X(tables_event_category_dispatch) \
    X(tables_event_data_line_before_header_goes_to_hiroba) \
    X(tables_event_decay_or_max_zero_for_parsed) \
    X(tables_event_no_trailing_newline) \
    X(tables_event_vendor_shape) \
    \
    X(tables_news_empty) \
    X(tables_news_layout_byte_offsets) \
    X(tables_news_comments_and_blanks_skipped) \
    X(tables_news_special_attr_basic) \
    X(tables_news_sjis_attr_mask) \
    X(tables_news_category_resolver_hit) \
    X(tables_news_item_resolver_hit) \
    X(tables_news_lookup_chain_precedence) \
    X(tables_news_days_range_optional) \
    X(tables_news_dash_row) \
    X(tables_news_target_group_sticky) \
    X(tables_news_period_sticky) \
    X(tables_news_period_defaults_apply_before_header) \
    X(tables_news_period_missing_dash_leaves_end_unchanged) \
    X(tables_news_no_trailing_newline) \
    X(tables_news_body_keeps_trailing_cr_on_crlf) \
    X(tables_news_body_strips_on_lf_only) \
    X(tables_news_no_resolver_misses_silently) \
    X(tables_news_max_records_cap) \
    X(tables_news_vendor_shape) \
    \
    X(tables_enemylist_layout_byte_offsets) \
    X(tables_enemylist_empty) \
    X(tables_enemylist_comments_and_blanks_skipped) \
    X(tables_enemylist_wisp_basic) \
    X(tables_enemylist_wisp_empty_value) \
    X(tables_enemylist_wisp10_silent_drop) \
    X(tables_enemylist_wisp_unknown_resolves_minus_one) \
    X(tables_enemylist_dungeon_header_resets_section) \
    X(tables_enemylist_f_no_dash_single_floor) \
    X(tables_enemylist_f_empty_skips) \
    X(tables_enemylist_multiple_f_lines_thread) \
    X(tables_enemylist_enemy_basic_one_drop) \
    X(tables_enemylist_enemy_multi_drops) \
    X(tables_enemylist_variant_suffix) \
    X(tables_enemylist_count_suffix) \
    X(tables_enemylist_longest_prefix_wins) \
    X(tables_enemylist_unknown_enemy_skipped) \
    X(tables_enemylist_drop_reset_per_line) \
    X(tables_enemylist_no_resolver_drops_minus_one) \
    X(tables_enemylist_no_trailing_newline) \
    X(tables_enemylist_enemies_thread_across_f_blocks) \
    X(tables_enemylist_vendor_shape) \
    \
    X(tables_stage_layout_byte_offsets) \
    X(tables_stage_empty) \
    X(tables_stage_lines_before_first_header_dropped) \
    X(tables_stage_comments_and_blanks_skipped) \
    X(tables_stage_defaults_applied_on_open) \
    X(tables_stage_id_dispatch_short_keys) \
    X(tables_stage_id_dispatch_long_keys) \
    X(tables_stage_id_unknown_falls_back_to_1_16) \
    X(tables_stage_int_fields) \
    X(tables_stage_float_fields) \
    X(tables_stage_flag_fields) \
    X(tables_stage_string_fields) \
    X(tables_stage_map_slots_thread) \
    X(tables_stage_map_overflow_safe) \
    X(tables_stage_mapcamera_slots_thread) \
    X(tables_stage_fog_pair) \
    X(tables_stage_fog_one_value_keeps_default_second) \
    X(tables_stage_int_triples) \
    X(tables_stage_float_triples_colon) \
    X(tables_stage_maplight_d_a_space_pairs) \
    X(tables_stage_sunpos_numeric_sets_mode_1) \
    X(tables_stage_sunpos_off_sets_mode_0) \
    X(tables_stage_sunset_numeric_sets_mode_2) \
    X(tables_stage_sunset_off_broken_quirk_36) \
    X(tables_stage_moonpos_quirk_35_shares_coords_keeps_sun_mode) \
    X(tables_stage_multiple_records_thread) \
    X(tables_stage_no_trailing_newline) \
    X(tables_stage_vendor_shape_minified) \
    \
    X(recet_ini_empty_applies_defaults) \
    X(recet_ini_default_pad_skill_tables) \
    X(recet_ini_screen_lookup_all_branches) \
    X(recet_ini_screen_drives_width_height) \
    X(recet_ini_all_setup_scalars) \
    X(recet_ini_option_pad_grid) \
    X(recet_ini_section_and_key_case_insensitive) \
    X(recet_ini_comments_and_blanks_skipped) \
    X(recet_ini_whitespace_around_equals) \
    X(recet_ini_bgnodisp_mirrors_easydisp) \
    X(recet_ini_volume_clamp) \
    X(recet_ini_unknown_keys_and_sections_ignored) \
    X(recet_ini_no_trailing_newline) \
    X(recet_ini_vendor_shape) \
    \
    X(rng_initial_seed_is_one) \
    X(rng_msvc_rand_sequence_from_seed_1) \
    X(rng_seed_resets_state) \
    X(rng_next_unit_range_zero_to_just_under_one) \
    X(rng_next_unit_seed_1_first_values) \
    X(rng_compute_seed_year_2000_jan_1) \
    X(rng_compute_seed_year_range_rejects) \
    X(rng_compute_seed_leap_bump_post_february) \
    X(rng_compute_seed_is_deterministic) \
    \
    X(math_vec3_normalize_3_4_0) \
    X(math_vec3_normalize_in_place) \
    X(math_lookat_z_back_camera) \
    X(math_lookat_off_axis) \
    X(math_perspective_fov_pi_over_2_aspect_1) \
    X(math_perspective_aspect_changes_w_only) \
    X(math_mul_identity_identity_is_identity) \
    X(math_mul_handles_output_alias) \
    X(math_mul_diagonal_scaling) \
    \
    X(prewindow_named_globals_set) \
    X(prewindow_object_table_y_set_first_last) \
    X(prewindow_object_table_other_fields_zero) \
    X(prewindow_particles_alive_flag_all_one) \
    X(prewindow_particles_pos_within_expected_range) \
    X(prewindow_particle_zero_deterministic_from_seed_1) \
    X(prewindow_advances_rng_by_600) \
    X(prewindow_proj_matrix_is_finite) \
    X(prewindow_view_is_degenerate_nan_or_inf) \
    \
    X(tick_init_zeros_state) \
    X(tick_speed_thresholds_match_rdata) \
    X(tick_first_frame_huge_delta_ticks_once) \
    X(tick_speed_one_runs_sim_twice) \
    X(tick_speed_four_runs_sim_five_times) \
    X(tick_delayed_far_from_frame_returns_5ms_sleep) \
    X(tick_delayed_close_to_frame_busy_spins) \
    X(tick_delayed_input_polls_after_frame_boundary) \
    X(tick_state_one_skips_sim_render) \
    X(tick_state_two_transitions_to_one) \
    X(tick_no_device_aborts_after_sim_before_render) \
    X(tick_state_alt_copies_state_seed) \
    X(tick_per_frame_flags_clear_on_ticked) \
    X(tick_per_frame_flags_not_cleared_on_delayed) \
    X(tick_null_callbacks_are_safe) \
    X(tick_adaptive_sleep_scales_with_remaining) \
    X(tick_steady_state_60fps_carries_residue) \
    X(tick_pending_speed_latches_at_top_of_frame) \
    \
    X(input_joy_decode_centered_pov_no_axes_no_buttons) \
    X(input_joy_decode_pov_eight_directions) \
    X(input_joy_decode_stick_axes_set_dpad) \
    X(input_joy_decode_pov_axes_or_together) \
    X(input_joy_decode_buttons_only_high_bit_matters) \
    X(input_apply_joystick_block_default_pad_one) \
    X(input_apply_joystick_block_virtual_base_offsets_per_joy) \
    X(input_apply_joystick_block_skill_slots_set_byte_plus_one) \
    X(input_apply_joystick_block_zero_binding_never_matches) \
    X(input_apply_joystick_block_ors_into_existing_mask) \
    X(input_apply_keyboard_block_default_pad_zero) \
    X(input_apply_keyboard_block_z_key_for_button_a) \
    X(input_apply_keyboard_block_low_bit_set_does_not_count) \
    X(input_apply_keyboard_block_out_of_range_binding_skipped) \
    X(input_apply_keyboard_block_skill_slots) \
    X(input_bindings_load_default_ini_matches_engine_layout) \
    X(input_bindings_load_round_trips_modified_ini) \
    X(input_dik_table_matches_vendor_dump) \
    X(input_binding_mask_dpad_face_buttons_layout) \
    X(input_state_stride_matches_engine)

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
