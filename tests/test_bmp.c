/*
 * test_bmp.c — unit tests for src/bmp.c
 *
 * Synthetic fixtures are assembled byte-by-byte so the test stays in
 * sync with the decoder without depending on any external image lib.
 * UBSan-safe writers come from tests/t.h.
 */
#include "t.h"
#include "bmp.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Assemble a minimal BI_RGB BMP into a malloc'd buffer.
 *   bpp:  24 or 32
 *   w:    positive
 *   h:    positive (bottom-up) or negative (top-down)
 *   pixels: bpp/8 bytes per pixel, row-major in DIB order. Caller
 *           supplies them without padding; this helper pads each row
 *           to a 4-byte boundary. For 24-bit pixels: BGR order.
 *
 * Returns buffer + sets *out_size on success; NULL on alloc failure. */
static uint8_t *build_bmp(int bpp, int w, int h_signed,
                          const uint8_t *pixels, size_t *out_size)
{
    int bpp_src   = bpp / 8;
    int abs_h     = h_signed < 0 ? -h_signed : h_signed;
    size_t row    = (size_t)w * bpp_src;
    size_t stride = (row + 3u) & ~(size_t)3u;
    size_t img    = stride * (size_t)abs_h;
    size_t fs     = 14 + 40 + img;

    uint8_t *buf = (uint8_t *)calloc(fs, 1);
    if (!buf) return NULL;

    buf[0] = 'B'; buf[1] = 'M';
    t_wr32_le(buf + 2,  (unsigned)fs);          /* file size */
    t_wr32_le(buf + 10, 54);                    /* off_bits */
    t_wr32_le(buf + 14, 40);                    /* info size */
    t_wr32_le(buf + 18, (unsigned)w);
    t_wr32_le(buf + 22, (unsigned)h_signed);    /* keep sign */
    t_wr16_le(buf + 26, 1);                     /* planes */
    t_wr16_le(buf + 28, (unsigned)bpp);
    t_wr32_le(buf + 30, 0);                     /* BI_RGB */
    t_wr32_le(buf + 34, (unsigned)img);

    uint8_t *dst = buf + 54;
    for (int y = 0; y < abs_h; y++) {
        memcpy(dst + (size_t)y * stride,
               pixels + (size_t)y * row,
               row);
    }
    *out_size = fs;
    return buf;
}

/* ─── tests ────────────────────────────────────────────────────────────── */

/* 2x2, bottom-up: file row 0 (bottom of image) = red, green;
 *                 file row 1 (top of image)    = blue, white. */
int test_bmp_basic_24bit(void)
{
    /* DIB pixel order: BGR per pixel, row-major. */
    uint8_t src[] = {
        /* row 0 (bottom) */ 0x00, 0x00, 0xFF,  0x00, 0xFF, 0x00,
        /* row 1 (top)    */ 0xFF, 0x00, 0x00,  0xFF, 0xFF, 0xFF,
    };
    size_t fs;
    uint8_t *file = build_bmp(24, 2, 2, src, &fs);
    T_ASSERT(file != NULL);

    bmp_image img = {0};
    T_ASSERT(bmp_load_mem(file, fs, 0, &img));
    T_ASSERT_EQ_U(img.width,  2);
    T_ASSERT_EQ_U(img.height, 2);

    /* Output is top-down BGRA. Displayed (0,0) = top-left = blue. */
    T_ASSERT_EQ_U(img.pixels[0 + 0], 0xFF);   /* B */
    T_ASSERT_EQ_U(img.pixels[0 + 1], 0x00);   /* G */
    T_ASSERT_EQ_U(img.pixels[0 + 2], 0x00);   /* R */
    T_ASSERT_EQ_U(img.pixels[0 + 3], 0xFF);   /* A synthesized */

    /* (1,0) = top-right = white */
    T_ASSERT_EQ_U(img.pixels[4 + 0], 0xFF);
    T_ASSERT_EQ_U(img.pixels[4 + 1], 0xFF);
    T_ASSERT_EQ_U(img.pixels[4 + 2], 0xFF);

    /* Row stride in dst = width * 4 = 8 bytes; row 1 starts at byte 8. */
    /* (0,1) = bottom-left = red */
    T_ASSERT_EQ_U(img.pixels[8 + 0], 0x00);   /* B */
    T_ASSERT_EQ_U(img.pixels[8 + 1], 0x00);   /* G */
    T_ASSERT_EQ_U(img.pixels[8 + 2], 0xFF);   /* R */

    /* (1,1) = bottom-right = green */
    T_ASSERT_EQ_U(img.pixels[8 + 4], 0x00);
    T_ASSERT_EQ_U(img.pixels[8 + 5], 0xFF);
    T_ASSERT_EQ_U(img.pixels[8 + 6], 0x00);

    bmp_free(&img);
    free(file);
    return 0;
}

/* 1x1 pure-green pixel + key=0x00FF00 → alpha must be 0 on that pixel,
 * and 0xFF on a non-keyed pixel in the same image. */
int test_bmp_color_key(void)
{
    /* 2x1 image: green (keyed) + blue (not keyed). */
    uint8_t src[] = {
        0x00, 0xFF, 0x00,  0xFF, 0x00, 0x00,  /* bottom row */
    };
    /* 2*3 = 6 bytes data → row stride must be padded to 8 bytes. */
    size_t fs;
    uint8_t *file = build_bmp(24, 2, 1, src, &fs);
    T_ASSERT(file != NULL);

    bmp_image img = {0};
    T_ASSERT(bmp_load_mem(file, fs, 0x00FF00u, &img));

    /* Pixel 0: green, keyed → A=0 */
    T_ASSERT_EQ_U(img.pixels[0],  0x00);
    T_ASSERT_EQ_U(img.pixels[1],  0xFF);
    T_ASSERT_EQ_U(img.pixels[2],  0x00);
    T_ASSERT_EQ_U(img.pixels[3],  0x00);

    /* Pixel 1: blue, not keyed → A=0xFF */
    T_ASSERT_EQ_U(img.pixels[4],  0xFF);
    T_ASSERT_EQ_U(img.pixels[5],  0x00);
    T_ASSERT_EQ_U(img.pixels[6],  0x00);
    T_ASSERT_EQ_U(img.pixels[7],  0xFF);

    bmp_free(&img);
    free(file);
    return 0;
}

