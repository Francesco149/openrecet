/*
 * scene1_emit_record.c — see scene1_emit_record.h for the chip
 * writeup.
 *
 * C8e port of FUN_00454f7c + FUN_00454fe4 + FUN_00455191.  The two
 * helpers land verbatim; the third is the C8e.bridge port that
 * walks g_mesh_tex_cache slots × the supplied mesh_t's submeshes
 * (option (b) from the original chip plan — see the .h notes).
 */

#include "scene1_emit_record.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>

#include "mesh_load.h"   /* g_mesh_tex_cache + mesh_tex_entry */
#include "sprite.h"      /* sprite_t (resolve from cache entry) */

/* ─── per-material state cache ─────────────────────────────────────────
 *
 * FUN_00454fe4 caches the most-recent device state for four channels
 * (cull, mip, address-u, address-v) so it can skip redundant writes
 * when adjacent materials share the same state.  FUN_00454f7c resets
 * the cache to 0 — which forces the *next* FUN_00454fe4 call to
 * write its first state diff unconditionally.
 *
 * Engine globals (BSS):
 *   DAT_06a49b10 — cull cache       (0 = CULLMODE=CCW=3 default)
 *   DAT_06a49b14 — mipfilter cache  (0 = MIPFILTER=LINEAR=2 default)
 *   DAT_06a49b18 — addressu cache   (0 = ADDRESSU=WRAP=1 default)
 *   DAT_06a49b1c — addressv cache   (0 = ADDRESSV=WRAP=1 default)
 */
static int g_cache_cullmode    = 0;  /* DAT_06a49b10 */
static int g_cache_mipfilter   = 0;  /* DAT_06a49b14 */
static int g_cache_addressu    = 0;  /* DAT_06a49b18 */
static int g_cache_addressv    = 0;  /* DAT_06a49b1c */

/* ─── per-material flag accessors (read from g_mesh_tex_cache) ─────────
 *
 * C5 mesh_load already populates the 10 mesh_tex_flags fields when a
 * texture name first hits the cache.  The engine reads 4 of them from
 * scene1_emit_apply_material_state; the other 6 are read by sibling
 * render paths (water/hikari/kabe_/yuka_/shop_jutan/ext_tga) elsewhere.
 *
 * Engine → mesh_tex_flags mapping (confirmed from mesh_load.h):
 *   DAT_073cb684 (cull) ↔ flags.has_w_     ("w_" anywhere in name)
 *   DAT_073cb5bc (mip)  ↔ flags.has_n_     ("n_" anywhere in name)
 *   DAT_073cb74c (u)    ↔ flags.u_index    (last "u<k>_" match)
 *   DAT_073cb814 (v)    ↔ flags.v_index    (last "v<k>_" match)
 *
 * Out-of-range slots return 0 (zero-init default — matches engine BSS
 * for unwritten slots past DAT_073cb108).
 */
static int em_cull_flag(int material_slot)
{
    if (material_slot < 0 || material_slot >= g_mesh_tex_cache.count) return 0;
    return g_mesh_tex_cache.entries[material_slot].flags.has_w_;
}

static int em_mipfilter_flag(int material_slot)
{
    if (material_slot < 0 || material_slot >= g_mesh_tex_cache.count) return 0;
    return g_mesh_tex_cache.entries[material_slot].flags.has_n_;
}

static int em_addressu_key(int material_slot)
{
    if (material_slot < 0 || material_slot >= g_mesh_tex_cache.count) return 0;
    return g_mesh_tex_cache.entries[material_slot].flags.u_index;
}

static int em_addressv_key(int material_slot)
{
    if (material_slot < 0 || material_slot >= g_mesh_tex_cache.count) return 0;
    return g_mesh_tex_cache.entries[material_slot].flags.v_index;
}

static int em_trilinear_off(void)
{
    /* DAT_0438b178 — recet.ini trilinear-off override.  Same accessor
     * shape as scene1_render.c's scene1_trilinear_off.  recet.ini
     * doesn't expose this yet — defaults to 0 (mipfilter follows
     * per-material flag verbatim). */
    return 0;
}

/* DAT_073cb108 — material slot count (outer-loop bound).  Now
 * g_mesh_tex_cache.count, populated by C5 mesh_load. */
static int em_material_slot_count(void)
{
    return g_mesh_tex_cache.count;
}

