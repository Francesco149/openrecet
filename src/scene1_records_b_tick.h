/*
 * scene1_records_b_tick.h — per-tick integrator for table B records.
 *
 * Chip C8j-tick.1 (2026-05-25) landed the SKELETON of engine FUN_0043ae20
 * @ 0x43ae20 (25750 B Mt. Everest); sub-chips C8j-tick.2..C8j-tick.13
 * fill the per-type bodies.  See docs/findings/scene1-records-b-tick.md
 * for the chip ladder.
 *
 * Outer-loop semantics (C8j-tick.1):
 *
 *   - Walk 512 slots (engine `local_2c == 0x200`).
 *   - Skip-dead-slot gate (`slot[TYPE] == 0` → LAB_0043fbbc).
 *   - Per-tick flag clear (engine DAT_06a46f98 = 0; modeled as
 *     `g_scene1_records_b_tick_flag = 0`, PHC #21).
 *   - Preamble: pos.{x,y,z} += vel.{x,y,z}; age++ — per engine L36456-62.
 *   - Per-type dispatch via `dispatch_default()` (sub-chip-extended
 *     switch table inside scene1_records_b_tick.c).
 *   - Kill helper `scene1_records_b_tick_kill_slot()` mirrors engine's
 *     LAB_004411e3 (`*piVar14 = 0`) — sets TYPE field to 0 to mark dead.
 *
 * C8j-tick.2 (2026-05-25) adds bodies for the anchor cascade:
 *
 *   - 0x1e / 0x2f / 0x88 / 0x9a — joint-table-anchored NPC effect, with
 *     a sub-dispatch (only for 0x1e) on owner+0x424 NPC motion-style ID:
 *     {0x48, 0x4b} → angle+0.3; 0x4c → angle-0.3; else → ALT_POS+1.5
 *     billboard.  Shared LAB_0043b205 tail sets drag, runs an iter loop
 *     of `state_machine + pos += vel` (anchored back), and kills at the
 *     per-type AGE threshold.
 *   - 0x89 / 0x9e — compass-direction billboard: pos anchored to owner
 *     pose with sin/cos(owner+0x18 / 8 * 2π) * radius; for 0x89 with
 *     age>10 spawns two scene1_overlay slots per tick (types 0xf and
 *     0x41); same iter-loop shape as the anchor cascade (reverse order:
 *     pos += vel BEFORE state_machine).  Kills on owner+0x428 != 1 OR
 *     age == 0xaf.
 *
 * C8j-tick.4 (2026-05-25) adds Body 1 (L689-L812) — kill-on-ground +
 * overlay-spawn cluster covering 10 types {2, 3, 4, 0x22, 0x54, 0x67,
 * 0x6d, 0x6e, 0x6f, 0x70}.  All bodies share:
 *
 *   - DRAG load per type (2.0 / 1.5 / 3.5 / 5.5 / 2.5).
 *   - Two-way pose dispatch on slot[FLAG_B] sign:
 *       FLAG_B <  0: pose = owner_a[+0x20..+0x28] + (sin(ROT_X), 1, cos(ROT_X));
 *                    ALT_POS = owner+0x20 + per_part_scale*(sin/cos, 0, cos)
 *                    where per_part_scale = (float)slot[PART_IDX] * -0.4.
 *                    Types 0x6d/0x6e/0x6f/0x70 additionally lift POS_Y by 1.
 *       FLAG_B >= 0: joint table at owner_a + FLAG_B*0x44 + 0x9e0; pose
 *                    same shape (joint.x+sin, joint.y+1, joint.z+cos);
 *                    ALT_POS is a DIRECT COPY of joint base + (0,1,0).
 *                    Engine still calls sinf/cosf in this branch but
 *                    DISCARDS the results (asm 0x43c214/0x43c256 fstp st0).
 *   - Type 0x67 only: spawn overlay 0xd at offset (sin(age*0.5)*4 + pos.x,
 *                     pos.y, cos(age*0.5)*4 + pos.z), scale 1.0, dur -1.
 *   - PART_IDX == 0 && AGE in [6, 10): up to 5 state_machine calls
 *     (engine FUN_0043865e); breaks early when state_machine reports 0
 *     ("no progress").  Per-iter reset of g_scene1_records_b_tick_anim_drive
 *     (DAT_06a46f94) BEFORE the call, and read AFTER.  When type==4 AND
 *     state_machine returned 1 AND anim_drive > 0: scale by /10 (floor 1),
 *     write to owner_a+0xe30 + owner_a+0xe38 = 0x1e.
 *   - Kill on owner_a+0xcf8 != 0 OR AGE == 0x14.
 *
 * Body 1 uses OWNER_A (entity-allocator side); per-type allocators in
 * the C8j ladder populate FLAG_B and OWNER_A.  In HOUSE, the FLAG_B=-1
 * branch fires from `--force-b-entity <type>` smoke since allocator sets
 * FLAG_B = -1 (engine "owner+0x20 pos source").
 *
 * C8j-tick.3 (2026-05-25) adds bodies for the mid-cascade (L408-L649):
 *
 *   - 0x9c — NPC shoulder-arc bend.  Per-tick reads slot[ROT_X] as the
 *     bend angle; pos = owner.pose - sin/cos*scale*1.5 + (local_c*scale)
 *     vertical lift, where local_c = clamp(10.0 - AGE*0.3, scale, ∞) for
 *     AGE<0xbe and local_c = (AGE-0xbe)*0.6 + scale for AGE>=0xbe.
 *     Writes slot[ROT_SCR] = -π/2 (AGE<0x1a) or clamp((AGE-0x1a)*π/40 -
 *     π/2, ≤0) (AGE>=0x1a).  For AGE in [0x21, 0xa3): emits 3 overlay
 *     slots (types 0x6a, 0x6e, 0x6f) at world offset (sin*scale*5, 2*scale,
 *     cos*scale*5) from owner.pose; type 0x6f is gated on AGE%3==0; all 3
 *     use shape_mode=6, override_dur=-1.  AGE==1 → SE(0x2c2) + NPC spawn
 *     scene1_record_b_spawn_npc(owner_B, 0x9e, 1).  AGE==0xc8 kills.
 *
 *   - 0x34 — NPC joint-target lerp.  Uses slot[AUX_SENT1] (preamble -1)
 *     as a joint index biased by +0x96; initial pose = owner[(idx+0x96)*12]
 *     joint slot; vel = (slot[ALT_POS] - pos) / 15 (lerp to alt-pos over
 *     15 frames); slot[DRAG] = -0.5.  When slot[PART_IDX]==0 && AGE in
 *     [0x5a, 0x78): iter loop 20× of state_machine + pos += vel; then
 *     anchor back pos -= 20*vel.  AGE==0x96 kills.
 *
 *   - 0x68 — three-phase NPC spawn cycle.  slot[DRAG] = 1.0 (default) or
 *     0.2 if slot[FLAG_A]==1 (and `age_off` = 0 or 0x14 correspondingly).
 *     SE(0x2a4) on AGE==age_off+10; SE(0x2a9) on AGE==age_off+0x28.
 *     Three overlay spawns at slot[ALT_POS] via owner_A: AGE==1 → type 5
 *     scale 1.0 dur 100; AGE==age_off+0x28 → type 0xe scale 0.2 dur -1;
 *     AGE==age_off+0x1e → type 0 scale 0.8 dur -1.  AGE < age_off+0x14 →
 *     anchor back pos -= vel.  State machine called once per tick for
 *     AGE in [age_off, age_off+0x3c).  AGE==age_off+0x4b kills.
 *
 *   - 0x74 / 0x79 (shared body) — entity ground-cull walker.  slot[DRAG]
 *     = 0 (0x74) or 0.7 (0x79).  Always: pos -= vel (anchor back from
 *     preamble), slot[AUX_C8] = 1.  AGE==1 → overlay spawn type 0x11
 *     scale 1.0 dur -1 override_rot_y = slot[ROT_X bits]; if type==0x74,
 *     also overlay spawn type 0x12 scale 1.0 dur -1 (no override).  When
 *     AGE%4==0: write owner_A+0x904..0x90c = vel*0.2 (anim drive), spawn
 *     overlay type 0x2c at jittered offset from pos using owner+0x948
 *     compass angle + 3 rng draws, then clear owner anim drive.  For AGE
 *     in [0, 0x28): iter 20× of cull-query (FUN_00490820) + state_machine
 *     (only when cull returns negative) + pos += vel; anchor back pos -=
 *     20*vel.  Kills if owner_A+0xcf8 != 0 OR AGE == 0x37.
 *
 *   - 0x69 — entity self-spawn-then-die.  On AGE == slot[PART_IDX]*4 +
 *     0x14: writes owner_A+0xea0 = PART_IDX (side effect), then calls
 *     scene1_record_b_spawn_entity(owner_A, 0x68, -1) (entity allocator
 *     with flag=-1 = "owner+0x20 pos source"), then kills self.
 *
 * Wiring:
 *   scene1_sim.c::scene1_ingame_default_arm_tick — calls
 *   `scene1_records_b_tick()` every INGAME frame on the default sim arm.
 *
 * Dormancy in production:
 *
 *   - Table B is BSS-zero at boot (every slot's TYPE field = 0 = dead).
 *   - The C8j allocator ladder is `unwired` in production (no in-port
 *     caller of scene1_record_b_spawn_npc / _entity).  Only the C8j.fin.b
 *     smoke flags (`--force-b-npc <type>` / `--force-b-entity <type>`)
 *     commit slots; defaults are -1 (no-op).
 *   - With smoke flags set to one of the C8j-tick.2 types, the slot's
 *     pose is reset every tick to the owner's anchor; with the SE/spawn
 *     hooks at their default no-op state, no audible / overlay output.
 *
 *   Consequence: in HOUSE with no smoke flags set, the tick walks
 *   512 dead slots per frame and returns — pure no-op.  Goldens
 *   (boot-idle, title-z-press, title-down-press) stay bit-exact.
 *
 * Hooks (host-installable):
 *
 *   - scene1_records_b_set_per_type_body(fn) — override the production
 *     dispatch.  Pass NULL to revert to the in-module dispatch_default
 *     (the version the engine ships).  Tests use this to inject
 *     instrumented bodies.
 *
 *   - scene1_records_b_set_state_machine_hook(fn) — stand-in for engine
 *     FUN_0043865e (Mt. Everest #2, 8059 B per-record state machine,
 *     PHC #20).  Called from the LAB_0043b205 iter loop AND the
 *     LAB_0043b325 0x89/0x9e iter loop; default `NULL` = no-op.  The
 *     C8jb.* ladder will install a real body once that ports.
 *
 *   - scene1_records_b_set_se_hook(fn) — stand-in for engine
 *     FUN_00499519 (sound-effect-by-ID).  Called from the 0x1e age==4
 *     and age==0x4b sites, and 0x89/0x9e age==0x50.  Default NULL =
 *     no-op (tests don't see SE side effects).
 */
