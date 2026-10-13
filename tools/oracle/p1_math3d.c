/* P1 — math3d oracle: retail D3DX8 math (statically linked into recettear.exe)
 * called in-process vs the port's src/math3d.c built -std=c11 AND -std=gnu11.
 *
 * D3DX8 math is dispatched through a function-pointer table at 0x5fda48
 * (57 slots).  At image load every slot holds an init thunk
 * (push 1; call D3DXCpuOptimizations@0x4cdd9f; jmp [slot]); the FIRST math
 * call fills the table: defaults (c_*, copied from 0x5fdb30) + x86_* overrides,
 * then by CPU: PF_3DNOW (IsProcessorFeaturePresent(7)) → x3d_* (mode 1),
 * else CPUID.1:EDX.26 SSE2 → sse2 table (mode 2), else PF_XMMI → sse table
 * (mode 3); registry HKLM\Software\Microsoft\Direct3D\DisableD3DXPSGP=1 skips
 * the CPU step (mode 0).  Mode → DAT_005fe908.
 *
 * This oracle triggers retail's OWN lazy init by calling the dispatch entry
 * D3DXMatrixMultiply (0x4a2a10) through the untouched init thunk, logs the
 * mode + every slot it uses, then compares the port against the entry points
 * the GAME calls (every retail call site goes through these):
 *   vec3_normalize          D3DXVec3Normalize        0x4a1f74 (slot 7)
 *   mat4_mul                D3DXMatrixMultiply       0x4a2a10 (slot 3)
 *   mat4_inverse            D3DXMatrixInverse        0x4a2f42 (slot 18)
 *   mat4_shadow             D3DXMatrixShadow         0x4a4461 (slot 27)
 *   plane_from_point_normal D3DXPlaneFromPointNormal 0x4a4f5f (slot 12)
 *   mat4_rotation_x/y/z     D3DXMatrixRotationX/Y/Z  0x4a3553/0x4a35ef/0x4a368c (slots 30/31/32)
 *   mat4_translation        D3DXMatrixTranslation    0x4a34b0 (slot 26)
 *   mat4_scaling            D3DXMatrixScaling        0x4a3400 (slot 33)
 *   mat4_lookat_rh          D3DXMatrixLookAtRH       0x4a3b52 (direct; normalizes via slot 7)
 *   mat4_perspective_fov_rh D3DXMatrixPerspectiveFovRH 0x4a3ee8 (direct)
 *   mat4_identity           D3DXMatrixIdentity       0x404bb8 (inline helper)
 * Every output float compared bit-exact (memcmp), plus inverse's NULL return
 * (out left untouched → both sides prefilled with the same sentinel).  x87
 * precision PC53 (retail in-game CW 0x027F) / PC24 / PC64 via _controlfp_s.
 * At PC53 every concrete variant (c_/x86_/sse_/sse2_) is also compared against
 * the dispatched one (x3d_ = 3DNow! cannot execute on this CPU).
 *
 * usage: p1_math3d.exe recettear.unpacked.exe [cases_per_fn=100000] [dump.bin|-] [ref.bin]
 *   dump/ref: per-case digests of retail's PC53 outputs, to compare retail across CPUs. */
#include "retail_map.h"
#include <float.h>
#include <math.h>
#include <cpuid.h>
#include "math3d.h"

RETAIL_HOLE_DEFINE

/* -std=gnu11 build of the same src/math3d.c, symbols renamed via -D (build.sh) */
void vec3_normalize_gnu11(float out[3], const float in[3]);
void mat4_lookat_rh_gnu11(float out[16], const float eye[3], const float target[3], const float up[3]);
void mat4_perspective_fov_rh_gnu11(float out[16], float fov_y, float aspect, float z_near, float z_far);
void mat4_mul_gnu11(float out[16], const float a[16], const float b[16]);
void mat4_identity_gnu11(float out[16]);
void mat4_translation_gnu11(float out[16], float tx, float ty, float tz);
void mat4_scaling_gnu11(float out[16], float sx, float sy, float sz);
void mat4_rotation_x_gnu11(float out[16], float radians);
void mat4_rotation_y_gnu11(float out[16], float radians);
void mat4_rotation_z_gnu11(float out[16], float radians);
void plane_from_point_normal_gnu11(float out[4], const float point[3], const float normal[3]);
void mat4_shadow_gnu11(float out[16], const float light[4], const float plane[4]);
int  mat4_inverse_gnu11(float out[16], const float in[16]);

