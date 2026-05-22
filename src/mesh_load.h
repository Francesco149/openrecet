/*
 * mesh_load.h — orchestrator for the engine's .x mesh-load path (C5).
 *
 * Ports FUN_00472836 (1609 B). Wires xfile_parse → mesh_build_from_xfile
 * → mesh_compute_bounds → mesh_upload_d3d8 (Win32 only), plus the two
 * pieces FUN_00472836 owns that the C4 build step didn't have:
 *
 *   1. A texture-name classifier that emits 10 mode flags from the
 *      texture's filename (water/hikari/kabe_/yuka_/shop_jutan + n_/w_
 *      + u-index 0..3 + v-index 0..3). Mirrors the per-character scan
 *      at FUN_00472836:138..273.
 *
 *   2. A *global* texture-name dedupe cache. The engine has one
 *      flat 200-entry registry at &DAT_073be908 (256-char strides,
 *      count at DAT_073cb108), plus 10 parallel uint8 side-tables at
 *      &DAT_073cb10c..&DAT_073cb814 that record the mode flags from
 *      step 1 — keyed by the global cache index. Loading the same
 *      texture from two meshes returns the same slot index; the flags
 *      are written ONCE (on first insert) and frozen from there.
 *
 * Texture-slot wiring on mesh_t: mesh_build_from_xfile fills
 * `m->materials[]`; mesh_load additionally allocates `m->texture_slots[]`
 * (parallel to materials) and resolves each entry to the global cache
 * index. -1 means the material has no TextureFilename.
 *
 * GPU texture creation is deferred to Win32. On non-Win32 builds the
 * cache holds reservation-only entries (name + flags); call
 * mesh_load_finalize_win32(dev) on Win32 to populate any cache entries
 * still missing their sprite_t handle. That keeps the pure-C build path
 * fully testable under ASan on the host.
 *
 * Engine details deferred:
 *   - The .tga→.bmp on-disk override loop inside FUN_00471b24 (sprite
 *     loader) — our src/sprite.c calls disk-first then storage_read
 *     fallback but does NOT rewrite the filename. The `ext_tga` flag
 *     in mesh_tex_flags is therefore set strictly from the .x's
 *     `TextureFilename "<name>"` string (which is what we'd write to
 *     local_248 in the engine — the rewrite happens inside the loader
 *     and doesn't affect what's stored in DAT_073cb4f4).
 *   - The 12-byte dynamic-bone scratch table at &DAT_073cc950 (param_3
 *     >= 0 path in the engine — `(param_3*200 + i)*0xc` zeroing). All
 *     static-mesh callers (scene_walls/floor/jutan/etc.) pass -1 so
 *     this is dormant; param_3 still accepted for API parity but the
 *     scratch table itself isn't materialised yet.
 *
 * See docs/findings/mesh-loader.md for the strategy rationale.
 */

#ifndef OPENRECET_MESH_LOAD_H
#define OPENRECET_MESH_LOAD_H

#include <stdint.h>

#include "mesh.h"

#define MESH_TEX_CACHE_CAP   200
#define MESH_TEX_NAME_MAX    256

/*
 * Per-texture mode flags. One row of the engine's 10 side-tables.
 *
 * Engine mapping (side-tables at &DAT_073cb10c..&DAT_073cb814, all
 * indexed by the global cache slot):
 *
 *   .water       — DAT_073cb10c[i]   set if name starts with "water"  (5)
 *   .hikari      — DAT_073cb1d4[i]   set if name starts with "hikari" (6)
 *   .kabe_       — DAT_073cb29c[i]   set if name starts with "kabe_"  (5)
 *   .yuka_       — DAT_073cb364[i]   set if name starts with "yuka_"  (5)
 *   .shop_jutan  — DAT_073cb42c[i]   set if name starts with "shop_jutan" (10)
 *   .ext_tga     — DAT_073cb4f4[i]   set if ".tga" appears in the on-disk path
 *                                    (engine checks via 2-char ".t" match on the
 *                                    "dir/tex_name" buffer — see ext_tga note above)
 *   .has_n_      — DAT_073cb5bc[i]   set if "n_" appears at any position
 *   .has_w_      — DAT_073cb684[i]   set if "w_" appears at any position
 *   .u_index     — DAT_073cb74c[i]   0..3 — last "u<k>_" match wins; defaults to 0
 *   .v_index     — DAT_073cb814[i]   0..3 — last "v<k>_" match wins; defaults to 0
 *
 * Defaults if no match: all zero (the engine zeroes locals_30/3c/2c/24/38/34/28
 * to 0, and local_18/local_1c to 0, before the per-character scan).
 */
typedef struct {
    int water;
    int hikari;
    int kabe_;
    int yuka_;
    int shop_jutan;
    int ext_tga;
    int has_n_;
    int has_w_;
    int u_index;
    int v_index;
} mesh_tex_flags;

/*
 * Pure classifier. Reads the texture filename (e.g. "kabe_01_u2_.tga")
 * and writes the 10 flags. `ext_tga` is set when the filename contains
 * ".tga" anywhere — see the note at the top of this file about the
 * engine doing this on the full path; for static meshes the directory
 * prefix never contains ".tga" so the result matches in practice.
 *
 * Safe to call with name == NULL or "" (writes all zeros).
 */
