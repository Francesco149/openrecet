/*
 * debug_param_tick.c — see debug_param_tick.h.
 *
 * Engine source: FUN_00405552 @ 0x405552 (498 bytes).
 *
 * Body deferred — this chip ports the gate + function-call boundary
 * only. The eight tunable globals, cursor select, dirty-flag pair,
 * and increment/decrement arms (engine asm 0x405562..0x405740) all
 * stay unported until a consumer of the tweaked values lands. In
 * normal play DAT_06a49938 is BSS-zero forever, so the deferred
 * body is unreachable in any captured trace — the call_trace_diff
 * row matches retail's count parity, and the CALL_TRACE_ENTER_STUB
 * marker keeps the partial port visible as `≈` for future eyes.
 */

#include "debug_param_tick.h"

#include "call_trace.h"

/* DAT_06a49938 — debug-menu activation gate. BSS-zero in normal play. */
static int g_gate = 0;

int  debug_param_tick_get_gate(void)        { return g_gate; }
void debug_param_tick_set_gate(int gate)    { g_gate = gate ? 1 : 0; }

void debug_param_tick_reset(void)           { g_gate = 0; }

void debug_param_tick(void)
{
    /* E.2 probe — FUN_00405552 @ 0x405552. Body intentionally deferred
     * (see header). STUB marker keeps the partial port honest in the
     * call_trace_diff output even when count parity matches. */
    CALL_TRACE_ENTER_STUB(0x405552u);

    if (g_gate == 0) {
        /* Engine asm 0x405556 → 0x405741: early return. */
        return;
    }

    /* Engine body deferred. When the debug-menu entry path lands and
     * a consumer of DAT_00529128..00529148 is ported, replace this
     * return with the eight-way switch on DAT_00647e08 keyed by
     * `g_sim_buttons[0].held & 0x3` (engine DAT_073dddd6) and lift the
     * probe to CALL_TRACE_ENTER. */
}
