/*
 * test_scene_buy.c — pure-C tests for the AE8 + B13 secondary inner
 * bodies (engine FUN_0047329b + FUN_0047333b — buy-phase inventory
 * loaders). Drives the dispatch sequence without a D3D device by
 * injecting a recording load_fn.
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

/* ─── shared constants ───────────────────────────────────────────────── */

int test_scene_buy_slot_count_is_ten(void)
{
    T_ASSERT_EQ_I(SCENE_BUY_SLOT_COUNT, 10);
    return 0;
}

int test_scene_buy_page_count_is_fifty(void)
{
    /* Engine init loop end-address: (0x0730fdb4 - 0x06a63bd4) /
     * 0x2c670 = 50. */
    T_ASSERT_EQ_I(SCENE_BUY_PAGE_COUNT, 50);
    return 0;
}

int test_scene_buy_format_string(void)
{
    /* Engine s_bmp__s_005c864c (AE8) and s_bmp__s_005c8680 (B13). */
    T_ASSERT(strcmp(scene_buy_format_string(), "bmp/%s") == 0);
    return 0;
}

int test_scene_buy_singleton_slots_are_distinct(void)
{
    T_ASSERT_EQ_I(SCENE_BUY_AE8_SLOT_CHRNAME,  10);
    T_ASSERT_EQ_I(SCENE_BUY_AE8_SLOT_SHOPMODE, 11);
    return 0;
}

/* ─── AE8 tests ──────────────────────────────────────────────────────── */

int test_scene_buy_ae8_zero_count_loads_only_singletons(void)
{
    scene_buy_reset();
    reset_recorded();

    /* valid=1 but count=0 → dynamic loop bails on the count check. */
    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 0;

    int loads = scene_buy_ae8_load_with(recording_load_fn,
                                         (void *)0xdeadbeef);

    T_ASSERT_EQ_I(loads, 2);
    T_ASSERT_EQ_I(g_rec.n, 2);

    T_ASSERT_EQ_I(g_rec.slot[0], SCENE_BUY_AE8_SLOT_CHRNAME);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/ivent/chrname.tga") == 0);
    T_ASSERT_EQ_I(g_rec.w[0], 0x200);
    T_ASSERT_EQ_I(g_rec.h[0], 0x200);

    T_ASSERT_EQ_I(g_rec.slot[1], SCENE_BUY_AE8_SLOT_SHOPMODE);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/shopmode.tga") == 0);
    T_ASSERT_EQ_I(g_rec.w[1], 0x400);
    T_ASSERT_EQ_I(g_rec.h[1], 0x200);

    T_ASSERT(g_rec.userdata[0] == (void *)0xdeadbeef);
    T_ASSERT(g_rec.userdata[1] == (void *)0xdeadbeef);
    return 0;
}

int test_scene_buy_ae8_zero_valid_loads_only_singletons(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_valid[0] = 0;
    g_scene_buy_count[0] = 5;

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);
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

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, 2);
    T_ASSERT_EQ_I(g_rec.n, 2);
    return 0;
}

int test_scene_buy_ae8_dynamic_loop_three_items(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 3;
    snprintf(g_scene_buy_names[0][0], 256, "item_potion.tga");
    snprintf(g_scene_buy_names[0][1], 256, "item_sword.tga");
    snprintf(g_scene_buy_names[0][2], 256, "item_shield.tga");

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

    T_ASSERT_EQ_I(loads, 5);
    T_ASSERT_EQ_I(g_rec.n, 5);

    T_ASSERT_EQ_I(g_rec.slot[0], 0);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/item_potion.tga") == 0);
    T_ASSERT_EQ_I(g_rec.w[0], 0x200);
    T_ASSERT_EQ_I(g_rec.h[0], 0x200);
    T_ASSERT_EQ_I(g_rec.slot[1], 1);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/item_sword.tga") == 0);
    T_ASSERT_EQ_I(g_rec.slot[2], 2);
    T_ASSERT(strcmp(g_rec.path[2], "bmp/item_shield.tga") == 0);

    T_ASSERT_EQ_I(g_rec.slot[3], SCENE_BUY_AE8_SLOT_CHRNAME);
    T_ASSERT_EQ_I(g_rec.slot[4], SCENE_BUY_AE8_SLOT_SHOPMODE);
    return 0;
}

