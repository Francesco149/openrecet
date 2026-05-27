/*
 * scene_new_game.c — see scene_new_game.h for the chip writeup.
 */

#include "scene_new_game.h"

#include "call_trace.h"

/* ─── DAT_09643684 — save-dialog state ────────────────────────────────────
 *
 * Engine: a small word the title save-dialog subsystem uses as its
 * current state / selected slot.  Cleared on New Game press; written
 * by the save/load dialog state machine (FUN_0049de0e) to track the
 * highlighted slot.  No port consumer today.
 */
static int32_t g_save_dialog_state = 0;

void scene_new_game_clear_save_dialog_state(void)
{
    /* E.2 probe — FUN_0049de18 @ 0x49de18. */
    CALL_TRACE_ENTER(0x49de18u);

    g_save_dialog_state = 0;
}

int32_t scene_new_game_save_dialog_state_get(void)
{
    return g_save_dialog_state;
}

void scene_new_game_save_dialog_state_set(int32_t value)
{
    g_save_dialog_state = value;
}

/* ─── 16-global UI scratch block (FUN_004060ff) ───────────────────────────
 *
 * The engine writes 16 scattered globals here.  Two get the 0xffffffff
 * "no selection" sentinel; the rest get 0.  Without any port consumer
 * for these, storage is module-local — when subsystems referencing
 * 0x00529704 / 0x0438b1dc / 0x00648258+ port, they can either declare
 * their own typed accessor or wire here.
 */
static int32_t g_ui_scratch_dat_00529704 = 0;
static int32_t g_ui_scratch_dat_00529708 = 0;
static int32_t g_ui_scratch_dat_0438b1dc = 0;
static int32_t g_ui_scratch_dat_00648258 = 0;
static int32_t g_ui_scratch_dat_0064825c = 0;
static int32_t g_ui_scratch_dat_0064822c = 0;
static int32_t g_ui_scratch_dat_00648230 = 0;
static int32_t g_ui_scratch_dat_00648234 = 0;
static int32_t g_ui_scratch_dat_00648238 = 0;
static int32_t g_ui_scratch_dat_00648254 = 0;
static int32_t g_ui_scratch_dat_0064826c = 0;
static int32_t g_ui_scratch_dat_00648294 = 0;
static int32_t g_ui_scratch_dat_00648270 = 0;
static int32_t g_ui_scratch_dat_00648274 = 0;
static int32_t g_ui_scratch_dat_00648278 = 0;
static int32_t g_ui_scratch_dat_0064827c = 0;

void scene_new_game_clear_ui_scratch(void)
{
    /* E.2 probe — FUN_004060ff @ 0x4060ff. */
    CALL_TRACE_ENTER(0x4060ffu);

    /* Engine writes 0xffffffff to the first two, 0 to the rest.
     * Order matches the disasm (0x529704/8 first, then 0x438b1dc,
     * then the 0x648 block).  See engine FUN_004060ff @ 4060ff. */
    g_ui_scratch_dat_00529704 = (int32_t)0xffffffff;
    g_ui_scratch_dat_00529708 = (int32_t)0xffffffff;
    g_ui_scratch_dat_0438b1dc = 0;
    g_ui_scratch_dat_00648258 = 0;
    g_ui_scratch_dat_0064825c = 0;
    g_ui_scratch_dat_0064822c = 0;
    g_ui_scratch_dat_00648230 = 0;
    g_ui_scratch_dat_00648234 = 0;
    g_ui_scratch_dat_00648238 = 0;
    g_ui_scratch_dat_00648254 = 0;
    g_ui_scratch_dat_0064826c = 0;
    g_ui_scratch_dat_00648294 = 0;
    g_ui_scratch_dat_00648270 = 0;
    g_ui_scratch_dat_00648274 = 0;
    g_ui_scratch_dat_00648278 = 0;
    g_ui_scratch_dat_0064827c = 0;
}

int32_t scene_new_game_ui_scratch_dat_00529704_get(void)
{
    return g_ui_scratch_dat_00529704;
}

int32_t scene_new_game_ui_scratch_dat_00529708_get(void)
{
    return g_ui_scratch_dat_00529708;
}

int32_t scene_new_game_ui_scratch_dat_00648258_get(void)
{
    return g_ui_scratch_dat_00648258;
}

/* ─── DAT_0734b9a0 (FUN_004682d0) ─────────────────────────────────────────
 *
 * Engine: tiny word at 0x0734b9a0 — likely a "stage-load pulse B" sister
 * to DAT_0734b9a4 (already ported as stage_load_pulse_b in
 * stage_load_pulse.c).  Cleared here as part of the New Game reset; no
 * port consumer today.
 */
static int32_t g_stage_load_pulse_b = 0;

void scene_new_game_clear_stage_load_pulse_b(void)
{
    /* E.2 probe — FUN_004682d0 @ 0x4682d0. */
    CALL_TRACE_ENTER(0x4682d0u);

    g_stage_load_pulse_b = 0;
}

int32_t scene_new_game_stage_load_pulse_b_get(void)
{
    return g_stage_load_pulse_b;
}
