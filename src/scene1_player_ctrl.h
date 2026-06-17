/*
 * scene1_player_ctrl.h — Cpop.1: HOUSE per-frame player controller.
 *
 * Engine FUN_0048b850 (0x48b850, 5030 B) is the live per-frame player /
 * camera controller in the playable HOUSE.  Runtime-confirmed 2026-05-30
 * (Frida call-trace, runs/calltrace-shop-probe): it fires 40,558× from
 * frame 4583 on, armed by FUN_0048670f (NOT FUN_0048b3f6 — that branch of
 * the dispatcher @all.c:40595 never fires in HOUSE).  See
 * docs/findings/scene1-char-sprite-render.md (ERRATA + runtime sections).
 *
 * FUN_0048b850 is a ~634-line multi-system controller (camera shake/zoom
 * inertia, input→facing octant, dash-trail/after-image fill of the
 * 0x56dab6c array the walker reads in sweep-0, party proximity, footstep
 * /audio cues).  This chip starts the faithful port with its genuinely
 * pure leaf math — the pieces with no engine-global or callee dependency,
 * host-testable in isolation.  The stateful body + the 0x56dab6c trail
 * fill follow in later sub-chips behind _WIN32.
 */
#ifndef SCENE1_PLAYER_CTRL_H
#define SCENE1_PLAYER_CTRL_H

#include <stdint.h>

/* Per-actor slots the shop-walker player draw walks (0=player, 1/2=party). */
#define PC_NUM_ACTORS        3
#define PC_ACTOR_REC_DWORDS  11   /* DAT_056daae8 ring stride (sprite-state) */

/*
 * Snap an 8-way movement octant to the 4-way sprite facing the engine
 * actually draws (0=down? 2=right, 4=up?, 6=left — engine octant ids),
 * using a *sticky* horizontal-bias bit to disambiguate the diagonals.
 *
 * Engine (FUN_0048b850 @ all.c L90015-90060, after `DAT_056dab00 = ftol & 7`):
 *   - octant 2 or 6 (pure horizontal)  → set   sticky (DAT_056dae3c = 1)
 *   - octant 0 or 4 (pure vertical)     → clear sticky (DAT_056dae3c = 0)
 *   - octants 1/3/5/7 (diagonals) leave sticky unchanged, then snap toward
 *     the sticky horizontal: sticky=0 → {1,7}→0, {3,5}→4;
 *                            sticky=1 → {1,3}→2, {5,7}→6.
 *   - octants 2/6 pass through unchanged in both branches.
 *
 * `octant` is masked to 0..7 on entry (mirrors the engine's `& 7`).
 * `*sticky` is read AND updated (it is the persistent DAT_056dae3c memory).
 * Returns the snapped octant.
 */
int player_ctrl_facing_snap(int octant, int *sticky);

/*
 * Per-frame camera zoom-bias (DAT_056daac0) decay.
 * Engine: `DAT_056daac0 -= 0.03; if (DAT_056daac0 < -2.0) DAT_056daac0 = -2.0;`
 * (.rdata 0x519900 = 0.03).  Returns the decayed value.
 */
float player_ctrl_camera_z_decay(float z);

/*
 * Clamp the camera-shake vector (DAT_056daabc, DAT_056daac4) so its
 * magnitude does not exceed `target` (the per-frame zoom-target scalar
 * `local_8`).  Engine (FUN_0048b850 @ all.c L90011-90014):
 *   mag = sqrt(x*x + y*y);  if (target <= mag) { x = x*target/mag;
 *                                                y = y*target/mag; }
 * Mirrors the engine's multiply-then-divide order and the `target <= mag`
 * guard (so mag==0 with target>0 takes no division).  In/out via pointers.
 */
void player_ctrl_camera_shake_clamp(float *shake_x, float *shake_y,
                                    float target);

