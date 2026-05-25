/*
 * scene1_combat_sm.h — per-record state machine (combat tick) for table B.
 *
 * Engine source: FUN_0043865e @ 0x43865e (8059 B, Mt. Everest #2 —
 * per-record state machine invoked 73× by the FUN_0043ae20 integrator).
 * Survey doc: docs/findings/scene1-records-b-state-machine.md.
 *
 * Chip ladder (C8jb.*):
 *
 *   C8jb.0 (2026-05-25, commit e72ac6c)  — Survey only.
 *   C8jb.1 (2026-05-25, commit 256a1f6)  — Phase A entry gates + per-tick
 *                                          flag write (resolves PHC #21).
 *   C8jb.2 (2026-05-25, commit 4b29713)  — Phase B head: attacker NPC scan
 *                                          iteration shell + 4 skip gates +
 *                                          per-NPC hit-history filter.
 *   C8jb.3 (this chip)                   — Phase B collision math: nested
 *                                          per-NPC sub-iter loop (1/7/2
 *                                          by NPC type), position lookup
 *                                          (npc.combat_pose OR anchor
 *                                          via rdata DAT_005c530c/5314),
 *                                          2D-XZ distance check + AABB
 *                                          Y-band check.  No hit
 *                                          registration yet.
 *   C8jb.4..11                           — Phase B angle filter / body /
 *                                          Phase C / Phase D.
 *   C8jb.fin                             — Install as integrator default
 *                                          SM hook (int-ret plumbing).
 *
 * Return contract (full SM):
 *
 *   0  — no interaction this tick (also: any entry gate non-zero).
 *   1  — hit fired (downstream body should apply damage write to owner).
 *   2  — full cleanup; slot self-killed inside the SM (`*slot = 0`).
 *
 * C8jb.1..3 scope: returns 0 unconditionally.  Phase A short-circuit OR
 * Phase B iteration-completes-without-hit OR Phase C/D stub fall-through.
 */
#ifndef OPENRECET_SCENE1_COMBAT_SM_H
#define OPENRECET_SCENE1_COMBAT_SM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Phase A entry gates (4 engine globals) ─────────────────────────── */
/*
 * If ANY of these is non-zero (positive for the first three; non-zero
 * for the paused-flag), the SM short-circuits to ret=0 without setting
 * the per-tick flag.
 *
 *   DAT_0438be98 — combat subphase (also referenced by music.c).  Reads
 *                  zero in HOUSE-idle.  Set positive during certain
 *                  combat phase transitions.
 *
 *   DAT_0438be9c — world-pause flag.  Set to 1 by Phase C's
 *                  type-4/5/8 sub-arm (L35762 of all.c) — a "dialog /
 *                  cinematic active" latch.  Also gates at least one
 *                  unrelated function at all.c L7624.
 *
 *   DAT_0438bea0 — sibling pause flag (less common, set by a few
 *                  scene-state mutators).
 *
 *   DAT_0438b1c8 — INGAME paused flag (modal active).  Already declared
 *                  by scene1_sim.h as g_scene1_ingame_paused_flag — we
 *                  read THAT here, not re-declare.
 *
 * Tests install non-zero values to verify each gate short-circuits.
 * Production keeps all four BSS-zero in HOUSE so SM-installed ticks
 * always pass Phase A and bump the per-tick flag.
 */
extern int32_t g_scene1_combat_subphase;     /* DAT_0438be98 */
extern int32_t g_scene1_combat_world_pause;  /* DAT_0438be9c */
extern int32_t g_scene1_combat_aux_pause;    /* DAT_0438bea0 */

/* ─── Phase B head — attacker NPC scan ───────────────────────────────── */
/*
 * Phase B gate: enters only when slot is in an "attacker" combat state
 * AND the player is alive (HP > 0).
 *
 *   slot[FLAG_A] in {0, 3}     attacker = idle (0) or hit-recovery (3)
 *   g_scene1_combat_player_hp > 0.0f
 *
 * Engine global: _DAT_056db0bc — float, written by the Phase D HP path
 * (not ported yet) and by FUN_0044b16c (damage applicator, unported).
 * Default 0.0f → entire Phase B scan is dormant.  Tests raise this to
 * exercise iteration paths.
 *
 * The scan iterates g_scene1_people[0..127], stride 0xba4 B in engine.
 * Each NPC passes 4 skip gates + the per-NPC hit-history filter; NPCs
 * that pass enter the nested per-NPC sub-iter loop (C8jb.3 collision
 * math).
 */
extern float g_scene1_combat_player_hp;      /* _DAT_056db0bc */

/*
 * Total count of NPCs that passed all 4 skip gates + the hit-history
 * filter during the most recent scene1_combat_sm_tick() call.  Reset to
 * 0 at the start of each tick that proceeds past Phase A.  Useful for
 * test smoke ("did the iteration reach any NPC?") without installing
 * a full visit hook.
 */
extern int32_t g_scene1_combat_phase_b_visit_count;

/*
 * Per-NPC visit hook.  Called once per NPC index `i` (0..127) that
 * passes all 4 skip gates + the hit-history filter.  Default NULL → no
 * callback.  Tests use this to capture which NPCs were "would-collide"
 * candidates.  The hook does NOT short-circuit iteration; it is a pure
 * observer.
 */
