/*
 * test_scene_title.c — title-scene module tests.
 *
 * Only the pure-C asset table is testable on Linux; the actual
 * sprite_load wiring is D3D-bound and exercised by the boot smoke.
 * What we check here:
 *   - 7 entries are defined in slot order with the expected paths
 *   - each expected_w/expected_h is a power of two (matches the
 *     engine's texture conventions)
 *   - the paths match exactly what the engine's .rdata stores
 */
#include "t.h"
#include "scene_title.h"

#include <string.h>

static int is_pow2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

int test_scene_title_assets_count_is_seven(void)
{
    T_ASSERT_EQ_I(SCENE_TITLE_TEX_COUNT, 7);
    return 0;
}

int test_scene_title_assets_paths_match_pe(void)
{
    /* Strings extracted from the unpacked binary via
     *   `tools/analyze/pe.py str 0x005c8688 0x005c869c ...`
     * in slot order. */
    static const char *expected[] = {
        "bmp/title_bg2.bmp",
        "bmp/title01.tga",
        "bmp/title_fuki.tga",
        "bmp/title_waku.tga",
        "bmp/pause.tga",
        "bmp/result_bord01.tga",
        "bmp/dungeonbord.tga",
    };
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        if (strcmp(scene_title_assets[i].path, expected[i]) != 0) {
            T_FAIL("slot %d: got %s, want %s",
                   i, scene_title_assets[i].path, expected[i]);
        }
    }
    return 0;
}

int test_scene_title_assets_sizes_power_of_two(void)
{
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        const scene_title_asset_t *a = &scene_title_assets[i];
        if (!is_pow2(a->expected_w) || !is_pow2(a->expected_h)) {
            T_FAIL("slot %d (%s): non-pow2 size %ux%u",
                   i, a->path, a->expected_w, a->expected_h);
        }
    }
    return 0;
}

int test_scene_title_assets_sizes_match_engine(void)
{
    /* (w, h) pairs in the engine's FUN_0047193c call site. */
    static const uint32_t expected[][2] = {
        {1024, 1024},  /* title_bg2.bmp */
        { 512,  256},  /* title01.tga   */
        { 512, 1024},  /* title_fuki.tga */
        {1024,  512},  /* title_waku.tga */
        {1024,  512},  /* pause.tga */
        { 512,  256},  /* result_bord01.tga */
        {1024,  512},  /* dungeonbord.tga */
    };
    for (int i = 0; i < SCENE_TITLE_TEX_COUNT; i++) {
        T_ASSERT_EQ_U(scene_title_assets[i].expected_w, expected[i][0]);
        T_ASSERT_EQ_U(scene_title_assets[i].expected_h, expected[i][1]);
    }
    return 0;
}
