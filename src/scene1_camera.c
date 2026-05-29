/*
 * scene1_camera.c — see scene1_camera.h for the Cc.1 scope.
 *
 * Engine references (line numbers from docs/decompiled/by-address/):
 *   441c3e.c  L45-L280  — pose helper.
 *   4424e7.c  L16-L44   — angle helper.
 *   40120c.c  L17-L30   — view matrix builder (inlined into the angle
 *                          helper's tail in the engine; we keep it as a
 *                          separate function so render-side callers can
 *                          rebuild the view without rerunning the pose
 *                          smoothing).
 */

#include "scene1_camera.h"

#include <math.h>

#include "math3d.h"
#include "scene1_particles_tick.h"  /* g_scene1_camera_anchor[], g_scene1_camera_yaw */
#include "stage_palette.h"           /* g_stage_palette */

/* ─── .rdata constants (Cc.0-resolved via PE section parse) ───────────── */
static const float DAT_005c4fd0 = -1.8f;
static const float DAT_005c4fd4 = 14.0f;
static const float DAT_005c4fd8 = 21.0f;

/* ─── public state (engine globals at fixed VAs) ──────────────────────── */

float g_scene1_camera_eye[3]     = { 0, 0, 0 };
float g_scene1_camera_lookat[3]  = { 0, 0, 0 };
float g_scene1_camera_orient[16] = { 0 };

int   g_scene1_camera_char_mode        = 2;     /* shop view; PHC #11 */
int   g_scene1_camera_stage_view_mode  = 0;     /* HOUSE BSS-zero */
float g_scene1_camera_z_roll           = 0.0f;  /* _DAT_006051c4 */

int   g_scene1_camera_sample_counter   = 0;     /* engine DAT_0438bfa8 */
float g_scene1_camera_sample_smoothed  = 0.0f;  /* engine DAT_0438bfac */

/* ─── module-internal smoothed state (engine `_DAT_0438cc38..58`) ─────── */

static float g_smooth_eye[3]    = { 0, 0, 0 };
static float g_smooth_lookat[3] = { 0, 0, 0 };

/* Engine `_DAT_06a46f9c`: smoothed floor-height bias. */
static float g_floor_bias = 0.0f;

/* Compose-formula globals the port previously hard-coded to 0.0f as a
 * "BSS-zero" assumption.  Retail HOUSE has them NON-zero — proven via
 * tools/dump_camera_groundtruth.py at the HOUSE furniture frame:
 *   _DAT_0695ef70 (radius add) = 14.0    = .rdata DAT_005c4fd4
 *   _DAT_044e2c70 (eye.y  add) = 21.0    = .rdata DAT_005c4fd8
 *   _DAT_069b2f78 (lookat.y add) = -1.8  = .rdata DAT_005c4fd0
 *   DAT_056da1d8 (bias_x src) = -0.3,  DAT_056da1e0 (bias_z src) = 9.35
 * These are loaded by an unported camera-param init (sentinel-terminated
 * arrays ending at &DAT_044e2c70 / &DAT_069b2f78, see all.c L44697/933/964;
 * values come from the per-stage camera config).  Default 0 keeps boot
 * behaviour byte-identical; scene1_camera_apply_house_groundtruth() sets
 * the retail-captured HOUSE values (flag-gated MVP, parallels Cf.minimal's
 * scene1_postload_apply_walker_phase2_house_groundtruth). */
static float g_radius_add = 0.0f;   /* _DAT_0695ef70 */
static float g_eyey_add   = 0.0f;   /* _DAT_044e2c70 */
static float g_looky_add  = 0.0f;   /* _DAT_069b2f78 */
static float g_bias_x_src = 0.0f;   /* DAT_056da1d8 */
static float g_bias_z_src = 0.0f;   /* DAT_056da1e0 */

/* Engine `_DAT_0438b77c/74/78`: per-class offset triplet computed in
 * block B and read in blocks E/G/I. */
static float g_class_off_x = 0.0f;  /* _DAT_0438b77c */
static float g_class_off_y = 0.0f;  /* _DAT_0438b774 */
static float g_class_off_z = 0.0f;  /* _DAT_0438b778 */

/* Engine `_DAT_0438cc68`: first-frame snap flag.  Init to 1 via
 * scene1_camera_init() — the engine's runtime-allocated writer for this
 * address isn't ported yet (see Cc.0 open question #5). */
static int g_first_frame = 1;

/* ─── init ────────────────────────────────────────────────────────────── */

