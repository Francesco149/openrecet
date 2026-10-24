/*
 * scene1_effects.c — see scene1_effects.h.
 */
#include "scene1_effects.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "call_trace.h"
#include "dungeon_objects.h"     /* DAT_0695ef78 */
#include "math3d.h"
#include "sim.h"                 /* DAT_0438b8cc */
#include "sprite.h"
#include "party.h"               /* DAT_056db093 / 094 */
#include "scene1_camera.h"      /* DAT_0438cdf8 */
#include "scene1_records.h"     /* tables A / B (DAT_069b2f80 / DAT_069324b0) */
#include "sysassets.h"          /* effect_shot.bmp DAT_073cc940 */
#include "scene1_wide_followup.h" /* FUN_00415f2e */
#include "scene1_maplight.h"     /* the stage record (DAT_068dd2f0) */
#include "scene1_overlay.h"      /* FUN_00414ee2 */
#include "actor_table.h"         /* actor #0's weapon points (+0x6fc) */
#include "x87.h"
#include <string.h>

static sprite_t s_mahoujin;      /* DAT_073b18c8: bmp/mahoujin.bmp (128×128) */

/* a byte of the party block by its retail VA */
#define PWB(va) (((const uint8_t *)g_party.w)[(uint32_t)(va) - PARTY_VA])



void scene1_effects_load_dungeon(struct IDirect3DDevice8 *dev)
{
    if (s_mahoujin.tex == NULL)
        (void)sprite_load((IDirect3DDevice8 *)dev, "bmp/mahoujin.bmp", 128, 128, &s_mahoujin);
}

/* FVF 0x142: xyz, diffuse, uv — 24 bytes */
typedef struct { float x, y, z; uint32_t diffuse; float u, v; } fx_vtx;
static uint32_t fx_rgb(int32_t r, int32_t g, int32_t b);
static void fx_set_col(fx_vtx q[4], uint32_t c);

/* DAT_0064a2c8, quad 0 of FUN_0040d132's eight: the XZ-plane ±256 strip, its
 * 128-texel cell with the half-texel inset */
static fx_vtx s_floor_quad[4] = {
    { -256.0f, 0.0f,  256.0f, 0xffffffffu, 0.5f / 128.0f,   0.5f / 128.0f },
    { -256.0f, 0.0f, -256.0f, 0xffffffffu, 0.5f / 128.0f,   127.5f / 128.0f },
    {  256.0f, 0.0f,  256.0f, 0xffffffffu, 127.5f / 128.0f, 0.5f / 128.0f },
    {  256.0f, 0.0f, -256.0f, 0xffffffffu, 127.5f / 128.0f, 127.5f / 128.0f },
};

void scene1_effects_exit_circle(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (dev == NULL || s_mahoujin.tex == NULL) return;
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    for (int i = 0; i < DOBJ_COUNT; i++) {
        const dobj_t *o = &g_dobj[i];
        if (o->i[35] != 8 || o->i[154] == 3) continue;
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)s_mahoujin.tex);
        const float s = o->f[32] * 0.024f;
        float w[16], m[16];
        mat4_translation(w, o->f[0], o->f[1], o->f[2]);
        mat4_scaling(m, s, s, s);
        mat4_mul(w, m, w);
        mat4_rotation_y(m, (float)g_sim_frame_count * 0.02f);
        mat4_mul(w, m, w);
        for (int k = 0; k < 4; k++) s_floor_quad[k].diffuse = 0xffffffffu;   /* FUN_0040d11b */
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, s_floor_quad, sizeof(fx_vtx));
    }
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
}

/* DAT_0064c088: FUN_0040d132's eight XY billboard quads, cell k = the 32-texel
 * column 32·k of rows 160.5 .. 191.5 of a 256 texture */
static fx_vtx s_slash_quad[8][4];
/* DAT_0064bf68: the atlas quad whose UVs the caller sets */
static fx_vtx s_atlas_quad[4] = {
    { -256.0f,  256.0f, 0.0f, 0xffffffffu, 0.001953125f, 0.25195312f },
    { -256.0f, -256.0f, 0.0f, 0xffffffffu, 0.001953125f, 0.37304688f },
    {  256.0f,  256.0f, 0.0f, 0xffffffffu, 0.12304688f,  0.25195312f },
    {  256.0f, -256.0f, 0.0f, 0xffffffffu, 0.12304688f,  0.37304688f },
};

/* FUN_0040d132's other billboard quads (XY ±256 unless noted):
 * DAT_0064b488 + 96k: the 32×32 cell (32·(k & 7), 128 + 32·(k / 8));
 * DAT_006486f8 + 96k: the 32×64 cell (32·k, 64);
 * DAT_006488d8: x ±256, y 0..-512, the cell (0, 64)-(31.5, 120.5) flipped in v;
 * DAT_0064a7a8: the 32×128 cell (192, 0) */
static fx_vtx s_q488[16][4], s_q6f8[5][4], s_q8d8[4], s_qa7a8[4];
/* DAT_0064e398: the 64×64 cell (0, 0) */
static fx_vtx s_q398[4];
/* DAT_00648da8: the whole 128×128 texture, half-texel inset (snow.bmp) */
static fx_vtx s_q8da8[4] = {
    { -256.0f,  256.0f, 0.0f, 0xffffffffu, 0.00390625f, 0.00390625f },
    { -256.0f, -256.0f, 0.0f, 0xffffffffu, 0.00390625f, 0.99609375f },
    {  256.0f,  256.0f, 0.0f, 0xffffffffu, 0.99609375f, 0.00390625f },
    {  256.0f, -256.0f, 0.0f, 0xffffffffu, 0.99609375f, 0.99609375f },
};

static void fx_quad(fx_vtx q[4], float x0, float y0, float x1, float y1,
                    float ul, float vt, float ur, float vb)
{
    const fx_vtx v[4] = {
        { x0, y0, 0.0f, 0xffffffffu, ul, vt },
        { x0, y1, 0.0f, 0xffffffffu, ul, vb },
        { x1, y0, 0.0f, 0xffffffffu, ur, vt },
        { x1, y1, 0.0f, 0xffffffffu, ur, vb },
    };
    memcpy(q, v, sizeof v);
}

static void fx_init_quads(void)
{
    static int done;
    if (done) return;
    done = 1;
    for (int k = 0; k < 16; k++) {
        const float u = (float)(32 * (k & 7)), v = (float)(32 * (k / 8));
        fx_quad(s_q488[k], -256.0f, 256.0f, 256.0f, -256.0f, (u + 0.5f) / 256.0f, (v + 128.5f) / 256.0f,
                (u + 31.5f) / 256.0f, (v + 159.5f) / 256.0f);
    }
    for (int k = 0; k < 5; k++) {
        const float u = (float)(32 * k);
        fx_quad(s_q6f8[k], -256.0f, 256.0f, 256.0f, -256.0f, (u + 0.5f) / 256.0f, 0.25195312f,
                (u + 31.5f) / 256.0f, 0.49804688f);
    }
    fx_quad(s_q8d8, -256.0f, 0.0f, 256.0f, -512.0f, 0.001953125f, 0.47070312f, 0.12304688f, 0.25195312f);
    fx_quad(s_qa7a8, -256.0f, 256.0f, 256.0f, -256.0f, 0.75195312f, 0.001953125f, 0.87304688f, 0.49804688f);
    fx_quad(s_q398, -256.0f, 256.0f, 256.0f, -256.0f, 0.001953125f, 0.001953125f, 0.24804688f, 0.24804688f);
    for (int k = 0; k < 8; k++) {
        const float u0 = ((float)(32 * k) + 0.5f) / 256.0f, u1 = ((float)(32 * k) + 31.5f) / 256.0f;
        const fx_vtx q[4] = {
            { -256.0f,  256.0f, 0.0f, 0xffffffffu, u0, 0.62695312f },
            { -256.0f, -256.0f, 0.0f, 0xffffffffu, u0, 0.74804688f },
            {  256.0f,  256.0f, 0.0f, 0xffffffffu, u1, 0.62695312f },
            {  256.0f, -256.0f, 0.0f, 0xffffffffu, u1, 0.74804688f },
        };
        memcpy(s_slash_quad[k], q, sizeof q);
    }
}

/* a dword of the record a table-B owner field points at (the port keeps
 * them as host pointers, retail as VAs) */
static int32_t owner_i32(const int32_t *r, int field, int off)
{
    const uintptr_t p = (uintptr_t)(uint32_t)r[field];
    if (p == 0) return 0;
    int32_t v;
    memcpy(&v, (const char *)p + off, 4);
    return v;
}

/* FUN_004176ff 0x417a6c..0x41848b: the weapon-swing trails of table B — pass 0
 * (ZERO / INVSRCCOLOR: 111, 112, 23) then pass 1 (ONE / ONE: 16, 2, 84, 3, 4,
 * 34, 103, 109, 110, 22).  A record r draws, for k in 0..99 within the last
 * 50 of t = r[38] · 100 / n (n: 8 / 10 / 12 / 16 by type), a quad on an arc:
 * a = k / 99 over the type's sweep, the point (0, cos a · (R + 0.5), sin a · R)
 * turned by RotZ(±π/2 by the owner's facing) · RotY(r[36]) (its translation
 * row, [ebp-0x8c]), scaled by its rank ((rank / count (· 0.5 for 109..112,
 * 22, 23) + 0.2) · base · 1.8, faded past k 75); world RotZ(r[38] · 0.2) · B ·
 * S · T(arc point + r pos). */