int test_scene_buy_ae8_dynamic_loop_full_ten_items(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = SCENE_BUY_SLOT_COUNT;
    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        snprintf(g_scene_buy_names[0][i], 256, "item_%d.bmp", i);
    }

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

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

    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 25;
    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        snprintf(g_scene_buy_names[0][i], 256, "x%d.bmp", i);
    }

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);

    /* Clamped to 10 dynamic + 2 singletons = 12 dispatches. */
    T_ASSERT_EQ_I(loads, 12);
    T_ASSERT_EQ_I(g_rec.n, 12);
    T_ASSERT_EQ_I(g_rec.slot[9], 9);
    T_ASSERT_EQ_I(g_rec.slot[10], SCENE_BUY_AE8_SLOT_CHRNAME);
    return 0;
}

int test_scene_buy_ae8_null_load_fn_returns_count_only(void)
{
    scene_buy_reset();

    int loads = scene_buy_ae8_load_with(0, 0);
    T_ASSERT_EQ_I(loads, 2);

    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 4;
    loads = scene_buy_ae8_load_with(0, 0);
    T_ASSERT_EQ_I(loads, 6);
    return 0;
}

int test_scene_buy_ae8_singleton_paths_match_rdata(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_count[0] = 0;
    scene_buy_ae8_load_with(recording_load_fn, 0);

    T_ASSERT_EQ_I(g_rec.n, 2);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/ivent/chrname.tga") == 0);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/shopmode.tga") == 0);
    return 0;
}

int test_scene_buy_ae8_ignores_current_page_selector(void)
{
    /* AE8 always reads page 0, never DAT_0730b56c. Setting
     * current_page=5 with non-zero state at page 5 must NOT cause
     * AE8 to dispatch from page 5 — it must still only see page 0
     * (which is BSS-zero here → no dynamic dispatch, just singletons). */
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_current_page = 5;
    g_scene_buy_valid[5] = 1;
    g_scene_buy_count[5] = 3;
    snprintf(g_scene_buy_names[5][0], 256, "shouldnt_appear.tga");

    int loads = scene_buy_ae8_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, 2);  /* only the two singletons */
    T_ASSERT_EQ_I(g_rec.n, 2);
    T_ASSERT_EQ_I(g_rec.slot[0], SCENE_BUY_AE8_SLOT_CHRNAME);
    return 0;
}

/* ─── B13 tests ──────────────────────────────────────────────────────── */

int test_scene_buy_b13_default_page_zero_loads_nothing(void)
{
    /* current_page=0 + BSS-zero state at page 0 → nothing fires. B13
     * has no singletons. */
    scene_buy_reset();
    reset_recorded();

    int loads = scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, 0);
    T_ASSERT_EQ_I(g_rec.n, 0);
    return 0;
}

int test_scene_buy_b13_page0_with_items(void)
{
    /* current_page=0 with non-zero state — B13 reads page 0 just
     * like AE8's dynamic phase, but no singletons. */
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_current_page = 0;
    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 2;
    snprintf(g_scene_buy_names[0][0], 256, "page0_a.tga");
    snprintf(g_scene_buy_names[0][1], 256, "page0_b.tga");

    int loads = scene_buy_b13_load_with(recording_load_fn,
                                         (void *)0xc0ffee);
    T_ASSERT_EQ_I(loads, 2);
    T_ASSERT_EQ_I(g_rec.n, 2);
    T_ASSERT_EQ_I(g_rec.slot[0], 0);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/page0_a.tga") == 0);
    T_ASSERT_EQ_I(g_rec.slot[1], 1);
    T_ASSERT(strcmp(g_rec.path[1], "bmp/page0_b.tga") == 0);
    T_ASSERT(g_rec.userdata[0] == (void *)0xc0ffee);
    return 0;
}

