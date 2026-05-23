/*
 * scene_sc1.h — engine "scene-1 inventory + chrname + icon loaders"
 *               (FUN_0046bf38 @ 0x46bf38, 230 bytes). Inner body for
 *               the AAB secondary worker thread (LAB_00452aab),
 *               paired with the FUN_00452d07 spawner (and the
 *               FUN_0046c01e pre-spawn hook on AAB).
 *
 * Last of the 9 secondary worker-thread inner bodies. Structurally
 * distinct from the wall/floor/jutan/table siblings:
 *
 *   1. Two unconditional fixed sprite_load calls:
 *        bmp/ivent/ive_window.tga → g_scene_sc1_ive_window  (0x200×0x200)
 *        bmp/ivent/chrname.tga    → g_scene_sc1_chrname     (0x200×0x200)
 *
 *   2. Variable .x mesh loop, count at engine DAT_073a3dfc. Names live
 *      at a 0x100-stride char array (engine DAT_0734fff0), destination
 *      mesh slots at a 0x28-stride array (engine DAT_0735dd88 — the
 *      D3DX mesh-dest struct array). Skipped entirely when count == 0
 *      (its default).
 *
 *   3. Variable sprite loop, count at engine DAT_073a3df0. Same shape
 *      as (2): 0x100-stride name array + 0x10-stride sprite_t dest.
 *      Each sprite loaded at 0x400×0x200 (1024×512). Skipped when
 *      count == 0.
 *
 *   4. Fixed 100-slot sprite array (engine puVar5 from &DAT_073a3ab8 to
 *      &DAT_073a3dd8, stride 8 — two-dword (w, h) size pairs). Each
 *      slot loads `g_scene_sc1_item_names[i]` into `g_scene_sc1_items[i]`
 *      using `g_scene_sc1_item_sizes[i]` as the requested dims. Slot is
 *      skipped when the name is the empty string.
 *
 * Loops (2), (3), and (4) are dormant by default — the count globals
 * and name arrays are BSS-zero, so the only sprite_load fires for the
 * 2 fixed slots in (1). Stage state ports later (item loaders / scene-1
 * init) will populate these and the body picks them up automatically.
 *
 * Worker_load wiring:
 *
 *   `scene_sc1_init(dev)` caches the D3D device and registers
 *   `scene_sc1_body` via
 *   `worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, …)`.
 *
 * Test injection:
 *
 *   `scene_sc1_load_with(load_fn, mesh_fn, userdata)` calls
 *   `load_fn(path, kind, slot)` for each sprite request and
 *   `mesh_fn(path, slot)` for each mesh request. `kind` distinguishes
 *   the four buckets:
 *
 *     SCENE_SC1_KIND_IVE_WINDOW    = 0   (fixed sprite #1)
 *     SCENE_SC1_KIND_CHRNAME       = 1   (fixed sprite #2)
 *     SCENE_SC1_KIND_VAR_SPRITE    = 2   (variable loop, idx 0..count-1)
 *     SCENE_SC1_KIND_ITEM          = 3   (fixed 100-slot, idx 0..99)
 */

#ifndef OPENRECET_SCENE_SC1_H
#define OPENRECET_SCENE_SC1_H

#include <stdint.h>

/* Variable-loop caps (engine has no compile-time bound — count globals
 * gate the loops). 100 is the same cap as the 100-slot fixed item
 * array and matches the FreeList sizes the engine inits at boot. */
#define SCENE_SC1_VAR_MESH_CAP    100
#define SCENE_SC1_VAR_SPRITE_CAP  100
#define SCENE_SC1_ITEM_CAP        100

#define SCENE_SC1_NAME_MAX        256

enum {
    SCENE_SC1_KIND_IVE_WINDOW = 0,
    SCENE_SC1_KIND_CHRNAME    = 1,
    SCENE_SC1_KIND_VAR_SPRITE = 2,
    SCENE_SC1_KIND_ITEM       = 3,
};

/* Variable-mesh state — engine DAT_073a3dfc count + DAT_0734fff0 name
 * array. Stage state writers haven't ported yet, so externs default
 * BSS-zero and the loop runs zero iterations. */
extern int32_t g_scene_sc1_mesh_count;
extern char    g_scene_sc1_mesh_names[SCENE_SC1_VAR_MESH_CAP][SCENE_SC1_NAME_MAX];

/* Variable-sprite state — engine DAT_073a3df0 count + DAT_07350df0
 * name array. Same dormancy. */
extern int32_t g_scene_sc1_sprite_count;
extern char    g_scene_sc1_sprite_names[SCENE_SC1_VAR_SPRITE_CAP][SCENE_SC1_NAME_MAX];

/* Fixed 100-slot item state — engine DAT_07357830 (names) +
 * DAT_073a3ab8 (size pairs). Item loop iterates all 100; per-slot
 * skipped when name is the empty string. */
extern char     g_scene_sc1_item_names[SCENE_SC1_ITEM_CAP][SCENE_SC1_NAME_MAX];
extern uint32_t g_scene_sc1_item_sizes[SCENE_SC1_ITEM_CAP][2];   /* (w, h) pairs */

typedef int (*scene_sc1_sprite_load_fn)(const char *path,
                                        int kind, int slot,
                                        uint32_t expected_w, uint32_t expected_h,
                                        void *userdata);

typedef int (*scene_sc1_mesh_load_fn)(const char *path, int slot, void *userdata);

/* Pure-C body — engine FUN_0046bf38 end-to-end.
 *
 *   load_fn:  called for each sprite request (3 buckets — see above).
 *             May be NULL for counting-only dry runs.
 *   mesh_fn:  called for each variable-mesh request. NULL → no-op for
 *             the mesh bucket (but the sprite bucket still fires).
 *   userdata: passed through unchanged to both callbacks.
 *
 * Returns the total number of dispatches across all 4 buckets. */
int  scene_sc1_load_with(scene_sc1_sprite_load_fn load_fn,
                         scene_sc1_mesh_load_fn   mesh_fn,
                         void *userdata);

void scene_sc1_reset(void);

#ifdef _WIN32
#include "sprite.h"
#include "mesh.h"

extern sprite_t g_scene_sc1_ive_window;
extern sprite_t g_scene_sc1_chrname;
extern sprite_t g_scene_sc1_sprites[SCENE_SC1_VAR_SPRITE_CAP];
extern sprite_t g_scene_sc1_items[SCENE_SC1_ITEM_CAP];
extern mesh_t  *g_scene_sc1_meshes[SCENE_SC1_VAR_MESH_CAP];

struct IDirect3DDevice8;
void scene_sc1_init(struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_SCENE_SC1_H */
