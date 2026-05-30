/*
 * Sprite path for OpenRecet — texture upload + textured-quad draw.
 *
 * Two entry points:
 *   - sprite_create(): low-level, takes raw BGRA pixels you already
 *     decoded (e.g. one of the test harnesses).
 *   - sprite_load():   engine-accurate, mirrors FUN_0047193c. Takes a
 *     filename, tries it on disk first, falls back to the storage
 *     overlay (bmpdata) like the original. Auto-detects BMP vs TGA
 *     from the buffer's magic, applies the engine's BMP green color
 *     key, decodes via src/bmp.c or src/tga.c, then uploads.
 *
 * Not yet engine-accurate: D3DX-style resampling to (expected_w,
 * expected_h) is skipped — every audited asset ships at native
 * resolution. The expected_w/expected_h args are still stored on the
 * sprite for future fan-out (matches the original's slot layout).
 */
#ifndef OPENRECET_SPRITE_H
#define OPENRECET_SPRITE_H

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>
#include <stdint.h>

typedef struct {
    IDirect3DTexture8 *tex;
    uint32_t           width;
    uint32_t           height;
} sprite_t;

/* Uploads BGRA (D3DFMT_A8R8G8B8) pixels into a managed-pool texture.
 * Returns 1 on success. On failure the sprite_t is left zeroed. */
int  sprite_create(IDirect3DDevice8 *dev,
                   const uint8_t *bgra, uint32_t w, uint32_t h,
                   sprite_t *out);

/* Engine-style load — mirrors FUN_0047193c.
 *
 *   1. Try fopen(name, "rb").
 *   2. If that fails, fall back to storage_read(name, ...) (bmpdata
 *      overlay, eventually lnkdatas too).
 *   3. Sniff the buffer: 'BM' → BMP with green color key (0x00FF00);
 *      otherwise → TGA (uncompressed or RLE).
 *   4. Upload via sprite_create.
 *
 * `expected_w`/`expected_h` are stored on the sprite for future fan-
 * out; we don't yet resample to match them. Returns 1 on success;
 * on failure the sprite_t is left zeroed. */
int  sprite_load(IDirect3DDevice8 *dev, const char *name,
                 uint32_t expected_w, uint32_t expected_h,
                 sprite_t *out);

/* Same as sprite_load, but generates a full box-filtered mip chain
 * (Levels=0). Mirrors the engine's MESH texture loader FUN_00471b24,
 * which calls D3DXCreateTextureFromFileInMemoryEx(MipLevels=0,
 * MipFilter=D3DX_DEFAULT) — vs the 2D UI loader FUN_0047193c, which
 * passes MipLevels=1 (no mips). Minified 3D meshes (book, table, the
 * back-room blinds) sample the smoothed mips in retail; without them
 * the port read visibly sharper at distance. See engine-quirks §54 and
 * docs/findings/texture-loader.md. */
int  sprite_load_mipped(IDirect3DDevice8 *dev, const char *name,
                        uint32_t expected_w, uint32_t expected_h,
                        sprite_t *out);

void sprite_destroy(sprite_t *s);

/* Draw the sprite at screen-space (x, y) at native pixel size, with
 * SRCALPHA/INVSRCALPHA blending. Caller must be inside BeginScene. */
void sprite_draw(IDirect3DDevice8 *dev, const sprite_t *s, float x, float y);

#endif
