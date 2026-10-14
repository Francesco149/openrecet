/*
 * actor_spawn.h — stage-entry actor-table reset (FUN_0041f252).
 *
 * FUN_00436f97 (stage-entry init) calls it right after FUN_0040d132/0040f64b,
 * on every stage entry including the house.  It clears three fields of each
 * of the 128 people-table actors (g_scene1_people, DAT_0076bd54 stride 0xba4)
 * and resets the per-stage fixed-actor placer FUN_0042df55's state: its
 * once-per-stage latch, its serial counter, and the 8-entry shuffled
 * variant order it hands out (8 rand() draws — the reason this matters
 * before the placer itself is ported).
 */
#ifndef OPENRECET_ACTOR_SPAWN_H
#define OPENRECET_ACTOR_SPAWN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTOR_SPAWN_ORDER_N 8

extern int32_t g_actor_spawn_order[ACTOR_SPAWN_ORDER_N];   /* DAT_007c8c38 */
extern int32_t g_actor_spawn_7c8c58;                        /* DAT_007c8c58 */
extern int32_t g_actor_spawn_done;                          /* DAT_007c8c5c — FUN_0042df55 latch */
extern int32_t g_actor_spawn_serial;                        /* DAT_007c8c60 */

void actor_spawn_reset(void);   /* FUN_0041f252 */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_ACTOR_SPAWN_H */
