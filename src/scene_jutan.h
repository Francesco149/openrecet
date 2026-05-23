/*
 * scene_jutan.h — engine jutan/rug asset loader (FUN_0047486a @
 *                  0x47486a, 142 bytes). Inner body for the BC6
 *                  secondary worker thread (LAB_00452bc6), paired with
 *                  the FUN_00452dfd spawner.
 *
 * Direct structural sibling of scene_walls (B3E) and scene_floor (B82).
 * Same 15-iteration… wait, 8-iteration loop. Differences vs walls:
 *
 *   - Filename table at .rdata 0x5c7ff4..0x5c8014 (8 entries
 *     PTR_s_shop_jutan01_tga_005c7ff4 family), strings at
 *     0x5ca334..0x5ca3c4. NB: only 8 entries here, vs 15 for walls
 *     and floors.
 *   - Sprintf format `"xfile/jutan/%s"` (engine s_xfile_jutan__s_005ca3d8).
 *   - Per-stage selector at offset 0x584 of the 0x2dfc8-byte stage
 *     record (engine `*(int *)(&DAT_04510584 + DAT_0438b1e0 * 0x2dfc8)`)
 *     — 4 bytes past the floor selector at 0x580 (and 8 past walls @
 *     0x57c).
 *   - Destination sprite_t array at engine DAT_073ac728 (stride 0x10).
 *
 * Worker_load wiring:
 *
 *   `scene_jutan_init(dev)` caches the D3D device and registers
 *   `scene_jutan_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_BC6, …)`. The body
 *   calls `scene_jutan_load_with(…, 1)` — engine LAB_00452bc6 passes a
 *   hard-coded literal `1` to FUN_0047486a (same push-esi=1 pattern
 *   the wall + floor siblings use; see scene_jutan.c body comment for
 *   the asm citation).
 *
 * Per-stage selector (engine DAT_04510584 + per-stage 0x2dfc8 stride):
 *
 *   Stage state isn't ported yet, so `g_scene_jutan_selector` is a
 *   standalone int32 (BSS-zero default). When the stage system lands,
 *   its loader writes this on each stage transition before spawning the
 *   BC6 worker.
 */

#ifndef OPENRECET_SCENE_JUTAN_H
#define OPENRECET_SCENE_JUTAN_H

#include <stdint.h>

/* Engine table length: (0x5c8014 - 0x5c7ff4) / 4 = 8. */
#define SCENE_JUTAN_COUNT 8

/* Per-stage jutan/rug selector index. Engine: offset 0x584 of the
 * current stage's 0x2dfc8-byte stage-state record. Zero by default
 * (BSS). Range [0, SCENE_JUTAN_COUNT); out-of-range values match the
 * engine's "no slot matches" behaviour. */
extern int32_t g_scene_jutan_selector;

/* Optional injected loader for tests. Receives the formatted path
 * ("xfile/jutan/<name>") and the destination slot index. Return value
 * is ignored — tests use it to record dispatches. */
typedef int (*scene_jutan_load_fn)(const char *path, int slot, void *userdata);

/* Pure-C body — engine FUN_0047486a end-to-end. Loops the 8 slots,
 * applies the per-stage-selector predicate inverted by `param`, and
 * calls `load_fn` for each selected slot. Returns the number of
 * dispatches made. NULL `load_fn` is a counting-only dry run. */
int  scene_jutan_load_with(scene_jutan_load_fn load_fn,
                           void *userdata,
                           int param);

/* Inspection helpers — exposed for tests. */
const char *scene_jutan_filename(int slot);  /* NULL if out of range */
const char *scene_jutan_format_string(void); /* "xfile/jutan/%s" */

/* Reset module state — clears the selector and (on Win32) zeroes the
 * sprite_t handle array. Tests only. */
void scene_jutan_reset(void);

#ifdef _WIN32

#include "sprite.h"

/* 8 destination sprite slots (engine DAT_073ac728, stride 0x10). */
extern sprite_t g_scene_jutan[SCENE_JUTAN_COUNT];

struct IDirect3DDevice8;

/* Cache the D3D device and register the body via
 * worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_BC6, ...). Call once
 * at boot, after the device is created. Idempotent. */
void scene_jutan_init(struct IDirect3DDevice8 *dev);

/* Foreground "load just the selected jutan/rug" pass — engine
 * FUN_0047486a(0) from FUN_00474a9a L73068. */
int  scene_jutan_load_foreground_win32(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_JUTAN_H */
