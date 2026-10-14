/*
 * party_move.h — the player's move + collision on the party block (party.h).
 *
 *   FUN_00483170  party_move        push off enemy bodies and solid objects,
 *                                   the try-move with the slope-edge slide,
 *                                   the radial wall push, the ground snap of
 *                                   actors 0 and 1
 *   FUN_00482c43  party_push        the player against one body circle
 *   FUN_004830f1  party_try_move    ground under (pos + vel)
 *   FUN_00433fa6  seg2_cross        2D segment intersection
 *   FUN_00434198  seg2_side         the orientation sign
 *
 * Float code mirrors the retail disassembly (x87.h).
 */
#ifndef OPENRECET_PARTY_MOVE_H
#define OPENRECET_PARTY_MOVE_H

#include <stdint.h>

#include "map_collision.h"

#ifdef __cplusplus
extern "C" {
#endif

int  seg2_side(float a1, float a2, float a3, float a4, float a5, float a6);   /* FUN_00434198 */
int  seg2_cross(const float a[4], const float b[4]);                         /* FUN_00433fa6 */
int  party_try_move(float x, float y, float z, float vx, float vy, float vz,
                    mapcol_hit_t *hit);                                       /* FUN_004830f1 */
int  party_push(const float body[3], float radius, float height);            /* FUN_00482c43 */
void party_move(void);                                                       /* FUN_00483170 */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_PARTY_MOVE_H */