static void fx_swing_trails(IDirect3DDevice8 *dev)
{
    static const float khpi = 1.5707963705062866f;
    fx_init_quads();
    int cell = 0;          /* v445: set by some types, carried over by 3 / 4 */
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ONE);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        }
        for (int i = 0; i < g_scene1_records_b_count && i < SCENE1_RECORDS_B_COUNT; i++) {
            const int32_t *r = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
            const int32_t t = r[0];
            if (t == 0) continue;
            if (pass == 1) {
                if (!(t == 16 || t == 2 || t == 84 || t == 3 || t == 4 || t == 34 || t == 103 ||
                      t == 109 || t == 110 || t == 22)) continue;
            } else if (!(t == 111 || t == 112 || t == 23)) continue;
            float f66, f29, f30, f31, f36;
            memcpy(&f66, &r[66], 4); memcpy(&f29, &r[29], 4); memcpy(&f30, &r[30], 4);
            memcpy(&f31, &r[31], 4); memcpy(&f36, &r[36], 4);
            float base = spill32((long double)f66 * 0.0018f);
            if (t == 16 || t == 2 || t == 109 || t == 111 || t == 22 || t == 23 || t == 103) cell = 0;
            if (t == 110 || t == 112 || t == 84 || t == 34) cell = 1;
            if (t == 84 || t == 2 || t == 109 || t == 110 || t == 111 || t == 112)
                base = spill32((long double)f66 * 0.0024999999f);
            if (t == 34 || t == 103) base = spill32((long double)f66 * 0.003f);
            int n = 10;
            if (t == 16 || t == 3) n = 8;
            if (t == 109 || t == 110 || t == 111 || t == 112 || t == 22 || t == 23) n = 12;
            if (t == 34 || t == 103) n = 16;
            const int narrow = t == 109 || t == 110 || t == 111 || t == 112 || t == 22 || t == 23;
            /* the arc's frame: RotZ(±π/2 by the owner's facing) · RotY(r[36]) */
            float rz = 0.0f;
            int has_rz = 1;
            if (t == 16) {
                const int32_t d = owner_i32(r, 5, 24);
                rz = (d >= 5 && d <= 7) ? khpi : (d >= 1 && d <= 3) ? -khpi : khpi;
            } else if (t == 2 || t == 84 || t == 109 || t == 110 || t == 111 || t == 112) {
                const int32_t d = owner_i32(r, 4, 0x948);
                rz = (d >= 5 && d <= 7) ? -khpi : khpi;
            } else if (t == 22 || t == 23) {
                const int32_t d = owner_i32(r, 5, 24);
                rz = (d >= 5 && d <= 7) ? -khpi : khpi;
            } else if (t == 3) {
                const int32_t d = owner_i32(r, 4, 0x948);
                rz = (d >= 5 && d <= 7) ? khpi : (d >= 1 && d <= 3) ? -khpi : khpi;
            } else if (t == 34 || t == 103) {
                rz = khpi;
            } else {
                has_rz = 0;
            }
            float arc[16], m[16];
            mat4_rotation_y(arc, f36);
            mat4_rotation_x(m, 0.0f);
            mat4_mul(arc, m, arc);
            if (has_rz) {
                mat4_rotation_z(m, rz);
                mat4_mul(arc, m, arc);
            }
            float R = 2.0f;
            if (t == 84 || t == 2 || t == 109 || t == 110) R = 2.5f;
            if (t == 111 || t == 112 || t == 23) R = 3.5f;
            if (t == 109 || t == 110) R = 3.5f;
            if (t == 34) R = 3.0f;
            if (t == 103) R = 4.5f;
            const float per = spill32(100.0L / (float)n);
            const float hi = spill32((long double)(float)r[38] * per);
            const float lo = spill32((long double)hi - 50.0f);
            int cnt = 1;
            for (int k = 0; k < 100; k++)
                if ((double)k >= (double)lo && (double)k <= (double)hi) cnt++;
            int rank = 0;
            for (int k = 0; k < 100; k++) {
                if (!(lo <= (float)k && (double)k <= (double)hi)) continue;
                rank++;
                float sc;
                if (narrow) {
                    sc = spill32((long double)(float)rank * 0.5L / (float)cnt);
                    sc = spill32(((long double)sc + 0.2f) * base * 1.8f);
                    if (k > 75) sc = spill32((25.0L - ((long double)k - 75.0f)) / 25.0f * sc);
                    if (sc < 0.00050000002f) sc = 0.00050000002f;
                } else {
                    sc = spill32((long double)(float)rank / (float)cnt);
                    sc = spill32(((long double)sc + 0.2f) * base * 1.8f);
                    if (k > 75) sc = spill32((25.0L - ((long double)k - 75.0f)) / 25.0f * sc);
                }
                const float u = spill32((long double)(float)k / 99.0L);
                float ang = spill32((long double)u * 4.0840702f - 1.5707964f);
                if (t == 3 || t == 16) ang = spill32((long double)u * 2.5132742f);
                else if (narrow) ang = spill32((long double)u * 3.7699115f - 0.94247788f);
                else if (t == 4) ang = spill32((long double)u * 1.8849558f - 0.31415927f);
                else if (t == 34 || t == 103) ang = spill32((long double)u * 7.853982f - 0.31415927f);
                const float py = spill32(x87_crt_cos((double)ang) * (R + 0.5f));
                const float pz = spill32(x87_crt_sin((double)ang) * R);
                /* the translation row of T(0, py, pz) · arc */
                const float ox = py * arc[4] + pz * arc[8];
                const float oy = py * arc[5] + pz * arc[9];
                const float oz = py * arc[6] + pz * arc[10];
                float w[16];
                mat4_translation(w, f29 + ox, f30 + oy, f31 + oz);
                mat4_scaling(m, sc, sc, sc);
                mat4_mul(w, m, w);
                mat4_mul(w, g_scene1_camera_orient, w);
                mat4_rotation_z(m, spill32((long double)(float)r[38] * 0.2f));
                mat4_mul(w, m, w);
                IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
                if (PWB(0x56db093u) != 0 || PWB(0x56db094u) != 0) {
                    int c = r[38] - rank;
                    if (c < 0) c = 0;
                    if (c > 7) c = 7;
                    const float row = PWB(0x56db094u) != 0 ? 192.0f : 224.0f;
                    const float u0 = ((float)(32 * c) + 0.5f) / 256.0f, u1 = ((float)(32 * c) + 31.5f) / 256.0f;
                    const float v0 = (row + 0.5f) / 256.0f, v1 = (row + 31.5f) / 256.0f;
                    s_atlas_quad[0].u = u0; s_atlas_quad[1].u = u0; s_atlas_quad[2].u = u1; s_atlas_quad[3].u = u1;
                    s_atlas_quad[0].v = v0; s_atlas_quad[1].v = v1; s_atlas_quad[2].v = v0; s_atlas_quad[3].v = v1;
                    for (int q = 0; q < 4; q++) s_atlas_quad[q].diffuse = 0xffffffffu;
                    IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, s_atlas_quad, sizeof(fx_vtx));
                } else {
                    for (int q = 0; q < 4; q++) s_slash_quad[cell][q].diffuse = 0xffffffffu;
                    IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, s_slash_quad[cell], sizeof(fx_vtx));
                }
            }
        }
    }
}

/* ── FUN_0040f892: the type-148 weapon trail (a boss's swing) ──────────────
 * Run at the top of FUN_004176ff, so it ticks only on rendered frames: per
 * table-A record of type 148 (all 4096), actor #0's two weapon points
 * (+0x6fc..+0x710, its draw-written anchors) are pushed into an 8-deep
 * history per coordinate (DAT_0064e458 + 0x20·c); from age 2 the strip
 * DAT_0064a828 (56 vertex pairs) is rebuilt along natural cubic splines of
 * the last min(age + 1, 8) points (FUN_0040f6cb / FUN_0040f7f3); then the
 * record ages and dies at 18. */
static float s_trail_hist[6][8];   /* DAT_0064e458 */
static int32_t s_trail_n = 8;      /* DAT_0052a7f8 */
static fx_vtx s_trail[112];        /* DAT_0064a828 */
static int s_trail_init;

static void trail_init(void)
{
    if (s_trail_init) return;
    s_trail_init = 1;
    for (int k = 0; k < 56; k++) {
        const float v = spill32(((long double)k * 0.57407409f + 96.5L) / 256.0L);
        const fx_vtx a = { 0.0f, -32.0f, 0.0f, 0xffffffffu, 0.13867188f, v };
        const fx_vtx b = { 0.0f, 32.0f, 0.0f, 0xffffffffu, 0.23632812f, v };
        s_trail[2 * k] = a;
        s_trail[2 * k + 1] = b;
    }
}

/* FUN_0040f6cb: the second-difference coefficients z of y (uniform spacing,
 * diagonal 4, off-diagonals h = 1) */
static void trail_spline(const float *y, float *z)
{
    static float h[8], d[8];   /* DAT_0064a2a8, DAT_0064a80c */
    const int n = s_trail_n;
    z[0] = 0.0f;
    z[n - 1] = 0.0f;
    if (n - 1 > 0)
        for (int k = 0; k < n - 1; k++) {
            h[k] = 1.0f;
            d[k] = spill32((long double)y[k + 1] - y[k]);
        }
    z[1] = spill32((long double)d[1] - d[0] - (long double)h[0] * z[0]);
    d[0] = 4.0f;
    for (int k = 1; k <= n - 3; k++) {
        const float r = spill32((long double)h[k] / d[k - 1]);
        const long double dd = (long double)d[k + 1] - d[k];
        z[k + 1] = spill32(dd - (long double)r * z[k]);
        d[k] = spill32(4.0L - (long double)r * h[k]);
    }
    z[n - 2] = spill32((long double)z[n - 2] - (long double)h[n - 2] * z[n - 1]);
    for (int m = n - 2; m > 0; m--)
        z[m] = spill32(((long double)z[m] - (long double)h[m] * z[m + 1]) / d[m - 1]);
}

/* FUN_0040f7f3: y at t from the segment around t */
static float trail_eval(float t, const float *y, const float *z)
{
    int lo = 0, hi = s_trail_n - 1, i = 0;
    if (s_trail_n - 1 > 0) {
        do {
            const int mid = (hi + lo) / 2;
            if ((float)mid >= t) hi = mid;
            else lo = mid + 1;
        } while (lo < hi);
        i = lo;
        if (lo > 0) i = --lo;
    }
    const float sg = spill32((long double)t - (float)i);
    return spill32(((long double)y[i + 1] - y[i] - ((long double)z[i] + z[i] + z[i + 1])
                    + (((long double)z[i + 1] - z[i]) * sg + (long double)z[i] * 3.0L) * sg) * sg + y[i]);
}

