/*
 * math3d.c — retail-exact mirror of the D3DX8 math retail links statically.
 *
 * D3DX8 dispatches its math through a 57-slot function-pointer table
 * (0x5fda48).  The first math call runs D3DXCpuOptimizations (0x4cdd9f):
 * c_* defaults + x86_* overrides, then PF_3DNOW → x3d_* table, else CPUID SSE2 →
 * the SSE2 table (DAT_005fe908 = 2), else SSE → sse table.  Every CPU since ~2011
 * (all Intel; AMD from Bulldozer, which dropped 3DNow!) takes the SSE2 table,
 * measured in-process: tools/oracle/p1_math3d.c.  So retail's math is:
 *   D3DXVec3Normalize    slot  7 → 0x4ecb60  SSE: rsqrtss + 1 Newton step
 *   D3DXMatrixMultiply   slot  3 → 0x4eca10  SSE: mulps/addps
 *   D3DXMatrixInverse    slot 18 → 0x4ecdb0  SSE: Cramer (Intel AP-928), rcpss + 1 Newton step
 *   D3DXMatrixShadow     slot 27 → 0x4ed550  SSE: inline rsqrt plane normalize
 *   D3DXMatrixRotationX  slot 30 → 0x4ed280  SSE: polynomial sse2_sincos (0x4f8730)
 *   D3DXMatrixRotationY  slot 31 → 0x4ed300  SSE: polynomial sse2_sincos
 *   D3DXMatrixRotationZ  slot 32 → 0x4a3692  x87 c_: fsincos
 *   D3DXPlaneFromPointNormal slot 12 → 0x4a4f65 x87 c_
 *   D3DXMatrixTranslation/Scaling slots 26/33 → c_ (pure copies)
 *   D3DXMatrixLookAtRH 0x4a3b52, D3DXMatrixPerspectiveFovRH 0x4a3ee8: x87, not
 *   table-dispatched (LookAtRH normalizes via slot 7 = the SSE normalize).
 *
 * SSE parts are transcribed instruction-by-instruction with intrinsics (each op
 * rounds to float; rsqrtss/rcpss are the CPU's own approximations, so the port
 * matches retail on the SAME CPU — Intel and AMD differ in those bits, and so does
 * retail).  x87 parts follow x87.h (long double = ST, spill32 = fstp m32).  Both
 * are independent of -std=c11/gnu11 and of the x87 CW.  Requires SSE2 (Win8+).
 * Allowed-SSE list for the x87 CI gate: tools/ci/no_sse_math.py SSE_ALLOWED.
 */
/* PORT-DEBT(simplified, FUN_004cdd9f): only the SSE2 D3DX table is mirrored; the 3DNow! x3d_ / SSE1-only / DisableD3DXPSGP (c_+x86_) tables are not (pre-2011 AMD or a registry override only). */
#include "math3d.h"
#include "x87.h"

#include <string.h>
#include <emmintrin.h>

#if defined(__i386__) && !defined(__SSE2__)
/* i686 port build: SSE2 only inside these functions; realign the stack because
 * Win32 callers only guarantee 4 bytes (retail's own SSE code does `and esp,-16`). */
# define M3_SSE __attribute__((target("sse2"), force_align_arg_pointer))
#else
# define M3_SSE
#endif

#define SHUF(a, b, imm) _mm_shuffle_ps((a), (b), (imm))
static inline M3_SSE __m128 m3_bits(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    return _mm_castsi128_ps(_mm_set_epi32((int)d, (int)c, (int)b, (int)a));
}

/* D3DXVec3Normalize, SSE2 table @0x4ecb60.  len2 = (x²+y²)+z²; len2 < 2^-46
 * (m32 0x28800000 @0x964f180) → (0,0,0); else s = (0.5·r)·(3 − (len2·r)·r),
 * r = rsqrtss(len2); out = s·v. */
