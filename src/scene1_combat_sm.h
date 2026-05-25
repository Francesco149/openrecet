/*
 * scene1_combat_sm.h — per-record state machine (combat tick) for table B.
 *
 * Engine source: FUN_0043865e @ 0x43865e (8059 B, Mt. Everest #2 —
 * per-record state machine invoked 73× by the FUN_0043ae20 integrator).
 * Survey doc: docs/findings/scene1-records-b-state-machine.md.
 *
 * Chip ladder (C8jb.*):
 *
 *   C8jb.0 (2026-05-25, commit e72ac6c)  — Survey only.
 *   C8jb.1 (2026-05-25, commit 256a1f6)  — Phase A entry gates + per-tick
 *                                          flag write (resolves PHC #21).
 *   C8jb.2 (2026-05-25, commit 4b29713)  — Phase B head: attacker NPC scan
 *                                          iteration shell + 4 skip gates +
 *                                          per-NPC hit-history filter.
 *   C8jb.3 (2026-05-25, commit c3a2dad)  — Phase B collision math: nested
 *                                          per-NPC sub-iter loop +
 *                                          distance check + AABB Y-band.
 *   C8jb.4 (2026-05-25, commit 6824d22)  — Phase B per-collision arming:
 *                                          0x48 disarm, 0x44/0x45 angle
 *                                          filter (atan2-based facing
 *                                          check, ±0.3π cone), plus
 *                                          implicit unarming from C8jb.3's
 *                                          anchor-path sub-iter > 0
 *                                          (engine `local_18 = 1`).
 *   C8jb.5a (this chip)                  — Phase B damage-roll prologue:
 *                                          velocity-derived KB factor
 *                                          (0.7 / sqrt(VX² + VZ²)),
 *                                          hit-history ring bump (per
 *                                          armed-or-disarmed collision
 *                                          in range), and slot TYPE==0x53
 *                                          heavy-attack short-circuit
 *                                          (per-NPC-type +0x20 gate +
 *                                          npc.npc_type != 0x22 +
 *                                          FUN_004319d6 cooldown lookup
 *                                          → kill_age write to NPC +0x734,
 *                                          DAT_0438bed8 = 4, local_8 = 0).
 *   C8jb.5b..11                          — Phase B general damage formula
 *                                          / hit registration / Phase C /
 *                                          Phase D.
 *   C8jb.fin                             — Install as integrator default
 *                                          SM hook (int-ret plumbing).
 *
 * Return contract (full SM):
 *
 *   0  — no interaction this tick (also: any entry gate non-zero).
 *   1  — hit fired (downstream body should apply damage write to owner).
 *   2  — full cleanup; slot self-killed inside the SM (`*slot = 0`).
 *
 * C8jb.1..4 scope: returns 0 unconditionally.  Phase A short-circuit OR
 * Phase B iteration-completes-without-hit OR Phase C/D stub fall-through.
 */
#ifndef OPENRECET_SCENE1_COMBAT_SM_H
#define OPENRECET_SCENE1_COMBAT_SM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Phase A entry gates (4 engine globals) ─────────────────────────── */
/*
 * If ANY of these is non-zero (positive for the first three; non-zero
 * for the paused-flag), the SM short-circuits to ret=0 without setting
 * the per-tick flag.
 *
 *   DAT_0438be98 — combat subphase (also referenced by music.c).  Reads
 *                  zero in HOUSE-idle.  Set positive during certain
 *                  combat phase transitions.
 *
 *   DAT_0438be9c — world-pause flag.  Set to 1 by Phase C's
 *                  type-4/5/8 sub-arm (L35762 of all.c) — a "dialog /
 *                  cinematic active" latch.  Also gates at least one
 *                  unrelated function at all.c L7624.
 *
 *   DAT_0438bea0 — sibling pause flag (less common, set by a few
 *                  scene-state mutators).
 *
 *   DAT_0438b1c8 — INGAME paused flag (modal active).  Already declared
 *                  by scene1_sim.h as g_scene1_ingame_paused_flag — we
 *                  read THAT here, not re-declare.
 *
 * Tests install non-zero values to verify each gate short-circuits.
 * Production keeps all four BSS-zero in HOUSE so SM-installed ticks
 * always pass Phase A and bump the per-tick flag.
 */
extern int32_t g_scene1_combat_subphase;     /* DAT_0438be98 */
extern int32_t g_scene1_combat_world_pause;  /* DAT_0438be9c */
extern int32_t g_scene1_combat_aux_pause;    /* DAT_0438bea0 */

