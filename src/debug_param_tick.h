/*
 * debug_param_tick.h — engine's per-frame debug-parameter tweaker.
 *
 * Engine source: FUN_00405552 @ 0x405552 (498 bytes).
 *
 * The function is a built-in debug overlay that lets a developer
 * adjust one of eight tunable globals (DAT_00529128..00529148) using
 * input bits 0x01 (increment) and 0x02 (decrement) on the just-held
 * mask DAT_073dddd6. Cursor DAT_00647e08 (0..7+) selects which value:
 *
 *   cursor  target              behaviour              clamp/step
 *   ------  -------------       --------------------   --------------
 *   0       DAT_00529128 (int)  mod-6 circular         {+5,+7} %6
 *   1       _DAT_0052912c (f32) free signed ±1.0       float
 *   2       DAT_00529130 (int)  clamped 200..2000      ±100
 *   3       DAT_00529134 (int)  clamped 0..255         ±1
 *   4       DAT_00529138 (int)  clamped 0..255         ±1
 *   5       DAT_0052913c (int)  clamped 0..255         ±1
 *   6       DAT_00529140 (int)  clamped 0..255         ±1
 *   7       DAT_00529144 (int)  clamped 0..255         ±1
 *   else    DAT_00529148 (int)  clamped 0..255         ±1
 *
 * The decrement arm additionally writes DAT_00648250 = DAT_073a6d74 =
 * 1 after the value tweak — likely a "params dirty, redraw needed"
 * pair consumed by a UI layer (engine FUN_00405744 sibling reads
 * DAT_00529134/8/c into a stack uint32 trio).
 *
 * Gate: DAT_06a49938 == 0 → entire body skipped. The gate is set
 * by an unported sub-menu entry path; in normal title-and-shop play
 * it stays BSS-zero forever, and the function is a no-op gate that
 * still fires once per sim_step_a frame.
 *
 * What this chip ports: the gate + the function-call boundary only.
 * The eight tunable globals, cursor, dirty-flag writes, and float
 * deltas are deferred until a future chip ports the debug-menu entry
 * path that sets DAT_06a49938 (or a consumer of one of the tweaked
 * values). The probe is marked CALL_TRACE_ENTER_STUB so the
 * call_trace_diff `≈` indicator surfaces the partial port honestly,
 * even though pure call-count parity matches retail at every frame
 * where the gate is 0 (= every frame in the captured pre-3D trace).
 */

#ifndef OPENRECET_DEBUG_PARAM_TICK_H
#define OPENRECET_DEBUG_PARAM_TICK_H

/* Gate accessor — used by tests to flip the gate and assert the body
 * still no-ops (we ported the gate, not the body). */
int  debug_param_tick_get_gate(void);
void debug_param_tick_set_gate(int gate);

/* Reset module state — gate=0. Idempotent. */
void debug_param_tick_reset(void);

/* Per-frame tick — port of FUN_00405552's gate. Returns early when
 * the gate is 0 (the only path exercised in normal play); when the
 * gate is 1, falls through to a no-op return because the body is
 * deferred. */
void debug_param_tick(void);

#endif /* OPENRECET_DEBUG_PARAM_TICK_H */