M3_SSE void vec3_normalize(float out[3], const float in[3])
{
    __m128 x0 = _mm_load_ss(&in[0]), x6 = _mm_load_ss(&in[1]), x5 = _mm_load_ss(&in[2]);
    __m128 x2 = _mm_mul_ss(x0, x0);
    __m128 x3 = _mm_mul_ss(x6, x6);
    x2 = _mm_add_ss(x2, x3);
    x3 = _mm_mul_ss(x5, x5);
    x3 = _mm_add_ss(x2, x3);
    const float eps = _mm_cvtss_f32(m3_bits(0x28800000u, 0, 0, 0));
    if (eps > _mm_cvtss_f32(x3)) {                      /* comiss eps,len2; jbe (NaN → normal path) */
        const __m128 z = _mm_setzero_ps();
        _mm_store_ss(&out[0], z); _mm_store_ss(&out[1], z); _mm_store_ss(&out[2], z);
        return;
    }
    __m128 x1 = _mm_set_ss(3.0f);
    x2 = _mm_set_ss(0.5f);
    __m128 x4 = x3;
    x3 = _mm_rsqrt_ss(x3);
    x4 = _mm_mul_ss(x4, x3);
    x2 = _mm_mul_ss(x2, x3);
    x4 = _mm_mul_ss(x4, x3);
    x1 = _mm_sub_ss(x1, x4);
    x3 = x0;
    x0 = _mm_mul_ss(x2, x1);
    _mm_store_ss(&out[0], _mm_mul_ss(x0, x3));
    _mm_store_ss(&out[1], _mm_mul_ss(x0, x6));
    _mm_store_ss(&out[2], _mm_mul_ss(x0, x5));
}

/* D3DXMatrixLookAtRH @0x4a3b52 (x87; not table-dispatched).
 *   z = spill32(eye − at); normalize(z)            (slot 7 → SSE normalize)
 *   t = spill32(up × z) per component; x = normalize(t)
 *   y = z × x kept in ST (spilled only into out[1/5/9])
 *   out[12..14] = spill32(−((a0·e0 + a1·e1) + a2·e2)) — y from ST, unrounded. */
void mat4_lookat_rh(float out[16], const float eye[3],
                    const float target[3], const float up[3])
{
    float z[3], t[3], x[3];
    z[0] = spill32((long double)eye[0] - target[0]);
    z[1] = spill32((long double)eye[1] - target[1]);
    z[2] = spill32((long double)eye[2] - target[2]);
    vec3_normalize(z, z);
    t[0] = spill32((long double)z[2] * up[1] - (long double)z[1] * up[2]);
    t[1] = spill32((long double)z[0] * up[2] - (long double)z[2] * up[0]);
    t[2] = spill32((long double)z[1] * up[0] - (long double)z[0] * up[1]);
    vec3_normalize(x, t);
    const long double y0 = (long double)z[1] * x[2] - (long double)z[2] * x[1];
    const long double y1 = (long double)z[2] * x[0] - (long double)z[0] * x[2];
    const long double y2 = (long double)z[0] * x[1] - (long double)z[1] * x[0];

    out[ 0] = x[0]; out[ 4] = x[1]; out[ 8] = x[2];
    out[12] = spill32(-(((long double)x[0] * eye[0] + (long double)x[1] * eye[1]) + (long double)x[2] * eye[2]));
    out[ 1] = spill32(y0); out[ 5] = spill32(y1); out[ 9] = spill32(y2);
    out[13] = spill32(-((y0 * eye[0] + y1 * eye[1]) + y2 * eye[2]));
    out[ 2] = z[0]; out[ 6] = z[1]; out[10] = z[2];
    out[14] = spill32(-(((long double)z[0] * eye[0] + (long double)z[1] * eye[1]) + (long double)z[2] * eye[2]));
    out[ 3] = 0.0f; out[ 7] = 0.0f; out[11] = 0.0f; out[15] = 1.0f;
}

/* D3DXMatrixPerspectiveFovRH @0x4a3ee8 (x87).
 *   t = spill32(fovy · 0.5f(m32 @0x51c6fc)); fsincos t → cos, sin spilled m32
 *   h = cos/sin (ST); out[0] = spill32(h/aspect); out[5] = spill32(h)
 *   q = zf/(zn − zf) (ST); out[10] = spill32(q); out[14] = spill32(q·zn) */
void mat4_perspective_fov_rh(float out[16], float fov_y, float aspect,
                             float z_near, float z_far)
{
    static const float K_HALF = 0.5f;
    long double s, c;
    x87_fsincos((long double)spill32((long double)fov_y * K_HALF), &s, &c);
    const float cf = spill32(c), sf = spill32(s);
    const long double h = (long double)cf / sf;
    const long double q = (long double)z_far / ((long double)z_near - z_far);

    out[ 0] = spill32(h / aspect); out[ 1] = 0.0f; out[ 2] = 0.0f;           out[ 3] = 0.0f;
    out[ 4] = 0.0f;                out[ 5] = spill32(h); out[ 6] = 0.0f;     out[ 7] = 0.0f;
    out[ 8] = 0.0f;                out[ 9] = 0.0f; out[10] = spill32(q);     out[11] = -1.0f;
    out[12] = 0.0f;                out[13] = 0.0f; out[14] = spill32(q * z_near); out[15] = 0.0f;
}