/*
 * Advance the player-controller's emote-bubble pulse counters one frame.
 * Engine FUN_0048b850 @ all.c L89799-89817 (objdump 0x48b8c9-0x48b917):
 *
 *   if (*down > 0) (*down)--;                     // DAT_056db00c
 *   if (*phase > 0) {                             // DAT_056db008
 *       (*phase)++;                               // pre-incremented, then tested
 *       if (*phase < 0x1e) { if (*level < 10) (*level)++; }   // DAT_056db000
 *       else               { if (*level > 0)  (*level)--; }
 *       if (*phase > 0x3c) *phase = 0;            // wrap the 0..60 phase
 *   }
 *
 * `phase` (db008) is a 0..0x3c frame timer kicked to 1 elsewhere; `level`
 * (db000, 0..10) is the emote-bubble scale/intensity it ramps up over the
 * first ~30 frames and back down over the next (consumed by the bubble
 * draw at all.c L6901+, a `sin(level·π/8)` scale).  `down` (db00c) is an
 * independent down-counter.  Both `phase` comparisons use the
 * *post-increment* value (matches the engine's `inc eax` before either
 * `cmp`).
 */
void player_ctrl_pulse_counters(int *down, int *phase, int *level);

/*
 * Place one dash-trail / after-image record around the player.
 * The geometric core of FUN_0048b850's `0x56dab6c` trail fill
 * (all.c L89906-89933; objdump 0x48c9c6-0x48ca47) — the array the walker
 * reads in sweep-0 (scene1-char-sprite-render.md).  Per active record:
 *
 *   angle = 2·table_val + stored_angle;     // table_val = (float[])0x5ce5c0[anim_idx]
 *   r     = (float)anim_idx + 3.0;
 *   out[0] = sinf(angle)·r + player[0];      // x   (FUN_00503a44 = sin)
 *   out[1] = player[1];                      // y   (unchanged)
 *   out[2] = cosf(angle)·r + player[2];      // z   (FUN_00503994 = cos)
 *
 * Note: the engine adds `stored_angle` via a raw `fadd DWORD [ebx-4]`
 * (a *float* load) — Ghidra mistypes the +0x3c field as `(float)int`;
 * it is a float.  `table_val` is supplied by the caller (the 0x5ce5c0
 * per-anim phase table is read but not owned by this leaf).
 */
void player_ctrl_trail_orbit_pos(int anim_idx, float stored_angle,
                                 float table_val, const float player[3],
                                 float out[3]);

/* Cpop.3: per-frame motion-history ring depth.  The engine keeps two
 * parallel 40-slot ring buffers the after-image/trail draw samples — a
 * 3-float position history (DAT_056da1fc, 40·0xc bytes) and an 11-dword
 * sprite-state record history (DAT_056da3dc, 40·0x2c bytes) — laid out
 * back-to-back, ending exactly at the shake globals DAT_056daabc. */
#define PC_HIST_SLOTS 40

/*
 * Advance the player-controller's motion-history rings one frame.
 * Engine FUN_0048b850 @ all.c L90243-90269 (objdump 0x48c8a9-0x48c90f):
 * a full memmove-style shift — slot[i] = slot[i-1] from the oldest end
 * down to slot 1 (the engine walks pointers high→low, copying each
 * 3-dword position and 11-dword record forward by one), then writes the
 * newest sample into slot 0: position 0 = the live player pos
 * (DAT_056da1d8/1dc/1e0) and record 0 = the live sprite-state record
 * (DAT_056daae8).  Index 0 is newest; increasing index is older.
 *
 * Pure: no engine globals or callees — the caller owns both arrays and
 * supplies the current sample.  `pos_hist`/`rec_hist` hold PC_HIST_SLOTS
 * slots each; `cur_rec` is PC_ACTOR_REC_DWORDS dwords.
 */
void player_ctrl_history_shift(float pos_hist[][3],
                               int32_t rec_hist[][PC_ACTOR_REC_DWORDS],
                               const float cur_pos[3],
                               const int32_t cur_rec[PC_ACTOR_REC_DWORDS]);

