/*
 * scene1_postload — scene-1 INGAME state-entry hooks (Cf.1 MVP).
 *
 * Targets the tail of engine FUN_00436f97 (line 690-700 of the
 * decompile, i.e. the 200-iter ambient-particle spawn loop).  Full
 * survey: `docs/findings/scene1-postload-init.md`.
 *
 * The Cf.1 MVP ports only:
 *
 *   - block 11 i=0 case   →  scene1_postload_pose_player()
 *   - block 23            →  scene1_postload_ambient_spawn()
 *
 * Everything else in the 710-line FUN_00436f97 body (spiral terrain-
 * collision retry, multi-player pose, ~120 BSS resets, the 11 sub-init
 * helpers, the per-stage-class init dispatch, the alt-stage arm, the
 * music start, the tail-call epilogue) is intentionally out of scope.
 *
 * Stand-in seam: the engine's per-stage default player-spawn-pos
 * globals at `_DAT_0438b1ec/f0/f4` are written by the per-stage init
 * dispatch family (not ported yet).  Until that lands we model them as
 * a static 3-float array with the FUN_0044f13d-observed default
 * (-40, 0, -60).  Pending-human-check #9: validate via Frida that
 * those literals match the engine's value at the moment FUN_00436f97
 * actually runs.
 */
#ifndef OPENRECET_SCENE1_POSTLOAD_H
#define OPENRECET_SCENE1_POSTLOAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Engine analog: `_DAT_0438b1ec/f0/f4`.  Default values match
 * `FUN_0044f13d:35-38` decompile (0xc2200000 / 0 / 0xc2700000 =
 * -40.0f / 0.0f / -60.0f).  Reset to defaults by
 * scene1_postload_init_stage_defaults().
 */
extern float g_scene1_stage_player_default_pos[3];

/*
 * Reset `g_scene1_stage_player_default_pos` to the
 * FUN_0044f13d-observed defaults.  Idempotent; safe to call multiple
 * times per scene transition.
 */
void scene1_postload_init_stage_defaults(void);

/*
 * Block-11 i=0 case of engine FUN_00436f97 (L228-243).  Copies
 * `g_scene1_stage_player_default_pos` into `g_scene1_player_pos`.
 * Engine also: zeroes `g_scene1_player_vel` (BSS-zero by default in
 * our port), zeroes the multi-player slots player[1] / player[2]
 * (no consumer), and runs a spiral terrain-collision retry via
 * FUN_00432e50 (unported — terrain query is its own chip).  Cf.1
 * does just the single-player pose copy.
 */
void scene1_postload_pose_player(void);

/*
 * Cchr.2h — seed the HOUSE *standing* player pose: set g_scene1_player_pos
 * to the leaf-validated standing position (-0.30, 0, 9.35) and seed the
 * player-controller actor-state model (char id / scale / sprite-state
 * record) via player_ctrl_pose_house_standing().  Call on HOUSE entry
 * AFTER pose_player (it intentionally overrides the (-40,0,-60) pre-gate
 * default).  De-MVP of scene1_shop_walker_set_player_inject; faithful
 * spawn-placement (DAT_0438b1ec) is the remaining follow-up.
 */
void scene1_postload_pose_house_standing(void);

/*
 * The DAY-2 in-scene actor re-place (retail FUN_0048526d → FUN_00436f97, polled
 * at the day-advance).  POSITIONS ONLY — re-seat the two live actors at the
 * house-standing spot (player -0.30/0/9.35, companion 0.6/3.0/9.35) so the day-2
 * broom no longer inherits the stale first-customer counter positions; the
 * conversation-pose beat re-derives the facing + holds the anim.  Driven as a
 * one-shot by scene1_tutorial_dispatch (armed at the iv2_5 beat, consumed on the
 * first free-roam frame).  Distinct from pose_house_standing, which ALSO resets
 * the beat/pose/bob and so must NOT be used mid-beat.
 */