/* D3DXMatrixMultiply, SSE2 table @0x4eca10: out row i = ((a_i0·B0 + a_i1·B1) +
 * a_i2·B2) + a_i3·B3, float-rounded per op.  All loads precede the stores
 * (out may alias a or b). */
M3_SSE void mat4_mul(float out[16], const float a[16], const float b[16])
{
    const __m128 b0 = _mm_loadu_ps(b), b1 = _mm_loadu_ps(b + 4),
                 b2 = _mm_loadu_ps(b + 8), b3 = _mm_loadu_ps(b + 12);
    __m128 r[4];
    for (int i = 0; i < 4; i++) {
        __m128 acc = _mm_mul_ps(_mm_set1_ps(a[i * 4 + 0]), b0);
        acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(a[i * 4 + 1]), b1));
        acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(a[i * 4 + 2]), b2));
        r[i] = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(a[i * 4 + 3]), b3));
    }
    for (int i = 0; i < 4; i++) _mm_storeu_ps(out + i * 4, r[i]);
}

/* D3DXVec3Transform, SSE2 table slot 1 @0x4ec790 (dispatch entry 0x4a2227): out is a
 * vec4, (x·M0 + y·M1) + (z·M2 + M3) per lane, float-rounded per op, no w-divide
 * (slot 2 @0x4ec850 is the TransformCoord variant).  Both of retail's branches
 * (16-aligned M or not) add in this order. */
M3_SSE void vec3_transform(float out[4], const float v[3], const float m[16])
{
    const __m128 r0 = _mm_loadu_ps(m), r1 = _mm_loadu_ps(m + 4),
                 r2 = _mm_loadu_ps(m + 8), r3 = _mm_loadu_ps(m + 12);
    const __m128 xy = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(v[0]), r0), _mm_mul_ps(_mm_set1_ps(v[1]), r1));
    const __m128 zw = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(v[2]), r2), r3);
    _mm_storeu_ps(out, _mm_add_ps(xy, zw));
}

void mat4_identity(float out[16])
{
    static const float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    memcpy(out, ident, sizeof ident);
}

void mat4_translation(float out[16], float tx, float ty, float tz)
{
    mat4_identity(out);
    out[12] = tx;
    out[13] = ty;
    out[14] = tz;
}

void mat4_scaling(float out[16], float sx, float sy, float sz)
{
    mat4_identity(out);
    out[ 0] = sx;
    out[ 5] = sy;
    out[10] = sz;
}

/* D3DXPlaneFromPointNormal, c_ @0x4a4f65 (slot 12; the SSE2 table keeps c_):
 * d = −((p.y·n.y + p.z·n.z) + p.x·n.x) in ST, one spill. */
void plane_from_point_normal(float out[4], const float point[3],
                             const float normal[3])
{
    out[0] = normal[0];
    out[1] = normal[1];
    out[2] = normal[2];
    out[3] = spill32(-(((long double)point[1] * normal[1] + (long double)point[2] * normal[2])
                       + (long double)point[0] * normal[0]));
}

/* D3DXMatrixShadow, SSE2 table @0x4ed550.
 *   lensq = (a² + c²) + b²  (masked square, movhlps/shufps sum)
 *   lensq == 0 or NaN → P = that sum vector (= 0 for a zero normal), no normalize
 *   else P = plane · s,  s = (0.5·r)·(3 − (lensq·r)·r), r = rsqrtss(lensq)
 *   dot = (P0·L0 + P2·L2) + (P1·L1 + P3·L3)
 *   row i = −P_i·L + (i==j ? dot : +0)  — rows 1..3 add the masked dot to every
 *   lane (−0 → +0 off-diagonal); row 0 adds dot to lane 0 only. */
