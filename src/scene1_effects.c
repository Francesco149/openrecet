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
#include "scene1_records.h"     /* table B DAT_069324b0 */
#include "sysassets.h"          /* effect_shot.bmp DAT_073cc940 */
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

static void fx_init_quads(void)
{
    static int done;
    if (done) return;
    done = 1;
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
}

#endif /* _WIN32 */
