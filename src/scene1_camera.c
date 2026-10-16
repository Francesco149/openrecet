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
#include "dungeon_flow.h"   /* DAT_0438b7ac / b7a0 / b7a8 */

#include <math.h>

#include "math3d.h"
#include "rng.h"                    /* FUN_00471084 / FUN_00471089 */
#include "x87.h"
#include "scene1_particles_tick.h"  /* g_scene1_camera_anchor[], g_scene1_camera_yaw */
#include "stage_palette.h"           /* g_stage_palette, DAT_0438b4dc */
#include "tables_stage.h"            /* the stage table DAT_068dd2f8 */
#include "party.h"                   /* DAT_056da1d8.. */
#include "save_work.h"               /* work +0x2ce0c */
#include <string.h>

/* ─── .rdata constants (Cc.0-resolved via PE section parse) ───────────── */
static const float DAT_005c4fd0 = -1.8f;
static const float DAT_005c4fd4 = 14.0f;
static const float DAT_005c4fd8 = 21.0f;

/* ─── public state (engine globals at fixed VAs) ──────────────────────── */

float g_scene1_camera_eye[3]     = { 0, 0, 0 };
float g_scene1_camera_lookat[3]  = { 0, 0, 0 };
float g_scene1_camera_orient[16] = { 0 };

int   g_scene1_camera_char_mode        = 0;     /* new-game HOUSE: record+0x2ce0c
                                                 * = 0 (was hard-coded 2, PHC #11);
                                                 * the production loader resets it
                                                 * from the save record on HOUSE
                                                 * entry (scene1_postload_load_
                                                 * house_phase2_inputs). */
int   g_scene1_camera_stage_view_mode  = 0;     /* HOUSE BSS-zero */
/* Engine DAT_0438b4e8 — the camera stage-CLASS.  0 = the player-follow
 * (free-roam), 1 = the cc08==4 customer-service cinematic counter camera
 * (FUN_00462403 sets it + writes the cinematic eye/lookat; FUN_00441c3e's
 * class-1 branch then uses those verbatim).  2/3 = dungeon variants. */
int   g_scene1_camera_stage_class      = 0;
float g_scene1_camera_z_roll           = 0.0f;  /* _DAT_006051c4 */

int   g_scene1_camera_sample_counter   = 0;     /* engine DAT_0438bfa8 */
float g_scene1_camera_sample_smoothed  = 0.0f;  /* engine DAT_0438bfac */

/* ─── module-internal smoothed state (engine `_DAT_0438cc38..58`) ─────── */

/* DAT_0438cc14..cc2c: the shake (FUN_0044b219 arms it, block L runs it) */
int32_t g_shake_ticks, g_shake_dx, g_shake_dz, g_shake_xr, g_shake_zr;
float   g_shake_y, g_shake_amp;

void camera_shake(int32_t ticks, int32_t xr, int32_t zr, float amp)
{
    g_shake_dx = 0;
    g_shake_dz = 0;
    g_shake_ticks = ticks;
    g_shake_xr = xr;
    g_shake_y = 0.0f;
    g_shake_zr = zr;
    g_shake_amp = amp;
}

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
 * These are loaded by an unported camera-param init (sentinel-terminated
 * arrays ending at &DAT_044e2c70 / &DAT_069b2f78, see all.c L44697/933/964;
 * values come from the per-stage camera config).  Default 0 keeps boot
 * behaviour byte-identical.
 *
 * The camera target *bias* (engine `DAT_056da1d8`/`DAT_056da1e0`) is NOT a
 * constant — it is the LIVE player position (`g_scene1_player_pos[0]`/`[2]`),
 * read each frame in pose_compute (block E) and clamped to the room.  W3 made
 * the player controller move those globals, so the camera now pans to follow
 * the walking player, matching retail.  (Previously the port injected the
 * retail-captured spawn −0.3/9.35 statically via a now-removed
 * `scene1_camera_apply_house_groundtruth` stand-in, from when the player was
 * frozen.) */
static float g_radius_add = 0.0f;   /* _DAT_0695ef70 */
static float g_eyey_add   = 0.0f;   /* _DAT_044e2c70 */
static float g_looky_add  = 0.0f;   /* _DAT_069b2f78 */

/* Engine `_DAT_0438b77c/74/78`: per-class offset triplet computed in
 * block B and read in blocks E/G/I. */