/*
 * Cpop.4: select the per-frame camera-shake damping factor.
 * Engine FUN_0048b850 @ all.c L90160-90198 (objdump 0x48c538-0x48c6a0):
 * the shake vector (DAT_056daabc, DAT_056daac4) is multiplied each frame
 * by one of six decay factors chosen by this tree; this returns that
 * factor.  (The zoom-bias DAT_056daac0 is *separately* and
 * *unconditionally* multiplied by 0.95 at LAB_0048c6a6 right after — not
 * returned here; the controller body applies it.)
 *
 *   if (mode_nonzero)                 → 0.97   // DAT_056da1bc != 0
 *   else if (!grounded)               → 0.99   // DAT_056da1dc != DAT_056daf88
 *   else if (flag_6ca && !held_96b)   → 0.95   // (DAT_068dd2f0[0x6ca]!=0) && !held
 *   else if (!( (!edge_9 && db100<1) || held_96b ))  → 0.998
 *   else {                                       // the DAT_056db048 state block
 *       if (db048 == 3)               → 0.95
 *       else if (db048 != 2 && !grounded) → 0.98   // dead in-engine (this arm is
 *                                                  //   only reached when grounded),
 *                                                  //   kept for faithfulness
 *       else                          → 0.82       // db048==2, or grounded
 *   }
 *
 * `flag_6ca` is DAT_068dd2f0[0x6ca] (tested != 0); `held_96b` is
 * FUN_004856d7(0x96b) ("is binding 0x96b held"); `edge_9` is
 * FUN_0043647f(9) ("is key 9 in this frame's edge list").  The caller
 * supplies the two query results (the leaf-first convention).
 */
float player_ctrl_shake_damp_factor(int mode_nonzero, int grounded,
                                    int flag_6ca, int held_96b,
                                    int edge_9, int db100, int db048);

/*
 * Cpop.5: accumulate the per-frame camera-shake *target* magnitude
 * (`local_8`), the scalar the shake-vector clamp (player_ctrl_camera_shake_clamp)
 * limits the vector to.  Engine FUN_0048b850 @ all.c L89957-90008
 * (objdump 0x48be33-0x48bfa0).  Together with the zoom decay, the
 * magnitude clamp, and the Cpop.4 per-frame damp factor, this completes
 * the camera-shake-magnitude subsystem.
 *
 *   t = base;                              // 0.175, or per-state amplitude table
 *   if (held_968) t += 0.02;               // FUN_004856d7(0x968)  [double const]
 *   if (held_969) t += 0.08;               // FUN_004856d7(0x969)  [double const]
 *   if (boost)    t *= 1.3;                // edge 0xb|0xc | db010>0 | db01c>0
 *   if (b8b0_is_neg1) t += db074;          // DAT_0438b8b0 == -1
 *   if (dae9c_active) {                    // DAT_056dae9c != 0
 *       if (daeac & 2)      t += 0.06;
 *       else if (daeac & 1) t += 0.03;
 *   }
 *   if (db048 == 1) t = 0.5;                               // overrides
 *   if (db048 == 4 || db048 == 5) t = (da1cc == 0x29) ? 1.0 : 0.5;
 *   if (daed8_is_1 && db07c_is_0)          // DAT_056daed8 == 1 (INT; quirk §56)
 *       t = 0.3 - clamp01(daedc - da1dc) * 0.1;            // proximity ease
 *   return t;
 *
 * `base` is resolved by the caller: 0.175 when `*DAT_068dd2f0 == 0`, else
 * the per-state shake-amplitude table value (`0x73ae058 + state*0x40`,
 * +0x1c, or +0x20 when DAT_056db034 == 1).  Boolean predicates are passed
 * in (the leaf-first convention); the two boost adds are 64-bit doubles in
 * the binary but fold to the same float result.
 */
float player_ctrl_shake_target(float base, int held_968, int held_969,
                               int boost, int b8b0_is_neg1, float db074,
                               int dae9c_active, int daeac,
                               int db048, int da1cc,
                               int daed8_is_1, int db07c_is_0,
                               float daedc, float da1dc);

/* ── Cpop.6: dash-trail / after-image record advance ─────────────────────
 *
 * The per-frame advance of the 5 after-image records (DAT_056dabac, stride
 * 0x44 = 17 dwords, ending at DAT_056dad00) the chr-sprite walker draws
 * behind a moving/dashing player.  Engine FUN_0048b850 @ all.c L90300-90334
 * (objdump 0x48c991-0x48ca9d); the live consumer of the already-ported
 * player_ctrl_trail_orbit_pos geometry leaf.
 *
 * Per record, only while its life counter is positive (signed `> 0`):
 *   1. (gated on `decay_spawn`) request FUN_0044376a(&DAT_056da1b8, 3, i)
 *      — fired BEFORE the copy, once per active record, when the
 *      DAT_056dae14 down-counter ticked to 0 this frame.
 *   2. snapshot the live sprite-state ring head (DAT_056daae8, 11 dwords)
 *      into the record's sprite slot.
 *   3. recompute the orbit position via player_ctrl_trail_orbit_pos
 *      (angle = 2·table[idx] + stored_angle, r = idx + 3); x/y/z are
 *      *floats* (the engine `fstp`s them — Ghidra mistypes them as int).
 *   4. if the life counter is exactly 600, request
 *      FUN_0041331d(0, x, y, z, 4, 0.7, 0xffffffff) at the new position.
 *   5. decrement the life counter.
 *
 * The two engine side-effect calls are reported through `ev` (may be NULL)
 * so the pure advance stays host-testable; the eventual _WIN32 controller
 * body fires them.  `angle_table` is the read-only DAT_005ce5c0 per-anim
 * phase table the leaf indexes by `idx`.
 */
