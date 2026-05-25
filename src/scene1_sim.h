/*
 * scene1_sim.h — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004536cb @ 0x4536cb state-1 sub-dispatch
 * (L50555-50568).  Three sub-arms gate on transient flags:
 *
 *   DAT_0438b1d0 != 0  →  FUN_004427d3 (30 B transition wrapper)
 *   DAT_0438b1d8 != 0  →  no sim call (skip)
 *   DAT_0438b1c8 == 0  →  FUN_00442cef (2490 B default-running arm)
 *   else               →  FUN_004427d3 (paused wrapper)
 *
 * Cs1 (2026-05-23) ported FUN_004427d3 minimally (just
 * scene1_particles_tick).  C8j.3 (2026-05-24) adds the default-running
 * arm and the sub-dispatch — for HOUSE all three flags are BSS-zero so
 * the default arm fires.  Default arm adds scene1_records_c_tick on
 * top of the particle tick.  C8j-tick.1 (2026-05-25) adds the table B
 * tick skeleton (preamble + per-type dispatch stub); per-type bodies
 * land in the C8j-tick.* sub-chip ladder.  See
 * docs/findings/scene1-records-b-tick.md.
 */
#ifndef OPENRECET_SCENE1_SIM_H
#define OPENRECET_SCENE1_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Stand-ins for engine state-1 sub-dispatch gate flags
 * (FUN_004536cb L50555-50568).  All BSS-zero by default → HOUSE selects
 * the default-running arm.  Writers are scene-state mutators not yet
 * ported (pause/menu open, scene transitions).  Set non-zero from tests
 * or future scene-state code to route through the other arms.
 */
extern int g_scene1_ingame_transition_flag;  /* DAT_0438b1d0 */
extern int g_scene1_ingame_skip_flag;        /* DAT_0438b1d8 */
extern int g_scene1_ingame_paused_flag;      /* DAT_0438b1c8 */

/*
 * Engine FUN_004427d3 (30 B) — the transition / paused arm.  Thin port:
 * scene1_particles_tick is the only ported callee.  Five siblings
 * (FUN_0048407f, FUN_00430c00, FUN_0043ae20, FUN_0043a5d9, FUN_004426a7)
 * stay stubbed — none of their outputs feed any rendered consumer today.
 */
void scene1_ingame_transition_arm_tick(void);

/*
 * Engine FUN_00442cef (2490 B) — the default-running arm.  Thin port:
 * the engine body is a tower of gameplay-logic gates (pause counters,
 * cinematic counters, equip-bag counter, player-state transitions) that
 * gate calls to the actual per-tick work.  In HOUSE all gates are
 * BSS-zero so the per-tick work collapses to (in engine order):
 *
 *   FUN_0043ae20  table B tick   → scene1_records_b_tick (skeleton only;
 *                                  per-type bodies in C8j-tick.2+; see
 *                                  scene1-records-b-tick.md)
 *   FUN_0044284b  table C tick   → scene1_records_c_tick
 *   FUN_0040fb3a  table A tick   → scene1_particles_tick
 *
 * The other gameplay-logic branches stay out of C8j.3 scope.
 */
void scene1_ingame_default_arm_tick(void);

/*
 * Run one INGAME sim tick.  Dispatches to the appropriate sub-arm per
 * the engine's state-1 flag check.  Safe to call with sentinel-empty
 * record tables — every per-tick integrator short-circuits on the
 * TYPE == -1 (or table B's TYPE == 0) sentinel.
 *
 * Called from `sim_step_a` when `g_scene_state == SCENE_STATE_INGAME`.
 */
void scene1_ingame_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_SIM_H */
