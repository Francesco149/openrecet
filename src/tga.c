#include "tga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/* Copy one source pixel (24- or 32-bit BGR(A) little-endian) to dst as
 * BGRA, synthesizing alpha=0xFF for 24-bit. */
static void copy_pixel(uint8_t *dst, const uint8_t *src, int bpp_src)
{
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = (bpp_src == 4) ? src[3] : 0xFF;
}

int tga_load_mem(const void *buf, size_t size, tga_image *img)
{
    if (size < 18) return 0;
    const uint8_t *p = (const uint8_t *)buf;
    const uint8_t *end = p + size;

    uint8_t  id_len   = p[0];
    uint8_t  cmap_t   = p[1];
    uint8_t  img_t    = p[2];
    uint32_t w        = rd16(p + 12);
    uint32_t h        = rd16(p + 14);
    uint8_t  depth    = p[16];
    uint8_t  desc     = p[17];

    if (cmap_t != 0) return 0;
    if (img_t != 2 && img_t != 10) return 0;
    if (depth != 24 && depth != 32) return 0;
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return 0;

    /* Skip image-id field. */
    p += 18;
    if ((size_t)(end - p) < id_len) return 0;
    p += id_len;

    int bpp_src   = depth / 8;
    int top_down  = (desc & 0x20) != 0;
    size_t row_dst = (size_t)w * 4;
    size_t pix_total = (size_t)w * h;

    uint8_t *dst = (uint8_t *)malloc(pix_total * 4);
    if (!dst) return 0;

    /* Decode into a top-down BGRA buffer at `tmp`. After decoding, flip
     * vertically if the source was bottom-up. */
    uint8_t *tmp = (top_down) ? dst : (uint8_t *)malloc(pix_total * 4);
    if (!tmp) { free(dst); return 0; }

    if (img_t == 2) {
        /* Uncompressed: w*h pixels of bpp_src bytes each. */
        if ((size_t)(end - p) < pix_total * bpp_src) {
            if (tmp != dst) free(tmp);
            free(dst);
            return 0;
        }
        for (size_t i = 0; i < pix_total; i++) {
            copy_pixel(tmp + i * 4, p + i * bpp_src, bpp_src);
        }
    } else {
        /* Type 10 RLE: a stream of packets.
         *   header byte: top bit = run kind, low 7 bits = count - 1.
         *     0xxxxxxx → raw packet, next (count) pixels are literal.
         *     1xxxxxxx → RLE packet, next 1 pixel is repeated (count) times.
         * Packets may straddle scanlines (treat the whole image as a
         * single flat stream). */
        size_t i = 0;
        while (i < pix_total) {
            if (p >= end) {
                if (tmp != dst) free(tmp);
                free(dst);
                return 0;
            }
            uint8_t hdr = *p++;
            uint32_t count = (uint32_t)(hdr & 0x7F) + 1;
            if (count > pix_total - i) count = pix_total - i;

            if (hdr & 0x80) {
                /* RLE: read one source pixel, repeat. */
                if ((size_t)(end - p) < (size_t)bpp_src) {
                    if (tmp != dst) free(tmp);
                    free(dst);
                    return 0;
                }
                uint8_t px[4];
                copy_pixel(px, p, bpp_src);
                p += bpp_src;
                for (uint32_t k = 0; k < count; k++) {
                    memcpy(tmp + (i + k) * 4, px, 4);
                }
            } else {
                /* Raw: count source pixels. */
                if ((size_t)(end - p) < (size_t)count * bpp_src) {
                    if (tmp != dst) free(tmp);
                    free(dst);
                    return 0;
                }
                for (uint32_t k = 0; k < count; k++) {
                    copy_pixel(tmp + (i + k) * 4, p + k * bpp_src, bpp_src);
                }
                p += count * bpp_src;
            }
            i += count;
        }
    }

    if (!top_down) {
        /* Flip rows: tmp is bottom-up, dst is top-down. */
        for (uint32_t y = 0; y < h; y++) {
            memcpy(dst + (size_t)y * row_dst,
                   tmp + (size_t)(h - 1 - y) * row_dst,
                   row_dst);
        }
        free(tmp);
    }

    img->width  = w;
    img->height = h;
    img->pixels = dst;
    return 1;
}

int tga_load_file(const char *path, tga_image *img)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return 0; }
    rewind(fp);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return 0; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return 0;
    }
    fclose(fp);

    int ok = tga_load_mem(buf, (size_t)sz, img);
    free(buf);
    return ok;
}

void tga_free(tga_image *img)
{
    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
    img->width = img->height = 0;
}