/* ─── Phase B head — attacker NPC scan ───────────────────────────────── */
/*
 * Phase B gate: enters only when slot is in an "attacker" combat state
 * AND the player is alive (HP > 0).
 *
 *   slot[FLAG_A] in {0, 3}     attacker = idle (0) or hit-recovery (3)
 *   g_scene1_combat_player_hp > 0.0f
 *
 * Engine global: _DAT_056db0bc — float, written by the Phase D HP path
 * (not ported yet) and by FUN_0044b16c (damage applicator, unported).
 * Default 0.0f → entire Phase B scan is dormant.  Tests raise this to
 * exercise iteration paths.
 *
 * The scan iterates g_scene1_people[0..127], stride 0xba4 B in engine.
 * Each NPC passes 4 skip gates + the per-NPC hit-history filter; NPCs
 * that pass enter the nested per-NPC sub-iter loop (C8jb.3 collision
 * math).
 */
extern float g_scene1_combat_player_hp;      /* _DAT_056db0bc */

/*
 * Total count of NPCs that passed all 4 skip gates + the hit-history
 * filter during the most recent scene1_combat_sm_tick() call.  Reset to
 * 0 at the start of each tick that proceeds past Phase A.  Useful for
 * test smoke ("did the iteration reach any NPC?") without installing
 * a full visit hook.
 */
extern int32_t g_scene1_combat_phase_b_visit_count;

/*
 * Per-NPC visit hook.  Called once per NPC index `i` (0..127) that
 * passes all 4 skip gates + the hit-history filter.  Default NULL → no
 * callback.  Tests use this to capture which NPCs were "would-collide"
 * candidates.  The hook does NOT short-circuit iteration; it is a pure
 * observer.
 */
typedef void (*scene1_combat_phase_b_visit_fn)(int npc_index);
scene1_combat_phase_b_visit_fn
scene1_combat_set_phase_b_visit_hook(scene1_combat_phase_b_visit_fn fn);

/* ─── Phase B collision math (C8jb.3) ──────────────────────────────── */
/*
 * Per-NPC-type collision attributes — engine's per-TYPE table at
 * DAT_005c23f0 (stride 0x68 B per entry, indexed by NPC type 0..255).
 * The combat SM reads three float fields at byte offsets +0x44 / +0x4c /
 * +0x50.  PHC #19 grep'd DAT_005c2400 (= +0x10) and three siblings as
 * having zero writers in the binary; this likely extends to the whole
 * table (no init code identified yet).  Modeled as a host-controllable
 * global so tests can inject collision attrs; production keeps it
 * BSS-zero → collision check fails for everything except direct overlap
 * (dist - reach < 0).
 *
 *   radius_mul   +0x44 (DAT_005c2434)  per-type attack-radius multiplier;
 *                                      gates `dist - reach < radius *
 *                                      radius_mul * dist_mul`.
 *   y_band_mul   +0x4c (DAT_005c243c)  per-type Y-band half-thickness
 *                                      multiplier; gates `|dy -
 *                                      reach*0.8| < radius * y_band_mul *
 *                                      dist_mul`.
 *   dist_mul     +0x50 (DAT_005c2440)  per-type distance multiplier,
 *                                      composed with both gates.
 *
 * "Reach" is the slot's reach radius at slot[SCENE1_RECORDS_B_OFF_DRAG]
 * (engine slot[0x2a] — same slot field that C8j-tick bodies use as DRAG;
 * combat reinterprets it as reach).  Engine drops the dy term when
 * within the Y-band gate.
 */
typedef struct {
    float radius_mul;
    float y_band_mul;
    float dist_mul;

    /* C8jb.5a — engine table byte +0x20 (asm `[local_38+0x20]`).
     * Heavy-attack gate: the 0x53-slot short-circuit fires only when this
     * field is zero AND npc_type != 0x22.  Production reads stay BSS-zero
     * (PHC #19 — no writers in binary), so the gate opens by default. */
    int32_t heavy_atk_mode;

    /* C8jb.5b — engine table bytes +0x34/+0x38/+0x3c/+0x40, all loaded via
     * `fild` (i.e. stored as int32 but interpreted as float for the damage
     * math).  Production reads stay BSS-zero (PHC #19 — no writers); tests
     * inject values directly.  See port C file for the formula.
     *
     *   attrs_int_34  attacker pass-2 KB modifier (* RNG / 2).
     *   attrs_int_38  pass-2 NPC quirk scale (* npc.damage_quirk_mul_ab8² / 4),
     *                 read from the NPC's own per-type entry regardless of
     *                 attacker/idle.
     *   attrs_int_3c  attacker pass-1 damage curve (* RNG / 2).
     *   attrs_int_40  pass-1 NPC quirk scale: idle multiplies by
     *                 npc.damage_quirk_mul_ab8² then / 4; attacker reads it
     *                 raw (no npc mul) then / 4.
     */
    int32_t attrs_int_34;
    int32_t attrs_int_38;
    int32_t attrs_int_3c;
    int32_t attrs_int_40;
} scene1_combat_npc_type_attrs_t;