typedef struct {
    const char *flag;
    void (*v3n)(float *, const float *);
    void (*lookat)(float *, const float *, const float *, const float *);
    void (*persp)(float *, float, float, float, float);
    void (*mul)(float *, const float *, const float *);
    void (*ident)(float *);
    void (*trans)(float *, float, float, float);
    void (*scale)(float *, float, float, float);
    void (*rot[3])(float *, float);
    void (*pfpn)(float *, const float *, const float *);
    void (*shadow)(float *, const float *, const float *);
    int (*inv)(float *, const float *);
} port_t;

static const port_t PORT[2] = {
    {"c11", vec3_normalize, mat4_lookat_rh, mat4_perspective_fov_rh, mat4_mul, mat4_identity,
     mat4_translation, mat4_scaling, {mat4_rotation_x, mat4_rotation_y, mat4_rotation_z},
     plane_from_point_normal, mat4_shadow, mat4_inverse},
    {"gnu11", vec3_normalize_gnu11, mat4_lookat_rh_gnu11, mat4_perspective_fov_rh_gnu11, mat4_mul_gnu11,
     mat4_identity_gnu11, mat4_translation_gnu11, mat4_scaling_gnu11,
     {mat4_rotation_x_gnu11, mat4_rotation_y_gnu11, mat4_rotation_z_gnu11},
     plane_from_point_normal_gnu11, mat4_shadow_gnu11, mat4_inverse_gnu11},
};

/* retail signatures (D3DX8: WINAPI = stdcall, except the inline Identity) */
typedef float *(__stdcall *r_v3n_t)(float *, const float *);
typedef float *(__stdcall *r_m3_t)(float *, const float *, const float *);       /* mul / shadow / pfpn */
typedef float *(__stdcall *r_inv_t)(float *, float *, const float *);
typedef float *(__stdcall *r_lookat_t)(float *, const float *, const float *, const float *);
typedef float *(__stdcall *r_persp_t)(float *, float, float, float, float);
typedef float *(__stdcall *r_rot_t)(float *, float);
typedef float *(__stdcall *r_f3_t)(float *, float, float, float);
typedef float *(__cdecl *r_ident_t)(float *);
#define RFN(t, va) ((t)(uintptr_t)(va))

#define SENTINEL 0x7fa5a5a5u   /* a NaN pattern neither side ever writes */
typedef struct { float f[40]; } in_t;
typedef struct { uint32_t o[16]; int32_t ret; } out_t;

static uint32_t xs = 0x2545f491u;
static uint32_t xr(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static float fr(double lo, double hi) { return (float)(lo + (hi - lo) * (xr() * (1.0 / 4294967296.0))); }
static float fany(int emin, int emax)   /* random finite float, |x| in [2^emin, 2^emax) */
{
    uint32_t e = (uint32_t)(127 + emin + (int)(xr() % (uint32_t)(emax - emin)));
    uint32_t b = (xr() & 0x80000000u) | (e << 23) | (xr() & 0x7fffffu);
    float f; memcpy(&f, &b, 4); return f;
}
static void unitish(float *v, float scale)
{
    float x = fr(-1, 1), y = fr(-1, 1), z = fr(-1, 1);
    double l = sqrt((double)x * x + (double)y * y + (double)z * z);
    if (l < 1e-6) { x = 0; y = 1; z = 0; l = 1; }
    v[0] = (float)(x / l * scale); v[1] = (float)(y / l * scale); v[2] = (float)(z / l * scale);
}
static void affine(float *m)   /* rotation (yaw/pitch) * scale + translation, row-vector convention */
{
    double a = fr(-3.2, 3.2), b = fr(-1.6, 1.6), s = (xr() & 1) ? 1.0 : fr(0.1, 4);
    double ca = cos(a), sa = sin(a), cb = cos(b), sb = sin(b);
    double r[9] = {ca, 0, -sa, sa * sb, cb, ca * sb, sa * cb, -sb, ca * cb};
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) m[i * 4 + j] = (float)(r[i * 3 + j] * s);
        m[i * 4 + 3] = 0;
    }
    m[12] = fr(-50, 50); m[13] = fr(-50, 50); m[14] = fr(-50, 50); m[15] = 1;
}

