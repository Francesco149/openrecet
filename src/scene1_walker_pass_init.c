/*
 * scene1_walker_pass_init.c — PII.3a (phase-2 matrix builder) +
 * PII.3b (outer cache-slot loop + draw loop B / HOUSE-furniture
 * renderer) port of FUN_00457714.  See scene1_walker_pass_init.h
 * for the chip writeup + asm refs.
 */

#include "scene1_walker_pass_init.h"

#include <stddef.h>
#include <string.h>

#include "math3d.h"
#include "scene_table.h"  /* g_scene_table + g_scene_table_selector */
#include "call_trace.h"

/* ─── per-mesh field arrays (BSS-zero by default) ─────────────────── */

int32_t g_scene1_walker_phase2_mesh_type[SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_rot_y    [SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_pos_y    [SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_pos_x    [SCENE1_WALKER_PHASE2_MAX];
float   g_scene1_walker_phase2_pos_z    [SCENE1_WALKER_PHASE2_MAX];

int32_t g_scene1_walker_phase2_count = 0;

/* PII.3c — phase-1 (wall/floor/jutan) per-instance arrays.  Resolved
 * AXIS values (the writer does the engine column→axis remap; see
 * scene1_walker_pass_init.h). */
int32_t g_scene1_walker_phase1_count = 0;
int32_t g_scene1_walker_phase1_mesh_index[SCENE1_WALKER_PHASE1_MAX];
float   g_scene1_walker_phase1_pos_x[SCENE1_WALKER_PHASE1_MAX];
float   g_scene1_walker_phase1_pos_y[SCENE1_WALKER_PHASE1_MAX];
float   g_scene1_walker_phase1_pos_z[SCENE1_WALKER_PHASE1_MAX];
float   g_scene1_walker_phase1_rot_y[SCENE1_WALKER_PHASE1_MAX];

void scene1_walker_phase1_reset(void)
{
    memset(g_scene1_walker_phase1_mesh_index, 0,
           sizeof g_scene1_walker_phase1_mesh_index);
    memset(g_scene1_walker_phase1_pos_x, 0, sizeof g_scene1_walker_phase1_pos_x);
    memset(g_scene1_walker_phase1_pos_y, 0, sizeof g_scene1_walker_phase1_pos_y);
    memset(g_scene1_walker_phase1_pos_z, 0, sizeof g_scene1_walker_phase1_pos_z);
    memset(g_scene1_walker_phase1_rot_y, 0, sizeof g_scene1_walker_phase1_rot_y);
    g_scene1_walker_phase1_count = 0;
}

/* ─── flag-byte hook ──────────────────────────────────────────────── */

static scene1_walker_phase2_flag_fn s_flag_hook = NULL;

void scene1_walker_phase2_set_flag_hook(scene1_walker_phase2_flag_fn fn)
{
    s_flag_hook = fn;
}

scene1_walker_phase2_flag_fn scene1_walker_phase2_get_flag_hook(void)
{
    return s_flag_hook;
}

/* ─── reset ───────────────────────────────────────────────────────── */

/* Defined further down in the PII.3b section so the file-scope
 * statics can be reset from the shared reset entrypoint. */
static void scene1_walker_phase2b_reset_internal(void);

void scene1_walker_phase2_reset(void)
{
    memset(g_scene1_walker_phase2_mesh_type, 0,
           sizeof(g_scene1_walker_phase2_mesh_type));
    memset(g_scene1_walker_phase2_rot_y, 0,
           sizeof(g_scene1_walker_phase2_rot_y));
    memset(g_scene1_walker_phase2_pos_y, 0,
           sizeof(g_scene1_walker_phase2_pos_y));
    memset(g_scene1_walker_phase2_pos_x, 0,
           sizeof(g_scene1_walker_phase2_pos_x));
    memset(g_scene1_walker_phase2_pos_z, 0,
           sizeof(g_scene1_walker_phase2_pos_z));
    g_scene1_walker_phase2_count = 0;
    s_flag_hook = NULL;
    scene1_walker_phase1_reset();
    scene1_walker_phase2b_reset_internal();
}

/* ─── matrix builder ──────────────────────────────────────────────── */

/* Engine .rdata constants (verified via tools/analyze/pe.py). */
#define K_TWO         2.0f               /* 0x519314 = 0x40000000 */
#define K_ZERO        0.0f               /* 0x519320 */
#define K_HALF_PI     1.5707964f         /* 0x519434 = 0x3fc90fdb */
#define K_PI          3.1415927f         /* 0x51943c = 0x40490fdb */
#define K_FIVE        5.0f               /* 0x51953c */
#define K_POINT_TWO   0.2f               /* 0x5198d8 = 0x3e4ccccd */
#define K_NEG_PT_TWO (-0.2f)             /* 0x519a8c = 0xbe4ccccd */

/* Append `T(2,0,0) × RotY(π) × world` to world, in place.
 *
 * Engine pattern (asm 0x457ebd..0x457f0a — repeated at 0x457f1c..0x457f65
 * and 0x457f78..0x457fc7).  Each occurrence: MatrixTranslation(t2,2,0,0);
 * Multiply(world, t2, world); MatrixRotationY(ry, π); Multiply(world, ry, world).
 *
 * Result on row-vector (D3D row-major) world matrix:
 *     world' = RotY(π) × T(2,0,0) × world
 * (composed right-to-left in our row-vector convention, so applied
 *  left-to-right to a vertex: first T(2,0,0), then RotY(π), then
 *  whatever was already there). */
static void append_flip_chain(float world[16])
{
    float t2[16];
    float ry_pi[16];
    mat4_translation(t2, K_TWO, K_ZERO, K_ZERO);
    mat4_mul(world, t2, world);
    mat4_rotation_y(ry_pi, K_PI);
    mat4_mul(world, ry_pi, world);
}

int scene1_walker_phase2_compute(float *out_matrices)
{
    if (!out_matrices) return 0;

    int count = g_scene1_walker_phase2_count;
    if (count < 0) count = 0;
    if (count > SCENE1_WALKER_PHASE2_MAX) count = SCENE1_WALKER_PHASE2_MAX;
    if (count == 0) return 0;

    for (int i = 0; i < count; i++) {
        float *world = out_matrices + i * 16;

        /* Stage 1: T(pos_x, pos_y, pos_z) → world.
         * Asm 0x457e48..0x457e64 (push pz [esi+0xf0], py [esi+0xa0],
         * px [esi+0x50], out=edi; call MatrixTranslation). */
        mat4_translation(world,
                         g_scene1_walker_phase2_pos_x[i],
                         g_scene1_walker_phase2_pos_y[i],
                         g_scene1_walker_phase2_pos_z[i]);

        /* Stage 2-3: world = RotY(rot_y) × world.
         * Asm 0x457e69..0x457e8a (RotationY(local, [esi]); Multiply
         * (edi, local, edi)). */
        {
            float ry[16];
            mat4_rotation_y(ry, g_scene1_walker_phase2_rot_y[i]);
            mat4_mul(world, ry, world);
        }

        /* Optional flip chain (mesh_type==4 + flag gates).
         * Asm 0x457e8f..0x457fc7. */
        if (g_scene1_walker_phase2_mesh_type[i] == 4) {
            int32_t flag = s_flag_hook ? s_flag_hook(i) : 0;
            if (flag != 0 && (flag & (int32_t)0xffffffc0) == (int32_t)0x000514c0) {
                float rot_y = g_scene1_walker_phase2_rot_y[i];
                if (rot_y == K_ZERO) {
                    /* rot==0 branch (asm 0x457ebd..0x457f0a, then
                     * jmp 0x457fc5 — skips the > 5.0 check). */
                    append_flip_chain(world);
                } else if (rot_y == K_HALF_PI) {
                    /* rot==π/2 branch (asm 0x457f1c..0x457f65 +
                     * fall-through to > 5.0 check). */
                    append_flip_chain(world);
                    if (g_scene1_walker_phase2_pos_y[i] > K_FIVE) {
                        /* > 5.0 branch (asm 0x457f78..0x457fc7) —
                         * applied AGAIN after the π/2 branch's flip. */
                        append_flip_chain(world);
                    }
                } else {
                    /* Direct fall-through to > 5.0 check (rot != 0,
                     * rot != π/2). */
                    if (g_scene1_walker_phase2_pos_y[i] > K_FIVE) {
                        append_flip_chain(world);
                    }
                }
            }
        }

        /* Stage 4: world = S(-0.2, 0.2, 0.2) × world.
         * Asm 0x457fcc..0x457fff (MatrixScaling(local, -0.2, 0.2, 0.2);
         * Multiply(edi, local, edi)).  Final per-mesh transform. */
        {
            float s[16];
            mat4_scaling(s, K_NEG_PT_TWO, K_POINT_TWO, K_POINT_TWO);
            mat4_mul(world, s, world);
        }
    }

    return count;
}

/* PII.3c — phase-1 matrix builder.  Engine asm 0x457d94..0x457e10:
 * per instance T(pos) then ×RotY(rot) then ×S(-0.2,0.2,0.2).  Same
 * chain as phase 2 minus the mesh_type==4 flip (phase-1 setup has no
 * such branch). */
int scene1_walker_phase1_compute(float *out_matrices)
{
    if (!out_matrices) return 0;

    int count = g_scene1_walker_phase1_count;
    if (count < 0) count = 0;
    if (count > SCENE1_WALKER_PHASE1_MAX) count = SCENE1_WALKER_PHASE1_MAX;
    if (count == 0) return 0;

    for (int i = 0; i < count; i++) {
        float *world = out_matrices + i * 16;

        mat4_translation(world,
                         g_scene1_walker_phase1_pos_x[i],
                         g_scene1_walker_phase1_pos_y[i],
                         g_scene1_walker_phase1_pos_z[i]);
        {
            float ry[16];
            mat4_rotation_y(ry, g_scene1_walker_phase1_rot_y[i]);
            mat4_mul(world, ry, world);
        }
        {
            float s[16];
            mat4_scaling(s, K_NEG_PT_TWO, K_POINT_TWO, K_POINT_TWO);
            mat4_mul(world, s, world);
        }
    }
    return count;
}

/* ═════════════════ PII.3b — outer loop + draw loop B ════════════════ */

int32_t g_scene1_walker_status_screen_open = 0;
int32_t g_usetree;                                  /* DAT_0438b17c */

static scene1_walker_stage_texture_fn s_hook_kabe_tex     = NULL;
static scene1_walker_stage_texture_fn s_hook_yuka_tex     = NULL;
static scene1_walker_stage_texture_fn s_hook_jutan_tex    = NULL;
static scene1_walker_stage_texture_fn s_hook_animated_tex = NULL;

void scene1_walker_set_kabe_texture_hook(scene1_walker_stage_texture_fn fn)
{ s_hook_kabe_tex = fn; }

void scene1_walker_set_yuka_texture_hook(scene1_walker_stage_texture_fn fn)
{ s_hook_yuka_tex = fn; }

void scene1_walker_set_jutan_texture_hook(scene1_walker_stage_texture_fn fn)
{ s_hook_jutan_tex = fn; }

void scene1_walker_set_animated_texture_hook(scene1_walker_stage_texture_fn fn)
{ s_hook_animated_tex = fn; }

static int default_shop_table_selector_hook(void)
{
    return g_scene_table_selector;
}

static scene1_walker_int_fn s_hook_shop_table_selector =
    default_shop_table_selector_hook;

void scene1_walker_set_shop_table_selector_hook(scene1_walker_int_fn fn)
{
    s_hook_shop_table_selector = fn ? fn : default_shop_table_selector_hook;
}

static void scene1_walker_phase2b_reset_internal(void)
{
    g_scene1_walker_status_screen_open = 0;
    s_hook_kabe_tex     = NULL;
    s_hook_yuka_tex     = NULL;
    s_hook_jutan_tex    = NULL;
    s_hook_animated_tex = NULL;
    s_hook_shop_table_selector = default_shop_table_selector_hook;
}

/* Pure-C classifier — mirrors decomp L52813-L52870 nested-if cascade.
 * Engine reads 6 byte flags via local_28[0x1cf2c43..0x1cf2d3d] = the
 * water / kabe_ / yuka_ / shop_jutan / ext_tga / hikari side-tables
 * indexed by cache slot (PII.0 findings).  Order of the cascade is
 * preserved verbatim. */
scene1_walker_slot_action scene1_walker_classify_slot(
    int water_flag, int kabe_flag, int yuka_flag, int shop_jutan_flag,
    int ext_tga_flag, int hikari_flag, int param_1)
{
    if (water_flag == 0) {
        if (kabe_flag == 0) {
            if (yuka_flag == 0) {
                if (shop_jutan_flag == 0) {
                    if (ext_tga_flag == 0) {
                        if (hikari_flag == 0) {
                            /* All classification flags zero —
                             * default sprite path. */
                            if (param_1 == 0) return SCENE1_WALKER_SLOT_DEFAULT;
                        } else {
                            if (param_1 == 3) return SCENE1_WALKER_SLOT_HIKARI;
                        }
                    } else {
                        if (param_1 == 1) return SCENE1_WALKER_SLOT_EXT_TGA;
                    }
                } else {
                    if (param_1 == 0) return SCENE1_WALKER_SLOT_JUTAN;
                }
            } else {
                if (param_1 == 0) return SCENE1_WALKER_SLOT_YUKA;
            }
        } else {
            if (param_1 == 0) return SCENE1_WALKER_SLOT_KABE;
        }
    } else {
        if (param_1 == 2) return SCENE1_WALKER_SLOT_WATER;
    }
    return SCENE1_WALKER_SLOT_SKIP;
}

/* Pure-C mesh-index calculator — mirrors asm 0x4583b8..0x4583f8.
 * For per-mesh flag == 0 → shop_table path (DAT_073b1ac8 = our
 * g_scene_table); for flag != 0 → wall/floor path (DAT_068dcca0).
 * Returns the resolved slot index for the caller to apply against the
 * destination array; -1 if the result would be out of bounds for the
 * shop_table path's [0, 16) range. */
int scene1_walker_draw_b_mesh_index(int mesh_type_value, int32_t flag_value,
                                    int selector, int *out_use_shop_table)
{
    int use_shop = (flag_value == 0);
    if (out_use_shop_table) *out_use_shop_table = use_shop ? 1 : 0;
    if (use_shop) {
        /* asm: lea eax, [ecx + eax*2 - 3]  ; ecx=mesh_type, eax=selector */
        int idx = mesh_type_value + selector * 2 - 3;
        if (idx < 0 || idx >= SCENE_TABLE_SLOT_COUNT) return -1;
        return idx;
    } else {
        /* asm: sar eax, 0x6; lea eax, [ecx + eax*2 - 0x28a0] */
        int32_t shifted = (int32_t)flag_value >> 6;  /* arithmetic shift */
        return mesh_type_value + (int)shifted * 2 - 0x28a0;
    }
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "mesh.h"
#include "mesh_load.h"
#include "light_debug.h"
#include "study_toggles.h"
#include "scene1_emit_record.h"
#include "scene_map_meshes.h"
#include "sprite.h"
#include "floor_layout.h"     /* DAT_073e03ac, DAT_0438c148 */
#include "scene1_camera.h"    /* DAT_073de31c / 324 */
#include "scene1_maplight.h"  /* the stage record (DAT_068dd2f0) */
#include "dungeon_objects.h"  /* DAT_0695ef78 */
#include "model_anchor.h"     /* the animated models' frame trees */
#include "model_render.h"     /* FUN_00403ca7's device states */
#include <math.h>

/* Engine `face_npc_ptr[face_i] == current_slot` is per-face NPC
 * association; our mesh_t uses per-MATERIAL texture_slots[mat_i] for
 * the same filter (PII.0 finding — the engine's `face_npc_ptr` is the
 * texture-cache slot, populated by mesh_load).  We draw entire
 * submeshes (which group faces by material), which is texture-binding
 * equivalent for static shop_table furniture meshes. */
/* light-debug (F5): set while the pass-3 hikari slot loop runs so
 * draw_loop_b_mesh can wrap each plane's draw with the visualization
 * state overrides.  Never set on the trace/TAS paths (mode is
 * interactive-only). */
static int g_hikari_debug_scope = 0;

/* study toggle (filming; see study_toggles.h): set while the PASS-3
 * (hikari god-ray plane) slot loop runs — the same draws light_debug
 * wraps.  With the hikari toggle OFF, draw_loop_b_mesh skips them
 * entirely.  Default ON ⇒ flag read only, behaviour bit-identical. */
static int g_hikari_pass_scope = 0;

static void draw_loop_b_mesh(IDirect3DDevice8 *dev,
                             const mesh_t *m,
                             int slot,
                             const float matrix[16])
{
    if (g_hikari_pass_scope && !study_toggle_on(STUDY_T_HIKARI))
        return;                       /* study: hikari planes off */
    if (!m || !m->vb || !m->ib) return;
    if (m->material_count <= 0 || !m->texture_slots) return;
    if (m->submesh_count <= 0) return;

    /* FVF for the mesh VB (created with MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1
     * in mesh.c).  The engine's per-mesh draw helper re-sets this before
     * every DrawIndexedPrimitive (retail D3D trace: ret_va 0xadf1b sets
     * SetVertexShader(0x152) 56× per HOUSE frame — once per mesh draw).
     * draw_loop_b_mesh used to rely on the base pass's one-time
     * SetVertexShader(0x152) in scene1_render.c, which held for draw
     * loop A's base draws but NOT the hikari pass: by then an earlier
     * sub-pass (Pass F / quad, FVF 0x142) had clobbered it, so the
     * hikari god-ray submeshes drew with FVF 0x142 (no NORMAL) → FFP
     * lighting fell back to flat vertex colour (cyan), not the
     * normal-lit warm green retail produces.  Set it per mesh, like the
     * standalone path (mesh_draw.c) and the engine. */
    IDirect3DDevice8_SetVertexShader(dev, MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1);

    /* L52966: SetTransform(D3DTS_WORLD, &local_5f8[i]) — engine's
     * matrix_array[i] is one 64 B D3DMATRIX per phase-2 mesh. */
    IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                  (const D3DMATRIX *)matrix);

    IDirect3DDevice8_SetStreamSource(dev, 0,
                                     (IDirect3DVertexBuffer8 *)m->vb,
                                     (UINT)sizeof(mesh_vertex));

    for (int32_t s = 0; s < m->submesh_count; s++) {
        const mesh_submesh *sm = &m->submeshes[s];
        if (sm->vertex_count <= 0 || sm->index_count < 3) continue;
        int mat_idx = sm->material_index;
        if (mat_idx < 0 || mat_idx >= m->material_count) continue;
        /* Per-material filter: L52973 in decomp (`face_npc_ptr[face_i]
         * == current_slot` — see PII.0 mapping in the docs). */
        if (m->texture_slots[mat_idx] != slot) continue;

        IDirect3DDevice8_SetIndices(dev,
                                    (IDirect3DIndexBuffer8 *)m->ib,
                                    (UINT)sm->vertex_offset);

        /* Material — engine duplicates Ambient = Diffuse and forces
         * Specular.a / Emissive.a = 1 (same shape as
         * scene1_emit_record and mesh_draw_d3d8). */
        D3DMATERIAL8 mat = {0};
        const xfile_material *mm = &m->materials[mat_idx];
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

        if (g_hikari_debug_scope) light_debug_plane_predraw((struct IDirect3DDevice8 *)dev);

        IDirect3DDevice8_DrawIndexedPrimitive(
            dev, D3DPT_TRIANGLELIST,
            0,
            (UINT)sm->vertex_count,
            (UINT)sm->index_offset,
            (UINT)(sm->index_count / 3));

        if (g_hikari_debug_scope && light_debug_wire_enabled()) {
            /* border mode: same draw again as a wireframe overlay */
            light_debug_wire_begin((struct IDirect3DDevice8 *)dev);
            IDirect3DDevice8_DrawIndexedPrimitive(
                dev, D3DPT_TRIANGLELIST,
                0,
                (UINT)sm->vertex_count,
                (UINT)sm->index_offset,
                (UINT)(sm->index_count / 3));
            light_debug_wire_end((struct IDirect3DDevice8 *)dev);
        }
    }
}

/* Resolve the IDirect3DBaseTexture8* to bind for a slot whose
 * classification is `action`.  Returns NULL when the action SetTexture
 * source isn't currently bound (default null hook).  Caller decides
 * whether to skip the SetTexture call vs. pass NULL through. */
static IDirect3DBaseTexture8 *pick_texture_for_action(
    int slot, scene1_walker_slot_action action)
{
    switch (action) {
        case SCENE1_WALKER_SLOT_DEFAULT:
        case SCENE1_WALKER_SLOT_EXT_TGA: {
            if (slot < 0 || slot >= g_mesh_tex_cache.count) return NULL;
            sprite_t *spr =
                (sprite_t *)g_mesh_tex_cache.entries[slot].sprite;
            return spr ? (IDirect3DBaseTexture8 *)spr->tex : NULL;
        }
        case SCENE1_WALKER_SLOT_KABE:
            return s_hook_kabe_tex
                ? (IDirect3DBaseTexture8 *)s_hook_kabe_tex() : NULL;
        case SCENE1_WALKER_SLOT_YUKA:
            return s_hook_yuka_tex
                ? (IDirect3DBaseTexture8 *)s_hook_yuka_tex() : NULL;
        case SCENE1_WALKER_SLOT_JUTAN:
            return s_hook_jutan_tex
                ? (IDirect3DBaseTexture8 *)s_hook_jutan_tex() : NULL;
        case SCENE1_WALKER_SLOT_HIKARI:
        case SCENE1_WALKER_SLOT_WATER:
            /* Engine binds the animated frame table DAT_073aa198[
             * (draw_counter/wateranimspeed) % wateranimnum] (decomp
             * L52825-52837 / L52871-52882).  For HOUSE the hikari
             * effect is a single static frame whose texture is
             * xfile/shop/hikari.bmp — loaded as the embedded material
             * texture of shop_1st.x, so it already sits in the mesh tex
             * cache at this very slot (the slot mesh_load flagged
             * `hikari`, mesh_load.c:63).  With no animated-frame table
             * ported, bind the slot's own sprite: identical result for
             * the single-frame case, and it kills the NULL-texture
             * fallback that drew the god-ray submeshes as opaque
             * material-coloured frustum geometry.  An explicit
             * animated-texture hook (water frame cycling) still wins
             * when installed. */
            if (s_hook_animated_tex)
                return (IDirect3DBaseTexture8 *)s_hook_animated_tex();
            if (slot < 0 || slot >= g_mesh_tex_cache.count) return NULL;
            {
                sprite_t *spr =
                    (sprite_t *)g_mesh_tex_cache.entries[slot].sprite;
                return spr ? (IDirect3DBaseTexture8 *)spr->tex : NULL;
            }
        case SCENE1_WALKER_SLOT_SKIP:
        default:
            return NULL;
    }
}

/* FUN_00457714's dungeon branch (stage maptype >= 1, asm 0x45775d..): each
 * floor cell (DAT_073e03ac, W × H) as S(-0.2, 0.2, 0.2) · RotY(-pat · π/2) ·
 * T(-40 col, 0, -40 row) (no rotation for an empty cell, whose mesh is 11);
 * then per texture slot: water → pass 2, ext-tga → pass 1, hikari → pass 3
 * (not with DAT_0438b180 1), the rest → pass 0, and every cell within 60 of
 * the eye (120 with the camera class b4e8 set; the z distance halved) draws
 * its mesh's subsets of that slot.  DAT_0438c148 set: only cell (1, 1), no
 * cull.  PORT-DEBT(render-coupled, FUN_00457714): the movers (DAT_0438c150,
 * the final boss) and the DAT_0438b194 texture / lighting reset. */
static void scene1_walker_pass_render_dungeon(IDirect3DDevice8 *dev, int param_1)
{
    static const float k40 = 40.0f, k_pi2 = 1.5707963705062866f, k0p2 = 0.2f, km0p2 = -0.2f;
    static const float k0p5 = 0.5f, k60 = 60.0f, k120 = 120.0f;
    static float cell_m[FLOOR_GRID_CELLS * 16];
    for (int row = 0; row < g_floor_h && row < FLOOR_GRID_STRIDE; row++) {
        for (int col = 0; col < g_floor_w && col < FLOOR_GRID_STRIDE; col++) {
            const floor_room_t *c = &g_floor_rooms[row * FLOOR_GRID_STRIDE + col];
            float *m = &cell_m[(row * FLOOR_GRID_STRIDE + col) * 16];
            const float z = (float)((long double)(float)-row * k40);
            const float x = (float)((long double)(float)-col * k40);
            mat4_translation(m, x, 0.0f, z);
            if (c->type != -1) {
                float ry[16];
                mat4_rotation_y(ry, (float)((long double)(float)-c->pat * k_pi2));
                mat4_mul(m, ry, m);
            }
            float sc[16];
            mat4_scaling(sc, km0p2, k0p2, k0p2);
            mat4_mul(m, sc, m);
        }
    }
    scene1_emit_preamble((struct IDirect3DDevice8 *)dev);
    int slot_count = g_mesh_tex_cache.count;
    if (slot_count > MESH_TEX_CACHE_CAP) slot_count = MESH_TEX_CACHE_CAP;
    int animated_set = 0;
    for (int slot = 0; slot < slot_count; slot++) {
        const mesh_tex_flags *f = &g_mesh_tex_cache.entries[slot].flags;
        if (f->water) {
            if (param_1 != 2) continue;
            if (!animated_set) {
                animated_set = 1;
                IDirect3DDevice8_SetTexture(dev, 0, pick_texture_for_action(slot, SCENE1_WALKER_SLOT_WATER));
            }
        } else if (f->ext_tga) {
            if (param_1 != 1) continue;
            IDirect3DDevice8_SetTexture(dev, 0, pick_texture_for_action(slot, SCENE1_WALKER_SLOT_EXT_TGA));
        } else if (f->hikari) {
            if (param_1 != 3) continue;
            if (!animated_set) {
                animated_set = 1;
                IDirect3DDevice8_SetTexture(dev, 0, pick_texture_for_action(slot, SCENE1_WALKER_SLOT_HIKARI));
            }
        } else {
            if (param_1 != 0) continue;
            IDirect3DDevice8_SetTexture(dev, 0, pick_texture_for_action(slot, SCENE1_WALKER_SLOT_DEFAULT));
        }
        scene1_emit_apply_material_state((struct IDirect3DDevice8 *)dev, slot);
        for (int row = 0; row < g_floor_h && row < FLOOR_GRID_STRIDE; row++) {
            for (int col = 0; col < g_floor_w && col < FLOOR_GRID_STRIDE; col++) {
                const floor_room_t *c = &g_floor_rooms[row * FLOOR_GRID_STRIDE + col];
                const int mi = c->type == -1 ? 11 : c->type;
                if (g_floor_c148 != 0) {
                    if (col != 1 || row != 1) continue;
                } else {
                    const float dx = (float)((long double)(float)-col * k40 - g_scene1_camera_eye[0]);
                    const float dz = (float)(((long double)(float)-row * k40 - g_scene1_camera_eye[2]) * k0p5);
                    const float d = (float)sqrt((double)dz * dz + (double)dx * dx);
                    if (d > (g_scene1_camera_stage_class != 0 ? k120 : k60)) continue;
                }
                mesh_t *m = scene_map_meshes_get(mi);
                if (m) draw_loop_b_mesh(dev, m, slot, &cell_m[(row * FLOOR_GRID_STRIDE + col) * 16]);
            }
        }
    }
}

/* FUN_00459847(pass): the dungeon objects (DAT_0695ef78) whose type record
 * (DAT_005c4c90 + 36·type) names this pass (+0x1c): the kind-0 types draw
 * their static mesh (DAT_068dcfc0[+4]) with world RotZ(+0x78) · RotX(+0x74) ·
 * RotY(+0x70) · S(-0.2·sx, 0.2·sy, 0.2·sz) · T(pos + (+0x18, 0, +0x20)), per
 * texture slot; with the +0x20 cull flag only within 50 of the eye (z
 * halved).  Pass 3 blends additively.  The kind-1 types draw an animated
 * model (DAT_073cba20: takarabako / obj_door) first. */
static const int32_t k_dobj_type[23][9] = {
    { 0, 0, -1, 0x40400000, 0x41200000, 0x3f800000, 1, 1, 1 },
    { 0, 1, -1, 0x40000000, 0x40000000, 0x3f000000, 0, 1, 1 },
    { 0, 2, -1, 0x3f800000, 0x41200000, 0, 1, 1, 1 },
    { 0, 3, -1, 0x3f800000, 0x41200000, 0, 1, 1, 1 },
    { 1, 2, 1, 0x40a00000, 0x40c00000, 0, 1, 1, 1 },
    { 1, 2, 1, 0x40a00000, 0x40c00000, 0, 1, 1, 1 },
    { 1, 3, 1, 0x40800000, 0x40c00000, 0, 1, 1, 1 },
    { 1, 1, 3, 0x3e99999a, 0x40000000, 0, 1, 1, 1 },
    { -1, 0, 5, 0x3e99999a, 0x40000000, 0, 1, 1, 1 },
    { 0, 11, -1, 0x3f4ccccd, 0x41200000, 0, 1, 1, 1 },
    { 0, 4, -1, 0x3f4ccccd, 0x41200000, 0, 1, 1, 1 },
    { 0, 5, -1, 0x3f4ccccd, 0x41200000, 0, 1, 1, 1 },
    { 0, 6, -1, 0x3f4ccccd, 0x41200000, 0, 1, 1, 1 },
    { 0, 7, -1, 0x3f4ccccd, 0x41200000, 0, 1, 0, 0 },
    { 0, 8, -1, 0x3f4ccccd, 0x41200000, 0, 1, 0, 0 },
    { 0, 9, -1, 0x3f4ccccd, 0x41200000, 0, 1, 0, 0 },
    { 0, 10, -1, 0x3f4ccccd, 0x41200000, 0, 1, 0, 0 },
    { 0, 11, -1, 0, 0x41200000, 0, 0, 2, 0 },
    { 0, 12, -1, 0, 0x41200000, 0, 0, 2, 0 },
    { 0, 5, -1, 0x3f4ccccd, 0x41200000, 0, 1, 1, 1 },
    { 0, 6, -1, 0x3f4ccccd, 0x41200000, 0, 1, 0, 0 },
    { 0, 12, -1, 0, 0x41200000, 0, 0, 1, 1 },
    { 0, 10, 1, 0x3f800000, 0x41200000, 0x3e99999a, 1, 3, 1 },
};

/* DAT_073cba20[4]: the dungeon objects' animated models (0x2f0 each) — 2
 * xfile/tree/takarabako.x, 3 xfile/tree/obj_door.x (FUN_00474a9a's dungeon arm,
 * FUN_00403b6e); 0 / 1 are not loaded by the paths ported so far */
static xfile_hier *s_dobj_hier[4];
static mesh_t     *s_dobj_model[4];

void scene1_walker_dobj_load(struct IDirect3DDevice8 *dev)
{
    static const char *const k_path[4] = { NULL, NULL, "xfile/tree/takarabako.x", "xfile/tree/obj_door.x" };
    for (int i = 0; i < 4; i++) {
        if (k_path[i] == NULL) continue;
        if (s_dobj_model[i]) { mesh_free(s_dobj_model[i]); s_dobj_model[i] = NULL; }
        if (s_dobj_hier[i] == NULL) s_dobj_hier[i] = model_hier_load(k_path[i]);
        mesh_t *m = mesh_load_hier(k_path[i]);
        if (m == NULL) continue;
        if (m->error[0]) { mesh_free(m); continue; }
        if (dev) (void)mesh_load_finalize_win32(m, dev);
        s_dobj_model[i] = m;
        model_render_loader_state(dev);   /* FUN_00403b6e → FUN_00403ca7 */
    }
}

/* FUN_004047df / FUN_00403eb7: each frame's container drawn with WORLD = the
 * frame's combined matrix, per material SetMaterial + SetTexture + DrawSubset */
static void dobj_model_draw(IDirect3DDevice8 *dev, const mesh_t *m, xfile_hier *h)
{
    if (m == NULL || h == NULL || !m->vb || !m->ib) return;
    IDirect3DDevice8_SetVertexShader(dev, MESH_FVF_XYZ_NORMAL_DIFFUSE_TEX1);
    IDirect3DDevice8_SetStreamSource(dev, 0, (IDirect3DVertexBuffer8 *)m->vb, (UINT)sizeof(mesh_vertex));
    for (int32_t s = 0; s < m->submesh_count; s++) {
        const mesh_submesh *sm = &m->submeshes[s];
        if (sm->vertex_count <= 0 || sm->index_count < 3) continue;
        const xfile_sframe *f = xfile_find_frame(h->root, sm->frame);
        if (f == NULL) f = h->root;
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)f->combined);
        const int mat_idx = sm->material_index;
        IDirect3DBaseTexture8 *tex = NULL;
        if (mat_idx >= 0 && mat_idx < m->material_count) {
            const xfile_material *mm = &m->materials[mat_idx];
            D3DMATERIAL8 mat = {0};
            mat.Diffuse.r = mm->diffuse.r; mat.Diffuse.g = mm->diffuse.g;
            mat.Diffuse.b = mm->diffuse.b; mat.Diffuse.a = mm->diffuse.a;
            mat.Ambient = mat.Diffuse;
            mat.Specular.r = mm->specular.r; mat.Specular.g = mm->specular.g;
            mat.Specular.b = mm->specular.b; mat.Specular.a = 1.0f;
            mat.Emissive.r = mm->emissive.r; mat.Emissive.g = mm->emissive.g;
            mat.Emissive.b = mm->emissive.b; mat.Emissive.a = 1.0f;
            mat.Power = mm->power;
            IDirect3DDevice8_SetMaterial(dev, &mat);
            if (m->texture_slots) tex = pick_texture_for_action(m->texture_slots[mat_idx], SCENE1_WALKER_SLOT_DEFAULT);
        }
        IDirect3DDevice8_SetTexture(dev, 0, tex);
        IDirect3DDevice8_SetIndices(dev, (IDirect3DIndexBuffer8 *)m->ib, (UINT)sm->vertex_offset);
        IDirect3DDevice8_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, 0, (UINT)sm->vertex_count,
                                              (UINT)sm->index_offset, (UINT)(sm->index_count / 3));
    }
}

