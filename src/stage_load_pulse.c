/*
 * stage_load_pulse.c — see stage_load_pulse.h.
 *
 * Engine source: FUN_004693e3 @ 0x4693e3 (41 bytes).
 *
 * Asm verbatim (cmpl/jle/decl + cmpl/jge/incl over the two DATs):
 *
 *   004693e3  cmpl $0x0, [0x734b9a0]
 *             jne  inc
 *             cmpl $0x0, [0x734b98c]
 *             jle  ret
 *             decl [0x734b98c]
 *             ret
 *   inc:      cmpl $0x5, [0x734b98c]
 *             jge  ret
 *             incl [0x734b98c]
 *   ret:      ret
 *
 * Bounds are half-open in the "settle" direction and clamp at the limit:
 * counter ramps 0 → 5 when active, 5 → 0 when inactive. Five frames to
 * fully ramp either way.
 */

#include "stage_load_pulse.h"

#include "call_trace.h"

/* DAT_0734b9a0 — set by the stage loader (FUN_00468338) on enter,
 * cleared by FUN_004682d0 on completion. BSS-zero defaults. */
static int g_active = 0;

/* DAT_0734b98c — clamped 0..5 ramp counter. BSS-zero defaults. */
static int g_counter = 0;

int  stage_load_pulse_get_active(void)  { return g_active; }
int  stage_load_pulse_get_counter(void) { return g_counter; }

void stage_load_pulse_set_active(int active)      { g_active = active ? 1 : 0; }
void stage_load_pulse_reset_counter_to_5(void)    { g_counter = 5; }

void stage_load_pulse_reset(void)
{
    g_active  = 0;
    g_counter = 0;
}

void stage_load_pulse_tick(void)
{
    /* E.2 probe — FUN_004693e3 @ 0x4693e3. Full body parity. */
    CALL_TRACE_ENTER(0x4693e3u);

    if (g_active == 0) {
        if (g_counter > 0) {
            g_counter--;
        }
    } else {
        if (g_counter < 5) {
            g_counter++;
        }
    }
}