#ifndef SCENE1_RECORDS_B_TICK_H
#define SCENE1_RECORDS_B_TICK_H

#include <stdint.h>

#include "scene1_records.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine DAT_06a46f98 — cleared at every slot iteration top; sub-chips
 * may set it from within per-type bodies to short-circuit retries.
 * PHC #21 — exact semantics unknown until first reader-body lands. */
extern int32_t g_scene1_records_b_tick_flag;

/* Engine DAT_06a46f94 — cleared by the C8j-tick.4 body before each
 * state-machine call in the type {2/3/4/0x22/0x54/0x67/0x6d..0x70} loop;
 * the state machine is expected to set this on relevant ticks.  When the
 * post-call value is positive AND type == 4, the body scales it down
 * (/10, min 1) and writes it to owner_a+0xe30/+0xe38 as an anim-drive
 * frame counter.  Tests with a custom state_machine hook can write this
 * global to exercise the type-4 special case. */
extern int32_t g_scene1_records_b_tick_anim_drive;

/* Hook signatures. */
typedef void (*scene1_b_per_type_body_fn)(int slot_idx, int32_t type);
typedef void (*scene1_b_state_machine_fn)(int32_t *slot);
typedef void (*scene1_b_se_fn)(uint16_t se_id);

/* Stand-in for engine FUN_00490820 (348 B, 0x490820) — view-frustum cull
 * / projection visibility test (vec3 × 4-row view-matrix vs radius).
 * Engine reads _DAT_095d3770..95d37bc (16-float matrix + 4 ints); returns
 * iVar3 = positive depth-bias (visible) or 0/negative (culled).  Used by
 * 0x74/0x79 shared body's iter loop to gate state-machine calls to
 * only-when-visible slots.  Default returns -1 = "always visible" — so
 * the state machine fires every iteration (same observable behavior as
 * a stub state machine that doesn't write anything). */
