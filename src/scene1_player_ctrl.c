/*
 * scene1_player_ctrl.c — Cpop.1: HOUSE per-frame player controller (leaf math).
 * See scene1_player_ctrl.h for the chip writeup.  Engine FUN_0048b850.
 */

#include "scene1_player_ctrl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "call_trace.h"          /* CALL_TRACE_ENTER / _STUB */
#include "scene1_chr_sprite.h"   /* CHR_ACTOR_* record-field indices, chr_anim_tick */
#include "scene1_particles_tick.h" /* g_scene1_player_pos[3] (DAT_056da1d8/dc/e0) */
#include "input.h"               /* g_input_state[].buttons (held d-pad mask) */
#include "collision_house.h"     /* collision_house_get — live room mesh */
#include "collision_resolve.h"   /* collision_resolve_player (FUN_00483170) */
#include "stage_palette.h"       /* g_stage_palette->mode (*DAT_068dd2f0) */
#include "title_save_dialog.h"   /* title_save_dialog_gate_tick (FUN_00434d6a guard) */
#include "scene1_spawn.h"        /* scene1_spawn (FUN_00447f4f) — foot-dust emit */
#include "scene1_companion_ctrl.h" /* scene1_companion_db054 (shared DAT_056db054) */
#include "rng.h"                 /* rng_next_unit (FUN_00471089) — dust jitter */
#include "scene1_bg_npc.h"       /* scene1_bg_npc_tick (FUN_0046f621 NPC pump) */
#include "scene1_intro_dialogue.h" /* prologue gate — suppress the walk arm */
#include "scene1_overlay.h"      /* scene1_overlay_spawn (FUN_00414345) — sparkle */
#include "save_work.h"           /* working-arena display grid (dword 0x4e26) */
#include "save_bank.h"           /* SAVE_BANK_FIELD_DISPLAY_GRID */
#include "sim.h"                 /* g_sim_frame_count (DAT_0438b8cc) */
#include "scene1_combat_sm.h"    /* g_scene1_combat_dat_056da1b8 (sparkle owner) */

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

/* ── W3 free-roam walk state (the live player-controller globals) ──────────
 *   s_player_vel    = (daabc, daac0, daac4)  — world velocity (x, y, z)
 *   s_player_facing = db05c                  — world facing angle (radians)
 *   s_facing_sticky = dae3c                  — diagonal-snap horizontal bias
 * Reset to the idle standing pose by player_ctrl_pose_house_standing(): the
 * idle capture shows facing +π/2 (oct 6), zero velocity. */
static float s_player_vel[3]    = { 0.0f, 0.0f, 0.0f };
static float s_player_facing    = 1.57079633f;   /* +π/2, idle facing */
static int   s_facing_sticky    = 0;
static int   s_player_moving    = 0;              /* last frame's moving state
                                                   * (held across opposing-pair
                                                   * d-pad frames, §69) */

/* ── FUN_0048670f cc08 dispatch state (engine-quirks §78, Chip 4) ───────────
 *   s_cc08 = DAT_0438cc08 — the in-game interaction state the controller
 *            dispatches on each frame.  1 = free-roam walk; the other values
 *            are the event / camera / counter / menu / dialogue arms (see the
 *            state table in plans/house-controller-unmvp.md).
 *   s_cc04 = DAT_0438cc04 — the free-roam interaction sub-state.  0 = walking;
 *            1/2 = an object/customer interaction is active (cc08==1 arm).
 * The port sets cc08 to 1 at HOUSE entry (player_ctrl_cc08_enter_freeroam =
 * FUN_004850ec) and leaves cc04 at 0; the transitions that would drive them off
 * free-roam (customer approach → cc08=4, counter open → cc08=0x32, talk → cc04)
 * are unported features, so both stay at their free-roam values in steady play.
 * That keeps the dispatch a real read of live state (not the old unconditional
 * shell) while the off-path arms remain honest stubs. */
static int   s_cc08             = 1;              /* DAT_0438cc08 */
static int   s_cc04             = 0;              /* DAT_0438cc04 */

/* ── FUN_0048b850 tail render-bank state (Chip 2; engine all.c L90242+) ─────
 * The two after-image banks the chr-sprite walker (FUN_00456f56) draws —
 *   s_trail_bank = DAT_056dab6c (walker sweep 0, always read)
 *   s_burst_bank = DAT_056dacc0 (walker sweep 1, daae0-gated)
 * — and the two 40-slot motion-history rings that feed the burst:
 *   s_pos_hist = DAT_056da1fc (3-float pos),  s_rec_hist = DAT_056da3dc
 *   (the 11-dword sprite-state record).  FUN_0048b850's tail is the engine's
 *   live writer of all four; this module owns them (the b850 state lives here)
 *   and scene1_chr_walker.c reads the banks through player_ctrl_render_bank_
 *   slot() / player_ctrl_burst_count().  Each bank slot is the PC_TRAIL_*
 *   0x44-byte record (sprite[0..10], x/y/z at 0xb..0xd, life/age at 0xe — the
 *   walker's active gate), so the layout matches what the draw side reads.
 *
 * DORMANT in HOUSE free-roam: nothing lights the banks today — no dash spawns
 * a trail record (their countdowns stay 0; the FUN_0044376a dash-alloc path is
 * a later b850 sub-chip) and the burst counter DAT_056daae0 stays 0.  So every
 * frame the ring shift runs (real work) but both bank fills find nothing to do
 * and the walker draws no after-images, matching retail's plain walk.  This is
 * the faithful replacement for the synthetic single-slot inject that
 * scene1_chr_walker.c previously hand-built. */
static float   s_pos_hist[PC_HIST_SLOTS][3];
static int32_t s_rec_hist[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS];
static int32_t s_trail_bank[PC_TRAIL_RECORDS][PC_TRAIL_REC_DWORDS];
static int32_t s_burst_bank[PC_BURST_RECORDS][PC_TRAIL_REC_DWORDS];
static int     s_burst_count = 0;   /* DAT_056daae0 (burst/egg after-image counter) */
static int     s_decay_spawn = 0;   /* DAT_056dae14 down-counter (trail alloc edge) */