static float g_class_off_x = 0.0f;  /* _DAT_0438b77c */
static float g_class_off_y = 0.0f;  /* _DAT_0438b774 */
static float g_class_off_z = 0.0f;  /* _DAT_0438b778 */

/* The emote-bubble draw (FUN_0040a765, all.c:6902) projects the player head at
 * world-Y = `_DAT_0438b778 * 0.1 + 4.0 + player_y`, i.e. it reads this live
 * camera z-offset.  Exposed for scene1_hud.c. */
float scene1_camera_class_off_z(void) { return g_class_off_z; }

/* Engine `_DAT_0438cc68`: first-frame snap flag.  Init to 1 via
 * scene1_camera_init() — the engine's runtime-allocated writer for this
 * address isn't ported yet (see Cc.0 open question #5). */
static int g_first_frame = 1;   /* DAT_0438cc68 — the next pose snaps (no 0.2 lerp) */

void scene1_camera_snap_next(void) { g_first_frame = 1; }

/* state_va: the pose globals by retail VA (float bits); 0 if unknown */
int scene1_camera_state_va(uint32_t va, float *out)
{
    const float *p = NULL;
    if (va >= 0x438cdf8u && va < 0x438cdf8u + 64u && va % 4 == 0) {   /* the orient matrix */
        *out = g_scene1_camera_orient[(va - 0x438cdf8u) / 4];
        return 1;
    }
    switch (va) {
    case 0x438cc38u: p = &g_smooth_eye[0]; break;
    case 0x438cc3cu: p = &g_smooth_eye[1]; break;
    case 0x438cc40u: p = &g_smooth_eye[2]; break;
    case 0x438cc50u: p = &g_smooth_lookat[0]; break;
    case 0x438cc54u: p = &g_smooth_lookat[1]; break;
    case 0x438cc58u: p = &g_smooth_lookat[2]; break;
    case 0x6a46f9cu: p = &g_floor_bias; break;
    case 0x438b774u: p = &g_class_off_y; break;
    case 0x438b778u: p = &g_class_off_z; break;
    case 0x438b77cu: p = &g_class_off_x; break;
    case 0x73de31cu: p = &g_scene1_camera_eye[0]; break;
    case 0x73de320u: p = &g_scene1_camera_eye[1]; break;
    case 0x73de324u: p = &g_scene1_camera_eye[2]; break;
    case 0x73de328u: p = &g_scene1_camera_lookat[0]; break;
    case 0x73de32cu: p = &g_scene1_camera_lookat[1]; break;
    case 0x73de330u: p = &g_scene1_camera_lookat[2]; break;
    default: return 0;
    }
    *out = *p;
    return 1;
}

/* ─── init ────────────────────────────────────────────────────────────── */

void scene1_camera_init(void)
{
    g_first_frame = 1;
    g_smooth_eye[0] = g_smooth_eye[1] = g_smooth_eye[2] = 0.0f;
    g_smooth_lookat[0] = g_smooth_lookat[1] = g_smooth_lookat[2] = 0.0f;
    g_floor_bias = 0.0f;
    /* The three compose adds (radius/eye.y/lookat.y) are scene-1 camera
     * CONSTANTS: they have no writer anywhere in the 2620-function decompile
     * and retail holds them at 14/21/-1.8 at every in-scene frame measured
     * (tools/dump_demvp_groundtruth.py).  Their .rdata source triplet is at
     * DAT_005c4fd0 = {-1.8, 14, 21}.  The port previously zeroed them as a
     * "BSS-zero" assumption + re-applied behind --force-walker-phase2; that
     * was a bug (the bracketing eye/lookat collapse without them).  Set them
     * to the faithful values here so every scene-1 camera pose is correct
     * without the flag.  (PHC: the exact rdata→BSS copy site is still
     * unidentified — a pre-scene read crashes, the page is unmapped before a
     * scene allocates it — but the value is confirmed in every scene frame.)
     * bias_x/z_src stay 0 here: those are NOT constants (computed by the
     * FUN_00432e50 placement search, still unported — see the HOUSE-entry
     * stand-in in scene1_camera_apply_house_groundtruth). */
    g_radius_add = 14.0f;   /* _DAT_0695ef70 */
    g_eyey_add   = 21.0f;   /* _DAT_044e2c70 */
    g_looky_add  = -1.8f;   /* _DAT_069b2f78 */
}

