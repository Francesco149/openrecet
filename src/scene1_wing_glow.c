/*
 * scene1_wing_glow.c — see scene1_wing_glow.h for the chip writeup.
 *
 * Ports FUN_004176ff's records-A type-0x1f arm (decompile L3818-3921):
 * the companion (Tear) wing-glow sparkle billboard.
 *
 * Recipe (retail ground truth, runs/wingglow-d3d via
 * tools/dump_wingglow_groundtruth.py; replayed render-state at the
 * 0x41e165 DrawPrimitiveUP):
 *   - Geometry  : static ±256 quad, TRIANGLESTRIP (prim_count 2),
 *                 FVF 0x142 (XYZ|DIFFUSE|TEX1, stride 24).
 *   - UVs       : a fixed 32x32 cell of bmp/effect.bmp,
 *                 u in [0.251953125, 0.373046875],
 *                 v in [0.501953125, 0.623046875]  (read live from the
 *                 BSS template &DAT_0064b548 — never written in code).
 *   - Texture   : bmp/effect.bmp (engine DAT_073cc8c0 → g_sysassets.effect_bmp).
 *   - Blend     : ALPHABLENDENABLE=1, SRCBLEND=ONE, DESTBLEND=ONE  (additive).
 *   - Alpha test: ENABLE=1, REF=0, FUNC=GREATEREQUAL  (passes everything).
 *   - Depth     : ZENABLE=1, ZWRITEENABLE=0.  CULLMODE=NONE.  LIGHTING=0.
 *   - Tex stage : COLOROP=MODULATE(COLORARG1=DIFFUSE, COLORARG2=TEXTURE),
 *                 ALPHAOP=MODULATE(ALPHAARG1=TEXTURE, ALPHAARG2=DIFFUSE).
 *   - Per slot  : scale  = SCALE_field * 0.005   (emit passes 0.1 → 0.0005).
 *                 world  = RotZ(rot.z) * billboard * Scale * Translate(pos)
 *                          (g_scene1_camera_orient is the billboard matrix
 *                          DAT_0438cdf8; rot.z is 0 for the wing-glow).
 *                 diffuse= 0xFF<<24 | i<<16 | i<<8 | i, grey age-fade
 *                          i = (age>0) ? (0x7f - 4*age) : 0x7f.
 *
 * PORT-DEBT(stub, FUN_004176ff): only the records-A type-0x1f arm is
 * ported here; the other ~30 particle-type arms, the records-B passes,
 * and the function's full per-pass state sequencing remain stubbed in
 * scene1_walk_chr_TODO.  Retire = full FUN_004176ff port.
 *
 * PORT-DEBT(simplified, FUN_004176ff L3876): the engine's boosted-glow
 * sub-branch (`if (DAT_0438b8f8 == 2) { intensity*=2; scale*=3; }`) is
 * NOT ported — DAT_0438b8f8 (the per-frame-emit override) is not exposed
 * by the port and is 0 in free-roam, so the branch is dormant.  Retire
 * when DAT_0438b8f8 is wired.
 */

#include "scene1_wing_glow.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>

#include "math3d.h"
#include "scene1_records.h"
#include "scene1_camera.h"   /* g_scene1_camera_orient (= engine DAT_0438cdf8) */
#include "sysassets.h"       /* g_sysassets.effect_bmp (= engine DAT_073cc8c0) */

#define SCENE1_WING_GLOW_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define WING_GLOW_TYPE 0x1f

typedef struct {
    float    x, y, z;
    uint32_t diffuse;
    float    u, v;
} wing_glow_vertex;

/* Static billboard template — verbatim object-space geometry + effect.bmp
 * atlas UVs read from the engine's BSS template &DAT_0064b548 (retail GT).
 * Strip winding TL -> BL -> TR -> BR; CULLMODE=NONE so it is unculled.
 * Diffuse is overwritten per slot below. */
