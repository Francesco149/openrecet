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
 *   - state 0 (title) calls `scene_title_sim` (see scene_title.{c,h})
 *   - every other state is a one-time logged no-op
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
 * `g_sim_frame_count++`. */
void sim_step_a(void);

#endif /* OPENRECET_SIM_H */