void scene1_walker_dobj_render(struct IDirect3DDevice8 *dev_in, int pass)
{
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    if (!dev) return;
    if (pass == 3) {
        IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_ONE);
    } else {
        IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_CCW);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);
    }
    {
        const stage_record_t *rec = scene1_current_stage_record();
        const int lit = rec != NULL && rec->maplight != 0;
        IDirect3DDevice8_LightEnable(dev, 0, lit);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, lit);
    }
    /* the first loop: the kind-1 types' animated models, at the dobj's
     * per-track times (+0xd4), root RotY(yaw) · S and T(pos + offset) */
    for (int i = 0; i < DOBJ_COUNT; i++) {
        const dobj_t *o = &g_dobj[i];
        const int32_t t = o->i[35];
        if (t == -1 || o->i[35 + 119] == 3) continue;
        if (t < 0 || t >= 23 || k_dobj_type[t][7] != pass) continue;
        if (g_usetree == 1 && t < 4) continue;
        if (k_dobj_type[t][0] != 1) continue;
        const int k = k_dobj_type[t][1];
        if (k < 0 || k >= 4 || s_dobj_model[k] == NULL || s_dobj_hier[k] == NULL) continue;
        float tr[16], rot[16], ry[16];
        mat4_translation(tr, o->f[0] + o->f[6], o->f[1], o->f[2] + o->f[8]);
        mat4_scaling(rot, o->f[32] * -0.2f, o->f[33] * 0.2f, o->f[34] * 0.2f);
        mat4_rotation_y(ry, o->f[28]);
        mat4_mul(rot, ry, rot);
        model_hier_pose(s_dobj_hier[k], &o->i[53], &o->i[53], 0.0f, rot, tr);
        dobj_model_draw(dev, s_dobj_model[k], s_dobj_hier[k]);
    }
    static float world[DOBJ_COUNT][16];
    static uint8_t on[DOBJ_COUNT];
    for (int i = 0; i < DOBJ_COUNT; i++) {
        const dobj_t *o = &g_dobj[i];
        const int32_t t = o->i[35];
        on[i] = 0;
        if (t == -1 || o->i[35 + 119] == 3) continue;
        if (t < 0 || t >= 23 || k_dobj_type[t][7] != pass) continue;
        if (g_usetree == 1 && t < 4) continue;
        const float px = o->f[0] + o->f[6], pz = o->f[2] + o->f[8];
        if (k_dobj_type[t][8] != 0) {
            const float dx = px - g_scene1_camera_eye[0];
            const float dz = (pz - g_scene1_camera_eye[2]) * 0.5f;
            if (sqrtf(dz * dz + dx * dx) > 50.0f) continue;
        }
        if (k_dobj_type[t][0] != 0) continue;
        on[i] = 1;
        float *w = world[i], m[16];
        mat4_translation(w, px, o->f[1], pz);
        mat4_scaling(m, o->f[32] * -0.2f, o->f[33] * 0.2f, o->f[34] * 0.2f);
        mat4_mul(w, m, w);
        mat4_rotation_y(m, o->f[28]);
        mat4_mul(w, m, w);
        mat4_rotation_x(m, o->f[29]);
        mat4_mul(w, m, w);
        mat4_rotation_z(m, o->f[30]);
        mat4_mul(w, m, w);
    }
    scene1_emit_preamble((struct IDirect3DDevice8 *)dev);
    int slot_count = g_mesh_tex_cache.count;
    if (slot_count > MESH_TEX_CACHE_CAP) slot_count = MESH_TEX_CACHE_CAP;
    for (int slot = 0; slot < slot_count; slot++) {
        int bound = 0;
        for (int i = 0; i < DOBJ_COUNT; i++) {
            if (!on[i]) continue;
            mesh_t *mm = scene_map_meshes_dobj(k_dobj_type[g_dobj[i].i[35]][1]);
            if (mm == NULL || mm->texture_slots == NULL) continue;
            int any = 0;
            for (int s = 0; s < mm->submesh_count; s++) {
                const int mi = mm->submeshes[s].material_index;
                if (mi >= 0 && mi < mm->material_count && mm->texture_slots[mi] == slot) { any = 1; break; }
            }
            if (!any) continue;
            if (!bound) {
                bound = 1;
                IDirect3DDevice8_SetTexture(dev, 0, pick_texture_for_action(slot, SCENE1_WALKER_SLOT_DEFAULT));
                scene1_emit_apply_material_state((struct IDirect3DDevice8 *)dev, slot);
            }
            draw_loop_b_mesh(dev, mm, slot, world[i]);
        }
    }
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
}

