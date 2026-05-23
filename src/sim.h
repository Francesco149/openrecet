/*
 * sim.h — per-frame simulation halves (FUN_004536cb / FUN_0049966a).
 *
 * The tick scheduler (src/tick.{c,h}) calls `sim_step_a` and `sim_step_b`
 * once each per simulation step, between `input_poll` and `render`.
 *
 * Engine source:
 *   - FUN_004536cb (sim_a): scene-state dispatcher. Runs the button-state
 *     ring (current/prev/pressed/held-with-repeat at DAT_073dddd0..d6),
 *     then dispatches by global scene index DAT_0438b1c0 into a per-scene
 *     sim function. State 0 → FUN_0049a59e (title).
 *   - FUN_0049966a (sim_b): music-track selector. Not ported in this
 *     commit; the scheduler tolerates a NULL `sim_b` callback.
 *
 * What's wired so far:
 *   - state 0 (title)  → `scene_title_sim_default`
 *   - state 1 (INGAME) → `scene1_ingame_tick` (Cs1, 2026-05-23 — minimal
 *                        port of FUN_004427d3; drives scene1_particles_tick)
 *   - states 2, 3, 6, 7, 8, 0xb, 0xd-0x10 → bare `scene1_particles_tick`
 *     (Cs2, 2026-05-23 — engine LAB_00453bed mass dispatch; per-state
 *     callees stubbed)
 *   - states 4, 5, 9, 0xa, 0xc, 0x11+ are no-ops (Cs3+ chip ladder; see
 *     docs/findings/sim-step-a-dispatch.md)
 *
 * The button-ring update is the only piece of FUN_004536cb that always
 * runs regardless of scene state, so it lives at the top of `sim_step_a`.
 * Pure-C testable helper `sim_button_ring_update` exposes the math.
 */
#ifndef OPENRECET_SIM_H
#define OPENRECET_SIM_H

#include <stdint.h>

/* ─── button-state ring ──────────────────────────────────────────────────
 * Mirrors the 16-bit-per-player state quad at DAT_073dddd0..d6 + the
 * 16-short per-bit repeat counter array at DAT_073dddda. The engine
 * keeps two of these (one per player) at stride 0x2a; we match that.
 *
 *   cur     (DAT_073dddd0):  this frame's accumulated button mask
 *                            (what input_poll writes to g_input_state[i].buttons)
 *   prev    (DAT_073dddd2):  the previous frame's `cur`, latched at the
 *                            tail of sim_a's ring update
 *   pressed (DAT_073dddd4):  ~prev & cur — bits that rose this frame
 *   held    (DAT_073dddd6):  cur ANDed with a key-repeat gate (a bit
 *                            held continuously appears in `held` only
 *                            every 4 frames after the initial 12-frame
 *                            settle window). See `sim_button_ring_update`.
 *   repeat  (DAT_073dddda):  one short per bit, [0..16). The values run
 *                            in the engine's `*= -1`-as-signed-clamp
 *                            pattern (see decompile for details).
 *
 * Field order matches the engine struct exactly so a future hot-load
 * test could memcpy in/out if it wanted. */
#define SIM_NUM_PLAYERS  2

struct sim_player_buttons {
    uint16_t cur;
    uint16_t prev;
    uint16_t pressed;
    uint16_t held;
    int16_t  repeat[16];
};

extern struct sim_player_buttons g_sim_buttons[SIM_NUM_PLAYERS];

/* Frame counter at DAT_0438b8cc — incremented at the tail of sim_a after
 * the per-scene sim returns. Exposed so tests can assert it advances. */
extern uint32_t g_sim_frame_count;

/* ─── pure-C helpers (testable without globals) ─────────────────────── */

/* One frame of the per-bit button-state ring. Reads `cur` and the
 * (in/out) `prev` + `repeat[16]` arrays, writes `*out_pressed` and
 * `*out_held`. Mirrors the inner do-while at lines 42..70 of
 * FUN_004536cb decomp.
 *
 * Logic per bit:
 *   - rising edge (bit set in cur, clear in prev): pressed bit goes set,
 *     repeat counter latches to 0xc (12).
 *   - unchanged (bit's value same as prev):
 *       counter > 0xc → clamp to 0xc
 *       counter < 1   → reset to 4 (the auto-repeat reload value)
 *       else          → decrement; while counter > 0 the bit is
 *                       cleared from the `held` mask.
 *   - falling edge (bit clear in cur, set in prev): counter latches
 *     to 0xc.  `held` already reflects cur (=0) so no extra masking.
 *
 * After the per-bit loop, `prev` is overwritten with `cur` so the next
 * call sees the latest as previous. */
void sim_button_ring_update(uint16_t cur,
                            uint16_t *prev,
                            int16_t   repeat[16],
                            uint16_t *out_pressed,
                            uint16_t *out_held);

