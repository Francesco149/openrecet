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

float player_ctrl_shake_damp_factor(int mode_nonzero, int grounded,
                                    int flag_6ca, int held_96b,
                                    int edge_9, int db100, int db048)
{
    if (mode_nonzero)                       /* DAT_056da1bc != 0 */
        return 0.97f;
    if (!grounded)                          /* da1dc != daf88 */
        return 0.99f;

    /* grounded: continue only if (flag_6ca==0 || held); else damp 0.95. */
    if (flag_6ca != 0 && !held_96b)
        return 0.95f;

    /* reach the db048 state block only if this gate holds; else 0.998. */
    if (!(((!edge_9) && db100 < 1) || held_96b))
        return 0.998f;

    /* DAT_056db048 state block (engine L90165-90178). */
    if (db048 != 2) {
        if (db048 == 3)
            return 0.95f;
        if (!grounded)                      /* unreachable here in-engine; faithful */
            return 0.98f;
    }
    return 0.82f;                           /* db048==2, or grounded fall-through */
}

float player_ctrl_shake_target(float base, int held_968, int held_969,
                               int boost, int b8b0_is_neg1, float db074,
                               int dae9c_active, int daeac,
                               int db048, int da1cc,
                               int daed8_is_1, int db07c_is_0,
                               float daedc, float da1dc)
{
    float t = base;

    if (held_968) t += 0.02f;        /* faddl 0x5199e8 — double in binary */
    if (held_969) t += 0.08f;        /* faddl 0x519fb0 */
    if (boost)    t *= 1.3f;         /* fmuls 0x519b90 */
    if (b8b0_is_neg1) t += db074;    /* DAT_0438b8b0 == -1 → += _DAT_056db074 */

    if (dae9c_active) {              /* DAT_056dae9c != 0 */
        if (daeac & 2)        t += 0.06f;
        else if (daeac & 1)   t += 0.03f;
    }

    if (db048 == 1)                  /* state override (later wins) */
        t = 0.5f;
    if (db048 == 4 || db048 == 5)
        t = (da1cc == 0x29) ? 1.0f : 0.5f;

    if (daed8_is_1 && db07c_is_0) {  /* §56: DAT_056daed8 == 1 is an INT test */
        t = daedc - da1dc;
        if (t > 1.0f) t = 1.0f;      /* clamp to [0,1] */
        if (t < 0.0f) t = 0.0f;
        t = 0.3f - t * 0.1f;         /* proximity-ease toward the target */
    }
    return t;
}

void player_ctrl_trail_advance(int32_t records[][PC_TRAIL_REC_DWORDS],
                               const int32_t sprite_ring[PC_ACTOR_REC_DWORDS],
                               const float player[3],
                               const float angle_table[],
                               int decay_spawn,
                               pc_trail_events *ev)
{
    if (ev) { ev->alloc_count = 0; ev->spawn_count = 0; }

    for (int i = 0; i < PC_TRAIL_RECORDS; i++) {
        int32_t *rec = records[i];

        /* [ebx-0x8] compared signed against 0 (`jle`): skip dead records. */
        if (rec[PC_TRAIL_COUNTDOWN] <= 0)
            continue;

        /* The DAT_056dae14-decay alloc spawn fires per active record, before
         * the sprite copy, when the caller's decay edge fired this frame. */
        if (decay_spawn && ev)
            ev->alloc_index[ev->alloc_count++] = i;

        /* Snapshot the live sprite-state ring head into this record's slot. */
        memcpy(&rec[PC_TRAIL_SPRITE], sprite_ring,
               PC_ACTOR_REC_DWORDS * sizeof rec[0]);

        /* Orbit position: angle = 2·table[idx] + stored_angle, r = idx+3.
         * x/y/z are stored as raw float bits (engine `fstp DWORD`). */
        int   idx = rec[PC_TRAIL_IDX];
        float stored_angle;
        memcpy(&stored_angle, &rec[PC_TRAIL_ANGLE], sizeof stored_angle);

        float out[3];
        player_ctrl_trail_orbit_pos(idx, stored_angle, angle_table[idx],
                                    player, out);
        memcpy(&rec[PC_TRAIL_X], &out[0], sizeof out[0]);
        memcpy(&rec[PC_TRAIL_Y], &out[1], sizeof out[1]);
        memcpy(&rec[PC_TRAIL_Z], &out[2], sizeof out[2]);

        /* Spawn-at-birth: the frame the life counter is exactly 600. */
        if (rec[PC_TRAIL_COUNTDOWN] == 600 && ev) {
            ev->spawn_pos[ev->spawn_count][0] = out[0];
            ev->spawn_pos[ev->spawn_count][1] = out[1];
            ev->spawn_pos[ev->spawn_count][2] = out[2];
            ev->spawn_count++;
        }

        rec[PC_TRAIL_COUNTDOWN]--;
    }
}

