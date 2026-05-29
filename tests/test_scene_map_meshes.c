/*
 * test_scene_map_meshes.c — unit tests for src/scene_map_meshes.c
 * (engine FUN_00474681, the per-stage "map" .x mesh loader).
 *
 * Pure-C coverage (the Win32 mesh_load + D3D path is exercised live in
 * the HOUSE A/B capture, not here):
 *   1. count reflects the stage record map_count, capped at the slot cap.
 *   2. load_with iterates map[] in slot order with the parsed paths.
 *   3. The HOUSE map list (stage 0) loads room + carpet at indices 0/1.
 *   4. Invalid stage → 0; empty map slots are skipped.
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "scene_map_meshes.h"
#include "tables_stage.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int  count;
    int  slots[SCENE_MAP_MESH_SLOTS];
    char paths[SCENE_MAP_MESH_SLOTS][256];
} captured;

static int capture_load_fn(const char *path, int slot, void *userdata)
{
    captured *c = (captured *)userdata;
    if (c->count >= SCENE_MAP_MESH_SLOTS) return 0;
    c->slots[c->count] = slot;
    snprintf(c->paths[c->count], sizeof c->paths[0], "%s", path);
    c->count++;
    return 1;
}

/* Seed g_stage.records[0] with the real HOUSE (stage:0-1) map list. */
static void seed_house_stage(void)
{
    memset(&g_stage, 0, sizeof g_stage);
    g_stage.count = 1;
    static const char *const house_maps[] = {
        "xfile/shop/shop_1st.x",        "xfile/jutan/shop_jutan.x",
        "xfile/jutan/shop_jutan_tora.x", "xfile/table/shop_table01.x",
        "xfile/table/shop_table02.x",   "xfile/jihanki/shop_jihanki01.x",
        "xfile/jihanki/shop_jihanki02.x", "xfile/jihanki/shop_02jihanki01.x",
        "xfile/jihanki/shop_02jihanki02.x", "xfile/jihanki/shop_03jihanki01.x",
        "xfile/jihanki/shop_03jihanki02.x",
    };
    int n = (int)(sizeof house_maps / sizeof house_maps[0]);
    for (int i = 0; i < n; i++)
        snprintf(g_stage.records[0].map[i], STAGE_NAME_MAX, "%s",
                 house_maps[i]);
    g_stage.records[0].map_count = n;
}

int test_scene_map_meshes_house_count(void)
{
    seed_house_stage();
    T_ASSERT_EQ_I(scene_map_meshes_count(SCENE_MAP_STAGE_HOUSE), 11);
    return 0;
}

int test_scene_map_meshes_house_order(void)
{
    seed_house_stage();
    captured c = {0};
    int n = scene_map_meshes_load_with(SCENE_MAP_STAGE_HOUSE,
                                       capture_load_fn, &c);
    T_ASSERT_EQ_I(n, 11);
    T_ASSERT_EQ_I(c.count, 11);
    /* Slot order + the two draw-loop-A meshes (idx 0 room, idx 1 carpet). */
    T_ASSERT_EQ_I(c.slots[0], 0);
    T_ASSERT_EQ_I(c.slots[1], 1);
    T_ASSERT(strcmp(c.paths[0], "xfile/shop/shop_1st.x") == 0);
    T_ASSERT(strcmp(c.paths[1], "xfile/jutan/shop_jutan.x") == 0);
    T_ASSERT(strcmp(c.paths[10], "xfile/jihanki/shop_03jihanki02.x") == 0);
    return 0;
}

int test_scene_map_meshes_count_cap(void)
{
    /* map_count past the slot cap → capped (engine bumps the counter
     * past the array on overflow; our loop honors the cap). */
    memset(&g_stage, 0, sizeof g_stage);
    g_stage.count = 1;
    g_stage.records[0].map_count = SCENE_MAP_MESH_SLOTS + 50;
    T_ASSERT_EQ_I(scene_map_meshes_count(0), SCENE_MAP_MESH_SLOTS);
    return 0;
}

int test_scene_map_meshes_invalid_stage(void)
{
    seed_house_stage();   /* count == 1 */
    T_ASSERT_EQ_I(scene_map_meshes_count(-1), 0);
    T_ASSERT_EQ_I(scene_map_meshes_count(1), 0);
    captured c = {0};
    T_ASSERT_EQ_I(scene_map_meshes_load_with(5, capture_load_fn, &c), 0);
    T_ASSERT_EQ_I(c.count, 0);
    return 0;
}

int test_scene_map_meshes_empty_slot_skipped(void)
{
    memset(&g_stage, 0, sizeof g_stage);
    g_stage.count = 1;
    snprintf(g_stage.records[0].map[0], STAGE_NAME_MAX, "%s", "a.x");
    /* slot 1 left empty */
    snprintf(g_stage.records[0].map[2], STAGE_NAME_MAX, "%s", "c.x");
    g_stage.records[0].map_count = 3;

    captured c = {0};
    int n = scene_map_meshes_load_with(0, capture_load_fn, &c);
    /* count() returns 3, but the empty slot 1 is skipped → 2 calls. */
    T_ASSERT_EQ_I(scene_map_meshes_count(0), 3);
    T_ASSERT_EQ_I(n, 2);
    T_ASSERT_EQ_I(c.slots[0], 0);
    T_ASSERT_EQ_I(c.slots[1], 2);
    return 0;
}