/* DAT_073be5e8[slot] — IDirect3DBaseTexture8* per slot.  The engine
 * stores one texture pointer per cache slot; our g_mesh_tex_cache
 * entry holds a sprite_t* with the same underlying texture (created
 * by mesh_load_finalize_win32's sprite_load pass). */
static IDirect3DBaseTexture8 *em_texture_for_slot(int material_slot)
{
    if (material_slot < 0 || material_slot >= g_mesh_tex_cache.count) return NULL;
    sprite_t *spr = (sprite_t *)g_mesh_tex_cache.entries[material_slot].sprite;
    if (!spr) return NULL;
    return (IDirect3DBaseTexture8 *)spr->tex;
}

/* ─── FUN_00454f7c — mid-walker state preamble ─────────────────────── */

void scene1_emit_preamble(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L6-L9: reset the four cache slots so the next
     * scene1_emit_apply_material_state issues its first write. */
    g_cache_cullmode  = 0;
    g_cache_mipfilter = 0;
    g_cache_addressu  = 0;
    g_cache_addressv  = 0;

    /* L10: CULLMODE = D3DCULL_CCW (= 3).  Engine default for the
     * shop interior pass. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CCW);

    /* L11: TSS MIPFILTER = LINEAR (= 2). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);

    /* L12-L13: ADDRESSU/V = WRAP. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
}

/* ─── FUN_00454fe4 — per-material state-flip ───────────────────────── */

void scene1_emit_apply_material_state(struct IDirect3DDevice8 *dev_in,
                                      int material_slot)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L10-L19: cull flag.  Cache slot DAT_06a49b10.
     *   table_value == 0  → want CULLMODE = D3DCULL_CCW (3)
     *   table_value != 0  → want CULLMODE = D3DCULL_NONE (1)
     *
     * Engine cache values:  0 = CCW (default), 1 = NONE
     *
     * Diff-write: only if cache != desired flag-mode.
     */
    {
        int flag = em_cull_flag(material_slot);
        if (flag == 0) {
            if (g_cache_cullmode != 0) {
                g_cache_cullmode = 0;
                IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CCW);
            }
        } else {
            if (g_cache_cullmode != 1) {
                g_cache_cullmode = 1;
                IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
            }
        }
    }

    /* L20-L34: mipfilter flag (with recet.ini trilinear-off
     * override).  Cache slot DAT_06a49b14.
     *   flag == 0 && !trilinear_off  →  want MIPFILTER = LINEAR (2)
     *   else                          →  want MIPFILTER = NONE (0)
     *
     * Cache value 0 = LINEAR, 1 = NONE.
     */
    {
        int flag    = em_mipfilter_flag(material_slot);
        int tri_off = em_trilinear_off();
        if (flag == 0 && tri_off != 1) {
            if (g_cache_mipfilter != 0) {
                g_cache_mipfilter = 0;
                IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,
                                                      D3DTEXF_LINEAR);
            }
        } else {
            if (g_cache_mipfilter != 1) {
                g_cache_mipfilter = 1;
                IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,
                                                      D3DTEXF_NONE);
            }
        }
    }

    /* L35-L60: address-u key → D3DTADDRESS_ value.  Cache slot
     * DAT_06a49b18.  Engine maps:
     *   key 0 → cache 0 → ADDRESSU = WRAP (1)
     *   key 1 → cache 1 → ADDRESSU = MIRROR (2)
     *   key 2 → cache 2 → ADDRESSU = CLAMP (3)
     *   key ≥3 → cache 3 → ADDRESSU = BORDER (4)
     *
     * Diff-write only if cache doesn't match key.
     */
    {
        int key = em_addressu_key(material_slot);
        int desired_cache;
        DWORD desired_value;
        switch (key) {
            case 0:  desired_cache = 0; desired_value = D3DTADDRESS_WRAP;   break;
            case 1:  desired_cache = 1; desired_value = D3DTADDRESS_MIRROR; break;
            case 2:  desired_cache = 2; desired_value = D3DTADDRESS_CLAMP;  break;
            default: desired_cache = 3; desired_value = D3DTADDRESS_BORDER; break;
        }
        if (g_cache_addressu != desired_cache) {
            g_cache_addressu = desired_cache;
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU,
                                                  desired_value);
        }
    }

    /* L61-L96: address-v key — identical structure to address-u. */
    {
        int key = em_addressv_key(material_slot);
        int desired_cache;
        DWORD desired_value;
        switch (key) {
            case 0:  desired_cache = 0; desired_value = D3DTADDRESS_WRAP;   break;
            case 1:  desired_cache = 1; desired_value = D3DTADDRESS_MIRROR; break;
            case 2:  desired_cache = 2; desired_value = D3DTADDRESS_CLAMP;  break;
            default: desired_cache = 3; desired_value = D3DTADDRESS_BORDER; break;
        }
        if (g_cache_addressv != desired_cache) {
            g_cache_addressv = desired_cache;
            IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV,
                                                  desired_value);
        }
    }
}