void mesh_classify_texture_name(const char *texture_name, mesh_tex_flags *out);

/*
 * One row of the global texture cache.
 *
 * `sprite` is a sprite_t* on Win32 (created from the on-disk file via
 * sprite_load when mesh_load_finalize_win32 runs); opaque void* on
 * other builds so the host-side tests don't need <d3d8.h>.
 */
typedef struct {
    char            name[MESH_TEX_NAME_MAX];
    mesh_tex_flags  flags;
    void           *sprite;     /* sprite_t* on Win32; NULL otherwise / until upload */
} mesh_tex_entry;

typedef struct {
    int             count;
    mesh_tex_entry  entries[MESH_TEX_CACHE_CAP];
} mesh_tex_cache_t;

/*
 * Global texture cache (the engine's &DAT_073be908 + count at
 * DAT_073cb108). Process-wide singleton — every mesh_load call writes
 * here. Read it from the renderer to fetch (sprite, flags) by slot.
 */
extern mesh_tex_cache_t g_mesh_tex_cache;

/*
 * Reset the cache (clear count, free any owned sprites on Win32). Call
 * on device reload or between unit tests.
 */
void mesh_tex_cache_reset(void);

/*
 * Linear scan for an existing entry by exact name. Returns the slot
 * index, or -1 if not found.
 */
int  mesh_tex_cache_find(const char *texture_name);

/*
 * Insert (assumes not present — call mesh_tex_cache_find first or use
 * mesh_tex_cache_lookup_or_reserve below). Fills the entry's name and
 * flags; sprite stays NULL.
 *
 * Returns the new slot index, or -1 on capacity overflow (engine has
 * the same 200-entry limit — DAT_073cb10c..DAT_073cb814 are 200 bytes
 * each).
 */
int  mesh_tex_cache_insert(const char *texture_name, const mesh_tex_flags *flags);

/*
 * One-shot find-or-insert. `*was_new` is set to 1 if we just inserted,
 * 0 if we returned an existing slot. `flags` is used only on insert
 * (existing entries keep their frozen flags). Returns the slot index,
 * or -1 on capacity overflow.
 */
int  mesh_tex_cache_lookup_or_reserve(const char *texture_name,
                                      const mesh_tex_flags *flags,
                                      int *was_new);

/*
 * The orchestrator (FUN_00472836-equivalent).
 *
 *   xfile_path : full storage-relative path, e.g. "xfile/etc/ice01.x"
 *                (callers like FUN_0046bf38 / FUN_004748f8 build this
 *                with their own "xfile/{subdir}/" prefix).
 *   param_3    : dynamic-bone scratch instance, or -1 to skip. Static
 *                meshes always pass -1.
 *
 * Behaviour:
 *   1. Read via storage_read; on storage miss, return NULL.
 *   2. xfile_parse → mesh_build_from_xfile → mesh_compute_bounds.
 *   3. Per material: classify the texture name, find-or-reserve in the
 *      global cache, write the slot into m->texture_slots[i].
 *   4. Easydisp ("_s.x" variant) gate: if g_config.easydisp != 0,
 *      replace ".x" with "_s.x" and try storage_read on the variant
 *      first, falling back to the normal name. (No `_s.x` ships in the
 *      vendor corpus — gate is dormant by default.)
 *
 * Returns a built mesh_t on success (caller mesh_free's it). On
 * parse/build error, returns the mesh_t with m->error[] populated (same
 * shape as mesh_build_from_xfile).
 *
 * GPU upload (VB/IB + textures) is NOT done here; on Win32, call
 * mesh_load_finalize_win32 after.
 */
mesh_t *mesh_load(const char *xfile_path, int param_3);

/*
 * Set the easydisp gate (engine DAT_0438b19c — read from recet.ini
 * [setup] easydisp at boot). When non-zero, mesh_load tries
 * "<name>_s.x" before falling back to "<name>.x". main.c calls this
 * once after recet_ini_load.
 */
void mesh_load_set_easydisp(int v);

/*
 * Same as mesh_load but takes the file contents directly. Used by unit
 * tests on the host (where the storage_* layer isn't linkable — it
 * pulls <windows.h>) and by any caller that already has the buffer in
 * hand. `path_for_diagnostics` is stored in m->path and is the source
 * of the texture-directory prefix on Win32 finalize.
 */
mesh_t *mesh_load_from_buf(const void *data, size_t len,
                           const char *path_for_diagnostics, int param_3);

#ifdef _WIN32
struct IDirect3DDevice8;

/*
 * Win32 finishing pass. Uploads mesh VB/IB (via mesh_upload_d3d8) and
 * creates sprite_t handles for any cache entries whose `sprite` is
 * still NULL (calls sprite_load with the dir-prefix from m->path +
 * cache entry's name). Returns S_OK on success, first failing HRESULT
 * otherwise. Safe to call multiple times.
 */
long mesh_load_finalize_win32(mesh_t *m, struct IDirect3DDevice8 *dev);
#endif

#endif /* OPENRECET_MESH_LOAD_H */