#define SCENE1_COMBAT_NPC_TYPE_ATTRS_COUNT 256
extern scene1_combat_npc_type_attrs_t
    g_scene1_combat_npc_type_attrs[SCENE1_COMBAT_NPC_TYPE_ATTRS_COUNT];

/*
 * Total count of (NPC, sub-iter) pairs that passed the distance check
 * + AABB Y-band check during the most recent scene1_combat_sm_tick()
 * call.  Reset to 0 alongside g_scene1_combat_phase_b_visit_count.
 * Observable for tests that want to count "would-hit" candidates
 * without installing the collision hook.
 *
 * Note: a single NPC may contribute up to 7 (0x44/0x45) or 2 (0x46/0x47)
 * collision hits to this counter — one per sub-iter that lands in range.
 */
extern int32_t g_scene1_combat_phase_b_collision_count;

/*
 * Per-collision hook.  Called once per (NPC, sub-iter) pair that passes
 * both the distance check and the AABB Y-band check.  `sub_iter` is the
 * engine's `local_50` (0-based index into the multi-hit anchor table);
 * for non-multi-hit NPCs always 0.  Default NULL → no callback.
 */
typedef void (*scene1_combat_phase_b_collision_fn)(int npc_index, int sub_iter);
scene1_combat_phase_b_collision_fn
scene1_combat_set_phase_b_collision_hook(scene1_combat_phase_b_collision_fn fn);

/*
 * Total count of "armed" collisions during the most recent
 * scene1_combat_sm_tick() call — collisions where engine `local_18`
 * would stay 0, meaning the downstream damage roll (C8jb.5+) will
 * apply real damage rather than a no-damage observable hit.
 *
 * A collision is unarmed (`local_18 = 1` in engine) when:
 *   - NPC type ∉ {0x46, 0x47} AND sub_iter > 0 (anchor-path sub-iter for
 *     0x44/0x45 secondary hits)
 *   - NPC type == 0x48 (always disarms in range)
 *   - NPC type ∈ {0x44, 0x45} AND NOT (phase==6 ∧ subphase==1) AND
 *     |atan2(dx, dz) - yaw + π normalized| ≥ 0.3π (≈ 0.9424779)
 *
 * Otherwise armed.  Counter resets alongside collision_count.  Range:
 * 0 ≤ armed ≤ collision_count.
 */
extern int32_t g_scene1_combat_phase_b_armed_collision_count;

/*
 * Per-collision armed hook.  Called once per (NPC, sub-iter) pair that
 * is BOTH in collision range AND armed.  Adds the arming dimension to
 * the existing collision hook; tests that want to verify arming logic
 * compare counts across the two hooks / counters.
 */
typedef void (*scene1_combat_phase_b_armed_fn)(int npc_index, int sub_iter);
scene1_combat_phase_b_armed_fn
scene1_combat_set_phase_b_armed_hook(scene1_combat_phase_b_armed_fn fn);

/* ─── C8jb.5a — damage-roll prologue surfaces ────────────────────────── */
/*
 * Knockback strength factor for the most recent in-range collision.
 * Engine `local_8` — initialized to `0.7 / sqrt(VEL_X² + VEL_Z²)` (= 0.0
 * when vel_mag == 0), then forced to 0 by the slot-TYPE==0x53 short-
 * circuit.  Reset to 0 at the start of each tick that proceeds past
 * Phase A.  Read by C8jb.5b/c (damage modifier chain) and C8jb.6
 * (hit-effect emit knockback writes).  Tests probe this to verify the
 * velocity-derived initial value + the 0x53-path zeroing.
 *
 * If the iteration sees multiple in-range collisions in a single tick,
 * this holds the value of the LAST collision processed (since the SM
 * still scans all per the C8jb.3 design — engine `return 1` early-exit
 * lands with C8jb.6).
 */
extern float g_scene1_combat_phase_b_kb_strength;