/* DAT_068dd3fc[stage · 0x6cf]: the stage record's +0x104 (the camera's
 * scene type: 0 the house, its rooms 1..3; the floors 6 and up) */
static int32_t stage_scene_type(int32_t idx)
{
    return (idx >= 0 && idx < g_stage.count) ? g_stage.records[idx].dungeon_id : 0;
}

int scene1_camera_view_mode(void)
{
    return stage_scene_type(g_stage_b4dc);
}

/* ─── FUN_00441c3e: the camera pose (0x441c3e..0x442207, x87 on the disasm) ─ */

void scene1_camera_pose_compute(void)
{
    static const float k4 = 4.0f, k3 = 3.0f, k0p1 = 0.10000000149011612f, k0p3 = 0.30000001192092896f;
    static const float k5 = 5.0f, km5 = -5.0f, k1 = 1.0f, km3 = -3.0f, k15 = 15.0f, k8p5 = 8.5f;
    static const float km1 = -1.0f, k0p5 = 0.5f, k0p2 = 0.20000000298023224f;
    static const float k_pi2 = 1.5707963705062866f, k180 = 180.0f;
    static const float km1p8 = -1.7999999523162842f, k18 = 18.0f, k11p8 = 11.800000190734863f;
    static const float k19 = 19.0f, k25p8 = 25.799999237060547f, k29 = 29.0f, k33 = 33.0f;

    /* 0x441c47: the char mode (work +0x2ce0c) counts only while the stage's
     * scene type (DAT_068dd3fc[b4dc · 0x6cf]) is 0..4 */
    const int32_t view = stage_scene_type(g_stage_b4dc);
    int32_t mode = 0;
    if (view >= 0 && view <= 4) {
        const uint8_t *w = save_work_bank_at(save_work_active_slot());
        if (w) memcpy(&mode, w + 0x2ce0c, 4);
    }
    g_scene1_camera_char_mode = mode;

    /* 0x441c7c: the class offsets b77c / b774 / b778 */
    const int stage_class = g_scene1_camera_stage_class;   /* DAT_0438b4e8 */
    if (stage_class == 0) {
        if (mode >= 2) {
            g_class_off_x = spill32((long double)km1p8 - DAT_005c4fd0);
            g_class_off_y = spill32((long double)k18 - DAT_005c4fd4);
            g_class_off_z = spill32((long double)k11p8 - DAT_005c4fd8);
        } else {
            g_class_off_x = 0.0f;
            g_class_off_y = 0.0f;
            g_class_off_z = 0.0f;
        }
    }
    if (stage_class == 2) {
        g_class_off_x = spill32((long double)km1p8 - DAT_005c4fd0);
        g_class_off_y = spill32((long double)k19 - DAT_005c4fd4);
        g_class_off_z = spill32((long double)k25p8 - DAT_005c4fd8);
    }
    if (stage_class == 3) {
        g_class_off_x = spill32((long double)km1p8 - DAT_005c4fd0);
        g_class_off_y = spill32((long double)k29 - DAT_005c4fd4);
        g_class_off_z = spill32((long double)k33 - DAT_005c4fd8);
    }

    /* 0x441d4c: the floor bias DAT_06a46f9c follows max(player y − 4, 0, the
     * stage's fall line +0x1b1c) — snapped (+3) on a snap frame, else eased */
    float lo = 0.0f;
    const float py4 = spill32((long double)g_party.pos[0][1] - k4);
    if (0.0f < py4) lo = py4;
    {
        const uint8_t *st = (const uint8_t *)g_stage_palette;
        int32_t fy = 0;
        if (st) memcpy(&fy, st + 0x1b1c, 4);
        const float ffy = spill32((long double)fy);
        if (lo < ffy) lo = ffy;
    }
    if (g_first_frame != 0)
        g_floor_bias = spill32((long double)lo + k3);
    else
        g_floor_bias = spill32((((long double)lo - g_floor_bias) * k0p1 + g_floor_bias) + k0p3);

    float eye_x, eye_y, eye_z, lookat_x, lookat_y, lookat_z;
    if (stage_class == 1) {
        /* 0x441dc5: an event camera — its targets are the smoothed pose
         * itself, the floor bias held at 3 */
        lookat_x = g_smooth_lookat[0]; lookat_y = g_smooth_lookat[1]; lookat_z = g_smooth_lookat[2];
        eye_x = g_smooth_eye[0]; eye_y = g_smooth_eye[1]; eye_z = g_smooth_eye[2];
        g_floor_bias = k3;
    } else {
        /* 0x441e1e: follow the player (5 back in the shop view), halfway to
         * b7a0 / b7a8 with the arena blend (b7ac) */
        float bx = g_party.pos[0][0], bz = g_party.pos[0][2];
        if (mode >= 2) bz = spill32((long double)bz - k5);
        if (g_cam_blend != 0) {
            bx = spill32(((long double)g_party.pos[0][0] - g_cam_blend_x) * k0p5 + g_cam_blend_x);
            bz = spill32(((long double)g_party.pos[0][2] - g_cam_blend_z) * k0p5 + g_cam_blend_z);
        }
        /* 0x441e7e: the room clamps by scene type (even modes only) */
        if ((mode & 1) == 0) {
            switch (view) {
            case 0:
                if (bx > km1) bx = km1;
                if (bx < km5) bx = km5;
                if (bz > k1) bz = k1;
                break;
            case 1:
                if (bx > k5) bx = k5;
                if (bx < km5) bx = km5;
                if (bz > k1) bz = k1;
                if (bz < km3) bz = km3;
                /* fall through */
            case 2:
                if (bx > k15) bx = k15;
                if (bx < km5) bx = km5;
                if (bz > k1) bz = k1;
                if (bz < km3) bz = km3;
                /* fall through */
            case 3:
                if (bx > k15) bx = k15;
                if (bx < km5) bx = km5;
                if (bz > k8p5) bz = k8p5;
                if (bz < km3) bz = km3;
                break;
            default:
                break;
            }
        }
        /* 0x441ff9: the floor-end swing — be94 61..239 eases the radius /
         * height out by 5 · sin((be94 − 60) · π/2 / 180), 5 after */
        float cine = 0.0f;
        if (g_floor_end_timer > 60) {
            if (g_floor_end_timer < 240) {
                const float a = spill32((long double)spill32((long double)(g_floor_end_timer - 60)) * k_pi2 / k180);
                cine = spill32((long double)spill32(x87_crt_sin(a)) * k5);
            } else {
                cine = k5;
            }
        }
        /* 0x442053: the pose */
        lookat_x = bx;
        lookat_y = spill32(((long double)g_class_off_x + g_looky_add) + g_floor_bias);
        lookat_z = bz;
        const double yaw = g_scene1_camera_yaw;
        const float s = spill32(x87_crt_sin(yaw));
        eye_x = spill32((long double)s * (((long double)g_class_off_y + g_radius_add) + cine) + bx);
        eye_y = spill32(((((long double)g_class_off_z + g_eyey_add) + cine) + lookat_y));
        const float c = spill32(x87_crt_cos(yaw));
        eye_z = spill32((long double)bz - (long double)c * (((long double)g_class_off_y + g_radius_add) + cine));
    }

    /* 0x442104: the smoothed pose DAT_0438cc38.. / cc50.. — snapped, else 0.2 */
    if (g_first_frame != 0) {
        g_first_frame = 0;
        g_smooth_eye[0] = eye_x; g_smooth_eye[1] = eye_y; g_smooth_eye[2] = eye_z;
        g_smooth_lookat[0] = lookat_x; g_smooth_lookat[1] = lookat_y; g_smooth_lookat[2] = lookat_z;
    } else {
        g_smooth_eye[0] = spill32(((long double)eye_x - g_smooth_eye[0]) * k0p2 + g_smooth_eye[0]);
        g_smooth_eye[1] = spill32(((long double)eye_y - g_smooth_eye[1]) * k0p2 + g_smooth_eye[1]);
        g_smooth_eye[2] = spill32(((long double)eye_z - g_smooth_eye[2]) * k0p2 + g_smooth_eye[2]);
        g_smooth_lookat[0] = spill32(((long double)lookat_x - g_smooth_lookat[0]) * k0p2 + g_smooth_lookat[0]);
        g_smooth_lookat[1] = spill32(((long double)lookat_y - g_smooth_lookat[1]) * k0p2 + g_smooth_lookat[1]);
        g_smooth_lookat[2] = spill32(((long double)lookat_z - g_smooth_lookat[2]) * k0p2 + g_smooth_lookat[2]);
    }

    /* Block I (L205-L213) — canonical writeback.  Engine adds
     * _DAT_0438cc20 (shake.y, last frame's) to both eye.y and lookat.y. */
    const float shake_y = g_shake_y;
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

    /* Block L (0x442411): the shake — x / z jitter (rand % range − range / 2),
     * y = (u − 0.5) · amp / 2, added to eye.y and lookat.y once more. */
    if (g_shake_ticks > 0) {
        if (--g_shake_ticks == 0) {
            g_shake_dx = 0;
            g_shake_dz = 0;
            g_shake_y = 0.0f;
        } else {
            if (g_shake_xr > 0)
                g_shake_dx = (int32_t)(rng_next15() % (uint32_t)g_shake_xr) - g_shake_xr / 2;
            if (g_shake_zr > 0)
                g_shake_dz = (int32_t)(rng_next15() % (uint32_t)g_shake_zr) - g_shake_zr / 2;
            if (g_shake_amp > 0.0f)
                g_shake_y = spill32(((long double)rng_next_unit() - 0.5f) * g_shake_amp / 2.0f);
        }
    }
    g_scene1_camera_eye[1]    = spill32((long double)g_shake_y + g_scene1_camera_eye[1]);
    g_scene1_camera_lookat[1] = spill32((long double)g_shake_y + g_scene1_camera_lookat[1]);
}