void scene1_effects_tick_148(void)
{
    trail_init();
    for (int i = 0; i < SCENE1_RECORDS_A_COUNT; i++) {
        int32_t *r = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        if (r[12] != 148) continue;
        for (int c = 0; c < 6; c++) {
            for (int k = 7; k > 0; k--) s_trail_hist[c][k] = s_trail_hist[c][k - 1];
            s_trail_hist[c][0] = act_getf(&g_actor[0], 0x6fc + 4 * c);
        }
        const int32_t age = r[13];
        if (age >= 2) {
            s_trail_n = age + 1 > 8 ? 8 : age + 1;
            for (int k = 0; k < 56; k++) {
                const float t = spill32((long double)spill32((float)s_trail_n) / 7.0L / 8.0L * spill32((float)k));
                float out[6];
                for (int c = 0; c < 6; c++) {
                    float z[8];
                    trail_spline(s_trail_hist[c], z);
                    out[c] = trail_eval(t, s_trail_hist[c], z);
                }
                s_trail[2 * k].x = out[0];     s_trail[2 * k].y = out[1];     s_trail[2 * k].z = out[2];
                s_trail[2 * k + 1].x = out[3]; s_trail[2 * k + 1].y = out[4]; s_trail[2 * k + 1].z = out[5];
            }
        }
        if (++r[13] >= 18) r[12] = -1;
    }
}

/* FUN_004176ff's table-A loop, type 148: the trail strip DAT_0064a828 (110
 * triangles) at the origin from age 3, grey (age - 2) · 64 to 255, closing
 * 64 a tick from 14 */
static void fx_trail_148(IDirect3DDevice8 *dev, const int32_t *r)
{
    const int32_t age = r[13];
    if (age < 3) return;
    int32_t c = (age - 2) << 6;
    if (c > 255) c = 255;
    if (age >= 14) c += (14 - age) << 6;
    fx_set_col(s_trail, fx_rgb(c, c, c));
    for (int k = 4; k < 112; k++) s_trail[k].diffuse = s_trail[0].diffuse;
    float w[16];
    mat4_translation(w, 0.0f, 0.0f, 0.0f);
    IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
    IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 110, s_trail, sizeof(fx_vtx));
}

/* the engine's colour idiom v | ((g | ((r | 0xffffff00) << 8)) << 8) on ints
 * (a negative component smears its high bits, as retail's does) */
static uint32_t fx_rgb(int32_t r, int32_t g, int32_t b)
{
    return (uint32_t)b | (((uint32_t)g | (((uint32_t)r | 0xffffff00u) << 8)) << 8);
}

static void fx_set_col(fx_vtx q[4], uint32_t c)
{
    for (int k = 0; k < 4; k++) q[k].diffuse = c;
}

/* DAT_0064bf68's UVs: v0 (ua, va), v1 (ua, vb), v2 (ub, va), v3 (ub, vb) */
static void fx_set_uv(float ua, float ub, float va, float vb)
{
    s_atlas_quad[0].u = ua; s_atlas_quad[0].v = va;
    s_atlas_quad[1].u = ua; s_atlas_quad[1].v = vb;
    s_atlas_quad[2].u = ub; s_atlas_quad[2].v = va;
    s_atlas_quad[3].u = ub; s_atlas_quad[3].v = vb;
}

/* the retail templates FUN_004176ff's table-A loop draws that the other loops
 * don't share (static after the template init; values read from a retail dump):
 * DAT_006483c8: a 128-wide arch of 5 rows (y 128..192, z ±256), the cell
 *   (192, 0)-(223.5, 127.5);
 * DAT_0064a748: the 64×64 cell (64, 64);  DAT_0064e3f8: the XZ ±256 quad on the
 *   cell (224, 0)-(255.5, 64.5);
 * DAT_0064e638: a 64-wide arc of 10 rows (radius 128 round x, 0 .. 0.78π), the
 *   cell (128, 0)-(191.5, 127.5);
 * DAT_0064a0c8: a 64-high ring of 10 columns (radius 128 round y), u 64.5 ..
 *   127.5 across, v 0 .. 31.5;
 * DAT_0064bfc8 / DAT_0064c028: the star cells (64 / 96, 32), 28 wide;
 * DAT_00649d08: the ring strip whose positions and colours the type-65 arm
 *   writes, u alternating 68.5 / 125, v 64.5 .. 95.5 */
static fx_vtx s_q3c8[10] = {
    { -64.0f, 128.0f, 256.0f, 0xffffffffu, 0.751953125f, 0.001953125f },
    { 64.0f, 128.0f, 256.0f, 0xffffffffu, 0.873046875f, 0.001953125f },
    { -64.0f, 173.254837f, 128.0f, 0xffffffffu, 0.751953125f, 0.125976562f },
    { 64.0f, 173.254837f, 128.0f, 0xffffffffu, 0.873046875f, 0.125976562f },
    { -64.0f, 192.0f, 0.0f, 0xffffffffu, 0.751953125f, 0.25f },
    { 64.0f, 192.0f, 0.0f, 0xffffffffu, 0.873046875f, 0.25f },
    { -64.0f, 173.254837f, -128.0f, 0xffffffffu, 0.751953125f, 0.374023438f },
    { 64.0f, 173.254837f, -128.0f, 0xffffffffu, 0.873046875f, 0.374023438f },
    { -64.0f, 127.999992f, -256.0f, 0xffffffffu, 0.751953125f, 0.498046875f },
    { 64.0f, 127.999992f, -256.0f, 0xffffffffu, 0.873046875f, 0.498046875f },
};
static fx_vtx s_qa748[4] = {
    { -256.0f, 256.0f, 0.0f, 0xffffffffu, 0.251953125f, 0.251953125f },
    { -256.0f, -256.0f, 0.0f, 0xffffffffu, 0.251953125f, 0.498046875f },
    { 256.0f, 256.0f, 0.0f, 0xffffffffu, 0.498046875f, 0.251953125f },
    { 256.0f, -256.0f, 0.0f, 0xffffffffu, 0.498046875f, 0.498046875f },
};
static fx_vtx s_q3f8[4] = {
    { -256.0f, 0.0f, 256.0f, 0xffffffffu, 0.876953125f, 0.001953125f },
    { -256.0f, 0.0f, -256.0f, 0xffffffffu, 0.876953125f, 0.251953125f },
    { 256.0f, 0.0f, 256.0f, 0xffffffffu, 0.998046875f, 0.001953125f },
    { 256.0f, 0.0f, -256.0f, 0xffffffffu, 0.998046875f, 0.251953125f },
};
static fx_vtx s_q638[20] = {
    { -32.0f, 0.0f, 128.0f, 0xffffffffu, 0.501953125f, 0.001953125f },
    { 32.0f, 0.0f, 128.0f, 0xffffffffu, 0.748046875f, 0.001953125f },
    { -32.0f, 34.7443771f, 123.194267f, 0xffffffffu, 0.501953125f, 0.0570746511f },
    { 32.0f, 34.7443771f, 123.194267f, 0xffffffffu, 0.748046875f, 0.0570746511f },
    { -32.0f, 66.8798141f, 109.137939f, 0xffffffffu, 0.501953125f, 0.112196177f },
    { 32.0f, 66.8798141f, 109.137939f, 0xffffffffu, 0.748046875f, 0.112196177f },
    { -32.0f, 93.9932785f, 86.8864975f, 0xffffffffu, 0.501953125f, 0.167317703f },
    { 32.0f, 93.9932785f, 86.8864975f, 0xffffffffu, 0.748046875f, 0.167317703f },
    { -32.0f, 114.048836f, 58.1107864f, 0xffffffffu, 0.501953125f, 0.222439229f },
    { 32.0f, 114.048836f, 58.1107864f, 0xffffffffu, 0.748046875f, 0.222439229f },
    { -32.0f, 125.540512f, 24.9715652f, 0xffffffffu, 0.501953125f, 0.277560771f },
    { 32.0f, 125.540512f, 24.9715652f, 0xffffffffu, 0.748046875f, 0.277560771f },
    { -32.0f, 127.605415f, -10.0427589f, 0xffffffffu, 0.501953125f, 0.332682282f },
    { 32.0f, 127.605415f, -10.0427589f, 0xffffffffu, 0.748046875f, 0.332682282f },
    { -32.0f, 120.088493f, -44.3029785f, 0xffffffffu, 0.501953125f, 0.387803793f },
    { 32.0f, 120.088493f, -44.3029785f, 0xffffffffu, 0.748046875f, 0.387803793f },
    { -32.0f, 103.554176f, -75.2365036f, 0xffffffffu, 0.501953125f, 0.442925334f },
    { 32.0f, 103.554176f, -75.2365036f, 0xffffffffu, 0.748046875f, 0.442925334f },
    { -32.0f, 79.2440186f, -100.520569f, 0xffffffffu, 0.501953125f, 0.498046875f },
    { 32.0f, 79.2440186f, -100.520569f, 0xffffffffu, 0.748046875f, 0.498046875f },
};
static fx_vtx s_q0c8[20] = {
    { 0.0f, -32.0f, 128.0f, 0xffffffffu, 0.251953125f, 0.001953125f },
    { 0.0f, 32.0f, 128.0f, 0xffffffffu, 0.251953125f, 0.123046875f },
    { 82.2768173f, -32.0f, 98.053688f, 0xffffffffu, 0.279296875f, 0.001953125f },
    { 82.2768173f, 32.0f, 98.053688f, 0xffffffffu, 0.279296875f, 0.123046875f },
    { 126.055397f, -32.0f, 22.2269573f, 0xffffffffu, 0.306640625f, 0.001953125f },
    { 126.055397f, 32.0f, 22.2269573f, 0xffffffffu, 0.306640625f, 0.123046875f },
    { 110.85125f, -32.0f, -64.0000076f, 0xffffffffu, 0.333984375f, 0.001953125f },
    { 110.85125f, 32.0f, -64.0000076f, 0xffffffffu, 0.333984375f, 0.123046875f },
    { 43.7785606f, -32.0f, -120.280663f, 0xffffffffu, 0.361328125f, 0.001953125f },
    { 43.7785606f, 32.0f, -120.280663f, 0xffffffffu, 0.361328125f, 0.123046875f },
    { -43.7785797f, -32.0f, -120.280655f, 0xffffffffu, 0.388671875f, 0.001953125f },
    { -43.7785797f, 32.0f, -120.280655f, 0xffffffffu, 0.388671875f, 0.123046875f },
    { -110.851257f, -32.0f, -63.9999886f, 0xffffffffu, 0.416015625f, 0.001953125f },
    { -110.851257f, 32.0f, -63.9999886f, 0xffffffffu, 0.416015625f, 0.123046875f },
    { -126.055389f, -32.0f, 22.226963f, 0xffffffffu, 0.443359375f, 0.001953125f },
    { -126.055389f, 32.0f, 22.226963f, 0xffffffffu, 0.443359375f, 0.123046875f },
    { -82.2767868f, -32.0f, 98.0537109f, 0xffffffffu, 0.470703125f, 0.001953125f },
    { -82.2767868f, 32.0f, 98.0537109f, 0xffffffffu, 0.470703125f, 0.123046875f },
    { 2.23802308e-05f, -32.0f, 128.0f, 0xffffffffu, 0.498046875f, 0.001953125f },
    { 2.23802308e-05f, 32.0f, 128.0f, 0xffffffffu, 0.498046875f, 0.123046875f },
};
static fx_vtx s_qstar[2][4] = {
    { { -256.0f, 256.0f, 0.0f, 0xffffffffu, 0.251953125f, 0.126953125f },
      { -256.0f, -256.0f, 0.0f, 0xffffffffu, 0.251953125f, 0.248046875f },
      { 256.0f, 256.0f, 0.0f, 0xffffffffu, 0.361328125f, 0.126953125f },
      { 256.0f, -256.0f, 0.0f, 0xffffffffu, 0.361328125f, 0.248046875f } },
    { { -256.0f, 256.0f, 0.0f, 0xffffffffu, 0.376953125f, 0.126953125f },
      { -256.0f, -256.0f, 0.0f, 0xffffffffu, 0.376953125f, 0.248046875f },
      { 256.0f, 256.0f, 0.0f, 0xffffffffu, 0.486328125f, 0.126953125f },
      { 256.0f, -256.0f, 0.0f, 0xffffffffu, 0.486328125f, 0.248046875f } },
};
static fx_vtx s_ring[20];