/*
 * Per-collision damage-roll surface.  Engine `param_1` — the integer
 * damage value computed at the end of the per-collision damage roll
 * (Phase B "damage int").  C8jb.5a writes 0 for the slot-TYPE==0x53
 * heavy-attack short-circuit path; C8jb.5b/c will compute the general
 * damage formula and quadrant-clamped variants.  Reset to 0 at tick top.
 *
 * Tests use this to verify the 0x53 path produces damage=0 (engine
 * explicitly zeroes inside that branch).  Multi-collision behavior:
 * holds the LAST collision's damage (see kb_strength note above).
 */
extern int32_t g_scene1_combat_phase_b_damage_out;

/*
 * Total count of slot TYPE==0x53 heavy-attack short-circuits that fired
 * during the most recent scene1_combat_sm_tick() call.  Each firing
 * writes npc.npc_b18_kill_age_out + DAT_0438bed8=4 + local_8=0.
 *
 * Reset to 0 alongside the visit/collision/armed counters.  Caps at the
 * armed_collision_count since unarmed collisions still take the same
 * short-circuit path (the engine evaluates 0x53 BEFORE checking arming).
 */
extern int32_t g_scene1_combat_phase_b_heavy_atk_count;

/*
 * Engine global DAT_0438bed8 (post-hit pose lock — see survey doc for
 * the full semantic).  Written to 4 by the 0x53 heavy-attack short-
 * circuit.  Also written by other Phase B / Phase D arms (C8jb.5b+ and
 * C8jb.10+).  Exposed as a host-readable global so tests can verify the
 * SM wrote it during a 0x53 path.  BSS-zero default.
 */
extern int32_t g_scene1_combat_dat_0438bed8;

/* ─── C8jb.5b — Phase B general damage formula surfaces ──────────────── */
/*
 * Engine flow (asm 0x438c1c..0x438eaa, decomp L35313-L35371): when slot
 * TYPE != 0x53, the SM runs a two-pass damage roll then mixes the passes by
 * slot.TYPE, scales by slot.SCALE_X, then applies combo/scene-state
 * modifiers + the npc.block_dodge_b38 halve.  For slot.TYPE == 0x53 the
 * engine jumps over this entire chunk (C8jb.5a's prologue handles all 0x53
 * paths).  See port C file for the per-line formula.
 *
 * The chip exposes 4 new globals + 2 new hooks:
 *
 *   g_scene1_combat_damage_base_idle    DAT_056db0b4 — int.  Idle pass-1
 *                                       base damage (FLAG_A == 0).  Read
 *                                       once, divided by 2, subtracted
 *                                       from the npc-quirk term.
 *   g_scene1_combat_damage_base_idle2   DAT_056db0ac — int.  Idle pass-2
 *                                       base damage.  Divided by 2,
 *                                       npc-quirk subtracted from it
 *                                       (opposite sign vs pass-1).
 *   g_scene1_combat_scene_mul_014       DAT_056db014 — int.  Scene-state
 *                                       damage multiplier.  When > 0
 *                                       (with combat_scene_mul_01c == 0)
 *                                       or in combination, idle damage
 *                                       gets a *2 via LAB_00438e6d.
 *   g_scene1_combat_scene_mul_01c       DAT_056db01c — int.  Sibling.
 *   g_scene1_combat_dat_056da1b8        DAT_056da1b8 — int.  Mesh-emit
 *                                       sound-bus container; the SM ORs
 *                                       bit 1 (= 2) into it on every
 *                                       collision that runs the general
 *                                       damage formula.  Survey doc lists
 *                                       this as a side effect for slot
 *                                       TYPEs {2, 0x54, 0x6d, 0x6f, 0x70};
 *                                       asm shows the OR is unconditional
 *                                       (regardless of TYPE).
 *
 * Combo-held hook — engine FUN_0043647f (61 B, `combo_held(button_id)`).
 * Returns nonzero if the button is held in the input ring buffer
 * (DAT_0438b93c).  C8jb.5b calls it 4 times: button ids {5, 3} (idle *2),
 * {4, 3} (attacker *2), {7, 6} (common /2).  Default NULL hook returns 0
 * (no buttons held → no combo modifiers).
 *
 * RNG-damage-scale hook — engine FUN_0041f46d (57 B,
 * `rng_damage_scale(int arg)`).  Returns `1.0 + FUN_0041f319(arg) * (0.1
 * or 0.12)`.  C8jb.5b calls it twice in the attacker branch (once per
 * pass).  Argument: when slot.OWNER_B is non-null, the OWNER_B's npc_type;
 * otherwise the literal 0x1a (default).  Default NULL hook returns 1.0f
 * (deterministic damage).
 */
