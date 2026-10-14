/*
 * dungeon_party.h — the party on a dungeon floor (*DAT_068dd2f0 >= 1): the
 * player controller FUN_0048b850 and the followers, on the party block
 * (party.h).
 *
 *   FUN_0048b850  dungeon_party_tick        the player controller (dungeon path)
 *   FUN_0048a833  dungeon_party_follow_tick the partner (actor 1) follows the
 *                                           player, Tear (actor 2) the partner
 *   FUN_0048a4d1  party_follow              Tear's spring-follow of an actor
 *   FUN_0048407f  dungeon_event_actor_tick  the actors during an event (dialogue)
 *   FUN_004897c6  party_status_tick         the status timers + the 3 anim ticks
 *   FUN_00483e7b  party_aura_tick           the stage's ambient particles
 *   FUN_00482a51  party_anim_set            an 11-dword record's anim
 *
 * The house runs its own reconstruction of FUN_0048b850 / FUN_0048a833
 * (scene1_player_ctrl.c, scene1_companion_ctrl.c) —
 * PORT-DEBT(structure, FUN_0048b850): one port for both.
 */
#ifndef OPENRECET_DUNGEON_PARTY_H
#define OPENRECET_DUNGEON_PARTY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void party_anim_set(int32_t *rec, int32_t anim);   /* FUN_00482a51 */
void party_follow(int target, int set_anim);       /* FUN_0048a4d1 */
void party_status_tick(void);                      /* FUN_004897c6(&DAT_056da1b8) */
void party_aura_tick(void);                        /* FUN_00483e7b */
void dungeon_party_follow_tick(void);              /* FUN_0048a833 */
void dungeon_party_tick(void);                     /* FUN_0048b850 */
void dungeon_event_actor_tick(void);               /* FUN_0048407f */

/* FUN_00436f97's party resets (the actor loop + the block's scalars) and the
 * spawn: the three actors at the floor origin, 1.5 apart in x; actor 0 probes
 * up to 5 points around it for ground (FUN_00432e50). */
void party_stage_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_DUNGEON_PARTY_H */
