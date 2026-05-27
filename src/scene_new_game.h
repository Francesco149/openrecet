/*
 * scene_new_game.h — port of three small global-zero helpers the
 * engine fires in FUN_0049a59e's NEW GAME commit block (L63-74).
 *
 * Engine sources:
 *   FUN_0049de18 @ 0x49de18 (8 B)   — writes DAT_09643684 = 0
 *   FUN_004060ff @ 0x4060ff (90 B)  — resets 16 UI scratch globals
 *   FUN_004682d0 @ 0x4682d0 (8 B)   — writes DAT_0734b9a0 = 0
 *
 * All three fire exactly once per New Game press, in the order
 * scene_post_fade_init() calls them.  None of the cleared globals
 * has a consumer in the port today; storage is module-local so
 * future consumers can either port their own typed accessor or
 * wire here.  Probes provide the call_trace structural visibility
 * (retail frame 59 burst was previously invisible on port side).
 *
 * DAT_09643684 has a documented getter/setter pair in the engine
 * (FUN_0049de08 = read, FUN_0049de0e = write); exposed here as
 * scene_new_game_save_dialog_state_{get,set} for when the
 * title save-dialog subsystem catches up.
 */
#ifndef OPENRECET_SCENE_NEW_GAME_H
#define OPENRECET_SCENE_NEW_GAME_H

#include <stdint.h>

/* FUN_0049de18 — write DAT_09643684 = 0. */
void scene_new_game_clear_save_dialog_state(void);

/* FUN_0049de08 — return DAT_09643684. */
int32_t scene_new_game_save_dialog_state_get(void);

/* FUN_0049de0e — write DAT_09643684 = value. */
void scene_new_game_save_dialog_state_set(int32_t value);

/* FUN_004060ff — reset 16 scattered UI scratch globals.  The first
 * two get 0xffffffff (engine sentinel for "no selection"); the
 * remaining 14 get 0. */
void scene_new_game_clear_ui_scratch(void);

/* Internal accessors for the UI scratch block — exposed so tests
 * can assert the reset behaviour without exporting every global. */
int32_t scene_new_game_ui_scratch_dat_00529704_get(void);
int32_t scene_new_game_ui_scratch_dat_00529708_get(void);
int32_t scene_new_game_ui_scratch_dat_00648258_get(void);

/* FUN_004682d0 — write DAT_0734b9a0 = 0. */
void scene_new_game_clear_stage_load_pulse_b(void);

/* Test-only — return DAT_0734b9a0 (no engine getter; we expose one
 * so test_scene_new_game can verify the clear without making the
 * global non-static). */
int32_t scene_new_game_stage_load_pulse_b_get(void);

#endif /* OPENRECET_SCENE_NEW_GAME_H */