typedef int (*scene1_b_cull_query_fn)(float x, float y);

/* Setters return prior value so tests can save/restore.  Pass NULL to
 * revert to the default — for `per_type_body` that's the in-module
 * `dispatch_default` (engine-faithful body); for `state_machine` and
 * `se` it's a no-op (engine helpers unported / side-effect-only); for
 * `cull_query` it's "always visible". */
scene1_b_per_type_body_fn scene1_records_b_set_per_type_body(
    scene1_b_per_type_body_fn fn);
scene1_b_state_machine_fn scene1_records_b_set_state_machine_hook(
    scene1_b_state_machine_fn fn);
scene1_b_se_fn            scene1_records_b_set_se_hook(scene1_b_se_fn fn);
scene1_b_cull_query_fn    scene1_records_b_set_cull_query_hook(
    scene1_b_cull_query_fn fn);

/* Mark a slot dead — engine LAB_004411e3 (`*piVar14 = 0`).  Setting
 * TYPE = 0 makes the next allocator scan reclaim it.  Death-effect
 * spawn (LAB_0043f39b 0x21 particle on age-parity) deferred to
 * C8j-tick.13. */
void scene1_records_b_tick_kill_slot(int slot_idx);

/* Walk all 512 slots; for each live slot run preamble + per-type
 * dispatch.  Engine FUN_0043ae20 outer loop. */
void scene1_records_b_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_B_TICK_H */
