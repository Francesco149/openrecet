/*
 * scene1_companion_ctrl.h — HOUSE companion (Tear / actor 2) controller.
 *
 * Engine FUN_0048a833 (0x48a833, 3011 B), the per-frame companion driver in the
 * free-roam call set (engine-quirks §60/§71).  It drives actor 2 — the bobbing
 * fairy companion (char id 1, position DAT_056da1f0/f4/f8 = the particle
 * "spawn origin" alias) — which hover-follows the player.  Actor 1 (char 3) is
 * disabled at free-roam (DAT_056da1d0 → -1, set by FUN_00436f97), so it never
 * renders; this controller only handles actor 2.
 *
 * Ground truth: runs/companion-truth/FINDINGS.md (retail Frida capture, the
 * controllable tour frames 4588+).  The visible HOUSE free-roam behaviour is the
 * spring-follow helper FUN_0048a4d1 (which FUN_0048a833 invokes for the
 * controllable companion) — NOT a fixed ±side offset:
 *
 *   d = comp.xz − player.xz;  dist = |d|
 *   desired = (dist > 1.5) ? player.xz + dir(d)·1.5 : comp.xz   (trail at 1.5)
 *   vel = (desired − comp.xz)·0.15,  |vel| clamped to 0.35;  comp.xz += vel
 *   comp.y += (sin(db054·0.04)·0.2 + ground_y + 3.0 − comp.y)·0.15   (hover bob)
 *   moved this frame ? → walk anim + copy the PLAYER's facing octant : idle
 *
 * i.e. the fairy springs to stay ~1.5 units from the player, hovering with a
 * slow Y bob, and faces the way the player faces while catching up.  Validated:
 * replaying retail's player trajectory through this law reproduces the
 * companion's XZ to one-step mean 0.0036, facing 621/621.
 *
 * FUN_0048a833's other branches (intro standing-pose, random-wander) are
 * retail-only (gated behind the unported §60 event-gate; the port reaches
 * free-roam directly) and are intentionally not ported — see FINDINGS.md.
 *
 * Wing-glow sparkle (engine-quirks §73): the controller also ports FUN_0048a833's
 * tail emit (LAB_0048b2a0) — one type-0x1f particle dropped just off the fairy
 * along her facing every 4th frame.  Faithful but invisible today; the table-A
 * glow-billboard renderer (FUN_004176ff) is unported, so the spawned particle is
 * ticked + killed but not drawn.  See the emit block in the .c for the gate.
 */
#ifndef OPENRECET_SCENE1_COMPANION_CTRL_H
#define OPENRECET_SCENE1_COMPANION_CTRL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Per-frame companion tick.  Wired into scene1_ingame_default_arm_tick right
 * after scene1_player_ctrl_tick (the engine runs FUN_0048a833 after the player
 * controller).  No-op unless actor 2 is live (char != -1). */
void scene1_companion_ctrl_tick(void);

/* Reset the per-scene hover-bob phase counter (engine DAT_056db054).  Called on
 * HOUSE entry from scene1_postload, alongside the actor seed. */
void scene1_companion_ctrl_reset(void);

/* Read the shared per-frame phase counter DAT_056db054 (the companion bob phase).
 * Other db054 readers (the player foot-dust emit) call this; it returns the
 * current frame's value (this counter is incremented at the end of the companion
 * tick, which runs after the player tick). */
int scene1_companion_db054(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_COMPANION_CTRL_H */