M3_SSE void mat4_shadow(float out[16], const float light[4], const float plane[4])
{
    const __m128 zero = _mm_setzero_ps();
    __m128 x2 = _mm_loadu_ps(plane);
    __m128 x3 = _mm_and_ps(m3_bits(~0u, ~0u, ~0u, 0), x2);
    __m128 x0 = zero, x1 = zero;
    x3 = _mm_mul_ps(x3, x3);
    x0 = _mm_movehl_ps(x0, x3);
    x1 = _mm_add_ps(x3, x0);
    x0 = SHUF(x1, x1, 0x01);
    x0 = _mm_add_ss(x0, x1);
    const float lensq = _mm_cvtss_f32(x0);
    if (lensq < 0.0f || lensq > 0.0f) {                 /* comiss lensq,0; ZF=0 */
        __m128 x4 = _mm_set_ss(0.5f), x5;
        x3 = _mm_set_ss(3.0f);
        x5 = _mm_rsqrt_ss(x0);
        x0 = _mm_mul_ss(x0, x5);
        x4 = _mm_mul_ss(x4, x5);
        x0 = _mm_mul_ss(x0, x5);
        x3 = _mm_sub_ss(x3, x0);
        x4 = _mm_mul_ss(x4, x3);
        x4 = SHUF(x4, x4, 0x00);
        x0 = _mm_mul_ps(x2, x4);
    }
    x2 = _mm_loadu_ps(light);
    x3 = m3_bits(0, ~0u, 0, 0);
    __m128 x4 = _mm_xor_ps(m3_bits(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u), x0);   /* −P */
    x0 = _mm_mul_ps(x0, x2);
    __m128 x5 = m3_bits(0, 0, ~0u, 0);
    x1 = _mm_movehl_ps(x1, x0);
    __m128 x6 = m3_bits(0, 0, 0, ~0u);
    x0 = _mm_add_ps(x0, x1);
    x1 = SHUF(x0, x0, 0x01);
    x0 = _mm_add_ss(x0, x1);
    x0 = SHUF(x0, x0, 0x00);                            /* dot, broadcast */
    x3 = _mm_and_ps(x3, x0);
    x5 = _mm_and_ps(x5, x0);
    x6 = _mm_and_ps(x6, x0);
    x1 = x4;
    x4 = SHUF(x4, x4, 0x55);
    x4 = _mm_mul_ps(x4, x2);
    const __m128 x7 = x0;
    x0 = _mm_add_ps(x4, x3);                            /* row 1 */
    x3 = SHUF(x1, x1, 0xaa);
    x3 = _mm_mul_ps(x3, x2);
    x4 = x1;
    x1 = _mm_add_ps(x3, x5);                            /* row 2 */
    x3 = x4;
    x4 = SHUF(x4, x4, 0xff);
    x3 = SHUF(x3, x3, 0x00);
    x4 = _mm_mul_ps(x4, x2);
    x3 = _mm_mul_ps(x3, x2);
    x2 = _mm_add_ps(x4, x6);                            /* row 3 */
    x3 = _mm_add_ss(x3, x7);                            /* row 0 */
    _mm_storeu_ps(out + 0, x3);
    _mm_storeu_ps(out + 4, x0);
    _mm_storeu_ps(out + 8, x1);
    _mm_storeu_ps(out + 12, x2);
}

/* sse2_sincos @0x4f8730 (D3DX8 d3dxtrigsse2): 4-lane polynomial sin/cos.  Input
 * broadcast; returns lane0 = cos, lane1 = sin (lanes 2/3 repeat).  Cody–Waite
 * reduction by π/2 (4-term split, each lane subtracts the terms in a rotated
 * order), quadrant from x·(2/π) + 2^23. */
