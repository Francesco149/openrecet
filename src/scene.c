#include "scene.h"

#include "d3d_pool.h"
#include "fade.h"
#include "nowloading.h"
#include "npc_schedule.h"
#include "save_bank.h"
#include "scene1_intro_events.h"
#include "scene_new_game.h"
#include "stage_post_load.h"
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

    /* Engine FUN_0049a59e L65 — write DAT_09643684 = 0 between the
     * LOADING and INGAME state writes (= save-dialog state reset).
     * No-op observable today; surfaces in call_trace for parity. */
    scene_new_game_clear_save_dialog_state();

    /* Engine FUN_0049a59e L213 (the `DAT_0438bed4 != 0` branch — the
     * NEW GAME path). Reset bank 0 (the active save slot's data) to
     * its fresh "new game" state. The engine reads the active slot
     * index from DAT_0438b1e0; until save-slot UI lands, we hardcode
     * bank 0 — matches the engine's behaviour on a fresh boot where
     * DAT_0438b1e0 is BSS-zero.
     * PORT-DEBT(hardcode, NONE): active save-slot pinned to bank 0; engine
     * sources it from DAT_0438b1e0. Retire = save-slot UI lands (plan Step 3.5).
     *
     * Position note: the engine calls FUN_0049001c (= save_bank_init_one)
     * much later in the commit block (~L213).  Our port hoists it
     * forward here so the bank is ready before fade_phase_out_start
     * + worker_load_spawn — the worker reads bank state to load
     * per-chara assets.  Faithful when no observer sits between the
     * two positions; we currently have none. */
    save_bank_init_one(0);

    /* Engine FUN_0049a59e L100757 — FUN_00435c98 fires right after
     * FUN_0049001c (= save_bank_init_one) in the NEW GAME post-fade
     * commit branch.  Body: chara XP threshold seeding, position
     * carry-forward, scratch reset for the per-stage tick state.
     * See src/stage_post_load.h. */
    stage_post_load_init();

    /* Engine FUN_0049a59e L232 — FUN_00490e56(0) fires inside the
     * NEW GAME branch right before FUN_00473474.  Body: per-bank NPC
     * schedule status array INIT pass (resets event_active flag,
     * walks 600 NPCs writing mode-dependent status values).  See
     * src/npc_schedule.h. */
    npc_schedule_apply(0);

    g_scene_state    = SCENE_STATE_INGAME;
    g_scene_substate = 0;

    /* STUB: arm the intro-event sequencer (src/scene1_intro_events.h). The
     * unported new-game intro runs TWO scripted events, each with its own
     * load, so retail fires HOUSE_FREEROAM twice before the player can move.
     * The port's single worker_load_spawn below fires it once; the stub
     * injects the second LOADING/HOUSE_FREEROAM edge a few frames later so
     * the TAS segtrace's second `wait HOUSE_FREEROAM` resolves on both
     * targets. Replace with the real dialogue subsystem when it ports. */
    scene1_intro_events_arm();

    /* Engine FUN_0049a59e L71-72 — the 16-global UI scratch reset
     * (FUN_004060ff) + DAT_0734b9a0 clear (FUN_004682d0).  Faithful
     * order: scratch reset first, then the pulse-B clear.  Both fire
     * after the INGAME write and before worker_load_close + the
     * fade-IN kick. */
    scene_new_game_clear_ui_scratch();
    scene_new_game_clear_stage_load_pulse_b();

    /* Engine FUN_0049a59e L74 — close any lingering worker thread
     * before kicking the new asset-load worker.  Our port keeps a
     * single-shot handle, so this is a no-op on fresh boot but
     * matches the engine's call site for call_trace parity. */
    worker_load_close();

    /* Engine FUN_0049a59e L100777 — FUN_00473474() releases all
     * stage-scoped D3D resources tagged type 2.  Port-side D3D pool is
     * empty (the wrapper allocator FUN_0047183b is unported), so this
     * walks 200 NULL slots and returns; probe fires for call_trace
     * parity.  Sits between worker_load_close and fade_phase_out_start
     * to match engine call order. */
    d3d_pool_release_post_fade();

    /* Engine FUN_0049a59e L235: FUN_0045281c(0, 0x11) — kick off the
     * phase-(-1) fade-IN so the alpha quad ramps from 255 → 0 over the
     * next 17 sim ticks, revealing the destination scene. Without this,
     * the phase-1 fade-OUT counter stays pinned at duration+1 and the
     * scene-1 render chain output would be hidden under a
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