/* FUN_00415eb4: Inverse(LookAtRH(pos, pos + vel, eye - pos)) — the quad's +y
 * along the record's velocity, facing the eye (a singular one stays the
 * look-at, as D3DX leaves it) */
static void fx_face_vel(float w[16], const float *f)
{
    const float eye[3] = { f[0], f[1], f[2] };
    const float at[3] = { spill32((long double)f[3] + f[0]), spill32((long double)f[4] + f[1]),
                          spill32((long double)f[5] + f[2]) };
    const float up[3] = { spill32((long double)g_scene1_camera_eye[0] - f[0]),
                          spill32((long double)g_scene1_camera_eye[1] - f[1]),
                          spill32((long double)g_scene1_camera_eye[2] - f[2]) };
    mat4_lookat_rh(w, eye, at, up);
    (void)mat4_inverse(w, w);
}

/* the streak worlds: fx_face_vel · RotX(π/2) · S(w, l, 2l) (left-multiplied) */
static void fx_streak(float w[16], const float *f, float wid, float len)
{
    float m[16];
    fx_face_vel(w, f);
    mat4_rotation_x(m, 1.5707963705062866f);
    mat4_mul(w, m, w);
    mat4_scaling(m, wid, len, spill32((long double)len + len));
    mat4_mul(w, m, w);
}

/* the billboards: B · S(s) · T(pos), then RotZ(+0x20) when `spin` */
static void fx_bill(float w[16], const float *f, float s, int spin)
{
    float m[16];
    mat4_translation(w, f[0], f[1], f[2]);
    mat4_scaling(m, s, s, s);
    mat4_mul(w, m, w);
    mat4_mul(w, g_scene1_camera_orient, w);
    if (spin) {
        mat4_rotation_z(m, f[8]);
        mat4_mul(w, m, w);
    }
}

/* T(pos), then the record's rotations in `order` ('x' / 'y' / 'z' → +0x18 /
 * +0x1c / +0x20), each left-multiplied */
static void fx_rot(float w[16], const float *f, const char *order)
{
    float m[16];
    mat4_translation(w, f[0], f[1], f[2]);
    for (; *order; order++) {
        if (*order == 'x') mat4_rotation_x(m, f[6]);
        else if (*order == 'y') mat4_rotation_y(m, f[7]);
        else mat4_rotation_z(m, f[8]);
        mat4_mul(w, m, w);
    }
}

static void fx_scale(float w[16], float sx, float sy, float sz)
{
    float m[16];
    mat4_scaling(m, sx, sy, sz);
    mat4_mul(w, m, w);
}

static void fx_draw(IDirect3DDevice8 *dev, const float w[16], fx_vtx *q, int tris)
{
    IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
    IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, (UINT)tris, q, sizeof(fx_vtx));
}

static void fx_col_n(fx_vtx *q, int n, uint32_t c)
{
    for (int k = 0; k < n; k++) q[k].diffuse = c;
}

/* the 32-texel frame `fr` of the atlas cell row v0 .. v1 into DAT_0064bf68 */
static void fx_atlas_frame(int32_t fr, float v0, float v1)
{
    const float cu = spill32((float)(32 * fr));
    fx_set_uv(spill32(((long double)cu + 0.5f) / 256.0f), spill32(((long double)cu + 31.5f) / 256.0f), v0, v1);
}

/* FUN_004176ff 0x41b147: the types 65 / 97 / 114 — four rising rings (the
 * pairs 14 ticks apart), each a 9-segment strip DAT_00649d08 of radius 64
 * (odd rings 96 out, 64·s + 96 in) and height 160·s (odd 96·s), s = (phase %
 * 28) · scale · 0.08, grey 96 closing past 16 (· the fade past 30 / 230 for
 * 97), green / grey / red / blue by +0x3c; world S(scale · 0.025) · RotY(age ·
 * 0.03) · T */
static void fx_rings(IDirect3DDevice8 *dev, const int32_t *r, const float *f)
{
    if (s_ring[0].u == 0.0f)
        for (int k = 0; k < 20; k++) {
            s_ring[k].u = (k & 2) ? 0.48828125f : 0.267578125f;
            s_ring[k].v = (k & 1) ? 0.373046875f : 0.251953125f;
        }
    const int32_t t = r[12];
    float w[16], m[16];
    const float s0 = spill32((long double)f[14] * 0.025f);
    mat4_translation(w, f[0], f[1], f[2]);
    mat4_rotation_y(m, spill32((long double)spill32((float)r[13]) * 0.03f));
    mat4_mul(w, m, w);
    fx_scale(w, s0, s0, s0);
    IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
    float fade = 1.0f;
    const int32_t past = t == 97 ? 230 : 30;
    if (r[13] > past) fade = spill32(1.0f - (long double)spill32((float)(r[13] - past)) * 0.05f);
    for (int k = 0; k < 4; k++) {
        int32_t a = r[13];
        if (k < 2) a -= 14;
        if (a < 0) continue;
        int32_t c = 96;
        const float s = spill32((long double)spill32((float)(a % 28)) * f[14] * 0.08f);
        if (a % 28 > 16) c = 8 * (28 - a % 28);
        c = x87_ftol((long double)spill32((float)c) * fade);
        if (c <= 0) continue;
        float ro = 64.0f, ri = 64.0f, y = spill32((long double)s * 160.0f);
        if (k & 1) {
            ro = 96.0f;
            ri = spill32((long double)s * 64.0f + 96.0f);
            y = spill32((long double)s * 96.0f);
        }
        uint32_t col;
        switch (r[15]) {
        case 0:  col = fx_rgb(c / 2, c, c / 2); break;
        case 1:  col = fx_rgb(c, c, c); break;
        case 2:  col = fx_rgb(c, c / 2, c / 2); break;
        default: col = fx_rgb(c / 2, c / 2, c); break;
        }
        for (int j = 0; j < 10; j++) {
            const float ang = spill32((long double)spill32((float)j) * 6.2831855f / 9.0f);
            const float sn = spill32(x87_crt_sin(ang)), cs = spill32(x87_crt_cos(ang));
            fx_vtx *v = &s_ring[2 * j];
            v[0].x = spill32((long double)sn * ri);
            v[0].y = y;
            v[0].z = spill32((long double)cs * ri);
            v[1].x = spill32((long double)sn * ro);
            v[1].y = 0.0f;
            v[1].z = spill32((long double)cs * ro);
            v[0].diffuse = v[1].diffuse = col;
        }
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 18, s_ring, sizeof(fx_vtx));
    }
}

/* the colour of the types 19 / 20: grey sin(age · 0.1) · 32 + 128, faded by
 * (60 - +0x44) / 60 while +0x44 > 0 */
static uint32_t fx_pulse(const int32_t *r)
{
    const float x = spill32((long double)spill32((float)r[13]) * 0.1f);
    int32_t c = x87_ftol(x87_crt_sin(x) * 32.0f + 128.0f);
    if (r[17] > 0) {
        const float k = spill32((long double)spill32((float)(60 - r[17])) / 60.0f);
        c = x87_ftol((long double)k * spill32((float)c));
    }
    return fx_rgb(c, c, c);
}