#define PC_TRAIL_RECORDS     5
#define PC_TRAIL_REC_DWORDS  17   /* 0x44-byte record stride */

/* dword field indices within one trail record */
#define PC_TRAIL_SPRITE      0    /* [0..10] 11-dword sprite-state snapshot   */
#define PC_TRAIL_X           11   /* float — orbit x       (engine [ebx-0x14]) */
#define PC_TRAIL_Y           12   /* float — player y      (engine [ebx-0x10]) */
#define PC_TRAIL_Z           13   /* float — orbit z       (engine [ebx-0x0c]) */
#define PC_TRAIL_COUNTDOWN   14   /* int   — life counter  (engine [ebx-0x08]) */
#define PC_TRAIL_ANGLE       15   /* float — stored angle  (engine [ebx-0x04]) */
#define PC_TRAIL_IDX         16   /* int   — anim/table id (engine [ebx])      */

typedef struct {
    /* FUN_0044376a(&DAT_056da1b8, 3, record_index) requests, in record order. */
    int alloc_count;
    int alloc_index[PC_TRAIL_RECORDS];
    /* FUN_0041331d(0, x,y,z, 4, 0.7, 0xffffffff) requests (life counter hit 600). */
    int   spawn_count;
    float spawn_pos[PC_TRAIL_RECORDS][3];
} pc_trail_events;

void player_ctrl_trail_advance(int32_t records[][PC_TRAIL_REC_DWORDS],
                               const int32_t sprite_ring[PC_ACTOR_REC_DWORDS],
                               const float player[3],
                               const float angle_table[],
                               int decay_spawn,
                               pc_trail_events *ev);

/* ── Cpop.7: dacc0 after-image burst materialization ─────────────────────
 *
 * The conditional burst writer of the *second* 5-record after-image bank
 * (DAT_056dacc0, the actor sprite-state array STATUS.md names as the top
 * HOUSE-pixel blocker).  Engine FUN_0048b850 @ all.c L90270-90298 (objdump
 * 0x48c918-0x48c971); fires after the per-frame motion-history ring shift.
 *
 * Source geometry (resolved 2026-05-30 — objdump-verified, no slot
 * straddling): both motion-history rings are 40-slot, slot 0 == newest
 * (the live sample written by player_ctrl_history_shift).  The burst
 * samples **every other slot starting at slot 3** — slots 3, 5, 7, 9, 11 —
 * into the bank's 5 records.  The engine's "doubled" 0x18 (position) /
 * 0x58 (record) strides are exactly 2× the 0xc / 0x2c slot pitches; the
 * `[edx-0x4]/[edx]/[edx+4]` reads land on the three components of one full
 * slot whose middle the source pointer addresses (da224 = slot-3 comp-1).
 *
 * Per destination record k (k = 0..4, source slot s = 3 + 2k):
 *   - sprite-state[0..10] ← rec_hist[s] (11-dword copy)
 *   - x/y/z (record [11..13]) ← pos_hist[s][0..2]   (full-slot copy)
 *   - life (record [14], engine +0x38) ← 0x14
 *
 * Drive (in/out `counter` == DAT_056daae0):
 *   - counter <= 0 → no-op, returns counter unchanged.
 *   - counter  > 0 → materialize the 5 records, then counter-- ; if that
 *     reaches 0, a clear pass zeroes every record's life field (engine
 *     L90293-90297, only the +0x38 dword) — so the final burst frame fills
 *     then immediately retires the bank.  Returns the decremented counter.
 *
 * Pure: the caller owns the bank + both history rings and the counter; the
 * steady-state writer of this same bank remains the unported Cf.*
 * FUN_00436f97.  Reuses the 0x44 PC_TRAIL_* record layout (sprite[0..10],
 * x=11,y=12,z=13, life=14).
 */
