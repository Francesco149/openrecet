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
 * Configure D3D8 render state to match the engine's pre-mesh-draw
 * baseline (FUN_00459dfd L86..L198). Idempotent — safe to call every
 * frame.
 *
 * The eventual scene-1 walker (C7j+ port of FUN_0040a765) will toggle
 * these as it draws different passes (fog on/off, Z write off for
 * alpha, projection swaps between z_far=350/2000, etc.). C7b sets the
 * floor every later chip will inherit.
 *
 * Render states set:
 *   D3DRS_CULLMODE         = D3DCULL_CCW   (engine L86: state 0x16, val 3)
 *   D3DRS_LIGHTING         = TRUE          (engine L132 sets 0 — initial
 *                                           pass; we land at TRUE so the
 *                                           preview shows shading)
 *   D3DRS_FOGENABLE        = FALSE         (engine L137; stage palette
 *                                           re-enables when 0x1a38 != 0)
 *   D3DRS_ZENABLE          = TRUE          (engine L168 — set AFTER the
 *                                           initial sky pass; matches the
 *                                           main scene-1 default)
 *   D3DRS_ZWRITEENABLE     = TRUE          (engine L169)
 *   D3DRS_AMBIENT          = 0xff000000    (engine L191 — alpha-only,
 *                                           RGB black. Stage palette
 *                                           overrides via FUN_00454f03.)
 *   D3DRS_COLORVERTEX      = TRUE          (engine L192)
 *   D3DRS_ALPHAFUNC        = D3DCMP_GREATER(engine L193: val 5)
 *   D3DRS_DIFFUSEMATERIALSOURCE = D3DMCS_COLOR1 (engine L194)
 *   D3DRS_AMBIENTMATERIALSOURCE = D3DMCS_COLOR1 (engine L195)
 *   D3DRS_SHADEMODE        = D3DSHADE_GOURAUD (engine L198: val 2)
 *   D3DRS_WRAP0            = 0              (engine L190)
 *
 * Texture stage 0:
 *   D3DTSS_COLORARG1       = D3DTA_DIFFUSE (engine L197)
 *   D3DTSS_COLORARG2       = D3DTA_TEXTURE (engine L196)
 *   D3DTSS_COLOROP         = D3DTOP_MODULATE (engine relies on default;
 *                            set explicitly so we don't inherit
 *                            SELECTARG1 from the sprite path)
 *   D3DTSS_ALPHAOP         = D3DTOP_DISABLE (engine L153)
 *   D3DTSS_MAGFILTER       = D3DTEXF_LINEAR (engine L98)
 *   D3DTSS_MINFILTER       = D3DTEXF_LINEAR (engine L92)
 *   D3DTSS_MIPFILTER       = D3DTEXF_NONE   (engine L106 when
 *                            DAT_0438b178 == 0 — the shipped recet.ini
 *                            default; mipmaps gate deferred)
 *   D3DTSS_ADDRESSU        = D3DTADDRESS_WRAP (engine L188)
 *   D3DTSS_ADDRESSV        = D3DTADDRESS_WRAP (engine L189)
 *
 * Vertex shader / FVF:
 *   SetVertexShader(0x152)  (engine L122)
 *
 * Fog, projection, view, world transform, per-stage palette ambient,
 * SetLight/LightEnable — owned by other helpers / the eventual walker.
 */
void mesh_set_default_render_state(struct IDirect3DDevice8 *dev);

/*
 * Preview-only directional light + ambient override so the --show-mesh
 * smoke produces a visibly shaded mesh against the engine's pitch-black
 * default ambient (D3DRS_AMBIENT = 0xff000000).
 *
 * Configures light 0 as D3DLIGHT_DIRECTIONAL with white diffuse and a
 * direction pointing roughly (+X, -Y, -Z) — from upper-front-right into
 * the scene. Sets D3DRS_AMBIENT to a soft gray (0xff404040) so the
 * shadowed side stays readable instead of going black. Enables light 0.
 *
 * The eventual scene-1 walker (FUN_0040a765, C7j+) supplies its own
 * light from stage palette + 0x1ae0 — when it ports, the preview helper
 * stops being called for non-`--show-mesh` paths.
 */
void mesh_setup_preview_light(struct IDirect3DDevice8 *dev);

/*
 * Build an orbital-camera view + perspective projection that frames a
 * sphere of (radius) around `centroid` on a (viewport_w × viewport_h)
 * back buffer, and SetTransform them on `dev` (D3DTS_VIEW + PROJECTION).
 *
 *   phase ∈ [0, 1)   orbit angle around the Y axis (0 = +Z, 0.25 = +X)
 *   eye distance     = radius * 3 (centroid + 1.2·radius Y lift so we
 *                                  see the top of the mesh)
 *   fov_y            = 45° (matches engine DAT_073de3a0 default —
 *                           0x42340000 at all.c:34225, used by
 *                           FUN_0045bbf9 etc.)
 *   aspect           = viewport_w / viewport_h (engine hard-codes 4/3
 *                      — 0x3faaaaab — but we honor the actual back
 *                      buffer so widescreen --show-mesh runs aren't
 *                      letterboxed)
 *   z_near / z_far   = 0.05 * radius / 5 * radius (sphere always
 *                      inside frustum; engine uses fixed 1.0/350.0
 *                      but that only frames scene-space units)
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
 * Override the orbital camera's distance multiplier. Default 1.0 keeps
 * the documented eye distance of 3·radius. For meshes whose
 * mesh_compute_bounds radius is inflated by a handful of outlier
 * vertices (engine-side scene geometry like ground planes / sky
 * markers), pass a value < 1 to pull the camera closer:
 *
 *   --mesh-zoom 0.3   →  eye distance = 0.9·radius (3·r·0.3)
 *
 * Z-near/z-far track the same scale so the sphere always stays inside
 * the frustum even when the camera is inside the engine-radius bound.
 * Values ≤ 0 are clamped to 1.0.
 */
void mesh_orbital_set_zoom(float factor);

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
