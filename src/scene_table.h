/*
 * scene_table.h — engine shop-table asset loader (FUN_004748f8 @
 *                  0x4748f8, 169 bytes). Inner body for the C0A
 *                  secondary worker thread (LAB_00452c0a), paired
 *                  with the FUN_00452e39 spawner.
 *
 * Eighth and final mesh-using sibling of the wall/floor/jutan loader
 * trio. Structurally identical idiom — per-stage selector predicate
 * inverted by `param` — but instead of loading a single sprite per
 * matching slot via FUN_0047193c, it loads a PAIR of `.x` meshes via
 * FUN_00472836 (our `mesh_load`).
 *
 *   - 8 pairs of `.x` file names at .rdata 0x5c8018..0x5c8058 (16
 *     pointer entries; pairs are (0,1), (2,3), … (14,15) — see the
 *     unrolled `ppuVar3[-1]` + `*ppuVar3` per-iter pattern in the
 *     engine).
 *   - Sprintf format `"xfile/table/%s"` (engine s_xfile_table__s_005ca500
 *     and DAT_005ca510 — same string at two addresses).
 *   - Per-stage selector at offset 0x588 of the 0x2dfc8-byte stage
 *     record (engine `*(int *)(&DAT_04510588 + DAT_0438b1e0 * 0x2dfc8)`)
 *     — i.e. 4 bytes past the jutan selector at offset 0x584.
 *   - Destination mesh array at engine `DAT_073b1ac8` (16 slots × 0x28
 *     bytes/slot = 0x280 = 640 bytes); the engine declares the symbol
 *     as `DAT_073b1af0` at the SECOND slot and uses `local_8 + -0x28`
 *     to address the first half of each pair, which is the same layout
 *     read backwards. We expose the real 16-slot array start.
 *
 * Worker_load wiring:
 *
 *   `scene_table_init(dev)` caches the D3D device and registers
 *   `scene_table_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C0A, …)`. The body
 *   calls `scene_table_load_with(…, 1)` — the engine's hard-coded
 *   "load everything except the selector" param at LAB_00452c0a.
 *
 * Test injection:
 *
 *   `scene_table_load_with(load_fn, userdata, param)` accepts a custom
 *   loader function so tests can record which pairs were requested
 *   without dragging in mesh_load / D3D. The load_fn receives the
 *   formatted path plus the slot index (0..15) so tests can verify
 *   both halves of each pair land in their expected slots.
 */

#ifndef OPENRECET_SCENE_TABLE_H
#define OPENRECET_SCENE_TABLE_H

#include <stdint.h>

/* 8 pairs × 2 names = 16 mesh slots. */
#define SCENE_TABLE_PAIR_COUNT 8
#define SCENE_TABLE_SLOT_COUNT 16

extern int32_t g_scene_table_selector;

typedef int (*scene_table_load_fn)(const char *path, int slot, void *userdata);

/* Pure-C body — engine FUN_004748f8 end-to-end. Iterates the 8 pairs,
 * applies the per-stage-selector predicate inverted by `param`, and
 * calls `load_fn(path, slot, userdata)` twice per matching pair (slot
 * = pair*2 then pair*2+1). Returns the number of dispatches. NULL
 * `load_fn` runs as a counting-only dry run. */
int  scene_table_load_with(scene_table_load_fn load_fn,
                           void *userdata,
                           int param);

const char *scene_table_filename(int slot);   /* NULL if out of range */
const char *scene_table_format_string(void);  /* "xfile/table/%s" */

void scene_table_reset(void);

#ifdef _WIN32
#include "mesh.h"

struct IDirect3DDevice8;

/* 16 destination mesh slots, each a mesh_t* (NULL until loaded).
 * Engine's DAT_073b1ac8 holds 16 × 0x28-byte D3DX-mesh dest structs;
 * our mesh_t replaces those wholesale. */
extern mesh_t *g_scene_table[SCENE_TABLE_SLOT_COUNT];

void scene_table_init(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_SCENE_TABLE_H */
