/*
 * scene1_chr_shadow.c — Csh.1: HOUSE character ground shadow (FUN_0045aa36).
 * See scene1_chr_shadow.h for the chip writeup + the per-block ledger.
 */

#include "scene1_chr_shadow.h"

#include "math3d.h"

/* ── pure per-actor shadow builder (host-tested) ───────────────────────────
 * Engine FUN_0045aa36 Block A, L66-119 / objdump @ 0x45ab90-0x45ae44.
 * Constants recovered from .rdata (see header):
 *   0x51953c = 5.0     alpha = (int)(height * 5.0)
 *   0x5199dc = 0.0015  size slope
 *   0x519da4 = 0.038   size cap        0x5193f4 = 0.025  size floor
 *   0x519c7c = 0.14    size scale      0x519718 = 0.9    companion size mult
 *   0x519748 = 0.7     |n.y| floor-flatness cutoff
 *   0x519b20 = -100.0  "no floor" sentinel (we use floor_hit instead)
 *   0x5198d8 = 0.2     plane point Y   0x519a68 = 0.12   shadow Y lift
 */
void chr_shadow_build_actor(int i, const float pos[3],
                            float scale_xz, float scale_y, int alive,
                            int floor_hit, float floor_y,
                            const float floor_normal[3],
                            chr_shadow_params *out)
{
    out->draw = 0;

    /* L68: char-id slot in use + a floor under the actor (engine's
     * DAT_056daf94 != -100 is our floor_hit). */
    if (!alive || !floor_hit)
        return;

    /* L70-74: |floor normal.y| >= 0.7 — skip on near-vertical surfaces. */
    float ny = floor_normal[1] < 0.0f ? -floor_normal[1] : floor_normal[1];
    if (ny < 0.7f)
        return;

    /* L110-111: both render scales must be positive. */
    if (!(scale_xz > 0.0f) || !(scale_y > 0.0f))
        return;

    /* height above the floor → alpha + size (engine L75-95). */
    float height = pos[1] - floor_y;          /* da1dc[i*3] - daf94[i] */

    int alpha = (int)(height * 5.0f);         /* __ftol: truncate toward zero */

    float size = 0.038f - height * 0.0015f;
    if (size > 0.038f) size = 0.038f;
    if (size < 0.025f) size = 0.025f;
    size *= 0.14f;

    if (i == 2) {                             /* companion (Tear) tweak */
        size *= 0.9f;
        alpha += 0x40;
    }
    if (alpha < 0)     alpha = 0;
    if (alpha > 0xff)  alpha = 0xff;

    /* world = Shadow(light,plane) · Scaling(-size,size,size)
     *                             · Translation(pos.x, floor_y+0.12, pos.z)
     * built exactly as the engine: W1 = Scaling*Translation (L99-100), then
     * W = Shadow*W1 (L108-109).  mat4_mul(out,a,b) == a*b == D3DXMatrixMultiply. */
    float trans[16], scale[16], w1[16], shadow[16];
    mat4_translation(trans, pos[0], floor_y + 0.12f, pos[2]);
    mat4_scaling(scale, -size, size, size);
    mat4_mul(w1, scale, trans);

    float point[3]  = { 0.0f, 0.2f, 0.0f };
    float normal[3] = { -floor_normal[0], floor_normal[1], -floor_normal[2] };
    float plane[4];
    plane_from_point_normal(plane, point, normal);

    float light[4] = { 0.0f, 1.0f, 0.0f, 0.0f };   /* engine local_8c = {0,1,0,0} */
    mat4_shadow(shadow, light, plane);
    mat4_mul(out->world, shadow, w1);

    /* colour = 0xFF<a><a><a> — opaque grey (engine packs at 0x45ae01). */
    out->color = 0xff000000u | ((uint32_t)alpha << 16)
               | ((uint32_t)alpha << 8) | (uint32_t)alpha;
    out->draw = 1;
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>
#ifdef SHADOW_DEBUG
#include <stdio.h>
#endif

#include "call_trace.h"             /* CALL_TRACE_ENTER probe */
#include "sysassets.h"              /* g_sysassets.shade_bmp (DAT_073cc8f0) */
#include "scene1_particles_tick.h"  /* g_scene1_actor_pos[3][3] (DAT_056da1d8..) */
#include "scene1_player_ctrl.h"     /* player_ctrl_actor_char / _scale_xz / _scale_y */
#include "collision_house.h"        /* collision_house_get */
#include "collision_query.h"        /* collision_query_ground (FUN_00432e50) */

#define SHADOW_FVF (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)   /* 0x142 */

/* The ±256 object-space quad template, engine DAT_0064bd88 (init at all.c
 * L9111-9134): a flat XZ quad drawn as a 2-prim TRIANGLESTRIP, UVs sampling
 * the 64×64 shade.bmp blob ((0.5..63.5)/256).  Only the diffuse colour is
 * patched per draw (engine FUN_0040d11b); XYZ+UV are constant. */
typedef struct { float x, y, z; uint32_t color; float u, v; } shadow_vertex;

#define SH_UV_LO  0.001953125f   /*  0.5/256 = 0x3b000000 */
#define SH_UV_HI  0.248046875f   /* 63.5/256 = 0x3e7e0000 */

static const shadow_vertex SHADOW_QUAD[4] = {
    { -256.0f, 0.0f,  256.0f, 0xffffffffu, SH_UV_LO, SH_UV_LO },
    { -256.0f, 0.0f, -256.0f, 0xffffffffu, SH_UV_LO, SH_UV_HI },
    {  256.0f, 0.0f,  256.0f, 0xffffffffu, SH_UV_HI, SH_UV_LO },
    {  256.0f, 0.0f, -256.0f, 0xffffffffu, SH_UV_HI, SH_UV_HI },
};

/* Number of actor slots Block A iterates: engine local_10, default 3 (the
 * DAT_0438b1a0==1 arm that drops it to 1 is a combat/menu mode, off in
 * free-roam).  Slot 0 = player, slot 1 = disabled guest (char -1, skipped),
 * slot 2 = companion. */
#define SHADOW_ACTOR_SLOTS 3

void scene1_chr_shadow_render(struct IDirect3DDevice8 *dev_in)
{
    /* Probe — the shadow pass FUN_0045aa36 @ 0x45aa36. */
    CALL_TRACE_ENTER(0x45aa36u);

    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    const sprite_t *shade = &g_sysassets.shade_bmp;
    if (shade->tex == NULL)
        return;                         /* shade.bmp not loaded → nothing to draw */

    /* ── render-state envelope (engine L36-54, objdump @ 0x45aa36) ───────── */
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)shade->tex);
    IDirect3DDevice8_SetVertexShader(dev, SHADOW_FVF);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZFUNC,            D3DCMP_LESSEQUAL);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_NORMALIZENORMALS, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF,         0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE,         D3DCULL_NONE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE,        FALSE);
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING,         FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADD);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,          TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE,     FALSE);
    /* Final blend is multiplicative darkening: result = DST · SRC.rgb. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,         D3DBLEND_ZERO);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,        D3DBLEND_SRCCOLOR);

    /* ── Block A: player + companion ground shadow (engine L59-121) ──────── */
    const collision_mesh *cm = collision_house_get();
    for (int i = 0; i < SHADOW_ACTOR_SLOTS; i++) {
        int alive = (player_ctrl_actor_char(i) != -1);
        if (!alive)
            continue;

        const float *pos = g_scene1_actor_pos[i];

        /* Floor under the actor — the FUN_00432e50 query the engine caches in
         * DAT_056daf94 (height) / DAT_056daebc.. (normal) via FUN_00483170.
         * The engine queries at the actor's y and its FUN_00432e50 hits even
         * when y == floor (internal +1.5 reference, decomp L140); the port's
         * collision_query_ground needs that raise made explicit, exactly as the
         * movement resolver does (collision_resolve.c CR_HEAD_HEIGHT) — without
         * it a grounded player (y == floor == 0) misses and gets no shadow.
         * `height` below still uses the true pos.y, so the lift only fixes the
         * hit, not the size/alpha. */
        const float SHADOW_FLOOR_PROBE_LIFT = 1.5f;   /* == CR_HEAD_HEIGHT */
        collision_hit hit;
        hit.hit = 0;
        if (cm)
            collision_query_ground(cm, pos[0], pos[1] + SHADOW_FLOOR_PROBE_LIFT,
                                   pos[2], &hit);

        chr_shadow_params p;
        chr_shadow_build_actor(i, pos,
                               player_ctrl_actor_scale_xz(i),
                               player_ctrl_actor_scale_y(i),
                               alive, hit.hit, hit.height, hit.normal, &p);
#ifdef SHADOW_DEBUG
        fprintf(stderr, "SHADOW i=%d pos=(%.3f,%.3f,%.3f) hit=%d floor=%.3f "
                "n=(%.2f,%.2f,%.2f) height=%.3f draw=%d color=%08x\n",
                i, pos[0], pos[1], pos[2], hit.hit, hit.height,
                hit.normal[0], hit.normal[1], hit.normal[2],
                pos[1] - hit.height, p.draw, p.color);
#endif
        if (!p.draw)
            continue;

        shadow_vertex quad[4];
        for (int v = 0; v < 4; v++) {
            quad[v] = SHADOW_QUAD[v];
            quad[v].color = p.color;     /* engine FUN_0040d11b colour patch */
        }

        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)p.world);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2,
                                         quad, sizeof(shadow_vertex));
    }

    /* ── dormant blocks (engine L122-365), HOUSE free-roam ledger ──────────
     * Every remaining shadow consumer walks a table HOUSE free-roam leaves
     * empty, so none draw.  Kept as documented stubs (the scene1_shop_walker
     * count-stub convention) until their tables are modelled:
     *   L122 FUN_00470385  — object/furniture shadow blobs (DAT_073a6e84,
     *                        count DAT_005c7dd0) + ambient motes FUN_0046f648.
     *                        The "missing table contact-shadow" in
     *                        scene1-house-render-gaps.md §4; needs the object
     *                        table model — a clean follow-up chip.
     *   L123 Block B       — customer / people shadows (DAT_0076c374 stride
     *                        0x2e9): no customers in free-roam.
     *   L189 Block C       — DAT_069324d4 effects (count DAT_0076b964 == 0).
     *   L234 Block D       — DAT_069b2fb0 type-0x50 (count DAT_0076b960 == 0).
     *   L266 Block E       — combat projectile shadows (DAT_0695f004); gated
     *                        DAT_0438b17c, no projectiles in HOUSE.
     *   L297 Block F       — DAT_06956cb4 (count DAT_0076b968 == 0).
     *   L347 Block G       — DAT_0438cc08==1 ground-decal special (DAT_073d8748).
     */

    /* ── render-state teardown (engine L366-373) ─────────────────────────── */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,     D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,    D3DBLEND_ONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetVertexShader(dev, SHADOW_FVF);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    /* L374-394 tail: DAT_056dae90 > 1 spawn/teleport flash (binds DAT_073de650).
     * DAT_056dae90 is the entry-poof counter, <= 1 in steady free-roam → dormant. */
}

#endif /* _WIN32 */
