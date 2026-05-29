/*
 * scene_map_meshes.h — engine per-stage "map" mesh loader
 *                      (FUN_00474681 @ 0x474681, 123 bytes).
 *
 * The stage's 3D geometry pool.  FUN_00474681 walks the parsed `map:`
 * filenames of the current stage record and `mesh_load`s each `.x` into
 * the engine's `DAT_068dcca0[]` array (20 slots × 0x28 bytes).  For the
 * HOUSE stage (stage:0-1, index 0) this loads 11 meshes:
 *
 *     map[0]  = xfile/shop/shop_1st.x        ← the shop ROOM (48 submeshes)
 *     map[1]  = xfile/jutan/shop_jutan.x     ← the floor CARPET
 *     map[2]  = xfile/jutan/shop_jutan_tora.x
 *     map[3]  = xfile/table/shop_table01.x   map[4] = shop_table02.x
 *     map[5..10] = xfile/jihanki/shop_*jihanki*.x  (vending machines)
 *
 * `FUN_00457714` draw loop A (PII.3c) renders the shop interior
 * background by drawing 2 phase-1 instances out of this pool — mesh
 * indices [0, 1] for HOUSE (room + carpet), per retail ground truth
 * (tools/dump_phase1_groundtruth.py → runs/phase1-groundtruth.json).
 *
 * Loading ALL map meshes (not just the 2 drawn) is faithful to the
 * engine AND required: every mesh_load call populates the global
 * texture cache (g_mesh_tex_cache), and draw loop A's per-cache-slot
 * classify→SetTexture path depends on the room/carpet textures being
 * present there.
 *
 * Engine layout: the map[] spec source is `&DAT_068dd60c + stage*0x1b3c`
 * — i.e. the stage record's `map[]` field at +0x314, stride 0x100
 * (STAGE_NAME_MAX) per entry.  We source the same strings from the
 * parsed `g_stage.records[stage].map[]` (src/tables_stage.c).
 *
 * NOT ported: the per-mesh `FUN_00471d45(spec, i)` companion call
 * (2777 B — parses per-map collision/bounds + aux matrices).  It is not
 * render-critical for HOUSE: draw loop A's distance cull threshold is
 * 1000 world units while the shop room/carpet sit at the origin ~25
 * units from the camera, so the cull never rejects the 2 instances.
 * Likewise the `DAT_068dcf98` single-mesh path (FUN_00455191) is gated
 * off for HOUSE (palette+0x108 == 0).
 */

#ifndef OPENRECET_SCENE_MAP_MESHES_H
#define OPENRECET_SCENE_MAP_MESHES_H

#include <stdint.h>

/* Engine DAT_068dcca0 array: 20 slots × 0x28 B.  Matches STAGE_MAP_SLOTS
 * (tables_stage.h) — the parsed map[] capacity. */
#define SCENE_MAP_MESH_SLOTS 20

/* HOUSE uses stage index 0 (stage:0-1) — the port hardcodes this the
 * same way stage_palette does (DAT_0438b4dc == 0 on new-game HOUSE)
 * until the stage-transition system lands. */
#define SCENE_MAP_STAGE_HOUSE 0

/* Number of `map:` meshes the given stage declares (g_stage record
 * map_count).  Pure C — testable without D3D. */
int scene_map_meshes_count(int stage);

/* Test-injection body: iterate the stage's map[] entries and call
 * `load_fn(path, slot, userdata)` for each, in slot order.  Returns the
 * number of load_fn calls.  Mirrors FUN_00474681's mesh-load loop
 * without dragging in mesh_load / D3D. */
typedef int (*scene_map_load_fn)(const char *path, int slot, void *userdata);
int scene_map_meshes_load_with(int stage, scene_map_load_fn load_fn,
                               void *userdata);

#ifdef _WIN32
struct IDirect3DDevice8;
#include "mesh.h"

/* The loaded mesh pool (engine DAT_068dcca0[]).  NULL slots = not
 * loaded / load failed. */
extern mesh_t *g_scene_map_meshes[SCENE_MAP_MESH_SLOTS];

/* FUN_00474681 — load the HOUSE stage's map[] meshes into
 * g_scene_map_meshes[], finalizing each on `dev`.  Returns the count of
 * successfully loaded meshes. */
int scene_map_meshes_load_house(struct IDirect3DDevice8 *dev);

/* Free all loaded map meshes and clear the array. */
void scene_map_meshes_reset(void);

/* Slot accessor for draw loop A; NULL if out of range or unloaded. */
mesh_t *scene_map_meshes_get(int idx);
#else
void scene_map_meshes_reset(void);
#endif

#endif /* OPENRECET_SCENE_MAP_MESHES_H */
