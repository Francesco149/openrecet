/*
 * scene1_walk_dust.c — see scene1_walk_dust.h for the chip writeup.
 *
 * Ports FUN_004176ff's records-A type-0xe arm (decompile L4958-5089): the
 * floor dust puff at the walking player's feet.
 *
 * Recipe (retail ground truth — Frida d3d-trace runs/walkdust-d3d at the
 * 0x41e97b DrawPrimitiveUP; per-slot pool-type watch runs/walkdust-types;
 * decompile + objdump of FUN_004176ff / FUN_0048b850):
 *   - Geometry  : the same static ±256 quad as the wing-glow, TRIANGLESTRIP
 *                 (prim_count 2), FVF 0x142 (XYZ|DIFFUSE|TEX1, stride 24).
 *                 Engine template &DAT_0064bf68 (40d132.c L758-772 init).
 *   - Texture   : bmp/effect.bmp (engine DAT_073cc8c0 → g_sysassets.effect_bmp).
 *   - UVs       : an AGE-ANIMATED 32x32 cell.  cell = min(age/3, 4); the cell
 *                 walks across columns 0..4 of effect.bmp row y≈160..191:
 *                   u in [(cell*32+0.5)/256, (cell*32+31.5)/256],
 *                   v in [0.6269531, 0.7480469]   (fixed, ≈ texel rows 160..191).
 *   - Blend     : ALPHABLENDENABLE=1, SRCBLEND=SRCALPHA, DESTBLEND=INVSRCCOLOR
 *                 (soft additive-ish white — NOT the 0x1f ONE/ONE).
 *   - Alpha test/Depth/Cull/Lighting/Tex-stage : same envelope as the 0x1f arm.
 *   - Per slot  : scale  = (age*0.0004 + 0.02) * SCALE_field * 0.8
 *                          (emit passes SCALE_field 0.125).
 *                 world  = RotZ(rot.z) * billboard * Scale * Translate(pos)
 *                          (rot.z is 0 for the dust; same compose as 0x1f).
 *                 diffuse= 0xFF<<24 | b<<16 | b<<8 | b, grey age-fade
 *                          b = 0x80; if (age > 0x10) b += (0x10 - age) * 8
 *                          (so b: 0x80 until age 16, then fades to 0 at age 32).
 *
 * PORT-DEBT(stub, FUN_004176ff L4958): this arm in the engine is SHARED by
 * types 0xe / 0x2b / 0x1b / 0x3b / 0x59 / 0x67 / 0x76, each with its own
 * diffuse builder / scale tweak.  Only the type-0xe path (the only one emitted
 * in HOUSE free-roam) is ported here; the others remain unported (none reach a
 * draw in free-roam).  Retire = full FUN_004176ff port.
 */

#include "scene1_walk_dust.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "math3d.h"
#include "scene1_records.h"
#include "scene1_camera.h"   /* g_scene1_camera_orient (= engine DAT_0438cdf8) */
#include "scene1_particles_tick.h" /* g_scene1_actor_pos */
#include "sysassets.h"       /* g_sysassets.effect_bmp (= engine DAT_073cc8c0) */
#include "tick.h"            /* g_tick.frame_count */

#define SCENE1_WALK_DUST_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)
#define WALK_DUST_TYPE 0xe

typedef struct {
    float    x, y, z;
    uint32_t diffuse;
    float    u, v;
} walk_dust_vertex;

/* effect.bmp row of the dust cells (engine literals 0x3e810000 / 0x3ebf0000 in
 * the V slots of &DAT_0064bf68; the arm overwrites only U per draw). */
#define WALK_DUST_V0 0.6269531f
#define WALK_DUST_V1 0.7480469f

/* ±256 TL/BL/TR/BR strip, same geometry as the wing-glow template (40d132.c).
 * Strip winding TL -> BL -> TR -> BR; CULLMODE=NONE.  diffuse + U overwritten
 * per slot below; V is the fixed dust row. */
static walk_dust_vertex g_walk_dust_vbuf[4] = {
    { -256.0f,  256.0f, 0.0f, 0xFFFFFFFFu, 0.0f, WALK_DUST_V0 }, /* TL */
    { -256.0f, -256.0f, 0.0f, 0xFFFFFFFFu, 0.0f, WALK_DUST_V1 }, /* BL */
    {  256.0f,  256.0f, 0.0f, 0xFFFFFFFFu, 0.0f, WALK_DUST_V0 }, /* TR */
    {  256.0f, -256.0f, 0.0f, 0xFFFFFFFFu, 0.0f, WALK_DUST_V1 }, /* BR */
};