/* FUN_0048dbfb L5-17 (0x48dbfb..0x48dd15): the day end's camera — class 1
 * with the offsets (-1.8, 36, 3.5) less DAT_005c4fd0.., the lookat x / z eased
 * 0.2 toward 0, the eye orbiting it, then FUN_00441c3e */
void scene1_camera_day_end_pose(void)
{
    static const float km1p8 = -1.7999999523162842f, k36 = 36.0f, k3p5 = 3.5f;
    static const float k0p2 = 0.20000000298023224f;
    g_class_off_x = spill32((long double)km1p8 - DAT_005c4fd0);
    g_class_off_y = spill32((long double)k36 - DAT_005c4fd4);
    g_class_off_z = spill32((long double)k3p5 - DAT_005c4fd8);
    g_scene1_camera_stage_class = 1;                       /* DAT_0438b4e8 */
    g_smooth_lookat[0] = spill32(-(long double)g_smooth_lookat[0] * k0p2 + g_smooth_lookat[0]);
    g_smooth_lookat[2] = spill32(-(long double)g_smooth_lookat[2] * k0p2 + g_smooth_lookat[2]);
    const double yaw = g_scene1_camera_yaw;
    const float s = spill32(x87_crt_sin(yaw));
    g_smooth_eye[0] = spill32((long double)s * ((long double)g_class_off_y + g_radius_add) + g_smooth_lookat[0]);
    g_smooth_eye[1] = spill32(((long double)g_class_off_z + g_eyey_add) + g_smooth_lookat[1]);
    const float c = spill32(x87_crt_cos(yaw));
    g_smooth_eye[2] = spill32((long double)g_smooth_lookat[2] - (long double)c * ((long double)g_class_off_y + g_radius_add));
    scene1_camera_pose_compute();                          /* FUN_00441c3e */
}