void player_ctrl_pose_house_standing(int player_char)
{
    for (int i = 0; i < PC_NUM_ACTORS; i++) {
        s_actor_char[i]     = -1;
        s_actor_scale_xz[i] = 0.0f;
        s_actor_scale_y[i]  = 0.0f;
        memset(s_actor_record[i], 0, sizeof s_actor_record[i]);
    }

    /* W3: reset the live walk state to the idle standing pose. */
    s_player_vel[0] = s_player_vel[1] = s_player_vel[2] = 0.0f;
    s_player_facing = 1.57079633f;   /* +π/2 (idle facing, oct 6) */
    s_facing_sticky = 0;
    s_player_moving = 0;

    /* Enter free-roam: cc08 = 1 (via the engine's FUN_004850ec), cc04 = 0
     * (walking, no interaction).  This is the HOUSE-entry state the dispatch in
     * scene1_player_ctrl_tick reads each frame (§78). */
    player_ctrl_cc08_enter_freeroam();
    s_cc04 = 0;

    /* Clear the b850-tail render banks + history rings (scene entry empties the
     * after-image state; the rings refill from the live sample each frame). */
    memset(s_pos_hist,   0, sizeof s_pos_hist);
    memset(s_rec_hist,   0, sizeof s_rec_hist);
    memset(s_trail_bank, 0, sizeof s_trail_bank);
    memset(s_burst_bank, 0, sizeof s_burst_bank);
    s_burst_count = 0;
    s_decay_spawn = 0;

    /* actor 0 = the standing player. */
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

    /* actor 2 = the bobbing fairy companion (char id 1 = DAT_056da1d4).  At
     * HOUSE free-roam entry FUN_00435c98 sets (da1cc,da1d0,da1d4) = (0,3,1),
     * then FUN_00436f97 DISABLES actor 1 (da1d0 → -1) and settles all live
     * actor scales to 1.0 with record FACING = 4 — so actor 1 stays char -1
     * (left as the loop default above; never renders) while actor 2 is the
     * live companion.  Validated against the retail capture (runs/companion-
     * truth/FINDINGS.md, engine-quirks §71).  scene1_companion_ctrl_tick is the
     * live writer of its position + anim/facing from here on. */
    s_actor_char[2]     = 1;
    s_actor_scale_xz[2] = 1.0f;
    s_actor_scale_y[2]  = 1.0f;
    s_actor_record[2][CHR_ACTOR_FACING] = 4;   /* FUN_00436f97 scene-entry default */

    /* The companion's sprite anim is left at its zero-init ANIM-START (ANIM 0,
     * FRAME 0, TIMER 0, COUNTER 0) and ADVANCED every frame by
     * scene1_companion_ctrl_tick's chr_anim_tick — Tear's idle wings FLAP in a
     * 4-frame loop, and the wing-glow leaf reads the live FRAME (frames 0/1 =
     * spread, 2 = folded, 3 = mid; ground truth runs/comp-anim-probe).
     * An earlier attempt to seed a fixed pose here (FRAME 2, matching retail's
     * mid-anim chr_leaf snapshot at frame 17544) froze the wings on one phase
     * and diverged from retail's animated wings — the fix is to animate, not to
     * pick a frame.  See docs/findings/scene1-wing-glow.md, engine-quirks §81. */

    /* Companion position (DAT_056da1f0/f4/f8 = g_scene1_actor_pos[2], aliased by
     * g_scene1_spawn_origin).  Seed to the retail controllable-onset value
     * (0.6, 3.0, 9.35) beside the player spawn; the controller's 0.1 lerp
     * settles any residual within ~1 s. */
    g_scene1_actor_pos[2][0] = 0.6f;
    g_scene1_actor_pos[2][1] = 3.0f;
    g_scene1_actor_pos[2][2] = 9.35f;
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

int32_t *player_ctrl_actor_record_mut(int i)
{
    return (i >= 0 && i < PC_NUM_ACTORS) ? s_actor_record[i] : NULL;
}

/* Trace-harness phase normalization (the segtrace `{phasepin}` op) for the
 * PLAYER (actor 0).  The companion side resets its own cycle in
 * scene1_companion_ctrl_phasepin; this is the player twin.  Recette's idle
 * breathing / walk anim cycle (FRAME/TIMER/COUNTER) free-runs from a seed laid
 * at scene entry, so at free-roam onset its phase ORIGIN depends on how long the
 * (non-deterministic) pre-free-roam load/dialogue ran — the same load-dependent
 * origin the companion has (engine-quirks §94).  While WALKING the idle↔walk
 * transition reset re-origins the cycle so a walk trace is already phase-clean
 * without this; a pure-IDLE comparison (e.g. house-movement cap_00) is NOT, and
 * needs this to normalize the origin.  Zeroes FRAME/TIMER/COUNTER (not ANIM/
 * FACING — those are state, not phase) so a retail run pinned the same way is
 * phase-clean.  Trace/comparison ONLY: the shipped game keeps the engine-
 * faithful free-running cycle (retail's own origin is equally load-dependent —
 * we normalize it in the TAS harness, NOT by re-seeding the shipped game). */
void player_ctrl_phasepin(void)
{
    union { float f; int32_t i; } z = { .f = 0.0f };
    s_actor_record[0][CHR_ACTOR_FRAME]   = 0;
    s_actor_record[0][CHR_ACTOR_TIMER]   = z.i;
    s_actor_record[0][CHR_ACTOR_COUNTER] = 0;
}

/* This frame's player walk-intent (s_player_moving = the d-pad-derived moving
 * state set by scene1_player_ctrl_tick, §69).  Read by the §95 dev-overlay
 * RNG-consume gate in scene1_sim.c — the overlay's LCG step fires every render
 * frame ONLY while the player is moving (engine-quirks §95). */
int player_ctrl_is_moving(void)
{
    return s_player_moving;
}

/* Render banks the chr-sprite walker (FUN_00456f56) reads each frame: sweep 0 =
 * the dash-trail bank (DAT_056dab6c), sweep 1 = the burst bank (DAT_056dacc0).
 * Each slot is a PC_TRAIL_REC_DWORDS (0x44-byte) record — sprite-state[0..10],
 * pos x/y/z at [0xb..0xd], life/age at [0xe] (the walker's > 0 active gate).
 * Returns NULL for an out-of-range sweep/idx. */
const int32_t *player_ctrl_render_bank_slot(int sweep, int idx)
{
    if (idx < 0)
        return NULL;
    if (sweep == 0 && idx < PC_TRAIL_RECORDS)
        return s_trail_bank[idx];
    if (sweep == 1 && idx < PC_BURST_RECORDS)
        return s_burst_bank[idx];
    return NULL;
}

/* The burst-bank sweep gate DAT_056daae0 (the egg/spell after-image counter the
 * walker's sweep-1 loop is conditioned on). */
int player_ctrl_burst_count(void)
{
    return s_burst_count;
}

/* Debug accessor for the per-frame controller state (W4.7 facing analysis):
 * the post-tick world velocity (daabc/daac4) and the stored facing db05c.
 * (engine-quirks §69). NULL-safe; any out param may be NULL. */
void player_ctrl_debug_state(float *vx, float *vz, float *facing, int *sticky)
{
    if (vx)     *vx     = s_player_vel[0];
    if (vz)     *vz     = s_player_vel[2];
    if (facing) *facing = s_player_facing;
    if (sticky) *sticky = s_facing_sticky;
}

/* Set the stored world facing db05c (the conversation-pose driver's ±π/2 write,
 * FUN_0048407f; scene1_conversation_pose.c).  Held until the next walk frame. */
void player_ctrl_set_facing_angle(float angle)
{
    s_player_facing = angle;
}

/* ── FUN_0048670f cc08 dispatch state writer + accessors (Chip 4) ──────────── */

/* FUN_004850ec (0x4850ec, 18 B): the canonical "enter HOUSE free-roam" setter.
 * The engine body is `DAT_074b2ec4 = 0; DAT_0438cc08 = 1;` — it clears the
 * scene-exit latch then sets the in-game state id to free-roam.  The port models
 * the cc08 write (the observable state transition); the DAT_074b2ec4 latch is
 * the unported scene-exit subsystem (read only at the dispatch's all.c:344
 * `DAT_074b2ec4 == 1` arm, itself unported), so its reset is omitted — _STUB
 * marks the body as not-yet-complete end-to-end (cf. mark-stubbed-ports).  The
 * engine calls this on every path that hands control back to the player (scene
 * entry, dialogue/menu close); the port calls it from
 * player_ctrl_pose_house_standing at HOUSE entry. */
void player_ctrl_cc08_enter_freeroam(void)
{
    CALL_TRACE_ENTER_STUB(0x4850ecu);
    s_cc08 = 1;
}

/* Read the current cc08 dispatch state (DAT_0438cc08). */
int player_ctrl_cc08(void)
{
    return s_cc08;
}

/* Debug/test hook: force the cc08 state id.  Stands in for the unported
 * state-transition writers (customer-approach → 4, counter-open → 0x32, talk →
 * the cc04 path, …) so the host suite can verify the dispatch actually gates the
 * walk on cc08 rather than driving unconditionally. */
void player_ctrl_debug_set_cc08(int state)
{
    s_cc08 = state;
}

/* ── W3 free-roam walk leaves ─────────────────────────────────────────── */

/* input.c d-pad mask bits (input_binding_mask[0..3]). */
#define PC_BTN_UP    0x0004u
#define PC_BTN_RIGHT 0x0001u
#define PC_BTN_DOWN  0x0008u
#define PC_BTN_LEFT  0x0002u

int player_ctrl_dpad_angle(unsigned held_mask, float *out_angle)
{
    int dx = ((held_mask & PC_BTN_RIGHT) ? 1 : 0) - ((held_mask & PC_BTN_LEFT) ? 1 : 0);
    int dz = ((held_mask & PC_BTN_DOWN)  ? 1 : 0) - ((held_mask & PC_BTN_UP)   ? 1 : 0);
    if (dx == 0 && dz == 0)
        return 0;
    /* vx = sin(angle), vz = cos(angle)  ⇒  angle = atan2(dx, dz). */
    if (out_angle)
        *out_angle = atan2f((float)dx, (float)dz);
    return 1;
}

/* Decode the held d-pad into a movement intent (facing + moving flag), applying
 * the engine's OPPOSING-PAIR REJECTION (engine-quirks §69):
 *
 *   When LEFT+RIGHT (or UP+DOWN) are both held the frame's d-pad is an invalid
 *   transient — the engine discards it entirely and REPEATS the previous frame's
 *   movement: the stored facing db05c is held and the previous moving state
 *   persists, so the player keeps walking along its current heading rather than
 *   snapping to the net axis.  Verified against retail at the rel-1822 central-
 *   table corner: with LEFT+RIGHT+DOWN held for one frame, retail's velocity is
 *   *byte-identical* to the prior frame (0.14350 @ -45°, the held down-left
 *   heading), not the atan2(0,+1)=0° straight-down the raw d-pad would give.
 *
 * Otherwise (a clean cardinal / valid diagonal / empty d-pad) the facing updates
 * to atan2(dx,dz) exactly as before, so the W3 cardinal walks + wall slide are
 * unaffected (they never press an opposing pair).
 *
 * *io_facing is updated in place when a valid direction is held; held across
 * opposing-pair / idle frames.  Returns the moving flag for this frame. */
int player_ctrl_dpad_intent(unsigned held, float *io_facing, int prev_moving)
{
    int both_h = (held & PC_BTN_LEFT) && (held & PC_BTN_RIGHT);
    int both_v = (held & PC_BTN_UP)   && (held & PC_BTN_DOWN);
    if (both_h || both_v)
        return prev_moving;           /* hold facing + previous moving state */

    float angle;
    int moving = player_ctrl_dpad_angle(held, &angle);
    if (moving && io_facing)
        *io_facing = angle;
    return moving;
}

int player_ctrl_facing_octant(float angle, float cam_yaw)
{
    /* b850 0x48bfd2: ((angle + cam_yaw + π/8) / 2π) * 8 + 8, ftol, & 7.
     * The + π/8 is a half-octant round-to-nearest bias; the + 8 keeps the
     * truncated value non-negative before the mask (faithful to the engine,
     * whose `and eax,7` runs on the post-+8 positive int). */
    float t = (angle + cam_yaw + 0.39269909f) / 6.28318548f;
    return ((int)(t * 8.0f + 8.0f)) & 7;
}

void player_ctrl_house_room_clamp(float *px, float *pz)
{
    /* FUN_00486435 small-room arm ((&DAT_04510578)[...] < 3). cc08 != 4 holds
     * in the controllable state, so the px stop is unconditional here. */
    if (*pz > 9.5f)
        *pz = 9.5f;
    if (*pz > 7.0f && *px < -1.5f)
        *px = -1.5f;
}

/* ── FUN_0048b850 (Cpop) free-roam body ────────────────────────────────────
 *
 * The movement/effects sub-controller that FUN_0048670f's cc08==1 arm calls
 * each frame (engine-quirks §75).  Ported here = the free-roam-ACTIVE position
 * physics: velocity clamp (|v|≤0.175), facing octant dab00 + sticky-snap, the
 * integrate-and-collide call (FUN_00483170), and the damp.
 *
 * NOT here, deliberately (each ports in its own engine site / chip):
 *   - the walk IMPULSE (daabc/daac4 += sin/cos(db05c)·0.1) — that's
 *     FUN_0048670f's cc08==1 controllable code (step 1), written through
 *     *(player+0x904) so it never shows as a DAT_056daabc= literal (§61);
 *     it lives in the cc08==1 arm (player_ctrl_cc08_freeroam_arm below).
 *   - the room-bounds clamp (FUN_00486435) — the FUN_0048670f tail.
 *   - the render-slot population + after-image/particle effects (Chip 2).
 *
 * The engine's dash / da1bc stun-hop / db048-cutscene speed branches all gate
 * OFF in HOUSE free-roam (da1bc==0, *DAT_068dd2f0==0), so the cap tree collapses
 * to 0.175 and the damp tree to 0.82 — the values W3 validated (§61). */
/* FUN_0048b850 tail (engine all.c L90242+): the per-frame motion-history ring
 * shift, the conditional after-image burst fill (DAT_056dacc0), and the
 * dash-trail advance (DAT_056dab6c) — the Cpop.3/6/7 leaves, wired here as the
 * live writer of the chr-walker render banks (engine-quirks §76).
 *
 * Engine order (must hold: burst reads the rings the shift just wrote):
 *   shift rings → burst fill → decay edge → trail advance.
 * Dormant in free-roam (see the bank-storage note): the shift runs but the
 * burst counter is 0 and no trail record is active, so the fills do nothing. */
static void player_ctrl_b850_render_tail(void)
{
    const float cur_pos[3] = { g_scene1_player_pos[0],
                               g_scene1_player_pos[1],
                               g_scene1_player_pos[2] };

    /* 1. shift both 40-slot motion-history rings; slot 0 ← live (pos, record). */
    player_ctrl_history_shift(s_pos_hist, s_rec_hist, cur_pos,
                              s_actor_record[0]);

    /* 2. burst bank (DAT_056dacc0): materialize 5 after-images from the rings
     *    while the burst counter is positive (egg/spell flash).  daae0 == 0 in
     *    free-roam → no-op, returns the counter unchanged. */
    s_burst_count = player_ctrl_burst_materialize(s_burst_bank, s_pos_hist,
                                                  s_rec_hist, s_burst_count);

    /* 3. decay-spawn edge: the DAT_056dae14 down-counter reaching 0 this frame
     *    fires the per-record FUN_0044376a alloc inside the advance.  Stays 0
     *    in free-roam (kicked only by the unported dash path). */
    int decay_spawn = 0;
    if (s_decay_spawn > 0 && --s_decay_spawn == 0)
        decay_spawn = 1;

    /* 4. trail bank (DAT_056dab6c): advance the active after-image records.  No
     *    record is spawned in free-roam, so the advance only ever touches
     *    countdown>0 slots — none today — and never indexes the per-anim angle
     *    table (DAT_005ce5c0), which the dash-spawn sub-chip will source.
     *    ev=NULL drops the (unreached) spawn callbacks. */
    player_ctrl_trail_advance(s_trail_bank, s_actor_record[0], cur_pos,
                              /*angle_table=*/NULL, decay_spawn, /*ev=*/NULL);
}

/* FUN_0048b850 L457-476 (asm 0x48c758-0x48c821): the grounded-walk foot dust.
 * After the integrate+damp, while the player is on the floor and moving, the
 * engine sprays a faint type-0xe records-A particle at the feet every 16th
 * frame.  The particle's spawn + age/kill are already ported (scene1_spawn,
 * scene1_particles_tick); the draw is scene1_walk_dust.c.  See
 * docs/findings/scene1-walk-dust.md + engine-quirks.
 *
 * Gates: not paused (DAT_0438b1a0==0, inert in free-roam), grounded
 * (da1dc==daf88 — always true in free-roam: no jump), horizontal speed > 0.1
 * (post-damp velocity), and (db054 & 0xf)==0.  The engine's extra running emit
 * (DAT_056db034==1) is dormant here — there is no dash in HOUSE free-roam, and
 * the measured live type-0xe count (~2/frame) matches the every-16-frame emit
 * with the 0x20-tick lifetime alone (records/walkdust-types).
 *
 * RNG: two rng_next_unit() are consumed per emit, z-jitter first then x-jitter
 * (asm order); the engine reuses these arg slots as the type(0xe)/scale(0.125)
 * args of FUN_00447f4f, which is why the decompile showed FUN_00471089(0xe,...). */
static void player_ctrl_b850_foot_dust(void)
{
    float vx = s_player_vel[0], vz = s_player_vel[2];
    if (vx * vx + vz * vz <= 0.1f * 0.1f)   /* |v| <= 0.1 → not moving enough */
        return;
    if ((scene1_companion_db054() & 0xf) != 0)
        return;

    float zj = (rng_next_unit() - 0.5f) * 0.5f;   /* rng #1 → z jitter */
    float xj = (rng_next_unit() - 0.5f) * 0.5f;   /* rng #2 → x jitter */
    scene1_spawn(0,
                 g_scene1_player_pos[0] + xj,
                 g_scene1_player_pos[1] + 0.5f,
                 g_scene1_player_pos[2] + zj,
                 0xe, 0.125f, 1);
}

static void player_ctrl_b850_move(void)
{
    /* _STUB: body is the free-roam position-physics subset only; the render-slot
     * populator + after-image/particle effects are Chip 2, so a call-trace diff
     * should still surface this row as ≠ until those land (cf. mark-stubbed-ports). */
    CALL_TRACE_ENTER_STUB(0x48b850u);

    /* step 2: clamp |(vx,vz)| ≤ 0.175 (b850 local_8 cap @ all.c L90010; reuses
     * the camera_shake_clamp leaf — the "shake" naming is the W1 misnomer). */
    player_ctrl_camera_shake_clamp(&s_player_vel[0], &s_player_vel[2],
                                   PC_WALK_SPEED_CAP);

    /* step 3: facing octant dab00 from the (world) facing + HOUSE camera yaw,
     * then the diagonal sticky-snap (identity for cardinals).  dab00 IS the
     * actor record FACING field (daae8[0]+0x18), read by the draw side + the
     * companion (§71/§73), so write it here. */
    int oct = player_ctrl_facing_octant(s_player_facing, PC_HOUSE_CAM_YAW);
    oct = player_ctrl_facing_snap(oct, &s_facing_sticky);
    s_actor_record[0][CHR_ACTOR_FACING] = oct;

    /* step 4: integrate + collide (FUN_00483170).  When the room collision mesh
     * is built (live HOUSE via collision_house_build), run the mesh resolver: it
     * integrates (px+=vx, pz+=vz), pushes the player out of any wall/counter
     * face with the radial probe, snaps Y to the floor (collision_resolve_player).
     * The radial push is the right model here because the counter has a
     * walkable-looking TOP triangle: a pure floor-edge try-move would let the
     * player climb onto it (the step-height gate isn't ported).  Furniture-on-
     * floor round-table collision is deferred with its placement chip (§65).
     * The no-mesh fallback (pre-HOUSE) just integrates. */
    const collision_mesh *cm = collision_house_get();
    if (cm) {
        /* palette mode 0 = HOUSE (*DAT_068dd2f0); gates the 20-ray push. */
        int palette_mode = g_stage_palette ? g_stage_palette->mode : 0;
        collision_resolve_player(cm, g_scene1_player_pos, s_player_vel,
                                 palette_mode);
    } else {
        g_scene1_player_pos[0] += s_player_vel[0];
        g_scene1_player_pos[2] += s_player_vel[2];
    }

    /* step 6: damp.  The engine's damp tree (all.c L90161-90198) collapses to
     * ×0.82 in HOUSE free-roam (grounded da1dc==daf88, db048==0, no dash); runs
     * every frame so velocity decays to 0 after the d-pad is released. */
    s_player_vel[0] *= PC_WALK_DAMP;
    s_player_vel[2] *= PC_WALK_DAMP;

    /* FUN_0048b850 L457-476: foot dust at the feet while grounded + moving. */
    player_ctrl_b850_foot_dust();

    /* FUN_0048b850 tail: motion-history rings + after-image render-bank fill
     * (engine-quirks §76).  Runs every frame; dormant in free-roam. */
    player_ctrl_b850_render_tail();
}

/* ── FUN_0048670f structural skeleton ───────────────────────────────────────
 *
 * Faithful outer shape of the engine's INGAME interaction controller
 * (FUN_0048670f, all.c:86539-88178; engine-quirks §75/§77/§78).  The prologue
 * guard, the periodic customer-spawn refresh, the scene-transition fade
 * handlers, and the screen-rumble tail are all OFF the HOUSE free-roam near-path
 * — they land here as structural stubs (real VA + CALL_TRACE_ENTER_STUB where
 * the engine call is a standalone function, faithful no-op bodies that are
 * correctly inert in steady free-roam — §77).  The cc08 dispatch reads the live
 * state id (DAT_0438cc08, set to free-roam by FUN_004850ec at HOUSE entry):
 * cc08==1 routes the free-roam arm (player_ctrl_cc08_freeroam_arm), every other
 * state the unported event/menu/dialogue arm.  The cc08==1 arm wraps the walk in
 * the engine's escalation/cc04/proximity/interaction guards (§78), all inert in
 * steady free-roam so control reaches the walk unchanged. */

/* FUN_0048670f prologue + scene-transition arms (all.c:86579-86721): the
 * periodic customer-spawn refresh (the DAT_005ce3c4 actor loop, gated
 * DAT_0438b8cc%8==3, spawning shop customers via FUN_004147d5) and the four
 * scene-transition fade handlers (DAT_0450f470/485/495/488 — each runs a fade
 * + its own move/clamp and early-returns).  None of these flags is set during
 * steady HOUSE free-roam (the entry fade is long done, no customer this frame),
 * so the engine falls straight through to the controller.  Inline regions of
 * FUN_0048670f (no standalone VA), modelled as a no-op that signals "no
 * transition active" → the caller falls through.  Returns nonzero only when a
 * transition consumed the frame (never, in this chip). */
static int player_ctrl_scene_transition_tick(void)
{
    return 0;
}

/* FUN_00485861 (0x485861, 280 B): the FUN_0048670f tail screen-rumble update
 * (LAB_004893ff).  Gated on DAT_0438b764 (BSS-zero in HOUSE free-roam → the
 * whole body is skipped), so the faithful free-roam behaviour is a no-op;
 * structural stub. */
static void player_ctrl_tail_rumble(void)
{
    CALL_TRACE_ENTER_STUB(0x485861u);
}

/* ── cc08==1 arm inert sub-regions (FUN_0048670f all.c:919-1313) ─────────────
 *
 * Inline regions of the free-roam arm that surround the walk, modelled here as
 * the engine's logical sub-steps.  Each is inert in steady HOUSE free-roam — its
 * inputs (shop-customer count, nearest-customer/item position, a live
 * interaction target) have no writer in the port yet — so they reduce to a no-op
 * and control reaches the walk exactly as before (the chip is bit-exact).  They
 * are NOT standalone engine functions (no per-region VA), so they carry no
 * CALL_TRACE marker — matching player_ctrl_scene_transition_tick (Chip 3).  Each
 * is the home a future chip fleshes out as the corresponding feature lands.
 *
 * customer-approach escalation (922-957): when shop customers are present
 * ((&DAT_0450fb98)[shop] > 0) the controller escalates to cc08==4 (scripted
 * approach) and consumes the frame.  No customer is spawned in the port, so the
 * three count-gated guards are all false → returns 0 ("no escalation"). */
static int player_ctrl_cc08_customer_escalate(void)
{
    return 0;
}

/* proximity / approach detection (961-1082): reads the nearest-customer and
 * item-pickup positions vs the player to raise the talk / pick-up affordance
 * bools and tick the approach timers (DAT_0438be7c/be80, the db000 gauge).  With
 * no live customer or item the affordances stay false and the only writes are to
 * approach-timer globals that don't feed back into this frame's walk → no-op. */
static void player_ctrl_cc08_proximity_detect(void)
{
}

/* d-pad interaction (1086-1214, the db048==0 block): the action-button masks
 * (cancel 0x20 → menu/exit, confirm 0x40 → talk-to-customer, 0x10 → object
 * interaction / door) each require a live target; none is wired in the port, so
 * every branch falls through to the walk → returns 0 ("no interaction
 * consumed"). */
static int player_ctrl_cc08_dpad_interact(void)
{
    return 0;
}

/* The cc08==1 free-roam walk arm (FUN_0048670f all.c:919-1225).  Surrounds the
 * validated walk with the engine's guard structure: customer-approach
 * escalation → cc04==0 gate → proximity detection → d-pad interaction → walk.
 * All four guards are inert in steady free-roam (above), so control reaches the
 * walk identically to the pre-Chip-4 arm.
 *
 * The walk itself: read the held d-pad → walk impulse (step 1) → FUN_0048b850
 * body (clamp/octant/integrate+collide/damp + render tail) → actor anim record.
 * This is the controllable (cc08==1, cc04==0, da1bc==0) walk validated against
 * runs/w3-walk-watch — byte-identical per-frame to retail (px, vx post-damp) and
 * through the house-table-corner slide (§69/§75).  The room-bounds clamp is NOT
 * here: the engine runs it in the tail (FUN_00486435 @ LAB_004893ff). */
static void player_ctrl_cc08_freeroam_arm(void)
{
    /* customer-approach escalation: inert (no customers) → falls through. */
    if (player_ctrl_cc08_customer_escalate())
        return;

    /* cc04 interaction sub-state: 0 = walking; 1/2 = an object/customer
     * interaction is active (dialogue/haggle — the all.c:1227+ arms, unported).
     * cc04 stays 0 in the port, so the interaction branch is dead. */
    if (s_cc04 != 0)
        return;                       /* cc04 != 0: unported interaction arm */

    /* proximity / approach detection: inert (no customer/item live). */
    player_ctrl_cc08_proximity_detect();

    /* d-pad interaction (door/talk/pickup): inert (no target) → falls through. */
    if (player_ctrl_cc08_dpad_interact())
        return;

    /* ===== the validated free-roam walk (all.c:1216 + the controllable impulse) ===== */

    /* Decode the d-pad → (facing, moving), applying the engine's opposing-pair
     * rejection: a conflicting L+R / U+D frame holds the stored facing + the
     * previous moving state instead of snapping to the net axis (§69). */
    int moving = player_ctrl_dpad_intent(g_input_state[0].buttons,
                                         &s_player_facing, s_player_moving);
    s_player_moving = moving;

    /* step 1: walk impulse.  daabc/daac4 += sin/cos(db05c)·0.1 — FUN_0048670f's
     * cc08==1 controllable code, written through *(player+0x904) so it never
     * shows as a DAT_056daabc= literal (§61).  Stays in the controller; the
     * clamp/octant/integrate/damp are FUN_0048b850 (player_ctrl_b850_move). */
    if (moving) {
        s_player_vel[0] += sinf(s_player_facing) * PC_WALK_ACCEL; /* daabc += sin·0.1 */
        s_player_vel[2] += cosf(s_player_facing) * PC_WALK_ACCEL; /* daac4 += cos·0.1 */
    }

    /* FUN_0048b850 free-roam body: clamp → octant(→FACING) → integrate+collide
     * → damp (engine-quirks §75). */
    player_ctrl_b850_move();

    /* actor record: anim id (0 idle / 1 walk = daae8).  The facing octant (dab00)
     * was set by player_ctrl_b850_move above.  On an idle↔walk transition, restart
     * the new animation at frame 0; otherwise let it continue so the idle's
     * breathing loop keeps the phase it was seeded with.  chr_anim_tick advances
     * the cycle EVERY frame — retail's idle animates too (a 4-frame breathing
     * loop, ~10 ticks/frame; validated runs/w3b-anim-watch), not just the walk. */
    int target_anim = moving ? 1 : 0;
    if (s_actor_record[0][CHR_ACTOR_ANIM] != target_anim) {
        /* idle↔walk transition: seed the new anim at frame 0 / counter 0 and do
         * NOT advance it this frame.  Retail observes counter==0 on the
         * transition frame, with the per-frame increment beginning the NEXT
         * frame (runs/w3b-anim-watch: walk starts at counter 0, then 1,2,…).
         * The old code reset counter=0 then ran chr_anim_tick unconditionally,
         * which incremented it to 1 on the seed frame — leaving the whole
         * walk-cycle phase 1 tick AHEAD of retail for the entire walk (the
         * cumulative sprite drift seen at the house-table-corner cap_08, §W3b).
         * Skipping the tick here is equivalent to suppressing only that
         * increment: on a seed frame (frame 0, timer 0) chr_anim_tick can't
         * advance or wrap anyway, so its sole effect would be the ++ we don't
         * want.  Internal wraps still ++ to 1 (counter→0 then ++), matching
         * retail's steady wrap (counter 36→1). */
        union { float f; int32_t i; } z = { .f = 0.0f };
        s_actor_record[0][CHR_ACTOR_ANIM]    = target_anim;
        s_actor_record[0][CHR_ACTOR_FRAME]   = 0;
        s_actor_record[0][CHR_ACTOR_COUNTER] = 0;
        s_actor_record[0][CHR_ACTOR_TIMER]   = z.i;
    } else {
        chr_anim_tick(s_actor_record[0], s_actor_char[0], 1.0f);
    }
}

/* The unported cc08 dispatch arms (FUN_0048670f all.c:358-918): the event /
 * camera / counter / menu / dialogue states (0,2,3,4,0xa,0xf,0x10,0x11,0x12,
 * 0x17,0x1e,0x32).  Each is reached only by a state transition the port doesn't
 * make yet (customer approach → 4, counter open → 0x32, …), so with cc08 pinned
 * to free-roam they are dead — they would each fall through to the common tail
 * (LAB_004893ff).  Inline regions (no per-state VA), modelled as one inert no-op
 * that lands the dispatch on the tail, faithful to the engine's gotos. */
static void player_ctrl_cc08_unported_arm(void)
{
}

/* ── FUN_0048670f prologue: shop-display "目玉商品" sparkle emitter ──────────
 *
 * (all.c:86579-86598; asm 0x486737-0x4867fa.)  Every 8th frame the engine
 * drops a sparkle over each occupied BACK-ROW display cell — template 0x3b
 * ("目玉商品" = featured display item) through the 2D-overlay particle system
 * (FUN_004147d5 → FUN_00414345 → scene1_overlay_spawn).  Found by a live retail
 * call-trace on the overlay spawn (caller ret_va 0x4867ee) + the template name;
 * full RE in docs/findings/shop-item-display-RE-status.md.
 *
 * Engine: iterate the 7 fixed back-row columns DAT_005ce3c4 = {1,2,3,4,11,12,13};
 * for each, if the furniture gate passes AND the display grid (row 0) cell is
 * occupied, spawn ONE sparkle.  Per emit, 3 rng_next_unit() in order x,y,z:
 *   x = 2*col - 9 + (r1 - 0.5)    (col 1→-7, 2→-5, 4→-1 = the user-save swords)
 *   y = r2 + 2.0                  (just above the item)
 *   z = (r3 - 0.5) - 7.0          (the back stand)
 * template 0x3b, scale 0.3 (0x3e99999a), color white, override_dur -1 → the
 * template's fade_out_default (24) → ~3 alive per item.  Owner = &DAT_056da1b8
 * (stored only; template 0x3b's type_shape 0 + shape_mode 0 never derefs it).
 *
 * PORT-DEBT: the engine's furniture-suppression gate FUN_004860c8(col,0) →
 * DAT_0450fee8[fidx]==0 is omitted.  It only differs from the grid check during
 * display-EDITING mode (DAT_0438cc08==2 — the not-yet-ported item-placement
 * menu); in steady free-roam every grid-occupied back-row cell sits on a visible
 * stand (gate==0 → emit), so grid-only is bit-faithful here, INCLUDING RNG (it
 * emits over exactly the same cells, the only LCG consumers).  Restore the exact
 * gate when the display-management UI lands. */
static const int kSparkleDisplayColumns[7] = { 1, 2, 3, 4, 11, 12, 13 };

static void player_ctrl_display_sparkle_emit(void)
{
    if ((g_sim_frame_count % 8) != 3)        /* DAT_0438b8cc % 8 == 3 */
        return;

    uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank == NULL)
        return;
    const int32_t *grid =
        (const int32_t *)(bank + SAVE_BANK_FIELD_DISPLAY_GRID);

    for (int i = 0; i < 7; i++) {
        int col = kSparkleDisplayColumns[i];
        if (grid[col] == -1)                 /* back-row cell empty */
            continue;

        float r1 = rng_next_unit();          /* x jitter */
        float x  = (float)(2 * col - 9) + (r1 - 0.5f);
        float r2 = rng_next_unit();          /* y */
        float y  = r2 + 2.0f;
        float r3 = rng_next_unit();          /* z jitter */
        float z  = (r3 - 0.5f) - 7.0f;

        scene1_overlay_spawn(&g_scene1_combat_dat_056da1b8,
                             x, y, z,
                             0x3b,            /* template: 目玉商品 sparkle */
                             0.3f,            /* scale_base (0x3e99999a) */
                             -1,              /* dur<1 → template fade_out_default (24) */
                             0, 0, 0);
    }
}

