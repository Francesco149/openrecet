/*
 * scene1_per_frame_open.h — per-frame open of the scene-1 particle
 * integrator (engine FUN_00414929, called at L1 of FUN_0040fb3a).
 *
 * The full FUN_00414929 ticks two unrelated entity tables before the
 * particle integrator runs its per-type handlers.  This header lands
 * Table A — a 256-slot spawn-request queue at engine `DAT_00730c20`
 * (sentinel field aliased at `DAT_00730c30`).  The Table B half is
 * already covered by `scene1_overlay.{c,h}` (chip O.2's slot table).
 *
 * Chip PFO.1 (this header): typed storage + sentinel init only.  The
 * tick body, the parent-template table at `DAT_007444e0`, and the
 * allocators FUN_004132c1 / FUN_0041331d land in PFO.2..PFO.6.  See
 * `docs/findings/scene1-per-frame-open.md` for the full ladder.
 *
 * Dormant in HOUSE today — no allocator caller exists, so all 256
 * slots stay sentinel-empty after the init runs.
 */
#ifndef SCENE1_PER_FRAME_OPEN_H
#define SCENE1_PER_FRAME_OPEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCENE1_PFO_TABLE_A_COUNT   256
#define SCENE1_PFO_TABLE_A_STRIDE  11   /* dw count; 44 B per slot */

/*
 * Slot layout (dw indices within an 11-dw slot).  The engine's tick
 * indexes off the sentinel field at +4 dw (= `DAT_00730c30`-aliased
 * pointer); we store the slot as a contiguous 11-dw array starting
 * at dw 0.
 *
 * Field semantics from the allocator FUN_004132c1 (10-arg pose-style)
 * and FUN_0041331d (9-arg explicit).  See survey doc for tick-side
 * reads.
 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM0   0   /* DAT_00730c20 — entry base; allocator-set */
#define SCENE1_PFO_TABLE_A_OFF_PARAM1   1   /* DAT_00730c24 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM2   2   /* DAT_00730c28 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM3   3   /* DAT_00730c2c — FUN_004132c1 sets to -520.0f */
#define SCENE1_PFO_TABLE_A_OFF_SENTINEL 4   /* DAT_00730c30 — TYPE / parent-template id; -1 = empty */
#define SCENE1_PFO_TABLE_A_OFF_PARAM5   5   /* DAT_00730c34 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM6   6   /* DAT_00730c38 */
#define SCENE1_PFO_TABLE_A_OFF_PARAM7   7   /* DAT_00730c3c */
#define SCENE1_PFO_TABLE_A_OFF_PARAM8   8   /* DAT_00730c40 */
#define SCENE1_PFO_TABLE_A_OFF_AGE      9   /* DAT_00730c44 — incremented per tick; kill at 300 */
#define SCENE1_PFO_TABLE_A_OFF_MODE    10   /* DAT_00730c48 — FUN_004132c1 sets to 1, FUN_0041331d sets to 0 */

extern int32_t g_scene1_pfo_table_a[SCENE1_PFO_TABLE_A_COUNT *
                                    SCENE1_PFO_TABLE_A_STRIDE];

/*
 * Engine FUN_00414902 Table A half (L12548-L12552): set every slot's
 * SENTINEL field to -1.  Other fields untouched (engine doesn't zero
 * them either).  Called from FUN_0040f64b on every scene-1 entry —
 * wired into `scene1_records_reset()` to match.
 */
void scene1_pfo_table_a_init(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_PER_FRAME_OPEN_H */
