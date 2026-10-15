/*
 * dungeon_objects.h — the stage object table in retail layout: the placed
 * objects of a dungeon floor (its portals, the marked cells' objects).
 *
 * Retail: 512 records × 0x2a0 bytes at DAT_0695ef78.  A record is free when its
 * kind (+0x8c) is -1.  Kinds the stage init places: 8 = the next-floor portal
 * (the exit cell), 6 = the return-to-town portal, 4/5 = the marked cells'
 * objects (alternating).
 *
 *   FUN_0044ad81  dobj_spawn   first free record: kind, position snapped to the
 *                              ground (FUN_00432e50), scale, yaw
 *   FUN_00436f97  dobj_reset   (its loop over the table) every record free
 *
 * The record is raw dwords + named indexes (only the fields a ported function
 * touches get a name), so `rhost --hook shadow:` compares whole records.
 */
#ifndef OPENRECET_DUNGEON_OBJECTS_H
#define OPENRECET_DUNGEON_OBJECTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOBJ_COUNT  512
#define DOBJ_DWORDS 168   /* 0x2a0 bytes */

typedef union {
    int32_t i[DOBJ_DWORDS];
    float   f[DOBJ_DWORDS];
} dobj_t;                 /* DAT_0695ef78 + i·0x2a0 */

extern dobj_t g_dobj[DOBJ_COUNT];

/* named fields (dword indexes) */
#define DOBJ_POS     0     /* float[3] */
#define DOBJ_HOME    3     /* float[3] the spawn point */
#define DOBJ_HIT     10    /* mapcol_hit_t (FUN_00432e50 output, 17 dwords); [13] = ground height */
#define DOBJ_D27     27    /* zeroed by the stage init */
#define DOBJ_YAW     28
#define DOBJ_D29     29
#define DOBJ_D30     30
#define DOBJ_D31     31
#define DOBJ_SCALE   32    /* float[3] */
#define DOBJ_KIND    35    /* -1 = free */
#define DOBJ_D36     36
#define DOBJ_RING    53    /* 100 dwords, all = [155] at a reset */
#define DOBJ_ATTR    153   /* k_dobj_kind[kind][0] */
#define DOBJ_STATE   154   /* 3 = spent (the proximity checks skip it) */
#define DOBJ_PARAM   155
#define DOBJ_LINKS   156   /* 10 dwords, -1 */

/* FUN_00436f97's table reset (the loop at 0x437b9b): kind = -1, [27] = 0,
 * [154] = 0, the ring = [155]. */
void dobj_reset(void);

/* FUN_0044ad81: returns the record index (0 if the table is full — retail
 * MessageBoxes and returns 0). */
int  dobj_spawn(const float pos[3], const float scale[3], float yaw, int32_t kind);
void dobj_tick(void);                                              /* FUN_0043a5d9 */

/* FUN_0044b257: spawn an object of `kind` for `role`, spent (state 3); its
 * slot goes to DAT_06a47054[role] */
void dobj_spawn_special(int role, int32_t kind, const float pos[3], const float scale[3], float yaw);
extern int32_t g_dobj_special[4];   /* DAT_06a47054 */
extern int32_t g_dobj_special_up;   /* DAT_06a4705c */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_DUNGEON_OBJECTS_H */
