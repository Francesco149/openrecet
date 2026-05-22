/*
 * scene_worldmap.h — engine world-map asset BMP loader (FUN_004735ad
 *                    @ 0x4735ad, 98 bytes). Inner-body BMP-loader HALF
 *                    of the C96 secondary-worker thread (LAB_00452c96),
 *                    paired with the FUN_00452eb1 spawner.
 *
 * The C96 inner body is the only secondary thread proc besides C4E
 * that calls two engine functions in sequence:
 *
 *   1. FUN_0049de20 — world-map state-machine init (374 bytes). Sets
 *      up the per-stage cursor position, walk-availability flags for
 *      each of the 7 map nodes, and a few selector globals. Deeply
 *      entangled with the INGAME stage-table machinery — calls
 *      FUN_00435c98 (309b), FUN_0045de68 (433b), FUN_0043561a,
 *      FUN_00435693 — and reads ~8 stage-table globals
 *      (DAT_0450f3f9 / f408 / f414 / fb84 / fb88 / 5a0 / 4fc, all
 *      indexed by `DAT_0438b1e0 * 0xb7f2` or `* 0x2dfc8`). Deferred
 *      with the rest of the world-map state machine.
 *
 *   2. FUN_004735ad — this module. Loads 4 fixed BMP/TGA textures
 *      (3 world-map backgrounds for the day/dusk/night cycle + the
 *      "mappoint" overlay used for the per-node markers). No selector,
 *      no per-stage variation — same 4 textures every time.
 *
 * C96 body STATUS: half-wired. `scene_worldmap_init` registers a body
 * that runs the BMP loader only. The state-machine half (FUN_0049de20)
 * will fold in as a sequential pre-call when its INGAME deps land. The
 * half-port is dormant in practice because no spawner caller exists
 * yet (the eb1 spawner has no port-side caller as of 2026-05-23), so
 * the only observable difference between "unwired" and "BMP-half wired"
 * is the worker_load_get_sec_body(C96) return value being non-NULL.
 *
 * Engine destinations:
 *   - worldmap_nomal.bmp  → DAT_073da000 (scene-1 generic sprite slot 0)
 *   - worldmap_yugata.bmp → DAT_073da010 (scene-1 generic sprite slot 1)
 *   - worldmap_night.bmp  → DAT_073da020 (scene-1 generic sprite slot 2)
 *   - mappoint.tga        → DAT_073aa7d8 (dedicated mappoint slot)
 *
 *   The first three reuse the scene-1 generic 20-entry sprite array
 *   (DAT_073da000, stride 0x10) — same array the title scene clobbers
 *   when it's active. Our port keeps a private `g_scene_worldmap[4]`
 *   instead, mirroring the scene_title pattern: each scene module owns
 *   its sprites rather than sharing the engine's per-scene-reused
 *   generic slots.
 *
 * Test injection follows the same convention as scene_floor / scene_jutan
 * / scene_walls — `scene_worldmap_load_with(load_fn, userdata)` accepts
 * a custom loader function so unit tests can record paths + slot indices
 * without dragging in D3D.
 */

#ifndef OPENRECET_SCENE_WORLDMAP_H
#define OPENRECET_SCENE_WORLDMAP_H

#include <stdint.h>

/* Engine: exactly 4 fixed sprite_load calls in FUN_004735ad. No loop,
 * no selector — same 4 textures every fire. */
#define SCENE_WORLDMAP_COUNT 4

enum {
    SCENE_WORLDMAP_TEX_NOMAL    = 0, /* bmp/worldmap_nomal.bmp  1024x512 */
    SCENE_WORLDMAP_TEX_YUGATA   = 1, /* bmp/worldmap_yugata.bmp 1024x512 */
    SCENE_WORLDMAP_TEX_NIGHT    = 2, /* bmp/worldmap_night.bmp  1024x512 */
    SCENE_WORLDMAP_TEX_MAPPOINT = 3, /* bmp/mappoint.tga         256x1024 */
};

/* Per-slot metadata. Used by the loader; exposed for tests. */
typedef struct {
    const char *path;
    uint32_t    expected_w;
    uint32_t    expected_h;
} scene_worldmap_asset_t;

extern const scene_worldmap_asset_t scene_worldmap_assets[SCENE_WORLDMAP_COUNT];

/* Inspection helpers — exposed for tests. */
const char *scene_worldmap_filename(int slot);              /* NULL if out of range */
int         scene_worldmap_dims(int slot,                   /* 0 if out of range; */
                                 uint32_t *w, uint32_t *h); /* 1 otherwise        */

/* Optional injected loader for tests. Receives the path + dims + slot
 * index. Return value is ignored — tests use it to record dispatches. */
typedef int (*scene_worldmap_load_fn)(const char *path,
                                      uint32_t w, uint32_t h,
                                      int slot, void *userdata);

/* Pure-C body — engine FUN_004735ad end-to-end. Dispatches all 4 slots
 * unconditionally (no selector). Returns the number of dispatches made
 * (== SCENE_WORLDMAP_COUNT on full success). NULL `load_fn` is a
 * counting-only dry run. */
int scene_worldmap_load_with(scene_worldmap_load_fn load_fn,
                             void *userdata);

/* Reset module state — (on Win32) zeroes the sprite_t handle array.
 * Tests only. */
void scene_worldmap_reset(void);

#ifdef _WIN32

#include "sprite.h"

/* 4 destination sprite slots — our private array. The engine reuses
 * DAT_073da000[0..2] (scene-1 generic, shared with title scene) for
 * the 3 worldmap backgrounds and DAT_073aa7d8 for mappoint; we keep
 * a dedicated array so scenes don't fight over the same slots. */
extern sprite_t g_scene_worldmap[SCENE_WORLDMAP_COUNT];

struct IDirect3DDevice8;

/* Cache the D3D device and register the C96 inner-body BMP-loader half
 * via worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C96, ...). Call
 * once at boot, after the device is created. Idempotent.
 *
 * Note: the C96 body the engine dispatches calls FUN_0049de20 BEFORE
 * FUN_004735ad. This port registers only the BMP-loader half — the
 * state-machine first call is deferred. See header banner. */
void scene_worldmap_init(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_WORLDMAP_H */
