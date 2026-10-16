/*
 * dungeon_party.h — the party: the player controller FUN_0048b850 and the
 * followers, on the party block (party.h), in the house and on a floor.
 *
 *   FUN_0048b850  party_tick                the player controller
 *   FUN_0048a833  party_follow_tick         the partner (actor 1) follows the
 *                                           player; Tear (actor 2) follows the
 *                                           partner, stands beside the player
 *                                           or wanders the shop
 *   FUN_0048a4d1  party_follow              Tear's spring-follow of an actor
 *   FUN_0048407f  dungeon_event_actor_tick  the actors during an event (dialogue)
 *   FUN_004897c6  party_status_tick         the status timers + the 3 anim ticks
 *   FUN_00483e7b  party_aura_tick           the stage's ambient particles
 *   FUN_00482a51  party_anim_set            an 11-dword record's anim
 *
 * The house's other FUN_0048a833 call sites (FUN_0048670f outside
 * FUN_0048b850, the customer-service master tick) still run the legacy
 * scene1_companion_ctrl.c — PORT-DEBT(structure, FUN_0048a833).
 */
#ifndef OPENRECET_DUNGEON_PARTY_H
#define OPENRECET_DUNGEON_PARTY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void party_anim_set(int32_t *rec, int32_t anim);   /* FUN_00482a51 */
float angle_ease(float a, float b, float step);    /* FUN_00482ae7 */
void party_follow(int target, int set_anim);       /* FUN_0048a4d1 */
void party_status_tick(void);                      /* FUN_004897c6(&DAT_056da1b8) */
void party_aura_tick(void);                        /* FUN_00483e7b */
void party_follow_tick(void);                      /* FUN_0048a833 */
void party_tick(void);                             /* FUN_0048b850 */
void dungeon_event_actor_tick(void);               /* FUN_0048407f */
void party_event_follow(void);                     /* FUN_0048407f's FUN_0048a4d1(v31, 0) + sparkle */

/* FUN_00436f97's party resets (the actor loop + the block's scalars) and the
 * spawn: the three actors at the floor origin, 1.5 apart in x; actor 0 probes
 * up to 5 points around it for ground (FUN_00432e50). */
void party_stage_reset(void);

/* FUN_00485712: an accessory's proc (1 = worn; the break roll, the notice) */
int party_accessory_proc(int32_t id, uint32_t brk, int32_t stop);
extern int32_t g_notice_item, g_notice_kind, g_notice_side, g_notice_on, g_notice_ticks, g_notice_b76c;
extern int32_t g_broke_on, g_broke_item;

/* DAT_0438be6c / be70 / be74 / be78: Tear's house wander (state, ticks, target) */
extern int32_t g_tear_wander, g_tear_wander_t;
extern float   g_tear_wander_x, g_tear_wander_z;

int  party_bag_full(void);                                     /* FUN_00484e45 */
int  party_bag_add(int32_t item, int32_t cap);                 /* FUN_00484da3 */
void party_get_item(int32_t kind, int32_t rare, int32_t grade);   /* FUN_00484dd1 */
extern int32_t g_got_item, g_got_item_on, g_got_item_bubble;    /* DAT_00648290 / 94 / 50 */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_DUNGEON_PARTY_H */
