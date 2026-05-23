/*
 * test_scene_sc1.c — unit tests for src/scene_sc1.c (engine
 * FUN_0046bf38, the AAB worker inner body — scene-1
 * inventory/chrname/icon loader).
 *
 * Coverage:
 *   1. Dormant default: exactly 2 sprite_loads (the 2 fixed ones).
 *   2. Variable mesh loop fires when count > 0.
 *   3. Variable sprite loop fires when count > 0.
 *   4. Fixed 100-slot item loop skips empty names.
 *   5. Cap clamping for the variable loops (over-cap count stays in
 *      bounds).
 */
#define _DEFAULT_SOURCE 1
#include "t.h"
#include "scene_sc1.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int        sprite_count;
    int        mesh_count;
    int        kind[300];
    int        slot[300];
    uint32_t   w[300];
    uint32_t   h[300];
    char       paths_sprite[300][96];
    char       paths_mesh[100][96];
} cap_t;

static int cap_sprite(const char *path, int kind, int slot,
                      uint32_t w, uint32_t h, void *ud)
{
    cap_t *c = (cap_t *)ud;
    int i = c->sprite_count;
    if (i < 300) {
        c->kind[i] = kind; c->slot[i] = slot;
        c->w[i] = w; c->h[i] = h;
        snprintf(c->paths_sprite[i], sizeof c->paths_sprite[i], "%s", path);
    }
    c->sprite_count++;
    return 1;
}

static int cap_mesh(const char *path, int slot, void *ud)
{
    cap_t *c = (cap_t *)ud;
    int i = c->mesh_count;
    if (i < 100) {
        snprintf(c->paths_mesh[i], sizeof c->paths_mesh[i], "%s", path);
    }
    (void)slot;
    c->mesh_count++;
    return 1;
}

int test_scene_sc1_dormant_default(void)
{
    scene_sc1_reset();
    cap_t c = {0};
    int n = scene_sc1_load_with(cap_sprite, cap_mesh, &c);
    /* Dormant: only the 2 fixed sprites + 0 variables + 0 items. */
    T_ASSERT_EQ_I(n, 2);
    T_ASSERT_EQ_I(c.sprite_count, 2);
    T_ASSERT_EQ_I(c.mesh_count, 0);
    T_ASSERT_EQ_I(c.kind[0], SCENE_SC1_KIND_IVE_WINDOW);
    T_ASSERT_EQ_I(c.kind[1], SCENE_SC1_KIND_CHRNAME);
    T_ASSERT(strcmp(c.paths_sprite[0], "bmp/ivent/ive_window.tga") == 0);
    T_ASSERT(strcmp(c.paths_sprite[1], "bmp/ivent/chrname.tga") == 0);
    T_ASSERT_EQ_U(c.w[0], 0x200u);
    T_ASSERT_EQ_U(c.h[0], 0x200u);
    return 0;
}

int test_scene_sc1_var_mesh_loop(void)
{
    scene_sc1_reset();
    /* Populate 3 mesh slots. */
    g_scene_sc1_mesh_count = 3;
    snprintf(g_scene_sc1_mesh_names[0], SCENE_SC1_NAME_MAX, "xfile/etc/foo.x");
    snprintf(g_scene_sc1_mesh_names[1], SCENE_SC1_NAME_MAX, "xfile/etc/bar.x");
    snprintf(g_scene_sc1_mesh_names[2], SCENE_SC1_NAME_MAX, "xfile/etc/baz.x");

    cap_t c = {0};
    int n = scene_sc1_load_with(cap_sprite, cap_mesh, &c);
    T_ASSERT_EQ_I(n, 2 + 3);
    T_ASSERT_EQ_I(c.mesh_count, 3);
    T_ASSERT(strcmp(c.paths_mesh[0], "xfile/etc/foo.x") == 0);
    T_ASSERT(strcmp(c.paths_mesh[2], "xfile/etc/baz.x") == 0);
    return 0;
}