void scene1_camera_init(void)
{
    g_first_frame = 1;
    g_smooth_eye[0] = g_smooth_eye[1] = g_smooth_eye[2] = 0.0f;
    g_smooth_lookat[0] = g_smooth_lookat[1] = g_smooth_lookat[2] = 0.0f;
    g_floor_bias = 0.0f;
    /* Reset the compose-add overrides to their boot-faithful 0.  Production
     * calls scene1_camera_apply_house_groundtruth() AFTER this on HOUSE
     * entry to set the real values; clearing here keeps non-HOUSE scenes
     * (and test isolation) at the original BSS-zero behaviour. */
    g_radius_add = 0.0f;
    g_eyey_add   = 0.0f;
    g_looky_add  = 0.0f;
    g_bias_x_src = 0.0f;
    g_bias_z_src = 0.0f;
}

/* MVP HOUSE camera fix — inject the retail-captured pose inputs the port
 * can't yet source from engine state, so the HOUSE furniture renders with
 * the correct framing/orientation/scale.  Parallels Cf.minimal's
 * scene1_postload_apply_walker_phase2_house_groundtruth(); wired behind
 * the same `--force-walker-phase2 0` path in main.c.
 *
 * Values from tools/dump_camera_groundtruth.py (retail new-game HOUSE):
 *   eye=(-1, 22.2, 15), lookat=(-1, 1.2, 1), yaw=π, fov=45° (already
 *   matched), z_roll=0.  This sets the five compose inputs + char_mode +
 *   yaw that produce exactly those eye/lookat through pose_compute.
 *
 * Faithful-port follow-ups (PHC): yaw=π is a direct write in the Cf block
 * (FUN_00436f97 L589); char_mode is *(int*)(&DAT_045105a4 + slot*0x2dfc8);
 * radius/eyey/looky adds come from the per-stage camera-param loader. */
void scene1_camera_apply_house_groundtruth(void)
{
    g_scene1_camera_char_mode = 0;   /* retail per-save-slot source = 0
                                      * (port hard-coded 2; with 0 block B
                                      * takes the uVar2<2 arm → offsets 0) */
    g_radius_add        = 14.0f;     /* _DAT_0695ef70 */
    g_eyey_add          = 21.0f;     /* _DAT_044e2c70 */
    g_looky_add         = -1.8f;     /* _DAT_069b2f78 */
    g_bias_x_src        = -0.3f;     /* DAT_056da1d8 (clamps to -1.0) */
    g_bias_z_src        = 9.35f;     /* DAT_056da1e0 (clamps to  1.0) */
    /* yaw=π is now written faithfully by scene1_postload_walker_phase2_init()
     * (engine FUN_00436f97 L589), so it is no longer set here. */
}

/* ─── FUN_00441c3e default-path pose helper ───────────────────────────── */

