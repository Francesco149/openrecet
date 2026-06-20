/*
 * esc_dispatch.c — see esc_dispatch.h. Pure C; no Win32 dependency.
 */
#include "esc_dispatch.h"

#include "scene.h"                 /* g_scene_state, SCENE_STATE_TITLE */
#include "scene1_intro_dialogue.h" /* scene1_intro_dialogue_active() — skippable ctx */
#include "skip_event.h"            /* skip_event_arm() */
#include "scene_pause.h"           /* pause_dispatch() — the in-game pause menu */
#include "worker_load.h"           /* worker_load_busy() — engine's ESC gate */
#include "customer_service.h"      /* customer_service_esc_skip_arm() — cc08==4 */
#include "scene1_player_ctrl.h"    /* player_ctrl_cc08() — the cc08==4 gate */

int g_esc_disabled = 0;   /* DAT_06a49954 */

esc_result_t esc_pressed(void)
{
    /* (A) FUN_00452911(): ESC globally disabled (loads / non-interruptible
     * transitions). PORT-DEBT: no producer wired; stays 0. */
    if (g_esc_disabled)
        return ESC_RESULT_SWALLOW;

    /* (B) DAT_0438b1c0 != 0 — any in-game sub-mode. The engine routes ESC
     * to FUN_00453384, which BOTH arms the skip-event prompt (its choice-box
     * branch, when a skippable event is up) AND opens the pause menu (its
     * pausable branch, otherwise). The port splits those:
     *
     *   - A skippable event up (prologue line / guild cutscene) → the "skip
     *     this event?" prompt (skip_event_arm; self-gated on
     *     g_skip_event_enabled). Unchanged.
     *   - Otherwise → the in-game pause menu (pause_dispatch(0)). It
     *     self-gates on the mode being pausable + no fade; while already
     *     paused (mode 9, fully open) it begins the unpause, so ESC toggles
     *     the pause like the engine. Gated on the primary asset-load worker
     *     being idle — matching the engine's ESC check (FUN_004536cb L50446:
     *     bit 0x100 only dispatches when DAT_06a49954 == 0).
     *
     * Either way the window-level action is to swallow; the pause/skip drive
     * from the sim tick. */
    if (g_scene_state != SCENE_STATE_TITLE) {
        if (scene1_intro_dialogue_skippable())
            skip_event_arm(1);
        else if (player_ctrl_cc08() == 4)
            /* cc08==4 customer service (FUN_00453384 @ 0x4533ce): the SCRIPTED
             * haggle tutorial (b51c==1) arms the "Cancelling tutorial?" skip
             * prompt (FUN_0045e6a5); a live customer (b51c==0) is NOT skippable
             * and ESC does nothing — in either case it never opens the pause
             * menu (the engine's cc08==4 branch never reaches the pausable arm). */
            customer_service_esc_skip_arm();
        else if (!worker_load_busy())
            pause_dispatch(0);
        return ESC_RESULT_SWALLOW;
    }

    /* (C)/(D) Title with no overlay open (FUN_0049a585) → quit-confirm box.
     * PORT-DEBT: the DAT_09643520/DAT_09643544 overlay-open suppress isn't
     * modelled — no in-game pause/menu overlay is reachable yet, so at the
     * title the quit gate is always open. */
    return ESC_RESULT_QUIT;
}
