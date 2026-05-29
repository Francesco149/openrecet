/*
 * test_chr_sprite_meta.c — Cchr.2a coverage.
 *
 * Exercises chr_meta_parse_idx (the per-file body of engine FUN_00479f78)
 * against synthetic .idx text.  The .idx grammar (resolved from the
 * engine's "%s" sscanf formats + the "HALT" keyword at 0x5cb994):
 *
 *   line 0        sheet name              (token)
 *   line 1        hdr0,hdr1               (two comma ints)
 *   line 2        sheet_w,hdr3            (two comma ints)
 *   line 3        y_origin                (one int)
 *   line 4        scale_x100              (one int)
 *   line 5        <discarded>
 *   then, per animation:
 *     "/"                                 (start / terminate animation)
 *     "a,b,c,d,e,f"                       (frame: up to 6 comma ints)
 *     "HALT"                              (hold marker: 6 × 0x3ff)
 *
 * Frames pack 6 dwords each within a 0x100-dword animation block; the
 * slot after an animation's last frame gets 0xffffffff on the next "/".
 */
#include "t.h"

#include <stdint.h>

#include "chr_sprite_meta.h"

/* A complete two-animation sheet exercising every parser branch. */
static const char *const IDX_SAMPLE =
    "recette_sheet\n"
    "1,5\n"
    "256,8\n"
    "0\n"
    "50\n"
    "extra\n"
    "/\n"
    "10,2,3,4,5,6\n"
    "20,2,3,4,5,6\n"
    "HALT\n"
    "/\n"
    "30,2,3,4,5,6\n";

static int parse_sample(int idx, const char *text)
{
    chr_meta_shutdown();
    if (!chr_meta_alloc())
        return 0;
    chr_meta_parse_idx(idx, text);
    return 1;
}

int test_chr_meta_header_fields(void)
{
    if (!parse_sample(0, IDX_SAMPLE))
        T_FAIL("alloc failed");
    if (strcmp(chr_meta_name(0), "recette_sheet") != 0)
        T_FAIL("name = '%s'", chr_meta_name(0));
    T_ASSERT_EQ_I(chr_meta_sheet_w(0), 256);
    T_ASSERT_EQ_I(chr_meta_scale_x100(0), 50);
    T_ASSERT_EQ_I(chr_meta_y_origin(0), 0);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_frames_packed(void)
{
    if (!parse_sample(0, IDX_SAMPLE))
        T_FAIL("alloc failed");
    /* anim 0, frame 0 = 10,2,3,4,5,6 */
    T_ASSERT_EQ_I(chr_meta_lut(0, 0, 0, 0), 10);
    T_ASSERT_EQ_I(chr_meta_lut(0, 0, 0, 5), 6);
    /* anim 0, frame 1 = 20,... (packed at dword 6) */
    T_ASSERT_EQ_I(chr_meta_lut(0, 0, 1, 0), 20);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_halt_marker(void)
{
    if (!parse_sample(0, IDX_SAMPLE))
        T_FAIL("alloc failed");
    /* anim 0, frame 2 = HALT → six 0x3ff */
    for (int f = 0; f < 6; f++)
        T_ASSERT_EQ_I(chr_meta_lut(0, 0, 2, f), CHR_META_HALT);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_anim_terminator(void)
{
    if (!parse_sample(0, IDX_SAMPLE))
        T_FAIL("alloc failed");
    /* the "/" after frames 0,1,HALT (= dwords 0..17) writes 0xffffffff at
     * dword 18 = anim 0, frame 3, field 0. */
    T_ASSERT_EQ_I((uint32_t)chr_meta_lut(0, 0, 3, 0), CHR_META_ANIM_END);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_second_animation(void)
{
    if (!parse_sample(0, IDX_SAMPLE))
        T_FAIL("alloc failed");
    /* anim 1 (0x100-dword block) frame 0 = 30,... */
    T_ASSERT_EQ_I(chr_meta_lut(0, 1, 0, 0), 30);
    T_ASSERT_EQ_I(chr_meta_lut(0, 1, 0, 5), 6);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_crlf_line_endings(void)
{
    /* CRLF must parse identically to LF (\r is whitespace for "%s"). */
    static const char *const crlf =
        "tear\r\n1,2\r\n128,4\r\n3\r\n75\r\nx\r\n/\r\n7,7,7,7,7,7\r\n";
    if (!parse_sample(0, crlf))
        T_FAIL("alloc failed");
    if (strcmp(chr_meta_name(0), "tear") != 0)
        T_FAIL("name = '%s'", chr_meta_name(0));
    T_ASSERT_EQ_I(chr_meta_sheet_w(0), 128);
    T_ASSERT_EQ_I(chr_meta_scale_x100(0), 75);
    T_ASSERT_EQ_I(chr_meta_y_origin(0), 3);
    T_ASSERT_EQ_I(chr_meta_lut(0, 0, 0, 0), 7);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_blocks_independent(void)
{
    chr_meta_shutdown();
    if (!chr_meta_alloc())
        T_FAIL("alloc failed");
    chr_meta_parse_idx(0, IDX_SAMPLE);
    chr_meta_parse_idx(5, "mint\n9,9\n64,1\n2\n10\nz\n/\n1,2,3,4,5,6\n");
    /* char 0 untouched by char 5's parse */
    if (strcmp(chr_meta_name(0), "recette_sheet") != 0)
        T_FAIL("char0 name clobbered: '%s'", chr_meta_name(0));
    T_ASSERT_EQ_I(chr_meta_sheet_w(0), 256);
    /* char 5 has its own values */
    if (strcmp(chr_meta_name(5), "mint") != 0)
        T_FAIL("char5 name = '%s'", chr_meta_name(5));
    T_ASSERT_EQ_I(chr_meta_sheet_w(5), 64);
    T_ASSERT_EQ_I(chr_meta_lut(5, 0, 0, 0), 1);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_oob_and_null_safe(void)
{
    chr_meta_shutdown();
    /* accessors before alloc → safe defaults */
    T_ASSERT(chr_meta_block(0) == NULL);
    T_ASSERT(chr_meta_name(0) == NULL);
    T_ASSERT_EQ_I(chr_meta_sheet_w(0), 0);
    if (!chr_meta_alloc())
        T_FAIL("alloc failed");
    /* out-of-range char index */
    T_ASSERT(chr_meta_block(-1) == NULL);
    T_ASSERT(chr_meta_block(CHR_META_NUM_CHARS) == NULL);
    T_ASSERT_EQ_I(chr_meta_sheet_w(CHR_META_NUM_CHARS), 0);
    /* parse with NULL text is a no-op (no crash) */
    chr_meta_parse_idx(0, NULL);
    chr_meta_shutdown();
    return 0;
}

int test_chr_meta_alloc_idempotent(void)
{
    chr_meta_shutdown();
    if (!chr_meta_alloc())
        T_FAIL("alloc failed");
    uint8_t *first = g_chr_desc;
    if (!chr_meta_alloc())
        T_FAIL("second alloc failed");
    if (g_chr_desc != first)
        T_FAIL("alloc not idempotent");
    chr_meta_shutdown();
    return 0;
}
