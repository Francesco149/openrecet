/*
 * test_scene_buy.c — pure-C tests for the AE8 secondary inner-body
 * (engine FUN_0047329b — buy-phase inventory loader). Drives the
 * dispatch sequence without a D3D device by injecting a recording
 * load_fn.
 */
#include "t.h"

#include <stdio.h>
#include <string.h>

#include "scene_buy.h"
#include "worker_load.h"

/* Recording scratchpad for the injected load_fn. */
#define MAX_RECORDED 32
static struct {
    int   n;
    char  path[MAX_RECORDED][64];
    int   slot[MAX_RECORDED];
    int   w[MAX_RECORDED];
    int   h[MAX_RECORDED];
    void *userdata[MAX_RECORDED];
} g_rec;

static void reset_recorded(void)
{
    g_rec.n = 0;
    memset(g_rec.path,     0, sizeof(g_rec.path));
    memset(g_rec.slot,     0, sizeof(g_rec.slot));
    memset(g_rec.w,        0, sizeof(g_rec.w));
    memset(g_rec.h,        0, sizeof(g_rec.h));
    memset(g_rec.userdata, 0, sizeof(g_rec.userdata));
}

static int recording_load_fn(const char *path, int slot, int w, int h,
                              void *userdata)
{
    if (g_rec.n < MAX_RECORDED) {
        size_t n = strlen(path);
        if (n >= sizeof(g_rec.path[0])) n = sizeof(g_rec.path[0]) - 1;
        memcpy(g_rec.path[g_rec.n], path, n);
        g_rec.path[g_rec.n][n] = '\0';
        g_rec.slot[g_rec.n]     = slot;
        g_rec.w[g_rec.n]        = w;
        g_rec.h[g_rec.n]        = h;
        g_rec.userdata[g_rec.n] = userdata;
        g_rec.n++;
    }
    return 1;
}

int test_scene_buy_slot_count_is_ten(void)
{
    /* Engine sprite-array per-page stride: 0xa0 / sizeof(sprite_t)
     * (0x10) = 10 slots. */
    T_ASSERT_EQ_I(SCENE_BUY_SLOT_COUNT, 10);
    return 0;
}

int test_scene_buy_format_string(void)
{
    /* Engine s_bmp__s_005c864c. */
    T_ASSERT(strcmp(scene_buy_format_string(), "bmp/%s") == 0);
    return 0;
}

int test_scene_buy_singleton_slots_are_distinct(void)
{
    /* Tests rely on slot index alone to identify dispatches — singleton
     * slots must NOT collide with the dynamic-loop range [0, 10). */
    T_ASSERT_EQ_I(SCENE_BUY_AE8_SLOT_CHRNAME,  10);
    T_ASSERT_EQ_I(SCENE_BUY_AE8_SLOT_SHOPMODE, 11);
    return 0;
}

int test_scene_buy_ae8_zero_count_loads_only_singletons(void)
{
    scene_buy_reset();
    reset_recorded();

    /* valid=1 but count=0 → dynamic loop bails on the count check. */
    g_scene_buy_page0_valid = 1;
    g_scene_buy_page0_count = 0;

    int loads = scene_buy_ae8_load_with(recording_load_fn,
                                         (void *)0xdeadbeef);

    T_ASSERT_EQ_I(loads, 2);
    T_ASSERT_EQ_I(g_rec.n, 2);

    /* chrname.tga at slot CHRNAME, 0x200×0x200. */
    T_ASSERT_EQ_I(g_rec.slot[0], SCENE_BUY_AE8_SLOT_CHRNAME);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/ivent/chrname.tga") == 0);
    T_ASSERT_EQ_I(g_rec.w[0], 0x200);
    T_ASSERT_EQ_I(g_rec.h[0], 0x200);

    /* shopmode.tga at slot SHOPMODE, 0x400×0x200. */
    T_ASSERT_EQ_I(g_rec.slot[1], SCENE_BUY_AE8_SLOT_SHOPMODE);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/shopmode.tga") == 0);
    T_ASSERT_EQ_I(g_rec.w[1], 0x400);
    T_ASSERT_EQ_I(g_rec.h[1], 0x200);

    /* userdata threads through both dispatches. */
    T_ASSERT(g_rec.userdata[0] == (void *)0xdeadbeef);
    T_ASSERT(g_rec.userdata[1] == (void *)0xdeadbeef);
    return 0;
}

int test_scene_buy_ae8_zero_valid_loads_only_singletons(void)
{
    scene_buy_reset();
    reset_recorded();

    /* count=5 but valid=0 → dynamic loop bails on the valid check. */
    g_scene_buy_page0_valid = 0;
    g_scene_buy_page0_count = 5;

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

    /* Only the two singletons fire. */
    T_ASSERT_EQ_I(loads, 2);
    T_ASSERT_EQ_I(g_rec.n, 2);
    T_ASSERT_EQ_I(g_rec.slot[0], SCENE_BUY_AE8_SLOT_CHRNAME);
    T_ASSERT_EQ_I(g_rec.slot[1], SCENE_BUY_AE8_SLOT_SHOPMODE);
    return 0;
}

int test_scene_buy_ae8_default_state_loads_only_singletons(void)
{
    scene_buy_reset();
    reset_recorded();

    /* Both globals BSS-zero after reset → dynamic loop bails. */
    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, 2);
    T_ASSERT_EQ_I(g_rec.n, 2);
    return 0;
}

