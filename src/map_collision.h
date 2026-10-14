/*
 * map_collision.h — the stage collision slots and the two map queries, in retail layout.
 *
 * Retail keeps one collision mesh per stage `map:` model in 20 fixed slots
 * (DAT_007ca434 + slot·0x2f8020).  The stage pre-load FUN_00474681 clears them
 * (FUN_0043244c) and builds slot i from map[i] of the stage (FUN_00471d45 →
 * FUN_0043289b → FUN_00432ac6).  Every ground / wall question in the game goes
 * through two functions that read these slots:
 *
 *   FUN_00432e50  mapcol_ground   ground under a point (height, normal, flags)
 *   FUN_00433674  mapcol_ray      first triangle hit along a segment (fraction)
 *
 * House (maptype 0): the objects are the placed models DAT_0438bfb8.. (phase-1
 * 0..4, phase-2 furniture 5..).  Dungeon (maptype >= 1): the probe is wrapped
 * into its 40×40 floor cell and rotated by the cell's pattern; the cell's room
 * model (DAT_073e03ac) is the slot.  Movers (DAT_0438c150..) add more objects.
 *
 * Every table is retail-layout so `rhost --hook shadow:` can point the port at
 * retail's own memory.  Float code mirrors the disassembly (x87.h conventions;
 * the D3DX matrix calls are math3d.c's SSE mirrors).
 *
 * Retail quirks kept (docs/findings/engine-quirks.md §142):
 *   - material `gake` classifies as type 1, not 6 (edx still holds 1 there);
 *   - the slot AABB spans the copy-A records [0, all faces), so faces dropped as
 *     type 4 leave stale records from an earlier build of that slot in it;
 *   - FUN_00432e50 runs up to five passes: passes 2 and 3 are discarded, and
 *     pass 4 accepts ground more than 5 below the probe;
 *   - FUN_00433674 writes plane[0] into all four plane slots of its output.
 */
#ifndef OPENRECET_MAP_COLLISION_H
#define OPENRECET_MAP_COLLISION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAPCOL_SLOTS     20      /* FUN_0043244c clears 0x14 slot headers */
#define MAPCOL_MAX_TRIS  10240   /* copy B 0x17c000 / 0x98; face table 0x32000 / 0x14 */
#define MAPCOL_MAX_VERTS 10240   /* vertex pool DAT_0432a754, 0x1e000 / 12 */
#define MAPCOL_OBJS      20
#define MAPCOL_MOVERS    50

/* One collision triangle (FUN_00432ac6), 0x98 bytes. */
typedef struct {
    float   plane[4];      /* +0x00 a,b,c,d: n = (C−B)×(B−A), d = −n·A */
    float   nlen2;         /* +0x10 */
    float   _pad14;
    float   vx[3], _pad24; /* +0x18 vertex x (A, B, C), negated art x */
    float   vy[3], _pad34; /* +0x28 */
    float   vz[3], _pad44; /* +0x38 */
    float   ex[3], _pad54; /* +0x48 edges B−A, C−B, A−C */
    float   ey[3], _pad64; /* +0x58 */
    float   ez[3], _pad74; /* +0x68 */
    float   bmin[3];       /* +0x78 padded triangle AABB */
    float   bmax[3];       /* +0x84 */
    int32_t type;          /* +0x90 material class */
    int32_t _pad94;
} mapcol_tri_t;

/* One slot: copy-B triangles + the header at slot+0x2f8000. */
typedef struct {
    mapcol_tri_t *tris;    /* DAT_007ca434 + slot·0x2f8020 */
    int32_t count;         /* DAT_00ac2434 */
    int32_t count2;        /* DAT_00ac2438 */
    float   aabb[6];       /* DAT_00ac243c: min x,y,z, max x,y,z (y never written) */
    int32_t cap;           /* port: records allocated in tris */
} mapcol_slot_t;

/* DAT_0438bfb0..0x438c147: the placed collision objects. */
typedef struct {
    int32_t n1;            /* DAT_0438bfb0 phase-1 count (objects 0..n1-1) */
    int32_t n2;            /* DAT_0438bfb4 phase-2 count (objects 5..5+n2-1) */
    int32_t slot[MAPCOL_OBJS];  /* DAT_0438bfb8 */
    float   rot[MAPCOL_OBJS];   /* DAT_0438c008 */
    float   x[MAPCOL_OBJS];     /* DAT_0438c058 */
    float   y[MAPCOL_OBJS];     /* DAT_0438c0a8 */
    float   z[MAPCOL_OBJS];     /* DAT_0438c0f8 */
} mapcol_objs_t;

/* DAT_0438c150..: moving collision objects (on[0] = count). */
typedef struct {
    int32_t on[100];            /* DAT_0438c150 */
    int32_t slot[100];          /* DAT_0438c2e0 */
    float   x[MAPCOL_MOVERS];   /* DAT_0438c470 */
    float   y[MAPCOL_MOVERS];   /* DAT_0438c538 */
    float   z[MAPCOL_MOVERS];   /* DAT_0438c600 */
} mapcol_movers_t;

/* The query output (FUN_00432e50 a4 / FUN_00433674 a8), 0x44 bytes. */
typedef struct {
    float   n[3];          /* +0x00 unit normal, y negated */
    float   height;        /* +0x0c ground height (FUN_00432e50) */
    int32_t tri;           /* +0x10 */
    int32_t slot;          /* +0x14 */
    int32_t obj;           /* +0x18 object loop index (FUN_00433674) */
    int32_t flag;          /* +0x1c 1 = water (type 5) above, 2 = type 6 (FUN_00432e50) */
    float   slope_h;       /* +0x20 type 5/6 plane height (FUN_00432e50) */
    int32_t _pad24[4];
    float   plane[4];      /* +0x34 */
} mapcol_hit_t;

extern mapcol_slot_t   g_mapcol_slot[MAPCOL_SLOTS];
extern mapcol_objs_t   g_mapcol_objs;
extern mapcol_movers_t g_mapcol_movers;
extern int32_t         g_mapcol_wall_pass;   /* DAT_0438bed0: type-7 faces collide */

/* FUN_0043244c: every slot's count = 0 (and the face-type scratch). */
void mapcol_clear_all(void);

/* FUN_00471d45 + FUN_0043289b: parse one `.x` (the text bytes of the file) into
 * `slot`.  `large_pad` = the triangle-AABB padding of FUN_00432ac6
 * (maptype of stage DAT_0438b4dc >= 1).  Returns 1 (retail returns 0 only when
 * fopen fails, which the caller models by not calling this). */
int mapcol_load_x(int slot, const void *text, size_t len, int large_pad);

/* FUN_00432e50: 1 = ground found (out filled), 0 = none (out->flag/slope_h may
 * still be written). */
int mapcol_ground(float x, float y, float z, mapcol_hit_t *out);

/* FUN_00433674: segment p → p+d; 1 = hit (*frac in [.., 1], out filled). */
int mapcol_ray(float px, float py, float pz, float dx, float dy, float dz,
               float *frac, mapcol_hit_t *out);

/* *DAT_068dd2f0 — the current stage's maptype (stage_palette.h). */
int32_t mapcol_maptype(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_MAP_COLLISION_H */
