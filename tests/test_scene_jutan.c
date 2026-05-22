/*
 * test_scene_jutan.c — pure-C tests for the BC6 secondary inner-body
 * (engine FUN_0047486a — jutan/rug asset loader). 8-slot sibling of
 * scene_walls / scene_floor.
 */
#include "t.h"

#include <string.h>

#include "scene_jutan.h"
#include "worker_load.h"

/* Recording scratchpad for the injected load_fn. */
#define MAX_RECORDED 16
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

int test_scene_jutan_count_is_eight(void)
{
    /* Engine table length: (0x5c8014 - 0x5c7ff4) / 4 = 0x20 / 4. NB:
     * jutan is the only one of the four sibling loaders with an 8-slot
     * table; walls/floor/table are all 15-slot. */
    T_ASSERT_EQ_I(SCENE_JUTAN_COUNT, 8);
    return 0;
}

int test_scene_jutan_filename_table_first_and_last(void)
{
    T_ASSERT(strcmp(scene_jutan_filename(0), "shop_jutan01.tga") == 0);
    T_ASSERT(strcmp(scene_jutan_filename(7), "shop_jutan_jya.bmp") == 0);
    return 0;
}

int test_scene_jutan_filename_table_full_order(void)
{
    static const char *const expected[SCENE_JUTAN_COUNT] = {
        "shop_jutan01.tga",     "shop_jutan03.bmp",   "capet_tora.bmp",
        "shop_jutan_umi.bmp",   "shop_jutan_check.bmp",
        "shop_jutan_hade.bmp",  "shop_jutan_kawai.bmp",
        "shop_jutan_jya.bmp",
    };
    for (int i = 0; i < SCENE_JUTAN_COUNT; i++) {
        const char *got = scene_jutan_filename(i);
        if (strcmp(got, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s", i, got, expected[i]);
        }
    }
    return 0;
}

int test_scene_jutan_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_jutan_filename(-1) == 0);
    T_ASSERT(scene_jutan_filename(SCENE_JUTAN_COUNT) == 0);
    T_ASSERT(scene_jutan_filename(9999) == 0);
    return 0;
}

int test_scene_jutan_format_string(void)
{
    /* Engine s_xfile_jutan__s_005ca3d8. */
    T_ASSERT(strcmp(scene_jutan_format_string(), "xfile/jutan/%s") == 0);
    return 0;
}

int test_scene_jutan_load_param0_loads_only_selector(void)
{
    scene_jutan_reset();
    reset_recorded();

    g_scene_jutan_selector = 2;  /* capet_tora.bmp */
    int loads = scene_jutan_load_with(recording_load_fn,
                                       (void *)0xfeedface, 0);

    T_ASSERT_EQ_I(loads, 1);
    T_ASSERT_EQ_I(g_rec.n, 1);
    T_ASSERT_EQ_I(g_rec.slot[0], 2);
    T_ASSERT(strcmp(g_rec.path[0], "xfile/jutan/capet_tora.bmp") == 0);
    T_ASSERT(g_rec.userdata[0] == (void *)0xfeedface);
    return 0;
}

int test_scene_jutan_load_param1_loads_everything_except_selector(void)
{
    scene_jutan_reset();
    reset_recorded();

    g_scene_jutan_selector = 5;  /* shop_jutan_hade.bmp */
    int loads = scene_jutan_load_with(recording_load_fn, 0, 1);

    T_ASSERT_EQ_I(loads, SCENE_JUTAN_COUNT - 1);
    T_ASSERT_EQ_I(g_rec.n, SCENE_JUTAN_COUNT - 1);

    for (int i = 0; i < g_rec.n; i++) {
        if (g_rec.slot[i] == 5) T_FAIL("selector slot 5 loaded under param=1");
    }
    return 0;
}

int test_scene_jutan_load_default_selector_zero(void)
{
    scene_jutan_reset();
    reset_recorded();

    /* scene_jutan_reset() leaves selector at 0. */
    int loads = scene_jutan_load_with(recording_load_fn, 0, 0);

    T_ASSERT_EQ_I(loads, 1);
    T_ASSERT_EQ_I(g_rec.slot[0], 0);
    T_ASSERT(strcmp(g_rec.path[0], "xfile/jutan/shop_jutan01.tga") == 0);
    return 0;
}

int test_scene_jutan_load_selector_out_of_range_param0_loads_nothing(void)
{
    scene_jutan_reset();
    reset_recorded();

    g_scene_jutan_selector = -1;
    T_ASSERT_EQ_I(scene_jutan_load_with(recording_load_fn, 0, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);

    g_scene_jutan_selector = SCENE_JUTAN_COUNT;
    T_ASSERT_EQ_I(scene_jutan_load_with(recording_load_fn, 0, 0), 0);
    T_ASSERT_EQ_I(g_rec.n, 0);
    return 0;
}

int test_scene_jutan_load_selector_out_of_range_param1_loads_all(void)
{
    scene_jutan_reset();
    reset_recorded();

    g_scene_jutan_selector = SCENE_JUTAN_COUNT;
    int loads = scene_jutan_load_with(recording_load_fn, 0, 1);

    T_ASSERT_EQ_I(loads, SCENE_JUTAN_COUNT);
    T_ASSERT_EQ_I(g_rec.n, SCENE_JUTAN_COUNT);
    return 0;
}

int test_scene_jutan_load_without_load_fn_returns_count_only(void)
{
    scene_jutan_reset();
    g_scene_jutan_selector = 3;
    int loads = scene_jutan_load_with(0, 0, 0);
    T_ASSERT_EQ_I(loads, 1);

    loads = scene_jutan_load_with(0, 0, 1);
    T_ASSERT_EQ_I(loads, SCENE_JUTAN_COUNT - 1);
    return 0;
}

int test_scene_jutan_reset_zeroes_state(void)
{
    g_scene_jutan_selector = 6;
    scene_jutan_reset();
    T_ASSERT_EQ_I(g_scene_jutan_selector, 0);
    return 0;
}

int test_scene_jutan_load_all_paths_prefixed(void)
{
    scene_jutan_reset();
    reset_recorded();

    g_scene_jutan_selector = SCENE_JUTAN_COUNT;  /* selector misses → param=1 loads all */
    int loads = scene_jutan_load_with(recording_load_fn, 0, 1);
    T_ASSERT_EQ_I(loads, SCENE_JUTAN_COUNT);

    for (int i = 0; i < SCENE_JUTAN_COUNT; i++) {
        const char *p = g_rec.path[i];
        T_ASSERT(strncmp(p, "xfile/jutan/", 12) == 0);
        T_ASSERT(strcmp(p + 12, scene_jutan_filename(g_rec.slot[i])) == 0);
    }
    return 0;
}

/* The 4 sibling selectors (walls/floor/jutan/table) live at adjacent
 * 4-byte offsets in the engine's stage record (0x57c/580/584/588).
 * In our standalone-int port they must be independent globals. */
int test_scene_jutan_selector_independent_from_siblings(void)
{
    extern int32_t g_scene_walls_selector;
    extern int32_t g_scene_floor_selector;

    scene_jutan_reset();
    g_scene_walls_selector = 1;
    g_scene_floor_selector = 2;
    g_scene_jutan_selector = 4;

    T_ASSERT_EQ_I(g_scene_walls_selector, 1);
    T_ASSERT_EQ_I(g_scene_floor_selector, 2);
    T_ASSERT_EQ_I(g_scene_jutan_selector, 4);

    g_scene_walls_selector = 0;
    g_scene_floor_selector = 0;
    T_ASSERT_EQ_I(g_scene_jutan_selector, 4);
    return 0;
}
