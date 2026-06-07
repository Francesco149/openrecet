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
 * T2 UPDATE (2026-06-07): the LIVE world-map path is the PRIMARY worker,
 * not this secondary C96 body. The door-exit (T1) spawns the primary
 * worker with g_scene_state == 8, whose engine jump-table case @ 0x452984
 * is `FUN_0049de20; FUN_004735ad` — so scene_worldmap_init now ALSO
 * registers worker_load_set_cb(8, ...) running the state-machine init
 * (scene_worldmap_init_state) followed by the BMP load. The secondary C96
 * body above stays BMP-only + dormant (no spawner caller); it is a
 * separate, still-unused load path.
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

/* ─── world-map (mode 8) destination model + scene-init ──────────────────
 *
 * The engine's mode-8 (WORLD MAP) primary worker body is
 *   FUN_0049de20 (scene init)  →  FUN_004735ad (texture load, above)
 * (primary jump-table @ 0x452984, dispatched on DAT_0438b1c0 == 8).
 *
 * FUN_0049de20 populates a 7-destination model + a per-dest state array
 * driven by the tutorial-progress flags, then snaps the shared cursor to
 * the selected destination's marker. See docs/findings/town-map-RE.md §3-4. */

/* Engine DAT_005fd588 = 7 destinations. */
#define SCENE_WORLDMAP_DEST_COUNT 7

/* Per-destination static layout. Engine .data table DAT_005fd590, stride
 * 0xc = {float x; float y; int sprite_row} (DAT_005fd594 = +4 (y),
 * DAT_005fd598 = +8 (mappoint sprite row)). Extracted from vendor/unpacked
 * @ 0x5fd590 (7 entries): screen X/Y of the marker + which mappoint.tga row
 * to draw for it. */
typedef struct {
    float x;            /* DAT_005fd590[i] — marker screen X */
    float y;            /* DAT_005fd594[i] — marker screen Y */
    int   sprite_row;   /* DAT_005fd598[i] — mappoint.tga row */
} scene_worldmap_dest_t;

extern const scene_worldmap_dest_t
    scene_worldmap_dest_layout[SCENE_WORLDMAP_DEST_COUNT];

/* Engine .data grid DAT_005fd620 (3 col × 5 row, index = col + row*3 →
 * dest-id, -1 = empty). Drives the cursor-nav (T4). Extracted @ 0x5fd620
 * (15 ints). */
#define SCENE_WORLDMAP_GRID_COLS 3
#define SCENE_WORLDMAP_GRID_ROWS 5
extern const int
    scene_worldmap_grid[SCENE_WORLDMAP_GRID_COLS * SCENE_WORLDMAP_GRID_ROWS];

/* FUN_0049de0e / FUN_0049de18 — set / read the selected destination
 * (engine DAT_09643684). The door-exit stage-2 calls set(0) before the
 * worker spawns; FUN_0049de20 reads it to snap the cursor. */
void scene_worldmap_set_sel_dest(int dest);
int  scene_worldmap_sel_dest(void);

/* Port of FUN_0049de20 — world-map scene-init. Run on the worker thread
 * for mode 8 (before the texture load). Populates the destination set +
 * the per-dest state array (tutorial gating) + snaps the shared cursor.
 * Pure-C; reads the live working save arena (save_work). */
void scene_worldmap_init_state(void);

/* Destination-model accessors (for the render/sim chips + tests). */
int   scene_worldmap_dest_count(void);     /* DAT_005fd588 (=7 after init) */
int   scene_worldmap_dest_state(int i);    /* DAT_09643588[i] (0 dis/1 norm/2 hi) */
int   scene_worldmap_dest_closed(int i);   /* DAT_0964362c[i] ("Closed") */
int   scene_worldmap_dest_pos(int i);      /* DAT_096435d8[i] (dest→map-pos) */
float scene_worldmap_entry_timer(void);    /* _DAT_09643628 (intro timer) */

/* Per-frame update — the mode-8 update dispatch (sim.c case 8). T2 stub;
 * T4 ports the body FUN_0049e163 (entry timer + 3×5 cursor-nav + Z-select).
 * Pure-C. */
void scene_worldmap_sim(void);

/* Reset module state — the dest model + (on Win32) the sprite_t handle
 * array. Tests only. */
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
 * Also registers the PRIMARY worker case-8 body (engine primary jump-table
 * @ 0x452984 = FUN_0049de20 → FUN_004735ad), which the door-exit stage-2
 * drives via worker_load_spawn() with g_scene_state == 8. */
void scene_worldmap_init(struct IDirect3DDevice8 *dev);

/* Per-frame render — the mode-8 render dispatch (main.c render switch
 * case 8). T2 stub; T3 ports the body FUN_0049e3a3 (worldmap bg
 * time-of-day crossfade + mappoint markers + "Closed" labels). */
void scene_worldmap_render(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_WORLDMAP_H */
