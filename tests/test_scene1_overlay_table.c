/*
 * test_scene1_overlay_table.c — unit tests for O.10:
 * scene1_overlay_table_parse_buf and the FUN_00474f4f port.
 *
 * The disk + storage-driven loaders aren't exercised here (no
 * filesystem dependencies in host tests); parse_buf is the pure layer
 * and covers every observable behaviour the engine has.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "scene1_overlay.h"
#include "scene1_overlay_table.h"

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    scene1_overlay_layers_reset();
    scene1_overlay_shapes_reset();
}

static float shape_uv_x(int idx)
{
    int32_t bits = g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE +
                                            SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static float shape_uv_y(int idx)
{
    int32_t bits = g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE +
                                            SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static float shape_uv_sx(int idx)
{
    int32_t bits = g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE +
                                            SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static float shape_uv_sy(int idx)
{
    int32_t bits = g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE +
                                            SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static int32_t shape_i(int idx, int off)
{
    return g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE + off];
}

static void parse(const char *text)
{
    scene1_overlay_table_parse_buf(text, strlen(text));
}

/* ─── tests ───────────────────────────────────────────────────────── */

int test_overlay_table_parse_skips_comment_and_blank_lines(void)
{
    reset_world();
    parse("/foo\n"
          "\n"
          "\r\n");
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 0);
    T_ASSERT_EQ_I(g_scene1_overlay_shapes_max_index, 0);
    return 0;
}

int test_overlay_table_parse_grp_writes_filename_slot(void)
{
    reset_world();
    parse("GRP00:bmp/effect00.bmp\n");
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 1);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[0],
                    "bmp/effect00.bmp") == 0);
    return 0;
}

int test_overlay_table_parse_grp_multiple_lines_increment_count(void)
{
    reset_world();
    /* Both lines use GRP01: — engine doesn't dedupe; each gets its
     * own slot, count increments per matching line. */
    parse("GRP00:bmp/effect00.bmp\n"
          "GRP01:bmp/effect01.bmp\n"
          "GRP01:bmp/effect02.bmp\n"
          "GRP02:bmp/b_coin.tga\n");
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 4);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[0],
                    "bmp/effect00.bmp") == 0);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[1],
                    "bmp/effect01.bmp") == 0);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[2],
                    "bmp/effect02.bmp") == 0);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[3],
                    "bmp/b_coin.tga") == 0);
    return 0;
}

int test_overlay_table_parse_grp_truncates_at_255(void)
{
    reset_world();
    /* Build a 300-char filename and confirm it's truncated to 255
     * with NUL at position 255 (max for the per-slot 256-byte
     * buffer). */
    char line[8 + 320];
    memcpy(line, "GRP00:", 6);
    for (int i = 0; i < 300; i++) line[6 + i] = 'A';
    line[6 + 300] = '\n';
    line[6 + 301] = '\0';
    parse(line);
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 1);
    T_ASSERT_EQ_I(
        (int)strlen(g_scene1_overlay_layer_filenames[0]),
        SCENE1_OVERLAY_LAYER_FILENAME_LEN - 1);
    return 0;
}