void scene1_postload_day2_actor_replace(void);

/*
 * Block-23 of engine FUN_00436f97 (L690-700).  No-op when
 * `g_stage_palette == NULL` or `g_stage_palette->ambient_spawn_flag
 * == 0` (the engine gate at `*(DAT_068dd2f0 + 0x1b28) != 0`).  When
 * the gate is set, runs the 200-iter spawn-and-tick loop:
 *
 *   for (i = 200; i > 0; --i) {
 *     scene1_spawn(0, player.x, player.y + 2.0f, player.z,
 *                  0x4f, 1.0f, 1);
 *     scene1_particles_tick();
 *   }
 *
 * Reads player pose from `g_scene1_player_pos[3]` — caller is
 * expected to have called scene1_postload_pose_player() first
 * (or set the pose directly).
 */
void scene1_postload_ambient_spawn(void);

/*
 * Test / smoke helper — writes `g_stage_palette->ambient_spawn_flag`
 * directly.  Safe with `g_stage_palette == NULL` (no-op).  Mirrors
 * how the engine's per-stage init writers will populate the field
 * once they port.
 */
void scene1_postload_force_ambient_flag(int value);

/*
 * CLI override — when nonzero, scene1_postload_ambient_spawn ignores
 * `g_stage_palette->ambient_spawn_flag` and runs the 200-iter loop
 * unconditionally (still skips when `g_stage_palette == NULL`).
 * Default is 0 (engine-faithful: gated on palette flag).
 *
 * Drives `--force-ambient-spawn`.  Set this in lieu of poking the
 * palette field directly when the override needs to survive a later
 * `stage_palette_init_house()` reset.
 */
void scene1_postload_set_force_ambient(int force);

/*
 * CLI override — when >= 0, replaces the hardcoded type 0x4f in
 * the ambient-spawn loop body with the given type id.  Pass -1 to
 * restore the engine default.
 *
 * Drives `--ambient-spawn-type <N>`.  Useful for surfacing Pass F
 * pixels through the real postload path (override = 0x92) instead
 * of the `--show-pass-f-test` manual injection.
 */
void scene1_postload_set_ambient_type_override(int type);

/*
 * CLI override — when `enable != 0`, the ambient-spawn loop uses
 * (x, y, z) verbatim as the spawn anchor instead of
 * (player.x, player.y + 2.0f, player.z).  Pass enable=0 to restore
 * the engine default (read from g_scene1_player_pos).
 *
 * Drives `--ambient-spawn-pose <x>,<y>,<z>`.  The HOUSE engine camera
 * is anchored near world origin and not driven by player pose (see
 * docs/findings/scene1-camera-helpers.md), so the engine-default
 * spawn at the player's HOUSE-default pose (-40, 0, -60) lands
 * off-frame.  This override places the smoke inside the camera
 * frustum (e.g. 0,0,-10) so the Pass F billboards become visible
 * through the production spawn pipeline.
 */
void scene1_postload_set_ambient_pose_override(int enable,
                                               float x, float y, float z);

/*
 * Chip C8j.fin.c (2026-05-24) — table C smoke wiring.
 *
 * Defaults match HOUSE-engine behaviour (no smoke): pickup_type = -1
 * and world_drop_type = -1 leave `scene1_postload_smoke_c_spawn()` as
 * a no-op.  When either type is set ≥ 0, the runner is fired once per
 * HOUSE entry from the preload tail (after the ambient-spawn hook),
 * staging records into the table C ring that the C8j.3 default-arm
 * ticker advances every INGAME frame.
 *
 * Spawn pose reuses `--ambient-spawn-pose` when set (the existing pose
 * override above); without it, the spawn anchor defaults to
 * (player.x, player.y + 2, player.z) — same as the ambient-spawn loop.
 *
 * Pass C/D walker bodies in `scene1_wide_followup.c` are TODO stubs,
 * so this chip does NOT yet produce visible pixels.  It validates that
 * the table C tick + spawn ports survive the production pipeline and
 * unblocks Pass C/D body ports (which can then count on real records
 * existing in HOUSE under a known smoke flag).
 */