#define PC_BURST_RECORDS   5    /* DAT_056dacc0 bank: 5 × 0x44-byte records */

int player_ctrl_burst_materialize(int32_t bank[][PC_TRAIL_REC_DWORDS],
                                  const float pos_hist[][3],
                                  const int32_t rec_hist[][PC_ACTOR_REC_DWORDS],
                                  int counter);

/*
 * Cpop.8 — the displayed HP/SP gauge tween.  Engine FUN_0048b6ad (0x48b6ad,
 * 407 B) is the player controller's FIRST per-frame sub-update (called at the
 * top of FUN_0048b850).  It eases two on-screen gauges toward their true
 * values at a per-character speed — the classic bar that slides down a few
 * frames after you take a hit.
 *
 *   Channel A — HP: `disp_hp` (DAT_056db0c4) follows `true_hp` (DAT_056db0bc,
 *     the player-HP float; see scene1-records-b-state-machine.md Q2) at
 *     `hp_rate`.  Tracks `counter` (DAT_056db0cc, frames spent un-settled,
 *     reset to 0 the frame it lands) and `dir` (DAT_056db0d0, 1 = rising /
 *     healing, 0 = falling / damage).
 *   Channel B — SP: `disp_sp` (DAT_056db0c8) follows `true_sp` (DAT_056db0c0)
 *     at `sp_rate`.  Clamp-on-overshoot only — no counter or direction.
 *
 * The targets db0bc/db0c0 are seeded from the chara record's [+0x3c..+0x42]
 * bytes and mirrored into the follower pair db0c4/db0c8 at stage post-load
 * (stage_post_load.h step 4).  Both rates come from the per-character record
 * `rec = &DAT_04510648 + DAT_0438b7d8*0x6c + DAT_0438b1e0*0x2dfc8`:
 *   hp_rate = (i16[rec+0x3c] + i16[rec+0x3e]) * 0.01
 *   sp_rate = (i16[rec+0x40] + i16[rec+0x42]) * 0.01
 * Resolved by the caller via player_ctrl_gauge_rate() so this leaf stays
 * table-free (matching player_ctrl_trail_orbit_pos / player_ctrl_shake_target).
 */
float player_ctrl_gauge_rate(int16_t field_lo, int16_t field_hi);
void  player_ctrl_gauge_track(float *disp_hp, float true_hp, float hp_rate,
                              float *disp_sp, float true_sp, float sp_rate,
                              int *counter, int *dir);

/* ── Cchr.2h: player/companion actor-state model ─────────────────────────
 *
 * The engine globals the shop-walker player draw (FUN_004552d0 L357-454)
 * reads per actor i: char id (DAT_056da1cc[i]), XZ/Y scale (DAT_056dae18[i]
 * / DAT_056dae24[i]), and the 11-dword sprite-state record (DAT_056daae8 +
 * i*0xb).  FUN_0048b850 is their live writer; until it lands, the pose
 * function below seeds actor 0 from the runs/cchr2b retail leaf ground
 * truth.  Position is the separate g_scene1_player_pos (DAT_056da1d8).
 */

/* Seed actor 0 = the standing player (idle pose), empty slots 1/2.
 * `player_char` is the engine's DAT_056da1cc (0 = Recette on HOUSE entry). */
void player_ctrl_pose_house_standing(int player_char);

/* Read accessors for the draw side (i out of range → -1 / 0 / NULL). */
int            player_ctrl_actor_char(int i);
float          player_ctrl_actor_scale_xz(int i);
float          player_ctrl_actor_scale_y(int i);
const int32_t *player_ctrl_actor_record(int i);

/* Mutable record access for the companion controller (scene1_companion_ctrl,
 * engine FUN_0048a833) — it owns the actor-2 record's anim/facing fields the
 * same way scene1_player_ctrl_tick owns actor 0's.  Returns NULL if i is out of
 * range.  PC_ACTOR_REC_DWORDS dwords. */
int32_t       *player_ctrl_actor_record_mut(int i);

/* Trace-harness `{phasepin}` for the player (actor 0): zero the anim cycle
 * (FRAME/TIMER/COUNTER) so an IDLE-window port↔retail comparison normalizes the
 * load-dependent phase origin (the companion twin lives in scene1_companion_ctrl).
 * Comparison ONLY — the shipped game keeps the free-running cycle. */
