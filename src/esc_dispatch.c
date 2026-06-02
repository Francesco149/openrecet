/*
 * esc_dispatch.c — see esc_dispatch.h. Pure C; no Win32 dependency.
 */
#include "esc_dispatch.h"

#include "scene.h"                 /* g_scene_state, SCENE_STATE_TITLE */
#include "scene1_intro_dialogue.h" /* scene1_intro_dialogue_active() — skippable ctx */
#include "skip_event.h"            /* skip_event_arm() */

int g_esc_disabled = 0;   /* DAT_06a49954 */

esc_result_t esc_pressed(void)
{
    /* (A) FUN_00452911(): ESC globally disabled (loads / non-interruptible
     * transitions). PORT-DEBT: no producer wired; stays 0. */
    if (g_esc_disabled)
        return ESC_RESULT_SWALLOW;

    /* (B) DAT_0438b1c0 != 0 — any in-game sub-mode (free-roam, dialogue,
     * menus). The engine hands this to FUN_0045337b → FUN_00453384, the
     * skip-event prompt. We arm it here: skip_event_arm() opens the "skip this
     * event?" prompt iff the current event is skippable (the prologue: a
     * dialogue line is up) AND g_skip_event_enabled is set (off until the
     * Phase C banner render lands, so an invisible prompt can't soft-lock the
     * frozen dialogue — until then this is a no-op and ESC stays swallowed,
     * preserving the Phase A fix: no quit box outside the title). Either way
     * the window-level action is to swallow; the prompt drives the skip from
     * the sim tick. */
    if (g_scene_state != SCENE_STATE_TITLE) {
        skip_event_arm(scene1_intro_dialogue_skippable());
        return ESC_RESULT_SWALLOW;
    }

    /* (C)/(D) Title with no overlay open (FUN_0049a585) → quit-confirm box.
     * PORT-DEBT: the DAT_09643520/DAT_09643544 overlay-open suppress isn't
     * modelled — no in-game pause/menu overlay is reachable yet, so at the
     * title the quit gate is always open. */
    return ESC_RESULT_QUIT;
}
