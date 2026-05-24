/*
 * scene1_sim.c — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004536cb @ 0x4536cb state-1 sub-dispatch
 * (L50555-50568) — picks one of three arms based on flag globals,
 * then runs that arm's per-tick body.
 *
 * Cs1 ported FUN_004427d3 (30 B transition/paused wrapper) minimally:
 *
 *   FUN_0048407f  (  795 B) — unported
 *   FUN_00430c00  (  109 B) — unported
 *   FUN_0043ae20  (25750 B) — unported (player + NPC + world tick)
 *   FUN_0043a5d9  ( 1429 B) — unported
 *   FUN_0040fb3a            — ported as scene1_particles_tick()
 *   FUN_004426a7  (  300 B) — unported
 *
 * C8j.3 ports FUN_00442cef (2490 B default-running arm) as a thin
 * wrapper.  The engine body is dominated by gameplay-logic gates
 * (pause counters, cinematic counters, equip-bag counter, player-state
 * transitions) that gate the actual ticks.  With BSS-zero gates (HOUSE
 * default) the per-tick effect collapses to:
 *
 *   FUN_0043ae20  table B tick  — stubbed (separate Mt. Everest)
 *   FUN_0044284b  table C tick  → scene1_records_c_tick()
 *   FUN_0040fb3a  table A tick  → scene1_particles_tick()
 *
 * The 5/3 stubbed siblings are safe to drop because their outputs feed
 * `FUN_0043ae20`'s game-logic tick (player movement, NPC AI, item
 * pickups), which we don't run.  Nothing in our port reads what they
 * would have written — scene-1 render reads only from the per-record
 * tables, which the integrators + spawn API populate independently.
 *
 * See docs/findings/sim-step-a-dispatch.md §"Cs1" for the original
 * Cs1 survey, and docs/findings/scene1-record-populators.md §"C8j.3"
 * for the C8j ladder.
 */

#include "scene1_sim.h"

#include "scene1_particles_tick.h"
#include "scene1_records_c_tick.h"

/* Engine flag stand-ins.  All BSS-zero default → HOUSE selects the
 * default-running arm.  See header for engine-globals mapping. */
int g_scene1_ingame_transition_flag = 0;  /* DAT_0438b1d0 */
int g_scene1_ingame_skip_flag       = 0;  /* DAT_0438b1d8 */
int g_scene1_ingame_paused_flag     = 0;  /* DAT_0438b1c8 */

void scene1_ingame_transition_arm_tick(void)
{
    /* FUN_004427d3 L10 — the one ported call.  The other 5 are
     * dormant; documented in this file's header. */
    scene1_particles_tick();
}

void scene1_ingame_default_arm_tick(void)
{
    /* FUN_00442cef L40603 — gated on DAT_0438b4b4 == 0 (BSS-zero in
     * HOUSE → gate opens) but unported anyway. */
    /* scene1_records_b_tick();  — FUN_0043ae20, stubbed */

    /* FUN_00442cef L40611 — unconditional inside the default-running
     * nested block. */
    scene1_records_c_tick();

    /* FUN_00442cef L40851 (LAB_004435f7) — unconditional tail; reaches
     * every code path of the function including the early-return pause
     * branches. */
    scene1_particles_tick();
}

void scene1_ingame_tick(void)
{
    /* Engine FUN_004536cb L50555-50568 — state-1 sub-dispatch:
     *
     *   if (transition_flag != 0)  → transition arm
     *   else if (skip_flag != 0)   → skip (no sim call)
     *   else if (paused_flag == 0) → default-running arm
     *   else                       → paused arm (= transition arm body)
     */
    if (g_scene1_ingame_transition_flag != 0) {
        scene1_ingame_transition_arm_tick();
    } else if (g_scene1_ingame_skip_flag != 0) {
        /* skip — no sim call */
    } else if (g_scene1_ingame_paused_flag == 0) {
        scene1_ingame_default_arm_tick();
    } else {
        scene1_ingame_transition_arm_tick();
    }
}
