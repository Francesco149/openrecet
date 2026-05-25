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
 * C8j-tick.9 (2026-05-25) covers asm 0x43d5f8..0x43dc02 — the gap between
 * C8j-tick.8's last body (0x3a) and Body 6 (10/0xb/0x14/0x13/0x99 starting
 * at 0x43dc03).  Six per-type bodies:
 *
 *   - 0x3c — DRAG=0.1f; state_machine if 8<=AGE<100; AGE==1 → spawn 0x54
 *     (with rng_next15() as param7, asm-recovered); AGE==0x78 kills.
 *   - 0x3b — DRAG=0.1f; NPC sister-spawn (temporarily grafts slot.POS
 *     into owner.pos[3f0..3f8] around scene1_record_b_spawn_npc(0x3c, 1),
 *     then restores owner.pos); for AGE in (30, 120) drifts VEL_{X,Z}
 *     toward `player_pos + ALT_POS` via `vel = ((target-pos)*0.005 + vel)
 *     * 0.95`; clamps |vel.xz| to 0.6 via sqrt-normalize.  AGE==0x100 kills.
 *   - Body 5 (0x21/0x25/0x31/0x32) — shared: LIFE_MULT += 0.002; DRAG =
 *     LIFE_MULT*0.1; ROT_Z += 0.03; type-split: 0x21 → SM if AGE<0x48,
 *     kill AGE==0x50; others → SM if AGE<0xf8, kill AGE==0x100.
 *   - 0x2b — DRAG = LIFE_MULT*0.2; SM if AGE<0x48; FLAG==1 short-circuits
 *     past big body to AGE==0x50 kill check.  FLAG!=1 path: ROT_SCR+=0.05,
 *     ROT_Z+=0.03, MATRIX0 = rot_y(ROT_Z) × rot_x(ROT_SCR), VEL_Y -= 0.02,
 *     ground_query.  On hit + VEL_Y<0 + POS_Y < LIFE_MULT*0.5+gy: snap
 *     POS_Y, zero VEL_xyz, FLAG=1, AGE=0x28, fire 2 scene1_overlay_spawn
 *     calls (type 7 scale 1.5 dur 0x20 at POS; type 0xb scale 1.0 dur -1
 *     at POS-(0,1,0)).  Engine quirk: `(slot_idx & 1) → SE(0x2c0)` —
 *     half of all 0x2b slots play the SE on bounce, partitioned by slot
 *     index (engine `[ebp-0x28]` = function-local loop iterator).
 *   - 0x26/0x2a — shared: LIFE_MULT += 0.002; DRAG = LIFE_MULT*0.2; ROT_Z
 *     += 0.03; SM if AGE<0x98; ground_query bounce inverts VEL_Y (not
 *     zero-snap like 0x2b).  AGE==0xa0 kills.
 *   - 0x27 — three-phase state machine.  FLAG==2: LIFE_MULT -= 0.1, kill
 *     iff LIFE_MULT < 0 (fade-out).  FLAG==1: LIFE_MULT += 0.3 (clamp 10),
 *     DRAG = LIFE_MULT*0.5, SM (grow).  FLAG==0 default: ROT_Z+=0.03,
 *     VEL_Y-=0.01, LIFE_MULT+=0.1; ground_query → if POS_Y < gy+0.3:
 *     snap, zero VEL, FLAG=1, LIFE_MULT += 0.5 splash boost (phase 0 → 1
 *     transition).  No AGE-kill — relies on FLAG==2 fade-out.
 *
 * No new hooks or globals — all existing (state_machine, scene1_spawn,
 * scene1_overlay_spawn, scene1_record_b_spawn_npc, ground_query,
 * notify_queue, aux_4532bc, se_play, g_scene1_player_pos[0/2]).
 *
 * Ghidra-dropped args recovered via raw asm (preserved in port via 7-arg
 * scene1_spawn / 10-arg scene1_overlay_spawn signatures):
 *   - 0x3c spawn at 0x43d629 — scale=0.1f, param7=rng_next15() (asm
 *     0x43d62e + 0x43d634).
 *   - 0x2b overlay_spawn 1 at 0x43d975 — override_rot_y=0 (asm 0x43d96f
 *     fldz + fstp [esp]); Ghidra L37801 had 8 args, asm has 9.
 *
 * C8j-tick.13 (2026-05-25) adds type 0x53 (asm 0x43e5d0..0x43e755) —
 * drift-damping body with two-phase life curve + stage-transition gate.
 * Largest single-type body in the integrator at 0x185 bytes.
 *
 *   kill_age = 600 (default) or 120 (if stage-transitioning via aux_4319d6
 *              AND FLAG_A in {0, 3}).
 *   LIFE_MULT = 0.005 (initial).
 *   if AGE >= 45:
 *     LIFE_MULT = clamp_max(0.005 + (AGE-45)*0.001, 0.015)
 *               * (1.0 + 0.02*sin((AGE-45)*0.04))
 *   if AGE >= kill_age-45:
 *     LIFE_MULT = clamp_min(0.015 - (AGE-(kill_age-45))*0.001, 0.0)
 *   if AGE < 30:   VEL_X *= 0.92; VEL_Z *= 0.92
 *   if AGE <= 45:  VEL_X = 0;     VEL_Z = 0
 *   if AGE in (45, kill_age-45):
 *     DRAG = LIFE_MULT * 1.9 / 0.015
 *     state_machine
 *   kill on AGE == kill_age.
 *
 *   New hook: scene1_records_b_set_aux_4319d6_hook (stage-transition
 *   gate, PHC FUN_004319d6).  Default returns 0 → kill_age stays at
 *   600 (the normal NPC walker lifetime).
 *
 * C8j-tick.12 (2026-05-25) adds Body 7b head (asm 0x43e22b..0x43e5d0) —
 * three per-type bodies that follow Body 7a in the outer cascade:
 *
 *   - Type 0xf (asm 0x43e22b..0x43e2ed) — wide-followup walker arm pose.
 *     Gate: owner+0x424 in {0x18, 0x3b, 0x3c}; else no-op (fall-through).
 *     DRAG = 1.5.
 *     POS_X = 0.5*sin(owner+0x420) + owner+0x3f0;
 *     POS_Y = owner+0x3f4 + 1.0;
 *     POS_Z = 0.5*cos(owner+0x420) + owner+0x3f8.
 *     state_machine.
 *     Kill AGE >= 1 (always — engine "pose + SM + die" anchor body).
 *
 *   - Type 0x9b (asm 0x43e2ed..0x43e5ac) — big AGE-stage animation +
 *     spawn-cascade body (5-arm AGE-window state machine):
 *     local_c = clamp_min(15.0 - AGE*0.3, LIFE_MULT*2);
 *     if AGE >= 365: local_c = (AGE-365)*0.6 + LIFE_MULT*2.
 *     ROT_SCR = -π/2.
 *     if AGE >= 36: ROT_SCR = clamp_max((AGE-36)*π/40 - π/2, 0.0).
 *     pose: POS = owner+0x20/24/28 - (1.5*sin*LIFE_MULT, -local_c,
 *                                       1.5*cos*LIFE_MULT) using ROT_X bend
 *           (engine uses owner+0x20 + (-LIFE_MULT*1.5*sin, +local_c,
 *                                       -LIFE_MULT*1.5*cos)).
 *     Spawn-coord precompute:
 *       local_28 = 3.5*sin(ROT_X)*LIFE_MULT;
 *       local_2c = 2*LIFE_MULT;
 *       local_18 = 3.5*cos(ROT_X)*LIFE_MULT.
 *     if AGE in [123, 365):
 *       overlay_spawn(OWNER_A, local_28, local_2c, local_18, 0x6a,
 *                     LIFE_MULT, -1, 0, 0, 1);
 *       overlay_spawn(OWNER_A, local_28, local_2c, local_18, 0x6e,
 *                     LIFE_MULT, -1, 0, 0, 1);
 *       if AGE % 3 == 0:
 *         overlay_spawn(OWNER_A, local_28, local_2c, local_18, 0x6f,
 *                       LIFE_MULT, -1, 0, 0, 1);
 *     if AGE == 200: scene1_record_b_spawn_entity(OWNER_A, 0x9d, -1)
 *                    (engine FUN_0044376a entity allocator, NOT NPC).
 *     if AGE == 130: se_play(0x2c2).
 *     if AGE == 390: kill.
 *     if owner+0xcf8 != 0: kill.
 *
 *   - Type 0x24 (asm 0x43e5ac..0x43e5d0) — trivial: DRAG = 10.0,
 *     state_machine, kill on AGE == 10.
 *
 * C8j-tick.11 (2026-05-25) adds Body 7a (asm 0x43dd79..0x43e22b) — the
 * first major FUN_0043865e (state_machine, PHC #20) dispatch cluster.
 * Four per-type bodies plus a motion-id sub-dispatch:
 *
 *   - Type 0x46 (asm 0x43dd96..0x43deb1) — overlay cascade.
 *     If AGE == 1: scene1_overlay_spawn(NULL, POS, 0x44, 2.5, 0xffffffff, 0).
 *     If AGE == 0x28: fire 4 overlay spawns + se_play(0x2a3):
 *       (POS+y+4.0, 0x42, 2.0), (POS, 0x43, 1.5), (POS, 0x45, 1.5).
 *     DRAG = 3.0; SM if AGE in [0x28, 0x30); kill on AGE == 0x3c.
 *
 *   - Type 0x97 (asm 0x43debd..0x43df16) — overlay emit + ramp-out.
 *     DRAG = 1.0; state_machine.
 *     If AGE % 2 == 1: scene1_overlay_spawn(OWNER_A, POS_X, 0, POS_Z,
 *                                            0x56, 1.0, 0xffffffff, 0).
 *     Kill on AGE >= 800.
 *
 *   - Types 0xe / 0x12 (asm 0x43df1b..0x43e108) — motion-id sub-dispatch
 *     reading owner+0x424:
 *       0x31 → DRAG = 3.0; SM; kill AGE >= 8.
 *       0xf  → DRAG = 1.0; SM; kill AGE >= 0 (always).
 *       0x25/0x26/0x27/0x28 → DRAG = 4.0; iter loop SM up to 20× (break
 *                              on SM return 0); always kills post-loop.
 *       0x3d/0x3e/0x3f/0x40/0x41/0x42 → DRAG = 3.5; SM; kill AGE >= 5.
 *       0x46/0x47 → DRAG = 1.0; SM; kill AGE >= 1 (always).
 *       0x44/0x45 → DRAG = g_scene1_b_motion_table[motion].drag_mul *
 *                          1.15; SM; kill AGE >= 1 (always).
 *       0x43 → DRAG = 3.5; pose write (2*sin/cos around owner+0x3f0..f8
 *               with owner+0x420 angle, +2.0 y); SM; kill AGE == 0x3c.
 *       0x18/0x3b/0x3c → DRAG = owner+0xa58 < 100 ? 6.5 : 8.5; SM;
 *                        kill AGE == 0xf.
 *       (else) → DRAG = 2.0; SM; kill AGE == 0xf.
 *
 *   - Types 0xd / 0x15 (asm 0x43e15d..0x43e226) — pose around owner.
 *     DRAG = 0.5 (or 0.0 for type 0x15 + owner motion == 0x19).
 *     POS_X = 2*sin(owner+0x420) + owner+0x3f0;
 *     POS_Y = owner+0x3f4 + 2.0;
 *     POS_Z = 2*cos(owner+0x420) + owner+0x3f8.
 *     SM gate: AGE in [5, 9) for 0xd; AGE in [0, 0xf) for 0x15.
 *     Kill on AGE == 0x28.
 *
 *   Engine quirk: the "AGE >= 1 always kills" cases ({0xe,0x12} +
 *   motion in {0xf, 0x44-0x47}) preserve their motion-id sub-dispatch
 *   despite immediate kill — the state_machine call is the side
 *   effect they want (e.g. trigger a one-shot effect via FUN_0043865e).
 *
 * C8j-tick.10 (2026-05-25) adds Body 6 + Body 7 (asm 0x43dc03..0x43dd79) —
 * two NPC-anchor bodies sharing a per-NPC-motion-style physical-constants
 * table (PHC #19 — three sibling tables at DAT_005c2434/8/c, 256-entry
 * stride 0x68; no in-binary writers visible, presumed .rdata copy from
 * an unported lnkdatas loader):
 *
 *   Body 6 — types {0x10, 0xb, 0x14, 0x13, 0x99} (asm 0x43dc03..0x43dcdb):
 *     Gate: owner+0x428 == 1 (else kill).  Per-tick:
 *       motion = owner+0x424 (NPC motion-style ID)
 *       if motion in {0xd, 0xe}:
 *         DRAG = -0.8
 *       else:
 *         DRAG = ((motion_drag_base + 0.1 - 1.5) * owner+0xabc *
 *                 motion_drag_mul) - 0.3
 *       POS_X = owner+0x3f0
 *       POS_Y = owner+0xabc * motion_pos_y_mul * motion_drag_mul * 0.5
 *               + owner+0x3f4
 *       POS_Z = owner+0x3f8
 *       int prog = state_machine(slot)
 *       if (prog != 0 && type == 0x13):
 *         owner+0xb90 = anim_drive (DAT_06a46f94)
 *         owner+0xb94 = 0x1e
 *     No AGE-kill (relies on owner+0x428 going to 0).
 *
 *   Body 7 — types {0x11, 0xc} (asm 0x43dcdb..0x43dd79):
 *     Gate: owner+0x428 == 1 (else kill).  Per-tick:
 *       if type == 0x11: DRAG = 0.0
 *       else:            DRAG = ((motion_drag_base + 0.1 - 1.5) *
 *                                owner+0xabc * motion_drag_mul) - 0.3
 *       Same pose write as Body 6.
 *       state_machine(slot)  (return value ignored)
 *       if AGE != 7: skip kill else kill.
 *
 *   New global: `g_scene1_b_motion_table[256]` (3-float entry: drag_mul /
 *   drag_base / pos_y_mul) — BSS-zero by default; tests inject per-motion
 *   constants directly.  With BSS-zero defaults and any motion!={0xd,0xe},
 *   DRAG = -0.3 and POS_Y = owner+0x3f4 (anchor pose, no scaling).  When
 *   the .rdata copier ports, this table will be populated from
 *   lnkdatas/<unidentified>.dat.
 *
 *   Body 6's state-machine return-int "prog" uses state_machine_call_ret()
 *   convention — 1 when hook installed (engine progressed), 0 when NULL
 *   (engine reported no progress).  Engine's int return is not exposed by
 *   the void hook signature; tests of the 0x13 anim-drive special case
 *   install a hook that writes g_scene1_records_b_tick_anim_drive.
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

/* Engine DAT_005c2434 / DAT_005c2438 / DAT_005c243c — per-NPC-motion-style
 * physical-constants table (PHC #19).  Three sibling addresses, each
 * indexed by `motion_id * 0x68` (= entry stride).  Fields are at byte
 * offsets +0x44, +0x48, +0x4c inside a 0x68-byte struct that has many
 * other fields not yet consumed.  Read by Body 6/7 (C8j-tick.10) and
 * deeper bodies in the same fashion.
 *
 *   - drag_mul  (+0x44, DAT_005c2434) — DRAG formula multiplier AND
 *                                       POS_Y formula multiplier
 *   - drag_base (+0x48, DAT_005c2438) — DRAG formula base value
 *   - pos_y_mul (+0x4c, DAT_005c243c) — POS_Y formula multiplier
 *
 * No in-binary writer found in the decomp dump; presumed populated by a
 * .rdata table copier (sibling pattern to Pass F's DAT_005c2410 traced
 * to "no in-binary writer" in C8c.F).  Default BSS-zero produces drag
 * = -0.3 (post-formula) and POS_Y = owner+0x3f4 (anchor only).  Tests
 * inject values directly by writing to this table. */
typedef struct {
    float drag_mul;
    float drag_base;
    float pos_y_mul;
} scene1_b_motion_entry_t;

extern scene1_b_motion_entry_t g_scene1_b_motion_table[256];

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

/* Stand-in for engine FUN_004319d6 (170 B, 0x4319d6) — stage-transition
 * gate check.  Reads DAT_0438b4c8/cc (current/next stage IDs) and
 * returns 1 when in a specific stage-to-stage transition (0→4, 4→0x1d,
 * 4→99), 0 otherwise.  Used by the C8j-tick.13 type-0x53 body to
 * compute a per-stage kill-age (120 ticks when transitioning; 600
 * ticks normal).  Default returns 0 ("not transitioning") — slots use
 * the 600-tick kill_age.  Tests can install a stub that returns 1 to
 * exercise the short-life path. */
typedef int (*scene1_b_aux_4319d6_fn)(void);

/* Stand-in for scene1_overlay_spawn calls fired from within per-type
 * bodies (scattered across C8j-tick.2..C8j-tick.13).  When installed,
 * the hook is called INSTEAD of scene1_overlay_spawn — tests use this
 * to observe arg construction without populating the full overlay
 * template/shape table.  Default NULL → production calls
 * scene1_overlay_spawn directly. */
typedef void (*scene1_b_overlay_spawn_fn)(const void *template_owner,
                                          float pos_x, float pos_y,
                                          float pos_z,
                                          int   template_id,
                                          float scale_base,
                                          int   override_dur,
                                          int   override_rot_y,
                                          int   shape_mode,
                                          int   mode);

/* Stand-in for engine's shop-walker record table at DAT_0076bd98..
 * DAT_007c8f94 (128 records × 0x2e9 dw stride = ~372 KB; NOT ported as
 * typed storage in this chip).  The C8j-tick.15j type-0x83 body iterates
 * this range to nudge each gated record's velocity toward the slot's
 * world POS by 0.03 per tick.
 *
 * Hook semantics: returns the record's TYPE-anchored dword pointer
 * (engine's `piVar10`) for `idx` in [0, 128), or NULL when the host
 * hasn't installed a real table (production today — table unported).
 * The body reads gate fields at +0x1b3 / +0 / +0x1b7, position triplet
 * at -0xe / -0xd / -0xc, and writes velocity triplet at -0xb / -0xa /
 * -0x9 (all in dword units from the anchor).  Default NULL → no records
 * iterated → body's nudge loop is a no-op (preserves BSS-zero behavior
 * matching unported retail). */
typedef int32_t *(*scene1_b_sw_record_at_fn)(int idx);

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
scene1_b_overlay_spawn_fn scene1_records_b_set_overlay_spawn_hook(
    scene1_b_overlay_spawn_fn fn);
scene1_b_aux_4319d6_fn    scene1_records_b_set_aux_4319d6_hook(
    scene1_b_aux_4319d6_fn fn);
scene1_b_sw_record_at_fn  scene1_records_b_set_sw_record_at_hook(
    scene1_b_sw_record_at_fn fn);

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