/* FUN_004176ff's table-A loop (0x41b07f..0x41e177): every record in slot
 * order, on effect.bmp (bound before the table-B projectile loop) under that
 * loop's ONE / ONE — 148 the trail strip, 65 / 97 / 114 the rings, then by type:
 *   streaks along the velocity (fx_streak): 1 / 94 / 2 / 3 / 82 / 64 / 83 / 93
 *     / 102 / 115 / 119, 105, 99 / 120, 53, 44 (grey by age);  41 un-turned on
 *     the XZ cell DAT_0064e3f8;  67 a Y-turned quad;
 *   billboards (fx_bill, sized by scale and age): 54 / 78 (shrinking over the
 *     last 8 of +0x40), 63 (swaying sin(age · 0.1) · 300), 52, 32 (the
 *     element's cell by DAT_056dae5c), 31 / 100 / 109 / 101 / 104 / 108 (the
 *     wing glows), 34 / 60 / 45 (the stars, sin-pulsed), 5 / 92 / 111 / 10 /
 *     11 / 12 (the 8-frame bursts), 25 (blue) / 68 (DAT_0064e398), 28, 4 / 112
 *     / 152, 19 (pulsing);
 *   turned meshes: 50 / 62 / 51 / 81 / 77 / 87 the arc DAT_0064e638, 61 the
 *     ring DAT_0064a0c8, 33 the arch DAT_006483c8 (tinted by DAT_056dae5c,
 *     sized by DAT_056dae84 or its table-B owner's timer), 35 / 20 flat;
 *   110 twelve bobbing motes, 30 / 46 eight blades round the record */
