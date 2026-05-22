/*
 * test_scene_pause.c — pure-C tests for the C4E secondary inner-body
 * (engine unnamed FUN @ 0x435873 pause-state FPU init + FUN_00473a3e
 * pause+adventurer-status asset loader).
 *
 * Two surfaces:
 *   - scene_pause_state_init() writes 10 named globals with exact
 *     constants extracted from .rdata @ 0x519440 / 0x519474.
 *   - scene_pause_load_with() dispatches all 20 fixed asset slots,
 *     with slot 0 toggling between pause.tga and pause_endless.tga via
 *     the per-stage selector.
 */
#include "t.h"

#include <string.h>

#include "scene_pause.h"
#include "worker_load.h"

/* ─── recording scratchpad for the injected load_fn ──────────────────── */

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
    memset(&g_rec, 0, sizeof(g_rec));
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

/* ─── filename table tests ───────────────────────────────────────────── */

int test_scene_pause_load_count_is_twenty(void)
{
    /* 4 singletons + 8 sousa + 8 status = 20. */
    T_ASSERT_EQ_I(SCENE_PAUSE_LOAD_COUNT, 20);
    T_ASSERT_EQ_I(SCENE_PAUSE_SOUSA_COUNT, 8);
    T_ASSERT_EQ_I(SCENE_PAUSE_STATUS_COUNT, 8);
    return 0;
}

int test_scene_pause_slot0_default_is_pause_tga(void)
{
    /* Selector 0 (BSS-zero / fresh-boot) → engine takes the else branch:
     * "bmp/pause.tga". */
    scene_pause_reset();
    T_ASSERT(strcmp(scene_pause_filename(0), "bmp/pause.tga") == 0);
    return 0;
}

int test_scene_pause_slot0_selector_2_is_endless(void)
{
    scene_pause_reset();
    g_scene_pause_selector = 2;
    T_ASSERT(strcmp(scene_pause_filename(0), "bmp/pause_endless.tga") == 0);
    return 0;
}

int test_scene_pause_slot0_selector_3_is_endless(void)
{
    scene_pause_reset();
    g_scene_pause_selector = 3;
    T_ASSERT(strcmp(scene_pause_filename(0), "bmp/pause_endless.tga") == 0);
    return 0;
}

int test_scene_pause_slot0_selector_other_values_are_pause(void)
{
    /* Spot-check the cases adjacent to the endless range. */
    scene_pause_reset();
    int probes[] = {-1, 0, 1, 4, 5, 99};
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        g_scene_pause_selector = probes[i];
        const char *got = scene_pause_filename(0);
        if (strcmp(got, "bmp/pause.tga") != 0) {
            T_FAIL("selector=%d: got %s, want bmp/pause.tga",
                   probes[i], got);
        }
    }
    return 0;
}

int test_scene_pause_filename_table_full_order(void)
{
    static const char *const expected[SCENE_PAUSE_LOAD_COUNT] = {
        "bmp/pause.tga",          /* slot 0 (selector=0) */
        "bmp/pause_bg_rete.tga",
        "bmp/result_bord01.tga",
        "bmp/dungeonbord.tga",
        "bmp/sousa_lui.tga",  "bmp/sousa_sya.tga",
        "bmp/sousa_cai.tga",  "bmp/sousa_tel.tga",
        "bmp/sousa_era.tga",  "bmp/sousa_nag.tga",
        "bmp/sousa_grf.tga",  "bmp/sousa_arm.tga",
        "bmp/st_ryui.tga",    "bmp/st_sya.tga",
        "bmp/st_caillou.tga", "bmp/st_tiers.tga",
        "bmp/st_eran.tga",    "bmp/st_nagi.tga",
        "bmp/st_griffe.tga",  "bmp/st_aruma.tga",
    };
    scene_pause_reset();
    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        const char *got = scene_pause_filename(i);
        if (strcmp(got, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s", i, got, expected[i]);
        }
    }
    return 0;
}

int test_scene_pause_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_pause_filename(-1) == 0);
    T_ASSERT(scene_pause_filename(SCENE_PAUSE_LOAD_COUNT) == 0);
    T_ASSERT(scene_pause_filename(9999) == 0);
    return 0;
}

/* ─── dims tests ─────────────────────────────────────────────────────── */

