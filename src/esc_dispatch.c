/*
 * esc_dispatch.c — see esc_dispatch.h. Pure C; no Win32 dependency.
 */
#include "esc_dispatch.h"

#include "scene.h"   /* g_scene_state, SCENE_STATE_TITLE */

int g_esc_disabled = 0;   /* DAT_06a49954 */

esc_result_t esc_pressed(void)
{
    /* (A) FUN_00452911(): ESC globally disabled (loads / non-interruptible
     * transitions). PORT-DEBT: no producer wired; stays 0. */
    if (g_esc_disabled)
        return ESC_RESULT_SWALLOW;

    /* (B) DAT_0438b1c0 != 0 — any in-game sub-mode (free-roam, dialogue,
     * menus). The engine hands this to FUN_0045337b → FUN_00453384, the
     * skip-event prompt. Until that subsystem lands (plan Phase B/C), in-game
     * ESC is swallowed — the fix that matters here is that ESC no longer pops
     * the quit box outside the title. */
    if (g_scene_state != SCENE_STATE_TITLE)
        return ESC_RESULT_SWALLOW;   /* PORT-DEBT(skip-event): arm the prompt here */

    /* (C)/(D) Title with no overlay open (FUN_0049a585) → quit-confirm box.
     * PORT-DEBT: the DAT_09643520/DAT_09643544 overlay-open suppress isn't
     * modelled — no in-game pause/menu overlay is reachable yet, so at the
     * title the quit gate is always open. */
    return ESC_RESULT_QUIT;
}
