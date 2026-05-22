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
    X(input_state_stride_matches_engine) \
    \
    X(input_trace_parse_single_line) \
    X(input_trace_parse_sparse_three_lines) \
    X(input_trace_parse_decimal_buttons) \
    X(input_trace_parse_skips_comments_and_blank_lines) \
    X(input_trace_parse_buttons_key_first_also_works) \
    X(input_trace_parse_rejects_out_of_order_frames) \
    X(input_trace_parse_rejects_duplicate_frames) \
    X(input_trace_parse_rejects_unknown_key) \
    X(input_trace_parse_rejects_missing_buttons) \
    X(input_trace_parse_rejects_mask_above_16_bit) \
    X(input_trace_parse_empty_buffer) \
    X(input_trace_lookup_before_first_returns_zero) \
    X(input_trace_lookup_holds_between_entries) \
    X(input_trace_lookup_empty_trace) \
    X(input_trace_load_round_trips_real_file) \
    X(input_trace_load_missing_file_returns_zero) \
    X(input_trace_record_emits_first_frame_and_changes) \
    X(input_trace_record_reopen_truncates) \
    X(input_trace_record_when_closed_is_noop) \
    X(input_trace_record_open_rejects_null) \
    \
    X(render_quad_init_seeds_z_rhw_specular) \
    X(render_quad_add_one_emits_six_vertices) \
    X(render_quad_uv_half_texel_inset_asymmetry) \
    X(render_quad_scale_widens_and_offsets) \
    X(render_quad_offset_shifts_top_left) \
    X(render_quad_top_left_truncated_to_int) \
    X(render_quad_returns_zero_when_full) \
    X(render_quad_rejects_zero_tex_dim) \
    X(render_quad_reset_keeps_z_rhw_prefill) \
    X(render_quad_init_zero_screen_w_defaults_640) \
    \
    X(scene_title_assets_count_is_seven) \
    X(scene_title_assets_paths_match_pe) \
    X(scene_title_assets_sizes_power_of_two) \
    X(scene_title_assets_sizes_match_engine) \
    X(scene_title_menu_fresh_boot_4_items) \
    X(scene_title_menu_has_save_no_adv8_6_items) \
    X(scene_title_menu_has_save_and_score_7_items) \
    X(scene_title_menu_full_unlock_8_items) \
    X(scene_title_menu_hidden_char_only_5_items) \
    X(scene_title_menu_survival_requires_both_flags) \
    \
    X(scene_title_anim_init_fresh_seeds_folding_out) \
    X(scene_title_sim_frame_counter_advances_on_idle) \
    X(scene_title_sim_pulse_phase_ticks_every_frame) \
    X(scene_title_sim_cursor_anim_clamps_at_zero) \
    X(scene_title_sim_down_held_wraps_cursor) \
    X(scene_title_sim_up_held_wraps_cursor_backwards) \
    X(scene_title_sim_a_pressed_starts_select_phase) \
    X(scene_title_sim_select_phase_pins_at_fifteen) \
    X(scene_title_sim_pending_action_default_is_none) \
    X(scene_title_sim_fade_counter_set_on_new_game) \
    X(scene_title_sim_pending_action_exit_on_exit_item) \
    X(scene_title_sim_fade_counter_advances_after_set) \
    X(scene_title_sim_cursor_input_ignored_while_select_pending) \
    X(scene_title_sim_cursor_pressed_only_no_held) \
    X(scene_title_sim_frame_counter_clamps_at_pre_movie_window) \
    X(scene_title_sim_null_guards) \
    \
    X(settings_a_on_options_transitions_to_state_2) \
    X(settings_input_gated_during_slide_in) \
    X(settings_down_wraps_cursor_mod_six) \
    X(settings_up_wraps_cursor_backwards) \
    X(settings_left_decrements_bgm) \
    X(settings_left_clamps_bgm_floor) \
    X(settings_right_clamps_bgm_ceiling) \
    X(settings_each_row_targets_correct_state) \
    X(settings_clear_row_consumes_a_press_no_exit) \
    X(settings_a_exits_clean_with_dirty_3) \
    X(settings_a_exits_dirty_with_dirty_2) \
    X(settings_b_also_exits) \
    X(settings_dirty_flag_cleared_on_re_entry) \
    X(settings_options_dispatch_does_not_set_pending_action) \
    X(settings_main_menu_other_actions_still_publish) \
    X(settings_reset_seeds_engine_defaults) \
    X(settings_slider3_clamps_to_range) \
    X(settings_slider4_clamps_to_range) \
    X(settings_round_trip_each_legal_value) \
    \
    X(save_bank_arena_sizing) \
    X(save_bank_bank_pointer_arithmetic) \
    X(save_bank_init_all_seeds_header_magic) \
    X(save_bank_init_all_seeds_slider_defaults) \
    X(save_bank_init_all_stamps_all_banks) \
    X(save_bank_init_all_is_idempotent) \
    X(save_bank_init_one_sets_money_and_objective) \
    X(save_bank_init_one_fills_item_slot_spans) \
    X(save_bank_init_one_mini_block) \
    X(save_bank_init_one_chara_records) \
    X(save_bank_init_one_consumes_8_rng_steps) \
    X(save_bank_checksum_detects_tamper) \
    X(save_bank_header_init_hook_fires_once_per_reset) \
    X(save_bank_header_slider_setters_clamp) \
    \
    X(sim_button_ring_first_press_sets_pressed) \
    X(sim_button_ring_held_clears_pressed_next_frame) \
    X(sim_button_ring_repeat_pulses_after_settle) \
    X(sim_button_ring_release_drops_held) \
    X(sim_button_ring_multiple_bits_independent) \
    X(sim_init_zeros_state) \
    X(sim_step_a_advances_frame_count) \
    X(sim_step_a_pipes_input_into_ring) \
    \
    X(music_init_engine_data_defaults) \
    X(music_select_title_bare_returns_track_zero) \
    X(music_select_title_bare_holds_until_fade_band) \
    X(music_select_title_fade_band_returns_none) \
    X(music_select_title_stop_sentinel) \
    X(music_select_title_post_stop_no_change) \
    X(music_select_title_submenu_open_uses_table_lookup) \
    X(music_select_title_invalid_language_falls_back_to_zero) \
    X(music_select_forced_override_wins) \
    X(music_select_pause_modal_routes_to_over) \
    X(music_select_pause_modal_other_b_not_one_skips_override) \
    X(music_select_state_7_returns_none) \
    X(music_select_state_9_no_quest_returns_none) \
    X(music_select_state_9_quest_pending_returns_fanfare) \
    X(music_select_town_states_return_track_one) \
    X(music_step_increments_frame_count) \
    X(music_step_title_bare_dispatches_track_zero_once) \
    X(music_step_speed_drops_to_0_75_at_state_10) \
    X(music_step_global_pause_blocks_dispatch) \
    X(music_step_se_stop_pending_sweeps_and_clears) \
    X(music_step_pending_fade_phase_latches) \
    X(music_step_no_modal_clears_fade_and_forced) \
    X(music_step_paused_b_keeps_forced_track) \
    X(music_step_target_volume_default_is_one) \
    X(music_step_target_volume_fade_band_decreases) \
    X(music_step_stop_sentinel_dispatches_track_minus_two) \
    X(music_step_forced_override_dispatches_overridden_track) \
    X(music_step_fade_phase_advances_progress) \
    X(music_step_fade_phase_one_walks_to_silence) \
    X(music_step_fade_phase_two_walks_to_loud) \
    X(music_step_no_fade_skips_apply_hook) \
    X(music_step_pending_fade_phase_drives_animation) \
    \
    X(audio_bgm_table_has_21_entries) \
    X(audio_bgm_table_well_known_indices) \
    X(audio_bgm_filename_bounds) \
    X(audio_one_shot_set_is_exact) \
    X(audio_music_bridge_fires_on_swap) \
    X(audio_music_bridge_skipped_when_null) \
    \
    X(audio_mci_buffer_size_is_4800_bytes) \
    X(audio_mci_clear_zeroes_buffer) \
    X(audio_mci_basic_copy_at_row_zero_col_zero) \
    X(audio_mci_nul_early_exit_writes_nothing) \
    X(audio_mci_row_and_column_indexing) \
    X(audio_mci_stops_at_first_nul_in_source) \
    X(audio_mci_full_80_byte_fill_no_terminator) \
    X(audio_mci_source_longer_than_cap_truncates) \
    X(audio_mci_repeated_record_at_same_slot_overwrites) \
    X(audio_mci_channel_offset_spans_into_next_row) \
    \
    X(audio_fade_frame_zero_is_hard_silence) \
    X(audio_fade_frame_nine_is_target) \
    X(audio_fade_frames_one_to_eight_monotonic_increasing) \
    X(audio_fade_intermediate_value_matches_reference) \
    X(audio_fade_target_threading_at_frame_five) \
    X(audio_fade_out_of_range_clamps) \
    X(audio_fade_slider_defaults_to_nine) \
    X(audio_fade_slider_set_clamps_to_0_9) \
    X(audio_fade_channel_centibel_matches_compute) \
    X(audio_fade_apply_calls_hook_with_centibel) \
    X(audio_fade_apply_skips_hook_when_unset) \
    X(audio_fade_apply_rejects_invalid_channel) \
    X(audio_fade_progress_phase1_starts_loud) \
    X(audio_fade_progress_phase1_ends_silent) \
    X(audio_fade_progress_phase1_monotonic_decreasing) \
    X(audio_fade_progress_phase2_starts_silent) \
    X(audio_fade_progress_phase2_ends_loud) \
    X(audio_fade_progress_phase2_monotonic_increasing) \
    X(audio_fade_progress_lower_slider_attenuates_peak) \
    X(audio_fade_progress_slider_clamped) \
    X(audio_fade_progress_progress_clamped_and_overshoot) \
    X(audio_fade_progress_degenerate_duration_falls_back_to_slider) \
    X(audio_fade_apply_progress_drives_hook) \
    X(audio_fade_apply_progress_rejects_invalid_channel) \
    \
    X(audio_trace_json_escape_passthrough_ascii) \
    X(audio_trace_json_escape_quote) \
    X(audio_trace_json_escape_backslash) \
    X(audio_trace_json_escape_newlines_and_tabs) \
    X(audio_trace_json_escape_non_ascii_to_u_form) \
    X(audio_trace_json_escape_truncates_safely) \
    X(audio_trace_json_escape_null_safe) \
    X(audio_trace_open_close_idempotent) \
    X(audio_trace_open_rejects_null) \
    X(audio_trace_emit_bgm_swap_writes_one_line) \
    X(audio_trace_emit_fade_start_writes_one_line) \
    X(audio_trace_emit_when_closed_is_noop) \
    \
    X(audio_se_table_has_110_entries) \
    X(audio_se_table_first_and_last_ids) \
    X(audio_se_table_out_of_order_swap_at_39_40) \
    X(audio_se_table_jumps_to_high_range_at_slot_69) \
    X(audio_se_table_skips_2c3) \
    X(audio_se_resource_id_bounds) \
    X(audio_se_table_resource_type_is_custom) \
    X(audio_play_se_rejects_out_of_range) \
    X(audio_play_se_emits_trace_event) \
    X(audio_play_se_no_trace_when_closed) \
    X(audio_se_table_matches_vendor_bytes) \
    \
    X(fade_reset_zeroes_state) \
    X(fade_phase1_start_seeds_state) \
    X(fade_phase_out_start_seeds_state) \
    X(fade_tick_idle_is_noop) \
    X(fade_tick_phase1_clamps_at_duration_plus_one) \
    X(fade_tick_phase_out_resets_at_end) \
    X(fade_is_done_idle_returns_zero) \
    X(fade_is_done_phase1_matches_duration) \
    X(fade_is_done_phase_out_never_returns_one) \
    X(fade_is_done_mode2_uses_0x1f_pin) \
    \
    X(scene_set_title_writes_title_and_zero_substate) \
    X(scene_post_fade_init_lands_in_ingame) \
    X(scene_post_fade_init_clears_substate) \
    X(scene_post_fade_init_starts_fade_in) \
    \
    X(font_init_zeros_state) \
    X(font_init_is_idempotent) \
    X(font_age_tick_advances_in_use_only) \
    X(font_age_tick_all_free_is_noop) \
    X(font_age_tick_does_not_touch_other_fields) \
    \
    X(font_atlas_record_size_is_40) \
    X(font_atlas_record_field_offsets) \
    X(font_atlas_special_table_size_is_576) \
    X(font_atlas_special_table_first_entry_is_fullwidth_space) \
    X(font_atlas_special_table_known_punctuation) \
    X(font_atlas_special_table_no_internal_nul) \
    X(font_atlas_padded_dim_zero_mod_4) \
    X(font_atlas_padded_dim_one_mod_4) \
    X(font_atlas_padded_dim_two_mod_4) \
    X(font_atlas_padded_dim_three_mod_4) \
    X(font_atlas_blit_zeroes_yield_zeroes) \
    X(font_atlas_blit_alpha_to_high_nibble_marker_in_low) \
    X(font_atlas_blit_clamps_alpha_at_15) \
    X(font_atlas_blit_keeps_alpha_zero_pixels_at_zero) \
    X(font_atlas_blit_skips_when_dst_too_small) \
    X(font_atlas_dilate_no_glyph_no_change) \
    X(font_atlas_dilate_glyph_body_unchanged) \
    X(font_atlas_dilate_neighbor_gets_intensity_15_within_edgewi) \
    X(font_atlas_dilate_neighbor_falloff_outside_edgewi) \
    X(font_atlas_dilate_skip_low_alpha_propagation) \
    X(font_atlas_dilate_does_not_overwrite_glyph_body) \
    X(font_atlas_dilate_neighbor_keeps_brighter_existing_halo) \
    X(font_atlas_pack_record_basic) \
    X(font_atlas_pack_record_zero_box_zero_pad) \
    X(font_atlas_load_missing_returns_zero) \
    X(font_atlas_load_basic_roundtrip) \
    X(font_atlas_load_rejects_bad_idx_size) \
    X(font_atlas_free_is_idempotent) \
    \
    X(font_codepoint_ascii_is_byte_value) \
    X(font_codepoint_high_byte_below_table_returns_none_if_missing) \
    X(font_codepoint_special_table_first_entry) \
    X(font_codepoint_special_table_known_punctuation) \
    X(font_codepoint_sjis_double_byte_at_boundary) \
    X(font_codepoint_sjis_double_byte_high_kanji) \
    X(font_slot_alloc_first_call_gets_slot_zero) \
    X(font_slot_alloc_match_returns_existing_resets_age) \
    X(font_slot_alloc_distinct_codepoints_get_distinct_slots) \
    X(font_slot_alloc_unknown_codepoint_returns_none) \
    X(font_slot_alloc_double_byte_codepoint_stores_both_bytes) \
    X(font_slot_alloc_match_double_byte_requires_both) \
    X(font_slot_alloc_eviction_via_age_gate) \
    X(font_slot_alloc_release_callback_fires_on_evict) \
    X(font_slot_alloc_no_callback_still_nulls_texture) \
    \
    X(font_upload_expand_transparent_byte) \
    X(font_upload_expand_body_pixel_white_full) \
    X(font_upload_expand_body_pixel_dim) \
    X(font_upload_expand_edge_only_pixel) \
    X(font_upload_expand_alpha_nibble_only) \
    X(font_upload_expand_edge_nibble_only)

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