static void fx_table_a_main(IDirect3DDevice8 *dev)
{
    static const float k_pi = 3.1415927410125732f, k_hpi = 1.5707963705062866f;
    static const float k_2pi = 6.2831854820251465f;
    fx_init_quads();
    if (g_sysassets.effect_bmp.tex == NULL) return;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_sysassets.effect_bmp.tex);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    for (int i = 0; i < g_scene1_records_a_count && i < SCENE1_RECORDS_A_COUNT; i++) {
        const int32_t *r = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        const int32_t t = r[12], age = r[13];
        if (t == -1) continue;
        float f[18];
        memcpy(f, r, sizeof f);
        const float sc = f[14];
        float w[16];
        if (t == 148) {
            fx_trail_148(dev, r);
            continue;
        }
        if (t == 65 || t == 97 || t == 114) {
            fx_rings(dev, r, f);
            continue;
        }
        switch (t) {
        case 1: case 94: case 2: case 3: case 82: case 64: case 83: case 93: case 102: case 115:
        case 119: {
            if (age < 0) continue;
            float len = 0.015f, wid = 0.02f;
            if (t == 93) {
                if (age > 16) {
                    wid = spill32(0.02f - (long double)spill32((float)(age - 16)) * 0.001f);
                    if (wid <= 0.0f) continue;
                }
            } else {
                if (t == 94) {
                    wid = 0.1f;
                    if (age > 8) {
                        const float d = spill32((float)(age - 8));
                        wid = spill32(0.1f - (long double)d * 0.01f);
                        len = spill32((long double)d * 0.01f + 0.015f);
                    }
                    if (wid <= 0.0f) continue;
                } else if (age > 8) {
                    wid = spill32(0.02f - (long double)spill32((float)(age - 8)) * 0.001f);
                    if (wid <= 0.0f) continue;
                }
            }
            wid = spill32((long double)wid * sc * 0.294f);
            len = spill32((long double)len * sc * 6.93f);
            if (t == 64) {
                wid = spill32((long double)wid * 1.5f);
                len = spill32((long double)len * 0.8f);
            }
            if (t == 102) wid = spill32((long double)wid * 0.125f);
            if (t == 115) {
                wid = spill32((long double)wid * 0.125f);
                len = spill32((long double)len * 0.5f);
            }
            int32_t c = 255;
            if (t == 83) {
                c = 32 * age;
                len = spill32((long double)len * 3.0f);
                if (c > 255) c = 255;
                if (age > 16) c += 32 * (16 - age);
                if (c < 0) continue;
            } else if (t == 93) {
                c = 16 * age;
                if (c > 255) c = 255;
                if (c < 0) continue;
            } else if (age > 8) {
                c = 511 - 32 * age;
                if (c < 0) continue;
            }
            fx_streak(w, f, wid, len);
            fx_vtx *q;
            switch (t) {
            case 1: case 102:  q = s_q488[2]; break;
            case 119:          q = s_qa7a8; break;
            case 94:           q = s_q488[(i & 1) ? 4 : 0]; break;
            case 83: case 115: q = s_q488[0]; break;
            case 82: case 2: case 64: q = s_q6f8[0]; break;
            case 93:           q = s_q8d8; break;
            default:           q = s_q6f8[1]; break;
            }
            fx_set_col(q, fx_rgb(c, c, c));
            fx_draw(dev, w, q, 2);
            continue;
        }
        case 105: {
            float wid = 0.02f;
            if (age > 96) {
                wid = spill32(0.02f - (long double)spill32((float)(age - 96)) * 0.001f);
                if (wid <= 0.0f) continue;
            }
            int32_t c = 255;
            wid = spill32((long double)wid * sc * 0.084f);
            const float len = spill32((long double)sc * 0.10395f);
            if (age > 96) {
                c = 1023 - 8 * age;
                if (c < 0) continue;
            }
            fx_streak(w, f, wid, len);
            fx_set_col(s_q488[0], fx_rgb(c, c, c));
            fx_draw(dev, w, s_q488[0], 2);
            continue;
        }
        case 99: case 120: case 53: case 44: {
            if (age < 0 && t != 53) continue;
            const int32_t grow = t == 53 ? 16 : 8;
            float wid = t == 44 ? 0.03f : 0.02f;
            if (age > grow) {
                wid = spill32((long double)wid - (long double)spill32((float)(age - grow)) * 0.001f);
                if (wid <= 0.0f) continue;
            }
            int32_t c = 255;
            wid = spill32((long double)wid * sc * (t == 53 ? 0.154f : t == 44 ? 0.0294f : 0.294f));
            const float len = spill32((long double)sc * (t == 44 ? 0.010395f : 0.10395f));
            if (t == 53 ? age > 32 : age > 8) {
                c = t == 53 ? 767 - 16 * age : 511 - 32 * age;
                if (c < 0) continue;
            }
            fx_streak(w, f, wid, len);
            fx_vtx *q = t == 120 ? s_q488[6] : s_q488[2];
            fx_set_col(q, fx_rgb(c, c, c));
            fx_draw(dev, w, q, 2);
            continue;
        }
        case 67: {
            float wid = 0.02f;
            if (age > 8) {
                wid = spill32(0.02f - (long double)spill32((float)(age - 8)) * 0.001f);
                if (wid <= 0.0f) continue;
            }
            int32_t c = 255;
            wid = spill32((long double)wid * sc * 0.294f);
            const float h = spill32((long double)sc * 0.04725f);
            if (age > 8) {
                c = 511 - 32 * age;
                if (c < 0) continue;
            }
            fx_rot(w, f, "y");
            fx_scale(w, wid, h, wid);
            fx_set_col(s_q6f8[1], fx_rgb(c, c, c));
            fx_draw(dev, w, s_q6f8[1], 2);
            continue;
        }
        case 54: case 78: {
            float s = spill32((long double)sc * (t == 78 ? 0.002f : 0.003f));
            const int32_t life = r[16];
            int32_t c = 255;
            if (age > life - 8) {
                const float d = spill32((float)(life - age));
                c = 32 * (life - age) - 1;
                s = spill32((long double)d / 8.0f * s);
                if (c < 0) continue;
            }
            fx_bill(w, f, s, 0);
            fx_set_col(s_q488[0], fx_rgb(c, c, c));
            fx_draw(dev, w, s_q488[0], 2);
            continue;
        }
        case 63: {
            float m[16];
            fx_bill(w, f, spill32((long double)sc * 0.0005f), 0);
            const float x = spill32((long double)spill32((float)age) * 0.1f);
            mat4_translation(m, spill32(x87_crt_sin(x) * 300.0f), 0.0f, 0.0f);
            mat4_mul(w, m, w);
            fx_set_col(s_q488[5], 0xffc8c8c8u);
            fx_draw(dev, w, s_q488[5], 2);
            continue;
        }
        case 52: {
            if (age < 0) continue;
            int32_t c = 32 * age;
            if (c > 255) c = 255;
            fx_bill(w, f, 0.0025f, 0);
            fx_set_col(s_q488[2], fx_rgb(c, c, c));
            fx_draw(dev, w, s_q488[2], 2);
            continue;
        }
        case 41: {
            int32_t c = 255;
            const float a = spill32((long double)sc * 0.002f), b = spill32((long double)sc * 0.01f);
            if (age > 8 && (c = 511 - 32 * age) < 0) continue;
            fx_col_n(s_q3f8, 4, fx_rgb(c, c, c));
            fx_face_vel(w, f);
            fx_scale(w, a, b, b);
            fx_draw(dev, w, s_q3f8, 2);
            continue;
        }
        case 50: {
            const float s = spill32((long double)sc * 0.05f);
            fx_rot(w, f, "zyx");
            fx_scale(w, s, s, s);
            int32_t c = 16 * age;
            if (c > 255) c = 255;
            if (age > 48) c += 16 * (48 - age);
            fx_col_n(s_q638, 20, fx_rgb(c, c, c));
            fx_draw(dev, w, s_q638, 18);
            continue;
        }
        case 61: {
            if (age < 0) continue;
            float a = spill32((long double)sc * 0.01f);
            const float b = spill32((long double)sc * 0.025f);
            const float x = spill32((long double)spill32((float)r[17]) * k_hpi / 20.0f);
            const float cs = spill32(x87_crt_cos(x));
            a = spill32((1.0f - (long double)cs) * 0.04f + a);
            fx_rot(w, f, "zy");
            fx_scale(w, a, b, a);
            int32_t c = 16 * age;
            if (c > 155) c = 155;
            if (c > 95) c = 95;
            fx_col_n(s_q0c8, 20, fx_rgb(2 * c / 3, 2 * c / 3, c));
            fx_draw(dev, w, s_q0c8, 18);
            continue;
        }
        case 62: {
            const float h = spill32((long double)spill32((float)(48 - age)) * 0.002f + (long double)sc * 0.015f);
            const float s = spill32((long double)sc * 0.05f);
            fx_rot(w, f, "yzx");
            fx_scale(w, s, h, h);
            int32_t c = 8 * age;
            if (c > 128) c = 128;
            if (age > 32) c += 8 * (32 - age);
            fx_col_n(s_q638, 20, fx_rgb(2 * c / 3, 2 * c / 3, c));
            fx_draw(dev, w, s_q638, 18);
            continue;
        }
        case 51: case 81: case 77: case 87: {
            const float g = spill32((long double)spill32((float)age) * 0.0015f);
            const float s = spill32(((long double)g + (t == 81 ? 0.05f : 0.002f)) * sc);
            const float x = spill32((long double)sc * 0.03f);
            fx_rot(w, f, "zyx");
            fx_scale(w, x, s, s);
            int32_t c = 16 * age;
            if (c > 255) c = 255;
            if (t == 87) {
                if (age > 128) c += 8 * (128 - age);
            } else if (t == 81) {
                if (age > 24) c += 32 * (24 - age);
            } else if (age > 32) {
                c += 16 * (32 - age);
            }
            if (t == 81) c /= 2;
            uint32_t col;
            if (t == 51 || t == 81) col = fx_rgb(c, c, c);
            else if (t == 87)       col = fx_rgb(c / 8, c / 8, c / 4);
            else                    col = fx_rgb(c, c / 3, c / 3);
            fx_col_n(s_q638, 20, col);
            fx_draw(dev, w, s_q638, 18);
            continue;
        }
        case 33: {
            const int32_t owner = r[17];
            float s = spill32(((long double)spill32((float)PW(0x56dae84u).i) * 0.00075f + 0.002f) * sc);
            if (owner != -1) {
                const float o = spill32((float)g_scene1_records_b[owner * SCENE1_RECORDS_B_STRIDE + 38]);
                s = spill32(((long double)o * 0.0002f + 0.005f) * sc);
            }
            float m[16];
            fx_rot(w, f, "zyx");
            mat4_translation(m, f[3], f[4], f[5]);
            mat4_mul(w, m, w);
            fx_scale(w, s, s, s);
            int32_t c = 32 * age;
            if (c > 255) c = 255;
            if (age > 24) c += 32 * (24 - age);
            switch (PW(0x56dae5cu).i) {   /* the element: another keeps the last tint */
            case 0: fx_col_n(s_q3c8, 10, fx_rgb(c / 2, c, c / 2)); break;
            case 1: fx_col_n(s_q3c8, 10, fx_rgb(c, c, c)); break;
            case 2: fx_col_n(s_q3c8, 10, fx_rgb(c, c / 2, c / 2)); break;
            case 3: fx_col_n(s_q3c8, 10, fx_rgb(c / 2, c / 2, c)); break;
            default: break;
            }
            fx_draw(dev, w, s_q3c8, 8);
            continue;
        }
        case 32: {
            const float s = spill32(((long double)spill32((float)PW(0x56dae84u).i) * 0.00075f + 0.002f) * sc);
            fx_bill(w, f, s, 1);
            static const int32_t k_cell[4] = { 3, 6, 5, 2 };
            const int32_t e = PW(0x56dae5cu).i;
            fx_vtx *q = s_q488[(e >= 0 && e < 4) ? k_cell[e] : 5];
            fx_set_col(q, 0xffffffffu);
            fx_draw(dev, w, q, 2);
            continue;
        }
        case 110: {
            static const float k_off[8] = { -0.25f, 0.25f, 0.25f, 0.15f, -0.25f, -0.15f, 0.2f, -0.25f };
            int32_t c = 128;
            const float s = spill32((long double)sc * 0.001f);
            if (age > 100) c = 8 * (116 - age);
            fx_set_col(s_q488[3], fx_rgb(c, c, c));
            for (int k = 0; k < 12; k++) {
                const float a = spill32((long double)spill32((float)(age + 10 * k)) * 0.05f);
                const float sn = spill32(x87_crt_sin(a));
                const float x = spill32((long double)k_off[2 * (k % 4)] * 0.5f + (long double)sn * 0.4f + f[0]);
                const float b = spill32((long double)spill32((float)(age + 30 * k)) * 0.03f);
                const float cs = spill32(x87_crt_cos(b));
                const float y = spill32((long double)k_off[2 * (k % 4) + 1] * 0.5f + (long double)cs * 0.4f + f[1]);
                const float pf[3] = { x, y, spill32((long double)f[2] + 0.5f) };
                float m[16];
                mat4_translation(w, pf[0], pf[1], pf[2]);
                mat4_scaling(m, s, s, s);
                mat4_mul(w, m, w);
                mat4_mul(w, g_scene1_camera_orient, w);
                fx_draw(dev, w, s_q488[3], 2);
            }
            continue;
        }
        case 31: case 100: case 109: case 101: case 104: case 108: {
            float s = spill32((long double)sc * 0.005f);
            if (t == 100) s = spill32((long double)s * 1.5f);
            int32_t c = 127;
            if (t == 101) {
                c = 255;
                if (age > 16) c = 767 - 32 * age;
                s = spill32((long double)s * 3.0f);
            } else if (t == 108) {
                const float fa = spill32((float)age);
                float k = spill32((long double)fa / 30.0f);
                if (k >= 1.0f) k = 1.0f;
                const float sn = spill32(x87_crt_sin(spill32((long double)fa * 0.2f)));
                s = spill32((long double)k * 0.005f * sc + (long double)sn * 0.0012f);
                if (age > 150) {
                    c = 2 * age - 173;
                    if (c > 255) c = 255;
                }
            } else if (t == 109) {
                int32_t g = 4 * age;
                if (g > 128) g = 127;
                if (age > r[17] - 32) g = g + 4 * (r[17] - age) - 128;
                c = g / 2;
            } else if (t == 104) {
                c = 8 * age;
                if (c > 255) c = 255;
                if (age > 40) c += 32 * (40 - age);
            } else if (age > 0) {
                /* DAT_0438b8f8 == 2 doubles c and triples s — only ever 0 */
                c = 127 - 4 * age;
            }
            fx_bill(w, f, s, 1);
            fx_set_col(s_q488[2], fx_rgb(c, c, c));
            fx_draw(dev, w, s_q488[2], 2);
            continue;
        }
        case 35: case 20: {
            float m[16];
            const float s = spill32((long double)sc * (t == 35 ? 0.1f : 0.01f));
            mat4_translation(w, f[0], f[1], f[2]);
            fx_scale(w, s, s, s);
            mat4_rotation_x(m, k_hpi);
            mat4_mul(w, m, w);
            if (t == 20) {
                fx_set_col(s_q488[5], fx_pulse(r));
                fx_draw(dev, w, s_q488[5], 2);
                continue;
            }
            int32_t c = 16 * age;
            if (c > 255) c = 255;
            if (age > 32) c += 16 * (32 - age);
            fx_set_col(s_q488[2], fx_rgb(c, c, c));
            fx_draw(dev, w, s_q488[2], 2);
            continue;
        }
        case 34: case 60: case 45: {
            if (age < 0) continue;
            const float s0 = spill32((long double)sc * (t == 45 ? 0.01f : 0.02f));
            const float a = spill32((long double)spill32((float)age) * k_pi / (t == 60 ? 24.0f : 32.0f));
            const float s = spill32((long double)spill32(x87_crt_sin(a)) * s0);
            fx_bill(w, f, s, 1);
            fx_vtx *q = s_qstar[t == 45];
            fx_set_col(q, 0xffffffffu);
            fx_draw(dev, w, q, 2);
            continue;
        }
        case 5: case 92: case 111: case 10: case 11: case 12: {
            const float s = spill32(((long double)spill32((float)age) * 0.001f + 0.01f) * sc);
            fx_bill(w, f, s, 1);
            int32_t fr = age / 2, c = 255;
            if (fr > 7) fr = 7;
            if (age > 16) c = 511 - 16 * age;
            const uint32_t col = fx_rgb(c / 2, c / 2, c / 2);
            fx_vtx *q;
            if (t == 5 || t == 92 || t == 111) {
                fx_set_col(s_atlas_quad, col);
                if (t == 111) fx_atlas_frame(fr, 0.75195312f, 0.87304688f);
                else          fx_atlas_frame(fr, 0.87695312f, 0.99804688f);
                q = s_atlas_quad;
            } else {
                q = s_q488[t == 10 ? 2 : t == 11 ? 3 : 0];
                fx_set_col(q, col);
            }
            fx_draw(dev, w, q, 2);
            continue;
        }
        case 25: case 68: {
            const float s = spill32(((long double)spill32((float)age) * (t == 25 ? 0.001f : 0.008f) +
                                     (t == 25 ? 0.04f : 0.01f)) * sc);
            fx_bill(w, f, s, 0);
            uint32_t col;
            if (t == 25) {
                int32_t c = 255;
                if (age > 8) {
                    c = 511 - 32 * age;
                    if (c < 0) continue;
                }
                col = 0xff000000u | ((uint32_t)(c / 4) << 16) | ((uint32_t)(c / 3) << 8) | (uint32_t)c;
            } else {
                int32_t c = 16 * age;
                if (c > 128) c = 128;
                if (age > 32) c += 16 * (32 - age);
                if (c < 0) continue;
                col = fx_rgb(c, c, c);
            }
            fx_set_col(s_q398, col);
            fx_draw(dev, w, s_q398, 2);
            continue;
        }
        case 30: case 46: {
            int32_t c = 255;
            const float h = spill32((long double)sc * 0.1f);
            if (t == 30 ? (age > 8 && (c = 511 - 32 * age) < 0) : (age > 16 && (c = 383 - 8 * age) < 0))
                continue;
            fx_set_col(s_qa748, fx_rgb(c, c, c));
            const float bw = spill32((long double)h * 0.4f);
            for (int k = 0; k < 8; k++) {
                const float a = spill32((long double)spill32((float)k) * k_2pi / 8.0f);
                const long double sn = x87_crt_sin(a), cs = x87_crt_cos(a);
                float x, y, z, bh;
                if (t == 30) {
                    const float s1 = spill32(sn), c1 = spill32(cs);
                    x = spill32((long double)s1 + s1 + f[0]);
                    y = spill32((long double)spill32((float)age) * 0.1f + f[1] - 0.5f);
                    z = spill32((long double)c1 + c1 + f[2]);
                    bh = spill32((long double)spill32((float)age) * 0.001f + 0.005f);
                } else {
                    x = spill32(sn * sc * 22.0f + f[0]);
                    y = spill32((long double)spill32((float)age) * 0.01f + f[1] - 0.5f);
                    z = spill32(cs * sc * 22.0f + f[2]);
                    const float e = spill32((long double)spill32((float)age) * k_pi / 56.0f);
                    bh = spill32(x87_crt_sin(e) * 0.03f + 0.005f);
                }
                float m[16];
                mat4_translation(w, x, y, z);
                mat4_rotation_y(m, a);
                mat4_mul(w, m, w);
                fx_scale(w, bw, bh, h);
                fx_draw(dev, w, s_qa748, 2);
            }
            continue;
        }
        case 28: {
            fx_bill(w, f, spill32((long double)sc * 0.01f), 1);
            if (((16 - age) & 0x10000000) != 0) continue;
            const int32_t c = 8 * (16 - age);
            fx_set_col(s_atlas_quad, fx_rgb(c, c, c));
            int32_t fr = age / 2;
            if (fr > 7) fr = 7;
            fx_atlas_frame(fr, 0.75195312f, 0.87304688f);
            fx_draw(dev, w, s_atlas_quad, 2);
            continue;
        }
        case 4: case 112: case 152: {
            fx_bill(w, f, spill32((long double)sc * 0.01f), 0);
            int32_t c = 8 * (16 - age);
            if (t == 112) {
                c = 128;
                if (age > 16) c = 8 * (32 - age);
            }
            if (t == 152) c = 255;
            if (c < 0) continue;
            fx_vtx *q = s_q488[t == 4 ? 0 : 2];
            fx_set_col(q, fx_rgb(c, c, c));
            fx_draw(dev, w, q, 2);
            continue;
        }
        case 19:
            fx_bill(w, f, spill32((long double)sc * 0.01f), 0);
            fx_set_col(s_q488[5], fx_pulse(r));
            fx_draw(dev, w, s_q488[5], 2);
            continue;
        default:
            continue;
        }
    }
}

