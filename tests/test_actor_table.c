/*
 * test_actor_table.c — src/actor_table.c (FUN_0041f4a6 / FUN_0041f319 /
 * FUN_0041f903).  Retail equality: the rhost enemy_place shadow compares the
 * whole table after every placement; these pin the formulas and defaults.
 */
#include "t.h"

#include <string.h>

#include "actor_table.h"
#include "rng.h"
#include "tables_enemy.h"

static actor_stage_t stage(int32_t dungeon, int32_t floor)
{
    actor_stage_t st = { dungeon, floor, -1, 0, 0 };
    return st;
}

int test_actor_table_enemy_level(void)
{
    actor_stage_t st = stage(0, 3);
    T_ASSERT_EQ_I(actor_enemy_level(&st, 0), 3);
    st = stage(2, 10);
    T_ASSERT_EQ_I(actor_enemy_level(&st, 0), 10 / 3 + 6);
    st = stage(5, 40);                       /* (40 − 30)·1.7 + 50 = 67 */
    T_ASSERT_EQ_I(actor_enemy_level(&st, 0), 67);
    st = stage(0, 0);
    st.event_dungeon = 1;                    /* (5·floor − 145)/2 + 2 */
    st.floor = 30;
    T_ASSERT_EQ_I(actor_enemy_level(&st, 0), 2 + 2);
    st = stage(1, 10);                       /* lv 7; type 34 adds kills/5: lo = lv+10 = 17 > hi = 2lv → capped at lo */
    st.kills = 100;
    T_ASSERT_EQ_I(actor_enemy_level(&st, 34), 17);
    return 0;
}

int test_actor_table_alloc_defaults_and_cooldown(void)
{
    memset(g_actor, 0, sizeof g_actor);
    g_actor_alloc_n = 0;
    act_seti(&g_actor[0], ACT_INUSE, 1);          /* busy */
    act_seti(&g_actor[1], ACT_COOLDOWN, 2);       /* free, cooling down: 2 → 1, skipped */
    actor_stage_t st = stage(0, 2);
    actor_t *a = actor_alloc(&st);
    T_ASSERT(a == &g_actor[2]);
    T_ASSERT_EQ_I(act_geti(&g_actor[1], ACT_COOLDOWN), 1);
    T_ASSERT_EQ_I(g_actor_alloc_n, 1);
    T_ASSERT_EQ_I(act_geti(a, ACT_SLOT), 2);
    T_ASSERT_EQ_I(act_geti(a, ACT_LEVEL), 2);
    T_ASSERT_EQ_I(act_geti(a, 0x14), 0);          /* −1, then FUN_00482a51(0) resets the block */
    T_ASSERT_EQ_I(act_geti(a, 0x18), 1);
    T_ASSERT(act_getf(a, 0x3e0) == -1000.0f);
    T_ASSERT_EQ_I(act_geti(a, 0xb5c), -1);
    T_ASSERT_EQ_I(act_geti(a, 0x3dc), 255);
    a = actor_alloc(&st);                         /* slot 1 counts down to 0 → taken */
    T_ASSERT(a == &g_actor[1]);
    return 0;
}

int test_actor_table_spawn_enemy_hp(void)
{
    memset(g_actor, 0, sizeof g_actor);
    g_enemy[3].hp = 50;
    actor_stage_t st = stage(0, 5);               /* level 5 → HP 50 × (5·0.2 + 1) = 100 */
    rng_seed(1);
    actor_t *a = actor_spawn_enemy(&st, -40.0f, 0.5f, -80.0f, 3);
    T_ASSERT(a != NULL);
    T_ASSERT(act_getf(a, ACT_HP_MAX) == 100.0f && act_getf(a, ACT_HP) == 100.0f);
    T_ASSERT_EQ_I(act_geti(a, ACT_TYPE), 3);
    T_ASSERT(act_getf(a, ACT_HOME + 8) == -80.0f);
    T_ASSERT(act_getf(a, ACT_YAW) == 3.1415927f);
    rng_seed(1);
    (void)rng_next15();
    const int32_t phase = rng_next15() & 0x3f;    /* draw 1 dropped, draw 2 → +0xa50 */
    T_ASSERT_EQ_I(act_geti(a, ACT_PHASE_RND), phase);
    st.weak_enemies = 1;
    a = actor_spawn_enemy(&st, 0.0f, 0.0f, 0.0f, 3);
    T_ASSERT(act_getf(a, ACT_HP_MAX) == 10.0f);
    return 0;
}