static inline M3_SSE __m128 d3dx_sse2_sincos(__m128 x0)
{
    const __m128 ABS = m3_bits(0x7fffffffu, 0x7fffffffu, 0x7fffffffu, 0x7fffffffu);
    const __m128 SGN = m3_bits(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u);
    const __m128 TWO_OVER_PI = m3_bits(0x3f22f983u, 0x3f22f983u, 0x3f22f983u, 0x3f22f983u);
    const __m128 MAGIC = m3_bits(0x4b000000u, 0x4b000000u, 0x4b000000u, 0x4b000000u);       /* 2^23 */
    const __m128 PIO2 = m3_bits(0x3fc90000u, 0x39fda000u, 0x33a22000u, 0x2c34611au);
    const __m128 MANT = m3_bits(0x007fffffu, 0x007fffffu, 0x007fffffu, 0x007fffffu);
    const __m128i ONE = _mm_set1_epi32(1), TWO = _mm_set1_epi32(2);
    const __m128 C3 = m3_bits(0xb94ca1f0u, 0x37ccf5ceu, 0xb94ca1f0u, 0x37ccf5ceu);
    const __m128 C2 = m3_bits(0x3c08839du, 0xbab6061au, 0x3c08839du, 0xbab6061au);
    const __m128 C1 = m3_bits(0xbe2aaaa3u, 0x3d2aaaa5u, 0xbe2aaaa3u, 0x3d2aaaa5u);
    const __m128 C0 = m3_bits(0x3f800000u, 0xbf000000u, 0x3f800000u, 0xbf000000u);
    const __m128 D0 = m3_bits(0, 0x3f800000u, 0, 0x3f800000u);

    __m128 x1 = x0;
    x0 = _mm_and_ps(x0, ABS);
    x1 = _mm_and_ps(x1, SGN);
    __m128 x2 = x0;
    x0 = _mm_mul_ps(x0, TWO_OVER_PI);
    __m128 x4 = PIO2;
    x0 = _mm_add_ps(x0, MAGIC);
    __m128 x3 = x0;
    x0 = _mm_sub_ps(x0, MAGIC);
    x4 = _mm_mul_ps(x4, x0);
    x2 = _mm_sub_ps(x2, x4);
    x4 = SHUF(x4, x4, 0x39);
    x2 = _mm_sub_ps(x2, x4);
    x4 = SHUF(x4, x4, 0x39);
    x2 = _mm_sub_ps(x2, x4);
    x4 = SHUF(x4, x4, 0x39);
    x2 = _mm_sub_ps(x2, x4);
    x4 = _mm_mul_ps(x2, x2);
    x3 = _mm_and_ps(x3, MANT);
    __m128i q = _mm_castps_si128(x3), q0 = _mm_and_si128(q, ONE);
    __m128i q3 = _mm_sub_epi32(q, q0), q7 = _mm_add_epi32(q, q0);
    q3 = _mm_slli_epi32(_mm_and_si128(q3, TWO), 30);
    q7 = _mm_slli_epi32(_mm_and_si128(q7, TWO), 30);
    q3 = _mm_xor_si128(q3, _mm_castps_si128(x1));
    q0 = _mm_slli_epi32(q0, 30);
    x0 = _mm_cmpneq_ps(_mm_castsi128_ps(q0), _mm_setzero_ps());
    __m128 x5 = _mm_mul_ps(C3, x4);
    x5 = _mm_add_ps(x5, C2);
    x5 = _mm_mul_ps(x5, x4);
    x5 = _mm_add_ps(x5, C1);
    x5 = _mm_mul_ps(x5, x4);
    x5 = _mm_add_ps(x5, C0);
    x2 = _mm_unpacklo_ps(x2, x4);
    x5 = _mm_mul_ps(x5, x2);
    x5 = _mm_add_ps(x5, D0);
    x2 = x0;
    x0 = _mm_and_ps(x0, x5);
    x2 = _mm_andnot_ps(x2, x5);
    x2 = SHUF(x2, x2, 0xb1);
    x0 = _mm_or_ps(x0, x2);
    const __m128 x7 = _mm_unpacklo_ps(_mm_castsi128_ps(q7), _mm_castsi128_ps(q3));
    return _mm_xor_ps(x0, x7);
}

/* D3DXMatrixRotationX, SSE2 table @0x4ed280: rows (1,0,0,0) (0,c,s,0) (0,−s,c,0) (0,0,0,1). */
M3_SSE void mat4_rotation_x(float out[16], float radians)
{
    const __m128 r = d3dx_sse2_sincos(_mm_set1_ps(radians));
    const __m128 e0 = _mm_set_ps(0, 0, 0, 1.0f), e3 = _mm_set_ps(1.0f, 0, 0, 0);
    _mm_storeu_ps(out, e0);
    _mm_store_ss(out + 4, e3);
    _mm_storel_pi((__m64 *)(out + 5), r);
    _mm_storel_pi((__m64 *)(out + 7), e3);
    _mm_store_ss(out + 11, e3);
    _mm_storel_pi((__m64 *)(out + 9), _mm_xor_ps(SHUF(r, r, 0x11), m3_bits(0x80000000u, 0, 0, 0)));
    _mm_storeu_ps(out + 12, e3);
}

