/*
 * scene1_emit_record.c — see scene1_emit_record.h for the chip
 * writeup.
 *
 * C8e port of FUN_00454f7c + FUN_00454fe4 + FUN_00455191.  The two
 * helpers land in full (pure state writes); the third is a
 * scaffold — outer loop structured, per-subset draw body deferred
 * to when the engine mesh-record shape can be bridged to our
 * flat mesh_t.
 */

#include "scene1_emit_record.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>

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

/* ─── TODO accessors for the per-material flag tables ──────────────────
 *
 * Each table holds one byte per material slot — read by index into
 * the table from FUN_00454fe4's `material_slot` parameter.  Tables
 * are BSS-zero today; HOUSE values are all 0.
 *
 *   DAT_073cb684 — cull flag       (0 = CCW, 1 = NONE)
 *   DAT_073cb5bc — mipfilter flag  (0 = LINEAR, 1 = NONE)
 *   DAT_073cb74c — addressu key    (0..3 → WRAP/MIRROR/CLAMP/BORDER)
 *   DAT_073cb814 — addressv key    (0..3 → WRAP/MIRROR/CLAMP/BORDER)
 *
 * Plus DAT_0438b178 — the recet.ini trilinear-off override.  If
 * the mipfilter flag table is 0 but the recet.ini override is 1,
 * the engine still writes NONE (forces trilinear off).
 */

static int em_cull_flag(int material_slot)
{
    /* TODO C8-followup: read (&DAT_073cb684)[material_slot]. */
    (void)material_slot;
    return 0;
}

static int em_mipfilter_flag(int material_slot)
{
    /* TODO C8-followup: read (&DAT_073cb5bc)[material_slot]. */
    (void)material_slot;
    return 0;
}

static int em_addressu_key(int material_slot)
{
    /* TODO C8-followup: read (&DAT_073cb74c)[material_slot]. */
    (void)material_slot;
    return 0;
}

static int em_addressv_key(int material_slot)
{
    /* TODO C8-followup: read (&DAT_073cb814)[material_slot]. */
    (void)material_slot;
    return 0;
}

static int em_trilinear_off(void)
{
    /* DAT_0438b178 — same accessor shape as scene1_render.c's
     * scene1_trilinear_off.  recet.ini doesn't expose this yet. */
    return 0;
}

/* DAT_073cb108 — material slot count (outer-loop bound for
 * FUN_00455191).  BSS-zero in HOUSE.  Populated by the per-stage
 * material registration that runs at scene-1 load (not ported). */
static int em_material_slot_count(void)
{
    return 0;
}

/* DAT_073be5e8 — texture pointer array, indexed by material slot.
 * BSS-zero (one IDirect3DTexture8* per slot, NULL today). */
static struct IDirect3DBaseTexture8 *em_texture_for_slot(int material_slot)
{
    (void)material_slot;
    return NULL;
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
                        const void *mesh_record,
                        const void *override_table)
{
    /* Engine `override_table` is read inside FUN_00454f7c / 4fe4 /
     * 5191 only indirectly — actually, looking at the decomp again,
     * it's the second parameter passed when the walker calls
     * FUN_00455191(&DAT_073a9680).  But the engine reads param_1
     * as the mesh-record (piVar2 = param_1).  The decomp shows the
     * pointer is reassigned (param_1 = (int *)0x0 inside the inner
     * loop), then later used as an offset accumulator for material
     * stride 0x44 — so the engine treats it as both a mesh-record
     * pointer AND a local material-iteration accumulator.  Ghidra
     * conflated the two roles.
     *
     * For our header API: mesh_record is the mesh struct (engine's
     * shape with [0]=ID3DXMesh*, [1]=mat-index*, [2]=mat-array*,
     * [4]=subset-count).  override_table is currently unused (the
     * engine never actually reads it under the gates that fire for
     * HOUSE).  Both are opaque void* to avoid leaking the
     * yet-to-port engine type shapes through our header. */
    (void)override_table;

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* L13: state preamble. */
    scene1_emit_preamble((struct IDirect3DDevice8 *)dev);

    /* L14-L41: outer loop over material slots [0, em_material_slot_count()).
     *
     * Engine body per slot:
     *   if (piVar2[0] != 0) {                  // mesh has an ID3DXMesh*
     *     FUN_00454fe4(local_8);               // per-material state flip
     *     for (iVar3 = 0; iVar3 < piVar2[4]; iVar3++) {
     *       if (piVar2[1][iVar3] == local_8) { // subset's material matches outer slot
     *         if (!bVar1) {
     *           bVar1 = true;
     *           SetTexture(0, &DAT_073be5e8[local_8]);
     *         }
     *         SetMaterial(piVar2[2] + iVar3*0x44);
     *         ((ID3DXMesh*)piVar2[0])->DrawSubset(iVar3);
     *       }
     *     }
     *   }
     *
     * For HOUSE: em_material_slot_count() == 0 → outer loop
     * short-circuits → no draws.  When data populates, the inner
     * body should bridge to our mesh_t.  See TODO inline.
     */
    int slot_count = em_material_slot_count();
    if (mesh_record != NULL && slot_count > 0) {
        for (int slot = 0; slot < slot_count; slot++) {
            /* TODO C8-followup: implement the bridge from the
             * engine's mesh-record shape to our mesh_t.  Two
             * possible paths:
             *
             *  (a) Build an engine-shaped wrapper struct alongside
             *      mesh_t at mesh-load time (cheap — re-use the
             *      existing material + texture-slot arrays).
             *
             *  (b) Adapt the iteration here: for each slot, walk
             *      our mesh_t's submeshes and bind matching
             *      materials, calling DrawIndexedPrimitive per
             *      submesh.  This mirrors what mesh_draw_d3d8
             *      already does in one call — but the engine's
             *      outer-loop-per-slot lets it cache state across
             *      meshes that share materials, which our
             *      mesh_draw_d3d8 doesn't exploit.  For a single
             *      mesh, (a) and (b) produce equivalent draw
             *      sequences.
             *
             * Until data populates, this loop body is reachable
             * only if a caller passes a non-NULL record AND
             * em_material_slot_count() returns > 0.  Both gates
             * are dormant in HOUSE today.
             *
             * The per-material state diff fires regardless of
             * mesh content — useful even without a draw, since
             * downstream consumers expect the cached cull/mip/
             * address state to reflect this slot. */
            scene1_emit_apply_material_state(
                (struct IDirect3DDevice8 *)dev, slot);

            /* Suppress unused-warning while body is TODO: */
            struct IDirect3DBaseTexture8 *tex = em_texture_for_slot(slot);
            (void)tex;
            (void)mesh_record;
        }
    }

    /* L42-L43: tail.  ADDRESSU = WRAP, ADDRESSV = WRAP.  Reset for
     * downstream callers — the per-material loop may have left
     * non-WRAP addressing on. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
}

#endif /* _WIN32 */
