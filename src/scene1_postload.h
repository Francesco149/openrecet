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

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_POSTLOAD_H */