/* D3DXMatrixRotationY, SSE2 table @0x4ed300: rows (c,0,−s,0) (0,1,0,0) (s,0,c,0) (0,0,0,1). */
M3_SSE void mat4_rotation_y(float out[16], float radians)
{
    __m128 x0 = d3dx_sse2_sincos(_mm_set1_ps(radians));
    x0 = SHUF(x0, x0, 0x50);
    x0 = _mm_and_ps(x0, m3_bits(~0u, 0, ~0u, 0));
    _mm_storeu_ps(out, _mm_xor_ps(m3_bits(0, 0, 0x80000000u, 0), x0));
    _mm_storeu_ps(out + 4, _mm_set_ps(0, 0, 1.0f, 0));
    _mm_storeu_ps(out + 8, SHUF(x0, x0, 0x46));
    _mm_storeu_ps(out + 12, _mm_set_ps(1.0f, 0, 0, 0));
}

/* D3DXMatrixRotationZ, c_ @0x4a3692 (slot 32; the SSE2 table keeps c_):
 * fsincos(angle) → cos/sin spilled m32. */
void mat4_rotation_z(float out[16], float radians)
{
    long double s, c;
    x87_fsincos((long double)radians, &s, &c);
    const float cf = spill32(c), sf = spill32(s);
    mat4_identity(out);
    out[ 0] =  cf;  out[ 1] =  sf;
    out[ 4] = -sf;  out[ 5] =  cf;
}

/* D3DXMatrixInverse, SSE2 table @0x4ecdb0 — Intel AP-928 Cramer inverse on the
 * transposed matrix, transcribed register-for-register (x0..x7 = xmm0..7,
 * sNN = the [esp+0xNN] spill slots).  det == 0 or NaN → NULL: out untouched,
 * return 1.  Else 1/det = rcpss + 1 Newton step (2r − det·r²). */
