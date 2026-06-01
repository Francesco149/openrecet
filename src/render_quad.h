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

/* Append one HORIZONTALLY-MIRRORED quad (FUN_00404e61). Identical to
 * render_quad_add but the source left/right edges are swapped, flipping the
 * sampled texture about its vertical axis (the engine's left-facing-vs-right-
 * facing sprite path). Same dst/tex/diffuse conventions as render_quad_add. */
int render_quad_add_mirrored(const float dst[4], const float src[4],
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

/* Pure-C entry: fill vertex slots 0..3 in the static vbuf with the
 * rotated-quad geometry of FUN_004063c7. Writes ONLY x, y, diffuse,
 * u, v fields (z/rhw/specular were pre-set by render_quad_init). Does
 * NOT touch the vertex counter or issue a D3D draw. Exposed for the
 * Linux unit-test build; the Win32 render_quad_draw_rotated below
 * wraps this and adds the SetVertexShader + DrawPrimitiveUP + counter-
 * reset that match the engine call site. */
void render_quad_fill_rotated_vbuf(float center_x, float center_y,
                                   float half_size, float rotation_rad,
                                   const float uv[4], uint32_t diffuse);

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

/* FUN_004063c7 — self-contained rotated-quad draw. Writes exactly 4
 * vertices into the static vbuf at indices 0..3 (CLOBBERING anything
 * batched there), then DrawPrimitiveUP as TRIANGLESTRIP (2 triangles)
 * with the texture currently bound to stage 0.
 *
 * IMPORTANT: caller must flush any pending batch before calling.
 * The engine relies on FUN_00405354 having been invoked immediately
 * before (see FUN_00453147 — the Now Loading overlay). On exit the
 * vertex counter is reset to 0.
 *
 *   (center_x, center_y) — quad centre in 640-relative pixels.
 *   half_size            — half-edge length of the (unrotated) quad,
 *                          640-relative pixels. The diagonal radius
 *                          is sqrt(2) * half_size.
 *   rotation_rad         — extra rotation around the centre, on top
 *                          of the engine's baked-in π/4 corner offset.
 *   uv[4]                — (u0, v0, u1, v1) sampling rectangle in
 *                          normalised texture coords (0..1). Engine
 *                          encodes uv in the same order via param_2
 *                          double-fetches.
 *   diffuse              — D3DCOLOR (0xAARRGGBB).
 *
 * The X/Y position math matches the engine bit-for-bit:
 *   screen_x = ((-sin(angle) * radius) + center_x) * screen_w / 640
 *   screen_y = ((-cos(angle) * radius) + center_y) * screen_w / 640
 * with `angle = (i/4)*2π + rotation + π/4` for the four corners.
 */
void render_quad_draw_rotated(IDirect3DDevice8 *dev,
                              float center_x, float center_y,
                              float half_size, float rotation_rad,
                              const float uv[4], uint32_t diffuse);

#endif /* _WIN32 */

#endif /* OPENRECET_RENDER_QUAD_H */
