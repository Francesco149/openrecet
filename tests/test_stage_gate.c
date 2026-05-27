/*
 * test_stage_gate.c — tests for src/stage_gate.{c,h}.
 *
 * Covers each of the three ported predicates:
 *
 *   stage_gate_boss_id_allowed  (FUN_00431990) — pure id table
 *   stage_gate_floor_is_checkpoint (FUN_0043195d) — globals predicate
 *   stage_gate_query (FUN_004319d6) — full outer gate (uses
 *                                     g_enemylist.sections fixture)
 *
 * Tests mutate g_enemylist.sections + g_scene1_combat_stage_id +
 * the stage_gate next setter.  We clear back to a safe state at the
 * top of every test to stay order-independent.
 */

#include "t.h"
#include "stage_gate.h"
#include "scene1_combat_sm.h"   /* g_scene1_combat_stage_id */
#include "tables_enemylist.h"   /* g_enemylist + ENEMYLIST_* */

/* Reset to a "no boss anywhere" baseline: every dungeon's section[0]
 * already terminates with floor_lo = -1, every enemy slot terminates
 * with enemy_id = -1.  We also clear next and current stage. */
static void stage_gate_test_reset(void)
{
    for (int d = 0; d < ENEMYLIST_DUNGEON_SLOTS; ++d) {
        for (int s = 0; s < ENEMYLIST_SECTIONS_PER_DUNGEON; ++s) {
            g_enemylist.sections[d][s].floor_lo = -1;
            g_enemylist.sections[d][s].floor_hi = -1;
            for (int e = 0; e < ENEMYLIST_ENEMY_SLOTS_PER_SECTION; ++e) {
                g_enemylist.sections[d][s].enemies[e].enemy_id = -1;
                g_enemylist.sections[d][s].enemies[e].variant  = 0;
                g_enemylist.sections[d][s].enemies[e].count    = 0;
            }
        }
    }
    g_scene1_combat_stage_id = 0;
    stage_gate_set_next(0);
}

/* ─── FUN_00431990 — boss-id predicate ──────────────────────────────── */

int test_stage_gate_boss_id_low_range_17_to_19(void)
{
    /* The first allowed range: 0x17..0x19 inclusive. */
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x16), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x17), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x18), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x19), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x1a), 0);
    return 0;
}

int test_stage_gate_boss_id_pair_1b_1c_singletons_29_2b(void)
{
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x1b), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x1c), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x1d), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x29), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x28), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x2a), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x2b), 1);
    return 0;
}

int test_stage_gate_boss_id_high_range_36_to_49(void)
{
    /* Three high-range chunks: {0x31}, [0x36..0x37], [0x3b..0x49]. */
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x30), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x31), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x32), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x35), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x36), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x37), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x38), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x3a), 0);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x3b), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x49), 1);
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0x4a), 0);
    return 0;
}

int test_stage_gate_boss_id_minus_one_sentinel(void)
{
    /* -1 sentinel (empty enemy slot) must return 0. */
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(-1), 0);
    /* Zero is not in any range either. */
    T_ASSERT_EQ_I(stage_gate_boss_id_allowed(0), 0);
    return 0;
}

/* ─── FUN_0043195d — checkpoint predicate ───────────────────────────── */

int test_stage_gate_checkpoint_non_5_dungeon_uses_mod5_eq_4(void)
{
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 0;

    /* floor % 5 == 4 → true: 4, 9, 14, ... */
    stage_gate_set_next(4);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);
    stage_gate_set_next(9);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);
    stage_gate_set_next(0x1d);  /* 29; 29 % 5 == 4 → true */
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);

    /* Off-checkpoint floors → false. */
    stage_gate_set_next(0);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    stage_gate_set_next(3);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    stage_gate_set_next(5);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);

    /* The mod-5 rule must apply for every non-5 dungeon. */
    for (int d = 1; d <= 4; ++d) {
        g_scene1_combat_stage_id = d;
        stage_gate_set_next(9);
        T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);
        stage_gate_set_next(10);
        T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    }
    return 0;
}

int test_stage_gate_checkpoint_dungeon_5_uses_geq_29(void)
{
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 5;

    /* 0x1d == 29 → true (explicit branch). */
    stage_gate_set_next(0x1d);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);
    /* Anything > 0x1d → true. */
    stage_gate_set_next(0x1e);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);
    stage_gate_set_next(99);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 1);
    /* Below 0x1d → false (including the mod-5==4 floors like 4/9/14). */
    stage_gate_set_next(0x1c);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    stage_gate_set_next(9);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    stage_gate_set_next(4);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    stage_gate_set_next(0);
    T_ASSERT_EQ_I(stage_gate_floor_is_checkpoint(), 0);
    return 0;
}

/* ─── FUN_004319d6 — full outer gate ────────────────────────────────── */

int test_stage_gate_query_hardcoded_0_to_4(void)
{
    /* (0 → 4) is one of the three hard-coded "always allow" pairs.
     * Should return 1 even with no enemy data populated. */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 0;
    stage_gate_set_next(4);
    T_ASSERT_EQ_I(stage_gate_query(), 1);
    return 0;
}

int test_stage_gate_query_hardcoded_4_to_29_and_99(void)
{
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 4;

    stage_gate_set_next(0x1d);
    T_ASSERT_EQ_I(stage_gate_query(), 1);

    stage_gate_set_next(99);
    T_ASSERT_EQ_I(stage_gate_query(), 1);

    /* Any other (4 → N) target falls through to the regular walker —
     * with all sections terminated at floor_lo=-1, the walker returns 0. */
    stage_gate_set_next(9);     /* mod-5==4 so checkpoint passes... */
    T_ASSERT_EQ_I(stage_gate_query(), 0);   /* ...but no sections set */
    return 0;
}

