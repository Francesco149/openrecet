/*
 * render_quad.h — 2D textured-quad batching for OpenRecet.
 *
 * Mirrors the engine's primitive draw path:
 *   FUN_00404efc — append one quad into a static vertex buffer
 *                  (VA &DAT_00605208 in the original).
 *   FUN_00405354 — flush via DrawPrimitiveUP(TRIANGLELIST).
 *   FUN_0049b425 — set the 2D render states (alpha blend, modulate,
 *                  linear filter) before any 2D draw work.
 *   FUN_00404e44 — one-shot vbuf initializer: z=0, rhw=1, specular=0
 *                  on all 8544 slots.
 *
 * Two-layer split (matches src/input.c / src/tick.c convention):
 *   - Top of file: pure-C math + vbuf writer + accessors. Compiles
 *     on Linux for the ASan unit-test build.
 *   - Bottom (#ifdef _WIN32): D3D-touching wrappers (state setup,
 *     flush).
 */
#ifndef OPENRECET_RENDER_QUAD_H
#define OPENRECET_RENDER_QUAD_H

#include <stdint.h>

/* 8544 vertices total — engine's buffer extent is 0x605208..0x647e14
 * (273408 bytes / 32-byte stride = 8544). At 6 vertices per quad
 * (two CCW triangles) that's 1424 quads per flush window. */
#define RENDER_QUAD_MAX_VERTICES 8544

/* Engine D3D8 vertex format used by the flush:
 *   D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1
 *   = 0x004 | 0x040 | 0x080 | 0x100 = 0x1c4
 * 32-byte stride exactly matches.  */
#define RENDER_QUAD_FVF 0x1c4u

/* Layout of one vertex in the buffer (FVF 0x1c4). */
typedef struct {
    float    x, y, z, rhw;   /* offset  0..15 */
    uint32_t diffuse;        /* offset 16..19 */
    uint32_t specular;       /* offset 20..23 */
    float    u, v;           /* offset 24..31 */
} render_quad_vtx_t;

/* Pure-C entry: initialize the vbuf (z=0, rhw=1, specular=0 on all
 * vertex slots) AND configure the screen-width-to-640 scale factor
 * applied to dst rect widths/heights.  Called once after D3D init.
 *
 * `screen_w` corresponds to DAT_005cbc04 in the engine, set by recet
 * .ini's `screen` value (640/800/1024/1280). 0 is treated as 640. */
void render_quad_init(uint32_t screen_w);

/* Screen-shake / global pixel offset, mirrors DAT_0438cc18/1c.
 * Added to every dst top-left at quad-add time. Both default to 0. */
void render_quad_set_offset(float ox, float oy);

/* Append one quad to the static buffer.
 *
 *   dst[4] = {dst_x, dst_y, dst_w, dst_h}  in 640-relative pixels;
 *            x/y are integer-truncated, w/h are scaled by
 *            (screen_w / 640.0f).
 *   src[4] = {left_x, top_y, right_x, bottom_y} in source texture
 *            pixel coordinates. Note the asymmetric conventions —
 *            dst is xywh, src is xyxy — matching the engine.
 *   tex_w, tex_h = native texture dimensions (from sprite_t).
 *   diffuse     = D3DCOLOR (0xAARRGGBB).
 *
 * Returns 1 on success, 0 if the buffer is full (engine has no
 * bounds check; we add one so a runaway frame doesn't trash memory).
 */
int render_quad_add(const float dst[4], const float src[4],
                    uint32_t tex_w, uint32_t tex_h,
                    uint32_t diffuse);

/* Total vertex count currently pending in the buffer. */
uint32_t render_quad_vertex_count(void);

/* Read access to the in-progress buffer for tests / debug. */
const render_quad_vtx_t *render_quad_buffer(void);

/* Reset the vertex counter without flushing. Tests use this; engine
 * code reaches the same end-state via flush() which zeroes after the
 * DrawPrimitiveUP call. */
void render_quad_reset(void);

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "sprite.h"

/* Bind a sprite to texture stage 0. Caller-managed; quads added after
 * this point sample from `s` until the next bind. */
void render_quad_bind(IDirect3DDevice8 *dev, const sprite_t *s);

/* FUN_0049b425 — set the 2D render-state preset. Call once per frame
 * before any 2D draws. (Engine duplicates the SRCBLEND/DESTBLEND
 * pair — we replicate that no-op for fidelity.) */
void render_quad_state_setup(IDirect3DDevice8 *dev);

/* FUN_00405354 — flush. SetVertexShader(FVF 0x1c4) +
 * DrawPrimitiveUP(TRIANGLELIST, count/3, vbuf, stride=32), then
 * clear the counter. No-op if no vertices pending. */
void render_quad_flush(IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_RENDER_QUAD_H */
