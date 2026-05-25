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
 * C8j-tick.5 (2026-05-25) adds Body 2 (L812-L1050) — chr-walker /
 * shop-walker driven types {0x71, 0x72, 0x7d, 0x85, 0x8a, 0x8b, 0x5b,
 * 0x5c, 0x5e, 0x86, 0x87}.  All read OWNER_A.pose at +0x20..+0x28.
 *
 *   - 0x85 — pose at owner+0x20 + (sin, 1, cos) radius 1.0; ALT_POS
 *     direct-copy.  DRAG=0.5.  state_machine: progress → owner+0xe90=7,
 *     owner+0xe94=0, kill (engine LAB_004411e3); no progress → kill if
 *     owner+0xcf8 != 0 or AGE == 0x24.
 *   - 0x8a / 0x8b — pose at owner+0x20 + (sin*0.5, 1, cos*0.5).  DRAG
 *     =0.5.  state_machine progress: zero owner+0xe7c/e80/e84, then
 *     per-type cascade.  0x8a: aux_485979(0), SE(0x13f), owner+0xcf8
 *     =0x2d, owner+0xe90=1, aux_482a51(owner+0x930, 2), notify_queue
 *     (8, 4, 4, 0.5), vel_scale=1.4.  0x8b: owner+0xcf8=0xf, owner
 *     +0xe90=1, vel_scale=0.8.  Common: kill slot (TYPE=0), owner+0x904
 *     = -0.1 × sin(ROT_X)×vel_scale, owner+0x908 = 0.3, owner+0x90c
 *     = -0.1 × cos(ROT_X)×vel_scale.  Kill checks: owner+0xe90 / e7c /
 *     cf8 != 0; AGE == 20000.
 *   - 0x5b / 0x5c / 0x5e / 0x86 / 0x87 — pose at owner+0x20 + (sin, 1,
 *     cos) full radius; ALT_POS direct-copy.  DRAG=1.5 default, 2.5 for
 *     0x87.  Y-offset to scene1_spawn = 0 for 0x5b, 1.0 for others.
 *     AGE==2 → scene1_spawn(0, x, y+offset, z, 4, 1.8, 1).  state_machine
 *     5-iter early-break loop for AGE in [2, 6).  Kill on owner+0xcf8 /
 *     AGE==0x14.
 *   - 0x71 / 0x72 / 0x7d — chr-walker dynamic-scale pose.  DRAG=0.5
 *     default, 1.5 for 0x7d, 0.4 for 0x72; 0x72 also overrides DRAG to
 *     1.0 later if its kill_age branch runs.  Scale = min(2.0, AGE*0.2
 *     + 0.5).  0x71 + AGE<20 overrides to sin(AGE * π/2 / 20) * 2.5
 *     + 0.5; 0x7d overrides to 2.5; 0x72 multiplies by 0.9.  Pose at
 *     owner+0x20 + scale*(sin, _, cos), POS_Y = owner+0x24 + 1.0.
 *     Compass dispatch via owner+0x948: ==0 → POS_X -= 0.4; ==4 → POS_X
 *     += 0.4; else → POS_Z += 0.4.  0x7d AGE==1 → PFO Table A pass-
 *     through alloc(owner, POS_X, 0, POS_Z, 6, 1.0, -1, 0, 0).  0x72
 *     AGE%5==4 → SEQ_ID = seq_counter_next(); 0x72 also sets kill_age
 *     to 20000 (vs 0x14 for 0x71/0x7d) and DRAG=1.0.  state_machine
 *     5-iter early-break loop for AGE in [4, kill_age).  Kill checks:
 *     owner+0xcf8 != 0 (all); 0x72: owner+0xe90 != 2 → kill, AGE%5!=4
 *     skips age-kill, AGE==20000 kills.  0x71/0x7d: AGE==0x1e kills.
 *
 * Hooks gained (all default no-op): aux_485979 (1-arg), aux_482a51
 * (2-arg), notify_queue (4-arg, PHC #22).
 *
 * C8j-tick.6 (2026-05-25) adds Body 3 (L1050-L1187, asm 0x43cb4a..
 * 0x43cdef) — six small slot-state bodies.
 *
 *   - 0x1f — LIFE_MULT += 0.03 (clamp 1.5); DRAG = LIFE_MULT*0.1 - 0.5;
 *     state_machine; kill AGE==0x78.  No owner read.
 *   - 0x5a / 0x98 — shared body, reads OWNER_B.  LIFE_MULT += 0.03
 *     (clamp 2.0); DRAG = LIFE_MULT*0.1 + 0.5.  When (PART_IDX+3)*10 <
 *     AGE < 0x78: VEL_{X,Z} += (g_scene1_player_pos[{0,2}] - POS_{X,Z})
 *     * 0.003 then *= 0.95 (drift toward player).  AGE==0x78: two
 *     overlay spawns (type 7 scale 1.5 dur 30 at POS, type 0xb scale
 *     1.0 dur -1 at POS-(0,1,0)), SE(0x2ac), POS_Y += 1.0, state_machine,
 *     kill.  owner_b+0x428 != 1 → kill.
 *   - 0x6c — DRAG = LIFE_MULT*0.1 - 0.5; state_machine; kill AGE==0xc8.
 *   - 0x6b — AGE==0x2d: SE(0x2ac) + overlay spawn(type 7 scale 1.5 dur
 *     0x78 at POS+(0,1.5,0)).  AGE>=0x2d: state_machine.  Kill AGE==0x9b.
 *   - 0x28 — DRAG = LIFE_MULT*0.1; VEL_Y -= 0.003; state_machine; kill
 *     AGE==0x12c.
 *
 * Survey corrections (vs docs/findings/scene1-records-b-tick.md):
 *   - Body 3 includes 0x1f (asm 0x43cb4a..0x43cba9) — survey omitted it.
 *   - 0x2d in survey type list was an AGE check inside 0x6b's body,
 *     NOT a separate type.
 *   - Body 3 does NOT use FUN_00432e50 (ground query) as the survey
 *     claimed — that hook is exercised by other bodies (PHC #15 sites
 *     at FUN_0044376a 0x29 etc).  Body 3 is pure slot-state + drift
 *     toward player.
 *
 * C8j-tick.7 (2026-05-25) adds three scattered post-Body-3 types from
 * the asm 0x43cdef..0x43d0b6 region:
 *
 *   - 0x38 — DRAG=2.0; state_machine; kill AGE==0x12c.
 *   - 0x29 — DRAG = LIFE_MULT*4.0.  AGE==1: notify_queue(10,16,16,1.0).
 *     AGE in (10, SCALE_Y*90): AGE%3==1 → state-machine 5-iter inner
 *     loop (POS_Y += n*3, sm, POS_Y -= n*3); AGE%10==0 → SEQ_ID =
 *     seq_counter_next().  Spawn-age = (int)(SCALE_Y*136 - 32) via
 *     __ftol (Ghidra-dropped); AGE<spawn_age → scene1_spawn(0, POS, 0x4e,
 *     LIFE_MULT*0.5, 1).  Kill: SCALE_Y*136 <= (float)AGE.  No owner read.
 *   - 0x8c — DRAG=1.0; ROT_X += 0.15; state_machine; kill PART_IDX==100
 *     OR AGE > 0x4af (= 1199).
 *
 * C8j-tick.8 (2026-05-25) adds three ground-bouncing types from the
 * scattered post-Body-3 region (asm 0x43cf60..0x43d5f8) — all share the
 * FUN_00432e50 ground-query hook (PHC #15, new):
 *
 *   - 0x2c — physics-bounce billboard.  VEL_Y = (VEL_Y - 0.02) * 0.95
 *     (gravity + damping); ROT_SCR += 0.05; ROT_Z += 0.03.  MATRIX0 =
 *     rot_y(ROT_Z) * rot_x(ROT_SCR) via mat4_rotation_x / _y + mat4_mul
 *     (read by state_machine in the FLAG==0 path).  Bounce gate: VEL_Y
 *     < 0 AND ground_query(POS) hits AND POS_Y <= LIFE_MULT*0.5 +
 *     slot[AUX_9] → POS_Y = threshold, VEL_Y *= -0.8; first hit only
 *     (FLAG==0): FLAG=1, notify_queue(4,4,4,1.0), SE(0x168).  FLAG > 0:
 *     FLAG++.  FLAG==0x1e (30) kills.  FLAG==0 only: DRAG = LIFE_MULT *
 *     1.2; state_machine(slot).  Always: mat4_identity(MATRIX0) — wipe
 *     for next user.  AGE >= 0x12c (300) kills.
 *   - 0x23 — ground-bounce with on-impact spawn cascade.  DRAG = LIFE_MULT
 *     * 3.0; state_machine(slot); ROT_SCR += 0.04; ROT_X += 0.04.
 *     FLAG==0 branch: if AGE&1, scene1_spawn(0, POS, 0x53, LIFE_MULT*0.1,
 *     1) (vestigial sin/cos discard at L37599-37602 omitted — no side
 *     effects).  ground_query(POS) hit AND POS_Y < gy+1.0:
 *       POS_Y = gy, FLAG = 1, VEL_Y = 0,
 *       aux_4532bc(0x20), notify_queue(0x28, 0x10, 0x10, 1.0),
 *       7 scene1_spawn calls (types 0xf/0x36/0x2a/0x52 + 3x type 0x51
 *       at increasing Y),
 *       DRAG = LIFE_MULT*8.0, then state_machine 30-iter early-break loop.
 *     FLAG != 0 branch: FLAG++ then if FLAG > 20: POS_Y -= 0.1; if FLAG
 *     == 10: kill.  Always: AGE == 200 kills.
 *   - 0x3a — alternate ground-bounce with cleanup-on-progress.  No DRAG/
 *     state_machine preamble (unlike 0x23).  FLAG==0 branch only: AGE&1
 *     spawn(0x53), ground_query gated: POS_Y = gy, FLAG=1, VEL_Y=0,
 *     DRAG=3.0, state_machine(slot), bvar17=1.  Then DRAG=0.5,
 *     state_machine(slot); if it "returned non-zero" (hook installed),
 *     bvar17=1.  If bvar17: aux_4532bc(0x20), notify_queue(0x28, 0x10,
 *     0x10, 1.0), 4 spawn calls (0x52 + 3x 0x51), kill.  AGE == 0x78 kills.
 *
 * Hook gained: scene1_b_tick_ground_query_fn (4-arg, FUN_00432e50, PHC #15),
 * scene1_b_aux_4532bc_fn (1-arg, FUN_004532bc).  Both default no-op (no
 * ground / no trigger); HOUSE smoke unchanged.
 *
 * Survey corrections (vs docs/findings/scene1-records-b-tick.md):
 *   - Body 4 in the survey was actually three scattered single bodies
 *     (0x2c, 0x23, 0x3a) all using ground_query, not a {0, 1} sub-table.
 *     The {0, 1} sub-table sits further down at asm 0x43d600+ and
 *     remains deferred to a later sub-chip.
 *
 * Deferred to next sub-chip(s): types {0, 1} sub-table dispatch and the
 * Body 5 group (0x21/0x25/0x31/0x32) per the survey.
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

/* Stand-ins for three engine helpers used by the C8j-tick.5 0x8a body.
 * All are deferred to their own ports (no in-port consumer reads their
 * side effects today); the hooks let tests observe arg shape and let the
 * defaults remain pure no-ops.
 *
 *   FUN_00485979 (731 B, 0x485979) — unknown 1-arg helper (item-pickup
 *     notify?).  Engine call site pushes 0; we surface as a 1-arg hook.
 *   FUN_00482a51 (32 B, 0x482a51) — small 2-arg wrapper, called with
 *     (owner_a + 0x930, 2).
 *   FUN_0044b219 (60 B, 0x44b219) — single-slot notification queue
 *     writer to DAT_0438cc14..cc2c (PHC #22).  4-arg signature
 *     `(int a, int b, int c, float d)`; engine call passes (8, 4, 4, 0.5). */
typedef void (*scene1_b_aux_1arg_fn)(int32_t arg1);
typedef void (*scene1_b_aux_2arg_fn)(int32_t arg1, int32_t arg2);
typedef void (*scene1_b_notify_queue_fn)(int32_t a, int32_t b, int32_t c,
                                         float d);

/* Stand-in for engine FUN_00490820 (348 B, 0x490820) — view-frustum cull
 * / projection visibility test (vec3 × 4-row view-matrix vs radius).
 * Engine reads _DAT_095d3770..95d37bc (16-float matrix + 4 ints); returns
 * iVar3 = positive depth-bias (visible) or 0/negative (culled).  Used by
 * 0x74/0x79 shared body's iter loop to gate state-machine calls to
 * only-when-visible slots.  Default returns -1 = "always visible" — so
 * the state machine fires every iteration (same observable behavior as
 * a stub state machine that doesn't write anything). */
typedef int (*scene1_b_cull_query_fn)(float x, float y);

/* Stand-in for engine FUN_00432e50 (2084 B, 0x432e50) — terrain ground-
 * height query (PHC #15).  Engine signature is 4-arg: takes a world
 * position (x, y, z) plus a pointer to a 4-float scratch buffer; writes
 * the hit point into the buffer (engine reads ground_y at buffer+0xc =
 * 4th float).  Returns 1 on hit, 0 on miss.
 *
 * Our hook collapses the 4-float buffer to a single `float *out_y` since
 * the C8j-tick bodies that consume it only read the ground Y component.
 * Default returns 0 ("no ground"); bounce gates that compare POS_Y to
 * (ground_y + 1.0f) therefore never trigger.  Hook receives the slot's
 * pre-preamble pos.{x,y,z} since the integrator preamble has already
 * added vel into pos by the time per-type bodies run. */
typedef int (*scene1_b_tick_ground_query_fn)(float x, float y, float z,
                                        float *out_y);

/* Stand-in for engine FUN_004532bc (29 B, 0x4532bc) — small 1-arg state
 * writer.  Engine implementation: `if (DAT_06a49998 == 0): DAT_06a49994
 * = 1; DAT_005c5938 = arg1`.  Looks like a fade/transition trigger that
 * latches on first call.  Both call sites in this chip (0x23 + 0x3a
 * post-ground-hit cleanup) pass 0x20.  Default no-op; no in-port consumer
 * reads the latched state today. */
typedef void (*scene1_b_aux_4532bc_fn)(int32_t arg1);

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
scene1_b_aux_1arg_fn      scene1_records_b_set_aux_485979_hook(
    scene1_b_aux_1arg_fn fn);
scene1_b_aux_2arg_fn      scene1_records_b_set_aux_482a51_hook(
    scene1_b_aux_2arg_fn fn);
scene1_b_notify_queue_fn  scene1_records_b_set_notify_queue_hook(
    scene1_b_notify_queue_fn fn);
scene1_b_tick_ground_query_fn  scene1_records_b_set_ground_query_hook(
    scene1_b_tick_ground_query_fn fn);
scene1_b_aux_4532bc_fn    scene1_records_b_set_aux_4532bc_hook(
    scene1_b_aux_4532bc_fn fn);

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
