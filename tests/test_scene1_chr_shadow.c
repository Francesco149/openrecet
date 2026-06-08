/*
 * test_scene1_chr_shadow.c — Csh.1 ground-shadow builder (FUN_0045aa36 Block A).
 *
 * Exercises the pure per-actor core chr_shadow_build_actor: the gating, the
 * alpha/size algebra, the colour packing, and the world-matrix placement
 * (object origin → world ≈ (px, floor+0.12, pz) on a flat floor).
 */
#include "t.h"

#include <math.h>

#include "scene1_chr_shadow.h"

static int near_(float a, float b, float tol) { return fabsf(a - b) <= tol; }
#define NEAR(a, b, tol) do { \
    if (!near_((a), (b), (tol))) \
        T_FAIL("expected %s≈%g, got %g", #a, (double)(b), (double)(a)); \
} while (0)

/* Row-vector transform: out = v * M (row-major M[16]). */
static void xform(const float M[16], const float v[4], float out[4])
{
    for (int j = 0; j < 4; j++)
        out[j] = v[0]*M[0*4+j] + v[1]*M[1*4+j] + v[2]*M[2*4+j] + v[3]*M[3*4+j];
}

static const float FLAT_N[3] = { 0.0f, 1.0f, 0.0f };

/* Grounded player (i=0): height 0 → alpha 0 → opaque-black diffuse (the shadow
 * darkness comes from the texture under the multiplicative blend). */
int test_chr_shadow_player_grounded(void)
{
    float pos[3] = { -0.30f, 0.0f, 9.35f };
    chr_shadow_params p;
    chr_shadow_build_actor(0, pos, 1.0f, 1.0f, /*alive*/1,
                           /*hit*/1, /*floor*/0.0f, FLAT_N, &p);
    if (!p.draw) T_FAIL("grounded player should draw");
    if (p.color != 0xff000000u) T_FAIL("height-0 colour = %08x, want ff000000", p.color);

    /* object origin maps to (px, floor+0.12 + 0.2*size, pz); size≈0.0053 so
     * the 0.2*size lift is ~0.001 — Y lands ~floor+0.12. */
    float o[4] = { 0, 0, 0, 1 }, w[4];
    xform(p.world, o, w);
    NEAR(w[0], -0.30f, 1e-3f);
    NEAR(w[2],  9.35f, 1e-3f);
    NEAR(w[1],  0.12f, 2e-3f);
    return 0;
}

/* Companion (i=2) floats: height>0 → alpha = (int)(h*5)+0x40, size ×0.9. */
int test_chr_shadow_companion_alpha(void)
{
    float pos[3] = { 0.60f, 2.80f, 9.35f };
    chr_shadow_params p;
    chr_shadow_build_actor(2, pos, 1.0f, 1.0f, 1, 1, 0.0f, FLAT_N, &p);
    if (!p.draw) T_FAIL("companion should draw");
    /* alpha = (int)(2.8*5)=14, +0x40 = 78 = 0x4e → 0xff4e4e4e */
    if (p.color != 0xff4e4e4eu) T_FAIL("companion colour = %08x, want ff4e4e4e", p.color);
    return 0;
}

/* alpha clamps to 255: a very tall height saturates. */
int test_chr_shadow_alpha_clamps(void)
{
    float pos[3] = { 0.0f, 100.0f, 0.0f };
    chr_shadow_params p;
    chr_shadow_build_actor(0, pos, 1.0f, 1.0f, 1, 1, 0.0f, FLAT_N, &p);
    /* (int)(100*5)=500 → clamps 255; size = clamp(0.038-100*0.0015, .025)= .025 */
    if ((p.color & 0xffu) != 0xffu) T_FAIL("alpha should clamp to 0xff, got %08x", p.color);
    return 0;
}

