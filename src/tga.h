/*
 * Minimal TGA reader for OpenRecet.
 *
 * Supports the two types the engine actually loads:
 *   - Type 2  (uncompressed truecolor)
 *   - Type 10 (RLE truecolor)
 * Both at 24- or 32-bit, top-down or bottom-up. Colormapped (type 1)
 * and grayscale (type 3) are not used by any audited asset.
 *
 * The original engine uses D3DXCreateTextureFromFileInMemoryEx (d3dx8)
 * for its textures — see FUN_0047193c in docs/findings/texture-loader.md.
 * We bypass d3dx8 because it's deprecated and not in nixpkgs; pixel-
 * identical loader behavior under resampling is a later milestone.
 */
#ifndef OPENRECET_TGA_H
#define OPENRECET_TGA_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t  width;
    uint32_t  height;
    /* width * height pixels, row-major, top-down, BGRA — i.e. the
     * D3DFMT_A8R8G8B8 byte order on little-endian. Caller frees with
     * tga_free(). */
    uint8_t  *pixels;
} tga_image;

/* Decode a TGA blob already in memory (e.g. from storage_read). Returns
 * 1 on success, 0 on any header/length error. On failure *img is left
 * untouched. */
int  tga_load_mem(const void *buf, size_t size, tga_image *img);

/* Convenience: open `path` from disk and run tga_load_mem on it.
 * Returns 1 on success. */
int  tga_load_file(const char *path, tga_image *img);

void tga_free(tga_image *img);

#endif