/* ---- generators: in->f layout per function ---- */
static void g_vec3(in_t *k, long i)
{
    switch (i % 8) {
    case 0: case 1: case 2: for (int j = 0; j < 3; j++) k->f[j] = fr(-100, 100); break;
    case 3: unitish(k->f, fr(0.5, 2)); break;
    case 4: for (int j = 0; j < 3; j++) k->f[j] = fany(-30, 30); break;
    case 5: for (int j = 0; j < 3; j++) k->f[j] = fr(-3e-7, 3e-7); break;           /* around len2 eps 2^-46 */
    case 6: for (int j = 0; j < 3; j++) k->f[j] = (xr() % 3) ? 0.0f : fr(-10, 10); break;
    default: for (int j = 0; j < 3; j++) k->f[j] = fany(-70, 70); break;           /* over/underflow of x^2 */
    }
}
static void g_lookat(in_t *k, long i)   /* eye f[0..2], at f[3..5], up f[6..8] */
{
    if (i == 0) { memset(k->f, 0, 36); k->f[7] = 1; return; }                     /* prewindow boot: eye=at=0 */
    for (int j = 0; j < 6; j++) k->f[j] = fr(-60, 60);
    int c = (int)(i % 10);
    if (c < 6) { k->f[6] = 0; k->f[7] = 1; k->f[8] = 0; }
    else if (c < 8) unitish(k->f + 6, 1);
    else if (c == 8) for (int j = 6; j < 9; j++) k->f[j] = fr(-30, 30);           /* overlay: up = cam - pos */
    else {
        if (xr() & 1) memcpy(k->f + 3, k->f, 12);                                  /* eye == at */
        else for (int j = 0; j < 3; j++) k->f[6 + j] = (k->f[j] - k->f[3 + j]) * 2;/* up ∥ z */
    }
}
static void g_persp(in_t *k, long i)    /* fovy, aspect, zn, zf */
{
    if (i % 10 == 0) {
        static const float G[][4] = {{0.7853981f, 4.0f / 3.0f, 10.f, 2000.f},      /* prewindow boot */
                                     {0.785398163f, 640.f / 480.f, 1.f, 2000.f}, {1.5707963f, 1.f, 1.f, 100.f},
                                     {0.6f, 1.3333334f, 0.5f, 500.f}, {0.785398163f, 1.3333334f, 10.f, 10000.f}};
        memcpy(k->f, G[i == 0 ? 0 : xr() % 5], 16);
        return;
    }
    k->f[0] = fr(0.05, 3.0); k->f[1] = (xr() & 3) ? fr(0.3, 3.0) : 1.3333334f;
    k->f[2] = (xr() & 1) ? fr(0.01, 20) : fany(-10, 5);
    k->f[3] = k->f[2] + fr(0.5, 10000);
    if (i % 17 == 0) k->f[3] = k->f[2];                                            /* zn == zf → div by 0 */
}
static void g_mat(float *m, long i)
{
    switch (i % 4) {
    case 0: case 1: for (int j = 0; j < 16; j++) m[j] = fr(-10, 10); break;
    case 2: affine(m); break;
    default: for (int j = 0; j < 16; j++) m[j] = fany(-20, 20); break;
    }
}
static void g_mul(in_t *k, long i) { g_mat(k->f, i); g_mat(k->f + 16, i / 4); }
static void g_inv(in_t *k, long i)
{
    int c = (int)(i % 10);
    if (c < 4) for (int j = 0; j < 16; j++) k->f[j] = fr(-10, 10);
    else if (c < 7) affine(k->f);
    else if (c == 7) {                                                              /* lookat-like view */
        double e[3], z[3], x[3], y[3], l;
        for (int j = 0; j < 3; j++) { e[j] = fr(-50, 50); z[j] = e[j] - fr(-50, 50); }
        l = sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]); for (int j = 0; j < 3; j++) z[j] /= l;
        x[0] = z[2]; x[1] = 0; x[2] = -z[0];                                        /* (0,1,0) × z */
        l = sqrt(x[0] * x[0] + x[2] * x[2]); x[0] /= l; x[2] /= l;
        y[0] = z[1] * x[2] - z[2] * x[1]; y[1] = z[2] * x[0] - z[0] * x[2]; y[2] = z[0] * x[1] - z[1] * x[0];
        for (int j = 0; j < 3; j++) {
            k->f[j * 4 + 0] = (float)x[j]; k->f[j * 4 + 1] = (float)y[j]; k->f[j * 4 + 2] = (float)z[j]; k->f[j * 4 + 3] = 0;
        }
        k->f[12] = (float)-(x[0] * e[0] + x[1] * e[1] + x[2] * e[2]);
        k->f[13] = (float)-(y[0] * e[0] + y[1] * e[1] + y[2] * e[2]);
        k->f[14] = (float)-(z[0] * e[0] + z[1] * e[1] + z[2] * e[2]); k->f[15] = 1;
    } else if (c == 8) {                                                            /* singular / degenerate */
        for (int j = 0; j < 16; j++) k->f[j] = fr(-10, 10);
        int r = (int)(xr() % 4), s = (int)(xr() % 4);
        switch (xr() % 4) {
        case 0: for (int j = 0; j < 4; j++) k->f[r * 4 + j] = 0; break;
        case 1: for (int j = 0; j < 4; j++) k->f[j * 4 + r] = 0; break;
        case 2: if (r != s) memcpy(k->f + r * 4, k->f + s * 4, 16); break;
        default: for (int j = 0; j < 16; j++) k->f[j] = 0; break;
        }
    } else for (int j = 0; j < 16; j++) k->f[j] = fany(-12, 12);
}
static void g_pfpn(in_t *k, long i)     /* point f[0..2], normal f[3..5] */
{
    for (int j = 0; j < 3; j++) k->f[j] = (i % 5 == 4) ? fany(-30, 30) : fr(-20, 20);
    if (i % 3 == 0) unitish(k->f + 3, 1);
    else for (int j = 3; j < 6; j++) k->f[j] = (i % 5 == 4) ? fany(-30, 30) : fr(-2, 2);
}
static void g_shadow(in_t *k, long i)   /* light f[0..3], plane f[4..7] */
{
    int c = (int)(i % 4);
    if (c < 2) { k->f[0] = 0; k->f[1] = 1; k->f[2] = 0; k->f[3] = 0; }
    else if (c == 2) { unitish(k->f, fr(0.2, 3)); k->f[3] = (float)(xr() & 1); }
    else for (int j = 0; j < 4; j++) k->f[j] = fr(-10, 10);
    int p = (int)((i / 4) % 10);
    if (p < 5) { unitish(k->f + 4, 1); k->f[7] = fr(-5, 5); }
    else if (p < 7) for (int j = 4; j < 8; j++) k->f[j] = fr(-10, 10);
    else if (p == 7) { k->f[4] = k->f[5] = k->f[6] = 0; k->f[7] = fr(-5, 5); }   /* zero normal */
    else if (p == 8) { k->f[4] = fr(-1e-20, 1e-20); k->f[5] = fr(-1e-20, 1e-20); k->f[6] = 0; k->f[7] = fr(-5, 5); }
    else for (int j = 4; j < 8; j++) k->f[j] = fany(-20, 20);
}
static void g_angle(in_t *k, long i)
{
    static const float S[] = {0.0f, -0.0f, 1.5707964f, -1.5707964f, 3.1415927f, -3.1415927f, 0.7853982f,
                              2.3561945f, 4.712389f, 6.2831855f, 1e-8f, -1e-8f, 0.5f, 1.0f};
    switch (i % 10) {
    case 0: case 1: case 2: case 3: case 4: k->f[0] = fr(-6.2831853, 6.2831853); break;
    case 5: case 6: k->f[0] = fr(-100, 100); break;
    case 7: k->f[0] = S[xr() % (sizeof S / sizeof S[0])]; break;
    case 8: k->f[0] = fr(-1e4, 1e4); break;
    default: k->f[0] = fany(-30, 20); break;     /* |x| < 2^20: fsincos needs |x| < 2^63 */
    }
}
static void g_f3(in_t *k, long i) { for (int j = 0; j < 3; j++) k->f[j] = (i & 1) ? fr(-50, 50) : fany(-30, 30); }
static void g_none(in_t *k, long i) { (void)k; (void)i; }

