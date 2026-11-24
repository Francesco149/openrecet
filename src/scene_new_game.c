/*
 * scene_new_game.c — see scene_new_game.h for the chip writeup.
 */

#include "scene1_hud.h"   /* DAT_0438b1dc */
#include "scene_new_game.h"
#include "stage_load_pulse.h"   /* DAT_0734b9a0 */

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
 * 0x00529704 / 0x00648258+ port, they can either declare
 * their own typed accessor or wire here.
 */
static int32_t g_ui_scratch_dat_00529704 = 0;
static int32_t g_ui_scratch_dat_00529708 = 0;
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
    scene1_hud_set_letterbox_height(0.0f);   /* DAT_0438b1dc */
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
 * The display-menu slide's "active" flag (stage_load_pulse.c owns it; FUN_004693e3 ramps DAT_0734b98c toward 5 while it
 * is set, toward 0 when clear). FUN_0049a59e clears it on every game start / Continue: a save taken with the item window
 * open (the Adventurers' Guild bag) resumes with the slide decaying 5 → 0 (rhost save-in-guilds f2393; the port's old
 * private copy of the flag left the real one set).
 */
void scene_new_game_clear_stage_load_pulse_b(void)
{
    /* E.2 probe — FUN_004682d0 @ 0x4682d0. */
    CALL_TRACE_ENTER(0x4682d0u);

    stage_load_pulse_set_active(0);
}

int32_t scene_new_game_stage_load_pulse_b_get(void)
{
    return stage_load_pulse_get_active();
}
