/*
 * scene1_walk_dust.h — HOUSE free-roam foot dust (records-A type 0xe).
 *
 * Ports the type-0xe arm of FUN_004176ff (decompile L4958-5089, draw ret_va
 * 0x41e97b): the faint white wispy smoke puff that kicks up at Recette's feet
 * while she walks.  The particle is emitted by the player controller
 * (FUN_0048b850; player_ctrl foot-dust emit) and aged/killed by
 * scene1_particles_tick (the 0xe/0x2b/0x1b/0x3b/0x76 gated scaled-drift handler,
 * kill at age 0x20) — both already ported; this chip adds the missing DRAW.
 *
 * Sibling of scene1_wing_glow.c (the type-0x1f arm) — same FUN_004176ff,
 * same effect.bmp sheet, same ±256 billboard template, different per-type
 * UV/diffuse/scale/blend.  See docs/findings/scene1-walk-dust.md.
 */
#ifndef OPENRECET_SCENE1_WALK_DUST_H
#define OPENRECET_SCENE1_WALK_DUST_H

#ifdef __cplusplus
extern "C" {
#endif

struct IDirect3DDevice8;

/* Draw every live records-A type-0xe slot as a floor dust billboard.  No-op
 * (touches no device state) when none are live, like the wing-glow arm. */
void scene1_walk_dust_render(struct IDirect3DDevice8 *dev);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_WALK_DUST_H */