/* Same green pixel but key=0 (disabled) → alpha stays 0xFF. */
int test_bmp_color_key_disabled(void)
{
    uint8_t src[] = { 0x00, 0xFF, 0x00 };
    size_t fs;
    uint8_t *file = build_bmp(24, 1, 1, src, &fs);
    T_ASSERT(file != NULL);

    bmp_image img = {0};
    T_ASSERT(bmp_load_mem(file, fs, 0, &img));
    T_ASSERT_EQ_U(img.pixels[3], 0xFF);

    bmp_free(&img);
    free(file);
    return 0;
}

/* Top-down BMP (negative height) — file row 0 is the top of the image. */
int test_bmp_top_down(void)
{
    uint8_t src[] = {
        /* file row 0 = top */    0x00, 0x00, 0xFF,
        /* file row 1 = bottom */ 0xFF, 0x00, 0x00,
    };
    size_t fs;
    uint8_t *file = build_bmp(24, 1, -2, src, &fs);
    T_ASSERT(file != NULL);

    bmp_image img = {0};
    T_ASSERT(bmp_load_mem(file, fs, 0, &img));
    T_ASSERT_EQ_U(img.height, 2);

    /* (0,0) = top = red */
    T_ASSERT_EQ_U(img.pixels[2], 0xFF);  /* R */
    /* (0,1) = bottom = blue */
    T_ASSERT_EQ_U(img.pixels[4 + 0], 0xFF);  /* B */

    bmp_free(&img);
    free(file);
    return 0;
}

/* 32-bit BMP: source alpha is preserved (not synthesized). */
int test_bmp_32bit(void)
{
    uint8_t src[] = {
        /* one pixel, BGRA */
        0x40, 0x80, 0xC0, 0x7F,
    };
    size_t fs;
    uint8_t *file = build_bmp(32, 1, 1, src, &fs);
    T_ASSERT(file != NULL);

    bmp_image img = {0};
    T_ASSERT(bmp_load_mem(file, fs, 0, &img));
    T_ASSERT_EQ_U(img.pixels[0], 0x40);
    T_ASSERT_EQ_U(img.pixels[1], 0x80);
    T_ASSERT_EQ_U(img.pixels[2], 0xC0);
    T_ASSERT_EQ_U(img.pixels[3], 0x7F);   /* alpha preserved */

    bmp_free(&img);
    free(file);
    return 0;
}

/* ─── rejection paths — these are where ASan earns its keep ──────────── */

int test_bmp_reject_bad_magic(void)
{
    uint8_t bogus[] = "FZ this is not a BMP, just garbage bytes hereXX";
    bmp_image img = {0};
    T_ASSERT(!bmp_load_mem(bogus, sizeof(bogus), 0, &img));
    return 0;
}

int test_bmp_reject_truncated(void)
{
    bmp_image img = {0};

    /* zero-length input */
    T_ASSERT(!bmp_load_mem(NULL, 0, 0, &img));

    /* 2 bytes — just enough for 'BM' but nothing else */
    uint8_t two[] = { 'B', 'M' };
    T_ASSERT(!bmp_load_mem(two, sizeof(two), 0, &img));

    /* Valid-looking header that promises 100x100 24-bit pixels, but the
     * file is only big enough for the headers. The decoder must reject
     * this rather than read past the buffer. */
    uint8_t hdr[60] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    t_wr32_le(hdr + 10, 54);
    t_wr32_le(hdr + 14, 40);
    t_wr32_le(hdr + 18, 100);
    t_wr32_le(hdr + 22, 100);
    t_wr16_le(hdr + 26, 1);
    t_wr16_le(hdr + 28, 24);
    T_ASSERT(!bmp_load_mem(hdr, sizeof(hdr), 0, &img));

    return 0;
}

/* Compression != BI_RGB — we don't decode RLE8/RLE4/BITFIELDS/etc. */
int test_bmp_reject_unsupported_compression(void)
{
    /* Build a normal 2x2 24-bit BMP, then flip the compression field. */
    uint8_t src[] = {
        0x00, 0x00, 0xFF,  0x00, 0xFF, 0x00,
        0xFF, 0x00, 0x00,  0xFF, 0xFF, 0xFF,
    };
    size_t fs;
    uint8_t *file = build_bmp(24, 2, 2, src, &fs);
    T_ASSERT(file != NULL);
    t_wr32_le(file + 30, 3);    /* BI_BITFIELDS */

    bmp_image img = {0};
    T_ASSERT(!bmp_load_mem(file, fs, 0, &img));

    free(file);
    return 0;
}

/* 8-bit palettized BMP — not supported. */
int test_bmp_reject_palettized(void)
{
    /* Build a valid 24-bit file, then flip depth → 8. The decoder
     * should reject at the depth check before touching the pixel data. */
    uint8_t src[3] = { 0x00, 0x00, 0x00 };
    size_t fs;
    uint8_t *file = build_bmp(24, 1, 1, src, &fs);
    T_ASSERT(file != NULL);
    t_wr16_le(file + 28, 8);

    bmp_image img = {0};
    T_ASSERT(!bmp_load_mem(file, fs, 0, &img));

    free(file);
    return 0;
}
