/*
 * scene1_camera — HOUSE camera-pose + orientation helpers (Cc.1).
 *
 * Ports the default-path HOUSE-faithful core of three engine functions:
 *
 *   FUN_00441c3e (2217 B) @ 0x441c3e — camera pose update.
 *   FUN_004424e7 ( 429 B) @ 0x4424e7 — pitch / yaw / orientation matrix.
 *   FUN_0040120c ( 116 B) @ 0x40120c — view matrix builder (LookAtRH × RotZ).
 *
 * Scope follows `docs/findings/scene1-camera-helpers.md`:
 *
 *   - Class 0 (HOUSE) and the class 2/3 offset constants only.  Class 1
 *     (post-load transition) is deferred to Cc.2.
 *   - No camera shake: block L of FUN_00441c3e gates on
 *     `DAT_0438cc14 > 0` which is BSS-zero with no writer in our port.
 *     Shake-y addend folds to 0.
 *   - No cinematic counter ramp: block F gates on `DAT_0438be94 > 0x3c`,
 *     BSS-zero today.  Deferred to Cc.4.
 *   - No debug HUD overlays: block J's four `FUN_00451874` calls are
 *     pure visuals (HEIT / DIST / ??? / MIPMAP labels).  Deferred to Cc.3.
 *   - No `_DAT_0438ce38` 4×4 build: it has no consumer in our port today.
 *
 * The orientation matrix `g_scene1_camera_orient` IS written — Pass D of
 * `scene1_wide_followup` is the consumer the moment its inner body ports
 * (today the per-record draw is a TODO stub but the matrix lookup is
 * already referenced in the survey).
 *
 * .rdata constants resolved by Cc.0 (via PE section parse, see survey
 * doc): `_DAT_005c4fd0/d4/d8 = -1.8 / 14.0 / 21.0`.
 *
 * Z-roll constant `_DAT_006051c4` is BSS-zero in the unpacked exe
 * (vsize > rsize cliff at 0x603e00 → loader-zeroed at this address).
 * `FUN_0040120c`'s `RotZ(z_roll / 2.0)` collapses to identity ⇒ the
 * matmul is a no-op and the view matrix is the raw LookAtRH.  Tests can
 * inject a non-zero roll via `g_scene1_camera_z_roll` to exercise the
 * matmul branch.
 *
 * Pending-human-check #11: `(&DAT_045105a4)[char*0x2dfc8]` lives outside
 * the unpacked exe's static image — written at runtime via VirtualAlloc
 * at a fixed VA by per-character / per-scene init code we haven't ported
 * yet.  Until that lands we model it as `g_scene1_camera_char_mode`
 * defaulting to 2 (= "shop view", the only HOUSE-relevant value).
 */
#ifndef OPENRECET_SCENE1_CAMERA_H
#define OPENRECET_SCENE1_CAMERA_H

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical eye + lookat (engine: `_DAT_073de31c..330`).  Eye orbits;
 * lookat is the static target.  Standard right-handed look-at; D3D8
 * SetTransform reads the view matrix built from these via
 * `scene1_camera_build_view_matrix`. */
extern float g_scene1_camera_eye[3];
extern float g_scene1_camera_lookat[3];

/* Orientation matrix (engine: `_DAT_0438cdf8`, 4×4 row-major).  Written
 * by `scene1_camera_angle_compute()` as `RotX(π/2 - pitch) × RotY(yaw + π)`
 * where pitch/yaw are derived from eye-vs-lookat (axis labels corrected
 * 2026-05-29 — see angle_compute).  Consumed by the chr-sprite walker's
 * billboard base (FUN_00456f56) and `scene1_wide_followup` Pass D. */
extern float g_scene1_camera_orient[16];

/* Engine DAT_0438bfa8 / DAT_0438bfac — written by
 * `scene1_camera_angle_compute`'s 8-azimuth loop tail (L32-L44 of the
 * decompile, asm 0x4425f9..0x44268c).  PHC #10 verified via asm read:
 * the 4 sin/cos calls inside the loop discard their return values via
 * `fstp st(0)`, so the only side effects are the global counter
 * increment + the final lerp.
 *
 *   _DAT_0438bfa8 (int)   — incremented to 8 then reset/incremented every
 *                            angle-compute call.
 *   _DAT_0438bfac (float) — smoothed sample count via
 *                            `f' = lerp(f, (float)counter, 0.1)`.
 *
 * No in-port consumer reads these globals today.  Exposed as externs so
 * tests can observe the side effects of angle_compute. */
extern int   g_scene1_camera_sample_counter;
extern float g_scene1_camera_sample_smoothed;

