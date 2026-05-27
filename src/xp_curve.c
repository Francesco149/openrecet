/*
 * xp_curve.c — see xp_curve.h.
 *
 * Engine source: FUN_0048a331 @ 0x48a331 (23 B).
 *
 *   if (param_1 != 0) iVar1 = (param_1 + 1) * param_1 * 0x96;
 *   else              iVar1 = 0;
 *   return iVar1;
 */

#include "xp_curve.h"

#include "call_trace.h"

int32_t xp_curve_threshold(int32_t level)
{
    /* E.2 probe — FUN_0048a331 @ 0x48a331. */
    CALL_TRACE_ENTER(0x48a331u);

    if (level == 0) {
        return 0;
    }
    return (level + 1) * level * 0x96;
}