void scene1_postload_set_force_c_pickup_type(int type);
void scene1_postload_set_force_c_world_drop_type(int type);
void scene1_postload_set_force_c_world_drop_count(int count);
void scene1_postload_set_force_c_world_drop_mag(float mag);
void scene1_postload_smoke_c_spawn(void);

/*
 * Chip C8j.fin.b (2026-05-24) — table B smoke wiring.
 *
 * Defaults (-1 / -1) make `scene1_postload_smoke_b_spawn()` a no-op.
 * When either type is set ≥ 0, fires `scene1_record_b_spawn_npc()`
 * and/or `_entity()` once per HOUSE entry from the preload tail
 * (after the C8j.fin.c table C runner).
 *
 * Owner pointers are backed by static blobs internal to scene1_postload.c
 * (NPC = 1024 B at +0x3f8 max, entity = 3760 B at +0xeac max).  The
 * blobs are zeroed once + populated with an identity matrix at the
 * allocator-read offset (NPC: +0x39c, entity: +0xde8) + pos triplet at
 * the allocator-read pos offset (NPC: +0x3f0, entity: +0x20).
 *
 * Spawn pose reuses `--ambient-spawn-pose` when set; else
 * (player.x, player.y + 2, player.z) — same as table A/C smoke runners.
 *
 * Anchor types: NPC 0xe / 0x97 / 0x46 (preamble-only, LAB_00447584
 * tail-share); entity 0x24 (preamble-only).  These exercise the
 * preamble + slot commit path without needing additional owner field
 * derefs.  More-complex types can be exercised once the chips have
 * been validated end-to-end.
 *
 * The C8j ladder's per-type bodies (C8j.5-13) are validated by direct
 * unit tests with hand-rolled fake-owner blobs (see
 * tests/test_scene1_records_b_spawn.c); this chip exercises the SAME
 * code through the production preload pipeline so the integration
 * surface is also covered.  Like C8j.fin.c, no visible HOUSE pixels
 * yet — wide_followup Pass A/B/E walker bodies are TODO stubs.
 */
void scene1_postload_set_force_b_npc_type(int type);
void scene1_postload_set_force_b_entity_type(int type);
void scene1_postload_smoke_b_spawn(void);

/*
 * Chip Cf.minimal (2026-05-26) — phase-2 walker-array writer.
 *
 * Engine analog: the FUN_00436f97 alt-stage arm chunk at decomp L34770+
 * (asm 0x4378d6..0x437a47 dispatch + scalar fanout, 0x437a4b..0x437ac5
 * 10-iter position loop).  Populates the per-mesh-furniture arrays
 * `g_scene1_walker_phase2_*` that PII.3b's `scene1_walker_pass_render_house`
 * iterates to draw the HOUSE shop_table furniture.
 *
 * The engine only runs this chunk when re-entering INGAME from a sub-
 * scene (sub-scene return path via `FUN_0048526d`/`FUN_0049e163` — all
 * unported).  Initial title → HOUSE entry doesn't fire it, so HOUSE
 * boot has phase2_count == 0 and no furniture is drawn.
 *
 * Defaults match engine BSS-zero behaviour (no furniture rendered on
 * default boot).  Enable via `scene1_postload_set_walker_phase2_scene_type()`
 * with a value in [0..4].
 *
 * Stand-in seams (no consumers wire to these in production yet):
 *   - scene_type (= engine `DAT_068dd3fc[stage*0x6cf]`): drives the
 *     count dispatch.  -1 disables (no writer fires).  0..4 enables.
 *   - per-stage furniture position table (= engine
 *     `stage_record + 0x2ce14`): 10 (x, z) int pairs used by the
 *     10-iter position loop as `pos[i] = 2 * (stage_pos[i] - rdata_anchor[i])`.
 *   - iVar8 stand-in: the engine value of `iVar8` at the moment the
 *     dispatch reads it.  Stays 0 in our model (consistent with the
 *     decompile's exit values from prior loops, but unverified without
 *     Frida — pending-human-check candidate).
 *
 * See `docs/findings/scene1-walker-pass-init.md` "Cf.survey landing"
 * section for full asm-decoded structure + reachability analysis.
 */