/* Same world chain as the wing-glow arm: T, then left-mul S, billboard, RotZ. */
static void walk_dust_compose_world(float out[16],
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

void scene1_walk_dust_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;

    int count = g_scene1_records_a_count;
    if (count <= 0) return;
    if (count > SCENE1_RECORDS_A_COUNT) count = SCENE1_RECORDS_A_COUNT;

    /* Nothing-to-draw guard (see the wing-glow arm): leave device state alone
     * on an empty sweep. */
    int have = 0;
    for (int slot = 0; slot < count; slot++) {
        const int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
        if (r[SCENE1_RECORDS_A_OFF_TYPE] == WALK_DUST_TYPE) { have = 1; break; }
    }
    if (!have) return;

    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* Soft (SRCALPHA / INVSRCCOLOR) blend envelope (retail GT, ret_va 0x41e97b). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    /* Retail draws at AF=GREATER (d3d-trace 0x41e97b, runs/retail-bw-d3d4) — NOT
     * GreaterEqual.  With AREF=0 the distinction is load-bearing for this blend:
     * GE passes alpha==0 texels, and the INVSRCCOLOR dest term means a
     * colored-but-transparent effect.bmp texel still darkens the framebuffer;
     * GREATER rejects them, matching retail. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);

    /* COLOR = diffuse(grey age-fade) * texture(white puff); ALPHA = tex * diffuse. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    /* Texture filtering (retail GT, full state extract at the 0x41e97b draw):
     * MAG/MINFILTER=LINEAR, **MIPFILTER=NONE**.  Critical: the dust is tiny on
     * screen, so with mipmapping on it samples a high mip level where the 32×32
     * effect.bmp cell averages to a uniform grey — the soft puff's black corners
     * vanish and it draws as a flat grey SQUARE (heavier than retail + lightens
     * the table shadow it overlaps via INVSRCCOLOR).  The engine draws this pass
     * with mips off; the port must set it here because this arm now runs before
     * the alpha/sprite passes that otherwise drop mipmapping. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);

    IDirect3DDevice8_SetTexture(dev, 0,
        (IDirect3DBaseTexture8 *)g_sysassets.effect_bmp.tex);

    IDirect3DDevice8_SetVertexShader(dev, SCENE1_WALK_DUST_FVF);

    for (int slot = 0; slot < count; slot++) {
        const int32_t *r = &g_scene1_records_a[slot * SCENE1_RECORDS_A_STRIDE];
        if (r[SCENE1_RECORDS_A_OFF_TYPE] != WALK_DUST_TYPE) continue;

        int   age      = r[SCENE1_RECORDS_A_OFF_AGE];
        float scale_f  = *(const float *)&r[SCENE1_RECORDS_A_OFF_SCALE];
        float px       = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_X];
        float py       = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_Y];
        float pz       = *(const float *)&r[SCENE1_RECORDS_A_OFF_POS_Z];
        float rot_z    = *(const float *)&r[SCENE1_RECORDS_A_OFF_ROT_Z];

        /* L4963 scale; L5031-5040 brightness; L5021-5025 0xe grey pack. */
        float scale = (age * 0.0004f + 0.02f) * scale_f * 0.8f;

        int b = 0x80;
        if (age > 0x10) b += (0x10 - age) * 8;      /* fade to 0 by age 0x20 */
        if (b < 0) b = 0;
        uint32_t bu = (uint32_t)b;
        uint32_t color = ((bu | 0xffffff00u) << 8 | bu) << 8 | bu;

        /* L5070 animated cell: cell = min(age/3, 4), 32px columns. */
        int cell = age / 3;
        if (cell > 4) cell = 4;
        float u0 = ((float)(cell * 32) + 0.5f)  / 256.0f;
        float u1 = ((float)(cell * 32) + 31.5f) / 256.0f;

        g_walk_dust_vbuf[0].u = u0;  /* TL */
        g_walk_dust_vbuf[1].u = u0;  /* BL */
        g_walk_dust_vbuf[2].u = u1;  /* TR */
        g_walk_dust_vbuf[3].u = u1;  /* BR */
        for (int v = 0; v < 4; v++) g_walk_dust_vbuf[v].diffuse = color;

        float world[16];
        walk_dust_compose_world(world, px, py, pz, scale, rot_z);
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)world);

        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2,
                                         g_walk_dust_vbuf,
                                         sizeof(walk_dust_vertex));
    }
}

#endif /* _WIN32 */
