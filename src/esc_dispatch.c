/*
 * esc_dispatch.c — see esc_dispatch.h. Pure C; no Win32 dependency.
 */
#include "esc_dispatch.h"

#include "scene.h"                 /* g_scene_state, SCENE_STATE_TITLE */
#include "scene_pause.h"           /* pause_dispatch() — the in-game pause menu */
#include "worker_load.h"           /* worker_load_busy() — engine's ESC gate */

int g_esc_disabled = 0;   /* DAT_06a49954 */

esc_result_t esc_pressed(void)
{
    /* The WndProc ESC arm (FUN_0047b2e7): FUN_00452911 (DAT_06a49954, the
     * primary load busy) swallows it; in game → FUN_0045337b = FUN_00453384(0)
     * (pause_dispatch: the skip boxes, the beeps, the pause toggle). */
    if (g_esc_disabled || worker_load_busy())
        return ESC_RESULT_SWALLOW;
    if (g_scene_state != SCENE_STATE_TITLE) {
        pause_dispatch(0);
        return ESC_RESULT_SWALLOW;
    }

    /* (C)/(D) Title with no overlay open (FUN_0049a585) → quit-confirm box.
     * PORT-DEBT: the DAT_09643520/DAT_09643544 overlay-open suppress isn't
     * modelled — no in-game pause/menu overlay is reachable yet, so at the
     * title the quit gate is always open. */
    return ESC_RESULT_QUIT;
}
