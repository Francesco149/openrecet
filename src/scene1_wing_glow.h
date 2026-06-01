/*
 * scene1_wing_glow.h — Tear companion wing-glow billboard renderer.
 *
 * Chip P0.1 (plans/freeroam-structural-parity.md).  Ports the records-A
 * type-0x1f arm of FUN_004176ff (decompile L3818-3921, draw ret_va
 * 0x41e165) — the bright translucent-blue sparkle that trails the flying
 * fairy.  The emit (scene1_companion_ctrl.c → scene1_spawn) and the sim
 * (scene1_particles_tick.c) were already ported; this is the missing
 * DRAW.  FUN_004176ff as a whole (30 KB, ~30 particle-type arms) stays
 * unported — this lands ONLY the 0x1f arm, modelled on scene1_pass_f.c
 * (the analogous records-A type-0x92 renderer).
 *
 * The full draw recipe was recovered from retail ground truth
 * (tools/dump_wingglow_groundtruth.py, runs/wingglow-d3d) because the
 * billboard vertex template (BSS &DAT_0064b548) and the blend envelope
 * are not statically derivable from the decompile.  See
 * docs/findings/scene1-wing-glow.md for the captured values.
 *
 * PORT-DEBT(stub, FUN_004176ff): only the records-A 0x1f arm is ported;
 * the other ~30 type arms + the records-B passes + the per-pass state
 * sequencing remain stubbed in scene1_walk_chr_TODO (dormant in HOUSE
 * free-roam beyond the glow).
 */
#ifndef SCENE1_WING_GLOW_H
#define SCENE1_WING_GLOW_H

#ifdef __cplusplus
extern "C" {
#endif

struct IDirect3DDevice8;

/*
 * Walk g_scene1_records_a for type-0x1f slots and draw one additive
 * blue glow billboard per slot.  No-op (leaves device state untouched)
 * when no 0x1f slot is live, matching the engine's count-gated sweep.
 * Call where FUN_004176ff sits in the render order (after the WIDE
 * re-projection, scene1_render.c L252-254).
 */
void scene1_wing_glow_render(struct IDirect3DDevice8 *dev);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_WING_GLOW_H */
