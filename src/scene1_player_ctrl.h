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

#endif /* SCENE1_PLAYER_CTRL_H */