int test_scene_sc1_var_sprite_loop(void)
{
    scene_sc1_reset();
    g_scene_sc1_sprite_count = 2;
    snprintf(g_scene_sc1_sprite_names[0], SCENE_SC1_NAME_MAX, "bmp/item/a.tga");
    snprintf(g_scene_sc1_sprite_names[1], SCENE_SC1_NAME_MAX, "bmp/item/b.tga");

    cap_t c = {0};
    int n = scene_sc1_load_with(cap_sprite, cap_mesh, &c);
    T_ASSERT_EQ_I(n, 2 + 2);
    T_ASSERT_EQ_I(c.sprite_count, 4);
    /* Engine dims for the variable sprite loop: 0x400×0x200. */
    T_ASSERT_EQ_I(c.kind[2], SCENE_SC1_KIND_VAR_SPRITE);
    T_ASSERT_EQ_U(c.w[2], 0x400u);
    T_ASSERT_EQ_U(c.h[2], 0x200u);
    return 0;
}

int test_scene_sc1_item_loop_skips_empty(void)
{
    scene_sc1_reset();
    /* Populate 3 of the 100 slots (5, 17, 42) with non-empty names. */
    snprintf(g_scene_sc1_item_names[5],  SCENE_SC1_NAME_MAX, "bmp/icon/i05.tga");
    snprintf(g_scene_sc1_item_names[17], SCENE_SC1_NAME_MAX, "bmp/icon/i17.tga");
    snprintf(g_scene_sc1_item_names[42], SCENE_SC1_NAME_MAX, "bmp/icon/i42.tga");
    g_scene_sc1_item_sizes[5][0] = 64; g_scene_sc1_item_sizes[5][1] = 64;
    g_scene_sc1_item_sizes[17][0] = 32; g_scene_sc1_item_sizes[17][1] = 32;
    g_scene_sc1_item_sizes[42][0] = 128; g_scene_sc1_item_sizes[42][1] = 128;

    cap_t c = {0};
    int n = scene_sc1_load_with(cap_sprite, cap_mesh, &c);
    T_ASSERT_EQ_I(n, 2 + 3);
    /* 3 item dispatches at positions 2,3,4 (after the 2 fixed ones). */
    T_ASSERT_EQ_I(c.kind[2], SCENE_SC1_KIND_ITEM);
    T_ASSERT_EQ_I(c.slot[2], 5);
    T_ASSERT_EQ_U(c.w[2], 64u);
    T_ASSERT_EQ_I(c.slot[3], 17);
    T_ASSERT_EQ_I(c.slot[4], 42);
    return 0;
}

int test_scene_sc1_cap_clamps_count(void)
{
    scene_sc1_reset();
    g_scene_sc1_sprite_count = SCENE_SC1_VAR_SPRITE_CAP + 50;
    for (int i = 0; i < SCENE_SC1_VAR_SPRITE_CAP; i++) {
        snprintf(g_scene_sc1_sprite_names[i], SCENE_SC1_NAME_MAX, "s%d.tga", i);
    }
    cap_t c = {0};
    int n = scene_sc1_load_with(cap_sprite, cap_mesh, &c);
    T_ASSERT_EQ_I(n, 2 + SCENE_SC1_VAR_SPRITE_CAP);
    /* No OOB writes. */
    return 0;
}

int test_scene_sc1_null_callbacks_count_only(void)
{
    scene_sc1_reset();
    g_scene_sc1_mesh_count = 2;
    g_scene_sc1_sprite_count = 3;
    snprintf(g_scene_sc1_item_names[0], SCENE_SC1_NAME_MAX, "x.tga");
    snprintf(g_scene_sc1_mesh_names[0], SCENE_SC1_NAME_MAX, "a.x");
    snprintf(g_scene_sc1_mesh_names[1], SCENE_SC1_NAME_MAX, "b.x");
    snprintf(g_scene_sc1_sprite_names[0], SCENE_SC1_NAME_MAX, "s0.tga");
    snprintf(g_scene_sc1_sprite_names[1], SCENE_SC1_NAME_MAX, "s1.tga");
    snprintf(g_scene_sc1_sprite_names[2], SCENE_SC1_NAME_MAX, "s2.tga");
    int n = scene_sc1_load_with(0, 0, 0);
    T_ASSERT_EQ_I(n, 2 + 2 + 3 + 1);
    return 0;
}
