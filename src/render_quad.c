/*
 * render_quad.c — 2D textured-quad batching.
 *
 * Engine source: FUN_00404efc (add), FUN_00405354 (flush),
 * FUN_0049b425 (state setup), FUN_00404e44 (vbuf init).
 *
 * Vertex layout (FVF 0x1c4, 32 bytes / vertex):
 *   off  0  float    x
 *   off  4  float    y
 *   off  8  float    z        (pre-init to 0.0 once; never rewritten)
 *   off 12  float    rhw      (pre-init to 1.0 once; never rewritten)
 *   off 16  D3DCOLOR diffuse
 *   off 20  D3DCOLOR specular (pre-init to 0; never rewritten)
 *   off 24  float    u
 *   off 28  float    v
 *
 * Two CCW triangles per quad, six vertices:
 *   0: BR   1: BL   2: TR     (triangle 1: BR, BL, TR)
 *   3: BL   4: TL   5: TR     (triangle 2: BL, TL, TR)
 *
 * Asymmetric input conventions (engine, faithfully reproduced):
 *   dst is (x, y, w, h) — top-left + size
 *   src is (left, top, right, bottom) — pixel coords in the texture
 *
 * Screen-resolution scaling: dst widths/heights are multiplied by
 * (screen_w / 640.0). dst top-left x/y are NOT scaled (engine quirk
 * — UI coords stay in 640-relative space, only sprite size grows).
 *
 * UV computation has a half-texel inset on the top/left edges and
 * NO inset on the bottom/right (engine quirk — see the (x-0.5+0.5)
 * pattern in the original decomp at FUN_00404efc). Reproduced.
 */

#include "render_quad.h"

#include <math.h>
#include <string.h>

/* ─── state ──────────────────────────────────────────────────────────── */

static render_quad_vtx_t g_vbuf[RENDER_QUAD_MAX_VERTICES];  /* DAT_00605208 */
static uint32_t          g_vcount = 0;                      /* DAT_00647e0c */
static float             g_screen_w = 640.0f;               /* DAT_005cbc04 (as float) */
static float             g_offset_x = 0.0f;                 /* DAT_0438cc18 */
static float             g_offset_y = 0.0f;                 /* DAT_0438cc1c */

/* ─── init ───────────────────────────────────────────────────────────── */

void render_quad_init(uint32_t screen_w)
{
    g_screen_w = (screen_w == 0) ? 640.0f : (float)screen_w;

    /* FUN_00404e44 — pre-init z/rhw/specular on all vertex slots.
     * After this point render_quad_add never touches these fields. */
    for (uint32_t i = 0; i < RENDER_QUAD_MAX_VERTICES; i++) {
        g_vbuf[i].z        = 0.0f;
        g_vbuf[i].rhw      = 1.0f;
        g_vbuf[i].specular = 0;
    }
    g_vcount   = 0;
    g_offset_x = 0.0f;
    g_offset_y = 0.0f;
}

void render_quad_set_offset(float ox, float oy)
{
    g_offset_x = ox;
    g_offset_y = oy;
}

uint32_t render_quad_vertex_count(void)
{
    return g_vcount;
}

const render_quad_vtx_t *render_quad_buffer(void)
{
    return g_vbuf;
}

void render_quad_reset(void)
{
    g_vcount = 0;
}

/* ─── rotated-quad vertex fill (FUN_004063c7 inner loop) ─────────────── */

void render_quad_fill_rotated_vbuf(float center_x, float center_y,
                                   float half_size, float rotation_rad,
                                   const float uv[4], uint32_t diffuse)
{
    /* Diagonal radius — engine uses `sqrt(half_size * half_size +
     * half_size * half_size)` = half_size * sqrt(2). */
    const float radius = sqrtf(2.0f * half_size * half_size);

    /* Vertex iteration order [0, 1, 3, 2] mirrors the engine's
     * piVar5[] table (0x4063c7 L23-26). This sequence lays out the
     * four corners in the correct rotational order for a CCW-wound
     * TRIANGLESTRIP. */
    static const int corner_index[4] = { 0, 1, 3, 2 };

    for (int k = 0; k < 4; k++) {
        const float frac  = (float)corner_index[k] / 4.0f;
        const float angle = frac * 6.2831855f + rotation_rad + 0.7853982f;
        const float s     = sinf(angle);
        const float c     = cosf(angle);
        const float x_off = -(s * radius);
        const float y_off = -(c * radius);

        g_vbuf[k].x       = ((x_off + center_x) * g_screen_w) / 640.0f;
        g_vbuf[k].y       = ((y_off + center_y) * g_screen_w) / 640.0f;
        g_vbuf[k].diffuse = diffuse;
    }

    /* UV writes — engine writes these to vertices 0..3 at hardcoded
     * VAs DAT_00605220 / 240 / 260 / 280. */
    g_vbuf[0].u = uv[0]; g_vbuf[0].v = uv[1];
    g_vbuf[1].u = uv[0]; g_vbuf[1].v = uv[3];
    g_vbuf[2].u = uv[2]; g_vbuf[2].v = uv[1];
    g_vbuf[3].u = uv[2]; g_vbuf[3].v = uv[3];
}

/* ─── add (FUN_00404efc) ─────────────────────────────────────────────── */