/* ── per-frame player-controller tick (engine FUN_0048670f driver) ─────────
 *
 * Wired into the live HOUSE frame (scene1_ingame_default_arm_tick, before
 * scene1_records_b_tick — the engine's order at FUN_00442cef L40593-40598).
 * Chip 3 gave it the faithful FUN_0048670f outer skeleton (prologue guard →
 * spawn/transition stubs → dispatch → tail); Chip 4 makes the cc08 dispatch a
 * real read of the live state id (DAT_0438cc08) instead of an unconditional
 * route, with the off-path arms as honest stubs (§78).
 *
 * cc08 is set to 1 (free-roam) at HOUSE entry by player_ctrl_cc08_enter_freeroam
 * (FUN_004850ec); the transitions that would drive it elsewhere are unported, so
 * it stays 1 in steady play and the dispatch lands on the free-roam arm — but it
 * now genuinely gates on cc08 (test_player_ctrl_dispatch_gates_on_cc08), so a
 * future writer that sets another state immediately takes effect.  The
 * controllable (cc08==1, cc04==0, da1bc==0) free-roam walk it routes to is
 * validated against runs/w3-walk-watch (per held-direction frame: impulse →
 * clamp → integrate → damp), byte-identical per-frame to the retail watch. */
void scene1_player_ctrl_tick(void)
{
    /* Per-frame ACTOR-STATE flow-trace payload (FUN_0048670f = the HOUSE
     * free-roam update, parent of both the player and companion controllers).
     * Read at onEnter (frame start, before this frame's update) so it mirrors
     * the retail Frida hook on 0x48670f reading the same engine globals at the
     * same point (tools/flow/retail_fields.json).  Player (actor 0) +
     * companion (actor 2) facing octant / world angle / position / anim cell —
     * the modern replacement for the old --player-pos-log px/oct/coct/… columns
     * that phase_probe.py diffed.  poct=DAT_056dab00, pang=DAT_056db05c,
     * coct=DAT_056dab58, pos arrays DAT_056da1d8[actor*3]. */
    CALL_TRACE_BEGIN(0x48670fu);
    {
        const int32_t *r0 = s_actor_record[0];
        const int32_t *r2 = s_actor_record[2];
        CALL_TRACE_F32("px",    g_scene1_player_pos[0]);
        CALL_TRACE_F32("py",    g_scene1_player_pos[1]);
        CALL_TRACE_F32("pz",    g_scene1_player_pos[2]);
        CALL_TRACE_I32("poct",  r0[CHR_ACTOR_FACING]);
        CALL_TRACE_F32("pang",  s_player_facing);
        CALL_TRACE_I32("panim", r0[CHR_ACTOR_ANIM]);
        CALL_TRACE_I32("pframe",r0[CHR_ACTOR_FRAME]);
        CALL_TRACE_I32("pcnt",  r0[CHR_ACTOR_COUNTER]);
        CALL_TRACE_F32("cx",    g_scene1_actor_pos[2][0]);
        CALL_TRACE_F32("cz",    g_scene1_actor_pos[2][2]);
        CALL_TRACE_I32("coct",  r2[CHR_ACTOR_FACING]);
        CALL_TRACE_I32("canim", r2[CHR_ACTOR_ANIM]);
        CALL_TRACE_I32("cframe",r2[CHR_ACTOR_FRAME]);
        /* db054 = the {phasepin}-zeroed per-scene counter — the shared clock
         * flow_diff --align-field uses to pair port↔retail frames on a
         * load-stretched HOUSE capture (port ~475 vs retail ~14285). */
        CALL_TRACE_I32("db054", scene1_companion_db054());
    }
    CALL_TRACE_END();

    /* prologue guard FUN_00434d6a (all.c:86575): the save/load dialog gate.
     * Returns -1 only while that dialog pumps (DAT_0438b148 BSS-zero → returns
     * 0 in HOUSE); the engine early-returns on -1. */
    if (title_save_dialog_gate_tick() == -1)
        return;

    if (s_actor_char[0] == -1)        /* no live player actor (pre-HOUSE) */
        return;

    /* prologue (all.c:86580, BEFORE the transition arms): drop the periodic
     * "目玉商品" sparkles over the items on display.  No-op (no RNG) on an empty
     * display, so it leaves the foot-dust/wing RNG stream untouched there. */
    player_ctrl_display_sparkle_emit();

    /* prologue: per-frame background-window-NPC pump (FUN_0046f621 → the
     * spawn/drift sim FUN_0046f2a3, ported in scene1_bg_npc.c).  The first call
     * runs the 180× warmup that seeds the 6 NPCs; later calls tick once.  This
     * is a live consumer of the shared LCG (sporadic bound-cross respawns) —
     * its position in the call order, BEFORE FUN_0048b850's dust emit, is what
     * the foot-dust RNG-stream parity depends on.  (The periodic customer-spawn
     * refresh that also lives in this prologue stays inert — no customers.) */
    scene1_bg_npc_tick();

    /* scene-transition fade handlers (DAT_0450f470/485/488/495): none fires in
     * steady HOUSE free-roam → fall through to the controller. */
    if (player_ctrl_scene_transition_tick())
        return;

    /* cc08 dispatch (DAT_0438cc08): cc08==1 = free-roam walk → the ported arm;
     * every other state is an unported event/menu/dialogue arm.  Reads the live
     * state id — a real dispatch, not the old unconditional route.
     *
     * Prologue guard: while the opening dialogue is running (iv1_1/iv1_2) or
     * loading its next script, the engine is in a cc08 != 1 (event) state, so
     * the free-roam WALK arm does NOT run and the player is immobile. The port
     * sets cc08=1 at HOUSE entry — *before* the prologue — so without this guard
     * the player walks during iv1_2 (the 2nd dialogue plays over the live
     * HOUSE), most visibly after an ESC skip of iv1_1 jumps onto it. Suppress
     * ONLY the walk arm: scene1_bg_npc_tick (above), records/particles, and the
     * bounds-clamp tail still run, so the load/scene path is untouched (gating
     * the whole controller broke the load — it skipped the NPC/RNG pump).
     * PORT-DEBT: the faithful fix is cc08 timing — it should flip to 1 only at
     * the real free-roam boundary (FUN_004850ec), after the prologue. */
    if (s_cc08 == 1 &&
        !scene1_intro_dialogue_active() && !scene1_intro_dialogue_loading())
        player_ctrl_cc08_freeroam_arm();
    else
        player_ctrl_cc08_unported_arm();

    /* tail LAB_004893ff: the room-bounds clamp (FUN_00486435) runs
     * UNCONDITIONALLY here — AFTER FUN_0048b850's mesh collision (§75).  The
     * mesh resolver gives the right wall (px 3.10) + counter (pz 8.94); this
     * clamp gives the front (pz≤9.5) + left (px≥-1.5 when pz>7) HOUSE bounds,
     * which aren't in the room mesh or any placed object (§67) — together they
     * reproduce retail's box px[-1.5,3.1] pz[8.94,9.5].  The clamp touches
     * position while b850's damp + the anim update touch velocity / the sprite
     * record, so the engine's split (damp inside b850, clamp in the tail) is
     * order-independent — moving it here from the old in-arm position is
     * bit-exact.  Then FUN_00485861 (screen-rumble; no-op in free-roam). */
    player_ctrl_house_room_clamp(&g_scene1_player_pos[0],
                                 &g_scene1_player_pos[2]);
    player_ctrl_tail_rumble();
}
