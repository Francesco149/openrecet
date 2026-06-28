/*
 * scene1_chr_shadow.c — Csh.1: HOUSE character ground shadow (FUN_0045aa36).
 * See scene1_chr_shadow.h for the chip writeup + the per-block ledger.
 */

#include "scene1_chr_shadow.h"

#include <math.h>

#include "math3d.h"

/* C3a glow scale — engine immediate 0x3b712c27 (= 0.0036799998, the spec's
 * "0.003685" was a lossy rounding); X mirrored (-) like the shadow scale. */
#define DISPLAY_GLOW_SCALE 0.0036799998f

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

/* ── C3a faced-display-cell glow builder (FUN_0045aa36 Block G) ──────────────
 * The world xform + pulsing diffuse for the orange cell decal.  Matrix order
 * matches Block A / the engine: world = Scaling · Translation (D3DXMatrixMultiply
 * = a·b).  Alpha + the 0.05/32/159 constants are float (objdump 0x45b902-94a). */
void chr_shadow_build_display_glow(float render_x, float render_z,
                                   uint32_t sim_frame,
                                   float out_world[16], uint32_t *out_color)
{
    float trans[16], scale[16];
    mat4_translation(trans, render_x, 1.9f, render_z);
    mat4_scaling(scale, -DISPLAY_GLOW_SCALE, DISPLAY_GLOW_SCALE, DISPLAY_GLOW_SCALE);
    mat4_mul(out_world, scale, trans);

    int alpha = (int)(sinf((float)sim_frame * 0.05f) * 32.0f + 159.0f);
    *out_color = ((uint32_t)alpha << 24) | 0x00ffffffu;
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
#include "collision_resolve.h"      /* g_scene1_player_floor (cached daf94[0]) */
#include "scene1_bg_npc.h"          /* scene1_bg_npc_shadow_render (FUN_0046f648) */
#include "scene1_shop_walker.h"     /* scene1_customer_npc_shadow_render (FUN_00470385) */
#include "scene1_shop_display.h"    /* shop_display_cbfc/cc00/render_x/render_z/bf68 — Block G gate */
#include "sim.h"                    /* g_sim_frame_count (DAT_0438b8cc) — glow pulse phase */

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

/* C3a glow quad — engine DAT_0064c388 (init all.c:9087-9105).  Same ±256 XZ
 * template as the shadow quad, but sampling item_win.tga's 63² orange-glow
 * patch at texel centres (224.5,480.5)-(287.5,543.5).  TRIANGLESTRIP, 2 prims;
 * only the diffuse is patched per draw (FUN_0040d11b).  UV hex verified vs the
 * .bss init (the spec's "225/288" were integer roundings of 224.5/287.5). */
#define GLOW_U_LO  (224.5f / 1024.0f)   /* 0x3e608000 */
#define GLOW_U_HI  (287.5f / 1024.0f)   /* 0x3e8fc000 */
#define GLOW_V_LO  (480.5f / 1024.0f)   /* 0x3ef04000 */
#define GLOW_V_HI  (543.5f / 1024.0f)   /* 0x3f07e000 */

static const shadow_vertex GLOW_QUAD[4] = {
    { -256.0f, 0.0f,  256.0f, 0xffffffffu, GLOW_U_LO, GLOW_V_LO },
    { -256.0f, 0.0f, -256.0f, 0xffffffffu, GLOW_U_LO, GLOW_V_HI },
    {  256.0f, 0.0f,  256.0f, 0xffffffffu, GLOW_U_HI, GLOW_V_LO },
    {  256.0f, 0.0f, -256.0f, 0xffffffffu, GLOW_U_HI, GLOW_V_HI },
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

        /* Floor under the actor.  The engine's shadow (FUN_0045aa36) is a pure
         * READER of the per-actor floor cache DAT_056daf94 (height) / DAT_056daebc
         * (normal), filled by FUN_00483170 / FUN_0048a833 every house_update frame.
         * For the PLAYER (actor 0) the port now mirrors that exactly: read the
         * cached g_scene1_player_floor (= daf94[0], the SAME FUN_00483170 query,
         * pre-snap +1.5 probe) instead of a live query — so during a conversation-
         * pose cutscene (when the player tick, hence the floor writer, doesn't run)
         * Recette's shadow rides the FROZEN floor like retail, the same value the
         * companion hover freezes on (RE §18.4).  On flat floors the cache == a live
         * query, so the shadow stays bit-exact in every other scene.  Other actors
         * still live-query — the engine's FUN_0048a833 per-actor daf94 cache is
         * unported (PORT-DEBT(cs-shadow-frozen-floor)); moot here since the companion
         * draws no contact shadow in this scene.
         * The engine adds +1.5 inside FUN_00432e50 (decomp L140 / CR_HEAD_HEIGHT) so
         * a grounded actor (y == floor) still hits; `height` below uses the true
         * pos.y, so the lift only fixes the hit, not the size/alpha. */
        collision_hit hit;
        hit.hit = 0;
        if (i == 0) {
            hit = g_scene1_player_floor;
        } else if (cm) {
            const float SHADOW_FLOOR_PROBE_LIFT = 1.5f;   /* == CR_HEAD_HEIGHT */
            collision_query_ground(cm, pos[0], pos[1] + SHADOW_FLOOR_PROBE_LIFT,
                                   pos[2], &hit);
        }

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

    /* ── L122 FUN_00470385: background-NPC + in-shop-customer shadow blobs ──
     * FUN_00470385 draws the 6 background-window NPC contact shadows
     * (FUN_0046f648) FIRST, then the in-shop browsing-CUSTOMER chibi contact
     * shadows (DAT_073a6e84 = the chibi slot[0xd], count DAT_005c7dd0 = the
     * customer cap — NOT object/furniture as an earlier note guessed).  Both are
     * soft dark shade.bmp blobs tinted 0xff202020, drawn here inside this pass's
     * render envelope and multiplied into the floor: the bg-NPC townsfolk (their
     * bright sprites draw in the shop-walker pass) + the wandering customers
     * (their bright billboards draw in scene1_customer_npc_sprite_render). */
    scene1_bg_npc_shadow_render(dev);
    scene1_customer_npc_shadow_render(dev);

    /* ── dormant blocks (engine L123-346), HOUSE free-roam ledger ───────────
     * Every one of these shadow consumers walks a table HOUSE free-roam leaves
     * empty, so none draw.  Kept as documented stubs (the scene1_shop_walker
     * count-stub convention) until their tables are modelled:
     *   L123 Block B       — customer / people shadows (DAT_0076c374 stride
     *                        0x2e9): no customers in free-roam.
     *   L189 Block C       — DAT_069324d4 effects (count DAT_0076b964 == 0).
     *   L234 Block D       — DAT_069b2fb0 type-0x50 (count DAT_0076b960 == 0).
     *   L266 Block E       — combat projectile shadows (DAT_0695f004); gated
     *                        DAT_0438b17c, no projectiles in HOUSE.
     *   L297 Block F       — DAT_06956cb4 (count DAT_0076b968 == 0).
     */

    /* ── Block G: cc08==1 faced-display-cell orange glow (engine L347-364,
     * objdump 0x45b8c2-0x45b952) — C3a.  A flat item_win decal laid over the
     * display cell the player faces, pulsing alpha, drawn with a standard
     * SRCALPHA/INVSRCALPHA blend + MODULATE (overriding Block A's multiplicative
     * darkening for this one quad).  Gate: free-roam (cc08==1), the faced cell
     * is a stand (cbfc/cc00 != -1) and unobstructed (bf68==0).  The teardown
     * below restores the blend; item_win is the always-loaded boot UI atlas. */
    if (player_ctrl_cc08() == 1 && shop_display_bf68() == 0 &&
        shop_display_cbfc() != -1 && shop_display_cc00() != -1 &&
        g_sysassets.item_win_tga.tex != NULL) {
        IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        IDirect3DDevice8_SetTexture(dev, 0,
            (IDirect3DBaseTexture8 *)g_sysassets.item_win_tga.tex);

        float world[16];
        uint32_t color;
        chr_shadow_build_display_glow(shop_display_render_x(),
                                      shop_display_render_z(),
                                      g_sim_frame_count, world, &color);

        shadow_vertex quad[4];
        for (int v = 0; v < 4; v++) {
            quad[v] = GLOW_QUAD[v];
            quad[v].color = color;       /* engine FUN_0040d11b colour patch */
        }

        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)world);
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2,
                                         quad, sizeof(shadow_vertex));
    }

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
