/*
 * collision_query.c — port of FUN_00432e50 (HOUSE static-mesh ground query).
 *
 * Per triangle (after a padded-AABB reject + a type-exclusion filter):
 *   - "above-plane" gate: the point must be on the positive-normal side
 *     (plane(px,py,pz) > 0), matching the engine's winding-sensitive test.
 *   - XZ point-in-triangle: three edge cross-products, all ≥ 0 (the
 *     consistent-winding test at FUN_00432e50 L168-173).
 *   - ground height: solve the plane for Y at (px,pz); keep the HIGHEST such
 *     Y across all triangles (the engine's best-ground tracking).
 * After the sweep, the hit counts only if the ground is within 5 units below
 * the query Y (FUN_00432e50 L265).  The output normal is recomputed from the
 * winning triangle's edges exactly as the engine does (L271-279).
 */
#include "collision_query.h"

#include <math.h>

/* FUN_00432e50 L157-160 exclusion: furniture/special types 8..16 are never
 * ground/wall here (the resolver's radial-push handles them); type 7 (wall) is
 * excluded unless the wall-pass flag DAT_0438bed0 is set — off for HOUSE. */
int collision_query_type_excluded(int type)
{
    if (type >= 8 && type <= 16) return 1;   /* 0x8..0x10 */
    if (type == 7) return 1;                 /* wall-pass flag off (HOUSE) */
    return 0;
}

/* XZ point-in-triangle via three edge cross-products (all ≥ 0 → inside). */
static int point_in_tri_xz(const collision_tri *t, float px, float pz)
{
    for (int i = 0; i < 3; i++) {
        float vx = t->v[i][0], vz = t->v[i][2];
        float ex = t->edge[i][0], ez = t->edge[i][2];
        float cross = (pz - vz) * ex - (px - vx) * ez;
        if (cross < 0.0f) return 0;
    }
    return 1;
}

/* Output normal from the winning triangle's edges (FUN_00432e50 L271-279):
 * cross(edgeAB, edgeBC), normalized, with the Y component negated. */
static void out_normal(const collision_tri *t, float n[3])
{
    const float *ab = t->edge[0];   /* B−A */
    const float *bc = t->edge[1];   /* C−B */
    float x = ab[1]*bc[2] - bc[1]*ab[2];
    float y = bc[0]*ab[2] - bc[2]*ab[0];
    float z = bc[1]*ab[0] - ab[1]*bc[0];
    float len = sqrtf(x*x + y*y + z*z);
    if (len < 1e-20f) len = 1.0f;
    n[0] = x / len;
    n[1] = -(y / len);
    n[2] = z / len;
}

int collision_query_ground_object(const collision_object *o,
                                   float px, float py, float pz,
                                   collision_hit *out)
{
    float best = -10000.0f;
    int   best_tri = -1;

    for (int ti = 0; ti < o->tri_count; ti++) {
        const collision_tri *t = &o->tris[ti];
        if (collision_query_type_excluded(t->type)) continue;

        /* Padded-AABB reject (FUN_00432e50 L153-156). */
        if (px < t->aabb_min[0] || px > t->aabb_max[0]) continue;
        if (py < t->aabb_min[1] || py > t->aabb_max[1]) continue;
        if (pz < t->aabb_min[2] || pz > t->aabb_max[2]) continue;

        const float nx = t->n[0], ny = t->n[1], nz = t->n[2], d = t->d;

        if (t->type == 5 || t->type == 6) {
            /* Slope: plane evaluated 1.5 above the point (head height). */
            float lp = px*nx + pz*nz + (py + 1.5f)*ny + d;
            if (lp <= 0.0f) continue;
            if (!point_in_tri_xz(t, px, pz)) continue;
            if (ny == 0.0f) continue;
            float h = -((px*nx + pz*nz + d) / ny);
            if (best < h) { best = h; best_tri = ti; }
            continue;
        }

        /* Floor/solid (types 0,1,2,3): above-plane gate at the point. */
        float lp = px*nx + py*ny + pz*nz + d;
        if (lp <= 0.0f) continue;
        if (!point_in_tri_xz(t, px, pz)) continue;
        if (ny == 0.0f) continue;       /* vertical wall: no ground Y here */
        float h = -((px*nx + pz*nz + d) / ny);
        if (best < h) { best = h; best_tri = ti; }
    }

    if (best_tri < 0) return 0;
    if (!(py - 5.0f < best)) return 0;     /* ground too far below */

    if (out) {
        const collision_tri *t = &o->tris[best_tri];
        out->hit = 1;
        out->height = best;
        out->tri = best_tri;
        out->plane[0] = t->n[0]; out->plane[1] = t->n[1];
        out->plane[2] = t->n[2]; out->plane[3] = t->d;
        out_normal(t, out->normal);
    }
    return 1;
}

int collision_query_ground(const collision_mesh *m,
                           float px, float py, float pz,
                           collision_hit *out)
{
    collision_hit best;
    best.hit = 0; best.height = -10000.0f; best.object = -1; best.tri = -1;
    best.normal[0] = best.normal[1] = best.normal[2] = 0.0f;
    best.plane[0] = best.plane[1] = best.plane[2] = best.plane[3] = 0.0f;

    for (int oi = 0; oi < m->object_count; oi++) {
        const collision_object *o = &m->objects[oi];
        collision_hit h;
        /* Translate the probe into this object's local frame (FUN_00432e50
         * L127-129 subtracts the per-object origin DAT_0438c058/0a8/0f8). */
        if (collision_query_ground_object(o, px - o->origin[0],
                                          py - o->origin[1],
                                          pz - o->origin[2], &h)) {
            h.height += o->origin[1];   /* local floor Y → world Y */
            if (!best.hit || h.height > best.height) {
                h.object = oi;
                best = h;
            }
        }
    }

    if (out) *out = best;
    return best.hit;
}