int test_stage_gate_query_checkpoint_fail_returns_zero(void)
{
    /* Non-special, non-checkpoint floor → returns 0 without walking. */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 1;
    stage_gate_set_next(3);   /* 3 % 5 == 3, not a checkpoint */
    T_ASSERT_EQ_I(stage_gate_query(), 0);
    return 0;
}

int test_stage_gate_query_walker_finds_boss_in_range(void)
{
    /* Set dungeon 2, section 0 to cover floors 5..9, with enemies[0]
     * being a boss-id (0x17).  Target floor 9 (checkpoint by mod-5). */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 2;
    stage_gate_set_next(9);

    g_enemylist.sections[2][0].floor_lo = 5;
    g_enemylist.sections[2][0].floor_hi = 9;
    g_enemylist.sections[2][0].enemies[0].enemy_id = 0x17;  /* boss-allowed */

    T_ASSERT_EQ_I(stage_gate_query(), 1);
    return 0;
}

int test_stage_gate_query_walker_finds_non_boss_returns_zero(void)
{
    /* Same setup but enemy_id outside the boss table → 0. */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 2;
    stage_gate_set_next(9);

    g_enemylist.sections[2][0].floor_lo = 5;
    g_enemylist.sections[2][0].floor_hi = 9;
    g_enemylist.sections[2][0].enemies[0].enemy_id = 0x10;  /* non-boss */
    /* enemies[1].enemy_id stays -1 → inner walker terminates */

    T_ASSERT_EQ_I(stage_gate_query(), 0);
    return 0;
}

int test_stage_gate_query_walker_skips_to_later_section(void)
{
    /* Section[0] covers floors 0..4 (no match for target 9);
     * Section[1] covers 5..9 (match). */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 2;
    stage_gate_set_next(9);

    g_enemylist.sections[2][0].floor_lo = 0;
    g_enemylist.sections[2][0].floor_hi = 4;
    g_enemylist.sections[2][0].enemies[0].enemy_id = 0x17;

    g_enemylist.sections[2][1].floor_lo = 5;
    g_enemylist.sections[2][1].floor_hi = 9;
    g_enemylist.sections[2][1].enemies[0].enemy_id = 0x31;

    T_ASSERT_EQ_I(stage_gate_query(), 1);
    return 0;
}

int test_stage_gate_query_walker_terminates_at_minus_one(void)
{
    /* No section covers floor 9 → outer walker hits floor_lo == -1
     * and returns 0. */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 2;
    stage_gate_set_next(9);

    g_enemylist.sections[2][0].floor_lo = 0;
    g_enemylist.sections[2][0].floor_hi = 4;
    g_enemylist.sections[2][0].enemies[0].enemy_id = 0x17;
    /* sections[2][1].floor_lo stays -1 → walker terminates */

    T_ASSERT_EQ_I(stage_gate_query(), 0);
    return 0;
}

int test_stage_gate_query_inner_walker_scans_past_non_bosses(void)
{
    /* The inner walker scans through non-boss enemies and returns 1
     * on the first boss-id hit, before the -1 sentinel. */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 3;
    stage_gate_set_next(14);   /* 14 % 5 == 4 → checkpoint */

    g_enemylist.sections[3][0].floor_lo = 10;
    g_enemylist.sections[3][0].floor_hi = 14;
    g_enemylist.sections[3][0].enemies[0].enemy_id = 0x05;  /* non-boss */
    g_enemylist.sections[3][0].enemies[1].enemy_id = 0x0a;  /* non-boss */
    g_enemylist.sections[3][0].enemies[2].enemy_id = 0x42;  /* boss-allowed */

    T_ASSERT_EQ_I(stage_gate_query(), 1);
    return 0;
}

int test_stage_gate_query_dungeon_5_uses_geq_29_rule(void)
{
    /* Dungeon 5 dispatches the >=29 checkpoint variant.  At floor 30
     * with a boss-id enemy in range, gate returns 1. */
    stage_gate_test_reset();
    g_scene1_combat_stage_id = 5;
    stage_gate_set_next(30);

    g_enemylist.sections[5][0].floor_lo = 25;
    g_enemylist.sections[5][0].floor_hi = 35;
    g_enemylist.sections[5][0].enemies[0].enemy_id = 0x29;

    T_ASSERT_EQ_I(stage_gate_query(), 1);

    /* Below 29 in dungeon 5: checkpoint fails → 0 even with bosses set. */
    stage_gate_set_next(20);
    T_ASSERT_EQ_I(stage_gate_query(), 0);
    return 0;
}

int test_stage_gate_query_out_of_range_dungeon_returns_zero(void)
{
    /* Negative + over-bounds dungeon id should both safely return 0
     * (mirrors the engine's "no boss" effective behaviour without
     * actually walking off the table). */
    stage_gate_test_reset();
    stage_gate_set_next(9);   /* mod-5==4 checkpoint */

    g_scene1_combat_stage_id = -1;
    T_ASSERT_EQ_I(stage_gate_query(), 0);

    g_scene1_combat_stage_id = ENEMYLIST_DUNGEON_SLOTS;
    T_ASSERT_EQ_I(stage_gate_query(), 0);
    return 0;
}

int test_stage_gate_next_setter_roundtrip(void)
{
    stage_gate_set_next(0);
    T_ASSERT_EQ_I(stage_gate_get_next(), 0);
    stage_gate_set_next(0x1d);
    T_ASSERT_EQ_I(stage_gate_get_next(), 0x1d);
    stage_gate_set_next(-1);
    T_ASSERT_EQ_I(stage_gate_get_next(), -1);
    stage_gate_set_next(0);  /* leave clean */
    return 0;
}
