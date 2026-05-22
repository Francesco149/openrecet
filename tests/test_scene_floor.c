/*
 * test_scene_floor.c — pure-C tests for the B82 secondary inner-body
 * (engine FUN_004747dc — floor asset loader). Mirrors test_scene_walls
 * since the two are structural siblings.
 */
#include "t.h"

#include <string.h>

#include "scene_floor.h"
#include "worker_load.h"

/* Recording scratchpad for the injected load_fn. */
#define MAX_RECORDED 32
static struct {
    int   n;
    char  path[MAX_RECORDED][64];
    int   slot[MAX_RECORDED];
    void *userdata[MAX_RECORDED];
} g_rec;

static void reset_recorded(void)
{
    g_rec.n = 0;
    memset(g_rec.path,     0, sizeof(g_rec.path));
    memset(g_rec.slot,     0, sizeof(g_rec.slot));
    memset(g_rec.userdata, 0, sizeof(g_rec.userdata));
}

static int recording_load_fn(const char *path, int slot, void *userdata)
{
    if (g_rec.n < MAX_RECORDED) {
        size_t n = strlen(path);
        if (n >= sizeof(g_rec.path[0])) n = sizeof(g_rec.path[0]) - 1;
        memcpy(g_rec.path[g_rec.n], path, n);
        g_rec.path[g_rec.n][n] = '\0';
        g_rec.slot[g_rec.n]     = slot;
        g_rec.userdata[g_rec.n] = userdata;
        g_rec.n++;
    }
    return 1;
}

int test_scene_floor_count_is_fifteen(void)
{
    /* Engine table length: (0x5c7ff4 - 0x5c7fb8) / 4 = 0x3c / 4. */
    T_ASSERT_EQ_I(SCENE_FLOOR_COUNT, 15);
    return 0;
}

int test_scene_floor_filename_table_first_and_last(void)
{
    /* Anchor against the two end strings extracted via pe.py from
     * 0x5ca220 and 0x5ca314. */
    T_ASSERT(strcmp(scene_floor_filename(0),  "yuka_ita2.bmp") == 0);
    T_ASSERT(strcmp(scene_floor_filename(14), "yuka_jya.bmp")  == 0);
    return 0;
}

int test_scene_floor_filename_table_full_order(void)
{
    static const char *const expected[SCENE_FLOOR_COUNT] = {
        "yuka_ita2.bmp",     "yuka_dairiseki.bmp", "yuka_ishi.bmp",
        "yuka_renga.bmp",    "yuka_isekiyuka.bmp", "yuka_tatami.bmp",
        "yuka_akaju.bmp",    "yuka_rapyuta.bmp",   "yuka_mori.bmp",
        "yuka_tuchi.bmp",    "yuka_kyousitu.bmp",  "yuka_chaishi.bmp",
        "yuka_euria.bmp",    "yuka_check.bmp",     "yuka_jya.bmp",
    };
    for (int i = 0; i < SCENE_FLOOR_COUNT; i++) {
        const char *got = scene_floor_filename(i);
        if (strcmp(got, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s", i, got, expected[i]);
        }
    }
    return 0;
}

int test_scene_floor_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_floor_filename(-1) == 0);
    T_ASSERT(scene_floor_filename(SCENE_FLOOR_COUNT) == 0);
    T_ASSERT(scene_floor_filename(9999) == 0);
    return 0;
}

int test_scene_floor_format_string(void)
{
    /* Engine s_xfile_floor__s_005ca324. */
    T_ASSERT(strcmp(scene_floor_format_string(), "xfile/floor/%s") == 0);
    return 0;
}

int test_scene_floor_load_param0_loads_only_selector(void)
{
    scene_floor_reset();
    reset_recorded();

    g_scene_floor_selector = 4;  /* yuka_isekiyuka.bmp */
    int loads = scene_floor_load_with(recording_load_fn,
                                       (void *)0xcafef00d, 0);

    T_ASSERT_EQ_I(loads, 1);
    T_ASSERT_EQ_I(g_rec.n, 1);
    T_ASSERT_EQ_I(g_rec.slot[0], 4);
    T_ASSERT(strcmp(g_rec.path[0], "xfile/floor/yuka_isekiyuka.bmp") == 0);
    /* userdata threads through verbatim. */
    T_ASSERT(g_rec.userdata[0] == (void *)0xcafef00d);
    return 0;
}

