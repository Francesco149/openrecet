/*
 * scene.h — engine scene-state globals + dispatcher entry points.
 *
 * The engine carries a small set of "what is the current scene"
 * globals that every per-frame function consults to decide what to
 * sim and render. The two that matter for the boot path:
 *
 *   DAT_0438b1c0  — scene state (0 = title, 1 = in-game world, etc.)
 *   DAT_0438b1c8  — scene sub-state (1 = within-state transition flag,
 *                                    used by the in-game world)
 *
 * Bootstrap write order in the engine:
 *   1. FUN_00451790 (prewindow_init) writes DAT_0438b1c0 = 1.
 *   2. FUN_0047b29e (title-scene bootstrap, called from WndProc/menu and
 *      indirectly at boot) writes DAT_0438b1c0 = 0 and DAT_0438b1c8 = 0.
 *
 * So by the time the main loop starts ticking, the title scene is
 * active (state == 0). This module names those globals + provides a
 * `scene_state_set_title()` that mirrors the first two writes of
 * FUN_0047b29e — the rest of FUN_0047b29e's calls (FUN_00452917 et al)
 * land as their target ports come online.
 */
#ifndef OPENRECET_SCENE_H
#define OPENRECET_SCENE_H

#include <stdint.h>

/* Engine scene state. Engine global DAT_0438b1c0.
 *
 * Known values (from FUN_004547ab render dispatch + FUN_0049a59e
 * NEW-GAME branch):
 *   0  — title screen
 *   1  — in-game world (town / dungeon, dispatched further by DAT_0438b1c8)
 *   2..5 — other state machines reached via fade-out from title
 *   6  — entry to NEW-GAME post-fade init (set by FUN_00490e16)
 *   8  — the WORLD / TOWN map (overworld destination picker). Reached from
 *        the shop via the door-exit (T1); preload FUN_004735ad, init
 *        FUN_0049de20, sim FUN_0049e163, render FUN_0049e3a3. (The NEW-GAME
 *        init also briefly flips here — DAT_0438b1c0=8 then =1 — purely to
 *        reset the world-map cursor via FUN_0049de18 before INGAME.)
 *
 * States 0, 1 and 8 have producers + consumers wired today; the rest are
 * documented for future ports. */
extern int32_t g_scene_state;

/* Engine scene sub-state. Engine global DAT_0438b1c8. Used by
 * FUN_004547ab's state==1 branch to pick between several in-game
 * render flavours. Initialised to 0 by FUN_0047b29e.
 *
 * It also selects the *ESC context* within the in-game state (b1c0==1):
 *   b1c8 != 0  — a script/cutscene is playing (the prologue dialogue is
 *                b1c8==1 throughout). ESC → the dialogue "skip this event?"
 *                prompt, via FUN_00453384's b1c8!=0 branch → FUN_0046c2cb →
 *                FUN_00434def (the choice box), gated on the dialogue's own
 *                skip_prompt counter DAT_073a3e18 > 1.
 *   b1c8 == 0  — free-roam. ESC → the PAUSE menu (FUN_00453384's b1c8==0 arm →
 *                DAT_06a49998/9c/a0 + the FUN_00454191 radial-blur RTT render).
 * (Confirmed live 2026-06-02: forcing b1c8=0 mid-dialogue + ESC pops the pause
 * menu, not the skip. See docs/findings/esc-skip-event.md "MAJOR CORRECTION".) */
extern int32_t g_scene_substate;

/* Scene-state values. Named so dispatch code reads better than the
 * raw integers. Add more as scenes port; for now only TITLE has a
 * real producer. */
enum {
    SCENE_STATE_TITLE    = 0,
    SCENE_STATE_INGAME   = 1,
    /* 2..7 reserved — see engine FUN_004547ab line 60 onward. */
    SCENE_STATE_WORLDMAP = 8,  /* the WORLD / TOWN map (overworld picker) */
};

/* Mirror of FUN_0047b29e first two writes:
 *
 *   DAT_0438b1c0 = 0;
 *   DAT_0438b1c8 = 0;
 *
 * Called from main.c after prewindow_init() + before scene_title
 * asset/menu/anim init. Idempotent. */
void scene_state_set_title(void);

/* Engine FUN_0049a59e L63-77 — post-fade scene-transition out of the
 * title. Called on the sim tick where `fade_is_done()` first returns 1
 * (after NEW GAME / NEW_HAS_SAVE / CONT_HAS_SAVE were committed and the
 * 17-tick black fade quad completed).
 *
 * Engine writes (in order):
 *   _DAT_0438b1e4 = 0;
 *   DAT_0438b1c0  = 8;            // WORLD MAP (transient — only to reset its cursor)
 *   FUN_0049de18();                // DAT_09643684 = 0 (world-map selected-dest reset)
 *   DAT_0438b1c0  = 1;            // INGAME
 *   DAT_0438b4e0  = 0;
 *   _DAT_0438b7d4 = 0.0;
 *   DAT_0438b4dc  = 0;
 *   DAT_0438b928  = 0;
 *   FUN_004060ff();                // UI/hit-test scratch reset (16 globals)
 *   FUN_004682d0();                // DAT_0734b9a0 = 0
 *   DAT_0438cc04  = 0;
 *   FUN_00452917();                // close worker-thread handle if any
 *   DAT_0438b1c8  = 0;
 *
 * The follow-up engine block (~150 lines) does save-bank init via the
 * pre-baked starting inventory tables + per-adventurer stat seed.
 * Bank 0 reset (FUN_0049001c) IS wired here via save_bank_init_one(0);
 * the rest of the block (FUN_004060ff / 4682d0 / 452917 et al — UI
 * scratch resets) stays deferred until their consumers port.  This
 * port drops the LOADING marker (engine never lingers there) and sets
 * INGAME so the render dispatch flips to the placeholder ingame scene.
 *
 * The intermediate LOADING write is preserved as an observable for
 * tests: scene_post_fade_init() returns the prior state so callers
 * (today only scene_title_sim) can assert the order if needed. */
void scene_post_fade_init(void);

#endif /* OPENRECET_SCENE_H */
