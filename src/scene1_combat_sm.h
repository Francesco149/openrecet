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
 *   C8jb.1 (this chip)                   — Phase A entry gates + per-tick
 *                                          flag write (resolves PHC #21).
 *                                          Bodies stubbed; ret = 0.
 *   C8jb.2..11                           — Phases B/C/D bodies.
 *   C8jb.fin                             — Install as integrator default
 *                                          SM hook (int-ret plumbing).
 *
 * Return contract (full SM):
 *
 *   0  — no interaction this tick (also: any entry gate non-zero).
 *   1  — hit fired (downstream body should apply damage write to owner).
 *   2  — full cleanup; slot self-killed inside the SM (`*slot = 0`).
 *
 * C8jb.1 scope only implements Phase A: returns 0 unconditionally after
 * either short-circuiting on an entry gate OR setting the per-tick flag.
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

/* ─── public entry ───────────────────────────────────────────────────── */
/*
 * Tick the per-record state machine for one slot.
 *
 * `slot` must point to the first dword of an SCENE1_RECORDS_B_STRIDE-sized
 * slot in g_scene1_records_b (caller's responsibility; the slot pointer
 * is treated identically to the engine's `param_1`).  The function does
 * NOT validate the pointer.
 *
 * Returns the SM ret contract value {0, 1, 2}.  C8jb.1 returns 0 in all
 * paths — Phase A short-circuit OR fall-through to a stubbed body.
 *
 * Side effects in C8jb.1:
 *   - On fall-through (all entry gates zero): writes
 *     g_scene1_records_b_tick_flag = 1.  This is engine PHC #21's
 *     resolution — the writer that the integrator's per-tick clear
 *     was looking for.  Resolves PHC #21.
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
