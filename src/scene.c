#include "scene.h"

#include "fade.h"
#include "nowloading.h"
#include "save_bank.h"
#include "worker_load.h"

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
     * INGAME write below immediately replaces it. */
    g_scene_state    = SCENE_STATE_LOADING;

    /* Engine FUN_0049a59e L213 (the `DAT_0438bed4 != 0` branch — the
     * NEW GAME path). Reset bank 0 (the active save slot's data) to
     * its fresh "new game" state. The engine reads the active slot
     * index from DAT_0438b1e0; until save-slot UI lands, we hardcode
     * bank 0 — matches the engine's behaviour on a fresh boot where
     * DAT_0438b1e0 is BSS-zero. */
    save_bank_init_one(0);

    g_scene_state    = SCENE_STATE_INGAME;
    g_scene_substate = 0;

    /* Engine FUN_0049a59e L235: FUN_0045281c(0, 0x11) — kick off the
     * phase-(-1) fade-IN so the alpha quad ramps from 255 → 0 over the
     * next 17 sim ticks, revealing the destination scene. Without this,
     * the phase-1 fade-OUT counter stays pinned at duration+1 and the
     * placeholder scene_ingame_render output would be hidden under a
     * fully-opaque black quad forever. */
    fade_phase_out_start(0, 0x11);

    /* Engine FUN_0049a59e L298: FUN_00452cde() — spawn the asset-load
     * worker thread, which (a) sets DAT_06a49954 = 1 (busy), (b) sets
     * DAT_06a49958 = 1 (nowloading overlay gate), (c) creates a
     * one-shot worker thread that dispatches LAB_0045293d against the
     * current g_scene_state (= 1 / INGAME here).
     *
     * The case-1 INGAME loader callback isn't registered yet — its
     * target functions (FUN_00474a9a + FUN_00436f97) are unported —
     * so the worker enters, finds no callback for slot 1, and cleans
     * up immediately. Same observable as the previous stub:
     *
     *   - busy briefly = 1 then 0 (Win32) / stays 1 until the test
     *     harness calls worker_load_end (non-Win32);
     *   - nowloading gate stays raised because the engine's per-tick
     *     "clear the gate if busy==0" lives at the top of
     *     FUN_004547ab, which we haven't ported yet.
     *
     * Position note: the engine's worker-spawn happens AFTER the
     * INGAME state flip and AFTER fade_phase_out_start (both above);
     * the worker reads g_scene_state to decide what to load. */
    worker_load_spawn();
}