typedef void (*scene1_combat_phase_b_visit_fn)(int npc_index);
scene1_combat_phase_b_visit_fn
scene1_combat_set_phase_b_visit_hook(scene1_combat_phase_b_visit_fn fn);

/* ─── Phase B collision math (C8jb.3) ──────────────────────────────── */
/*
 * Per-NPC-type collision attributes — engine's per-TYPE table at
 * DAT_005c23f0 (stride 0x68 B per entry, indexed by NPC type 0..255).
 * The combat SM reads three float fields at byte offsets +0x44 / +0x4c /
 * +0x50.  PHC #19 grep'd DAT_005c2400 (= +0x10) and three siblings as
 * having zero writers in the binary; this likely extends to the whole
 * table (no init code identified yet).  Modeled as a host-controllable
 * global so tests can inject collision attrs; production keeps it
 * BSS-zero → collision check fails for everything except direct overlap
 * (dist - reach < 0).
 *
 *   radius_mul   +0x44 (DAT_005c2434)  per-type attack-radius multiplier;
 *                                      gates `dist - reach < radius *
 *                                      radius_mul * dist_mul`.
 *   y_band_mul   +0x4c (DAT_005c243c)  per-type Y-band half-thickness
 *                                      multiplier; gates `|dy -
 *                                      reach*0.8| < radius * y_band_mul *
 *                                      dist_mul`.
 *   dist_mul     +0x50 (DAT_005c2440)  per-type distance multiplier,
 *                                      composed with both gates.
 *
 * "Reach" is the slot's reach radius at slot[SCENE1_RECORDS_B_OFF_DRAG]
 * (engine slot[0x2a] — same slot field that C8j-tick bodies use as DRAG;
 * combat reinterprets it as reach).  Engine drops the dy term when
 * within the Y-band gate.
 */
typedef struct {
    float radius_mul;
    float y_band_mul;
    float dist_mul;
} scene1_combat_npc_type_attrs_t;

#define SCENE1_COMBAT_NPC_TYPE_ATTRS_COUNT 256
extern scene1_combat_npc_type_attrs_t
    g_scene1_combat_npc_type_attrs[SCENE1_COMBAT_NPC_TYPE_ATTRS_COUNT];

/*
 * Total count of (NPC, sub-iter) pairs that passed the distance check
 * + AABB Y-band check during the most recent scene1_combat_sm_tick()
 * call.  Reset to 0 alongside g_scene1_combat_phase_b_visit_count.
 * Observable for tests that want to count "would-hit" candidates
 * without installing the collision hook.
 *
 * Note: a single NPC may contribute up to 7 (0x44/0x45) or 2 (0x46/0x47)
 * collision hits to this counter — one per sub-iter that lands in range.
 */
extern int32_t g_scene1_combat_phase_b_collision_count;

/*
 * Per-collision hook.  Called once per (NPC, sub-iter) pair that passes
 * both the distance check and the AABB Y-band check.  `sub_iter` is the
 * engine's `local_50` (0-based index into the multi-hit anchor table);
 * for non-multi-hit NPCs always 0.  Default NULL → no callback.
 */
typedef void (*scene1_combat_phase_b_collision_fn)(int npc_index, int sub_iter);
scene1_combat_phase_b_collision_fn
scene1_combat_set_phase_b_collision_hook(scene1_combat_phase_b_collision_fn fn);

/* ─── public entry ───────────────────────────────────────────────────── */
/*
 * Tick the per-record state machine for one slot.
 *
 * `slot` must point to the first dword of an SCENE1_RECORDS_B_STRIDE-sized
 * slot in g_scene1_records_b (caller's responsibility; the slot pointer
 * is treated identically to the engine's `param_1`).  The function does
 * NOT validate the pointer.
 *
 * Returns the SM ret contract value {0, 1, 2}.  C8jb.1..3 return 0 in
 * all paths — Phase A short-circuit OR Phase B iter-completes-with-no-
 * hit OR Phase C/D stub fall-through.
 *
 * Side effects in C8jb.1..3:
 *   - On fall-through past Phase A (all entry gates zero): writes
 *     g_scene1_records_b_tick_flag = 1.  Resolves PHC #21.
 *   - Resets g_scene1_combat_phase_b_visit_count + collision_count to 0
 *     then increments them per NPC / per collision during Phase B scan.
 *     If the Phase B outer gate (FLAG_A in {0,3} && player_hp > 0)
 *     fails, both counters stay at 0.
 *   - Calls g_scene1_combat_phase_b_visit_hook + collision_hook (if
 *     installed) per visit / per collision.
 */
int scene1_combat_sm_tick(int32_t *slot);

/*
 * Wrapper adapter: install scene1_combat_sm_tick as the existing
 * scene1_records_b_set_state_machine_hook (void return).  The void hook
 * loses the {0, 1, 2} ret distinction — the integrator's translator
 * (`state_machine_call_ret`) coerces all "hook installed" into ret=1.
 *
 * C8jb.1 does NOT auto-install — production wiring stays as-is (no SM
 * hook = ret=0).  Tests call this to exercise the integrator coupling
 * with Phase A side effects (per-tick flag write).
 */
void scene1_combat_sm_install_as_void_hook(void);
void scene1_combat_sm_uninstall_void_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_COMBAT_SM_H */
