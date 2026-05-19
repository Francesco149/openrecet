#include "tga.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int tga_load_file(const char *path, tga_image *img)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    uint8_t hdr[18];
    if (fread(hdr, 1, 18, fp) != 18) { fclose(fp); return 0; }

    uint8_t  id_len   = hdr[0];
    uint8_t  cmap_t   = hdr[1];
    uint8_t  img_t    = hdr[2];
    uint32_t w        = rd16(hdr + 12);
    uint32_t h        = rd16(hdr + 14);
    uint8_t  depth    = hdr[16];
    uint8_t  desc     = hdr[17];

    if (cmap_t != 0 || img_t != 2 || (depth != 24 && depth != 32) ||
        w == 0 || h == 0 || w > 8192 || h > 8192) {
        fclose(fp);
        return 0;
    }

    if (id_len && fseek(fp, id_len, SEEK_CUR) != 0) { fclose(fp); return 0; }

    size_t bpp_src   = depth / 8;
    size_t row_src   = (size_t)w * bpp_src;
    size_t pix_count = (size_t)w * h;
    uint8_t *src = malloc(row_src * h);
    uint8_t *dst = malloc(pix_count * 4);
    if (!src || !dst) { free(src); free(dst); fclose(fp); return 0; }

    if (fread(src, 1, row_src * h, fp) != row_src * h) {
        free(src); free(dst); fclose(fp); return 0;
    }
    fclose(fp);

    int top_down = (desc & 0x20) != 0;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t src_y = top_down ? y : (h - 1 - y);
        const uint8_t *s = src + src_y * row_src;
        uint8_t       *d = dst + (size_t)y * w * 4;
        if (depth == 32) {
            memcpy(d, s, row_src);
        } else {
            for (uint32_t x = 0; x < w; x++) {
                d[x*4 + 0] = s[x*3 + 0];
                d[x*4 + 1] = s[x*3 + 1];
                d[x*4 + 2] = s[x*3 + 2];
                d[x*4 + 3] = 0xFF;
            }
        }
    }
    free(src);

    img->width  = w;
    img->height = h;
    img->pixels = dst;
    return 1;
}

void tga_free(tga_image *img)
{
    if (img->pixels) {
        free(img->pixels);
        img->pixels = NULL;
    }
    img->width = img->height = 0;
}
