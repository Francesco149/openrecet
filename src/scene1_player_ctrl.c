/*
 * scene1_player_ctrl.c — Cpop.1: HOUSE per-frame player controller (leaf math).
 * See scene1_player_ctrl.h for the chip writeup.  Engine FUN_0048b850.
 */

#include "scene1_player_ctrl.h"

#include <math.h>
#include <string.h>

#include "scene1_chr_sprite.h"   /* CHR_ACTOR_* record-field indices */

/* ── engine float constants (FUN_0048b850 .rdata, decoded 2026-05-30) ──
 *   0x519900 = 0.03   0x519360 = 2.0 (the -2.0 clamp = fchs of 0x...)   */
#define PC_CAM_Z_DECAY   0.03f
#define PC_CAM_Z_FLOOR   (-2.0f)

int player_ctrl_facing_snap(int octant, int *sticky)
{
    octant &= 7;

    /* Pure-horizontal octants set the sticky bias; pure-vertical clear it.
     * The diagonals (1/3/5/7) leave it as-is — that persistence is the
     * whole point of DAT_056dae3c. */
    if (octant == 2 || octant == 6)
        *sticky = 1;
    if (octant == 0 || octant == 4)
        *sticky = 0;

    if (*sticky == 0) {              /* engine: DAT_056dae3c == 0 branch */
        if (octant == 1) octant = 0;
        if (octant == 7) octant = 0;
        if (octant == 3) octant = 4;
        if (octant == 5) octant = 4;
    } else {                         /* engine: else branch */
        if (octant == 1) octant = 2;
        if (octant == 7) octant = 6;
        if (octant == 3) octant = 2;
        if (octant == 5) octant = 6;
    }
    return octant;                   /* 2 and 6 fall through unchanged */
}

float player_ctrl_camera_z_decay(float z)
{
    z -= PC_CAM_Z_DECAY;
    if (z < PC_CAM_Z_FLOOR)
        z = PC_CAM_Z_FLOOR;
    return z;
}

void player_ctrl_camera_shake_clamp(float *shake_x, float *shake_y,
                                    float target)
{
    /* engine: local_10 = sqrt(daac4*daac4 + daabc*daabc) via FUN_005031e4 */
    float mag = sqrtf((*shake_x) * (*shake_x) + (*shake_y) * (*shake_y));
    if (target <= mag) {             /* engine: if (local_8 <= local_c) */
        *shake_x = (*shake_x * target) / mag;  /* multiply-then-divide */
        *shake_y = (*shake_y * target) / mag;
    }
}

void player_ctrl_pulse_counters(int *down, int *phase, int *level)
{
    if (*down > 0)                   /* DAT_056db00c */
        (*down)--;

    if (*phase > 0) {                /* DAT_056db008 */
        (*phase)++;                  /* engine pre-increments, then compares */
        if (*phase < 0x1e) {         /* ramp the level up while in the first half */
            if (*level < 10)         /* DAT_056db000, capped at 10 */
                (*level)++;
        } else {                     /* ramp it back down in the second half */
            if (*level > 0)
                (*level)--;
        }
        if (*phase > 0x3c)           /* wrap the 0..60 phase timer */
            *phase = 0;
    }
}

void player_ctrl_trail_orbit_pos(int anim_idx, float stored_angle,
                                 float table_val, const float player[3],
                                 float out[3])
{
    /* angle = 2*table[idx] + stored (engine: fld table; fadd st,st; fadd +0x3c) */
    float angle = table_val + table_val + stored_angle;
    float r = (float)anim_idx + 3.0f;             /* 0x519438 = 3.0 */

    out[0] = sinf(angle) * r + player[0];         /* x — FUN_00503a44 = sin */
    out[1] = player[1];                           /* y — copied straight (da1dc) */
    out[2] = cosf(angle) * r + player[2];         /* z — FUN_00503994 = cos */
}

