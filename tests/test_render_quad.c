/*
 * test_render_quad.c — pure-C tests for the 2D quad batcher.
 *
 * Covers FUN_00404efc (add) + FUN_00404e44 (vbuf init). The D3D-side
 * wrappers (flush, state setup) are Win32-only and not exercised here.
 *
 * Verified properties:
 *   - vbuf init seeds z=0, rhw=1, specular=0 across all 8544 slots
 *   - one add() writes exactly 6 vertices in the engine's BR/BL/TR
 *     BL/TL/TR ordering
 *   - dst is xywh, src is xyxy (asymmetric convention reproduced)
 *   - dst width/height scale by (screen_w / 640), top-left does not
 *   - top/left UVs get +0.5 half-texel inset; bottom/right do not
 *   - screen-shake offset added to top-left
 *   - dst top-left is truncated to integer (engine __ftol pattern)
 *   - overflow guard returns 0 once the 8544-vertex buffer fills
 *   - tex_w/tex_h == 0 returns 0
 *
 * No D3D headers pulled in — render_quad.h gates the Win32 surface
 * behind `#ifdef _WIN32`.
 */
#include "t.h"
#include "render_quad.h"

#include <math.h>
#include <string.h>

/* Float-compare helper. Quad math is integer-grid-friendly; UVs
 * involve a divide so use a small epsilon. */
static int feq(float a, float b)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d < 1e-4f;
}

#define T_ASSERT_FEQ(a, b) do { \
    if (!feq((a), (b))) \
        T_FAIL("expected %s == %s (got %.6f, want %.6f)", \
               #a, #b, (double)(a), (double)(b)); \
} while (0)

/* ─── init / vbuf prefill ────────────────────────────────────────────── */

int test_render_quad_init_seeds_z_rhw_specular(void)
{
    render_quad_init(640);

    const render_quad_vtx_t *vb = render_quad_buffer();
    /* Spot-check first, last, and a middle slot — the init loop must
     * touch all 8544 entries. */
    const uint32_t spots[] = { 0, 1, 4271, 8542, 8543 };
    for (size_t i = 0; i < sizeof spots / sizeof spots[0]; i++) {
        uint32_t k = spots[i];
        T_ASSERT_FEQ(vb[k].z,   0.0f);
        T_ASSERT_FEQ(vb[k].rhw, 1.0f);
        T_ASSERT_EQ_U(vb[k].specular, 0u);
    }
    T_ASSERT_EQ_U(render_quad_vertex_count(), 0u);
    return 0;
}

/* ─── one add → 6 vertices ───────────────────────────────────────────── */

int test_render_quad_add_one_emits_six_vertices(void)
{
    render_quad_init(640);
    /* dst at top-left 100,200, size 64x32 in 640-relative space.
     * src is the entire 64x32 region of a 64x32 texture (left=0, top=0,
     * right=64, bottom=32). */
    const float dst[4] = { 100.0f, 200.0f,  64.0f,  32.0f };
    const float src[4] = {   0.0f,   0.0f,  64.0f,  32.0f };
    T_ASSERT_EQ_I(render_quad_add(dst, src, 64, 32, 0xFF112233), 1);
    T_ASSERT_EQ_U(render_quad_vertex_count(), 6u);

    const render_quad_vtx_t *v = render_quad_buffer();
    /* Engine ordering: 0:BR 1:BL 2:TR | 3:BL 4:TL 5:TR */
    T_ASSERT_FEQ(v[0].x, 164.0f); T_ASSERT_FEQ(v[0].y, 232.0f); /* BR */
    T_ASSERT_FEQ(v[1].x, 100.0f); T_ASSERT_FEQ(v[1].y, 232.0f); /* BL */
    T_ASSERT_FEQ(v[2].x, 164.0f); T_ASSERT_FEQ(v[2].y, 200.0f); /* TR */
    T_ASSERT_FEQ(v[3].x, 100.0f); T_ASSERT_FEQ(v[3].y, 232.0f); /* BL */
    T_ASSERT_FEQ(v[4].x, 100.0f); T_ASSERT_FEQ(v[4].y, 200.0f); /* TL */
    T_ASSERT_FEQ(v[5].x, 164.0f); T_ASSERT_FEQ(v[5].y, 200.0f); /* TR */

    for (int i = 0; i < 6; i++) {
        T_ASSERT_EQ_U(v[i].diffuse, 0xFF112233u);
        T_ASSERT_FEQ(v[i].z,        0.0f);
        T_ASSERT_FEQ(v[i].rhw,      1.0f);
        T_ASSERT_EQ_U(v[i].specular, 0u);
    }
    return 0;
}

