/*
 * stage_load_pulse.h — per-frame counter ramp for the engine's stage-
 * load animation pulse.
 *
 * Engine source: FUN_004693e3 @ 0x4693e3 (41 bytes).
 *
 * Two globals form a tiny state machine:
 *
 *   active  (DAT_0734b9a0): 0/1 flag set by the stage loader. Engine
 *                           setters FUN_004682c5 (=1), FUN_004682d0
 *                           (=0), FUN_00468338 (=1 on enter). Read by
 *                           FUN_004682bf.
 *   counter (DAT_0734b98c): 0..5 clamped. Ramps UP toward 5 while
 *                           `active` is set, DOWN toward 0 otherwise.
 *                           Reset to 5 by FUN_004682e3, to 0 by
 *                           FUN_00468338.
 *
 * Per-frame contract (FUN_004693e3 = `stage_load_pulse_tick`):
 *
 *   if (active == 0) {
 *       if (counter > 0) counter--;
 *   } else {
 *       if (counter < 5) counter++;
 *   }
 *
 * Called once per frame from sim_step_a (engine FUN_004536cb L50471,
 * AFTER the button mode-cycle, video poll, and scene-transition
 * counter blocks, BEFORE the DAT_06a499c4 scene-reseed check).
 *
 * Wiring in our port: sim_step_a calls stage_load_pulse_tick() after
 * the button ring update, before the scene-state switch. Because our
 * port lacks the intermediate sub-blocks (video poll / DAT_06a499c8 /
 * DAT_06a49998==3 transition), the call lands earlier in execution
 * order than the engine, but the function is idempotent w.r.t. order
 * within sim_step_a since it only touches its own globals.
 *
 * The setters (FUN_004682c5/d0/e3) and the loader entry (FUN_00468338)
 * are unported in this chip — the globals stay BSS-zero in normal play,
 * so the tick body decrements toward 0 forever and does nothing
 * observable. Future chip lands the setters when a consumer of
 * `counter` (likely a sub-overlay alpha ramp) gets ported.
 */

#ifndef OPENRECET_STAGE_LOAD_PULSE_H
#define OPENRECET_STAGE_LOAD_PULSE_H

/* Read accessors — used by tests + future render-side consumers. */
int stage_load_pulse_get_active(void);
int stage_load_pulse_get_counter(void);

/* Setters — mirror the engine accessor cluster at 0x4682c5/d0/e3.
 * Not wired today; here so a future chip touching `g_stage_load_pulse`
 * doesn't reach into the module's internals. */
void stage_load_pulse_set_active(int active);
void stage_load_pulse_reset_counter_to_5(void);

/* Reset module state to BSS-zero defaults. Idempotent. */
void stage_load_pulse_reset(void);

/* Per-frame tick — port of FUN_004693e3. Idempotent on counter
 * extremes (active=0 + counter=0; active=1 + counter=5). */
void stage_load_pulse_tick(void);

#endif /* OPENRECET_STAGE_LOAD_PULSE_H */