int test_scene_buy_b13_indexes_by_current_page(void)
{
    /* Set state at pages 0, 3, 7 — flip current_page and verify B13
     * reads from the right page each time. */
    scene_buy_reset();

    g_scene_buy_valid[0] = 1; g_scene_buy_count[0] = 1;
    snprintf(g_scene_buy_names[0][0], 256, "fromp0.tga");
    g_scene_buy_valid[3] = 1; g_scene_buy_count[3] = 1;
    snprintf(g_scene_buy_names[3][0], 256, "fromp3.tga");
    g_scene_buy_valid[7] = 1; g_scene_buy_count[7] = 1;
    snprintf(g_scene_buy_names[7][0], 256, "fromp7.tga");

    reset_recorded();
    g_scene_buy_current_page = 3;
    scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(g_rec.n, 1);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/fromp3.tga") == 0);

    reset_recorded();
    g_scene_buy_current_page = 7;
    scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(g_rec.n, 1);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/fromp7.tga") == 0);

    reset_recorded();
    g_scene_buy_current_page = 0;
    scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(g_rec.n, 1);
    T_ASSERT(strcmp(g_rec.path[0], "bmp/fromp0.tga") == 0);
    return 0;
}

int test_scene_buy_b13_zero_valid_at_current_page_is_noop(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_current_page = 12;
    g_scene_buy_valid[12] = 0;  /* gated off */
    g_scene_buy_count[12] = 5;

    int loads = scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, 0);
    T_ASSERT_EQ_I(g_rec.n, 0);
    return 0;
}

