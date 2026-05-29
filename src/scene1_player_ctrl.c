/*
 * scene1_player_ctrl.c — Cpop.1: HOUSE per-frame player controller (leaf math).
 * See scene1_player_ctrl.h for the chip writeup.  Engine FUN_0048b850.
 */

#include "scene1_player_ctrl.h"

#include <math.h>

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
