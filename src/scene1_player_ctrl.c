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