/* ─── UV math (half-texel inset on top/left only) ────────────────────── */

int test_render_quad_uv_half_texel_inset_asymmetry(void)
{
    render_quad_init(640);
    /* 1024x1024 texture; sample [256..768) x [0..512) — full pixels. */
    const float dst[4] = { 0, 0, 100, 100 };
    const float src[4] = { 256.0f, 0.0f, 768.0f, 512.0f };
    render_quad_add(dst, src, 1024, 1024, 0xFFFFFFFF);

    const render_quad_vtx_t *v = render_quad_buffer();
    /* left  u = (256 + 0.5) / 1024 = 0.2504883
     * right u = 768 / 1024       = 0.75
     * top    v = (0   + 0.5) / 1024 = 0.000488
     * bottom v = 512 / 1024       = 0.5
     */
    const float left_u   = 256.5f / 1024.0f;
    const float right_u  = 768.0f / 1024.0f;
    const float top_v    =   0.5f / 1024.0f;
    const float bottom_v = 512.0f / 1024.0f;

    /* v0 = BR, v1 = BL, v2 = TR, v3 = BL, v4 = TL, v5 = TR */
    T_ASSERT_FEQ(v[0].u, right_u); T_ASSERT_FEQ(v[0].v, bottom_v); /* BR */
    T_ASSERT_FEQ(v[1].u, left_u ); T_ASSERT_FEQ(v[1].v, bottom_v); /* BL */
    T_ASSERT_FEQ(v[2].u, right_u); T_ASSERT_FEQ(v[2].v, top_v   ); /* TR */
    T_ASSERT_FEQ(v[3].u, left_u ); T_ASSERT_FEQ(v[3].v, bottom_v); /* BL */
    T_ASSERT_FEQ(v[4].u, left_u ); T_ASSERT_FEQ(v[4].v, top_v   ); /* TL */
    T_ASSERT_FEQ(v[5].u, right_u); T_ASSERT_FEQ(v[5].v, top_v   ); /* TR */
    return 0;
}

/* ─── resolution scaling — all four dst components scale ─────────────── */

int test_render_quad_scale_widens_and_offsets(void)
{
    /* 1024-wide screen → scale 1024/640 = 1.6. */
    render_quad_init(1024);
    const float dst[4] = { 100.0f, 200.0f, 64.0f, 32.0f };
    const float src[4] = {   0.0f,   0.0f, 64.0f, 32.0f };
    render_quad_add(dst, src, 64, 32, 0xFFFFFFFF);

    const render_quad_vtx_t *v = render_quad_buffer();
    /* All four dst components scale by 1.6, with the top-left
     * components additionally truncated to int.
     *   TL.x = trunc(100 * 1.6) = trunc(160.0) = 160
     *   TL.y = trunc(200 * 1.6) = trunc(320.0) = 320
     *   width  =  64 * 1.6 = 102.4 → BR.x = 160 + 102.4 = 262.4
     *   height =  32 * 1.6 =  51.2 → BR.y = 320 +  51.2 = 371.2
     */
    T_ASSERT_FEQ(v[4].x, 160.0f);              /* TL x */
    T_ASSERT_FEQ(v[4].y, 320.0f);              /* TL y */
    T_ASSERT_FEQ(v[0].x, 160.0f + 102.4f);     /* BR x */
    T_ASSERT_FEQ(v[0].y, 320.0f +  51.2f);     /* BR y */
    return 0;
}

/* ─── screen-shake offset ───────────────────────────────────────────── */

int test_render_quad_offset_shifts_top_left(void)
{
    render_quad_init(640);   /* scale = 1.0 — keeps the offset math obvious */
    render_quad_set_offset(7.0f, -3.0f);
    const float dst[4] = { 100.0f, 200.0f, 64.0f, 32.0f };
    const float src[4] = {   0.0f,   0.0f, 64.0f, 32.0f };
    render_quad_add(dst, src, 64, 32, 0xFFFFFFFF);

    const render_quad_vtx_t *v = render_quad_buffer();
    T_ASSERT_FEQ(v[4].x, 107.0f);   /* TL: 100 + 7 */
    T_ASSERT_FEQ(v[4].y, 197.0f);   /* TL: 200 - 3 */
    T_ASSERT_FEQ(v[0].x, 171.0f);   /* BR: 107 + 64 (scale 1.0) */
    T_ASSERT_FEQ(v[0].y, 229.0f);   /* BR: 197 + 32 */
    return 0;
}