/* ---- runners ---- */
static void prefill(out_t *o) { for (int j = 0; j < 16; j++) o->o[j] = SENTINEL; o->ret = 0; }
#define OUTF(o) ((float *)(o)->o)
static void r_vec3(uint32_t va, const in_t *k, out_t *o)   { RFN(r_v3n_t, va)(OUTF(o), k->f); }
static void p_vec3(const port_t *p, const in_t *k, out_t *o) { p->v3n(OUTF(o), k->f); }
static void r_lookat(uint32_t va, const in_t *k, out_t *o) { RFN(r_lookat_t, va)(OUTF(o), k->f, k->f + 3, k->f + 6); }
static void p_lookat(const port_t *p, const in_t *k, out_t *o) { p->lookat(OUTF(o), k->f, k->f + 3, k->f + 6); }
static void r_persp(uint32_t va, const in_t *k, out_t *o)  { RFN(r_persp_t, va)(OUTF(o), k->f[0], k->f[1], k->f[2], k->f[3]); }
static void p_persp(const port_t *p, const in_t *k, out_t *o) { p->persp(OUTF(o), k->f[0], k->f[1], k->f[2], k->f[3]); }
static void r_mul(uint32_t va, const in_t *k, out_t *o)    { RFN(r_m3_t, va)(OUTF(o), k->f, k->f + 16); }
static void p_mul(const port_t *p, const in_t *k, out_t *o) { p->mul(OUTF(o), k->f, k->f + 16); }
static void r_inv(uint32_t va, const in_t *k, out_t *o)    { o->ret = RFN(r_inv_t, va)(OUTF(o), NULL, k->f) == NULL; }
static void p_inv(const port_t *p, const in_t *k, out_t *o) { o->ret = p->inv(OUTF(o), k->f) != 0; }
static void r_pfpn(uint32_t va, const in_t *k, out_t *o)   { RFN(r_m3_t, va)(OUTF(o), k->f, k->f + 3); }
static void p_pfpn(const port_t *p, const in_t *k, out_t *o) { p->pfpn(OUTF(o), k->f, k->f + 3); }
static void r_shadow(uint32_t va, const in_t *k, out_t *o) { RFN(r_m3_t, va)(OUTF(o), k->f, k->f + 4); }
static void p_shadow(const port_t *p, const in_t *k, out_t *o) { p->shadow(OUTF(o), k->f, k->f + 4); }
static void r_rot(uint32_t va, const in_t *k, out_t *o)    { RFN(r_rot_t, va)(OUTF(o), k->f[0]); }
static void p_rotx(const port_t *p, const in_t *k, out_t *o) { p->rot[0](OUTF(o), k->f[0]); }
static void p_roty(const port_t *p, const in_t *k, out_t *o) { p->rot[1](OUTF(o), k->f[0]); }
static void p_rotz(const port_t *p, const in_t *k, out_t *o) { p->rot[2](OUTF(o), k->f[0]); }
static void r_f3(uint32_t va, const in_t *k, out_t *o)     { RFN(r_f3_t, va)(OUTF(o), k->f[0], k->f[1], k->f[2]); }
static void p_trans(const port_t *p, const in_t *k, out_t *o) { p->trans(OUTF(o), k->f[0], k->f[1], k->f[2]); }
static void p_scale(const port_t *p, const in_t *k, out_t *o) { p->scale(OUTF(o), k->f[0], k->f[1], k->f[2]); }
static void r_ident(uint32_t va, const in_t *k, out_t *o)  { (void)k; RFN(r_ident_t, va)(OUTF(o)); }
static void p_ident(const port_t *p, const in_t *k, out_t *o) { (void)k; p->ident(OUTF(o)); }

