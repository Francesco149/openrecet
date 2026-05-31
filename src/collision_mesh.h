/*
 * collision_mesh.h — HOUSE/level collision-mesh ingestion (W4.1).
 *
 * The engine builds a per-object triangle collision mesh from the same
 * `.x` files it renders (it first tries a `<name>_s.x` companion, then
 * falls back to the base `.x` — and the HOUSE shop has no `_s.x`, so the
 * collision triangles live in the render `shop_1st.x` etc.).  Each face's
 * collision *type* comes from the name of the material it references in
 * the MeshMaterialList; faces typed `nohit`(4) are dropped from the mesh.
 *
 * Engine provenance:
 *   - FUN_00471d45  — the `.x` text parser + frame-transform + ×0.2 scale
 *                     + material-name → type classification.  We reuse the
 *                     port's oracle-validated xfile parser (xfile.h) for the
 *                     text and replicate only the transform/scale/classify.
 *   - FUN_0043289b  — builds the runtime triangle records (skips type 4) +
 *                     per-object AABB bounds (DAT_00ac243c…).
 *   - FUN_00432ac6  — per-triangle plane equation (cross-product normal,
 *                     d = −n·A, |n|²), padded AABB, edge vectors.
 *
 * Coordinate space: the vertex pool (DAT_0432a754) holds frame-transformed,
 * ×0.2-scaled "art" coordinates (positive X).  The per-triangle record
 * negates X so its vertices/normal live in the engine *world* space that the
 * player position (DAT_056da1d8/dc/e0 = g_scene1_player_pos) moves in.  So a
 * built record is directly comparable to the live player position.
 *
 * Type codes (from material-name classification, FUN_00471d45):
 *   2  Plane/dame (floor)     12 taru (barrel)    16 taimatu (torch)
 *   3  hit                    13 takara (treasure)
 *   4  nohit/hikari (DROPPED — never built)
 *   5  mizu (water)           14 shokudai (candlestick)
 *   6  gake/yuki (cliff/snow) 15 crystal
 *   7  toumei/kabe (wall)     8/9 tree01/02   10/11 kusa01/02 (grass)
 *   0  default (unnamed / numeric material → generic solid)
 * The HOUSE shop only produces 0 (solid) and 4 (dropped).
 */
#ifndef OPENRECET_COLLISION_MESH_H
#define OPENRECET_COLLISION_MESH_H

#include <stddef.h>
#include "xfile.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Collision-mesh world scale applied to every parsed vertex (FUN_00471d45
 * stores `coord * 0.2` into the vertex pool). */
#define COLLISION_MESH_SCALE 0.2f

/* AABB padding modes — FUN_00432ac6 picks per-level via the collision-enabled
 * flag (`DAT_068dd2f8 + level*0x1b3c < 1`).  HOUSE uses the SMALL padding. */
typedef enum {
    COLLISION_PAD_SMALL = 0, /* x:±0.5  y:−0.5/+3.0  z:±0.5   (HOUSE/indoor) */
    COLLISION_PAD_LARGE = 1  /* x:±1.0  y:−5.0/+10.0 z:±1.0   (outdoor/world) */
} collision_pad_mode;

/*
 * One built collision triangle.  Field names map to the FUN_00432ac6 record
 * (0x98 bytes / 0x26 floats) the query (FUN_00432e50) reads:
 *   n[3]      ← record[0..2]   plane normal (cross of (C−B),(B−A); NOT unit)
 *   d         ← record[3]      plane offset = −(n · A)
 *   nlen2     ← record[4]      |n|²
 *   v[3][3]   ← record[6..8]/[10..12]/[14..16]  A,B,C verts (X negated)
 *   edge[3][3]← record[0x12..0x1d]  edge vectors AB, BC, CA
 *   aabb_min/max ← record[0x1e..0x23]  padded triangle AABB
 *   type      ← record copied from the source type code
 */
typedef struct {
    float n[3];
    float d;
    float nlen2;
    float v[3][3];        /* v[0]=A, v[1]=B, v[2]=C; each {x,y,z}, X negated */
    float edge[3][3];     /* edge[0]=B−A, edge[1]=C−B, edge[2]=A−C */
    float aabb_min[3];
    float aabb_max[3];
    int   type;
} collision_tri;

/* One collision object (one source mesh / map slot). */
typedef struct {
    collision_tri *tris;
    int            tri_count;
    float          aabb_min[3]; /* over the triangle *vertices* (unpadded) */
    float          aabb_max[3];
    /* World placement origin (engine DAT_0438c058/0a8/0f8 per slot).  The
     * query/raycast translate the probe point into this object's local frame
     * by subtracting `origin` (FUN_00432e50 L127-129: `px - DAT_0438c058`).
     * The room (slot 0) is at (0,0,0); placed furniture carries its origin so
     * the same local-space triangles serve every instance.  Zero by default. */
    float          origin[3];
} collision_object;

/* A whole level's collision: one object per loaded map mesh. */
typedef struct {
    collision_object *objects;
    int               object_count;
    float             aabb_min[3]; /* union over all objects */
    float             aabb_max[3];
} collision_mesh;

/*
 * Build one collision object from a parsed `.x` (FUN_0043289b + FUN_00432ac6).
 * Drops `nohit`(4)-typed faces.  Returns 0 on success, non-zero on OOM.
 * `out` is zero-initialised then filled; free with collision_object_free.
 */
int collision_object_build(collision_object *out, const xfile_t *xf,
                           collision_pad_mode pad);

void collision_object_free(collision_object *o);

/*
 * Classify a material-reference name → collision type code (FUN_00471d45's
 * keyword chain).  An unmatched / NULL name returns 0 (generic solid).
 */
int collision_classify_material(const char *name);

/*
 * Compute one triangle record from three art-space (×0.2-applied) vertices
 * and a type code (FUN_00432ac6).  Exposed for host testing.  `pad` selects
 * the AABB padding mode.
 */
void collision_tri_build(collision_tri *t,
                         const float a[3], const float b[3], const float c[3],
                         int type, collision_pad_mode pad);

/* Free a whole level mesh (array of objects + the array). */
void collision_mesh_free(collision_mesh *m);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_COLLISION_MESH_H */
