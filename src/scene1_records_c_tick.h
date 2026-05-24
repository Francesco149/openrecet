/*
 * scene1_records_c_tick.h — per-tick integrator for the scene-1
 * "world drop" record table (table C, 200 × 0x25 dw slots, sentinel
 * TYPE == -1 at slot offset 10 dw).
 *
 * Chip C8j.1 (2026-05-24).  Ports engine FUN_0044284b @ 0x44284b
 * (1083 B).  See docs/findings/scene1-record-populators.md for the
 * survey + chip ladder.
 *
 * Slot layout (per allocator FUN_0044aef0 + integrator FUN_0044284b):
 *
 *   slot[0..2]   pos.xyz (float)
 *   slot[3..5]   vel.xyz (float)
 *   slot[9]      pos.y backup (set when ground hit)
 *   slot[10]     TYPE / sentinel (== -1 means free)
 *   slot[11]     age (int, increments per tick, kill at 0xf0)
 *   slot[12]     scale / dt? (float, allocator sets 1.0f)
 *   slot[14]     pickup extra1 (passed to commit_pickup)
 *   slot[15]     pickup extra2
 *   slot[16]     state (0=world-drop physics, 1=age decrement only,
 *                       2=pickup bob — allocator initial)
 *   slot[22]     ground_y (set by ground-query hook)
 *   slot[36]     aux flag (1 → just decrement age)
 *
 * Stride 0x25 dw matches scene1_records.h.  All "unused?" slots between
 * the named offsets are left at allocator-set values (mostly zero) until
 * a future chip identifies them.
 *
 * Helper hooks (function-pointer-mockable for tests + future engine
 * fidelity).  Defaults are stubs that match HOUSE-dormant behavior:
 *
 *   ground_query_fn   : FUN_00432e50 (ground-height query at xz).
 *                       Returns nonzero if pos has ground; writes the
 *                       ground height to *out_y.  Default: returns 0.
 *   terrain_raycast_fn: FUN_00433674 (horizontal raycast at angle).
 *                       Returns nonzero on hit; writes hit_t (0..1)
 *                       and surface normal (n_x, n_z).  Default: 0.
 *   commit_pickup_fn  : FUN_00484dd1 (commit world drop into player
 *                       inventory + play SE).  3-arg signature per the
 *                       engine prototype; Ghidra dropped the 3rd arg
 *                       at the integrator's call site.  Default: no-op.
 *
 * The integrator never reads engine pos/SE globals directly — drag-to-
 * player uses g_scene1_player_pos[] from scene1_particles_tick.h
 * (DAT_056da1d8/dc/e0).  The age==10 sparkle uses scene1_spawn() with
 * type 0x2d, scale 0.15f.
 */
#ifndef SCENE1_RECORDS_C_TICK_H
#define SCENE1_RECORDS_C_TICK_H

#include <stdint.h>

#include "scene1_records.h"   /* SCENE1_RECORDS_C_OFF_TYPE + tables */

#ifdef __cplusplus
extern "C" {
#endif

/* Slot-field offsets in dwords from slot base.  TYPE = 10 lives in
 * scene1_records.h since it's part of the canonical table layout. */
#define SCENE1_RECORDS_C_OFF_POS_X        0
#define SCENE1_RECORDS_C_OFF_POS_Y        1
#define SCENE1_RECORDS_C_OFF_POS_Z        2
#define SCENE1_RECORDS_C_OFF_VEL_X        3
#define SCENE1_RECORDS_C_OFF_VEL_Y        4
#define SCENE1_RECORDS_C_OFF_VEL_Z        5
#define SCENE1_RECORDS_C_OFF_POS_Y_BAK    9
/* SCENE1_RECORDS_C_OFF_TYPE = 10 — see scene1_records.h */
#define SCENE1_RECORDS_C_OFF_AGE          11
#define SCENE1_RECORDS_C_OFF_SCALE        12
#define SCENE1_RECORDS_C_OFF_PICKUP_E1    14
#define SCENE1_RECORDS_C_OFF_PICKUP_E2    15
#define SCENE1_RECORDS_C_OFF_STATE        16
#define SCENE1_RECORDS_C_OFF_GROUND_Y     22
#define SCENE1_RECORDS_C_OFF_AUX          36

/* Hook signatures. */
typedef int  (*scene1_c_ground_query_fn)(float x, float z, float *out_y);
typedef int  (*scene1_c_raycast_fn)(float ox, float oy, float oz,
                                    float dx, float dy, float dz,
                                    float *out_t, float *out_n_x,
                                    float *out_n_z);
typedef void (*scene1_c_commit_pickup_fn)(int type, int extra1, int extra2);

/* Test + future-port hooks.  Setters return prior value so tests can
 * save/restore.  Pass NULL to revert to the default stub. */
scene1_c_ground_query_fn  scene1_records_c_set_ground_query(scene1_c_ground_query_fn fn);
scene1_c_raycast_fn       scene1_records_c_set_raycast(scene1_c_raycast_fn fn);
scene1_c_commit_pickup_fn scene1_records_c_set_commit_pickup(scene1_c_commit_pickup_fn fn);

/* Run the per-tick integrator over all live table C slots.
 * Mirrors engine FUN_0044284b semantics — see header. */
void scene1_records_c_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_C_TICK_H */
