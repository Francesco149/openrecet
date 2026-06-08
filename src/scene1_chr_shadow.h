/*
 * scene1_chr_shadow.h — Csh.1: HOUSE character (Recette / Tear) ground shadow.
 *
 * Port of FUN_0045aa36 @ 0x45aa36 (4493 B) — the scene-1 shadow pass that runs
 * between FUN_00459847(0) and the second narrow-frustum walker (engine
 * FUN_00459dfd L204-205).  The function draws ground shadows for SEVEN distinct
 * actor/effect tables, all using the same recipe: a ±256 unit quad
 * (DAT_0064bd88) textured with shade.bmp (DAT_073cc8f0), projected onto the
 * actor's floor plane by a D3DXMatrixShadow, colour-keyed grey by an
 * age/height alpha, and drawn with a multiplicative (SRCBLEND=ZERO,
 * DESTBLEND=SRCCOLOR) darkening blend.
 *
 * Csh.1 ports the engine's render-state envelope (verbatim) plus **Block A** —
 * the live player + companion actor shadow (engine L59-121, the `DAT_056da1b8`
 * actor table) — which is the only block that draws in HOUSE free-roam.  The
 * remaining six blocks walk tables that HOUSE free-roam leaves empty (customers
 * / objects / combat projectiles / spawn-flash) and are documented dormant
 * stubs gated on their engine guards; see the .c for the per-block ledger.
 *
 * Geometry (per live actor i, engine L66-119, objdump @ 0x45ab90-0x45ae44):
 *   height = pos.y - floor_y                       (actor height above floor)
 *   alpha  = clamp((int)(height * 5.0), 0..255)     (grey level; +0x40 for i==2)
 *   size   = clamp(0.038 - height*0.0015, .025..038) * 0.14  (×0.9 for i==2)
 *   world  = Shadow(light=(0,1,0,0), plane) · Scaling(-size,size,size)
 *                                            · Translation(pos.x, floor_y+0.12, pos.z)
 *   plane  = PlaneFromPointNormal((0,0.2,0), (-n.x, n.y, -n.z))   n = floor normal
 *   colour = 0xFF<a><a><a>                          (opaque grey, a = alpha)
 *   gates  : char != -1 ; floor hit (height ref != -100) ; |n.y| >= 0.7 ;
 *            scale_xz > 0 ; scale_y > 0
 * The floor height + normal are the FUN_00432e50 query already ported as
 * collision_query_ground (W4.2); the engine caches them per-actor in
 * DAT_056daf94 / DAT_056daebc.. via FUN_00483170, we re-query at draw time.
 *
 * i==2 is the companion (Tear): a slightly smaller (×0.9), more opaque (+0x40)
 * shadow — engine L86-89 keys this on the actor index == 2.
 */
#ifndef OPENRECET_SCENE1_CHR_SHADOW_H
#define OPENRECET_SCENE1_CHR_SHADOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-actor shadow draw parameters (host-testable core output). */
typedef struct {
    int      draw;       /* 1 = emit the quad, 0 = gated out */
    float    world[16];  /* SetTransform(WORLD) matrix (row-major) */
    uint32_t color;      /* diffuse 0xFF<a><a><a> */
} chr_shadow_params;

/*
 * Build the shadow draw parameters for actor `i` (0 = player, 2 = companion).
 * `pos` is the actor world position, `scale_xz`/`scale_y` its render scales
 * (engine DAT_056dae18 / DAT_056dae24), `alive` the char-id slot gate (char
 * != -1).  `floor_hit` / `floor_y` / `floor_normal` are the collision-query
 * result under the actor.  Returns out->draw = 1 when all gates pass.
 *
 * Pure: no globals, no D3D — exercised by the host unit tests.
 */
void chr_shadow_build_actor(int i, const float pos[3],
                            float scale_xz, float scale_y, int alive,
                            int floor_hit, float floor_y,
                            const float floor_normal[3],
                            chr_shadow_params *out);

/*
 * Build the C3a faced-display-cell orange glow decal (FUN_0045aa36 Block G,
 * asm 0x45b8e0-0x45b94f).  A flat item_win patch laid on the display surface
 * over the cell the player faces, with a pulsing alpha.  `render_x`/`render_z`
 * are the cell's pre-computed world position (_DAT_0438cbf4 / _DAT_0438cbf8),
 * `sim_frame` the pinned sim counter (DAT_0438b8cc) that drives the pulse.
 *
 *   world  = Scaling(-0.0036799998, +s, +s) · Translation(render_x, 1.9, render_z)
 *   alpha  = (int)(sinf(sim_frame*0.05)*32 + 159)   (pulses 127..191)
 *   colour = (alpha<<24) | 0xffffff                 (white, pulsing alpha)
 *
 * Pure: no globals, no D3D — exercised by the host unit tests.  The gate
 * (cc08==1 && bf68==0 && cbfc!=-1 && cc00!=-1), texture bind and draw live in
 * scene1_chr_shadow_render.
 */
void chr_shadow_build_display_glow(float render_x, float render_z,
                                   uint32_t sim_frame,
                                   float out_world[16], uint32_t *out_color);

#ifdef _WIN32
struct IDirect3DDevice8;
/*
 * The full FUN_0045aa36 pass: render-state envelope + Block A (player +
 * companion ground shadows).  Call site = scene1_render.c, replacing
 * scene1_walk_narrow_followup_TODO().
 */
void scene1_chr_shadow_render(struct IDirect3DDevice8 *dev);
#endif

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_CHR_SHADOW_H */
