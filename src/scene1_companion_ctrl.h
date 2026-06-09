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

/* Per-frame companion tick (engine FUN_0048a833 spring-follow + wing-sparkle).
 * The engine nests FUN_0048a833 INSIDE the player controller FUN_0048b850 (just
 * before the foot dust), so on a free-roam walk frame the port calls this from
 * player_ctrl_b850_move(); scene1_sim.c ticks it on every other (non-walk) frame.
 * Does NOT advance the phase counter — call scene1_companion_ctrl_advance_phase()
 * after the foot dust for that.  No-op unless actor 2 is live (char != -1). */
void scene1_companion_ctrl_tick(void);

/* Advance the db054 phase counter (engine FUN_0048b850 tail, all.c:90242) — run
 * AFTER both the companion tick and the foot-dust emit have read db054, so the
 * wing-sparkle's RNG precedes the dust's yet the counter still ticks once/frame.
 * Gated cc04==0 (frozen while the display-stand menu is open; engine-quirks §110). */
void scene1_companion_ctrl_advance_phase(void);

/* db054 advance, EVENT-ARM variant — the unconditional `DAT_056db054++` at the
 * FUN_0048407f tail (all.c:84658).  No cc04 gate: the event arm owns the whole
 * frame (no menu path can have cleared cc04 mid-frame), and retail demonstrably
 * ticks db054 through every dialogue frame (item-display-2: db054 1205 at the
 * inter-dialogue gap while house_update never ran).  Called only from
 * scene1_event_actor_tail_tick(). */
void scene1_companion_ctrl_advance_phase_event(void);

/* Reset the per-scene hover-bob phase counter (engine DAT_056db054).  Called on
 * HOUSE entry from scene1_postload, alongside the actor seed. */
void scene1_companion_ctrl_reset(void);

/* Read the shared per-frame phase counter DAT_056db054 (the companion bob phase).
 * Other db054 readers (the player foot-dust emit) call this; it returns the
 * current frame's value (the counter is bumped by scene1_companion_ctrl_advance_
 * phase() only AFTER all of this frame's readers have run). */
int scene1_companion_db054(void);

/* Trace-harness ONLY: normalize the companion's load-time-dependent free-roam
 * phase (db054 bob/sparkle counter + the sprite anim FRAME/TIMER/COUNTER) to a
 * canonical zero, so a port↔retail trace comparison (both pinned at the same
 * anchor) is phase-clean.  Wired to the segtrace `{phasepin}` op; the shipped
 * game never calls it.  See the .c banner + engine-quirks §94. */
void scene1_companion_ctrl_phasepin(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_COMPANION_CTRL_H */
