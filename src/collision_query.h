/*
 * collision_query.h — point→triangle ground/wall query (W4.2).
 *
 * Port of FUN_00432e50 (the HOUSE/static-mesh path).  Given a world-space
 * point, finds the highest floor triangle directly beneath it (in the XZ
 * plane) whose plane sits within 5 units below the query Y, and returns that
 * ground height + surface normal.  This is the primitive the slide-resolver
 * (FUN_00483170, W4.3) probes: a point with no ground triangle under it has
 * walked off the floor mesh (into a wall / past the counter) and is blocked.
 *
 * Differences from the engine (documented seams):
 *   - The 15×15 worldmap grid cell-select (DAT_073e03ac) + the 40-unit tiling
 *     wrap are SKIPPED — they are gated off for HOUSE (DAT_0438c148==1).  We
 *     test every object's triangles; the per-triangle AABB reject makes this
 *     correct (same winning triangle) if slower than the grid.
 *   - The dynamic-object path (DAT_0438c150 spawned props) is not ported; HOUSE
 *     free-roam has none.
 *   - Coordinates are world-space throughout (collision_mesh records already
 *     baked the frame transform + ×0.2 + X-negation), so there is no per-object
 *     origin subtraction.
 */
#ifndef OPENRECET_COLLISION_QUERY_H
#define OPENRECET_COLLISION_QUERY_H

#include "collision_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   hit;          /* 1 = a ground triangle was found under the point */
    float height;       /* ground Y at (px,pz) on the winning triangle */
    float normal[3];    /* unit surface normal (FUN_00432e50 output form) */
    int   object;       /* winning object index (−1 if none) */
    int   tri;          /* winning triangle index within the object */
    float plane[4];     /* winning triangle plane n.x,n.y,n.z,d (world space) */
} collision_hit;

/*
 * Query the floor under world point (px,py,pz).  Returns 1 and fills `out`
 * when a covering floor triangle is found within 5 units below py; else 0
 * (out->hit = 0).  `out` may be NULL only if you ignore the result (it isn't).
 */
int collision_query_ground(const collision_mesh *m,
                           float px, float py, float pz,
                           collision_hit *out);

/* Same against a single object (the per-object core; exposed for testing). */
int collision_query_ground_object(const collision_object *o,
                                   float px, float py, float pz,
                                   collision_hit *out);

/*
 * True if a triangle of this type is excluded from the ground query
 * (FUN_00432e50 L157-160): the furniture/special types 8..16, plus type 7
 * (wall) when the wall-pass flag is off.  Exposed for testing.
 */
int collision_query_type_excluded(int type);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_COLLISION_QUERY_H */
