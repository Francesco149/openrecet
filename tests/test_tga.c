/*
 * test_tga.c — unit tests for src/tga.c
 *
 * Covers Type 2 (uncompressed truecolor) and Type 10 (RLE truecolor),
 * 24- and 32-bit, top-down and bottom-up, plus malformed-input
 * rejection paths. The RLE tests also exercise edge cases where a
 * single RLE/raw packet straddles a scanline boundary.
 */
#include "t.h"
#include "tga.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Build a TGA header (18 bytes) into `out`. desc: bit 5 = top-down. */
static void tga_header(uint8_t out[18], int img_t, int w, int h,
                       int depth, int desc)
{
    memset(out, 0, 18);
    out[0]  = 0;                            /* id_len */
    out[1]  = 0;                            /* cmap_t */
    out[2]  = (uint8_t)img_t;
    /* bytes 3..11 = cmap spec + origin (zero) */
    t_wr16_le(out + 12, (unsigned)w);
    t_wr16_le(out + 14, (unsigned)h);
    out[16] = (uint8_t)depth;
    out[17] = (uint8_t)desc;
}

/* ─── Type 2 (uncompressed) ──────────────────────────────────────────── */

/* 2x2 BGR, bottom-up — file row 0 = bottom of image. */
int test_tga_type2_24bit_bottom_up(void)
{
    uint8_t buf[18 + 2*2*3];
    tga_header(buf, 2, 2, 2, 24, 0x00);   /* desc=0: bottom-up */
    uint8_t *p = buf + 18;
    /* row 0 (bottom): red, green */
    p[0]=0x00; p[1]=0x00; p[2]=0xFF;  p[3]=0x00; p[4]=0xFF; p[5]=0x00;
    /* row 1 (top): blue, white */
    p[6]=0xFF; p[7]=0x00; p[8]=0x00;  p[9]=0xFF; p[10]=0xFF; p[11]=0xFF;

    tga_image img = {0};
    T_ASSERT(tga_load_mem(buf, sizeof(buf), &img));
    T_ASSERT_EQ_U(img.width,  2);
    T_ASSERT_EQ_U(img.height, 2);
    /* Top-down BGRA. (0,0) = top-left = blue. */
    T_ASSERT_EQ_U(img.pixels[0], 0xFF);   /* B */
    T_ASSERT_EQ_U(img.pixels[1], 0x00);
    T_ASSERT_EQ_U(img.pixels[2], 0x00);
    T_ASSERT_EQ_U(img.pixels[3], 0xFF);   /* synth alpha */
    /* (0,1) = bottom-left = red */
    T_ASSERT_EQ_U(img.pixels[(2*2)*1 + 2], 0xFF);

    tga_free(&img);
    return 0;
}

/* 2x1, 32-bit, top-down: alpha preserved. */
int test_tga_type2_32bit_top_down(void)
{
    uint8_t buf[18 + 2*1*4];
    tga_header(buf, 2, 2, 1, 32, 0x20);   /* desc=0x20: top-down */
    uint8_t *p = buf + 18;
    /* one row: pixel 0 = (BGRA) 10,20,30,40 ; pixel 1 = 0xff*4 */
    p[0]=10; p[1]=20; p[2]=30; p[3]=40;
    p[4]=0xFF; p[5]=0xFF; p[6]=0xFF; p[7]=0xFF;

    tga_image img = {0};
    T_ASSERT(tga_load_mem(buf, sizeof(buf), &img));
    T_ASSERT_EQ_U(img.pixels[0], 10);
    T_ASSERT_EQ_U(img.pixels[1], 20);
    T_ASSERT_EQ_U(img.pixels[2], 30);
    T_ASSERT_EQ_U(img.pixels[3], 40);

    tga_free(&img);
    return 0;
}

/* ─── Type 10 RLE ────────────────────────────────────────────────────── */

