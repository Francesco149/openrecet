#include "scene.h"

#include "fade.h"

int32_t g_scene_state    = SCENE_STATE_TITLE;
int32_t g_scene_substate = 0;

void scene_state_set_title(void)
{
    g_scene_state    = SCENE_STATE_TITLE;
    g_scene_substate = 0;
}

void scene_post_fade_init(void)
{
    /* Engine's LOADING marker. The engine writes 8 then 1 in adjacent
     * statements within a single sim tick, so no observer ever sees 8
     * mid-flight; we preserve the write for symmetry but the same-tick
     * INGAME write below immediately replaces it. The save-bank init
     * block between the two writes touches state that no ported code
     * reads yet — deferred until the ingame scene module lands. */
    g_scene_state    = SCENE_STATE_LOADING;
    g_scene_state    = SCENE_STATE_INGAME;
    g_scene_substate = 0;

    /* Engine FUN_0049a59e L235: FUN_0045281c(0, 0x11) — kick off the
     * phase-(-1) fade-IN so the alpha quad ramps from 255 → 0 over the
     * next 17 sim ticks, revealing the destination scene. Without this,
     * the phase-1 fade-OUT counter stays pinned at duration+1 and the
     * placeholder scene_ingame_render output would be hidden under a
     * fully-opaque black quad forever. */
    fade_phase_out_start(0, 0x11);
}
