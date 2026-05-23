/*
 * scene1_sim.c — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004427d3 @ 0x4427d3 (30 bytes) — the 6-call
 * wrapper invoked by FUN_004536cb when DAT_0438b1c0 == 1.
 *
 *   FUN_0048407f  (  795 B) — unported
 *   FUN_00430c00  (  109 B) — unported
 *   FUN_0043ae20  (25750 B) — unported (player + NPC + world tick)
 *   FUN_0043a5d9  ( 1429 B) — unported
 *   FUN_0040fb3a            — ported as scene1_particles_tick()
 *   FUN_004426a7  (  300 B) — unported
 *
 * The 5 stubbed siblings are safe to drop because their outputs feed
 * `FUN_0043ae20`'s game-logic tick (player movement, NPC AI, item
 * pickups), which we don't run.  Nothing in our port reads what they
 * would have written — scene-1 render reads only from the per-record
 * tables, which the integrator + spawn API populate independently.
 *
 * See docs/findings/sim-step-a-dispatch.md §"Cs1" for the survey and
 * follow-up chip ladder (Cs2 — LAB_00453bed mass dispatch, etc.).
 */

#include "scene1_sim.h"

#include "scene1_particles_tick.h"

void scene1_ingame_tick(void)
{
    /* FUN_004427d3 L10 — the one ported call.  The other 5 are
     * dormant; documented in this file's header. */
    scene1_particles_tick();
}