void scene1_postload_walker_phase2_init(void);

/*
 * Set the scene_type used by the writer.  Values in [0..4] enable the
 * writer to fire; -1 (default) disables.  Drives `--force-walker-phase2
 * <N>`.  When enabled, the writer runs at HOUSE-entry time from
 * scene1_preload_house.
 */
void scene1_postload_set_walker_phase2_scene_type(int scene_type);

/*
 * Override the per-stage furniture position table (engine stage_record
 * + 0x2ce14, 10 × (int x, int z) pairs).  When `positions` is non-NULL,
 * copies 20 ints into the stand-in storage.  Pass NULL to reset to
 * BSS-zero defaults (which produce world-position output that mirrors
 * the .rdata anchor table inverted — see formula in implementation).
 */
void scene1_postload_set_walker_phase2_stage_positions(const int32_t positions[10][2]);

/*
 * Override the iVar8 stand-in.  Default 0.  Surfaced for tests that
 * need to exercise the scene_type==0/2 dispatch paths (which write
 * iVar8 to a count field).  Production code should leave at default.
 */
void scene1_postload_set_walker_phase2_ivar8(int ivar8);

/*
 * Apply the retail-captured new-game HOUSE inputs (scene_type=0, ivar8=3,
 * and the 10 stage-position pairs read from the per-save-slot record at
 * &DAT_044e3798 + 0x2ce10).  Ground truth: tools/dump_phase2_groundtruth.py
 * → runs/phase2-groundtruth.clean.json, validated bit-for-bit by the
 * test_scene1_postload_walker_phase2_retail_groundtruth_new_game_house
 * host test.  This is an MVP stand-in for the unported runtime sources of
 * those three inputs (the DAT_068dd3fc selector + the save record); it lets
 * `--force-walker-phase2 0` reproduce retail's 3 live furniture meshes so
 * HOUSE furniture pixels surface for visual A/B.  Call before
 * scene1_postload_walker_phase2_init (i.e. before scene1_preload_house).
 */
void scene1_postload_apply_walker_phase2_house_groundtruth(void);

/*
 * Port of engine FUN_0048ffd9 — seed the active save-slot record's
 * furniture array (+0x2ce10) from the static template (DAT_005cf864) row
 * selected by the record's shop-tier field (+0x2cde0).  Called by the
 * new-game record seeder (engine FUN_0049d36d); here, by the HOUSE-entry
 * loader below.
 */
void scene1_postload_seed_house_furniture(void);

/*
 * Production HOUSE-entry input loader (de-MVP of --force-walker-phase2 0).
 * Sources the Cf walker's scene_type / ivar8 / stage_positions and the
 * camera char_mode from real engine state (the stage selector + the seeded
 * per-save-slot record) instead of the flag-gated retail-capture injection.
 * Call on HOUSE entry, before scene1_postload_walker_phase2_init().  The
 * save arena must already be seeded (save_bank_init_all at boot).
 */
void scene1_postload_load_house_phase2_inputs(void);

/*
 * Test/debug override for the HOUSE-entry loader's scene_type.  <0 (the
 * default) makes the loader use the real HOUSE value (0); >=0 forces that
 * scene_type so `--force-walker-phase2 N` can still drive the synthetic
 * tiers 1..4.
 */
void scene1_postload_set_house_scene_type_override(int scene_type);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_POSTLOAD_H */