void           player_ctrl_phasepin(void);

/* This frame's player walk-intent (the d-pad moving state, §69).  The §95
 * dev-overlay RNG consume (scene1_sim.c) is gated on it — the overlay's LCG step
 * fires every render frame only while the player is moving. */
int            player_ctrl_is_moving(void);

/* ── FUN_0048b850 tail render banks (Chip 2) ─────────────────────────────────
 * The two after-image banks the chr-sprite walker (FUN_00456f56) draws, owned
 * and written by the b850 tail (player_ctrl_b850_render_tail, engine all.c
 * L90242+): sweep 0 = the dash-trail bank (DAT_056dab6c, always read), sweep 1
 * = the burst bank (DAT_056dacc0, gated on the burst count below).  Each slot
 * is a PC_TRAIL_REC_DWORDS record (sprite[0..10], x/y/z at [0xb..0xd], life/age
 * at [0xe]).  Both banks are empty in HOUSE free-roam (no dash spawn / zero
 * burst counter); the walker iterates them and draws nothing.  Out-of-range
 * sweep/idx → NULL. */
const int32_t *player_ctrl_render_bank_slot(int sweep, int idx);
int            player_ctrl_burst_count(void);

/* Debug accessor: post-tick velocity (vx,vz), stored facing db05c, sticky flag.
 * Any out param may be NULL (engine-quirks §69). */
void player_ctrl_debug_state(float *vx, float *vz, float *facing, int *sticky);

/* Set the stored world facing db05c (conversation-pose ±π/2 write). */
void player_ctrl_set_facing_angle(float angle);

/* ── FUN_0048670f cc08 dispatch state (Chip 4, engine-quirks §78) ────────────
 * The in-game interaction state the controller dispatches on each frame
 * (DAT_0438cc08): 1 = free-roam walk; other values are the unported event /
 * camera / counter / menu / dialogue arms. */
void player_ctrl_cc08_enter_freeroam(void); /* FUN_004850ec: set cc08 = 1 (HOUSE entry) */
int  player_ctrl_cc08(void);                /* read cc08 */
int  player_ctrl_cc04(void);                /* read cc04 (free-roam interaction sub-state) */
int  player_ctrl_companion_ticked(void);    /* did b850_move tick the companion inline this frame? */
void player_ctrl_debug_set_cc08(int state); /* test hook: force cc08 (stands in for the
                                             * unported state-transition writers) */

/* T1 — shop-door exit → the world map (mode 8).  `player_ctrl_at_shop_door` is the
 * pure door-zone predicate (the engine bVar17 subset, all.c:87531-87539); the arm
 * + stage-2 drive the dissolve fade (FUN_004526f5) and the mode-8 load
 * (FUN_0045281c / FUN_00452cde).  See scene1_player_ctrl.c. */
int  player_ctrl_at_shop_door(float player_x, float facing, int already_exited);
void player_ctrl_worldmap_exit_arm(void);
int  player_ctrl_worldmap_exit_stage2(void);
int  player_ctrl_worldmap_exit_armed(void);  /* test accessor */
void player_ctrl_worldmap_exit_reset(void);  /* test reset */

/* Free-roam interaction-affordance emote bubble (DAT_056db000 / DAT_056db004) —
 * the "GO!" door tooltip + talk/pick-up prompts.  `level` (0..10) is the slide-in
 * gauge the bubble's sin scale + visible gate read; `type` is the hpmp_base.tga
 * cell (7 = the shop door).  Driven by player_ctrl_cc08_proximity_detect (the
 * bVar17 door-zone ramp), consumed by the FUN_0040a765 draw (scene1_hud.c). */
int  player_ctrl_emote_level(void);          /* DAT_056db000 */
int  player_ctrl_emote_type(void);           /* DAT_056db004 */

/* Pure ramp step (host-testable): at an affordance → set `type`, ramp `level` up
 * to 10; off it → ramp `level` down to 0.  See scene1_player_ctrl.c. */
void player_ctrl_emote_ramp_step(int at_affordance, int affordance_type,
                                 int *level, int *type);