/* One RLE packet covering the whole image: 4 px run of red. */
int test_tga_type10_rle_single_run(void)
{
    uint8_t buf[18 + 1 + 3];
    tga_header(buf, 10, 4, 1, 24, 0x20);   /* top-down */
    buf[18]    = 0x80 | 3;                 /* RLE: 4 repeats */
    buf[19]    = 0x00;                     /* B=0 */
    buf[20]    = 0x00;                     /* G=0 */
    buf[21]    = 0xFF;                     /* R=255 */

    tga_image img = {0};
    T_ASSERT(tga_load_mem(buf, sizeof(buf), &img));
    T_ASSERT_EQ_U(img.width, 4);
    T_ASSERT_EQ_U(img.height, 1);
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_U(img.pixels[i*4 + 0], 0x00);
        T_ASSERT_EQ_U(img.pixels[i*4 + 1], 0x00);
        T_ASSERT_EQ_U(img.pixels[i*4 + 2], 0xFF);
        T_ASSERT_EQ_U(img.pixels[i*4 + 3], 0xFF);
    }
    tga_free(&img);
    return 0;
}

/* Mixed: one raw packet of 2 px (red, green) + one RLE packet of 2 px (blue). */
int test_tga_type10_rle_mixed(void)
{
    uint8_t buf[18 + 1 + 6 + 1 + 3];
    tga_header(buf, 10, 4, 1, 24, 0x20);
    /* Raw packet: count=2, two BGR pixels (red, green). */
    buf[18] = 0x00 | 1;
    buf[19]=0x00; buf[20]=0x00; buf[21]=0xFF;
    buf[22]=0x00; buf[23]=0xFF; buf[24]=0x00;
    /* RLE packet: count=2 of blue. */
    buf[25] = 0x80 | 1;
    buf[26]=0xFF; buf[27]=0x00; buf[28]=0x00;

    tga_image img = {0};
    T_ASSERT(tga_load_mem(buf, sizeof(buf), &img));
    /* x=0 red, x=1 green, x=2 blue, x=3 blue */
    T_ASSERT_EQ_U(img.pixels[ 0*4 + 2], 0xFF);  /* R */
    T_ASSERT_EQ_U(img.pixels[ 1*4 + 1], 0xFF);  /* G */
    T_ASSERT_EQ_U(img.pixels[ 2*4 + 0], 0xFF);  /* B */
    T_ASSERT_EQ_U(img.pixels[ 3*4 + 0], 0xFF);  /* B */
    tga_free(&img);
    return 0;
}

/* A packet that straddles two scanlines — engine treats the stream as
 * one flat sequence, not per-row. We do too. */
int test_tga_type10_rle_split_pixel(void)
{
    uint8_t buf[18 + 1 + 3];
    tga_header(buf, 10, 2, 2, 24, 0x20);   /* 2x2, top-down */
    buf[18] = 0x80 | 3;                    /* one RLE run of 4 = whole image */
    buf[19] = 0x12; buf[20] = 0x34; buf[21] = 0x56;

    tga_image img = {0};
    T_ASSERT(tga_load_mem(buf, sizeof(buf), &img));
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_U(img.pixels[i*4 + 0], 0x12);
        T_ASSERT_EQ_U(img.pixels[i*4 + 1], 0x34);
        T_ASSERT_EQ_U(img.pixels[i*4 + 2], 0x56);
    }
    tga_free(&img);
    return 0;
}

/* ─── rejection paths ────────────────────────────────────────────────── */

int test_tga_reject_unsupported_type(void)
{
    uint8_t buf[18];
    tga_header(buf, 1, 1, 1, 8, 0);    /* colormapped, depth=8 */

    tga_image img = {0};
    T_ASSERT(!tga_load_mem(buf, sizeof(buf), &img));
    return 0;
}

/* Header says 100x100 but the buffer is the header only. */
int test_tga_reject_truncated_uncompressed(void)
{
    uint8_t buf[18];
    tga_header(buf, 2, 100, 100, 24, 0x20);

    tga_image img = {0};
    T_ASSERT(!tga_load_mem(buf, sizeof(buf), &img));
    return 0;
}

/* RLE stream cut off mid-run: header promises 4 px, packet says
 * 4-pixel RLE run, but the source pixel is missing. */
int test_tga_reject_truncated_rle(void)
{
    uint8_t buf[18 + 1];   /* room for header + one packet header, no pixel */
    tga_header(buf, 10, 4, 1, 24, 0x20);
    buf[18] = 0x80 | 3;    /* RLE: 4 repeats, but missing 3 bytes of pixel */

    tga_image img = {0};
    T_ASSERT(!tga_load_mem(buf, sizeof(buf), &img));
    return 0;
}
