/*
 * scene_floor.h — engine floor asset loader (FUN_004747dc @ 0x4747dc,
 *                  142 bytes). Inner body for the B82 secondary worker
 *                  thread (LAB_00452b82), paired with the FUN_00452dc1
 *                  spawner.
 *
 * Direct structural sibling of scene_walls (FUN_0047474e / B3E) —
 * same 15-iteration loop, same 1-bit predicate inverted by `param`,
 * same per-stage selector idiom. Differences:
 *
 *   - Filename table at .rdata 0x5c7fb8..0x5c7ff4 (15 entries
 *     PTR_s_yuka_ita2_bmp_005c7fb8 family), strings at
 *     0x5ca220..0x5ca314.
 *   - Sprintf format `"xfile/floor/%s"` (engine s_xfile_floor__s_005ca324).
 *   - Per-stage selector at offset 0x580 of the 0x2dfc8-byte stage
 *     record (engine `*(int *)(&DAT_04510580 + DAT_0438b1e0 * 0x2dfc8)`)
 *     — i.e. exactly 4 bytes past the wall selector at offset 0x57c.
 *   - Destination sprite_t array at engine DAT_073b18d8 (stride 0x10).
 *
 * Worker_load wiring:
 *
 *   `scene_floor_init(dev)` caches the D3D device and registers
 *   `scene_floor_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B82, …)`. The body
 *   re-reads `g_worker_sec_param` to recover the param (worker_load
 *   callbacks are `void (*)(void)`) and dispatches to the test-
 *   injectable pure-C entry point `scene_floor_load_with`.
 *
 * Per-stage selector (engine DAT_04510580 + per-stage 0x2dfc8 stride):
 *
 *   Stage state isn't ported yet, so `g_scene_floor_selector` is a
 *   standalone int32 (BSS-zero default). When the stage system lands,
 *   its loader writes this on each stage transition before spawning the
 *   B82 worker.
 *
 * Test injection:
 *
 *   `scene_floor_load_with(load_fn, userdata, param)` accepts a custom
 *   loader function so the unit tests can record which slots were
 *   requested without dragging in D3D. The load_fn signature takes only
 *   the path + slot index (no sprite_t — that's gated behind _WIN32
 *   below). The Win32 build's body wraps sprite_load against
 *   g_scene_floor[slot] internally.
 */

#ifndef OPENRECET_SCENE_FLOOR_H
#define OPENRECET_SCENE_FLOOR_H

#include <stdint.h>

/* Engine table length: (0x5c7ff4 - 0x5c7fb8) / 4 = 15. */
#define SCENE_FLOOR_COUNT 15

/* Per-stage floor selector index. Engine: offset 0x580 of the current
 * stage's 0x2dfc8-byte stage-state record. Zero by default (BSS); the
 * stage loader (when it ports) will write this before kicking the B82
 * worker. Range [0, SCENE_FLOOR_COUNT); out-of-range values match the
 * engine's "no slot matches" behaviour (param==0 loads nothing,
 * param==1 loads everything). */
extern int32_t g_scene_floor_selector;

/* Optional injected loader for tests. Receives the formatted path
 * ("xfile/floor/<name>") and the destination slot index. Return value
 * is ignored — tests use it to record dispatches. */
typedef int (*scene_floor_load_fn)(const char *path, int slot, void *userdata);

/* Pure-C body — engine FUN_004747dc end-to-end. Loops the 15 slots,
 * applies the per-stage-selector predicate inverted by `param`, and
 * calls `load_fn` for each selected slot. Returns the number of
 * dispatches made. NULL `load_fn` is a counting-only dry run. */
int  scene_floor_load_with(scene_floor_load_fn load_fn,
                           void *userdata,
                           int param);

/* Inspection helpers — exposed for tests. */
const char *scene_floor_filename(int slot);  /* NULL if out of range */
const char *scene_floor_format_string(void); /* "xfile/floor/%s" */

/* Reset module state — clears the selector and (on Win32) zeroes the
 * sprite_t handle array. Tests only. */
void scene_floor_reset(void);

#ifdef _WIN32

#include "sprite.h"

/* 15 destination sprite slots (engine DAT_073b18d8, stride 0x10).
 * Our sprite_t is 12 bytes — the engine's spare 4 bytes per slot hold
 * the sprite_load format flag (3, here) which the openrecet port
 * doesn't carry. */
extern sprite_t g_scene_floor[SCENE_FLOOR_COUNT];

struct IDirect3DDevice8;

/* Cache the D3D device and register the body via
 * worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B82, ...). Call once
 * at boot, after the device is created. Idempotent. */
void scene_floor_init(struct IDirect3DDevice8 *dev);

/* Foreground "load just the selected floor" pass — engine
 * FUN_004747dc(0) from FUN_00474a9a L73067. */
int  scene_floor_load_foreground_win32(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_FLOOR_H */