/*
 * ── W1: the per-frame player-controller tick (FUN_0048670f entry) ─────────
 *
 * Engine FUN_0048670f (0x48670f, 11.5 KB) is the HOUSE player driver — it
 * reads the input masks, moves the player, and sets the animation record,
 * calling FUN_0048b850 (the Cpop camera/effects sub-controller) as a sub-step.
 * Runtime-confirmed live in HOUSE (scene1_player_ctrl.h top: fires 40,558×
 * from frame 4583).
 *
 * Call order (engine FUN_00442cef / scene1_ingame_default_arm_tick,
 * all.c L40593-40598): the controller runs FIRST — before FUN_0043ae20
 * (records-B), gated on `DAT_0438be94 < 0x78` and a dispatcher that picks
 * FUN_0048670f when `0 <= DAT_068dd3fc[DAT_0438b4dc*0x6cf] <= 4` (the live
 * HOUSE branch) else the FUN_0048b3f6 variant.  Both gates are open in HOUSE;
 * faithfully reproducing them is deferred to a later sub-chip.
 *
 * W3 (2026-05-31) fills the controllable free-roam walk: read the held
 * d-pad → facing angle → velocity accumulate/clamp/integrate/damp →
 * room-bounds clamp → actor anim/facing.  Ground-truth-validated against
 * runs/w3-walk-watch (retail, HOUSE walk-left); see the walk-physics §
 * in docs/findings/engine-quirks.md.  The actor anim is advanced EVERY frame
 * via chr_anim_tick (dt=1.0) for both idle and walk — retail's idle breathes
 * (a 4-frame loop at ~10 ticks/frame, validated runs/w3b-anim-watch), not just
 * the walk cycle.  Furniture/mesh collision (FUN_00483170) is W4.
 */
void scene1_player_ctrl_tick(void);

/* ── W3 free-roam walk leaves (pure, host-testable) ──────────────────────
 *
 * Engine constants (objdump of FUN_0048b850 / FUN_00486435, 2026-05-31):
 *   accel    0.1   per held frame    (velocity impulse, player-struct code)
 *   cap      0.175 = b850 local_8 base (the |v| clamp, all.c L90010)
 *   damp     0.82  = grounded-steady factor (all.c L90177)
 * Octant formula (b850 0x48bfd2-0x48bffb): consts 8.0 / 2π / (π/8); the
 * camera-yaw term DAT_073de39c is -π for the fixed HOUSE camera (solved from
 * the watch: idle ang +π/2 → oct 6, LEFT ang −π/2 → oct 2). */
#define PC_WALK_ACCEL      0.1f
#define PC_WALK_SPEED_CAP  0.175f
#define PC_WALK_DAMP       0.82f
#define PC_HOUSE_CAM_YAW   (-3.14159265f)

/* Decode the held button mask (input.c bits: UP 0x04 / RIGHT 0x01 /
 * DOWN 0x08 / LEFT 0x02) into a world-space facing angle (db05c convention:
 * vx = sin(angle), vz = cos(angle), so angle = atan2(dx, dz) with
 * dx = +right/-left, dz = +down/-up).  Returns 1 and writes *out_angle when
 * any direction is held; 0 (leaving *out_angle untouched) when none is. */
int player_ctrl_dpad_angle(unsigned held_mask, float *out_angle);

/* Decode the held d-pad into a movement intent, applying the engine's
 * opposing-pair rejection: a conflicting L+R or U+D frame is discarded and the
 * previous facing (*io_facing) + moving state (prev_moving) persist, so the
 * player keeps walking its stored heading instead of snapping to the net axis
 * (engine-quirks §69, verified byte-identical at the rel-1822 table corner).
 * Otherwise updates *io_facing to atan2(dx,dz).  Returns this frame's moving. */
int player_ctrl_dpad_intent(unsigned held, float *io_facing, int prev_moving);

/* Engine facing octant from a world facing angle + the scene camera yaw.
 * oct = (int)((angle + cam_yaw + π/8) * 8/(2π) + 8) & 7  (b850 ftol formula;
 * the +8 keeps the value positive before the mask, mirroring the engine). */
int player_ctrl_facing_octant(float angle, float cam_yaw);

/* FUN_00486435 (small-room arm, room-type < 3): clamp the player into the
 * HOUSE shop bounds — pz ≤ 9.5, and px ≥ −1.5 while pz > 7.0 (the left-wall
 * stop the walk-left capture hits).  In/out via pointers. */
void player_ctrl_house_room_clamp(float *px, float *pz);

#endif /* SCENE1_PLAYER_CTRL_H */