static wing_glow_vertex g_wing_glow_vbuf[4] = {
    { -256.0f,  256.0f, 0.0f, 0xFFFFFFFFu, 0.251953125f, 0.501953125f }, /* TL */
    { -256.0f, -256.0f, 0.0f, 0xFFFFFFFFu, 0.251953125f, 0.623046875f }, /* BL */
    {  256.0f,  256.0f, 0.0f, 0xFFFFFFFFu, 0.373046875f, 0.501953125f }, /* TR */
    {  256.0f, -256.0f, 0.0f, 0xFFFFFFFFu, 0.373046875f, 0.623046875f }, /* BR */
};

/* Engine L3882-3909 world chain, replicated onto an accumulator the same
 * way scene1_pass_f does: start at Translate, then left-multiply Scale,
 * billboard, RotZ.  mat4_mul(out, A, B) computes out = A*B and tolerates
 * out aliasing B (matches D3DXMatrixMultiply). */
static void wing_glow_compose_world(float out[16],
                                    float px, float py, float pz,
                                    float scale, float rot_z)
{
    float tmp[16];

    mat4_translation(out, px, py, pz);                 /* T               */
    mat4_scaling(tmp, scale, scale, scale);
    mat4_mul(out, tmp, out);                            /* S * T           */
    mat4_mul(out, g_scene1_camera_orient, out);         /* billboard * S*T */
    mat4_rotation_z(tmp, rot_z);
    mat4_mul(out, tmp, out);                            /* Rz * billboard*S*T */
}

void scene1_wing_glow_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;

    int count = g_scene1_records_a_count;
    if (count <= 0) return;
    if (count > SCENE1_RECORDS_A_COUNT) count = SCENE1_RECORDS_A_COUNT;

    /* Nothing-to-draw guard: the engine's FUN_004176ff manages render
     * state across all its passes; this isolated arm sets state only when
     * it has a glow to draw, so an empty sweep leaves device state alone. */
    int have = 0;
    for (int slot = 0; slot < count; slot++) {
        const int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
        if (r[SCENE1_RECORDS_A_OFF_TYPE] == WING_GLOW_TYPE) { have = 1; break; }
    }
    if (!have) return;

    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* Additive blend envelope (retail GT). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    /* Retail draws at AF=GREATER (d3d-trace 0x41e165) — NOT GreaterEqual; the
     * comment above said GT but the code set GE.  ONE/ONE additive makes alpha==0
     * texels contribute ~0, so this is mostly a state-parity fix, but match retail. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);

    /* COLOR = diffuse(grey age-fade) * texture(blue glow); ALPHA = tex * diffuse. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    IDirect3DDevice8_SetTexture(dev, 0,
        (IDirect3DBaseTexture8 *)g_sysassets.effect_bmp.tex);

    IDirect3DDevice8_SetVertexShader(dev, SCENE1_WING_GLOW_FVF);

    for (int slot = 0; slot < count; slot++) {
        const int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
        if (r[SCENE1_RECORDS_A_OFF_TYPE] != WING_GLOW_TYPE) continue;

        int   age      = r[SCENE1_RECORDS_A_OFF_AGE];
        float scale_f  = *(const float *)&r[SCENE1_RECORDS_A_OFF_SCALE] * 0.005f;
        float px       = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_X];
        float py       = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_Y];
        float pz       = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_Z];
        float rot_z    = *(const float *)&r[SCENE1_RECORDS_A_OFF_ROT_Z];

        /* L3862-3880 (0x1f path): grey intensity fades with age.  i stays
         * in [3, 0x7f] for the sim's age range (kill at 0x20), so the
         * channel-pack never overflows; replicated bit-exact. */
        uint32_t i = (age > 0) ? (uint32_t)(0x7f - 4 * age) : 0x7fu;
        uint32_t color = ((i | 0xffffff00u) << 8 | i) << 8 | i;

        float world[16];
        wing_glow_compose_world(world, px, py, pz, scale_f, rot_z);
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                      (const D3DMATRIX *)world);

        for (int v = 0; v < 4; v++) g_wing_glow_vbuf[v].diffuse = color;

        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2,
                                         g_wing_glow_vbuf,
                                         sizeof(wing_glow_vertex));
    }
}

#endif /* _WIN32 */
