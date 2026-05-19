/*
 * Minimal TGA reader for OpenRecet — uncompressed truecolor (type 2),
 * 24- or 32-bit, top-down or bottom-up. RLE (type 10) intentionally
 * not supported yet: the assets we've audited so far are all type 2.
 *
 * The original engine uses D3DXCreateTextureFromFileInMemoryEx (d3dx8)
 * for its textures — see FUN_0047193c in
 * docs/findings/winmain-and-bootstrap.md. We bypass d3dx8 here because
 * it's deprecated and not in nixpkgs; pixel-identical loader behavior
 * (including .bmp green-key) is a later milestone.
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

/* Returns 1 on success, 0 on failure. Reads the whole file into memory.
 * On failure *img is left untouched. */
int  tga_load_file(const char *path, tga_image *img);
void tga_free(tga_image *img);

#endif
