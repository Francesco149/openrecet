/*
 * actor_table.h — the scene actor table in retail layout (enemies, and the
 * people that share the table), and its allocator / enemy spawn.
 *
 * Retail: 128 records × 0xba4 bytes at DAT_0076b970.  A record is free when
 * +0x428 (in use) and +0xad0 are 0 and its +0xb58 cooldown has run out.
 *
 *   FUN_0041f4a6  actor_alloc          first free record, reset to defaults
 *   FUN_0041f319  actor_enemy_level    level for an enemy type on this floor
 *   FUN_0041f903  actor_spawn_enemy    alloc + place + HP from enemy.txt
 *   FUN_00482a51  actor_anim_set       the animation block at +0x00..+0x14
 *
 * The record is raw bytes + named offsets (only the fields a ported function
 * touches get a name), so `rhost --hook shadow:` compares whole records.
 * The older semantic model of the same table, g_scene1_people
 * (scene1_particles_tick.h), predates this and is
 * PORT-DEBT(actor-table-split, FUN_0041f4a6): fold its users onto g_actor.
 */
#ifndef OPENRECET_ACTOR_TABLE_H
#define OPENRECET_ACTOR_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTOR_COUNT 128
#define ACTOR_SIZE  0xba4

typedef struct { uint8_t b[ACTOR_SIZE]; } actor_t;   /* DAT_0076b970 + i·0xba4 */

extern actor_t g_actor[ACTOR_COUNT];
extern int32_t g_actor_alloc_n;   /* DAT_0438b1bc: allocations since the stage init */

/* named fields (byte offsets) */
#define ACT_ANIM        0x000   /* FUN_00482a51 block: anim +0x00, frame f +0x08, +0x0c, +0x10, cur +0x14 */
#define ACT_POS         0x3f0   /* float[3] */
#define ACT_HOME        0x414   /* float[3] spawn point */
#define ACT_YAW         0x420   /* float */
#define ACT_TYPE        0x424   /* enemy type (enemy.txt index) */
#define ACT_INUSE       0x428
#define ACT_HP          0x42c   /* float */
#define ACT_HP2         0x430   /* float */
#define ACT_HP_MAX      0x434   /* float */
#define ACT_PHASE_RND   0xa50   /* rand() & 0x3f */
#define ACT_PLACE_KIND  0xa60   /* FUN_00431d54: 1 = 35/36 or wall-16, 2 = edge-16 */
#define ACT_BUSY        0xad0
#define ACT_LEVEL       0xae0
#define ACT_GROUP_VAR   0xae4   /* enemylist variant of the placement group */
#define ACT_COOLDOWN    0xb58   /* free-slot reuse countdown */
#define ACT_SLOT        0xb3c   /* own index */

static inline int32_t act_geti(const actor_t *a, size_t off) { int32_t v; memcpy(&v, a->b + off, 4); return v; }
static inline float   act_getf(const actor_t *a, size_t off) { float v; memcpy(&v, a->b + off, 4); return v; }
static inline void    act_seti(actor_t *a, size_t off, int32_t v) { memcpy(a->b + off, &v, 4); }
static inline void    act_setf(actor_t *a, size_t off, float v) { memcpy(a->b + off, &v, 4); }

/* Stage inputs FUN_0041f319 / FUN_0041f903 read. */
typedef struct {
    int32_t dungeon;          /* DAT_0438b4c8 */
    int32_t floor;            /* DAT_0438b4cc */
    int32_t event_dungeon;    /* DAT_0438b4d0 (>= 0: the event-floor formulas) */
    int32_t kills;            /* DAT_0438cbe4 (type 34's level bonus / 5) */
    int32_t weak_enemies;     /* work record byte +0x2bc57: HP × 0.1 */
} actor_stage_t;

actor_t *actor_alloc(const actor_stage_t *st);                          /* FUN_0041f4a6 */
int32_t  actor_enemy_level(const actor_stage_t *st, int32_t type);      /* FUN_0041f319 */
actor_t *actor_spawn_enemy(const actor_stage_t *st, float x, float y, float z, int32_t type);   /* FUN_0041f903 */
void     actor_anim_set(actor_t *a, int32_t anim);                      /* FUN_00482a51 */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_ACTOR_TABLE_H */