typedef struct { uint32_t va; const char *name; } variant_t;
typedef struct {
    const char *port_name, *d3dx_name;
    uint32_t entry;        /* what game code calls */
    int slot;              /* fast-table slot (-1: direct export) */
    int n_out;
    void (*gen)(in_t *, long);
    void (*rr)(uint32_t, const in_t *, out_t *);
    void (*pr)(const port_t *, const in_t *, out_t *);
    variant_t var[5];
} fn_t;

static const fn_t FNS[] = {
    {"vec3_normalize", "D3DXVec3Normalize", 0x4a1f74, 7, 3, g_vec3, r_vec3, p_vec3,
     {{0x4a5427, "c_"}, {0x4e2e31, "x86_"}, {0x4eaff0, "sse_"}, {0x4ecb60, "sse2-table"}}},
    {"mat4_mul", "D3DXMatrixMultiply", 0x4a2a10, 3, 16, g_mul, r_mul, p_mul,
     {{0x4a2a16, "c_"}, {0x4e2efb, "x86_"}, {0x4eaea0, "sse_"}, {0x4eca10, "sse2-table"}}},
    {"mat4_inverse", "D3DXMatrixInverse", 0x4a2f42, 18, 16, g_inv, r_inv, p_inv,
     {{0x4a2f48, "c_"}, {0x4eb240, "sse_"}, {0x4ecdb0, "sse2-table"}}},
    {"mat4_shadow", "D3DXMatrixShadow", 0x4a4461, 27, 16, g_shadow, r_shadow, p_shadow,
     {{0x4a5c86, "c_"}, {0x4ec0d0, "sse_"}, {0x4ed550, "sse2-table"}}},
    {"plane_from_point_normal", "D3DXPlaneFromPointNormal", 0x4a4f5f, 12, 4, g_pfpn, r_pfpn, p_pfpn,
     {{0x4a4f65, "c_"}}},
    {"mat4_rotation_x", "D3DXMatrixRotationX", 0x4a3553, 30, 16, g_angle, r_rot, p_rotx,
     {{0x4a3559, "c_"}, {0x4eb7b0, "sse_"}, {0x4ed280, "sse2-table"}}},
    {"mat4_rotation_y", "D3DXMatrixRotationY", 0x4a35ef, 31, 16, g_angle, r_rot, p_roty,
     {{0x4a35f5, "c_"}, {0x4eb830, "sse_"}, {0x4ed300, "sse2-table"}}},
    {"mat4_rotation_z", "D3DXMatrixRotationZ", 0x4a368c, 32, 16, g_angle, r_rot, p_rotz, {{0x4a3692, "c_"}}},
    {"mat4_translation", "D3DXMatrixTranslation", 0x4a34b0, 26, 16, g_f3, r_f3, p_trans, {{0x4a34b6, "c_"}}},
    {"mat4_scaling", "D3DXMatrixScaling", 0x4a3400, 33, 16, g_f3, r_f3, p_scale, {{0x4a3406, "c_"}}},
    {"mat4_lookat_rh", "D3DXMatrixLookAtRH", 0x4a3b52, -1, 16, g_lookat, r_lookat, p_lookat, {{0}}},
    {"mat4_perspective_fov_rh", "D3DXMatrixPerspectiveFovRH", 0x4a3ee8, -1, 16, g_persp, r_persp, p_persp, {{0}}},
    {"mat4_identity", "D3DXMatrixIdentity", 0x404bb8, -1, 16, g_none, r_ident, p_ident, {{0}}},
};
#define N_FNS ((int)(sizeof FNS / sizeof FNS[0]))

