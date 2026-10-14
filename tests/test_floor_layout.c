/*
 * test_floor_layout.c — src/floor_layout.c + src/actor_spawn.c against retail
 * golden values.  Exhaustive retail equality lives in tools/oracle/
 * p1_floor_layout.c (P1) and the rhost shadow hooks; these pin one real house
 * entry so a host-only change can't silently move the stream.
 *
 * Golden: buy-40pct-accept Continue load under rhost (seed 19937 = 0x4de1 at
 * FUN_0047df4d entry, stage class 0 / floor 0, house), dumped at f651.
 */
#include "t.h"

#include "actor_spawn.h"
#include "floor_layout.h"
#include "rng.h"

static int never(void) { return 0; }

static floor_layout_in_t house_in(void)
{
    floor_layout_in_t in = { 0, 0, 0, 0, 0, 0, never, never };
    return in;
}

int test_floor_layout_house_continue_golden(void)
{
    floor_layout_in_t in = house_in();
    rng_seed(0x4de1u);
    floor_layout_generate(&in);
    T_ASSERT_EQ_I(g_floor_w, 7);
    T_ASSERT_EQ_I(g_floor_h, 5);
    T_ASSERT_EQ_I(g_floor_ncells, 7);
    T_ASSERT_EQ_I(g_floor_origin[0], 3);
    T_ASSERT_EQ_I(g_floor_origin[1], 1);
    T_ASSERT_EQ_I(g_floor_marks[0][0], 1);   /* start */
    T_ASSERT_EQ_I(g_floor_marks[0][1], 1);
    T_ASSERT_EQ_I(g_floor_marks[1][0], 3);   /* exit */
    T_ASSERT_EQ_I(g_floor_marks[1][1], 2);
    T_ASSERT_EQ_I(g_floor_extra_n, 2);
    T_ASSERT_EQ_I(g_floor_rooms_total, 7);
    T_ASSERT(g_scene1_stage_player_default_pos[0] == -40.0f);
    T_ASSERT(g_scene1_stage_player_default_pos[1] == 0.0f);
    T_ASSERT(g_scene1_stage_player_default_pos[2] == -40.0f);
    T_ASSERT_EQ_I((int)g_rng_seed, (int)0xe844f803u);   /* 34 draws */
    return 0;
}

int test_floor_layout_actor_spawn_reset_golden(void)
{
    /* FUN_0041f252 right after the floor layout above: 8 draws */
    rng_seed(0xe844f803u);
    actor_spawn_reset();
    static const int32_t want[ACTOR_SPAWN_ORDER_N] = { 2, 0, 1, 5, 6, 4, 7, 3 };
    for (int i = 0; i < ACTOR_SPAWN_ORDER_N; i++)
        T_ASSERT_EQ_I(g_actor_spawn_order[i], want[i]);
    T_ASSERT_EQ_I((int)g_rng_seed, (int)0xc667e3abu);
    return 0;
}

int test_floor_layout_rec_flag_is_3x3_without_picks(void)
{
    /* a set work-record flag forces a 3x3 floor and skips every pick */
    floor_layout_in_t in = house_in();
    in.rec_flag_a = 1;
    rng_seed(1u);
    floor_layout_generate(&in);
    T_ASSERT_EQ_I(g_floor_w, 3);
    T_ASSERT_EQ_I(g_floor_h, 3);
    T_ASSERT_EQ_I(g_floor_extra_n, 0);
    T_ASSERT_EQ_I(g_floor_rooms_total, g_floor_ncells);
    return 0;
}