int test_scene_floor_load_param1_loads_everything_except_selector(void)
{
    scene_floor_reset();
    reset_recorded();

    g_scene_floor_selector = 8;  /* yuka_mori.bmp */
    int loads = scene_floor_load_with(recording_load_fn, 0, 1);

    T_ASSERT_EQ_I(loads, SCENE_FLOOR_COUNT - 1);
    T_ASSERT_EQ_I(g_rec.n, SCENE_FLOOR_COUNT - 1);

    /* Selector slot must NOT appear in the record. */
    for (int i = 0; i < g_rec.n; i++) {
        if (g_rec.slot[i] == 8) T_FAIL("selector slot 8 loaded under param=1");
    }
    return 0;
}

int test_scene_floor_load_default_selector_zero(void)
{
    scene_floor_reset();
    reset_recorded();

    /* scene_floor_reset() leaves selector at 0. */
    int loads = scene_floor_load_with(recording_load_fn, 0, 0);

    T_ASSERT_EQ_I(loads, 1);
    T_ASSERT_EQ_I(g_rec.slot[0], 0);
    T_ASSERT(strcmp(g_rec.path[0], "xfile/floor/yuka_ita2.bmp") == 0);
    return 0;
}

int test_scene_floor_load_selector_out_of_range_param0_loads_nothing(void)
{
    scene_floor_reset();
    reset_recorded();

    /* An out-of-range selector matches no slot — under param=0 that's
     * zero loads (which is exactly what the engine does when the
     * stage state isn't initialised). */
    g_scene_floor_selector = -1;
    T_ASSERT_EQ_I(scene_floor_load_with(recording_load_fn, 0, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);

    g_scene_floor_selector = SCENE_FLOOR_COUNT;
    T_ASSERT_EQ_I(scene_floor_load_with(recording_load_fn, 0, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);
    return 0;
}

int test_scene_floor_load_selector_out_of_range_param1_loads_all(void)
{
    scene_floor_reset();
    reset_recorded();

    g_scene_floor_selector = SCENE_FLOOR_COUNT;  /* matches no slot */
    int loads = scene_floor_load_with(recording_load_fn, 0, 1);

    T_ASSERT_EQ_I(loads, SCENE_FLOOR_COUNT);
    T_ASSERT_EQ_I(g_rec.n, SCENE_FLOOR_COUNT);
    return 0;
}

int test_scene_floor_load_without_load_fn_returns_count_only(void)
{
    /* No load_fn injected — body should still return the dispatch
     * count without crashing (counting-only dry run). */
    scene_floor_reset();
    g_scene_floor_selector = 11;
    int loads = scene_floor_load_with(0, 0, 0);
    T_ASSERT_EQ_I(loads, 1);

    loads = scene_floor_load_with(0, 0, 1);
    T_ASSERT_EQ_I(loads, SCENE_FLOOR_COUNT - 1);
    return 0;
}

int test_scene_floor_reset_zeroes_state(void)
{
    /* Stamp non-zero state into the selector + verify reset clears it. */
    g_scene_floor_selector  = 13;
    scene_floor_reset();
    T_ASSERT_EQ_I(g_scene_floor_selector, 0);
    return 0;
}

/* All 15 floor paths concatenate the format string with the filename
 * — make sure none truncate or duplicate the prefix. */
int test_scene_floor_load_all_paths_prefixed(void)
{
    scene_floor_reset();
    reset_recorded();

    g_scene_floor_selector = SCENE_FLOOR_COUNT;  /* selector misses → param=1 loads all */
    int loads = scene_floor_load_with(recording_load_fn, 0, 1);
    T_ASSERT_EQ_I(loads, SCENE_FLOOR_COUNT);

    for (int i = 0; i < SCENE_FLOOR_COUNT; i++) {
        const char *p = g_rec.path[i];
        /* Path begins with "xfile/floor/" (12 bytes). */
        T_ASSERT(strncmp(p, "xfile/floor/", 12) == 0);
        /* And ends with the corresponding filename. */
        T_ASSERT(strcmp(p + 12, scene_floor_filename(g_rec.slot[i])) == 0);
    }
    return 0;
}

/* Wall + floor selectors must be independent globals: writing one
 * must not perturb the other. (Engine's stage record has them at
 * separate offsets 0x57c and 0x580.) */
int test_scene_floor_selector_independent_from_walls(void)
{
    extern int32_t g_scene_walls_selector;
    scene_floor_reset();
    g_scene_walls_selector = 0;
    g_scene_floor_selector = 7;

    T_ASSERT_EQ_I(g_scene_walls_selector, 0);
    T_ASSERT_EQ_I(g_scene_floor_selector, 7);

    g_scene_walls_selector = 3;
    T_ASSERT_EQ_I(g_scene_floor_selector, 7);
    return 0;
}
