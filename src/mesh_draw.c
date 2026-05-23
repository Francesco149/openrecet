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

    /* FVF — engine FUN_00459dfd L122 (SetVertexShader(0x152)). */
    IDirect3DDevice8_SetVertexShader(dev, MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1);

    /* Cull. Engine L86 sets CULLMODE=CCW (val 3 in the D3DCULL enum)
     * for scene-1 — back-face culling on CCW-wound faces, i.e. front
     * faces are CW after the view+proj transform. The .x files'
     * source vertex order combined with RH projection lands CCW =
     * back for typical static meshes. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CCW);

    /* Depth. Engine L168/L169 — both set TRUE after the initial 2D
     * sky pass at L142/L147 turned them off. Scene-1 mesh draws run
     * with Z on. The walker (C7j+) toggles ZWRITE for alpha passes. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      D3DZB_TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);

    /* Blending / fog — off by default. Walker re-enables alpha for the
     * post-fx passes; fog gates on stage palette + 0x1a38 (engine
     * L170-184). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE,        FALSE);

    /* Lighting: engine L132 starts with LIGHTING=FALSE for the sky
     * pass, then L230 turns it ON conditionally for the main mesh
     * walk based on stage palette + 0x1ae0. The preview path always
     * wants lighting ON (the engine ambient is pitch black —
     * mesh_setup_preview_light raises it + adds a light source). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);

    /* Vertex-color → material-color routing. Engine L192/194/195:
     * COLORVERTEX on; DIFFUSE + AMBIENT sources read from the per-
     * vertex diffuse channel (COLOR1). The .x materials' diffuse
     * gets overridden by per-vertex colour — engine quirk; matches
     * our mesh.c default of vertex.diffuse = 0xFFFFFFFF for any
     * material-list-driven mesh. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_COLORVERTEX,            TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DIFFUSEMATERIALSOURCE,  D3DMCS_COLOR1);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENTMATERIALSOURCE,  D3DMCS_COLOR1);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);

    /* Engine ambient. L191 sets 0xff000000 (alpha-only); per-stage
     * the walker calls FUN_00454f03(palette[0x1a40]) to override.
     * The preview helper raises this to a soft gray so unlit faces
     * stay readable. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff000000);

    /* Shading + alpha test. Engine L198 sets SHADEMODE=GOURAUD (2);
     * L193 sets ALPHAFUNC=GREATER (5). Engine doesn't enable alpha
     * test here — ALPHATESTENABLE stays at the default. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    /* WRAP0 cleared (engine L190). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_WRAP0, 0);

    /* Texture stage 0. Engine L196/L197: arg2=TEXTURE, arg1=DIFFUSE.
     * Engine leaves COLOROP at the D3D8 default (MODULATE) — we set
     * it explicitly so we don't inherit SELECTARG1 from
     * sprite_draw's setup. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);

    /* Engine L153: ALPHAOP=DISABLE (val 1 = D3DTOP_DISABLE). With
     * alpha op disabled the FFP outputs full-alpha pixels — matches
     * the engine's opaque mesh pass. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    /* Sampler. Engine L92/L98 set MIN/MAGFILTER=LINEAR. L106 sets
     * MIPFILTER=NONE in the shipped recet.ini default (DAT_0438b178
     * == 0); the trilinear gate is deferred. L188/L189 set
     * ADDRESSU/V=WRAP. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU,  D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV,  D3DTADDRESS_WRAP);
}

void mesh_setup_preview_light(IDirect3DDevice8 *dev)
{
    if (!dev) return;

    D3DLIGHT8 light = {0};
    light.Type         = D3DLIGHT_DIRECTIONAL;
    light.Diffuse.r    = 1.0f;
    light.Diffuse.g    = 1.0f;
    light.Diffuse.b    = 1.0f;
    light.Diffuse.a    = 1.0f;
    light.Specular.r   = 0.0f;
    light.Specular.g   = 0.0f;
    light.Specular.b   = 0.0f;
    light.Specular.a   = 0.0f;
    light.Ambient.r    = 0.0f;
    light.Ambient.g    = 0.0f;
    light.Ambient.b    = 0.0f;
    light.Ambient.a    = 0.0f;
    /* Direction: into the scene from upper-front-right, normalized.
     * Hits the +X, +Y, +Z octant of most meshes — good shading for
     * static props sitting near origin with y-up. */
    {
        float dx = 0.5f, dy = -1.0f, dz = -0.3f;
        float len = (float)sqrt((double)(dx*dx + dy*dy + dz*dz));
        light.Direction.x = dx / len;
        light.Direction.y = dy / len;
        light.Direction.z = dz / len;
    }
    /* Range/Falloff/etc. ignored for D3DLIGHT_DIRECTIONAL. */

    IDirect3DDevice8_SetLight(dev, 0, &light);
    IDirect3DDevice8_LightEnable(dev, 0, TRUE);

    /* Raise ambient above the engine's 0xff000000 floor so the
     * shadowed side of preview meshes isn't fully black. The engine's
     * stage palette ambient comes from FUN_00454f03 at L185; preview
     * substitutes a fixed soft gray. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff404040);
}

static float g_orbital_zoom = 1.0f;

void mesh_orbital_set_zoom(float factor)
{
    g_orbital_zoom = (factor > 0.0f) ? factor : 1.0f;
}

void mesh_orbital_view_proj(IDirect3DDevice8 *dev,
                            const float centroid[3], float radius,
                            float phase,
                            int viewport_w, int viewport_h)
{
    if (!dev || !centroid) return;

    float r = (radius > 0.0f) ? radius : 1.0f;
    float d = r * 3.0f * g_orbital_zoom;

    float angle = phase * 6.28318530717958647692f;   /* 2π */
    float eye[3]    = { centroid[0] + d * sinf(angle),
                        centroid[1] + d * 0.4f,
                        centroid[2] + d * cosf(angle) };
    float target[3] = { centroid[0], centroid[1], centroid[2] };
    float up[3]     = { 0.0f, 1.0f, 0.0f };

    float view[16];
    mat4_lookat_rh(view, eye, target, up);

    /* fov_y = 45° — engine DAT_073de3a0 default (0x42340000 at
     * all.c:34225, used in every FUN_004a3ee8 call in FUN_0045bbf9
     * and FUN_00459dfd). */
    float fov_y  = 45.0f * 3.14159265358979323846f / 180.0f;
    float aspect = (viewport_h > 0)
                       ? ((float)viewport_w / (float)viewport_h)
                       : 1.3333333f;
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
