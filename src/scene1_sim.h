/*
 * scene1_sim.h — per-tick INGAME (scene state == 1) sim handler.
 *
 * Engine source: FUN_004427d3 @ 0x4427d3 (30 bytes) — the 6-call
 * wrapper invoked by FUN_004536cb when DAT_0438b1c0 == 1.  See
 * `docs/findings/sim-step-a-dispatch.md` for the full survey + chip
 * ladder.  Cs1: only the particle-integrator call is ported; the
 * five sibling callees (player+NPC+world tick + UI + camera) stay
 * stubbed because their consumers haven't ported yet.
 */
#ifndef OPENRECET_SCENE1_SIM_H
#define OPENRECET_SCENE1_SIM_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Run one INGAME sim tick.  Today this is just
 * `scene1_particles_tick()`.  Called from `sim_step_a` when
 * `g_scene_state == SCENE_STATE_INGAME` (engine state 1).
 *
 * Safe to call with sentinel-empty record tables: the integrator
 * short-circuits per-slot on the TYPE == -1 sentinel.
 */
void scene1_ingame_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_SIM_H */