extern int32_t g_scene1_combat_damage_base_idle;     /* DAT_056db0b4 */
extern int32_t g_scene1_combat_damage_base_idle2;    /* DAT_056db0ac */
extern int32_t g_scene1_combat_scene_mul_014;        /* DAT_056db014 */
extern int32_t g_scene1_combat_scene_mul_01c;        /* DAT_056db01c */
extern int32_t g_scene1_combat_dat_056da1b8;         /* DAT_056da1b8 */

/*
 * Stand-in for engine deref `*(int *)(slot.OWNER_B + 0x424)` — the OWNER_B
 * record's npc_type field.  Engine asm 0x438c76 / 0x438d58 read this when
 * the attacker damage path runs.  Production OWNER_B is unported (a record
 * in another table whose stride/layout aren't modeled yet), so we expose a
 * host-settable int.  When slot.OWNER_B == 0 the SM uses npc_type=0x1a's
 * per-type entry (matching engine's hard-coded DAT_005c2e80 default).
 *
 * Default 0 → attacker damage path uses g_scene1_combat_npc_type_attrs[0].
 * Tests set this to a specific NPC type to verify per-type damage table
 * lookups.
 */
extern int32_t g_scene1_combat_owner_b_npc_type;

typedef int   (*scene1_combat_combo_held_fn)(int button_id);
typedef float (*scene1_combat_rng_damage_scale_fn)(int arg);

scene1_combat_combo_held_fn
scene1_combat_set_combo_held_hook(scene1_combat_combo_held_fn fn);

scene1_combat_rng_damage_scale_fn
scene1_combat_set_rng_damage_scale_hook(scene1_combat_rng_damage_scale_fn fn);

/* ─── C8jb.5c — Phase B post-damage clamp surfaces ───────────────────── */
/*
 * `local_1c` bitfield (engine `[ebp-0x18]`).  Direction-hit accumulator
 * built up across the per-collision damage roll.  Read by C8jb.6 (hit-
 * effect emit) to pick which hit-particle / SE to spawn; here the C8jb.5b
 * code seeds bit 0 in idle pass-2 and C8jb.5c adds bits 1/2/3 via the
 * quadrant-atan2 + npc_phase paths.  Reset at start of each per-collision
 * iteration.  Exposed as an int32_t observable so tests can inspect the
 * final bits after a tick.
 *
 *   bit 0 (= 0x1) — idle pass-2 saw slot.OWNER_FLAG != 0 (IS_PLAYER).
 *   bit 1 (= 0x2) — quadrant atan2 |angle| ≥ 3π/4 (rear hit, *1.5 dmg).
 *   bit 2 (= 0x4) — quadrant atan2 π/4 ≤ |angle| < 3π/4 (side, *1.2 dmg).
 *   bit 3 (= 0x8) — npc.npc_phase in [1, 6] (NPC is mid-combat anim,
 *                   *1.2 dmg).
 *   bits 4-7 untouched in C8jb.5b/5c.
 *
 * Multi-collision behavior: holds the LAST collision's value (consistent
 * with damage_out's reset semantics).
 */
extern int32_t g_scene1_combat_phase_b_local_1c_bits;

/*
 * Stand-ins for engine derefs `*(int *)(slot.OWNER_A + 0xce4)` and
 * `*(int *)(slot.OWNER_A + 0xcec)`.  Engine reads them at L35415 of the
 * Phase B damage roll (asm 0x439094 / 0x43909a) for idle-branch "OWNER_A
 * status flag" gating: if either is non-zero, damage *= 1.5.  Slot.OWNER_A
 * points to an unported per-character / per-NPC record block; same caveat
 * as g_scene1_combat_owner_b_npc_type — host-settable, BSS-zero in
 * production.
 */
extern int32_t g_scene1_combat_owner_a_ce4;
extern int32_t g_scene1_combat_owner_a_cec;

/*
 * Unsigned RNG hook — engine FUN_00471084 (= rng wrapper, returns
 * uint).  C8jb.5c calls it once per collision in the final clamp path
 * (`damage += rng % (damage / 5)` for damage >= 5 OR `damage += rng & 1`
 * for damage in [1, 5)).  Default returns 0 (deterministic — no jitter).
 * Tests inject specific values to verify the formula.
 */