/* ─── FUN_00462403 cinematic block: the cc08==4 customer-service COUNTER camera ─
 *
 * (all.c:60280-60314.)  During customer service the engine DECOUPLES the camera
 * from the player and pins the lookat to a fixed per-shop-tier counter target
 * (NOT the player position — that's why the player can ramp to px=-4.5 while the
 * camera holds the counter framing), orbits the eye by the same HOUSE radius the
 * free-roam camera uses, and flags stage_class=1 so scene1_camera_pose_compute
 * passes these values straight through.  Called from customer_service's master
 * tick each cc08==4 frame.  Verified vs retail (house-customer-tutorial-a361c768):
 * tier-0 lookat smooths to (-3.0, 0.0), eye to (-3.0, 14.0) at the settled view. */
void scene1_camera_cs_counter_cam(int shop_tier)
{
    /* lookat target by shop tier (engine local_8 = lookat X, local_c = lookat Z). */
    float look_x_t = -3.0f, look_z_t = 0.0f;          /* tier 0 / default */
    if (shop_tier == 1)      { look_x_t = 0.0f; look_z_t =  0.0f; }
    else if (shop_tier == 2) { look_x_t = 5.5f; look_z_t = -1.0f; } /* +b778 ramp→25: PORT-DEBT(cs-cam-tier) */
    else if (shop_tier == 3) { look_x_t = 5.5f; look_z_t =  5.5f; } /* +b778 ramp→29: PORT-DEBT(cs-cam-tier) */

    /* smooth lookat x/z toward the target at 0.1/frame (engine 0.1 lerp on
     * _DAT_0438cc50/58).  lookat y (_DAT_0438cc54) is left frozen at its last
     * free-roam value, exactly as the engine does. */
    g_smooth_lookat[0] = (look_x_t - g_smooth_lookat[0]) * 0.1f + g_smooth_lookat[0];
    g_smooth_lookat[2] = (look_z_t - g_smooth_lookat[2]) * 0.1f + g_smooth_lookat[2];

    g_scene1_camera_stage_class = 1;                  /* DAT_0438b4e8 = 1 */

    /* eye = lookat + the HOUSE orbital offset (radius = class_off_y + radius_add,
     * the same params the free-roam camera uses; yaw = the camera yaw).  These
     * read the previous frame's class offsets — stable in cc08==4 (char_mode 0). */
    float yaw    = g_scene1_camera_yaw;
    float radius = g_class_off_y + g_radius_add;
    g_smooth_eye[0] = radius * sinf(yaw) + g_smooth_lookat[0];
    g_smooth_eye[1] = g_class_off_z + g_eyey_add + g_smooth_lookat[1];
    g_smooth_eye[2] = g_smooth_lookat[2] - radius * cosf(yaw);
}

