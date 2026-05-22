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
 *   8  — transient "scene-loading" placeholder
 *
 * Only state == 0 has a producer + consumer wired today; all other
 * values are documented for future ports. */
extern int32_t g_scene_state;

/* Engine scene sub-state. Engine global DAT_0438b1c8. Used by
 * FUN_004547ab's state==1 branch to pick between several in-game
 * render flavours. Initialised to 0 by FUN_0047b29e. */
extern int32_t g_scene_substate;

/* Scene-state values. Named so dispatch code reads better than the
 * raw integers. Add more as scenes port; for now only TITLE has a
 * real producer. */
enum {
    SCENE_STATE_TITLE   = 0,
    SCENE_STATE_INGAME  = 1,
    /* 2..7 reserved — see engine FUN_004547ab line 60 onward. */
    SCENE_STATE_LOADING = 8,
};

/* Mirror of FUN_0047b29e first two writes:
 *
 *   DAT_0438b1c0 = 0;
 *   DAT_0438b1c8 = 0;
 *
 * Called from main.c after prewindow_init() + before scene_title
 * asset/menu/anim init. Idempotent. */
void scene_state_set_title(void);

#endif /* OPENRECET_SCENE_H */