static const char *va_name(uint32_t va)
{
    for (int f = 0; f < N_FNS; f++)
        for (int v = 0; v < 5 && FNS[f].var[v].va; v++)
            if (FNS[f].var[v].va == va) return FNS[f].var[v].name;
    return "?";
}
static void dump_case(const char *tag, const fn_t *fn, const in_t *k, const out_t *r, const out_t *p)
{
    printf("    [%s] in:", tag);
    int n_in = fn->gen == g_mul || fn->gen == g_inv ? (fn->gen == g_mul ? 32 : 16) : 9;
    for (int j = 0; j < n_in; j++) printf(" %.9g", k->f[j]);
    printf("\n      retail ret=%d:", r->ret);
    for (int j = 0; j < fn->n_out; j++) printf(" %08x", r->o[j]);
    printf("\n      port   ret=%d:", p->ret);
    for (int j = 0; j < fn->n_out; j++) printf(" %08x%s", p->o[j], p->o[j] != r->o[j] ? "*" : "");
    printf("\n");
}
static int same(const fn_t *fn, const out_t *a, const out_t *b)
{
    return a->ret == b->ret && !memcmp(a->o, b->o, (size_t)fn->n_out * 4);
}

int main(int argc, char **argv)
{
    retail_image_t img;
    const char *exe = argc > 1 ? argv[1] : "recettear.unpacked.exe";
    long n_per = argc > 2 ? atol(argv[2]) : 100000;
    if (retail_map(exe, &img, 1)) return 1;
    printf("mapped %s @%p size %08x imports resolved=%d stubbed=%d\n", exe, img.base, img.size,
           img.n_imports_resolved, img.n_imports_stubbed);

    /* ---- CPU facts + retail's own dispatch init ---- */
    unsigned a, b, c, d; char vendor[13] = {0};
    __get_cpuid(0, &a, &b, &c, &d); memcpy(vendor, &b, 4); memcpy(vendor + 4, &d, 4); memcpy(vendor + 8, &c, 4);
    __get_cpuid(1, &a, &b, &c, &d);
    unsigned e1 = 0, ext = __get_cpuid_max(0x80000000u, NULL);
    if (ext >= 0x80000001u) { unsigned t0, t1, t2; __get_cpuid(0x80000001u, &t0, &t1, &t2, &e1); }
    printf("cpu %s sse=%u sse2=%u 3dnow=%u | IsProcessorFeaturePresent PF_XMMI(6)=%d PF_3DNOW(7)=%d PF_XMMI64(10)=%d\n",
           vendor, (d >> 25) & 1, (d >> 26) & 1, e1 >> 31, IsProcessorFeaturePresent(6),
           IsProcessorFeaturePresent(7), IsProcessorFeaturePresent(10));
    volatile uint32_t *slots = (volatile uint32_t *)(uintptr_t)0x5fda48;
    volatile int32_t *mode = (volatile int32_t *)(uintptr_t)0x5fe908;
    printf("before init: DAT_005fe908=%#x slot3(MatrixMultiply)=%08x (init thunk)\n", (unsigned)*mode, slots[3]);
    float m1[16], m2[16], m3[16];
    mat4_identity(m1); mat4_identity(m2);
    RFN(r_m3_t, 0x4a2a10)(m3, m1, m2);          /* first D3DX call: retail's lazy init runs */
    static const char *MODES[] = {"0 (c_+x86_ only)", "1 (x3d_/3DNow!)", "2 (sse2 table)", "3 (sse table)"};
    int md = *mode;
    printf("after first D3DXMatrixMultiply: DAT_005fe908 = %d = %s\n", md, md >= 0 && md < 4 ? MODES[md] : "?");
    for (int f = 0; f < N_FNS; f++)
        if (FNS[f].slot >= 0)
            printf("  slot %2d %-26s -> %08x %s\n", FNS[f].slot, FNS[f].d3dx_name, slots[FNS[f].slot],
                   va_name(slots[FNS[f].slot]));
    printf("  slot 21 D3DXPlaneNormalize         -> %08x\n", slots[21]);
    printf("D3DXCpuOptimizations(1) again -> %d (idempotent)\n", ((int(__stdcall *)(int))(uintptr_t)0x4cdd9f)(1));
    unsigned csr; __asm__ volatile("stmxcsr %0" : "=m"(csr)); printf("MXCSR=%08x\n", csr);

    /* ---- port vs dispatched retail, 3 x87 precisions ----
     * inputs are generated at PC53 (identical for every mode); only the calls run
     * under the tested CW.  PC53 retail outputs are digested per case (FNV-1a) for
     * the cross-machine compare (argv[3] dump, argv[4] reference). */
    unsigned int cw_modes[] = {_PC_53, _PC_24, _PC_64};
    const char *cw_names[] = {"PC53", "PC24", "PC64"};
    uint32_t *dig = calloc((size_t)N_FNS * (size_t)n_per + 1000, 4);
    long total_cases = 0, total_mis = 0;
    for (int m = 0; m < 3; m++) {
        for (int f = 0; f < N_FNS; f++) {
            const fn_t *fn = &FNS[f];
            const uint32_t va = fn->entry;                  /* what game code calls (jmp [slot] / direct) */
            long n = fn->gen == g_none ? 1000 : n_per, mis[2] = {0, 0};
            xs = 0x2545f491u + (uint32_t)f * 7919u;
            for (long i = 0; i < n; i++) {
                in_t k; memset(&k, 0, sizeof k); fn->gen(&k, i);
                out_t r, p[2]; unsigned int old;
                _controlfp_s(&old, cw_modes[m], _MCW_PC);
                prefill(&r); fn->rr(va, &k, &r);
                for (int g = 0; g < 2; g++) { prefill(&p[g]); fn->pr(&PORT[g], &k, &p[g]); }
                _controlfp_s(&old, _PC_53, _MCW_PC);
                if (m == 0 && i < n_per) {
                    uint32_t h = 2166136261u;
                    const uint8_t *q = (const uint8_t *)&r;
                    for (size_t j = 0; j < sizeof r; j++) h = (h ^ q[j]) * 16777619u;
                    dig[(size_t)f * (size_t)n_per + (size_t)i] = h;
                }
                for (int g = 0; g < 2; g++) {
                    if (!same(fn, &r, &p[g]) && mis[g]++ < 3) {
                        char tag[64]; snprintf(tag, sizeof tag, "%s %s %s #%ld", cw_names[m], fn->port_name, PORT[g].flag, i);
                        dump_case(tag, fn, &k, &r, &p[g]);
                    }
                }
            }
            total_cases += 2 * n; total_mis += mis[0] + mis[1];
            printf("RESULT %-4s %-24s retail %-26s @%08x: c11 %ld/%ld | gnu11 %ld/%ld bit-exact\n", cw_names[m],
                   fn->port_name, fn->d3dx_name, va, n - mis[0], n, n - mis[1], n);
        }
    }
    printf("TOTAL port-vs-retail comparisons %ld, mismatches %ld\n", total_cases, total_mis);

    /* ---- every concrete variant vs the dispatched one (PC53) ---- */
    unsigned int old; _controlfp_s(&old, _PC_53, _MCW_PC);
    printf("variant agreement with the dispatched implementation (PC53, %ld cases each):\n", n_per);
    for (int f = 0; f < N_FNS; f++) {
        const fn_t *fn = &FNS[f];
        if (!fn->var[0].va) continue;
        printf("  %-26s", fn->d3dx_name);
        for (int v = 0; v < 5 && fn->var[v].va; v++) {
            long agree = 0;
            xs = 0x2545f491u + (uint32_t)f * 7919u;
            for (long i = 0; i < n_per; i++) {
                in_t k; memset(&k, 0, sizeof k); fn->gen(&k, i);
                out_t r, q; prefill(&r); prefill(&q);
                fn->rr(fn->entry, &k, &r); fn->rr(fn->var[v].va, &k, &q);
                agree += same(fn, &r, &q);
            }
            printf(" | %s@%08x %ld/%ld", fn->var[v].name, fn->var[v].va, agree, n_per);
        }
        printf("\n");
    }

    /* ---- cross-machine: this machine's retail outputs vs a reference dump ---- */
    if (argc > 3 && strcmp(argv[3], "-")) {
        FILE *fo = fopen(argv[3], "wb");
        if (fo) { fwrite(&n_per, sizeof n_per, 1, fo); fwrite(dig, 4, (size_t)N_FNS * (size_t)n_per, fo); fclose(fo); }
        printf("wrote PC53 retail digests -> %s\n", argv[3]);
    }
    if (argc > 4) {
        FILE *fi = fopen(argv[4], "rb"); long rn = 0;
        size_t cnt = (size_t)N_FNS * (size_t)n_per;
        uint32_t *ref = calloc(cnt, 4);
        if (fi && fread(&rn, sizeof rn, 1, fi) == 1 && rn == n_per && fread(ref, 4, cnt, fi) == cnt) {
            printf("retail(this cpu) vs retail(reference %s), PC53, same inputs:\n", argv[4]);
            for (int f = 0; f < N_FNS; f++) {
                long n = FNS[f].gen == g_none ? 1000 : n_per, agree = 0;
                for (long i = 0; i < n; i++)
                    agree += dig[(size_t)f * (size_t)n_per + (size_t)i] == ref[(size_t)f * (size_t)n_per + (size_t)i];
                printf("  CROSS %-26s %ld/%ld identical\n", FNS[f].d3dx_name, agree, n);
            }
        } else printf("reference %s missing or different case count\n", argv[4]);
        free(ref);
        if (fi) fclose(fi);
    }
    return total_mis ? 2 : 0;
}
