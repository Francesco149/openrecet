/*
 * test_scene_walls.c — pure-C tests for the B3E secondary inner-body
 * (engine FUN_0047474e — wall asset loader). Drives the dispatch
 * table without a D3D device by injecting a recording load_fn.
 */
#include "t.h"

#include <string.h>

#include "scene_walls.h"
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

int test_scene_walls_count_is_fifteen(void)
{
    /* Engine table length: (0x5c7fb4 - 0x5c7f78) / 4 = 0x3c / 4. */
    T_ASSERT_EQ_I(SCENE_WALLS_COUNT, 15);
    return 0;
}

int test_scene_walls_filename_table_first_and_last(void)
{
    /* Anchor against the two end strings extracted via pe.py from
     * 0x5ca11c and 0x5ca200. If the table ever shifts by even one
     * entry these will flag it. */
    T_ASSERT(strcmp(scene_walls_filename(0),  "kabe_sikkui.bmp") == 0);
    T_ASSERT(strcmp(scene_walls_filename(14), "kabe_check.bmp")  == 0);
    return 0;
}

int test_scene_walls_filename_table_full_order(void)
{
    static const char *const expected[SCENE_WALLS_COUNT] = {
        "kabe_sikkui.bmp", "kabe_ita.bmp",     "kabe_hosi.bmp",
        "kabe_umi.bmp",    "kabe_moru.bmp",    "kabe_renga.bmp",
        "kabe_giseki.bmp", "kabe_8bit.bmp",    "kabe_jya.bmp",
        "kabe_iseki.bmp",  "kabe_euria.bmp",   "kabe_namako.bmp",
        "kabe_chuka.bmp",  "kabe_kouhaku.bmp", "kabe_check.bmp",
    };
    for (int i = 0; i < SCENE_WALLS_COUNT; i++) {
        const char *got = scene_walls_filename(i);
        if (strcmp(got, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s", i, got, expected[i]);
        }
    }
    return 0;
}

int test_scene_walls_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_walls_filename(-1) == 0);
    T_ASSERT(scene_walls_filename(SCENE_WALLS_COUNT) == 0);
    T_ASSERT(scene_walls_filename(9999) == 0);
    return 0;
}

int test_scene_walls_format_string(void)
{
    /* Engine s_xfile_wall__s_005ca210. */
    T_ASSERT(strcmp(scene_walls_format_string(), "xfile/wall/%s") == 0);
    return 0;
}

int test_scene_walls_load_param0_loads_only_selector(void)
{
    scene_walls_reset();
    reset_recorded();

    g_scene_walls_selector = 3;  /* kabe_umi.bmp */
    int loads = scene_walls_load_with(recording_load_fn,
                                       (void *)0xdeadbeef, 0);

    T_ASSERT_EQ_I(loads, 1);
    T_ASSERT_EQ_I(g_rec.n, 1);
    T_ASSERT_EQ_I(g_rec.slot[0], 3);
    T_ASSERT(strcmp(g_rec.path[0], "xfile/wall/kabe_umi.bmp") == 0);
    /* userdata threads through verbatim. */
    T_ASSERT(g_rec.userdata[0] == (void *)0xdeadbeef);
    return 0;
}

int test_scene_walls_load_param1_loads_everything_except_selector(void)
{
    scene_walls_reset();
    reset_recorded();

    g_scene_walls_selector = 7;  /* kabe_8bit.bmp */
    int loads = scene_walls_load_with(recording_load_fn, 0, 1);

    T_ASSERT_EQ_I(loads, SCENE_WALLS_COUNT - 1);
    T_ASSERT_EQ_I(g_rec.n, SCENE_WALLS_COUNT - 1);

    /* Selector slot must NOT appear in the record. */
    for (int i = 0; i < g_rec.n; i++) {
        if (g_rec.slot[i] == 7) T_FAIL("selector slot 7 loaded under param=1");
    }
    return 0;
}

int test_scene_walls_load_default_selector_zero(void)
{
    scene_walls_reset();
    reset_recorded();

    /* scene_walls_reset() leaves selector at 0. */
    int loads = scene_walls_load_with(recording_load_fn, 0, 0);

    T_ASSERT_EQ_I(loads, 1);
    T_ASSERT_EQ_I(g_rec.slot[0], 0);
    T_ASSERT(strcmp(g_rec.path[0], "xfile/wall/kabe_sikkui.bmp") == 0);
    return 0;
}

int test_scene_walls_load_selector_out_of_range_param0_loads_nothing(void)
{
    scene_walls_reset();
    reset_recorded();

    /* An out-of-range selector matches no slot — under param=0 that's
     * zero loads (which is exactly what the engine does when the
     * stage state isn't initialised). */
    g_scene_walls_selector = -1;
    T_ASSERT_EQ_I(scene_walls_load_with(recording_load_fn, 0, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);

    g_scene_walls_selector = SCENE_WALLS_COUNT;
    T_ASSERT_EQ_I(scene_walls_load_with(recording_load_fn, 0, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);
    return 0;
}

int test_scene_walls_load_selector_out_of_range_param1_loads_all(void)
{
    scene_walls_reset();
    reset_recorded();

    g_scene_walls_selector = SCENE_WALLS_COUNT;  /* matches no slot */
    int loads = scene_walls_load_with(recording_load_fn, 0, 1);

    T_ASSERT_EQ_I(loads, SCENE_WALLS_COUNT);
    T_ASSERT_EQ_I(g_rec.n, SCENE_WALLS_COUNT);
    return 0;
}

int test_scene_walls_load_without_load_fn_returns_count_only(void)
{
    /* No load_fn injected — body should still return the dispatch
     * count without crashing (counting-only dry run). */
    scene_walls_reset();
    g_scene_walls_selector = 5;
    int loads = scene_walls_load_with(0, 0, 0);
    T_ASSERT_EQ_I(loads, 1);

    loads = scene_walls_load_with(0, 0, 1);
    T_ASSERT_EQ_I(loads, SCENE_WALLS_COUNT - 1);
    return 0;
}

int test_scene_walls_reset_zeroes_state(void)
{
    /* Stamp non-zero state into the selector + verify reset clears it. */
    g_scene_walls_selector  = 9;
    scene_walls_reset();
    T_ASSERT_EQ_I(g_scene_walls_selector, 0);
    return 0;
}
