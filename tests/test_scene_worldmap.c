/*
 * test_scene_worldmap.c — pure-C tests for the C96 secondary inner-body
 * BMP-loader half (engine FUN_004735ad — world-map asset loader).
 *
 * Structural sibling of test_scene_floor / test_scene_jutan but simpler
 * (no selector, no loop — 4 fixed dispatches).
 */
#include "t.h"

#include <string.h>

#include "scene_worldmap.h"
#include "worker_load.h"

/* Recording scratchpad for the injected load_fn. */
#define MAX_RECORDED 8
static struct {
    int      n;
    char     path[MAX_RECORDED][64];
    uint32_t w[MAX_RECORDED];
    uint32_t h[MAX_RECORDED];
    int      slot[MAX_RECORDED];
    void    *userdata[MAX_RECORDED];
} g_rec;

static void reset_recorded(void)
{
    memset(&g_rec, 0, sizeof(g_rec));
}

static int recording_load_fn(const char *path,
                             uint32_t w, uint32_t h,
                             int slot, void *userdata)
{
    if (g_rec.n < MAX_RECORDED) {
        size_t n = strlen(path);
        if (n >= sizeof(g_rec.path[0])) n = sizeof(g_rec.path[0]) - 1;
        memcpy(g_rec.path[g_rec.n], path, n);
        g_rec.path[g_rec.n][n] = '\0';
        g_rec.w[g_rec.n]        = w;
        g_rec.h[g_rec.n]        = h;
        g_rec.slot[g_rec.n]     = slot;
        g_rec.userdata[g_rec.n] = userdata;
        g_rec.n++;
    }
    return 1;
}

int test_scene_worldmap_count_is_four(void)
{
    /* Engine FUN_004735ad has exactly 4 sprite_load calls. */
    T_ASSERT_EQ_I(SCENE_WORLDMAP_COUNT, 4);
    return 0;
}

int test_scene_worldmap_filename_table_full_order(void)
{
    /* Strings extracted from vendor/unpacked/recettear.unpacked.exe via
     *   tools/analyze/pe.py str 0x005c87f4 0x005c880c 0x005c8824 0x005c883c
     */
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_NOMAL),
                    "bmp/worldmap_nomal.bmp")  == 0);
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_YUGATA),
                    "bmp/worldmap_yugata.bmp") == 0);
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_NIGHT),
                    "bmp/worldmap_night.bmp")  == 0);
    T_ASSERT(strcmp(scene_worldmap_filename(SCENE_WORLDMAP_TEX_MAPPOINT),
                    "bmp/mappoint.tga")        == 0);
    return 0;
}

int test_scene_worldmap_filename_out_of_range_is_null(void)
{
    T_ASSERT(scene_worldmap_filename(-1) == 0);
    T_ASSERT(scene_worldmap_filename(SCENE_WORLDMAP_COUNT) == 0);
    T_ASSERT(scene_worldmap_filename(9999) == 0);
    return 0;
}

int test_scene_worldmap_dims_match_engine_calls(void)
{
    /* Engine FUN_0047193c args at @ 0x4735ad:
     *   nomal/yugata/night → (0x400, 0x200)
     *   mappoint           → (0x100, 0x400) */
    uint32_t w, h;

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_NOMAL,    &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x400);
    T_ASSERT_EQ_U(h, 0x200);

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_YUGATA,   &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x400);
    T_ASSERT_EQ_U(h, 0x200);

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_NIGHT,    &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x400);
    T_ASSERT_EQ_U(h, 0x200);

    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_TEX_MAPPOINT, &w, &h), 1);
    T_ASSERT_EQ_U(w, 0x100);
    T_ASSERT_EQ_U(h, 0x400);
    return 0;
}

int test_scene_worldmap_dims_out_of_range_zeroes_outputs(void)
{
    uint32_t w = 0xdeadbeefu, h = 0xcafef00du;
    T_ASSERT_EQ_I(scene_worldmap_dims(-1, &w, &h), 0);
    T_ASSERT_EQ_U(w, 0);
    T_ASSERT_EQ_U(h, 0);

    w = 0xdeadbeefu; h = 0xcafef00du;
    T_ASSERT_EQ_I(scene_worldmap_dims(SCENE_WORLDMAP_COUNT, &w, &h), 0);
    T_ASSERT_EQ_U(w, 0);
    T_ASSERT_EQ_U(h, 0);

    /* NULL out-pointers are tolerated. */
    T_ASSERT_EQ_I(scene_worldmap_dims(0, 0, 0), 1);
    return 0;
}

