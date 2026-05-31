/*
 * scene1_pass_f.c — see scene1_pass_f.h for the chip writeup.
 *
 * MVP port of FUN_004161c7 lines L423-481 (Pass F of the wide-followup
 * walker).  Walks g_scene1_records_a for type-0x92 slots and emits one
 * color-cycle billboard quad per match.
 *
 * PORT-DEBT(simplified, FUN_004161c7): ports ONLY Pass F (L423-481), not the
 * surrounding A-E context; standalone-smoke divergences documented in the .h.
 * Retire = full FUN_004161c7 wide-followup port (plan Step 3).
 */

#include "scene1_pass_f.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>
#include <string.h>

#include "math3d.h"
#include "scene1_records.h"

/* Vertex format: D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 = 0x142.
 * Matches the engine's wide-followup Pass F SetVertexShader at the
 * mid-pass injection (L130).  Stride 0x18 = 24 bytes. */
#define SCENE1_PASS_F_FVF \
    (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

typedef struct {
    float    x, y, z;
    uint32_t diffuse;
    float    u, v;
} pass_f_vertex;

/* Static vbuf — 4 vertices for a TRIANGLESTRIP (2 triangles, 1 quad).
 *
 * Object-space extents at ±100 so the engine's scale formula
 *   final_scale = piVar11[5] / 200.0 * piVar11[2] * 0.005
 * applied to a typical spawn (param2=200, scale=1.0 → final 0.005)
 * yields a 1-world-unit half-extent (2-unit full quad).  At HOUSE
 * camera distances (~50 units) that's ~50 px in a 480-tall viewport.
 *
 * UVs left in [0,1] for FVF compliance; the diffuse-only color path
 * means the texture stage never samples them.
 *
 * Triangle-strip winding: v0=TL → v1=BL → v2=TR → v3=BR.  CULLMODE=NONE
 * is set on entry so winding choice doesn't matter for visibility. */
static pass_f_vertex g_pass_f_vbuf[4] = {
    { -100.0f,  100.0f, 0.0f, 0xFFFFFFFFu, 0.0f, 0.0f },  /* TL */
    { -100.0f, -100.0f, 0.0f, 0xFFFFFFFFu, 0.0f, 1.0f },  /* BL */
    {  100.0f,  100.0f, 0.0f, 0xFFFFFFFFu, 1.0f, 0.0f },  /* TR */
    {  100.0f, -100.0f, 0.0f, 0xFFFFFFFFu, 1.0f, 1.0f },  /* BR */
};

/* 9-color cycle from FUN_004161c7 L440-L467.  The engine builds three
 * 9-entry channel tables (local_100/c0/80) and indexes them by
 * `slot_index % 9`, then packs the result into ARGB at L473:
 *
 *   *puVar7 = ((R | 0xffffff00) << 8 | G) << 8 | B
 *           = 0xFF<<24 | R<<16 | G<<8 | B
 *
 * Pre-flatten that into a 9-entry ARGB table.  Values verbatim from
 * the decomp (local_100/c0/80 array literals). */
static const uint32_t g_pass_f_color_cycle[9] = {
    0xFFFF0000u,  /* i=0: R=0xFF G=0x00 B=0x00 — red    */
    0xFF00FF00u,  /* i=1: R=0x00 G=0xFF B=0x00 — green  */
    0xFF0000FFu,  /* i=2: R=0x00 G=0x00 B=0xFF — blue   */
    0xFFFFFF00u,  /* i=3: R=0xFF G=0xFF B=0x00 — yellow */
    0xFFFF00FFu,  /* i=4: R=0xFF G=0x00 B=0xFF — magenta*/
    0xFF00FFFFu,  /* i=5: R=0x00 G=0xFF B=0xFF — cyan   */
    0xFFFF7F00u,  /* i=6: R=0xFF G=0x7F B=0x00 — orange (local_100[6]=0xFF, c0[6]=0x7F, 80[6]=0) */
    0xFF7FFFFFu,  /* i=7: R=0x7F G=0xFF B=0xFF — pale cyan */
    0xFF007FFFu,  /* i=8: R=0x00 G=0x7F B=0xFF — sky blue  */
};

/* Compose the per-record world matrix matching the engine's L430-L438
 * chain: T(pos) → S(scale) → RotZ(rot.z) → RotY(rot.x) → RotX(rot.y).
 *
 * Each thunk_FUN_004a2a03 is D3DXMatrixMultiply(dest, A, B) with
 * dest = A * B.  Starting from M = T and reading the chain
 *   M = S * M
 *   M = RZ * M
 *   M = RY * M
 *   M = RX * M
 * gives final M = RX * RY * RZ * S * T.  Under row-vector convention
 * (v' = v * M), that applies rotations first, then scale, then
 * translate — standard "place a scaled, oriented object at world
 * coordinates". */
static void pass_f_compose_world(float out[16],
                                 float pos_x, float pos_y, float pos_z,
                                 float scale,
                                 float rot_x, float rot_y, float rot_z)
{
    float scratch[16];

    mat4_translation(out, pos_x, pos_y, pos_z);

    mat4_scaling(scratch, scale, scale, scale);
    mat4_mul(out, scratch, out);

    mat4_rotation_z(scratch, rot_z);
    mat4_mul(out, scratch, out);

    /* Engine quirk: rot-field-X drives RotationY thunk (3537), and
     * rot-field-Y drives RotationX thunk (35d3).  Verbatim.  See
     * docs/findings/scene1-particles-tick.md "Pass F" matrix chain. */
    mat4_rotation_y(scratch, rot_x);
    mat4_mul(out, scratch, out);

    mat4_rotation_x(scratch, rot_y);
    mat4_mul(out, scratch, out);
}

void scene1_pass_f_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    if (g_scene1_records_a_count <= 0) return;

    int count = g_scene1_records_a_count;
    if (count > SCENE1_RECORDS_A_COUNT) count = SCENE1_RECORDS_A_COUNT;

    /* Nothing-to-draw guard.  This MVP renders only type-0x92 quads, but it is
     * wired LIVE into the HOUSE render path, where records_a is now populated by
     * non-0x92 particles (e.g. the companion wing-glow, type 0x1f — §73).
     * Writing the state preamble below (texture-stage SELECTARG1, CULLMODE=NONE,
     * LIGHTING=FALSE) and then drawing nothing leaks that state into the rest of
     * the frame — a scene-wide lighting regression.  The engine's full
     * FUN_004161c7 manages state across all six passes; until that lands, treat
     * "no drawable 0x92 slot" as a no-op and leave device state untouched. */
    {
        int have_drawable = 0;
        for (int slot = 0; slot < count; slot++) {
            const int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
            if (r[SCENE1_RECORDS_A_OFF_TYPE] == 0x92 &&
                r[SCENE1_RECORDS_A_OFF_AGE]  >= 0) { have_drawable = 1; break; }
        }
        if (!have_drawable) return;
    }

    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* Diffuse-only texture stage: SELECTARG1 with COLORARG1 =
     * D3DTA_DIFFUSE.  No texture binding needed.  Engine path uses
     * MODULATE2X against the last-bound texture; we diverge here for
     * MVP simplicity (see scene1_pass_f.h "State setup"). */
    IDirect3DDevice8_SetTexture(dev, 0, NULL);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                          D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1,
                                          D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,
                                          D3DTOP_SELECTARG1);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1,
                                          D3DTA_DIFFUSE);

    /* Engine wide-followup preamble (FUN_004161c7 L42-L45) sets
     * CULLMODE=1 (NONE).  Replicated locally for the MVP path. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);

    IDirect3DDevice8_SetVertexShader(dev, SCENE1_PASS_F_FVF);

    for (int slot = 0; slot < count; slot++) {
        const int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];

        /* Engine L427 gate: type==0x92, type!=-1 (sentinel), age >= 0. */
        if (r[SCENE1_RECORDS_A_OFF_TYPE] != 0x92) continue;
        if (r[SCENE1_RECORDS_A_OFF_AGE]  <  0)    continue;

        /* L428-L429: scale formula.  piVar11[5]=PARAM2 is int (the
         * engine treats it as int → float value-convert); piVar11[2]
         * =SCALE is float bits in an int* slot (reinterpret). */
        float param2_f = (float)r[SCENE1_RECORDS_A_OFF_PARAM2];
        float scale_f  = *(const float *)&r[SCENE1_RECORDS_A_OFF_SCALE];
        float final_scale = (param2_f / 200.0f) * scale_f * 0.005f;

        float pos_x = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_X];
        float pos_y = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_Y];
        float pos_z = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_Z];
        float rot_x = *(const float *)&r[SCENE1_RECORDS_A_OFF_ROT_X];
        float rot_y = *(const float *)&r[SCENE1_RECORDS_A_OFF_ROT_Y];
        float rot_z = *(const float *)&r[SCENE1_RECORDS_A_OFF_ROT_Z];

        float world[16];
        pass_f_compose_world(world,
                             pos_x, pos_y, pos_z, final_scale,
                             rot_x, rot_y, rot_z);

        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                      (const D3DMATRIX *)world);

        uint32_t color = g_pass_f_color_cycle[slot % 9];
        for (int v = 0; v < 4; v++) {
            g_pass_f_vbuf[v].diffuse = color;
        }

        IDirect3DDevice8_DrawPrimitiveUP(dev,
                                         D3DPT_TRIANGLESTRIP,
                                         2,
                                         g_pass_f_vbuf,
                                         sizeof(pass_f_vertex));
    }
}

#endif /* _WIN32 */
