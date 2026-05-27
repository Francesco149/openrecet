/*
 * test_scene_new_game.c — tests for src/scene_new_game.{c,h}.
 *
 * Covers each of the three engine helpers ported in this chip
 * (FUN_0049de18 / FUN_004060ff / FUN_004682d0):
 *
 *   - the clear writes the expected sentinel/zero pattern
 *   - the getter round-trips a non-zero value the engine setter
 *     would have written
 *   - the clear is idempotent (calling it twice keeps state cleared)
 */

#include "t.h"
#include "scene_new_game.h"

/* ─── FUN_0049de18 + getter/setter (DAT_09643684) ────────────────────── */

int test_scene_new_game_save_dialog_state_clear_zeroes(void)
{
    scene_new_game_save_dialog_state_set(0x42);
    T_ASSERT_EQ_I(scene_new_game_save_dialog_state_get(), 0x42);
    scene_new_game_clear_save_dialog_state();
    T_ASSERT_EQ_I(scene_new_game_save_dialog_state_get(), 0);
    return 0;
}

int test_scene_new_game_save_dialog_state_setter_roundtrip(void)
{
    scene_new_game_save_dialog_state_set(0x12345678);
    T_ASSERT_EQ_I(scene_new_game_save_dialog_state_get(), 0x12345678);
    scene_new_game_save_dialog_state_set(-1);
    T_ASSERT_EQ_I(scene_new_game_save_dialog_state_get(), -1);
    /* leave cleared so the suite is order-independent */
    scene_new_game_clear_save_dialog_state();
    return 0;
}

/* ─── FUN_004060ff (16-global UI scratch reset) ───────────────────────── */

int test_scene_new_game_ui_scratch_clear_sentinels_first_two(void)
{
    /* Engine writes 0xffffffff to DAT_00529704 and DAT_00529708 —
     * the "no selection" sentinel pattern.  Everything else gets 0. */
    scene_new_game_clear_ui_scratch();
    T_ASSERT_EQ_I(scene_new_game_ui_scratch_dat_00529704_get(),
                  (int32_t)0xffffffff);
    T_ASSERT_EQ_I(scene_new_game_ui_scratch_dat_00529708_get(),
                  (int32_t)0xffffffff);
    T_ASSERT_EQ_I(scene_new_game_ui_scratch_dat_00648258_get(), 0);
    return 0;
}

int test_scene_new_game_ui_scratch_clear_idempotent(void)
{
    scene_new_game_clear_ui_scratch();
    scene_new_game_clear_ui_scratch();
    T_ASSERT_EQ_I(scene_new_game_ui_scratch_dat_00529704_get(),
                  (int32_t)0xffffffff);
    T_ASSERT_EQ_I(scene_new_game_ui_scratch_dat_00648258_get(), 0);
    return 0;
}

/* ─── FUN_004682d0 (DAT_0734b9a0) ─────────────────────────────────────── */

int test_scene_new_game_stage_load_pulse_b_clear_zeroes(void)
{
    /* No engine setter exposed; we can't seed a non-zero value through
     * the API.  The clear is still observable as "the getter returns 0
     * after the clear" — combined with the static's BSS-zero init that
     * also implies the clear didn't crash. */
    scene_new_game_clear_stage_load_pulse_b();
    T_ASSERT_EQ_I(scene_new_game_stage_load_pulse_b_get(), 0);
    return 0;
}
