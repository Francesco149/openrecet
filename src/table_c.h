/*
 * table_c.h — table C (DAT_06956cb0: 200 records × 0x25 dwords; the EXP orbs,
 * the dropped items, the pickups flying to the bag), in retail layout
 * (scene1_records.h's g_scene1_records_c).
 *
 *   FUN_0044284b  table_c_tick          every tick: the orb cap, the motion
 *                                       (drag 0.97, the 3-ray wall push, the
 *                                       orbs homing after 60), the ground
 *                                       bounce, a pickup's float to the bag
 *   FUN_00459dfd  table_c_bound_update  (the render's) DAT_0076b968 = the last
 *                                       live record + 1 — the sim's loops stop
 *                                       there, so a record spawned past it
 *                                       waits a frame
 *
 * Dword fields: [0..2] pos, [3..5] vel, [9] ground y, [10] kind (-1 free; 0..3
 * orbs, >= 7 items: id + 7), [11] age, [12] speed scale, [14] rare, [15] grade,
 * [16] state (0 flying, 1 resting, 2 to the bag), [17] a hit's orb, [19..35]
 * the ground probe, [36] frozen.
 *
 * Float code mirrors the retail disassembly (x87.h).
 */
#ifndef OPENRECET_TABLE_C_H
#define OPENRECET_TABLE_C_H

#include <stdint.h>

#include "scene1_records.h"

#ifdef __cplusplus
extern "C" {
#endif

#define g_table_c_bound g_scene1_records_c_count   /* DAT_0076b968 */

void table_c_tick(void);             /* FUN_0044284b */
void table_c_bound_update(void);     /* FUN_00459dfd's table-C count */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_TABLE_C_H */