typedef uint32_t (*scene1_combat_rng_unsigned_fn)(void);
scene1_combat_rng_unsigned_fn
scene1_combat_set_rng_unsigned_hook(scene1_combat_rng_unsigned_fn fn);

/*
 * Hook for FUN_00482a51 — engine 2-arg helper called from the charge-
 * attack disarm body (`call 0x482a51(npc, 4)`).  Already host-installable
 * via `scene1_records_b_set_aux_482a51_hook` (from C8j-tick.5 — same
 * function called by other bodies).  We reuse the existing hook; no new
 * surface here.  Documented for completeness.
 */

/* ─── public entry ───────────────────────────────────────────────────── */
/*
 * Tick the per-record state machine for one slot.
 *
 * `slot` must point to the first dword of an SCENE1_RECORDS_B_STRIDE-sized
 * slot in g_scene1_records_b (caller's responsibility; the slot pointer
 * is treated identically to the engine's `param_1`).  The function does
 * NOT validate the pointer.
 *
 * Returns the SM ret contract value {0, 1, 2}.  C8jb.1..5a return 0 in
 * all paths — Phase A short-circuit OR Phase B iter-completes-with-no-
 * hit OR Phase C/D stub fall-through.
 *
 * Side effects in C8jb.1..5a:
 *   - On fall-through past Phase A (all entry gates zero): writes
 *     g_scene1_records_b_tick_flag = 1.  Resolves PHC #21.
 *   - Resets visit_count + collision_count + armed_collision_count to 0
 *     then increments them per NPC / per collision / per armed collision
 *     during Phase B scan.  If the Phase B outer gate (FLAG_A in {0,3}
 *     && player_hp > 0) fails, all three counters stay at 0.
 *   - Calls visit / collision / armed hooks (if installed) per
 *     visit / per collision / per armed collision.
 *   - Per in-range collision (armed or not): writes hit_history ring +
 *     hit_cursor bump, computes velocity-derived kb_strength.
 *   - Per in-range collision with slot TYPE==0x53 AND per-NPC-type
 *     heavy_atk_mode==0 AND npc.npc_type != 0x22: writes
 *     npc.npc_b18_kill_age_out + g_scene1_combat_dat_0438bed8 = 4,
 *     bumps heavy_atk_count, sets kb_strength = 0, damage_out = 0.
 *
 * Additional C8jb.5b side effects (per in-range collision when slot TYPE
 * != 0x53):
 *   - OR `g_scene1_combat_dat_056da1b8 |= 2` (mesh-emit sound bit).
 *   - Compute the two-pass damage formula then write the final int into
 *     g_scene1_combat_phase_b_damage_out.  Holds the LAST collision's
 *     value when multiple collisions occur in one tick.
 *   - Calls the combo_held hook up to 4 times (per branch) and the
 *     rng_damage_scale hook up to 2 times (attacker only).
 *
 * Additional C8jb.5c side effects (per in-range collision when slot TYPE
 * != 0x53):
 *   - May reset NPC combat phase to 6 (NPC 0x44/0x45 + slot.TYPE 0x12 +
 *     sub_iter==0 + npc_phase != 6).
 *   - May fire the charge-attack disarm body (writes npc_phase=4, calls
 *     aux_482a51 hook).  Disarms damage to 0 in the final clamp.
 *   - Multiplies damage by 1.2 (npc_phase in [1,6]), 1.2 (side hit), 1.5
 *     (back hit), 1.5 (idle OWNER_A flag set), 2.0 (idle IS_PLAYER).
 *   - Final clamp writes damage to 0 if disarmed; clamps negative to 0
 *     for NPC type 5; floors at 1 + adds RNG jitter for others.
 *   - Writes g_scene1_combat_phase_b_local_1c_bits with the accumulated
 *     direction-hit bits.
 *   - Calls the rng_unsigned hook once per collision (in the final
 *     clamp path).
 */
int scene1_combat_sm_tick(int32_t *slot);

/*
 * Wrapper adapter: install scene1_combat_sm_tick as the existing
 * scene1_records_b_set_state_machine_hook (void return).  The void hook
 * loses the {0, 1, 2} ret distinction — the integrator's translator
 * (`state_machine_call_ret`) coerces all "hook installed" into ret=1.
 *
 * C8jb.1 does NOT auto-install — production wiring stays as-is (no SM
 * hook = ret=0).  Tests call this to exercise the integrator coupling
 * with Phase A side effects (per-tick flag write).
 */
void scene1_combat_sm_install_as_void_hook(void);
void scene1_combat_sm_uninstall_void_hook(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_COMBAT_SM_H */