/* ─── integer truncation of dst top-left ─────────────────────────────── */

int test_render_quad_top_left_truncated_to_int(void)
{
    render_quad_init(640);
    /* 100.9 should truncate to 100 (engine uses __ftol which truncates
     * toward zero with the default FPU rounding mode set elsewhere —
     * but engine setup leaves it at "round toward nearest", and __ftol
     * is a runtime helper that's literally "truncate to int". We match
     * with C's (int) cast.) */
    const float dst[4] = { 100.9f, 200.9f, 64.0f, 32.0f };
    const float src[4] = {   0.0f,   0.0f, 64.0f, 32.0f };
    render_quad_add(dst, src, 64, 32, 0xFFFFFFFF);

    const render_quad_vtx_t *v = render_quad_buffer();
    T_ASSERT_FEQ(v[4].x, 100.0f);   /* TL: trunc(100.9) */
    T_ASSERT_FEQ(v[4].y, 200.0f);   /* TL: trunc(200.9) */
    /* Width is the unrounded float * scale, so right = 100 + 64 = 164. */
    T_ASSERT_FEQ(v[0].x, 164.0f);
    return 0;
}

/* ─── buffer-fill bounds check ───────────────────────────────────────── */

int test_render_quad_returns_zero_when_full(void)
{
    render_quad_init(640);
    const float dst[4] = { 0, 0, 16, 16 };
    const float src[4] = { 0, 0, 16, 16 };
    /* 8544 / 6 = 1424 quads fit. */
    for (int i = 0; i < 1424; i++) {
        if (!render_quad_add(dst, src, 16, 16, 0xFFFFFFFF))
            T_FAIL("add #%d unexpectedly failed before buffer full", i);
    }
    T_ASSERT_EQ_U(render_quad_vertex_count(), 8544u);

    /* 1425th add must fail and not advance the count. */
    T_ASSERT_EQ_I(render_quad_add(dst, src, 16, 16, 0xFFFFFFFF), 0);
    T_ASSERT_EQ_U(render_quad_vertex_count(), 8544u);
    return 0;
}

int test_render_quad_rejects_zero_tex_dim(void)
{
    render_quad_init(640);
    const float dst[4] = { 0, 0, 16, 16 };
    const float src[4] = { 0, 0, 16, 16 };
    T_ASSERT_EQ_I(render_quad_add(dst, src,  0, 16, 0xFFFFFFFF), 0);
    T_ASSERT_EQ_I(render_quad_add(dst, src, 16,  0, 0xFFFFFFFF), 0);
    T_ASSERT_EQ_U(render_quad_vertex_count(), 0u);
    return 0;
}

/* ─── reset clears the counter without disturbing prefill ────────────── */

int test_render_quad_reset_keeps_z_rhw_prefill(void)
{
    render_quad_init(640);
    const float dst[4] = { 0, 0, 16, 16 };
    const float src[4] = { 0, 0, 16, 16 };
    render_quad_add(dst, src, 16, 16, 0xAABBCCDD);
    T_ASSERT_EQ_U(render_quad_vertex_count(), 6u);

    render_quad_reset();
    T_ASSERT_EQ_U(render_quad_vertex_count(), 0u);

    /* z/rhw/specular must still be intact at the slot we touched. */
    const render_quad_vtx_t *vb = render_quad_buffer();
    for (int i = 0; i < 6; i++) {
        T_ASSERT_FEQ(vb[i].z,   0.0f);
        T_ASSERT_FEQ(vb[i].rhw, 1.0f);
        T_ASSERT_EQ_U(vb[i].specular, 0u);
    }
    return 0;
}

/* ─── default screen_w == 0 falls back to 640 ────────────────────────── */

int test_render_quad_init_zero_screen_w_defaults_640(void)
{
    render_quad_init(0);  /* should equal init(640) */
    const float dst[4] = { 0, 0, 100, 100 };
    const float src[4] = { 0, 0, 100, 100 };
    render_quad_add(dst, src, 100, 100, 0xFFFFFFFF);
    const render_quad_vtx_t *v = render_quad_buffer();
    /* Right edge should be 100 (no scaling) — confirms scale == 1.0. */
    T_ASSERT_FEQ(v[0].x, 100.0f);
    return 0;
}