int test_scene_pause_slot_dims_pause_and_singletons(void)
{
    /* Slot 0 (pause)              : 0x400 x 0x200
     * Slot 1 (pause_bg_rete)      : 0x400 x 0x200
     * Slot 2 (result_bord01)      : 0x200 x 0x100  ← only outlier in the
     *                                                 4 singletons.
     * Slot 3 (dungeonbord)        : 0x400 x 0x200
     */
    int w = -1, h = -1;
    T_ASSERT(scene_pause_slot_dims(0, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x400); T_ASSERT_EQ_I(h, 0x200);
    T_ASSERT(scene_pause_slot_dims(1, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x400); T_ASSERT_EQ_I(h, 0x200);
    T_ASSERT(scene_pause_slot_dims(2, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x200); T_ASSERT_EQ_I(h, 0x100);
    T_ASSERT(scene_pause_slot_dims(3, &w, &h) == 1);
    T_ASSERT_EQ_I(w, 0x400); T_ASSERT_EQ_I(h, 0x200);
    return 0;
}

int test_scene_pause_slot_dims_sousa_all_400x200(void)
{
    /* Slots 4..11 are the sousa cursor portraits, all 0x400 x 0x200. */
    for (int slot = 4; slot <= 11; slot++) {
        int w = -1, h = -1;
        T_ASSERT(scene_pause_slot_dims(slot, &w, &h) == 1);
        T_ASSERT_EQ_I(w, 0x400);
        T_ASSERT_EQ_I(h, 0x200);
    }
    return 0;
}

int test_scene_pause_slot_dims_status_all_200x200(void)
{
    /* Slots 12..19 are the st_* status portraits, all 0x200 x 0x200. */
    for (int slot = 12; slot <= 19; slot++) {
        int w = -1, h = -1;
        T_ASSERT(scene_pause_slot_dims(slot, &w, &h) == 1);
        T_ASSERT_EQ_I(w, 0x200);
        T_ASSERT_EQ_I(h, 0x200);
    }
    return 0;
}

int test_scene_pause_slot_dims_out_of_range_returns_zero(void)
{
    int w = 999, h = 999;
    T_ASSERT(scene_pause_slot_dims(-1, &w, &h) == 0);
    T_ASSERT(scene_pause_slot_dims(SCENE_PAUSE_LOAD_COUNT, &w, &h) == 0);
    /* Output pointers must not be touched on failure. */
    T_ASSERT_EQ_I(w, 999);
    T_ASSERT_EQ_I(h, 999);
    return 0;
}

/* ─── load_with dispatch tests ───────────────────────────────────────── */

int test_scene_pause_load_dispatches_all_twenty(void)
{
    scene_pause_reset();
    reset_recorded();

    int loads = scene_pause_load_with(recording_load_fn, (void *)0xc0ffee);

    T_ASSERT_EQ_I(loads, SCENE_PAUSE_LOAD_COUNT);
    T_ASSERT_EQ_I(g_rec.n, SCENE_PAUSE_LOAD_COUNT);

    /* Slots must be in 0..19 order — engine FUN_00473a3e is a straight
     * call sequence with no permutation. */
    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        T_ASSERT_EQ_I(g_rec.slot[i], i);
        T_ASSERT(g_rec.userdata[i] == (void *)0xc0ffee);
    }
    return 0;
}

int test_scene_pause_load_paths_match_filename_table(void)
{
    scene_pause_reset();
    reset_recorded();

    g_scene_pause_selector = 0;  /* slot 0 = "bmp/pause.tga" */
    int loads = scene_pause_load_with(recording_load_fn, 0);
    T_ASSERT_EQ_I(loads, SCENE_PAUSE_LOAD_COUNT);

    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        const char *want = scene_pause_filename(i);
        if (strcmp(g_rec.path[i], want) != 0) {
            T_FAIL("slot %d: dispatched %s, table %s",
                   i, g_rec.path[i], want);
        }
    }
    return 0;
}

int test_scene_pause_load_dims_match_metadata(void)
{
    scene_pause_reset();
    reset_recorded();

    scene_pause_load_with(recording_load_fn, 0);

    for (int i = 0; i < SCENE_PAUSE_LOAD_COUNT; i++) {
        int want_w = 0, want_h = 0;
        scene_pause_slot_dims(i, &want_w, &want_h);
        if (g_rec.w[i] != want_w || g_rec.h[i] != want_h) {
            T_FAIL("slot %d: dispatched %dx%d, table %dx%d",
                   i, g_rec.w[i], g_rec.h[i], want_w, want_h);
        }
    }
    return 0;
}

int test_scene_pause_load_selector_endless_swaps_slot_zero(void)
{
    scene_pause_reset();
    reset_recorded();

    g_scene_pause_selector = 3;  /* endless variant */
    scene_pause_load_with(recording_load_fn, 0);

    T_ASSERT(strcmp(g_rec.path[0], "bmp/pause_endless.tga") == 0);
    /* Slots 1..19 must not be affected. */
    T_ASSERT(strcmp(g_rec.path[1], "bmp/pause_bg_rete.tga") == 0);
    T_ASSERT(strcmp(g_rec.path[19], "bmp/st_aruma.tga") == 0);
    return 0;
}

int test_scene_pause_load_without_load_fn_returns_count_only(void)
{
    scene_pause_reset();
    int loads = scene_pause_load_with(0, 0);
    T_ASSERT_EQ_I(loads, SCENE_PAUSE_LOAD_COUNT);
    return 0;
}

/* ─── FPU init tests (engine unnamed @ 0x435873) ─────────────────────── */

int test_scene_pause_state_init_writes_constants(void)
{
    /* Constants extracted from .rdata:
     *   0x00519474 → 32.0f
     *   0x00519440 → 80.0f
     */
    scene_pause_reset();
    /* Pre-state: all zero (BSS / reset). */
    T_ASSERT_EQ_I(g_scene_pause_state_b150, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 0.0f);

    scene_pause_state_init();

    T_ASSERT_EQ_I(g_scene_pause_state_b150, 1);
    T_ASSERT_EQ_I(g_scene_pause_state_b158, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_b15c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac18, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac1c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac20, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 32.0f);
    T_ASSERT(g_scene_pause_state_abf4 == 32.0f);
    T_ASSERT(g_scene_pause_state_ac04 == 80.0f);
    T_ASSERT(g_scene_pause_state_abf8 == 80.0f);
    return 0;
}

int test_scene_pause_state_init_is_idempotent(void)
{
    /* Engine asm is straight-line writes — re-running must produce the
     * same end state without disturbing anything. */
    scene_pause_reset();
    scene_pause_state_init();
    scene_pause_state_init();
    T_ASSERT_EQ_I(g_scene_pause_state_b150, 1);
    T_ASSERT(g_scene_pause_state_ac00 == 32.0f);
    T_ASSERT(g_scene_pause_state_abf8 == 80.0f);
    return 0;
}

int test_scene_pause_state_init_overrides_dirty_state(void)
{
    /* Stamp arbitrary non-zero / non-32 values, then re-init — every
     * field must be reset to the engine constant. */
    g_scene_pause_state_b150 = 0xdead;
    g_scene_pause_state_b158 = 0xbeef;
    g_scene_pause_state_b15c = 0xcafe;
    g_scene_pause_state_ac18 = 0xfade;
    g_scene_pause_state_ac1c = 0xfeed;
    g_scene_pause_state_ac20 = 0xface;
    g_scene_pause_state_abf4 = -1.0f;
    g_scene_pause_state_abf8 = -2.0f;
    g_scene_pause_state_ac00 = -3.0f;
    g_scene_pause_state_ac04 = -4.0f;

    scene_pause_state_init();

    T_ASSERT_EQ_I(g_scene_pause_state_b150, 1);
    T_ASSERT_EQ_I(g_scene_pause_state_b158, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_b15c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac18, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac1c, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_ac20, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 32.0f);
    T_ASSERT(g_scene_pause_state_abf4 == 32.0f);
    T_ASSERT(g_scene_pause_state_ac04 == 80.0f);
    T_ASSERT(g_scene_pause_state_abf8 == 80.0f);
    return 0;
}

/* ─── reset + cross-sibling independence ─────────────────────────────── */

int test_scene_pause_reset_zeroes_state(void)
{
    g_scene_pause_selector = 2;
    scene_pause_state_init();
    scene_pause_reset();
    T_ASSERT_EQ_I(g_scene_pause_selector, 0);
    T_ASSERT_EQ_I(g_scene_pause_state_b150, 0);
    T_ASSERT(g_scene_pause_state_ac00 == 0.0f);
    return 0;
}

int test_scene_pause_selector_independent_from_siblings(void)
{
    /* Wall/floor/jutan/pause selectors are independent globals even
     * though the engine stores them at distinct offsets inside the same
     * 0x2dfc8-byte stage-state record. Make sure none of the 4 share
     * storage. */
    extern int32_t g_scene_walls_selector;
    extern int32_t g_scene_floor_selector;
    extern int32_t g_scene_jutan_selector;

    scene_pause_reset();
    g_scene_walls_selector = 1;
    g_scene_floor_selector = 2;
    g_scene_jutan_selector = 4;
    g_scene_pause_selector = 3;

    T_ASSERT_EQ_I(g_scene_walls_selector, 1);
    T_ASSERT_EQ_I(g_scene_floor_selector, 2);
    T_ASSERT_EQ_I(g_scene_jutan_selector, 4);
    T_ASSERT_EQ_I(g_scene_pause_selector, 3);

    g_scene_walls_selector = 0;
    g_scene_floor_selector = 0;
    g_scene_jutan_selector = 0;
    T_ASSERT_EQ_I(g_scene_pause_selector, 3);
    return 0;
}