int test_scene_buy_b13_out_of_range_page_is_noop(void)
{
    /* Engine reads `&DAT_06a63bdc[page * 0xb19c]` with no bounds
     * check on `page`. Port clamps for memory safety. -1 is the
     * engine's "no page" sentinel; >= 50 is past the page-block end. */
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 9;
    for (int i = 0; i < 9; i++) {
        snprintf(g_scene_buy_names[0][i], 256, "p0_%d.tga", i);
    }

    g_scene_buy_current_page = -1;
    T_ASSERT_EQ_I(scene_buy_b13_load_with(recording_load_fn, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);

    reset_recorded();
    g_scene_buy_current_page = SCENE_BUY_PAGE_COUNT;
    T_ASSERT_EQ_I(scene_buy_b13_load_with(recording_load_fn, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);

    reset_recorded();
    g_scene_buy_current_page = 9999;
    T_ASSERT_EQ_I(scene_buy_b13_load_with(recording_load_fn, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);
    return 0;
}

int test_scene_buy_b13_count_overflow_is_clamped(void)
{
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_current_page = 4;
    g_scene_buy_valid[4] = 1;
    g_scene_buy_count[4] = 99;
    for (int i = 0; i < SCENE_BUY_SLOT_COUNT; i++) {
        snprintf(g_scene_buy_names[4][i], 256, "p4_%d.bmp", i);
    }

    int loads = scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, SCENE_BUY_SLOT_COUNT);
    T_ASSERT_EQ_I(g_rec.n, SCENE_BUY_SLOT_COUNT);
    /* Last clamped dispatch is slot 9. */
    T_ASSERT_EQ_I(g_rec.slot[9], 9);
    T_ASSERT(strcmp(g_rec.path[9], "bmp/p4_9.bmp") == 0);
    return 0;
}

int test_scene_buy_b13_null_load_fn_returns_count_only(void)
{
    scene_buy_reset();

    g_scene_buy_current_page = 2;
    g_scene_buy_valid[2] = 1;
    g_scene_buy_count[2] = 7;
    int loads = scene_buy_b13_load_with(0, 0);
    T_ASSERT_EQ_I(loads, 7);
    return 0;
}

int test_scene_buy_b13_does_not_load_singletons(void)
{
    /* AE8 always loads chrname + shopmode; B13 never does. Even with
     * fully-populated state, B13 returns only the dynamic count. */
    scene_buy_reset();
    reset_recorded();

    g_scene_buy_current_page = 0;
    g_scene_buy_valid[0] = 1;
    g_scene_buy_count[0] = 3;
    snprintf(g_scene_buy_names[0][0], 256, "x.tga");
    snprintf(g_scene_buy_names[0][1], 256, "y.tga");
    snprintf(g_scene_buy_names[0][2], 256, "z.tga");

    int loads = scene_buy_b13_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, 3);
    T_ASSERT_EQ_I(g_rec.n, 3);
    /* No SCENE_BUY_AE8_SLOT_CHRNAME / SHOPMODE dispatches. */
    for (int i = 0; i < g_rec.n; i++) {
        if (g_rec.slot[i] >= SCENE_BUY_SLOT_COUNT) {
            T_FAIL("B13 dispatched singleton slot %d", g_rec.slot[i]);
        }
    }
    return 0;
}

/* ─── reset ──────────────────────────────────────────────────────────── */

int test_scene_buy_reset_zeroes_state(void)
{
    /* Stamp non-zero state into multiple pages + the current-page
     * selector, then verify reset clears them all. */
    g_scene_buy_current_page = 17;
    g_scene_buy_valid[0]  = 7; g_scene_buy_count[0]  = 5;
    g_scene_buy_valid[17] = 1; g_scene_buy_count[17] = 2;
    g_scene_buy_valid[49] = 9; g_scene_buy_count[49] = 4;
    snprintf(g_scene_buy_names[0][0], 256, "stale_p0.tga");
    snprintf(g_scene_buy_names[17][1], 256, "stale_p17.tga");
    snprintf(g_scene_buy_names[49][9], 256, "stale_p49.tga");

    scene_buy_reset();

    T_ASSERT_EQ_I(g_scene_buy_current_page, 0);
    T_ASSERT_EQ_I(g_scene_buy_valid[0],  0);
    T_ASSERT_EQ_I(g_scene_buy_count[0],  0);
    T_ASSERT_EQ_I(g_scene_buy_valid[17], 0);
    T_ASSERT_EQ_I(g_scene_buy_count[17], 0);
    T_ASSERT_EQ_I(g_scene_buy_valid[49], 0);
    T_ASSERT_EQ_I(g_scene_buy_count[49], 0);
    T_ASSERT_EQ_I(g_scene_buy_names[0][0][0],   0);
    T_ASSERT_EQ_I(g_scene_buy_names[17][1][0],  0);
    T_ASSERT_EQ_I(g_scene_buy_names[49][9][0],  0);
    return 0;
}

/* ─── per-stage grp: parser (FUN_00475270 block #4) ──────────────────── */

int test_scene_buy_parse_stage_grp_lines(void)
{
    scene_buy_reset();
    /* A customer `file:` data buffer: two grp standee lines (2-digit NN,
     * ':' at +5, path at +6), a comment, plus se/msg arms that are ignored. */
    const char *buf =
        "grp00:ivent/01recette_04.tga\r\n"
        "grp01:ivent/02tear_01.tga\r\n"
        "/comment line\r\n"
        "se00:foo.wav\r\n"
        "msg00:hello\r\n";
    scene_buy_parse_stage_buffer(5, buf, strlen(buf));

    T_ASSERT_EQ_I(g_scene_buy_count[5], 2);   /* two grp lines */
    if (strcmp(g_scene_buy_names[5][0], "ivent/01recette_04.tga") != 0)
        T_FAIL("slot 0: got '%s'", g_scene_buy_names[5][0]);
    if (strcmp(g_scene_buy_names[5][1], "ivent/02tear_01.tga") != 0)
        T_FAIL("slot 1: got '%s'", g_scene_buy_names[5][1]);
    return 0;
}

int test_scene_buy_parse_stage_clamps_and_counts(void)
{
    scene_buy_reset();
    /* count tracks EVERY grp line (engine +0x5144), but names are stored
     * only for slots < SCENE_BUY_SLOT_COUNT; a grp NN past the cap bumps
     * count without an OOB write.  Also exercise a bare-\n terminator. */
    const char *buf =
        "grp00:a.tga\r\n"
        "grp09:b.tga\r\n"
        "grp15:c.tga\n";
    scene_buy_parse_stage_buffer(3, buf, strlen(buf));

    T_ASSERT_EQ_I(g_scene_buy_count[3], 3);
    if (strcmp(g_scene_buy_names[3][0], "a.tga") != 0)
        T_FAIL("slot 0: got '%s'", g_scene_buy_names[3][0]);
    if (strcmp(g_scene_buy_names[3][9], "b.tga") != 0)
        T_FAIL("slot 9: got '%s'", g_scene_buy_names[3][9]);

    /* out-of-range page / null buffer are no-ops (memory safety). */
    scene_buy_parse_stage_buffer(-1, buf, strlen(buf));
    scene_buy_parse_stage_buffer(SCENE_BUY_PAGE_COUNT, buf, strlen(buf));
    scene_buy_parse_stage_buffer(0, 0, 0);
    return 0;
}
