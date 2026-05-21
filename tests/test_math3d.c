/*
 * test_math3d.c — vec3/mat4 helper unit tests.
 *
 * Verifies algebraic outputs match D3DXMatrixLookAtRH /
 * MatrixPerspectiveFovRH / MatrixMultiply semantics on hand-picked inputs.
 * Bit-exact match against the engine's D3DX path isn't pursued — the
 * dispatcher there picks between x87 / MMX / SSE backends, so even the
 * engine has its own per-CPU bit drift.
 */

#include "t.h"
#include "math3d.h"

#include <math.h>

/* M_PI is glibc-only — define locally so -std=c11 builds work without
 * any feature-test macros. */
#ifndef M_PI
# define M_PI 3.14159265358979323846
#endif

static int approx(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

#define T_NEAR(a, b) do { \
    if (!approx((a), (b), 1e-5f)) \
        T_FAIL("expected %s ≈ %s (got %f, want %f)", #a, #b, (a), (b)); \
} while (0)

int test_math_vec3_normalize_3_4_0(void)
{
    float in[3]  = { 3.0f, 4.0f, 0.0f };
    float out[3] = {0};
    vec3_normalize(out, in);
    T_NEAR(out[0], 0.6f);
    T_NEAR(out[1], 0.8f);
    T_NEAR(out[2], 0.0f);
    return 0;
}

int test_math_vec3_normalize_in_place(void)
{
    float v[3] = { 0.0f, 0.0f, 5.0f };
    vec3_normalize(v, v);
    T_NEAR(v[0], 0.0f);
    T_NEAR(v[1], 0.0f);
    T_NEAR(v[2], 1.0f);
    return 0;
}

int test_math_lookat_z_back_camera(void)
{
    /* Camera at (0,0,10) looking at origin, up=Y. RH lookat:
     *   zaxis = normalize((0,0,10) - (0,0,0)) = (0,0,1)
     *   xaxis = normalize((0,1,0) × (0,0,1))  = (1,0,0)
     *   yaxis = (0,0,1) × (1,0,0) = (0,1,0)
     * Translation: -dot(z, eye) = -(1*10) = -10.
     */
    float eye[3]    = { 0, 0, 10 };
    float target[3] = { 0, 0, 0 };
    float up[3]     = { 0, 1, 0 };
    float m[16];
    mat4_lookat_rh(m, eye, target, up);

    /* Diagonal of the rotation block is identity (no rotation). */
    T_NEAR(m[ 0], 1.0f); T_NEAR(m[ 5], 1.0f); T_NEAR(m[10], 1.0f);
    /* Off-diagonal rotation entries zero. */
    T_NEAR(m[ 1], 0.0f); T_NEAR(m[ 2], 0.0f);
    T_NEAR(m[ 4], 0.0f); T_NEAR(m[ 6], 0.0f);
    T_NEAR(m[ 8], 0.0f); T_NEAR(m[ 9], 0.0f);
    /* Translation row. */
    T_NEAR(m[12], 0.0f);
    T_NEAR(m[13], 0.0f);
    T_NEAR(m[14], -10.0f);
    T_NEAR(m[15], 1.0f);
    /* Last column zero (m[3]=m[7]=m[11]=0). */
    T_NEAR(m[ 3], 0.0f);
    T_NEAR(m[ 7], 0.0f);
    T_NEAR(m[11], 0.0f);
    return 0;
}

int test_math_lookat_off_axis(void)
{
    /* Camera at (10, 0, 0) looking at origin, up=Y.
     *   zaxis = normalize((10,0,0)) = (1,0,0)
     *   xaxis = normalize((0,1,0) × (1,0,0)) = normalize((0,0,-1)) = (0,0,-1)
     *   yaxis = (1,0,0) × (0,0,-1) = (0,1,0)
     * Translation: m[12]=-dot(x,eye)=0; m[14]=-dot(z,eye)=-10. */
    float eye[3]    = { 10, 0, 0 };
    float target[3] = { 0, 0, 0 };
    float up[3]     = { 0, 1, 0 };
    float m[16];
    mat4_lookat_rh(m, eye, target, up);

    T_NEAR(m[ 0],  0.0f); T_NEAR(m[ 4], 0.0f); T_NEAR(m[ 8], -1.0f);
    T_NEAR(m[ 1],  0.0f); T_NEAR(m[ 5], 1.0f); T_NEAR(m[ 9],  0.0f);
    T_NEAR(m[ 2],  1.0f); T_NEAR(m[ 6], 0.0f); T_NEAR(m[10],  0.0f);
    T_NEAR(m[12],  0.0f); T_NEAR(m[13], 0.0f); T_NEAR(m[14], -10.0f);
    T_NEAR(m[15],  1.0f);
    return 0;
}

int test_math_perspective_fov_pi_over_2_aspect_1(void)
{
    /* fov=π/2, aspect=1, near=1, far=10:
     *   h = cot(π/4) = 1
     *   w = h/aspect = 1
     *   z_range = far / (near - far) = 10 / -9 = -1.11111
     *   m[14] = z_range * near = -1.11111
     *   m[11] = -1 (RH marker)
     */
    float m[16];
    mat4_perspective_fov_rh(m, (float)(M_PI / 2.0), 1.0f, 1.0f, 10.0f);
    T_NEAR(m[ 0], 1.0f);
    T_NEAR(m[ 5], 1.0f);
    T_NEAR(m[10], -10.0f / 9.0f);
    T_NEAR(m[11], -1.0f);
    T_NEAR(m[14], -10.0f / 9.0f);
    T_NEAR(m[15], 0.0f);
    /* Off-diagonals zero. */
    T_NEAR(m[ 1], 0.0f); T_NEAR(m[ 2], 0.0f); T_NEAR(m[ 3], 0.0f);
    T_NEAR(m[ 4], 0.0f); T_NEAR(m[ 6], 0.0f); T_NEAR(m[ 7], 0.0f);
    T_NEAR(m[ 8], 0.0f); T_NEAR(m[ 9], 0.0f);
    T_NEAR(m[12], 0.0f); T_NEAR(m[13], 0.0f);
    return 0;
}

int test_math_perspective_aspect_changes_w_only(void)
{
    float m1[16], m2[16];
    mat4_perspective_fov_rh(m1, (float)(M_PI / 2.0), 1.0f, 1.0f, 10.0f);
    mat4_perspective_fov_rh(m2, (float)(M_PI / 2.0), 2.0f, 1.0f, 10.0f);
    /* w halves when aspect doubles; h unchanged; z entries unchanged. */
    T_NEAR(m2[ 0], 0.5f);
    T_NEAR(m2[ 5], m1[5]);
    T_NEAR(m2[10], m1[10]);
    T_NEAR(m2[14], m1[14]);
    return 0;
}

int test_math_mul_identity_identity_is_identity(void)
{
    float id[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    float out[16];
    mat4_mul(out, id, id);
    for (int i = 0; i < 16; i++) T_NEAR(out[i], id[i]);
    return 0;
}

int test_math_mul_handles_output_alias(void)
{
    /* The engine calls D3DXMatrixMultiply(view, view, proj) — output
     * aliases m1. Our mat4_mul must support this via an internal temp. */
    float a[16] = {
         2, 0, 0, 0,
         0, 3, 0, 0,
         0, 0, 4, 0,
         5, 6, 7, 1,
    };
    float b[16] = {
         1, 0, 0, 0,
         0, 1, 0, 0,
         0, 0, 1, 0,
         0, 0, 0, 1,
    };
    float a_copy[16];
    memcpy(a_copy, a, sizeof a_copy);

    mat4_mul(a, a, b);    /* alias output with input m1 */
    /* a * identity == a */
    for (int i = 0; i < 16; i++) T_NEAR(a[i], a_copy[i]);
    return 0;
}

int test_math_mul_diagonal_scaling(void)
{
    float scale2[16] = {
        2, 0, 0, 0,
        0, 2, 0, 0,
        0, 0, 2, 0,
        0, 0, 0, 1,
    };
    float scale3[16] = {
        3, 0, 0, 0,
        0, 3, 0, 0,
        0, 0, 3, 0,
        0, 0, 0, 1,
    };
    float out[16];
    mat4_mul(out, scale2, scale3);
    T_NEAR(out[ 0], 6.0f);
    T_NEAR(out[ 5], 6.0f);
    T_NEAR(out[10], 6.0f);
    T_NEAR(out[15], 1.0f);
    return 0;
}