M3_SSE int mat4_inverse(float out[16], const float in[16])
{
    __m128 x0, x1, x2, x3, x4, x5, x6, x7;
    __m128 s20, s30, s40, s50, s60, s70, s80, s90, sa0, sb0, sc0, sd0, se0;
    x0 = _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), (const __m64 *)(in + 2)), (const __m64 *)(in + 6));
    x4 = _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), (const __m64 *)(in + 10)), (const __m64 *)(in + 14));
    x3 = _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), (const __m64 *)(in + 8)), (const __m64 *)(in + 12));
    x1 = _mm_loadh_pi(_mm_loadl_pi(_mm_setzero_ps(), (const __m64 *)(in + 0)), (const __m64 *)(in + 4));
    x5 = x0;
    x0 = SHUF(x0, x4, 0x88);
    x4 = SHUF(x4, x5, 0xdd);
    x5 = _mm_mul_ps(x4, x0);
    x5 = SHUF(x5, x5, 0xb1);
    x6 = x5;
    x5 = SHUF(x5, x5, 0x4e);
    x7 = x3;
    x3 = SHUF(x3, x1, 0xdd);
    x1 = SHUF(x1, x7, 0x88);
    x7 = x3;
    x3 = _mm_mul_ps(x3, x5);
    x2 = _mm_setzero_ps();
    x5 = _mm_mul_ps(x5, x1);
    s30 = x5;
    x5 = x7;
    x7 = _mm_mul_ps(x7, x6);
    x6 = _mm_mul_ps(x6, x1);
    x3 = _mm_sub_ps(x3, x7);
    x7 = x5;
    x5 = _mm_mul_ps(x5, x0);
    x0 = SHUF(x0, x0, 0x4e);
    x5 = SHUF(x5, x5, 0xb1);
    s40 = x6;
    x6 = _mm_mul_ps(x4, x5);
    x6 = _mm_add_ps(x6, x3);
    x3 = x5;
    x5 = SHUF(x5, x5, 0x4e);
    x3 = _mm_mul_ps(x3, x1);
    s50 = x3;
    x3 = _mm_mul_ps(x4, x5);
    x5 = _mm_mul_ps(x5, x1);
    x6 = _mm_sub_ps(x6, x3);
    x3 = x7;
    x7 = SHUF(x7, x7, 0x4e);
    x7 = _mm_mul_ps(x7, x4);
    x7 = SHUF(x7, x7, 0xb1);
    s60 = x5;
    x5 = x0;
    x0 = _mm_mul_ps(x0, x7);
    x0 = _mm_add_ps(x0, x6);
    x6 = x7;
    x7 = SHUF(x7, x7, 0x4e);
    x6 = _mm_mul_ps(x6, x1);
    s70 = x4;
    x4 = x5;
    x5 = _mm_mul_ps(x5, x7);
    x7 = _mm_mul_ps(x7, x1);
    x0 = _mm_sub_ps(x0, x5);
    x7 = _mm_sub_ps(x7, x6);
    x5 = x1;
    x1 = _mm_mul_ps(x1, x0);
    x7 = SHUF(x7, x7, 0x4e);
    x6 = x1;
    x1 = SHUF(x1, x1, 0x4e);
    s20 = x0;
    x1 = _mm_add_ps(x1, x6);
    x0 = x1;
    x1 = SHUF(x1, x1, 0xb1);
    x1 = _mm_add_ss(x1, x0);                            /* det (lane 0) */
    x0 = x5;
    x5 = _mm_mul_ps(x5, x3);
    x5 = SHUF(x5, x5, 0xb1);
    x6 = x5;
    x5 = SHUF(x5, x5, 0x4e);
    s80 = x4;
    x4 = s70;
    s90 = x3;
    x3 = _mm_mul_ps(x4, x6);
    x3 = _mm_add_ps(x3, x7);
    x7 = _mm_mul_ps(x4, x5);
    x7 = _mm_sub_ps(x7, x3);
    x3 = _mm_mul_ps(x4, x0);
    x3 = SHUF(x3, x3, 0xb1);
    sa0 = x5;
    x5 = s90;
    sb0 = x6;
    x6 = x5;
    x5 = _mm_mul_ps(x5, x3);
    x5 = _mm_add_ps(x5, x7);
    x7 = x3;
    x3 = SHUF(x3, x3, 0x4e);
    sc0 = x4;
    x4 = _mm_mul_ps(x6, x3);
    sd0 = x6;
    x6 = _mm_sub_ps(x5, x4);
    x4 = _mm_sub_ps(s30, s40);
    x4 = SHUF(x4, x4, 0x4e);
    x5 = s80;
    x7 = _mm_mul_ps(x7, x5);
    x3 = _mm_mul_ps(x3, x5);
    x4 = _mm_sub_ps(x4, x7);
    x0 = _mm_mul_ps(x0, x5);
    x4 = _mm_add_ps(x4, x3);
    x0 = SHUF(x0, x0, 0xb1);
    x3 = x0;
    x0 = SHUF(x0, x0, 0x4e);
    x7 = sc0;
    se0 = x6;
    x6 = _mm_mul_ps(x7, x0);
    x7 = _mm_mul_ps(x7, x3);
    x4 = _mm_add_ps(x4, x7);
    x4 = _mm_sub_ps(x4, x6);
    x6 = _mm_sub_ps(s60, s50);
    x6 = SHUF(x6, x6, 0x4e);
    x7 = _mm_mul_ps(sb0, x5);
    x7 = _mm_sub_ps(x7, x6);
    x5 = _mm_mul_ps(x5, sa0);
    x7 = _mm_sub_ps(x7, x5);
    x5 = sd0;
    x3 = _mm_mul_ps(x3, x5);
    x5 = _mm_mul_ps(x5, x0);
    x7 = _mm_sub_ps(x7, x3);
    x2 = _mm_add_ps(x5, x7);
    const float det = _mm_cvtss_f32(x1);
    if (!(det < 0.0f || det > 0.0f))                    /* comiss det,0; je → NULL (0 or NaN) */
        return 1;
    x6 = se0;
    x0 = x1;
    x1 = s20;
    x3 = x0;
    x0 = _mm_rcp_ss(x0);
    x5 = x0;
    x0 = _mm_mul_ss(x0, x0);
    x3 = _mm_mul_ss(x3, x0);
    x5 = _mm_add_ss(x5, x5);
    x5 = _mm_sub_ss(x5, x3);
    x5 = SHUF(x5, x5, 0x00);
    _mm_storeu_ps(out + 0, _mm_mul_ps(x1, x5));
    _mm_storeu_ps(out + 4, _mm_mul_ps(x4, x5));
    _mm_storeu_ps(out + 8, _mm_mul_ps(x6, x5));
    _mm_storeu_ps(out + 12, _mm_mul_ps(x5, x2));
    return 0;
}
