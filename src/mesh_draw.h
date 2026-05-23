/*
 * mesh_draw.h — C7a: per-submesh draw helper + preview camera/render-state
 * setup for the mesh pipeline.
 *
 * Pulls the C1-C6 pipeline (mesh_load → mesh_load_finalize_win32) onto
 * the screen via DrawIndexedPrimitive. The eventual scene-1 walker
 * (FUN_0040a765 port, C7j+) will own its own world-transform + light
 * setup; the helpers here are the bare minimum so the --show-mesh CLI
 * flag can render a single mesh end-to-end.
 *
 * Pure-C surface (host-testable):
 *   - mesh_resolve_texture_slot: material index → global cache slot (-1
 *     if no texture or unbuilt).
 *
 * Win32-only surface (needs IDirect3DDevice8):
 *   - mesh_resolve_texture_sprite: material index → sprite_t* (or NULL).
 *   - mesh_set_default_render_state: depth/cull/lighting/texture-stage
 *     defaults sufficient for an unlit, textured preview.
 *   - mesh_orbital_view_proj: lookat + perspective framed around the
 *     mesh's bounds; phase∈[0,1) orbits the Y axis.
 *   - mesh_draw_d3d8: walk submeshes, emit Set*+DrawIndexedPrimitive.
 */

#ifndef OPENRECET_MESH_DRAW_H
#define OPENRECET_MESH_DRAW_H

#include "mesh.h"

/*
 * Resolve a material index on `m` to the global texture-cache slot.
 * Returns the slot index when `m->texture_slots[material_index]` is in
 * range, or -1 otherwise (no texture on this material, mesh built
 * without mesh_load, OOB material index, or stale slot past the cache
 * count).
 *
 * Pure — does not touch D3D. The slot value can be fed straight into
 * `g_mesh_tex_cache.entries[slot]` for the matching name+flags+sprite.
 */
int mesh_resolve_texture_slot(const mesh_t *m, int material_index);

#ifdef _WIN32
struct IDirect3DDevice8;

/*
 * Same as mesh_resolve_texture_slot, but returns the resolved sprite_t*
 * (cast to void* to keep callers out of the d3d8.h include path) or
 * NULL when the slot has no uploaded texture. Win32-only because the
 * sprite_t lives behind the Win32 sprite layer.
 */
void *mesh_resolve_texture_sprite(const mesh_t *m, int material_index);

/*
 * Configure D3D8 render state for an unlit, textured preview:
 *   D3DRS_ZENABLE        = D3DZB_TRUE
 *   D3DRS_ZWRITEENABLE   = TRUE
 *   D3DRS_LIGHTING       = FALSE         (use vertex diffuse straight)
 *   D3DRS_CULLMODE       = D3DCULL_NONE  (matches engine FUN_004547ab L60)
 *   D3DRS_ALPHABLENDENABLE = FALSE
 *   D3DTSS_COLOROP / COLORARG / ALPHAOP = D3DTOP_MODULATE / SELECTARG1
 *   D3DSAMP_*ADDR        = D3DTADDRESS_WRAP
 *   D3DSAMP_*FILTER      = D3DTEXF_LINEAR
 *   Vertex shader        = FVF 0x152
 *
 * Lighting + per-light state belong to C7b. Idempotent — safe to call
 * every frame.
 */
void mesh_set_default_render_state(struct IDirect3DDevice8 *dev);

/*
 * Build an orbital-camera view + perspective projection that frames a
 * sphere of (radius) around `centroid` on a (viewport_w × viewport_h)
 * back buffer, and SetTransform them on `dev` (D3DTS_VIEW + PROJECTION).
 *
 *   phase ∈ [0, 1)   orbit angle around the Y axis (0 = +Z, 0.25 = +X)
 *   eye distance     = radius * 3 (centroid + 8% Y lift so the top
 *                                  of the sphere doesn't hide bottom faces)
 *   fov_y            = 60°
 *   z_near / z_far   = 0.05 * radius / 5 * radius (sphere always
 *                                                  inside frustum)
 *
 * Degenerate radius (≤ 0) gets bumped to 1.0 so the math doesn't NaN.
 * D3DTS_WORLD is left at identity (the mesh's vertices are already in
 * mesh-local space — C7a is fine to draw it where it sits).
 */
void mesh_orbital_view_proj(struct IDirect3DDevice8 *dev,
                            const float centroid[3], float radius,
                            float phase,
                            int viewport_w, int viewport_h);

/*
 * Draw every submesh of `m`. Must be inside BeginScene; caller is
 * responsible for the device-wide render state (see
 * mesh_set_default_render_state above) and matrices
 * (mesh_orbital_view_proj or the eventual scene-1 setup).
 *
 * Per submesh:
 *   1. SetStreamSource(0, m->vb, sizeof(mesh_vertex))
 *   2. SetIndices(m->ib, baseVertexIndex = submesh.vertex_offset)
 *   3. SetTexture(0, sprite->tex)   (resolved via texture_slots → cache)
 *      — falls back to SetTexture(0, NULL) when the material has no
 *      uploaded sprite, so the geometry still draws (white diffuse).
 *   4. SetMaterial({diffuse=material.diffuse, ambient=diffuse,
 *                   specular=material.specular, emissive=material.emissive,
 *                   power=material.power})
 *      — engine "ambient = diffuse" duplication. Material has no effect
 *      under our unlit C7a state, but it's the cheap shape the C7b
 *      lighting pass will inherit.
 *   5. DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0,
 *                           submesh.vertex_count, submesh.index_offset,
 *                           submesh.index_count / 3)
 *
 * No-op (early return) when m or m->vb or m->ib is NULL.
 */
void mesh_draw_d3d8(struct IDirect3DDevice8 *dev, const mesh_t *m);
#endif /* _WIN32 */

#endif /* OPENRECET_MESH_DRAW_H */
