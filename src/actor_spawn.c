/*
 * actor_spawn.c — see actor_spawn.h.
 */
#include "actor_spawn.h"

#include "call_trace.h"
#include "rng.h"
#include "scene1_particles_tick.h"   /* g_scene1_people */

int32_t g_actor_spawn_order[ACTOR_SPAWN_ORDER_N];
int32_t g_actor_spawn_7c8c58;
int32_t g_actor_spawn_done;
int32_t g_actor_spawn_serial;

void actor_spawn_reset(void)
{
    /* E.2 probe — FUN_0041f252 @ 0x41f252. */
    CALL_TRACE_ENTER(0x41f252u);

    for (int i = 0; i < SCENE1_PEOPLE_COUNT; i++) {
        g_scene1_people[i].alive = 0;          /* +0x44 */
        g_scene1_people[i].npc_field_6ec = 0;
        g_scene1_people[i].npc_field_774 = 0;
    }
    g_actor_spawn_7c8c58 = 0;
    g_actor_spawn_done = 0;
    g_actor_spawn_serial = 0;
    for (int i = 0; i < ACTOR_SPAWN_ORDER_N; i++)
        g_actor_spawn_order[i] = i;
    for (int i = 0; i < ACTOR_SPAWN_ORDER_N; i++) {
        int r = rng_next15() & 7;
        int32_t t = g_actor_spawn_order[i];
        g_actor_spawn_order[i] = g_actor_spawn_order[r];
        g_actor_spawn_order[r] = t;
    }
}