void scene1_camera_pose_compute(void)
{
    /* Block A (L45-L49) — uVar2 = char_mode, gated by stage view-mode in
     * [0, 5).  HOUSE: stage_view_mode = 0 (in range), so the gate opens
     * and uVar2 picks up char_mode.  Other stage modes (≥ 5 or negative)
     * leave uVar2 at 0 — equivalent to "shop view disabled". */
    int uVar2 = 0;
    if (g_scene1_camera_stage_view_mode >= 0 &&
        g_scene1_camera_stage_view_mode < 5) {
        uVar2 = g_scene1_camera_char_mode;
    }

    /* Block B (L50-L71) — class-offset triplet.  Class 0 = HOUSE (engine
     * BSS-zero) splits on uVar2 < 2 vs ≥ 2.  Class 1 (transition) is
     * deferred to Cc.2; class 2/3 are dungeon variants — port the
     * arithmetic for completeness so the stage-class writer doesn't
     * surprise us when it lands. */
    const int stage_class = 0;  /* _DAT_0438b4e8 BSS-zero for HOUSE */
    if (stage_class == 0) {
        if (uVar2 < 2) {
            g_class_off_x = 0.0f;
            g_class_off_y = 0.0f;
            g_class_off_z = 0.0f;
        } else {
            g_class_off_x = -1.8f - DAT_005c4fd0;  /* = 0 */
            g_class_off_y = 18.0f - DAT_005c4fd4;  /* = 4 */
            g_class_off_z = 11.8f - DAT_005c4fd8;  /* = -9.2 */
        }
    } else if (stage_class == 2) {
        g_class_off_x = -1.8f - DAT_005c4fd0;
        g_class_off_y = 19.0f - DAT_005c4fd4;
        g_class_off_z = 25.8f - DAT_005c4fd8;
    } else if (stage_class == 3) {
        g_class_off_x = -1.8f - DAT_005c4fd0;
        g_class_off_y = 29.0f - DAT_005c4fd4;
        g_class_off_z = 33.0f - DAT_005c4fd8;
    }

    /* Block C (L72-L85) — floor-height bias.  Engine:
     *   local_8 = max(DAT_056da1dc - 4, 0)
     *   local_c = (float)*(int*)(palette + 0x1b1c)
     *   local_8 = max(local_8, local_c)
     *   _DAT_06a46f9c =
     *       first_frame ? local_8 + 3.0 : lerp(local_8, _DAT_06a46f9c, 0.1) + 0.3
     *
     * DAT_056da1dc is the vertical input bias (BSS-zero today, no writer).
     * palette + 0x1b1c is BSS-zero on HOUSE (`stage_palette_t` doesn't
     * type the field yet; treating as 0 is engine-faithful for HOUSE).
     * Therefore local_8 = 0, and the floor-bias collapses to a fixed
     * point at 3.0 (steady state: old' = 0.9*old + 0.3 → fp = 3.0). */
    const float local_8_floor = 0.0f;  /* both candidates fold to 0 */
    if (g_first_frame == 0) {
        g_floor_bias = (local_8_floor - g_floor_bias) * 0.1f
                       + g_floor_bias + 0.3f;
    } else {
        g_floor_bias = local_8_floor + 3.0f;
    }

    /* Block E (L86-L163) — default-path eye/lookat compute.  No class-1
     * arm (deferred Cc.2). */
    float bias_x = g_bias_x_src;  /* engine: local_10 = DAT_056da1d8 */
    float bias_z = g_bias_z_src;  /* engine: local_8  = DAT_056da1e0 */

    /* `(uVar2 > 1) → local_8 -= 5`.  Char_mode=2 (shop view) lifts the
     * eye 5 units further from the target along z. */
    if (uVar2 > 1) {
        bias_z = bias_z - 5.0f;
    }
    /* DAT_0438b7ac (cinematic blend gate) BSS-zero → skip blend. */

    /* `(uVar2 & 1) == 0` clamp dispatch.  Char_mode=2 (even) opens the
     * gate; the inner branch on stage_view_mode picks one of four
     * clamp shapes.  For HOUSE (view_mode=0) bias_x is clamped to
     * [-5, -1] (lower edge, since initial 0 > -1 → -1).  bias_z gets a
     * max-of-1 ceiling but no floor — its post-uVar2 value of -5
     * stays.
     *
     * Even-only gate means the odd char_modes (1, 3, ...) skip clamps —
     * an engine quirk we preserve verbatim. */
    if ((uVar2 & 1) == 0) {
        int view_mode = g_scene1_camera_stage_view_mode;
        if (view_mode == 0) {
            if (bias_x > -1.0f) bias_x = -1.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  1.0f) bias_z =  1.0f;
            /* fallthrough to LAB_00441ff9: skips second-block clamp */
        } else if (view_mode == 1) {
            /* iVar1==1 branch: clamp twice (first pair, then
             * LAB_00441f05) and then the second-block clamp. */
            if (bias_x >  5.0f) bias_x =  5.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  1.0f) bias_z =  1.0f;
            if (bias_z < -3.0f) bias_z = -3.0f;
            /* LAB_00441f05: */
            if (bias_x > 15.0f) bias_x = 15.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  1.0f) bias_z =  1.0f;
            if (bias_z < -3.0f) bias_z = -3.0f;
            /* second-block clamp: */
            if (bias_x > 15.0f) bias_x = 15.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  8.5f) bias_z =  8.5f;
            if (bias_z < -3.0f) bias_z = -3.0f;
        } else if (view_mode == 2) {
            /* iVar1==2 branch: LAB_00441f05 then second-block. */
            if (bias_x > 15.0f) bias_x = 15.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  1.0f) bias_z =  1.0f;
            if (bias_z < -3.0f) bias_z = -3.0f;
            if (bias_x > 15.0f) bias_x = 15.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  8.5f) bias_z =  8.5f;
            if (bias_z < -3.0f) bias_z = -3.0f;
        } else if (view_mode == 3) {
            /* iVar1==3 branch: second-block only. */
            if (bias_x > 15.0f) bias_x = 15.0f;
            if (bias_x < -5.0f) bias_x = -5.0f;
            if (bias_z >  8.5f) bias_z =  8.5f;
            if (bias_z < -3.0f) bias_z = -3.0f;
        }
        /* view_mode out of {0..3}: skip all clamps (engine: LAB_00441ff9
         * via `goto`). */
    }

    /* Block F (L164-L175) — cinematic counter ramp.  Gated on
     * DAT_0438be94 > 0x3c; BSS-zero here, so local_c stays 0. */
    const float cinematic = 0.0f;

    /* Block G (L176-L187) — compose eye + lookat.
     *
     * Lookat (= block-2 "static" target — eq engine `_DAT_073de328/2c/30`):
     *   x = bias_x
     *   y = _DAT_0438b77c + _DAT_069b2f78 (BSS=0) + _DAT_06a46f9c
     *   z = bias_z
     *
     * Eye (= block-1 "orbital" — eq engine `_DAT_073de31c/320/324`):
     *   x = radius_xz * sin(yaw) + bias_x
     *   y = _DAT_0438b778 + _DAT_044e2c70 (BSS=0) + cinematic + lookat.y
     *   z = bias_z - radius_xz * cos(yaw)
     *
     * Where radius_xz = _DAT_0438b774 + _DAT_0695ef70 (BSS=0) + cinematic.
     *
     * For HOUSE + char_mode=2 + yaw=0 this yields:
     *   lookat = (-1, 3.0, -5)        — target at floor level, slightly back
     *   eye    = (-1, 3.0 + -9.2, -9) = (-1, -6.2, -9)
     *                                  — 6.2 above floor (y-down), 4 back
     * Pitch downward ≈ atan(6.2 / 4) ≈ 57°.  Camera looks down toward
     * the static room interior centered near world-origin. */
    float yaw       = g_scene1_camera_yaw;
    float radius_xz = g_class_off_y + g_radius_add + cinematic;

    float lookat_x = bias_x;
    float lookat_y = g_class_off_x + g_looky_add + g_floor_bias;
    float lookat_z = bias_z;

    float eye_x = radius_xz * sinf(yaw) + bias_x;
    float eye_y = g_class_off_z + g_eyey_add + cinematic + lookat_y;
    float eye_z = bias_z - radius_xz * cosf(yaw);

    /* Block H (L188-L204) — smoothing lerp / first-frame snap. */
    if (g_first_frame == 0) {
        g_smooth_eye[0]    = (eye_x    - g_smooth_eye[0])    * 0.2f + g_smooth_eye[0];
        g_smooth_eye[1]    = (eye_y    - g_smooth_eye[1])    * 0.2f + g_smooth_eye[1];
        g_smooth_eye[2]    = (eye_z    - g_smooth_eye[2])    * 0.2f + g_smooth_eye[2];
        g_smooth_lookat[0] = (lookat_x - g_smooth_lookat[0]) * 0.2f + g_smooth_lookat[0];
        g_smooth_lookat[1] = (lookat_y - g_smooth_lookat[1]) * 0.2f + g_smooth_lookat[1];
        g_smooth_lookat[2] = (lookat_z - g_smooth_lookat[2]) * 0.2f + g_smooth_lookat[2];
    } else {
        g_first_frame = 0;
        g_smooth_eye[0]    = eye_x;
        g_smooth_eye[1]    = eye_y;
        g_smooth_eye[2]    = eye_z;
        g_smooth_lookat[0] = lookat_x;
        g_smooth_lookat[1] = lookat_y;
        g_smooth_lookat[2] = lookat_z;
    }

    /* Block I (L205-L213) — canonical writeback.  Engine adds
     * _DAT_0438cc20 (shake.y) to both eye.y and lookat.y; that channel
     * is BSS-zero (block L gates on a non-existent writer) so we treat
     * shake.y as 0. */
    const float shake_y = 0.0f;
    g_scene1_camera_eye[0]    = g_smooth_eye[0];
    g_scene1_camera_eye[1]    = g_smooth_eye[1] + shake_y;
    g_scene1_camera_eye[2]    = g_smooth_eye[2];
    g_scene1_camera_lookat[0] = g_smooth_lookat[0];
    g_scene1_camera_lookat[1] = g_smooth_lookat[1] + shake_y;
    g_scene1_camera_lookat[2] = g_smooth_lookat[2];

    /* Mirror lookat.x/z into the existing g_scene1_camera_anchor[] used
     * by scene1_particles_tick's type-6..9 orbit handlers.  Aliases
     * engine `_DAT_073de328` + `_DAT_073de330`. */
    g_scene1_camera_anchor[0] = g_scene1_camera_lookat[0];
    g_scene1_camera_anchor[1] = g_scene1_camera_lookat[2];
}

