/*
 * test_stage_state.c — C7c stage_init_house().
 *
 * Engine BSS already starts the selectors at zero, so stage_init_house
 * is a sanity-check + future-hook function more than an active
 * transformation. The tests below pin the documented behaviour so the
 * function's contract can't drift silently:
 *
 *   1. Selectors land at the engine fresh-game defaults (all 0).
 *   2. The function is idempotent — calling it twice gives the same
 *      result.
 *   3. The function OVERWRITES whatever was in the globals before,
 *      not OR-merges. Important for the future case where a stage
 *      transition lands here mid-run with stale selectors from the
 *      previous stage.
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "stage_state.h"
#include "scene_floor.h"
#include "scene_jutan.h"
#include "scene_table.h"
#include "scene_walls.h"

int test_stage_state_house_defaults(void)
{
    /* Smudge the selectors first so we know stage_init_house actually
     * wrote something; if they happened to already be zero we'd see a
     * pass-by-accident otherwise. */
    g_scene_walls_selector = 9;
    g_scene_floor_selector = 5;
    g_scene_jutan_selector = 3;
    g_scene_table_selector = 7;

    stage_init_house();

    T_ASSERT_EQ_I(g_scene_walls_selector, STAGE_HOUSE_WALLS_SELECTOR_DEFAULT);
    T_ASSERT_EQ_I(g_scene_floor_selector, STAGE_HOUSE_FLOOR_SELECTOR_DEFAULT);
    T_ASSERT_EQ_I(g_scene_jutan_selector, STAGE_HOUSE_JUTAN_SELECTOR_DEFAULT);
    T_ASSERT_EQ_I(g_scene_table_selector, STAGE_HOUSE_TABLE_SELECTOR_DEFAULT);

    /* Engine values (documented contract — must stay at zero unless
     * the engine's slot-0 starter assets change). */
    T_ASSERT_EQ_I(g_scene_walls_selector, 0);
    T_ASSERT_EQ_I(g_scene_floor_selector, 0);
    T_ASSERT_EQ_I(g_scene_jutan_selector, 0);
    T_ASSERT_EQ_I(g_scene_table_selector, 0);
    return 0;
}

int test_stage_state_idempotent(void)
{
    stage_init_house();
    int32_t w1 = g_scene_walls_selector;
    int32_t f1 = g_scene_floor_selector;
    int32_t j1 = g_scene_jutan_selector;
    int32_t t1 = g_scene_table_selector;

    stage_init_house();
    T_ASSERT_EQ_I(g_scene_walls_selector, w1);
    T_ASSERT_EQ_I(g_scene_floor_selector, f1);
    T_ASSERT_EQ_I(g_scene_jutan_selector, j1);
    T_ASSERT_EQ_I(g_scene_table_selector, t1);
    return 0;
}

int test_stage_state_overwrites_stale(void)
{
    /* Simulate "leaving the dungeon back to the shop" — selectors had
     * been bumped by some other stage. stage_init_house must not OR
     * or AND, it must overwrite. */
    g_scene_walls_selector = 14;       /* last slot — kabe_check */
    g_scene_floor_selector = 14;       /* last floor slot */
    g_scene_jutan_selector = 7;        /* last jutan slot */
    g_scene_table_selector = 7;        /* last table-pair slot */

    stage_init_house();
    T_ASSERT_EQ_I(g_scene_walls_selector, 0);
    T_ASSERT_EQ_I(g_scene_floor_selector, 0);
    T_ASSERT_EQ_I(g_scene_jutan_selector, 0);
    T_ASSERT_EQ_I(g_scene_table_selector, 0);
    return 0;
}
