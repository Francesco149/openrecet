/*
 * test_scene_table.c — unit tests for src/scene_table.c (engine
 * FUN_004748f8, the .x shop-table loader / C0A worker inner body).
 *
 * Coverage:
 *   1. Format string + table introspection (pre-baked array).
 *   2. param == 1 loads all NON-selector pairs (engine background).
 *   3. param == 0 loads ONLY the selector pair (engine foreground).
 *   4. Out-of-range selector (param == 0): no dispatches.
 *   5. Out-of-range selector (param == 1): all 8 pairs load.
 *   6. Pair indexing: load_fn slots arrive as pair*2 then pair*2+1.
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "scene_table.h"

#include <stdio.h>
#include <string.h>

/* Capture buffer for load_fn dispatches. */
typedef struct {
    int  count;
    int  slots[SCENE_TABLE_SLOT_COUNT];
    char paths[SCENE_TABLE_SLOT_COUNT][128];
} captured;

static int capture_load_fn(const char *path, int slot, void *userdata)
{
    captured *c = (captured *)userdata;
    if (c->count >= SCENE_TABLE_SLOT_COUNT) return 0;
    c->slots[c->count] = slot;
    snprintf(c->paths[c->count], sizeof c->paths[0], "%s", path);
    c->count++;
    return 1;
}

int test_scene_table_introspection(void)
{
    T_ASSERT(strcmp(scene_table_format_string(), "xfile/table/%s") == 0);
    T_ASSERT(strcmp(scene_table_filename(0),  "shop_table01.x") == 0);
    T_ASSERT(strcmp(scene_table_filename(15), "shop_jwel02.x")  == 0);
    T_ASSERT(scene_table_filename(-1) == 0);
    T_ASSERT(scene_table_filename(16) == 0);
    return 0;
}

int test_scene_table_param1_loads_non_selector(void)
{
    scene_table_reset();
    g_scene_table_selector = 3;   /* pair 3 = tarudesk */

    captured c = {0};
    int n = scene_table_load_with(capture_load_fn, &c, 1);

    /* 7 non-matching pairs × 2 = 14 loads. */
    T_ASSERT_EQ_I(n, 14);
    T_ASSERT_EQ_I(c.count, 14);

    /* Pair 3 (slots 6, 7) should NOT appear in c.slots. */
    for (int i = 0; i < c.count; i++) {
        T_ASSERT(c.slots[i] != 6 && c.slots[i] != 7);
    }
    return 0;
}

int test_scene_table_param0_loads_only_selector(void)
{
    scene_table_reset();
    g_scene_table_selector = 5;   /* pair 5 = kyoudan */

    captured c = {0};
    int n = scene_table_load_with(capture_load_fn, &c, 0);

    T_ASSERT_EQ_I(n, 2);
    T_ASSERT_EQ_I(c.count, 2);
    /* Slots 10, 11 — pair 5. */
    T_ASSERT_EQ_I(c.slots[0], 10);
    T_ASSERT_EQ_I(c.slots[1], 11);
    T_ASSERT(strcmp(c.paths[0], "xfile/table/shop_kyoudan01.x") == 0);
    T_ASSERT(strcmp(c.paths[1], "xfile/table/shop_kyoudan02.x") == 0);
    return 0;
}

int test_scene_table_oob_selector_param0(void)
{
    scene_table_reset();
    g_scene_table_selector = 99;
    captured c = {0};
    int n = scene_table_load_with(capture_load_fn, &c, 0);
    T_ASSERT_EQ_I(n, 0);
    return 0;
}

int test_scene_table_oob_selector_param1(void)
{
    scene_table_reset();
    g_scene_table_selector = 99;
    captured c = {0};
    int n = scene_table_load_with(capture_load_fn, &c, 1);
    /* All 8 pairs match the "not == 99" predicate. */
    T_ASSERT_EQ_I(n, 16);
    return 0;
}

int test_scene_table_pair_slot_order(void)
{
    scene_table_reset();
    g_scene_table_selector = 0;
    captured c = {0};
    (void)scene_table_load_with(capture_load_fn, &c, 0);
    T_ASSERT_EQ_I(c.count, 2);
    T_ASSERT_EQ_I(c.slots[0], 0);
    T_ASSERT_EQ_I(c.slots[1], 1);
    T_ASSERT(strcmp(c.paths[0], "xfile/table/shop_table01.x") == 0);
    T_ASSERT(strcmp(c.paths[1], "xfile/table/shop_table02.x") == 0);
    return 0;
}

int test_scene_table_null_loader_dry_run(void)
{
    scene_table_reset();
    g_scene_table_selector = 2;
    /* NULL load_fn → count-only. */
    int n = scene_table_load_with(0, 0, 0);
    T_ASSERT_EQ_I(n, 2);
    n = scene_table_load_with(0, 0, 1);
    T_ASSERT_EQ_I(n, 14);
    return 0;
}