/* ─── FUN_004424e7 angle / orientation helper ─────────────────────────── */

void scene1_camera_angle_compute(void)
{
    /* L16-L20: dx/dz in the horizontal plane.  Singular-dx-dz guard
     * injects dz=0.01 so atan2 stays well-defined. */
    float dx = g_scene1_camera_eye[0] - g_scene1_camera_lookat[0];
    float dz = g_scene1_camera_eye[2] - g_scene1_camera_lookat[2];

    if (dx == 0.0f && dz == 0.0f) {
        dz = 0.01f;
    }

    /* L21: yaw_xz = atan2(dx, dz).
     * L22: dist   = sqrt(dx² + dz²).
     * L23-25: zero-dist guard — engine calls FUN_00404bb8 (error
     *        reporter); we mirror by bailing out before any matrix
     *        write so the previous frame's orientation persists. */
    float yaw_xz = atan2f(dx, dz);
    float dist   = sqrtf(dx * dx + dz * dz);

    if (dist == 0.0f) {
        return;
    }

    /* L27: pitch = atan2(dist, eye.y - lookat.y). */
    float pitch_atan = atan2f(dist,
                              g_scene1_camera_eye[1] - g_scene1_camera_lookat[1]);

    /* L28: thunk_FUN_004a3537 = RotationY → writes 16-float matrix at
     * &_DAT_0438cd78.  Ghidra typed `_DAT_0438cd78` as float because
     * D3DXMatrixRotationY's first arg looks like a float pointer in
     * the call site, but the function writes 64 bytes starting there.
     *
     * L30: thunk_FUN_004a35d3 = RotationX → writes matrix at &_DAT_0438cdb8.
     * L31: thunk_FUN_004a2a03 = Multiply → out @ _DAT_0438cdf8.
     *
     * Net: orient = RotY(π/2 - pitch_atan) * RotX(yaw_xz + π). */
    float angle_y = 1.5707964f - pitch_atan;
    float angle_x = yaw_xz + 3.1415927f;

    float m_y[16];
    float m_x[16];
    mat4_rotation_y(m_y, angle_y);
    mat4_rotation_x(m_x, angle_x);
    mat4_mul(g_scene1_camera_orient, m_y, m_x);

    /* L32-L43: 8-azimuth loop (asm 0x4425f9..0x44266c).  Resets
     * g_scene1_camera_sample_counter to 0, then runs 8 iterations of
     * `sinf(iter * 2π/8); cosf(iter * 2π/8); sinf(angle_y_long_double);
     * cosf(angle_y_long_double); inc counter`.
     *
     * Asm verification (PHC #10): the 4 trig calls all end with
     * `fstp st(0)` (pop FPU TOS without storing) — return values are
     * explicitly discarded.  Only the counter increment is observable.
     * The angle_y_long_double argument is the just-computed angle_y
     * stored as a float10 via `fld DWORD PTR [ebp-0x10]; fstp QWORD PTR
     * [ebp-0x8]` at 0x4425f6/0x442604.
     *
     * L44 (asm 0x44266e..0x44268c): smoothed lerp at rate 0.1
     * (.rdata 0x5193a0 = 0.1).
     *
     * No in-port consumer; preserved for engine-faithful side effects. */
    g_scene1_camera_sample_counter = 0;
    for (int iter = 0; iter < 8; iter++) {
        float iter_angle = (float)iter * 6.2831855f / 8.0f;
        (void)sinf(iter_angle);
        (void)cosf(iter_angle);
        (void)sinf(angle_y);
        (void)cosf(angle_y);
        g_scene1_camera_sample_counter++;
    }
    g_scene1_camera_sample_smoothed +=
        ((float)g_scene1_camera_sample_counter
         - g_scene1_camera_sample_smoothed) * 0.1f;
}

/* ─── FUN_0040120c view matrix builder ────────────────────────────────── */

void scene1_camera_build_view_matrix(float out[16])
{
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    mat4_lookat_rh(out, g_scene1_camera_eye, g_scene1_camera_lookat, up);

    if (g_scene1_camera_z_roll != 0.0f) {
        float rotz[16];
        float tmp[16];
        mat4_rotation_z(rotz, g_scene1_camera_z_roll / 2.0f);
        mat4_mul(tmp, out, rotz);
        for (int i = 0; i < 16; i++) out[i] = tmp[i];
    }
}
