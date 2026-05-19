/*
 * Bare-minimum textured-quad path for OpenRecet — enough to render a
 * single sprite over the back buffer. Not engine-accurate yet (the
 * original uses d3dx8's sprite helper); good enough as a first visible
 * milestone confirming texture upload + alpha blending work.
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

void sprite_destroy(sprite_t *s);

/* Draw the sprite at screen-space (x, y) at native pixel size, with
 * SRCALPHA/INVSRCALPHA blending. Caller must be inside BeginScene. */
void sprite_draw(IDirect3DDevice8 *dev, const sprite_t *s, float x, float y);

#endif