/* ─── lifecycle + per-frame entry points ────────────────────────────── */

/* Zero `g_sim_buttons` + `g_sim_frame_count`. Idempotent. Called from
 * main.c after window/D3D init. */
void sim_init(void);

/* The tick-scheduler `.sim_a` callback. Reads `g_input_state[i].buttons`,
 * advances the per-player button ring into `g_sim_buttons[i]`, and
 * dispatches by scene state (currently only state==0 / title). Tail:
 * `g_sim_frame_count++`.
 *
 * Engine FUN_004536cb L4-9 (the "while worker loading" guard):
 *   - If the primary asset-load worker is busy, pump the scene-effect
 *     counters via `sim_loading_pump` and early-return — the rest of
 *     sim (button ring, scene dispatch, fade tick, frame counter) is
 *     SKIPPED on those frames. This is what freezes the player's input
 *     and the title menu's animation while "Now Loading…" is up.
 *   - Once the worker is no longer busy, clear the primary nowloading
 *     gate so the overlay drops on the very next render. The secondary
 *     nowloading gate (collapsed-OR'd into the same `nowloading.g_active`
 *     today) stays raised — only a secondary thread cleanup can drop
 *     that.
 *
 * Note: the engine ALSO calls FUN_004532df unconditionally at the top
 * of FUN_004547ab (render), so during loading the counters tick twice
 * per frame (sim + render). Outside loading, only render pumps them.
 * That 2× pump is dormant today because no consumer reads these
 * counters yet — they only matter once scene-1 render lands. */
void sim_step_a(void);

/* ─── FUN_004532df scene-effect counter pump ─────────────────────────────
 *
 * Engine source: FUN_004532df @ 0x4532df (129 bytes). Advances four
 * counters + reads one mode flag.
 *
 *   DAT_06a49990 — 1..0x1f cyclic (wraps to 0 at 0x20). Read by the
 *                  white-flash overlay in FUN_004547ab L50965.
 *   DAT_06a49994 — 1..(threshold-1) cyclic (wraps to 0 when it hits
 *                  the latched threshold). Read by the screen-shake
 *                  effect in FUN_004547ab L50790.
 *                  Threshold (DAT_005c5938) latched by FUN_004532bc.
 *   DAT_06a49998 — depends on mode (DAT_06a499a0):
 *                  mode==0: 1..0x13 cyclic, wraps to 0 at 0x14
 *                  mode!=0: clamped to [1, 0xc] — runs up to 0xc and
 *                           stays there until something writes mode=0
 *                           or clears the counter.
 *   DAT_06a4999c — engine pumps this in FUN_004547ab not here; we
 *                  don't pump it from sim. Same mode-flag-gated shape
 *                  as DAT_06a49998. Exposed only for parity / future
 *                  consumers.
 *   DAT_06a499a0 — mode flag controlling the 998/99c counters. Reset
 *                  to 0 by FUN_00453373.
 *
 * Counters only advance if their value is > 0; they sit at 0 dormant
 * until external setters (FUN_004532b1 / FUN_004532bc / and a few
 * scene-transition helpers) start them. Today every starter is
 * unported, so the pump is a no-op against BSS-zero state. We port
 * it so the per-tick "if worker busy → pump + return" guard at the
 * top of sim_step_a matches the engine's control flow, ready for
 * the scene-1 render port to start using them. */

/* Pure-C side. Inputs/outputs are by reference so tests can drive any
 * combination of counter state. Returns nothing. The `threshold94`
 * arg corresponds to DAT_005c5938 — the engine's latched-once-per-
 * scene threshold for counter 994. */
void sim_loading_pump_pure(int32_t *c990,
                           int32_t *c994,
                           int32_t *c998,
                           int32_t  mode,
                           int32_t  threshold94);

/* Pump against the module-level globals. Called from sim_step_a's busy
 * branch — mirror of the engine's `FUN_004536cb` -> `FUN_004532df`
 * call edge. */
void sim_loading_pump(void);

/* Inspect / set / reset the scene-effect counter state for tests. The
 * engine writes the 90/94 starters via FUN_004532b1 / FUN_004532bc
 * (both unported); the 998/99c/9a0 globals are written by a cluster
 * of scene-transition helpers (also unported). These getters/setters
 * are deliberately test-shaped — the engine has no analogue. */
int32_t sim_get_counter_990(void);
int32_t sim_get_counter_994(void);
int32_t sim_get_counter_998(void);
int32_t sim_get_counter_99c(void);
int32_t sim_get_mode_9a0(void);
int32_t sim_get_threshold94(void);

void    sim_set_counter_990(int32_t v);
void    sim_set_counter_994(int32_t v, int32_t threshold94);
void    sim_set_counter_998(int32_t v);
void    sim_set_counter_99c(int32_t v);
void    sim_set_mode_9a0(int32_t v);

#endif /* OPENRECET_SIM_H */