int test_scene_worldmap_load_dispatches_all_four(void)
{
    scene_worldmap_reset();
    reset_recorded();

    int loads = scene_worldmap_load_with(recording_load_fn,
                                          (void *)0xdecafbadu);

    T_ASSERT_EQ_I(loads,    SCENE_WORLDMAP_COUNT);
    T_ASSERT_EQ_I(g_rec.n,  SCENE_WORLDMAP_COUNT);

    /* Slot order matches the engine call order. */
    T_ASSERT_EQ_I(g_rec.slot[0], SCENE_WORLDMAP_TEX_NOMAL);
    T_ASSERT_EQ_I(g_rec.slot[1], SCENE_WORLDMAP_TEX_YUGATA);
    T_ASSERT_EQ_I(g_rec.slot[2], SCENE_WORLDMAP_TEX_NIGHT);
    T_ASSERT_EQ_I(g_rec.slot[3], SCENE_WORLDMAP_TEX_MAPPOINT);

    /* Userdata threads through verbatim on every dispatch. */
    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        T_ASSERT(g_rec.userdata[i] == (void *)0xdecafbadu);
    }
    return 0;
}

int test_scene_worldmap_load_paths_match_filename_table(void)
{
    scene_worldmap_reset();
    reset_recorded();

    (void)scene_worldmap_load_with(recording_load_fn, 0);

    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        T_ASSERT(strcmp(g_rec.path[i],
                        scene_worldmap_filename(g_rec.slot[i])) == 0);
    }
    return 0;
}

int test_scene_worldmap_load_dims_match_metadata(void)
{
    scene_worldmap_reset();
    reset_recorded();

    (void)scene_worldmap_load_with(recording_load_fn, 0);

    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        uint32_t want_w, want_h;
        T_ASSERT_EQ_I(scene_worldmap_dims(g_rec.slot[i], &want_w, &want_h), 1);
        T_ASSERT_EQ_U(g_rec.w[i], want_w);
        T_ASSERT_EQ_U(g_rec.h[i], want_h);
    }
    return 0;
}

int test_scene_worldmap_load_without_load_fn_returns_count(void)
{
    /* NULL load_fn is a counting-only dry run. */
    scene_worldmap_reset();
    int loads = scene_worldmap_load_with(0, 0);
    T_ASSERT_EQ_I(loads, SCENE_WORLDMAP_COUNT);
    return 0;
}

int test_scene_worldmap_body_slot_starts_null(void)
{
    /* Body callback is NULL until scene_worldmap_init runs (which is
     * Win32-only). On the Linux test build the C96 slot stays NULL —
     * a half-port that's dormant until the spawner gets a caller. */
    worker_load_reset();
    T_ASSERT(worker_load_get_sec_body(WORKER_LOAD_SEC_BODY_C96) == 0);
    return 0;
}

/* Worldmap mappoint slot's dims (256x1024) are unique among the 4
 * worldmap entries — the other three are all 1024x512. Anchor this
 * specifically since the mappoint engine dest (DAT_073aa7d8) is at a
 * different base address from the worldmap-BG trio. */
int test_scene_worldmap_mappoint_is_tall_unique(void)
{
    uint32_t w, h;
    (void)scene_worldmap_dims(SCENE_WORLDMAP_TEX_MAPPOINT, &w, &h);
    T_ASSERT(w < h);  /* the only tall-portrait slot of the 4 */

    for (int i = 0; i < SCENE_WORLDMAP_COUNT; i++) {
        if (i == SCENE_WORLDMAP_TEX_MAPPOINT) continue;
        uint32_t bw, bh;
        (void)scene_worldmap_dims(i, &bw, &bh);
        T_ASSERT(bw > bh);  /* the 3 BG textures are wide-landscape */
    }
    return 0;
}