/* FUN_004176ff 0x41e177..0x41e5bb, after the table-A loop, under COLOROP
 * MODULATE4X with Z off: table A's 42 and table B's 41 (the faint glows: grey 8
 * (42, closing 2 a tick past 4) or 32 (41, closing 2 a tick past +0xbc · 136 -
 * 16) on DAT_0064b668, billboards of scale · 0.2 / 0.03 — 41 one unit up), then
 * SRCBLEND ZERO / DESTBLEND INVSRCCOLOR for 117, the dark orbs a type-19 hit
 * leaves (the white DAT_0064b488 cell 0, billboards of scale · 0.005: they
 * darken what's behind); then Z back on without Z write, SRCALPHA /
 * INVSRCCOLOR, COLOROP MODULATE and FUN_00414ee2(2, 0) before the puffs */
static void fx_after_main(IDirect3DDevice8 *dev)
{
    float w[16];
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE4X);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
    for (int i = 0; i < g_scene1_records_a_count && i < SCENE1_RECORDS_A_COUNT; i++) {
        const int32_t *r = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        if (r[12] != 42) continue;
        float f[15];
        memcpy(f, r, sizeof f);
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_sysassets.effect_bmp.tex);
        fx_bill(w, f, spill32((long double)f[14] * 0.2f), 0);
        int32_t c = 8;
        if (r[13] > 4) c = (8 - r[13]) << 1;
        fx_set_col(s_q488[5], fx_rgb(c, c, c));
        fx_draw(dev, w, s_q488[5], 2);
    }
    int n = g_scene1_records_b_count;
    if (n > SCENE1_RECORDS_B_COUNT) n = SCENE1_RECORDS_B_COUNT;
    for (int i = 0; i < n; i++) {
        const int32_t *r = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
        if (r[0] != 41) continue;
        const float *f = (const float *)r;
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_sysassets.effect_bmp.tex);
        const float pos[3] = { f[23], spill32((long double)f[24] + 1.0f), f[25] };
        fx_bill(w, pos, spill32((long double)f[66] * 0.03f), 0);
        const int32_t t = x87_ftol((long double)f[47] * 136.0f - 16.0f);
        int32_t c = 32;
        if (r[38] > t) {
            c = 2 * (t - r[38]) + 32;
            if (c < 0) continue;
        }
        fx_set_col(s_q488[5], fx_rgb(c, c, c));
        fx_draw(dev, w, s_q488[5], 2);
    }
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ZERO);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    for (int i = 0; i < g_scene1_records_a_count && i < SCENE1_RECORDS_A_COUNT; i++) {
        const int32_t *r = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        if (r[12] != 117) continue;
        float f[15];
        memcpy(f, r, sizeof f);
        fx_bill(w, f, spill32((long double)f[14] * 0.005f), 0);
        fx_set_col(s_q488[0], 0xffffffffu);
        fx_draw(dev, w, s_q488[0], 2);
    }
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    scene1_overlay_render(dev, /*layer=*/2, /*mode=*/0);
}

/* FUN_004176ff's table-B projectile loop (after the swing trails, ONE / ONE on
 * effect_shot.bmp): per type a world built on the record's position (+0x5c),
 * yaw +0x90, rotations +0x8c / +0x94, timer +0x98 and scale +0x108, then
 * DAT_0064bf68 with the type's atlas cell or DAT_0064b488's cell 0:
 *   88 / 102         the cell (32, 0)-(64, 120) flat on the floor along the yaw;
 *   101 / 100 / 105 / 132 / 77..80 / 165 / 166, and 98 / 123 / 161 / 164
 *                    a streak facing the eye along the velocity (FUN_00415f2e);
 *   81 / 99          a flat cell along the yaw; 82 / 106, 30 / 136 / 154
 *                    billboards (the latter fading in by the timer);
 *   38 / 42 / 46     spinning billboards fading out past 128 / 224;
 *   31, 90 / 152     tilted / flat discs (90 / 152 tinted blue), 107 / 108
 *                    growing / velocity-facing cell-0 quads.
 * Type 123 with +0x9c == 1 draws cell 0 with whatever world the loop last
 * built (retail's stack matrix). */
static void fx_projectiles(IDirect3DDevice8 *dev)
{
    static float s_w[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };   /* v438 */
    static const float khpi = 1.5707963705062866f;
    fx_init_quads();
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    int n = g_scene1_records_b_count;
    if (n > SCENE1_RECORDS_B_COUNT) n = SCENE1_RECORDS_B_COUNT;
    for (int i = 0; i < n; i++) {
        const int32_t *r = &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
        const float *f = (const float *)r;
        const int32_t t = r[0];
        if (t == 0) continue;
        float m[16];
        fx_vtx *q = s_atlas_quad;
        if (t == 88 || t == 102) {
            float sx = spill32((long double)f[66] * 0.001f);
            const float syz = spill32((long double)f[66] * 0.01f);
            fx_set_col(s_atlas_quad, 0xffffffffu);
            if (t == 102) sx = spill32((long double)sx * 1.8f);
            if (r[38] > 9) {
                const float e = spill32((float)(r[38] - 9));
                sx = spill32((1.0L - (long double)e / 4.0f) * sx);
            }
            fx_set_uv(0.12695312f, 0.24804688f, 0.001953125f, 0.47070312f);
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_rotation_x(m, khpi);   mat4_mul(s_w, m, s_w);
            mat4_rotation_z(m, -f[36]); mat4_mul(s_w, m, s_w);
            mat4_scaling(m, sx, syz, syz); mat4_mul(s_w, m, s_w);
        } else if (t == 101 || t == 100 || t == 105 || t == 132 || t == 77 || t == 78 ||
                   t == 79 || t == 80 || t == 165 || t == 166 ||
                   t == 98 || (t == 123 && r[39] != 1) || t == 161 || t == 164) {
            const int streak = t == 98 || t == 123 || t == 161 || t == 164;
            const float a = spill32((long double)f[66] * (streak ? 0.003 : 0.002));
            const float b = spill32((long double)f[66] * (streak ? 0.01 : 0.02));
            wf_pass_e_fan_billboard_matrix(s_w, r, g_scene1_camera_eye);   /* FUN_00415f2e */
            mat4_rotation_x(m, khpi); mat4_mul(s_w, m, s_w);
            mat4_scaling(m, a, b, b); mat4_mul(s_w, m, s_w);
            fx_set_col(s_atlas_quad, 0xffffffffu);
            if (streak) fx_set_uv(0.25195312f, 0.31054688f, 0.24804688f, 0.001953125f);
            else        fx_set_uv(0.001953125f, 0.12304688f, 0.47070312f, 0.001953125f);
        } else if (t == 123) {
            q = s_q488[0];
        } else if (t == 81 || t == 99) {
            float b = spill32((long double)f[66] * 0.002f);
            float a = spill32((long double)f[66] * 0.005f);
            if (t == 99) {
                a = spill32((long double)a * 1.2f);
                fx_set_uv(0.001953125f, 0.12304688f, 0.001953125f, 0.47070312f);
            } else {
                b = spill32((long double)f[66] * 0.016f);
                a = spill32((long double)f[66] * 0.01f);
                fx_set_uv(0.25195312f, 0.74804688f, 0.25195312f, 0.49804688f);
            }
            fx_set_col(s_atlas_quad, 0xffffffffu);
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_rotation_x(m, khpi);   mat4_mul(s_w, m, s_w);
            mat4_rotation_z(m, -f[36]); mat4_mul(s_w, m, s_w);
            mat4_scaling(m, b, a, a);   mat4_mul(s_w, m, s_w);
        } else if (t == 82 || t == 106 || t == 30 || t == 136 || t == 154) {
            const float s = spill32((long double)f[66] * 0.005f);
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_scaling(m, s, s, s); mat4_mul(s_w, m, s_w);
            mat4_mul(s_w, g_scene1_camera_orient, s_w);
            q = s_q488[0];
            if (t == 82 || t == 106) {
                fx_set_col(q, 0xffffffffu);
            } else {
                int32_t c = 16 * r[38];
                if (c > 255) c = 255;
                if (c < 0) continue;
                fx_set_col(q, fx_rgb(c, c, c));
            }
        } else if (t == 38 || t == 42 || t == 46) {
            const float s = spill32((long double)f[66] * 0.004f);
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_scaling(m, s, s, s); mat4_mul(s_w, m, s_w);
            mat4_mul(s_w, g_scene1_camera_orient, s_w);
            mat4_rotation_z(m, f[37]); mat4_mul(s_w, m, s_w);
            int32_t c = 16 * r[38];
            if (c > 255) c = 255;
            const int32_t cap = t == 46 ? 224 : 128;
            if (r[38] > cap) c += 8 * (cap - r[38]);
            fx_set_col(s_atlas_quad, fx_rgb(c, c, c));
            if (t == 42) fx_set_uv(0.87695312f, 0.99804688f, 0.12695312f, 0.24804688f);
            else         fx_set_uv(0.62695312f, 0.74804688f, 0.12695312f, 0.24804688f);
        } else if (t == 31) {
            const float s = spill32((long double)f[66] * 0.002f);
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_scaling(m, s, s, s);           mat4_mul(s_w, m, s_w);
            mat4_rotation_x(m, 0.62831855f);   mat4_mul(s_w, m, s_w);
            mat4_rotation_y(m, f[36]);         mat4_mul(s_w, m, s_w);
            mat4_rotation_x(m, f[35]);         mat4_mul(s_w, m, s_w);
            int32_t c = 255;
            if (r[38] > 112) c = 3839 - 32 * r[38];
            fx_set_col(s_atlas_quad, fx_rgb(c, c, c));
            fx_set_uv(0.001953125f, 0.24804688f, 0.75195312f, 0.99804688f);
        } else if (t == 108) {
            const float s = spill32((long double)f[66] * 0.004);
            wf_pass_e_fan_billboard_matrix(s_w, r, g_scene1_camera_eye);
            mat4_rotation_x(m, khpi); mat4_mul(s_w, m, s_w);
            mat4_scaling(m, s, s, s); mat4_mul(s_w, m, s_w);
            q = s_q488[0];
            fx_set_col(q, 0xffffffffu);
        } else if (t == 107) {
            float s = spill32((long double)spill32((float)r[38]) * 0.001f);
            if (s > 0.004f) s = 0.004f;
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_scaling(m, s, s, s);   mat4_mul(s_w, m, s_w);
            mat4_rotation_x(m, khpi);   mat4_mul(s_w, m, s_w);
            int32_t c = 16 * r[38];
            if (c > 255) c = 255;
            if (r[38] > 60) c += 32 * (60 - r[38]);
            if (c < 0) c = 0;
            q = s_q488[0];
            fx_set_col(q, fx_rgb(c, c, c));
        } else if (t == 90 || t == 152) {
            const float s = spill32((long double)f[66] * 0.002f);
            mat4_translation(s_w, f[23], f[24], f[25]);
            mat4_scaling(m, s, s, s);   mat4_mul(s_w, m, s_w);
            mat4_rotation_x(m, khpi);   mat4_mul(s_w, m, s_w);
            int32_t c = 255;
            if (r[38] > 112) c = 3839 - 32 * r[38];
            fx_set_col(s_atlas_quad, fx_rgb(c / 3, c / 3, c));
            fx_set_uv(0.001953125f, 0.24804688f, 0.75195312f, 0.99804688f);
        } else {
            continue;
        }
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)s_w);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(fx_vtx));
    }
}

