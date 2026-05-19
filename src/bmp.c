#include "bmp.h"

#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int bmp_load_mem(const void *buf, size_t size, uint32_t color_key,
                 bmp_image *img)
{
    /* 14-byte BITMAPFILEHEADER + at least 12-byte info header. */
    if (size < 14 + 12) return 0;
    const uint8_t *p   = (const uint8_t *)buf;
    const uint8_t *end = p + size;

    if (p[0] != 'B' || p[1] != 'M') return 0;
    uint32_t off_bits  = rd32(p + 10);
    uint32_t info_size = rd32(p + 14);

    /* Only support BITMAPINFOHEADER (40) and its v4/v5 supersets. */
    if (info_size < 40) return 0;
    if (14 + info_size > size) return 0;
    if (off_bits > size) return 0;

    int32_t  width      = (int32_t)rd32(p + 18);
    int32_t  height_raw = (int32_t)rd32(p + 22);
    uint16_t planes     = rd16(p + 26);
    uint16_t depth      = rd16(p + 28);
    uint32_t compr      = rd32(p + 30);

    if (planes != 1) return 0;
    if (depth != 24 && depth != 32) return 0;
    if (compr != 0) return 0;   /* BI_RGB only — no BITFIELDS / RLE / JPEG. */
    if (width <= 0 || width > 8192) return 0;

    int top_down = (height_raw < 0);
    uint32_t h   = (uint32_t)(top_down ? -height_raw : height_raw);
    uint32_t w   = (uint32_t)width;
    if (h == 0 || h > 8192) return 0;

    int bpp_src   = depth / 8;
    /* DIB rows are padded to a 4-byte boundary. */
    size_t row_src = ((size_t)w * bpp_src + 3u) & ~(size_t)3u;
    size_t pixels_bytes = row_src * h;

    if (off_bits + pixels_bytes > size) return 0;

    const uint8_t *src_base = p + off_bits;
    uint8_t *dst = (uint8_t *)malloc((size_t)w * h * 4);
    if (!dst) return 0;

    uint8_t key_b = (uint8_t)(color_key);
    uint8_t key_g = (uint8_t)(color_key >> 8);
    uint8_t key_r = (uint8_t)(color_key >> 16);
    int     use_key = (color_key != 0);

    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = top_down ? y : (h - 1 - y);
        const uint8_t *s = src_base + (size_t)src_y * row_src;
        uint8_t       *d = dst + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t b = s[0], g = s[1], r = s[2];
            uint8_t a = (bpp_src == 4) ? s[3] : 0xFF;
            if (use_key && b == key_b && g == key_g && r == key_r) {
                a = 0;
            }
            d[0] = b; d[1] = g; d[2] = r; d[3] = a;
            s += bpp_src;
            d += 4;
        }
    }

    (void)end;
    img->width  = w;
    img->height = h;
    img->pixels = dst;
    return 1;
}

void bmp_free(bmp_image *img)
{
    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
    img->width = img->height = 0;
}