int player_ctrl_burst_materialize(int32_t bank[][PC_TRAIL_REC_DWORDS],
                                  const float pos_hist[][3],
                                  const int32_t rec_hist[][PC_ACTOR_REC_DWORDS],
                                  int counter)
{
    /* engine: `if (0 < DAT_056daae0)` — nothing happens below the burst. */
    if (counter <= 0)
        return counter;

    for (int k = 0; k < PC_BURST_RECORDS; k++) {
        int s = 3 + 2 * k;                  /* source slot: 3, 5, 7, 9, 11 */
        int32_t *rec = bank[k];

        /* sprite-state ← history record slot (engine `rep movs`, 11 dwords). */
        memcpy(&rec[PC_TRAIL_SPRITE], rec_hist[s],
               PC_ACTOR_REC_DWORDS * sizeof rec[0]);

        /* position ← full history position slot (x/y/z as raw float bits). */
        memcpy(&rec[PC_TRAIL_X], &pos_hist[s][0], sizeof pos_hist[s][0]);
        memcpy(&rec[PC_TRAIL_Y], &pos_hist[s][1], sizeof pos_hist[s][1]);
        memcpy(&rec[PC_TRAIL_Z], &pos_hist[s][2], sizeof pos_hist[s][2]);

        rec[PC_TRAIL_COUNTDOWN] = 0x14;     /* life, engine `[eax+0xc] = 0x14` */
    }

    counter--;
    if (counter == 0) {
        /* engine L90293-90297: zero each record's life field, nothing else. */
        for (int k = 0; k < PC_BURST_RECORDS; k++)
            bank[k][PC_TRAIL_COUNTDOWN] = 0;
    }
    return counter;
}

/* Cpop.8: the engine's gauge-rate derivation (FUN_0048b6ad, .rdata 0x5193a4
 * = 0.01f).  Both fields are sign-extended 16-bit then summed — the order is
 * immaterial, so callers may pass them in either order. */
float player_ctrl_gauge_rate(int16_t field_lo, int16_t field_hi)
{
    return ((float)((int)field_lo + (int)field_hi)) * 0.01f;
}

void player_ctrl_gauge_track(float *disp_hp, float true_hp, float hp_rate,
                             float *disp_sp, float true_sp, float sp_rate,
                             int *counter, int *dir)
{
    /* Channel A — HP gauge (DAT_056db0c4 toward DAT_056db0bc).  Manages the
     * run-length counter (db0cc) and direction flag (db0d0).  The engine
     * branches `jae` then `jbe` so the equal case resets the counter and
     * leaves the direction untouched (objdump 0x48b6bc-0x48b799). */
    if (*disp_hp < true_hp) {              /* below target → rise toward it */
        *disp_hp += hp_rate;
        if (*disp_hp > true_hp) *disp_hp = true_hp;   /* clamp overshoot */
        (*counter)++;
        *dir = 1;
    } else if (*disp_hp > true_hp) {       /* above target → fall toward it */
        *disp_hp -= hp_rate;
        if (*disp_hp < true_hp) *disp_hp = true_hp;
        (*counter)++;
        *dir = 0;
    } else {                               /* already settled → reset counter */
        *counter = 0;
    }

    /* Channel B — SP gauge (DAT_056db0c8 toward DAT_056db0c0).  Clamp-on-
     * overshoot only; no counter or direction (objdump 0x48b7a0-0x48b842). */
    if (*disp_sp < true_sp) {
        *disp_sp += sp_rate;
        if (*disp_sp > true_sp) *disp_sp = true_sp;
    } else if (*disp_sp > true_sp) {
        *disp_sp -= sp_rate;
        if (*disp_sp < true_sp) *disp_sp = true_sp;
    }
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