/* Stand-in for `(&DAT_045105a4)[char*0x2dfc8]` — the per-character view
 * mode that gates the class-offset and z-bias branches inside
 * FUN_00441c3e.  Pending-human-check #11.  Default 2 = "shop view"
 * (HOUSE).  Tests can override to exercise the < 2 / >= 2 paths. */
extern int g_scene1_camera_char_mode;

/* Stand-in for `(&DAT_068dd3fc)[stage*0x6cf]` — the per-stage view-mode
 * byte that gates the 4-way clamp switch in block E.  Cc.0 verified
 * this falls into BSS-zero territory for HOUSE; default 0 picks the
 * `[-5, -1]` x / `≤1` z clamp. */
extern int g_scene1_camera_stage_view_mode;

/* Z-roll constant (engine: `_DAT_006051c4`).  Cc.0 verified this lands
 * in BSS-zero in the unpacked exe.  Tests can set a non-zero value to
 * exercise the `RotZ` post-multiply inside `scene1_camera_build_view_matrix`;
 * production keeps it 0. */
extern float g_scene1_camera_z_roll;

/* Live per-class camera z-offset (engine `_DAT_0438b778`).  The FUN_0040a765
 * emote-bubble draw projects the player head at world-Y `off_z·0.1 + 4.0 + py`. */
float scene1_camera_class_off_z(void);

/* Initialise camera state.  Sets the first-frame flag (engine
 * `_DAT_0438cc68 = 1`) so the first `scene1_camera_pose_compute()` call
 * snap-copies instead of lerping.  Idempotent — every call resets the
 * flag and clears the smoothed-state scratch so a test or scene
 * transition gets a clean start. */
void scene1_camera_init(void);

/* Default-path port of FUN_00441c3e.  Reads:
 *   - `g_scene1_camera_char_mode` (stand-in for the +0x2dfc8 char rec)
 *   - `g_scene1_camera_stage_view_mode` (stand-in for the +0x6cf stage rec)
 *   - `g_scene1_camera_yaw` (extern, engine `_DAT_073de39c`)
 *   - `g_stage_palette` (only NULL-tested today — floor-height bias at
 *      +0x1b1c is BSS-zero per HOUSE defaults, so the read folds to 0)
 *
 * Writes:
 *   - `g_scene1_camera_eye[3]` (orbital eye)
 *   - `g_scene1_camera_lookat[3]` (target / player anchor base)
 *   - `g_scene1_camera_anchor[2]` (existing extern, kept in sync with
 *     lookat.x/lookat.z so the particle handlers reading it pick up the
 *     new pose this frame)
 *
 * Idempotent: each call advances the smoothing lerp one step toward the
 * computed target.  After the first call (snap branch), subsequent
 * calls converge floor-bias to its 3.0 fixed point and eye/lookat to
 * their steady-state values determined by yaw and char_mode. */
void scene1_camera_pose_compute(void);

/* Port of FUN_004424e7.  Reads `g_scene1_camera_eye` + `_lookat` and
 * writes:
 *   - `g_scene1_camera_orient[16]` (4×4 = RotY × RotX product)
 *   - `g_scene1_camera_sample_counter` (int, reset to 0 then incremented
 *     to 8 each call) and `g_scene1_camera_sample_smoothed` (float,
 *     lerped toward sample_counter at rate 0.1).  PHC #10 verified via
 *     asm — the 4 sin/cos calls inside the 8-azimuth loop discard their
 *     return values via `fstp st(0)`, so the only side effects are the
 *     counter increment + smoothed lerp.
 *
 * Engine singular-dist guard: when `eye.xz == lookat.xz` and the
 * vertical delta is also zero, engine calls `FUN_00404bb8` (error
 * reporter) and the matrix write is skipped.  We mirror that — bail
 * out before the matrix write so the previous frame's orientation
 * stays.  When only `dx==0 && dz==0`, engine injects `dz = 0.01` to
 * keep the atan2 well-defined; we do the same. */
void scene1_camera_angle_compute(void);

/* Port of FUN_0040120c.  Builds the view matrix at `out[16]` from
 * `g_scene1_camera_eye` (RH eye), `g_scene1_camera_lookat` (RH target),
 * up = (0, 1, 0).  Applies the Z-roll via `RotZ(z_roll / 2.0)`
 * post-multiply; with the production z_roll=0 the matmul is a no-op.
 *
 * Engine analog: writes `_DAT_073de29c` (row-major float[16] cast to
 * D3DMATRIX for SetTransform).  Caller passes the destination buffer
 * directly. */
void scene1_camera_build_view_matrix(float out[16]);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_SCENE1_CAMERA_H */
