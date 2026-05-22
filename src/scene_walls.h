/*
 * scene_walls.h — engine wall asset loader (FUN_0047474e @ 0x47474e,
 *                  142 bytes). Inner body for the B3E secondary worker
 *                  thread (LAB_00452b3e), paired with the FUN_00452d85
 *                  spawner.
 *
 * The engine loads one of two complementary slices of the 15-entry
 * wall texture table per call:
 *
 *   - `param == 0`: load ONLY the slot whose index matches the
 *     current stage's wall-selector (engine
 *     `*(int *)(&DAT_0451057c + DAT_0438b1e0 * 0x2dfc8)`). This is the
 *     "load the destination room's wall" pass.
 *
 *   - `param != 0`: load EVERY slot EXCEPT the selector. This is the
 *     "load all the other variations" background pass.
 *
 * The 15 filenames are pre-baked in `.rdata` at 0x5c7f78..0x5c7fb4
 * (engine `PTR_s_kabe_sikkui_bmp_005c7f78` family). Extracted from
 * `vendor/unpacked/recettear.unpacked.exe` via tools/analyze/pe.py and
 * mirrored as a const string array in `src/scene_walls.c`.
 *
 * Sprintf format `"xfile/wall/%s"` (engine s_xfile_wall__s_005ca210)
 * — note `xfile/` even though wall assets are BMP textures, not .x
 * meshes. Same directory used for organisational consistency with
 * the engine's .x mesh tree.
 *
 * Per-stage selector (engine DAT_0451057c + per-stage 0x2dfc8 stride):
 *
 *   The engine reads the selector from offset 0x57c of the current
 *   stage's "stage state" record. Stage state isn't ported yet, so we
 *   expose `g_scene_walls_selector` as a single int32 (BSS-zero
 *   default). When the stage system lands, its loader writes this on
 *   each stage transition before spawning the B3E worker.
 *
 * Worker_load wiring:
 *
 *   `scene_walls_init(dev)` caches the D3D device and registers
 *   `scene_walls_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B3E, …)`. The body
 *   calls `scene_walls_load_with(…, 1)` — the engine's LAB_00452b3e
 *   passes a hard-coded literal `1` to FUN_0047474e (the runtime-
 *   variable `g_worker_sec_param` is consumed elsewhere for the fade-
 *   kick gate, not by this loader). See scene_walls.c body comment
 *   for the asm citation.
 *
 * Test injection:
 *
 *   `scene_walls_load_with(load_fn, userdata, param)` accepts a
 *   custom loader function so the unit tests can record which slots
 *   were requested without dragging in D3D. The load_fn signature
 *   takes only the path + slot index (no sprite_t — that's gated
 *   behind _WIN32 below). The Win32 build's body wraps sprite_load
 *   against g_scene_walls[slot] internally.
 */

#ifndef OPENRECET_SCENE_WALLS_H
#define OPENRECET_SCENE_WALLS_H

#include <stdint.h>

/* Engine table length: (0x5c7fb4 - 0x5c7f78) / 4 = 15. */
#define SCENE_WALLS_COUNT 15

/* Per-stage wall selector index. Engine: offset 0x57c of the current
 * stage's 0x2dfc8-byte stage-state record. Zero by default (BSS); the
 * stage loader (when it ports) will write this before kicking the B3E
 * worker. Range [0, SCENE_WALLS_COUNT); out-of-range values match the
 * engine's "no slot matches" behaviour (param==0 loads nothing,
 * param==1 loads everything). */
extern int32_t g_scene_walls_selector;

/* Optional injected loader for tests. Receives the formatted path
 * ("xfile/wall/<name>") and the destination slot index. Return value
 * is ignored — tests use it to record dispatches. */
typedef int (*scene_walls_load_fn)(const char *path, int slot, void *userdata);

/* Pure-C body — engine FUN_0047474e end-to-end. Loops the 15 slots,
 * applies the per-stage-selector predicate inverted by `param`, and
 * calls `load_fn` for each selected slot. Returns the number of
 * dispatches made. NULL `load_fn` is a counting-only dry run. */
int  scene_walls_load_with(scene_walls_load_fn load_fn,
                           void *userdata,
                           int param);

/* Inspection helpers — exposed for tests. */
const char *scene_walls_filename(int slot);  /* NULL if out of range */
const char *scene_walls_format_string(void); /* "xfile/wall/%s" */

/* Reset module state — clears the selector and (on Win32) zeroes the
 * sprite_t handle array. Tests only. */
void scene_walls_reset(void);

#ifdef _WIN32

#include "sprite.h"

/* 15 destination sprite slots (engine DAT_073cc630, stride 0x10).
 * Our sprite_t is 12 bytes — the engine's spare 4 bytes per slot hold
 * the sprite_load format flag (3, here) which the openrecet port
 * doesn't carry. */
extern sprite_t g_scene_walls[SCENE_WALLS_COUNT];

struct IDirect3DDevice8;

/* Cache the D3D device and register the body via
 * worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B3E, ...). Call once
 * at boot, after the device is created. Idempotent. */
void scene_walls_init(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_WALLS_H */