int test_scene_buy_ae8_dynamic_loop_three_items(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_page0_valid = 1;
    g_scene_buy_page0_count = 3;
    snprintf(g_scene_buy_page0_names[0], 256, "item_potion.tga");
    snprintf(g_scene_buy_page0_names[1], 256, "item_sword.tga");
    snprintf(g_scene_buy_page0_names[2], 256, "item_shield.tga");

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

    /* 3 dynamic + 2 singletons. */
    T_ASSERT_EQ_I(loads, 5);
    T_ASSERT_EQ_I(g_rec.n, 5);

    /* Dynamic dispatches at slots 0..2 in order, dims 0x200×0x200. */
    T_ASSERT_EQ_I(g_rec.slot[0], 0);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/item_potion.tga") == 0);
    T_ASSERT_EQ_I(g_rec.w[0], 0x200);
    T_ASSERT_EQ_I(g_rec.h[0], 0x200);
    T_ASSERT_EQ_I(g_rec.slot[1], 1);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/item_sword.tga") == 0);
    T_ASSERT_EQ_I(g_rec.slot[2], 2);
    T_ASSERT(strcmp(g_rec.path[2], "bmp/item_shield.tga") == 0);

    /* Singletons follow at slots 10/11. */
    T_ASSERT_EQ_I(g_rec.slot[3], SCENE_BUY_AE8_SLOT_CHRNAME);
    T_ASSERT_EQ_I(g_rec.slot[4], SCENE_BUY_AE8_SLOT_SHOPMODE);
    return 0;
}

int test_scene_buy_ae8_dynamic_loop_full_ten_items(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_page0_valid = 1;
    g_scene_buy_page0_count = SCENE_BUY_SLOT_COUNT;
    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        snprintf(g_scene_buy_page0_names[i], 256, "item_%d.bmp", i);
    }

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

    /* 10 dynamic + 2 singletons. */
    T_ASSERT_EQ_I(loads, 12);
    T_ASSERT_EQ_I(g_rec.n, 12);

    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        T_ASSERT_EQ_I(g_rec.slot[i], i);
        char expected[64];
        snprintf(expected, sizeof(expected), "bmp/item_%d.bmp", i);
        if (strcmp(g_rec.path[i], expected) != 0) {
            T_FAIL("slot %d: got %s, want %s", i, g_rec.path[i], expected);
        }
    }
    T_ASSERT_EQ_I(g_rec.slot[10], SCENE_BUY_AE8_SLOT_CHRNAME);
    T_ASSERT_EQ_I(g_rec.slot[11], SCENE_BUY_AE8_SLOT_SHOPMODE);
    return 0;
}

int test_scene_buy_ae8_dynamic_loop_count_overflow_is_clamped(void)
{
    scene_buy_reset();
    reset_recorded();

    /* Engine has no bounds check — counts above 10 overflow into
     * adjacent pages' sprite slots. Port clamps at SCENE_BUY_SLOT_COUNT
     * for memory safety. */
    g_scene_buy_page0_valid = 1;
    g_scene_buy_page0_count = 25;
    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        snprintf(g_scene_buy_page0_names[i], 256, "x%d.bmp", i);
    }

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

    /* Clamped to 10 dynamic + 2 singletons = 12 dispatches. */
    T_ASSERT_EQ_I(loads, 12);
    T_ASSERT_EQ_I(g_rec.n, 12);

    /* The last dynamic dispatch is slot 9 (clamped). */
    T_ASSERT_EQ_I(g_rec.slot[9], 9);
    /* Singletons follow at 10/11 — not at 24/25. */
    T_ASSERT_EQ_I(g_rec.slot[10], SCENE_BUY_AE8_SLOT_CHRNAME);
    return 0;
}

int test_scene_buy_ae8_null_load_fn_returns_count_only(void)
{
    scene_buy_reset();

    /* Zero state → only the two singletons count. */
    int loads = scene_buy_ae8_load_with(0, 0);
    T_ASSERT_EQ_I(loads, 2);

    /* With 4 dynamic items, total is 4 + 2. */
    g_scene_buy_page0_valid = 1;
    g_scene_buy_page0_count = 4;
    loads = scene_buy_ae8_load_with(0, 0);
    T_ASSERT_EQ_I(loads, 6);
    return 0;
}

int test_scene_buy_reset_zeroes_state(void)
{
    /* Stamp non-zero state into all the page-0 globals + verify reset
     * clears them. */
    g_scene_buy_page0_valid = 7;
    g_scene_buy_page0_count = 5;
    snprintf(g_scene_buy_page0_names[0], 256, "stale.tga");
    snprintf(g_scene_buy_page0_names[9], 256, "alsostale.tga");

    scene_buy_reset();

    T_ASSERT_EQ_I(g_scene_buy_page0_valid, 0);
    T_ASSERT_EQ_I(g_scene_buy_page0_count, 0);
    T_ASSERT_EQ_I(g_scene_buy_page0_names[0][0], 0);
    T_ASSERT_EQ_I(g_scene_buy_page0_names[9][0], 0);
    return 0;
}

int test_scene_buy_singleton_paths_match_rdata(void)
{
    /* Sanity: the two singleton paths are the engine .rdata strings at
     * 0x005c8654 and 0x005c866c. These would change if a future port
     * accidentally renamed them. */
    scene_buy_reset();
    reset_recorded();

    /* Force the dynamic loop to bail so only the singletons fire. */
    g_scene_buy_page0_count = 0;
    scene_buy_ae8_load_with(recording_load_fn, 0);

    T_ASSERT_EQ_I(g_rec.n, 2);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/ivent/chrname.tga") == 0);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/shopmode.tga") == 0);
    return 0;
}