int render_quad_add(const float dst[4], const float src[4],
                    uint32_t tex_w, uint32_t tex_h,
                    uint32_t diffuse)
{
    if (g_vcount + 6 > RENDER_QUAD_MAX_VERTICES) {
        /* Engine has no bounds check (it would corrupt memory past
         * DAT_00647e14). We bail so a runaway frame is recoverable. */
        return 0;
    }
    if (tex_w == 0 || tex_h == 0) {
        return 0;
    }

    /* Scale ALL FOUR dst components by screen-width / 640 (positions
     * too — not just sizes). Top-left is then truncated to integer
     * pixel after scaling, matching the engine's __ftol pattern.
     * Two of those scale multiplications are hidden inside Ghidra's
     * `__ftol` artifact in FUN_00404efc; recovered by comparison
     * against the stock title-screen layout at 1024x768. */
    const float scale = g_screen_w / 640.0f;
    const float dst_w = scale * dst[2];
    const float dst_h = scale * dst[3];

    const float left   = g_offset_x + (float)(int)(scale * dst[0]);
    const float top    = g_offset_y + (float)(int)(scale * dst[1]);
    const float right  = left + dst_w;
    const float bottom = top  + dst_h;

    /* UVs: top/left get the +0.5 half-texel inset, right/bottom don't.
     * (Yes, asymmetric. Yes, engine quirk. Yes, intentional in the
     * port — see file header comment.) */
    const float u_left   = (src[0] + 0.5f) / (float)tex_w;
    const float v_top    = (src[1] + 0.5f) / (float)tex_h;
    const float u_right  =  src[2]         / (float)tex_w;
    const float v_bottom =  src[3]         / (float)tex_h;

    render_quad_vtx_t *v = &g_vbuf[g_vcount];

    /* Triangle 1 — BR, BL, TR */
    v[0].x = right;  v[0].y = bottom; v[0].diffuse = diffuse; v[0].u = u_right; v[0].v = v_bottom;
    v[1].x = left;   v[1].y = bottom; v[1].diffuse = diffuse; v[1].u = u_left;  v[1].v = v_bottom;
    v[2].x = right;  v[2].y = top;    v[2].diffuse = diffuse; v[2].u = u_right; v[2].v = v_top;

    /* Triangle 2 — BL, TL, TR */
    v[3].x = left;   v[3].y = bottom; v[3].diffuse = diffuse; v[3].u = u_left;  v[3].v = v_bottom;
    v[4].x = left;   v[4].y = top;    v[4].diffuse = diffuse; v[4].u = u_left;  v[4].v = v_top;
    v[5].x = right;  v[5].y = top;    v[5].diffuse = diffuse; v[5].u = u_right; v[5].v = v_top;

    g_vcount += 6;
    return 1;
}

/* ─── D3D wrappers (Win32 only) ──────────────────────────────────────── */

#ifdef _WIN32

void render_quad_bind(IDirect3DDevice8 *dev, const sprite_t *s)
{
    IDirect3DDevice8_SetTexture(
        dev, 0, s && s->tex ? (IDirect3DBaseTexture8 *)s->tex : NULL);
}

void render_quad_state_setup(IDirect3DDevice8 *dev)
{
    /* SetVertexShader(0x142 = XYZ|DIFFUSE|TEX1). Note: this FVF is
     * for the engine's 3D-quad path; the 2D flush will override it
     * with 0x1c4. The engine sets it here defensively. */
    IDirect3DDevice8_SetVertexShader(dev, 0x142);

    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE,        FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    /* Engine quirk: SRCBLEND/DESTBLEND set twice — reproduced. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);

    /* Order matches the engine: ALPHAOP first, then COLOROP, then
     * MIN/MAGFILTER. Engine does NOT set COLORARG1/2 or ALPHAARG1/2
     * — it relies on the D3D8 defaults (TEXTURE, CURRENT). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
}

void render_quad_flush(IDirect3DDevice8 *dev)
{
    if (g_vcount == 0) return;

    IDirect3DDevice8_SetVertexShader(dev, RENDER_QUAD_FVF);
    IDirect3DDevice8_DrawPrimitiveUP(
        dev, D3DPT_TRIANGLELIST, g_vcount / 3,
        g_vbuf, sizeof(render_quad_vtx_t));
    g_vcount = 0;
}

void render_quad_draw_rotated(IDirect3DDevice8 *dev,
                              float center_x, float center_y,
                              float half_size, float rotation_rad,
                              const float uv[4], uint32_t diffuse)
{
    /* Engine FUN_004063c7. Fill vertex slots 0..3, draw a 2-triangle
     * strip, reset counter.  Pure-C math lives in
     * render_quad_fill_rotated_vbuf() so it can be unit-tested without
     * D3D. */
    IDirect3DDevice8_SetVertexShader(dev, RENDER_QUAD_FVF);
    render_quad_fill_rotated_vbuf(center_x, center_y,
                                  half_size, rotation_rad, uv, diffuse);
    IDirect3DDevice8_DrawPrimitiveUP(
        dev, D3DPT_TRIANGLESTRIP, 2,
        g_vbuf, sizeof(render_quad_vtx_t));
    g_vcount = 0;
}

#endif /* _WIN32 */
