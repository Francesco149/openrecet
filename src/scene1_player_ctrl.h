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

#endif /* SCENE1_PLAYER_CTRL_H */
