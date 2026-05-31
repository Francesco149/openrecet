/*
 * scene1_fx_overlays.c — see scene1_fx_overlays.h.
 *
 * Scaffold port of FUN_00454191.  Body is the engine's two outer gate
 * checks; both inner render branches are deferred (the counter starters
 * that would activate them are unported today).  CALL_TRACE_ENTER_STUB
 * keeps the partial port visible in call_trace_diff as `≈`.
 *
 * PORT-DEBT(stub, FUN_00454191): outer-gate scaffold only; the 3 inner render
 * branches (alpha quad + screen-capture dim + white-flash overlay) are deferred
 * until the counter starters port. Retire = full FUN_00454191 body.
 */

#include "scene1_fx_overlays.h"

#include "call_trace.h"
#include "sim.h"

void scene1_fx_overlays(struct IDirect3DDevice8 *dev)
{
    /* Engine FUN_00454191 @ 0x454191.  Marked STUB: the function body
     * is the entry probe + outer-gate scaffold.  All three inner render
     * branches (cleanup dance + simple alpha quad + full screen-capture
     * dim quad + white-flash overlay) are deferred until the counter
     * starters port. */
    CALL_TRACE_ENTER_STUB(0x454191u);
    (void)dev;

    /* Engine L8: gate `1 < DAT_06a4999c`.  Counter is BSS-zero today
     * (no starter ported).  When > 1, the engine runs:
     *
     *   - value 2 + sfnouse=0:  cleanup of saved RT/DS refs.
     *   - 2 < counter: outer dispatch with three sub-branches:
     *       · sfnouse=1: simple alpha quad (counter*0x10 → 0..0xff)
     *       · counter==3: full screen-capture render-to-texture +
     *                     SetTransform/SetTexture/Clear dance.
     *       · else: dim quad with counter*0x16 alpha + late-frame
     *               attenuation when counter > 0xc.
     */
    if (1 < sim_get_counter_99c()) {
        /* Body deferred — see header. */
    }

    /* Engine L121: gate `(1 < DAT_06a49990) && (DAT_0438b1b0 == 0)`.
     * The 990 counter is BSS-zero today; sfnouse is also user-controlled
     * via recet.ini and almost always 0.  When 990 > 1 and sfnouse=0,
     * the engine runs a white-flash overlay quad with alpha derived
     * from the counter (value 2 = cleanup; 2 < value = quad with full
     * 0xffffffff or counter-modulated alpha). */
    if (1 < sim_get_counter_990()) {
        /* Body deferred — see header. */
    }
}
