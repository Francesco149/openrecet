/*
 * Minimal Windows DIB BMP decoder for OpenRecet.
 *
 * Used by the sprite loader to handle assets stored as `.bmp` (which
 * the original engine pairs with a green color key — pure 0x00FF00
 * pixels become fully transparent). 24- and 32-bit BI_RGB images,
 * top-down or bottom-up. Colormapped/palettized BMPs are not used by
 * any audited Recettear asset.
 *
 * The engine path is `D3DXCreateTextureFromFileInMemoryEx(...
 * ColorKey=0xFF00FF00)`; this module replicates that color-key behavior
 * directly since we don't link d3dx8 (see docs/findings/texture-
 * loader.md).
 */
#ifndef OPENRECET_BMP_H
#define OPENRECET_BMP_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t  width;
    uint32_t  height;
    /* width * height pixels, row-major, top-down, BGRA — D3DFMT_A8R8G8B8
     * byte order on little-endian. Pixels matching the color key (if
     * non-zero) have alpha forced to 0. Caller frees with bmp_free(). */
    uint8_t  *pixels;
} bmp_image;

/* Decode a BMP blob already in memory. `color_key` is in 0x00BBGGRR
 * form (only the low 24 bits are compared); pass 0 to disable keying.
 * The engine uses 0x00FF00 (pure green). Returns 1 on success. */
int  bmp_load_mem(const void *buf, size_t size, uint32_t color_key,
                  bmp_image *img);

void bmp_free(bmp_image *img);

#endif
