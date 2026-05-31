/*
 * collision_resolve.h — HOUSE player slide-resolve (W4.3).
 *
 * Player-only (actor-0) slice of FUN_00483170 + the raycast FUN_00433674.
 * HOUSE has no type-1/2 wall triangles, so the engine's atan2 wall-slide loop
 * never fires; blocking instead comes from the **radial push** — 8 horizontal
 * rays cast around the player (radius ~1.05) that push it back out of any
 * vertical wall / furniture face they hit.  This produces the ~1-unit standoff
 * that pins the player against the right wall (px≈2.29) and stops it head-on at
 * the central round table (px≈0.73), per runs/w4-table3 retail ground truth.
 *
 * Not ported (documented seams): the enemy-proximity loop (DAT_0695f004) and
 * companion actor (actor-1) — both moot at first HOUSE free-roam; the worldmap
 * tiling/grid; the dynamic-prop path.  Y/gravity is a flat-floor ground snap.
 */
#ifndef OPENRECET_COLLISION_RESOLVE_H
#define OPENRECET_COLLISION_RESOLVE_H

#include "collision_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cast a ray from `pos` along `dir` (NOT normalized — its length is the cast
 * distance) against the mesh.  Returns 1 and fills the outputs with the
 * NEAREST hit whose fraction along `dir` is in [0,1]; else 0.  Port of the
 * ray-vs-triangle core of FUN_00433674.
 */
int collision_raycast(const collision_mesh *m,
                      const float pos[3], const float dir[3],
                      float *out_frac, float out_normal[3], int *out_type);

/*
 * Resolve one player-physics frame in place: integrate `vel` into `pos`, apply
 * the radial push (FUN_00483170 L84404-84445), and snap Y to the floor.
 * `pos`/`vel` are world-space (x,y,z).  `palette_mode` is the stage-palette
 * mode (`*DAT_068dd2f0`; 0 = HOUSE) — it selects 20 rays at stacked heights +
 * the 1.03 cos scale when the player is past pz=0.7, vs the plain 8-ray push.
 */
void collision_resolve_player(const collision_mesh *m, float pos[3], float vel[3],
                              int palette_mode);

/*
 * Faithful HOUSE wall resolve: the engine's try-move probe (FUN_004830f1 →
 * FUN_00432e50, the off-floor block model of engine-quirks §64).  Integrates
 * `vel` into `pos` by querying the floor at the *destination*: if the moved
 * point is still over a floor triangle the move is taken, else it is blocked.
 * Done per-axis so a wall slides the free axis (the "X blocked, Z free" retail
 * behavior).  Snaps Y to the floor afterward.  No standoff fudge — the pin is
 * the floor edge, so it follows the room's real (non-rectangular) wall contour.
 *
 * This handles room walls + the counter (floor ends at both).  Furniture that
 * sits ON the floor (round table) is NOT blocked by this — that needs the
 * radial push above + furniture placement (deferred, engine-quirks §65).
 */
void collision_resolve_player_floor(const collision_mesh *m,
                                    float pos[3], const float vel[3]);

/* True if `type` is excluded from the radial-push raycast (furniture-special
 * types 5,6,8..16).  Walls are type 0 (kept) and distinguished by normal. */
int collision_raycast_type_excluded(int type);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_COLLISION_RESOLVE_H */
