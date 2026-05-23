/*
 * mesh_draw.c — C7a: visual smoke for the mesh pipeline.
 *
 * See mesh_draw.h for the API contract. The pure-C slot resolver sits
 * outside the Win32 guard so the host tests can exercise it without
 * linking the d3d8 layer; everything else compiles only under _WIN32.
 */

#include "mesh_draw.h"

#include <math.h>
#include <string.h>

#include "mesh_load.h"

#ifdef _WIN32
#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "math3d.h"
#include "sprite.h"
#endif

/* ───── Pure resolver — host-testable ──────────────────────────────────── */

int mesh_resolve_texture_slot(const mesh_t *m, int material_index)
{
    if (!m || material_index < 0 || material_index >= m->material_count) return -1;
    if (!m->texture_slots) return -1;
    int slot = (int)m->texture_slots[material_index];
    if (slot < 0 || slot >= g_mesh_tex_cache.count) return -1;
    return slot;
}

/* ───── Win32 draw helpers ─────────────────────────────────────────────── */
#ifdef _WIN32

void *mesh_resolve_texture_sprite(const mesh_t *m, int material_index)
{
    int slot = mesh_resolve_texture_slot(m, material_index);
    if (slot < 0) return NULL;
    return g_mesh_tex_cache.entries[slot].sprite;
}

void mesh_set_default_render_state(IDirect3DDevice8 *dev)
{
    if (!dev) return;

    IDirect3DDevice8_SetVertexShader(dev, MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1);

    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,          D3DZB_TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE,     TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING,         FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE,         D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE,        FALSE);

    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_WRAP);
}

void mesh_orbital_view_proj(IDirect3DDevice8 *dev,
                            const float centroid[3], float radius,
                            float phase,
                            int viewport_w, int viewport_h)
{
    if (!dev || !centroid) return;

    float r = (radius > 0.0f) ? radius : 1.0f;
    float d = r * 3.0f;

    float angle = phase * 6.28318530717958647692f;   /* 2π */
    float eye[3]    = { centroid[0] + d * sinf(angle),
                        centroid[1] + d * 0.4f,
                        centroid[2] + d * cosf(angle) };
    float target[3] = { centroid[0], centroid[1], centroid[2] };
    float up[3]     = { 0.0f, 1.0f, 0.0f };

    float view[16];
    mat4_lookat_rh(view, eye, target, up);

    float fov_y  = 60.0f * 3.14159265358979323846f / 180.0f;
    float aspect = (viewport_h > 0)
                       ? ((float)viewport_w / (float)viewport_h)
                       : 1.0f;
    float z_near = 0.05f * r;
    float z_far  = 5.00f * r;
    if (z_near < 0.01f) z_near = 0.01f;
    if (z_far  < z_near + 1.0f) z_far = z_near + 1.0f;

    float proj[16];
    mat4_perspective_fov_rh(proj, fov_y, aspect, z_near, z_far);

    /* D3D8 wants 4×4 row-major matrices in D3DMATRIX, which is also
     * row-major float[4][4] — same layout as our math3d output. */
    IDirect3DDevice8_SetTransform(dev, D3DTS_VIEW,
                                  (const D3DMATRIX *)view);
    IDirect3DDevice8_SetTransform(dev, D3DTS_PROJECTION,
                                  (const D3DMATRIX *)proj);

    float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                  (const D3DMATRIX *)ident);
}

void mesh_draw_d3d8(IDirect3DDevice8 *dev, const mesh_t *m)
{
    if (!dev || !m || !m->vb || !m->ib) return;
    if (m->submesh_count <= 0) return;

    IDirect3DDevice8_SetStreamSource(
        dev, 0, (IDirect3DVertexBuffer8 *)m->vb, (UINT)sizeof(mesh_vertex));

    for (int32_t s = 0; s < m->submesh_count; s++) {
        const mesh_submesh *sm = &m->submeshes[s];
        if (sm->vertex_count <= 0 || sm->index_count < 3) continue;

        /* SetIndices: BaseVertexIndex is the offset added to every index
         * before fetching from the VB. Our indices are 0..vertex_count-1
         * within the submesh, so passing vertex_offset here makes
         * StartIndex==0/MinIndex==0 still address the right rows. */
        IDirect3DDevice8_SetIndices(
            dev, (IDirect3DIndexBuffer8 *)m->ib, (UINT)sm->vertex_offset);

        /* Texture (or NULL for materials with none — still draws the
         * geometry; with COLOROP=MODULATE and texture NULL D3D8 falls
         * back to DIFFUSE, so the vertex white shows through). */
        sprite_t *spr = (sprite_t *)mesh_resolve_texture_sprite(m, sm->material_index);
        IDirect3DDevice8_SetTexture(
            dev, 0,
            (spr && spr->tex) ? (IDirect3DBaseTexture8 *)spr->tex : NULL);

        /* Material — engine "ambient = diffuse" duplication
         * (FUN_004c75e3 D3DXLoadMeshFromXof clone). Has no effect under
         * D3DRS_LIGHTING=FALSE; will start mattering at C7b. */
        D3DMATERIAL8 mat = {0};
        if (sm->material_index >= 0 && sm->material_index < m->material_count) {
            const xfile_material *mm = &m->materials[sm->material_index];
            mat.Diffuse.r  = mm->diffuse.r;
            mat.Diffuse.g  = mm->diffuse.g;
            mat.Diffuse.b  = mm->diffuse.b;
            mat.Diffuse.a  = mm->diffuse.a;
            mat.Ambient    = mat.Diffuse;
            mat.Specular.r = mm->specular.r;
            mat.Specular.g = mm->specular.g;
            mat.Specular.b = mm->specular.b;
            mat.Specular.a = 1.0f;
            mat.Emissive.r = mm->emissive.r;
            mat.Emissive.g = mm->emissive.g;
            mat.Emissive.b = mm->emissive.b;
            mat.Emissive.a = 1.0f;
            mat.Power      = mm->power;
        } else {
            mat.Diffuse.r = mat.Diffuse.g = mat.Diffuse.b = mat.Diffuse.a = 1.0f;
            mat.Ambient   = mat.Diffuse;
        }
        IDirect3DDevice8_SetMaterial(dev, &mat);

        IDirect3DDevice8_DrawIndexedPrimitive(
            dev, D3DPT_TRIANGLELIST,
            /* MinIndex     */ 0,
            /* NumVertices  */ (UINT)sm->vertex_count,
            /* StartIndex   */ (UINT)sm->index_offset,
            /* PrimitiveCount */ (UINT)(sm->index_count / 3));
    }

    /* Leave texture unbound so later 2D overlays (fade/nowloading) don't
     * inherit the last mesh's texture if they forget to SetTexture. */
    IDirect3DDevice8_SetTexture(dev, 0, NULL);
}

#endif /* _WIN32 */
