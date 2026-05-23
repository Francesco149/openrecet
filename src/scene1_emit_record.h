/*
 * scene1_emit_record.h — C8e port of the per-record draw helpers
 * called from inside the four mesh walkers.
 *
 * Three engine functions land together because they're tightly
 * coupled:
 *
 *   FUN_00454f7c (104 B)  — mid-walker state preamble.  Zeros 4
 *                           scratch flags + sets CULLMODE=CW +
 *                           TSS MIPFILTER=POINT + TSS ADDRESS=WRAP.
 *                           Called once at the top of FUN_00455191
 *                           and twice more from FUN_00457714 +
 *                           FUN_00459847 (still TODO).
 *
 *   FUN_00454fe4 (429 B)  — per-material state-flip helper.  Takes
 *                           a material slot index, reads four
 *                           per-material flag tables at DAT_073cb684
 *                           / 5bc / 74c / 814, and applies the
 *                           corresponding D3D state diff (CULLMODE,
 *                           MIPFILTER, ADDRESSU, ADDRESSV).  Skips
 *                           writes if state already matches —
 *                           hence the cached scratch globals.
 *
 *   FUN_00455191 (217 B)  — the per-mesh draw entry.  Takes an
 *                           engine mesh-record pointer
 *                           (ID3DXMesh*, material-index array,
 *                           per-subset materials, subset count).
 *                           Outer-loops over material slots
 *                           [0, DAT_073cb108), inner-loops over
 *                           the mesh's subsets matching each slot,
 *                           binds material+texture per slot,
 *                           DrawSubset per matching subset.
 *
 * C8e.bridge (2026-05-24):
 *   - Stub accessors wired to g_mesh_tex_cache.  The engine's
 *     DAT_073cb108 is our `g_mesh_tex_cache.count`; the four flag
 *     tables (DAT_073cb684/5bc/74c/814) are the .has_w_ / .has_n_ /
 *     .u_index / .v_index fields of each entry's mesh_tex_flags;
 *     DAT_073be5e8[slot] is the entry's sprite_t->tex.  C5 mesh_load
 *     already populates all of these.
 *   - Inner draw body implemented via option (b): bridge engine-
 *     mesh-record → our mesh_t.  Outer loop walks cache slots [0,
 *     count); inner loop walks our mesh_t's submeshes filtered by
 *     `m->texture_slots[sm.material_index] == slot`.  Same draw
 *     sequence as the engine, different iteration source (mesh_t's
 *     submeshes vs ID3DXMesh subsets).
 *   - The engine's API takes ONE arg (the mesh-record); our prior
 *     header speculated an "override_table" second arg that turned
 *     out to be the mesh-record itself (callers pass &DAT_073a9680
 *     etc).  Header now matches the engine: `(dev, mesh)`.
 *
 * Wiring: scene1_shop_walker.c::sw_pass_d passes a per-pass mesh
 * via the new scene1_shop_walker_set_pass_d_mesh() setter (default
 * NULL → dormant).  CLI flag `--force-pass-d-mesh <path>` plumbs a
 * hand-loaded mesh through main.c, analogous to `--show-pass-f-test`.
 */

#ifndef OPENRECET_SCENE1_EMIT_RECORD_H
#define OPENRECET_SCENE1_EMIT_RECORD_H

#include "mesh.h"  /* mesh_t */

#ifdef _WIN32

struct IDirect3DDevice8;

/* FUN_00454f7c — mid-walker state preamble.  Resets the four
 * cached per-material state slots so subsequent FUN_00454fe4 calls
 * always re-issue their first state diff.  Also issues 4
 * unconditional device writes.  No-op when dev is NULL. */
void scene1_emit_preamble(struct IDirect3DDevice8 *dev);

/* FUN_00454fe4 — apply per-material state diff for material slot
 * `material_slot`.  Reads four engine flag tables (cull / mipfilter
 * / address-u / address-v) via g_mesh_tex_cache and writes the
 * device state only when different from the cached value.  No-op
 * when dev is NULL or material_slot is out of range. */
void scene1_emit_apply_material_state(struct IDirect3DDevice8 *dev,
                                      int material_slot);

/* FUN_00455191 — per-mesh draw entry.  Takes a mesh_t (the engine's
 * mesh-record analog; see C8e.bridge note above).  Walks cache
 * slots × mesh submeshes, binds material+texture, calls
 * DrawIndexedPrimitive per matching submesh.
 *
 * `mesh` may be NULL — short-circuits the inner draw loop (engine
 * guards on piVar2[0] != 0; equivalent to our NULL mesh check).
 * The preamble + tail state writes still fire so the next call's
 * state diffs see the correct base.
 *
 * No-op when dev is NULL. */
void scene1_emit_record(struct IDirect3DDevice8 *dev,
                        const mesh_t *mesh);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_EMIT_RECORD_H */