/* An event camera (dungeon 0 floor 4's intro, FUN_0044c9ba): the smoothed eye
 * DAT_0438cc38..40 and lookat DAT_0438cc50..58 written directly */
void scene1_camera_set_targets(const float eye[3], const float look[3])
{
    for (int k = 0; k < 3; k++) {
        g_smooth_eye[k] = eye[k];
        g_smooth_lookat[k] = look[k];
    }
}

void scene1_camera_set_eye(const float eye[3])
{
    for (int k = 0; k < 3; k++) g_smooth_eye[k] = eye[k];
}

void scene1_camera_set_lookat(const float look[3])
{
    for (int k = 0; k < 3; k++) g_smooth_lookat[k] = look[k];
}

/* Reset to the free-roam player-follow camera (stage_class 0) — called from the
 * cc08==1 free-roam arm so the camera resumes tracking after customer service. */
void scene1_camera_set_freeroam_class(void)
{
    g_scene1_camera_stage_class = 0;
}

/* ─── FUN_004424e7 angle / orientation helper ─────────────────────────── */

void scene1_camera_angle_compute(void)
{
    /* 0x4424ed..0x4425f1 (x87): yaw = atan2(dx, dz) + π, the horizontal
     * distance, the pitch atan2(dist, dy); orient = RotX(π/2 − pitch) ×
     * RotY(yaw) (0x4a3553 / 0x4a35ef / 0x4a2a10).  A zero distance makes the
     * first matrix the identity (FUN_00404bb8) — the RotY and the multiply
     * still run. */
    static const float k_pi = 3.1415927410125732f, k_pi2 = 1.5707963705062866f;
    static const float k0p01 = 0.009999999776482582f;
    const float dx = spill32((long double)g_scene1_camera_eye[0] - g_scene1_camera_lookat[0]);
    float dz = spill32((long double)g_scene1_camera_eye[2] - g_scene1_camera_lookat[2]);
    if (dx == 0.0f && dz == 0.0f) dz = k0p01;
    const float angle_y = spill32((long double)spill32(x87_crt_atan2(dx, dz)) + k_pi);
    const float dist = spill32(spill32(sqrtl((long double)spill64((long double)dz * dz + (long double)dx * dx))));
    float m_x[16];
    float m_y[16];
    if (dist != 0.0f) {
        const float dy = spill32((long double)g_scene1_camera_eye[1] - g_scene1_camera_lookat[1]);
        const float pitch = spill32(x87_crt_atan2(dist, dy));
        mat4_rotation_x(m_x, spill32((long double)k_pi2 - pitch));
    } else {
        mat4_identity(m_x);
    }
    mat4_rotation_y(m_y, angle_y);
    mat4_mul(g_scene1_camera_orient, m_x, m_y);

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
