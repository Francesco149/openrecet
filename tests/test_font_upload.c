/*
 * test_font_upload.c — tests for the pure-C pixel-expansion math.
 * D3D upload itself is Win32-only and exercised by the scenario
 * runner.
 */

#include "t.h"

#include "../src/font_upload.h"

int test_font_upload_expand_transparent_byte(void)
{
    /* glyph_byte 0x00 → fully transparent black */
    T_ASSERT_EQ_U(font_upload_expand_pixel(0x00), 0x00000000u);
    return 0;
}

int test_font_upload_expand_body_pixel_white_full(void)
{
    /* glyph_byte 0xff → alpha=15 in nibble, edge=15 in nibble.
     * RGB = 15 << 4 = 0xf0. A = 15 << 4 = 0xf0.
     * Expected: 0xf0f0f0f0 (ARGB). */
    T_ASSERT_EQ_U(font_upload_expand_pixel(0xff), 0xf0f0f0f0u);
    return 0;
}

int test_font_upload_expand_body_pixel_dim(void)
{
    /* glyph_byte 0x1f → alpha=1, edge=15. RGB = 0x10, A = 0xf0. */
    T_ASSERT_EQ_U(font_upload_expand_pixel(0x1f), 0xf0101010u);
    return 0;
}

int test_font_upload_expand_edge_only_pixel(void)
{
    /* Edge-only pixel after dilation: high nibble = 1, low nibble = e.g. 8.
     * RGB = 0x10 (dark), A = 0x80. */
    T_ASSERT_EQ_U(font_upload_expand_pixel(0x18), 0x80101010u);
    return 0;
}

int test_font_upload_expand_alpha_nibble_only(void)
{
    /* glyph_byte 0xf0 → high nibble 15, low nibble 0. RGB = 240, A = 0. */
    T_ASSERT_EQ_U(font_upload_expand_pixel(0xf0), 0x00f0f0f0u);
    return 0;
}

int test_font_upload_expand_edge_nibble_only(void)
{
    /* glyph_byte 0x0a → high nibble 0, low nibble 10. RGB = 0, A = 160. */
    T_ASSERT_EQ_U(font_upload_expand_pixel(0x0a), 0xa0000000u);
    return 0;
}