/* Gating: not-alive, no floor hit, steep normal, zero scale → no draw. */
int test_chr_shadow_gates(void)
{
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    chr_shadow_params p;

    chr_shadow_build_actor(0, pos, 1.0f, 1.0f, /*alive*/0, 1, 0.0f, FLAT_N, &p);
    if (p.draw) T_FAIL("dead actor must not draw");

    chr_shadow_build_actor(0, pos, 1.0f, 1.0f, 1, /*hit*/0, -10000.0f, FLAT_N, &p);
    if (p.draw) T_FAIL("no floor hit must not draw");

    float steep[3] = { 0.99f, 0.10f, 0.0f };   /* |n.y|=0.1 < 0.7 */
    chr_shadow_build_actor(0, pos, 1.0f, 1.0f, 1, 1, 0.0f, steep, &p);
    if (p.draw) T_FAIL("steep floor (|n.y|<0.7) must not draw");

    chr_shadow_build_actor(0, pos, 0.0f, 1.0f, 1, 1, 0.0f, FLAT_N, &p);
    if (p.draw) T_FAIL("zero scale_xz must not draw");
    chr_shadow_build_actor(0, pos, 1.0f, 0.0f, 1, 1, 0.0f, FLAT_N, &p);
    if (p.draw) T_FAIL("zero scale_y must not draw");
    return 0;
}

/* |n.y| >= 0.7 exactly passes; the negative-normal abs is honoured. */
int test_chr_shadow_normal_abs_threshold(void)
{
    float pos[3] = { 0.0f, 1.0f, 0.0f };
    chr_shadow_params p;
    float n[3] = { 0.0f, -0.7f, 0.0f };        /* |n.y| = 0.7 → passes */
    chr_shadow_build_actor(0, pos, 1.0f, 1.0f, 1, 1, 0.0f, n, &p);
    if (!p.draw) T_FAIL("|n.y|==0.7 should draw");
    return 0;
}

/* C3a faced-cell glow (Block G): the object origin maps to (render_x, 1.9,
 * render_z); the local +X corner mirrors to render_x − 256·scale (scale.x is
 * negative) and +Z extends to render_z + 256·scale. */
int test_chr_shadow_glow_placement(void)
{
    float w[16];
    uint32_t color;
    chr_shadow_build_display_glow(1.0f, 2.0f, /*sim_frame*/0, w, &color);

    float o[4] = { 0, 0, 0, 1 }, wo[4];
    xform(w, o, wo);
    NEAR(wo[0], 1.0f, 1e-4f);
    NEAR(wo[1], 1.9f, 1e-4f);
    NEAR(wo[2], 2.0f, 1e-4f);

    /* 256 · 0.0036799998 = 0.94208 */
    float c[4] = { 256, 0, 256, 1 }, wc[4];
    xform(w, c, wc);
    NEAR(wc[0], 1.0f - 0.94208f, 1e-3f);       /* mirrored (−X scale) */
    NEAR(wc[1], 1.9f, 1e-4f);
    NEAR(wc[2], 2.0f + 0.94208f, 1e-3f);
    return 0;
}

/* Glow alpha pulses 127..191 around 159; RGB stays white; frame 0 → 0x9f. */
int test_chr_shadow_glow_alpha(void)
{
    float w[16];
    uint32_t color;

    chr_shadow_build_display_glow(0.0f, 0.0f, 0, w, &color);
    if (color != 0x9fffffffu)
        T_FAIL("frame-0 glow colour = %08x, want 9fffffff", color);

    for (uint32_t f = 0; f < 400; f++) {
        chr_shadow_build_display_glow(0.0f, 0.0f, f, w, &color);
        if ((color & 0x00ffffffu) != 0x00ffffffu)
            T_FAIL("glow RGB must be white, got %08x at f=%u", color, f);
        uint32_t a = color >> 24;
        if (a < 127u || a > 191u)
            T_FAIL("glow alpha %u out of [127,191] at f=%u", a, f);
    }
    return 0;
}