void player_ctrl_history_shift(float pos_hist[][3],
                               int32_t rec_hist[][PC_ACTOR_REC_DWORDS],
                               const float cur_pos[3],
                               const int32_t cur_rec[PC_ACTOR_REC_DWORDS])
{
    /* Shift every slot one place toward "older" (slot[i] = slot[i-1]),
     * matching the engine's high→low pointer walk; then slot 0 takes the
     * live sample.  The engine moves position (3 dwords) and the
     * sprite-state record (11 dwords) in lockstep per slot. */
    for (int i = PC_HIST_SLOTS - 1; i > 0; i--) {
        pos_hist[i][0] = pos_hist[i - 1][0];
        pos_hist[i][1] = pos_hist[i - 1][1];
        pos_hist[i][2] = pos_hist[i - 1][2];
        memcpy(rec_hist[i], rec_hist[i - 1], sizeof rec_hist[i]);
    }
    pos_hist[0][0] = cur_pos[0];
    pos_hist[0][1] = cur_pos[1];
    pos_hist[0][2] = cur_pos[2];
    memcpy(rec_hist[0], cur_rec, sizeof rec_hist[0]);
}

/* ── Cchr.2h: the player/companion actor-state model ─────────────────────
 *
 * The per-actor fields the shop-walker player draw (FUN_004552d0 L357-454,
 * the loop over actor i = 0=player, 1/2=companion) reads each frame:
 *
 *   char id  (&DAT_056da1cc)[i]      — gate: != -1 (slot in use)
 *   XZ scale (&DAT_056dae18)[i]      — gate: > 0
 *   Y  scale (&DAT_056dae24)[i]      — gate: > 0
 *   record   (&DAT_056daae8)[i*0xb]  — 11-dword sprite-state the leaf samples
 *
 * Actor *position* is the separate DAT_056da1d8 array (actor 0 ==
 * g_scene1_player_pos); the draw side combines it with these fields.
 *
 * FUN_0048b850 (the per-frame player controller; Cpop) is these globals'
 * live writer — the ring shift at 48b850.c:483-584 fills DAT_056daae8 from
 * the live input/anim state and the spawn fade-in routine settles dae18/
 * dae24 to 1.0.  Until that body lands, player_ctrl_pose_house_standing()
 * seeds actor 0 from the runs/cchr2b retail leaf ground truth (HOUSE frame
 * 17544): char 0, scale 1.0/1.0, record anim 0 / timer 5.0f / counter 25 /
 * frame 2 / facing 6 (the idle standing pose; renders at color 0xff808080).
 * This replaces the per-call scene1_shop_walker_set_player_inject MVP. */

static int32_t s_actor_char[PC_NUM_ACTORS]     = { -1, -1, -1 };
static float   s_actor_scale_xz[PC_NUM_ACTORS] = { 0.0f, 0.0f, 0.0f };
static float   s_actor_scale_y[PC_NUM_ACTORS]  = { 0.0f, 0.0f, 0.0f };
static int32_t s_actor_record[PC_NUM_ACTORS][PC_ACTOR_REC_DWORDS];

void player_ctrl_pose_house_standing(int player_char)
{
    for (int i = 0; i < PC_NUM_ACTORS; i++) {
        s_actor_char[i]     = -1;
        s_actor_scale_xz[i] = 0.0f;
        s_actor_scale_y[i]  = 0.0f;
        memset(s_actor_record[i], 0, sizeof s_actor_record[i]);
    }

    /* actor 0 = the standing player (companion slots 1/2 await the
     * DAT_056da1d0 char ids + DAT_056da1e4.. position port). */
    s_actor_char[0]     = player_char;
    s_actor_scale_xz[0] = 1.0f;   /* DAT_056dae18[0] — settled spawn scale */
    s_actor_scale_y[0]  = 1.0f;   /* DAT_056dae24[0] */

    /* DAT_056daae8[0] idle pose.  TIMER is float bits (5.0f); the rest int. */
    union { float f; int32_t i; } timer; timer.f = 5.0f;
    s_actor_record[0][CHR_ACTOR_ANIM]    = 0;
    s_actor_record[0][CHR_ACTOR_TIMER]   = timer.i;
    s_actor_record[0][CHR_ACTOR_COUNTER] = 25;
    s_actor_record[0][CHR_ACTOR_FRAME]   = 2;
    s_actor_record[0][CHR_ACTOR_FACING]  = 6;
}

int player_ctrl_actor_char(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_char[i] : -1;
}

float player_ctrl_actor_scale_xz(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_scale_xz[i] : 0.0f;
}

float player_ctrl_actor_scale_y(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_scale_y[i] : 0.0f;
}

const int32_t *player_ctrl_actor_record(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_record[i] : NULL;
}