void scene1_walker_pass_render_house(struct IDirect3DDevice8 *dev_in,
                                     int param_1)
{
    /* E.2 probe — FUN_00457714 @ 0x457714 (HOUSE furniture renderer). */
    CALL_TRACE_ENTER(0x457714u);

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;
    {
        const stage_record_t *rec = scene1_current_stage_record();
        if (rec != NULL && rec->maptype >= 1) {   /* DAT_068dd2f8[stage] */
            scene1_walker_pass_render_dungeon(dev, param_1);
            return;
        }
    }

    /* Build phase 2 matrices into a per-call scratch buffer.  Static
     * lifetime is fine — render is single-threaded and the buffer is
     * fully rewritten each call. */
    static float phase2_matrices[SCENE1_WALKER_PHASE2_MAX * 16];
    int phase2_n = scene1_walker_phase2_compute(phase2_matrices);

    /* PII.3c — phase-1 (wall/floor/jutan) matrices for draw loop A. */
    static float phase1_matrices[SCENE1_WALKER_PHASE1_MAX * 16];
    int phase1_n = scene1_walker_phase1_compute(phase1_matrices);

    /* L52806: FUN_00454f7c() barrier (same as scene1_emit_preamble). */
    scene1_emit_preamble((struct IDirect3DDevice8 *)dev);

    /* light-debug overlay (F6): pass 3 draws exactly the hikari god-ray
     * planes, so the whole pass is the recolour scope.  Gated on the
     * OVERLAY, not the free camera — flying the camera with the scene
     * rendered normally is the default (diorama-reveal recording). */
    int hikari_dbg = (param_1 == 3) && light_debug_overlay_active();
    if (hikari_dbg) {
        light_debug_hikari_begin();
        g_hikari_debug_scope = 1;
    }
    g_hikari_pass_scope = (param_1 == 3);   /* study hikari-off scope */

    int slot_count = g_mesh_tex_cache.count;
    if (slot_count <= 0) {
        g_hikari_debug_scope = 0;
        g_hikari_pass_scope = 0;
        return;
    }
    if (slot_count > MESH_TEX_CACHE_CAP) slot_count = MESH_TEX_CACHE_CAP;

    /* Engine `local_2c`: arms-once-per-outer-pass latch for the
     * animated overlay binding (water / hikari arms).  Once set, the
     * stride doesn't rebind. */
    int animated_overlay_set = 0;

    int selector = s_hook_shop_table_selector();
    scene1_walker_phase2_flag_fn flag_hook = scene1_walker_phase2_get_flag_hook();

    for (int slot = 0; slot < slot_count; slot++) {
        const mesh_tex_flags *f = &g_mesh_tex_cache.entries[slot].flags;
        scene1_walker_slot_action action = scene1_walker_classify_slot(
            f->water, f->kabe_, f->yuka_, f->shop_jutan,
            f->ext_tga, f->hikari, param_1);

        if (action == SCENE1_WALKER_SLOT_SKIP) continue;

        /* Texture binding.  HIKARI/WATER fire SetTexture only once per
         * outer pass (engine `local_2c` latch).  All other actions
         * fire SetTexture per slot. */
        if (action == SCENE1_WALKER_SLOT_HIKARI ||
            action == SCENE1_WALKER_SLOT_WATER) {
            if (!animated_overlay_set) {
                animated_overlay_set = 1;
                IDirect3DDevice8_SetTexture(
                    dev, 0, pick_texture_for_action(slot, action));
            }
        } else {
            IDirect3DDevice8_SetTexture(
                dev, 0, pick_texture_for_action(slot, action));
        }

        /* L52883: per-cache-slot TextureStageState picker
         * (FUN_00454fe4 = scene1_emit_apply_material_state). */
        scene1_emit_apply_material_state(
            (struct IDirect3DDevice8 *)dev, slot);

        /* L52887-L52901: per-slot BSS palette writes
         * (DAT_0438bfbc..0438bff0).  Skipped — those globals feed the
         * pulse path (DAT_0438cc08==2 gate) which is BSS-zero in HOUSE
         * and never observable.  When the pulse path lands (likely
         * post-Cf.*), revisit. */

        /* L52902-L52950: draw loop A (wall/floor/jutan, DAT_068dcca0)
         * — PII.3c.  Draw each phase-1 instance's mesh (room + carpet
         * for HOUSE) filtered to this cache slot's textures.
         *
         * Engine distance-cull (L52908: threshold `*DAT_068dd2f0+0x1a78`
         * vs eye-distance): the HOUSE threshold is 1000 world units while
         * the room/carpet sit at the origin ~25 units from the camera, so
         * the cull never rejects — we draw unconditionally.  Draw loop A
         * is NOT gated on the status-screen flag (that gates only draw
         * loop B), so it runs before that gate below. */
        for (int i = 0; i < phase1_n; i++) {
            mesh_t *m = scene_map_meshes_get(g_scene1_walker_phase1_mesh_index[i]);
            if (!m) continue;
            draw_loop_b_mesh(dev, m, slot, &phase1_matrices[i * 16]);
        }

        /* L52952: draw loop B gate. */
        if (g_scene1_walker_status_screen_open != 0) continue;
        if (phase2_n <= 0) continue;

        for (int i = 0; i < phase2_n; i++) {
            int32_t flag = flag_hook ? flag_hook(i) : 0;
            int use_shop_table = 0;
            int idx = scene1_walker_draw_b_mesh_index(
                g_scene1_walker_phase2_mesh_type[i], flag, selector,
                &use_shop_table);

            const mesh_t *m = NULL;
            if (use_shop_table) {
                if (idx < 0) continue;
                m = g_scene_table[idx];
            } else {
                /* wall/floor/jutan path (DAT_068dcca0).  Mesh array
                 * not exposed in our port today — PII.3c scope. */
                continue;
            }
            if (!m) continue;

            draw_loop_b_mesh(dev, m, slot, &phase2_matrices[i * 16]);
        }
    }

    g_hikari_pass_scope = 0;
    if (hikari_dbg) {
        g_hikari_debug_scope = 0;
        light_debug_hikari_end((struct IDirect3DDevice8 *)dev);
    }
}

#endif /* _WIN32 */