/* FUN_004176ff 0x4189xx: table-A type 79, the snowflakes — snow.bmp
 * (DAT_073aa178) on DAT_00648da8, T(pos) · S(scale · 0.003) · B · RotZ(+0x20),
 * grey min(8 · age, 128) + ftol(sin(age · 0.05) · 64), closing 8 a tick past
 * the lifetime +0x44; additive (the projectile loop's ONE / ONE) */
static void fx_snow(IDirect3DDevice8 *dev)
{
    int bound = 0;
    for (int i = 0; i < g_scene1_records_a_count && i < SCENE1_RECORDS_A_COUNT; i++) {
        const int32_t *r = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        if (r[12] != 79) continue;
        if (!bound) {
            if (g_sysassets.snow_bmp.tex == NULL) return;
            IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_sysassets.snow_bmp.tex);
            bound = 1;
        }
        float f[15];
        memcpy(f, r, sizeof f);
        const float sz = spill32((long double)f[14] * 0.003);
        float w[16], m[16];
        mat4_translation(w, f[0], f[1], f[2]);
        mat4_scaling(m, sz, sz, sz);   mat4_mul(w, m, w);
        mat4_mul(w, g_scene1_camera_orient, w);
        mat4_rotation_z(m, f[8]);      mat4_mul(w, m, w);
        const int32_t age = r[13];
        int32_t base = 8 * age;
        if (base >= 128) base = 128;
        float a = spill32((float)age);
        a = spill32((long double)a * 0.050000001f);
        a = spill32(x87_crt_sin((double)a));
        int32_t c = x87_ftol((long double)a * 64.0L) + base;
        if (c < 0) c = 0;
        if (age > r[17]) {
            c += 8 * (r[17] - age);
            if (c < 0) c = 0;
        }
        fx_set_col(s_q8da8, fx_rgb(c, c, c));
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, s_q8da8, sizeof(fx_vtx));
    }
}

/* FUN_004176ff's puff loop (0x41d6xx, after FUN_00414ee2(…, 2, 0)): table-A
 * types 14 / 43 / 27 / 59 / 89 / 103 / 118 — a billboard T(pos) · S(z) · B ·
 * RotZ(+0x20) on effect.bmp, z = (age · 0.0004 + 0.02) · scale · 0.8 (103:
 * age · 0.00015), the frame min(age / 3, 4) of the cells (32k, 160)..(32k+32,
 * 191); alpha 128 (89: 8 · age to 127) closing 8 a tick past 16 (103: 128,
 * then 8 · (128 - age) past 112), grey (14 / 27 / 103), magenta (59),
 * red-orange (118), blue-white (89) or the stage's smokecolor (43);
 * SRCALPHA / INVSRCCOLOR, Z test, no Z write */
static void fx_table_a_puffs(IDirect3DDevice8 *dev)
{
    if (g_sysassets.effect_bmp.tex == NULL) return;
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    int bound = 0;
    for (int i = 0; i < g_scene1_records_a_count && i < SCENE1_RECORDS_A_COUNT; i++) {
        const int32_t *r = &g_scene1_records_a[i * SCENE1_RECORDS_A_STRIDE];
        const int32_t t = r[12];
        if (!(t == 14 || t == 43 || t == 27 || t == 59 || t == 89 || t == 103 || t == 118)) continue;
        if (!bound) {   /* FUN_00415e90: the cached SetTexture */
            IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_sysassets.effect_bmp.tex);
            bound = 1;
        }
        float f[15];
        memcpy(f, r, sizeof f);
        const int32_t age = r[13];
        const float fa = spill32((float)age);
        const float z = t == 103
            ? spill32(((long double)fa * 0.00015000001f + 0.02) * f[14] * 0.80000001f)
            : spill32(((long double)fa * 0.00039999999f + 0.02) * f[14] * 0.80000001f);
        float w[16], m[16];
        mat4_translation(w, f[0], f[1], f[2]);
        mat4_scaling(m, z, z, z);   mat4_mul(w, m, w);
        mat4_mul(w, g_scene1_camera_orient, w);
        mat4_rotation_z(m, f[8]);   mat4_mul(w, m, w);
        int32_t a = 128;
        if (t == 89) {
            a = 8 * age;
            if (a > 127) a = 127;
        }
        int32_t frame = age / 3;
        if (frame > 4) frame = 4;
        if (age > 16) a += 8 * (16 - age);
        if (t == 103) {
            a = 128;
            if (age > 112) a = 8 * (128 - age);
        }
        uint32_t col;
        if (t == 59) {
            col = (uint32_t)a | (((uint32_t)a | 0xffffff00u) << 16);
        } else if (t == 118) {
            col = ((uint32_t)(a / 8) | (((uint32_t)a | 0xffffff00u) << 8)) << 8;
        } else if (t == 89) {
            col = fx_rgb(3 * a / 2, 3 * a / 2, 2 * a);
        } else if (t == 43) {
            const stage_record_t *rec = scene1_current_stage_record();   /* smokecolor: +0x1a9c */
            const int32_t cr = rec ? a * rec->smokecolor[0] / 255 : 0,
                          cg = rec ? a * rec->smokecolor[1] / 255 : 0,
                          cb = rec ? a * rec->smokecolor[2] / 255 : 0;
            col = (uint32_t)cb | (((uint32_t)cg | (((uint32_t)cr | 0xff00u) << 8)) << 8);
        } else {
            col = fx_rgb(a, a, a);
        }
        fx_set_col(s_atlas_quad, col);
        const float cu = spill32((float)(32 * frame));
        fx_set_uv(spill32(((long double)cu + 0.5) / 256.0), spill32(((long double)cu + 31.5) / 256.0),
                  0.62695312f, 0.74804688f);
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)w);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, s_atlas_quad, sizeof(fx_vtx));
    }
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ZERO);
}

void scene1_effects_table_b(struct IDirect3DDevice8 *dev_in)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (dev == NULL || g_sysassets.effect_shot_bmp.tex == NULL) return;
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)g_sysassets.effect_shot_bmp.tex);
    /* the stage args as FUN_004176ff leaves them (texture × diffuse) — the
     * port's overlay dispatch just before sets ARG1 to DIFFUSE */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ZERO);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    fx_swing_trails(dev);
    fx_projectiles(dev);
    fx_snow(dev);
    fx_table_a_main(dev);
    fx_after_main(dev);
    fx_table_a_puffs(dev);
}

#endif /* _WIN32 */