int test_overlay_table_parse_nnn_writes_shape_fields(void)
{
    reset_world();
    parse("003:01:(0,0,64,64)(16,2,0)\n");
    T_ASSERT_EQ_I(shape_i(3, SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP), 1);
    T_ASSERT(fabsf(shape_uv_x(3) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(shape_uv_y(3) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(shape_uv_sx(3) - 64.0f) < 1e-6f);
    T_ASSERT(fabsf(shape_uv_sy(3) - 64.0f) < 1e-6f);
    T_ASSERT_EQ_I(shape_i(3, SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT), 16);
    T_ASSERT_EQ_I(shape_i(3, SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD), 2);
    T_ASSERT_EQ_I(shape_i(3, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE), 0);
    return 0;
}

int test_overlay_table_parse_nnn_defaults_when_no_inner_group(void)
{
    reset_world();
    /* No second `(...)` group → defaults 1/1/0. */
    parse("000:00:(0,128,32,32)\n");
    T_ASSERT_EQ_I(shape_i(0, SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT), 1);
    T_ASSERT_EQ_I(shape_i(0, SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD), 1);
    T_ASSERT_EQ_I(shape_i(0, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE), 0);
    T_ASSERT_EQ_I(shape_i(0, SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP), 0);
    return 0;
}

int test_overlay_table_parse_nnn_inner_group_two_args_defaults_loop(void)
{
    reset_world();
    /* (frames,stride) without trailing comma → loop stays at default 0. */
    parse("004:02:(0,0,64,64)(8,2)\n");
    T_ASSERT_EQ_I(shape_i(4, SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT), 8);
    T_ASSERT_EQ_I(shape_i(4, SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD), 2);
    T_ASSERT_EQ_I(shape_i(4, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE), 0);
    return 0;
}

int test_overlay_table_parse_nnn_inner_group_three_args_writes_loop(void)
{
    reset_world();
    parse("004:02:(0,0,64,64)(8,2,1)\n");
    T_ASSERT_EQ_I(shape_i(4, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE), 1);
    return 0;
}

int test_overlay_table_parse_nnn_max_index_tracks_highest(void)
{
    reset_world();
    parse("005:00:(0,224,32,32)(8,4,0)\n"
          "030:03:(0,80,40,40)(6,4,1)\n"
          "012:01:(0,0,32,32)\n");
    T_ASSERT_EQ_I(g_scene1_overlay_shapes_max_index, 31);
    return 0;
}

int test_overlay_table_parse_nnn_oob_index_silently_dropped(void)
{
    reset_world();
    /* SCENE1_OVERLAY_SHAPE_COUNT == 256 → idx 500 must NOT corrupt
     * memory but DOES still update max_index (matches engine fidelity
     * — DAT_0076b94c is incremented before the body writes). */
    parse("500:01:(0,0,32,32)\n");
    T_ASSERT_EQ_I(g_scene1_overlay_shapes_max_index, 501);
    /* Confirm no write happened to any in-range slot: scan all
     * SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP fields. */
    for (int i = 0; i < SCENE1_OVERLAY_SHAPE_COUNT; i++) {
        T_ASSERT_EQ_I(shape_i(i, SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP), 0);
    }
    return 0;
}

int test_overlay_table_parse_nnn_uv_floats_stored_as_bits(void)
{
    reset_world();
    parse("007:02:(128,64,16,32)\n");
    /* Bit pattern of 128.0f vs 128 (integer in IEEE754): floats. */
    T_ASSERT(fabsf(shape_uv_x(7)  - 128.0f) < 1e-6f);
    T_ASSERT(fabsf(shape_uv_y(7)  -  64.0f) < 1e-6f);
    T_ASSERT(fabsf(shape_uv_sx(7) -  16.0f) < 1e-6f);
    T_ASSERT(fabsf(shape_uv_sy(7) -  32.0f) < 1e-6f);
    return 0;
}

int test_overlay_table_parse_handles_crlf_and_lf(void)
{
    reset_world();
    /* CRLF + LF mix; both must yield identical parse output. */
    parse("GRP00:a.bmp\r\n"
          "GRP01:b.bmp\n");
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 2);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[0], "a.bmp") == 0);
    T_ASSERT(strcmp(g_scene1_overlay_layer_filenames[1], "b.bmp") == 0);
    return 0;
}

int test_overlay_table_parse_mixed_grp_and_nnn(void)
{
    reset_world();
    /* Realistic excerpt from ef/grp1.idx (without the SJIS comments). */
    parse("/file registration\n"
          "GRP00:bmp/effect00.bmp\n"
          "GRP01:bmp/effect01.bmp\n"
          "\n"
          "/graphics registration\n"
          "000:00:(0,128,32,32)\n"
          "003:01:(0,0,64,64)(16,2,0)\n"
          "028:03:(0,0,40,40)(6,4,1)\n");
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 2);
    T_ASSERT_EQ_I(g_scene1_overlay_shapes_max_index, 29);
    T_ASSERT_EQ_I(shape_i(28, SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP), 3);
    T_ASSERT(fabsf(shape_uv_sx(28) - 40.0f) < 1e-6f);
    T_ASSERT_EQ_I(shape_i(28, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE), 1);
    return 0;
}

int test_overlay_table_parse_buf_empty_is_noop(void)
{
    reset_world();
    scene1_overlay_table_parse_buf(NULL, 0);
    scene1_overlay_table_parse_buf("", 0);
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 0);
    T_ASSERT_EQ_I(g_scene1_overlay_shapes_max_index, 0);
    return 0;
}