/* ─── FUN_00455191 — per-mesh draw entry ───────────────────────────── */

void scene1_emit_record(struct IDirect3DDevice8 *dev_in,
                        const mesh_t *mesh)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L13: state preamble. */
    scene1_emit_preamble((struct IDirect3DDevice8 *)dev);

    /* L14-L41: outer loop over material slots [0, em_material_slot_count()).
     *
     * Engine body per slot (with our mesh_t adapter — see option (b)
     * in scene1_emit_record.h):
     *   for slot in [0, count):
     *     if mesh has VB+IB:
     *       FUN_00454fe4(slot)              // per-material state flip
     *       for each submesh sm in mesh:
     *         mat_idx = sm.material_index
     *         if mat_idx < 0: continue
     *         if mesh.texture_slots[mat_idx] != slot: continue
     *         if !texture_bound_for_slot:
     *           SetTexture(0, em_texture_for_slot(slot))
     *           texture_bound_for_slot = true
     *         SetMaterial(material at mat_idx)
     *         DrawIndexedPrimitive(sm.range)
     *
     * The engine's per-slot FUN_00454fe4 fires once regardless of
     * whether the inner subset loop hits — the state cache makes
     * this cheap and the next slot starts from a known base. */
    int slot_count = em_material_slot_count();
    if (mesh && mesh->vb && mesh->ib && mesh->submesh_count > 0 && slot_count > 0) {
        /* Engine sets the stream once per mesh-record; subsets share
         * the VB and only swap SetIndices.  Our mesh_t has one VB+IB
         * for the whole mesh — same shape. */
        IDirect3DDevice8_SetStreamSource(
            dev, 0, (IDirect3DVertexBuffer8 *)mesh->vb,
            (UINT)sizeof(mesh_vertex));

        for (int slot = 0; slot < slot_count; slot++) {
            scene1_emit_apply_material_state(
                (struct IDirect3DDevice8 *)dev, slot);

            int texture_bound = 0;

            for (int32_t s = 0; s < mesh->submesh_count; s++) {
                const mesh_submesh *sm = &mesh->submeshes[s];
                if (sm->vertex_count <= 0 || sm->index_count < 3) continue;
                int mat_idx = sm->material_index;
                if (mat_idx < 0 || mat_idx >= mesh->material_count) continue;
                if (!mesh->texture_slots) continue;
                if (mesh->texture_slots[mat_idx] != slot) continue;

                if (!texture_bound) {
                    texture_bound = 1;
                    IDirect3DDevice8_SetTexture(
                        dev, 0, em_texture_for_slot(slot));
                }

                /* SetIndices with BaseVertexIndex = sm.vertex_offset
                 * so the submesh's local 0..vertex_count-1 indices
                 * address the right VB rows.  Mirrors mesh_draw_d3d8. */
                IDirect3DDevice8_SetIndices(
                    dev, (IDirect3DIndexBuffer8 *)mesh->ib,
                    (UINT)sm->vertex_offset);

                /* Material — engine "ambient = diffuse" duplication
                 * (FUN_004c75e3 D3DXLoadMeshFromXof clone), same
                 * shape as mesh_draw_d3d8. */
                D3DMATERIAL8 mat = {0};
                const xfile_material *mm = &mesh->materials[mat_idx];
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
                IDirect3DDevice8_SetMaterial(dev, &mat);

                IDirect3DDevice8_DrawIndexedPrimitive(
                    dev, D3DPT_TRIANGLELIST,
                    /* MinIndex      */ 0,
                    /* NumVertices   */ (UINT)sm->vertex_count,
                    /* StartIndex    */ (UINT)sm->index_offset,
                    /* PrimitiveCount*/ (UINT)(sm->index_count / 3));
            }
        }
    }

    /* L42-L43: tail.  ADDRESSU = WRAP, ADDRESSV = WRAP.  Reset for
     * downstream callers — the per-material loop may have left
     * non-WRAP addressing on. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
}

#endif /* _WIN32 */
