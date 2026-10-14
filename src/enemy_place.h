/*
 * enemy_place.h — the per-floor enemy placement of the stage-entry init
 * FUN_00436f97 (its `> 4` dungeon arm).
 *
 *   FUN_00431d54  enemy_place_floor   groups of enemies.txt's floor roster in the
 *                                     floor's rooms: random spot 5..19 from the
 *                                     room centre, no wall on the way
 *                                     (FUN_00433674), ground under it (FUN_00432e50)
 *   FUN_00431b1c  enemy_place_boss    a boss floor's fixed boss spots (else falls
 *                                     back to enemy_place_floor)
 *
 * Reads the floor layout (floor_layout.h), the enemylist sections
 * (g_enemylist, DAT_0053f8e8), the work record and the map collision
 * (map_collision.h); spawns through actor_table.h.  Writes the room numbers
 * into the debug text rows 30.. (FUN_00451874, audio_mci_record_command).
 */
#ifndef OPENRECET_ENEMY_PLACE_H
#define OPENRECET_ENEMY_PLACE_H

#include <stdint.h>

#include "actor_table.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    actor_stage_t  st;              /* dungeon / floor / event / kills / weak (FUN_0041f903) */
    const uint8_t *rec;             /* the active work record (DAT_044e3798 + slot·0x2dfc8) */
    int          (*is_boss_floor)(void);   /* FUN_0043195d */
    int          (*gate_query)(void);      /* FUN_004319d6 */
} enemy_place_in_t;

void enemy_place_floor(const enemy_place_in_t *in);   /* FUN_00431d54 */
void enemy_place_boss(const enemy_place_in_t *in);    /* FUN_00431b1c */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_ENEMY_PLACE_H */
