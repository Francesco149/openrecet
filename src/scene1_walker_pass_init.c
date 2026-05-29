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
#include "scene1_emit_record.h"
#include "scene_map_meshes.h"
#include "sprite.h"

/* Engine `face_npc_ptr[face_i] == current_slot` is per-face NPC
 * association; our mesh_t uses per-MATERIAL texture_slots[mat_i] for
 * the same filter (PII.0 finding — the engine's `face_npc_ptr` is the
 * texture-cache slot, populated by mesh_load).  We draw entire
 * submeshes (which group faces by material), which is texture-binding
 * equivalent for static shop_table furniture meshes. */
static void draw_loop_b_mesh(IDirect3DDevice8 *dev,
                             const mesh_t *m,
                             int slot,
                             const float matrix[16])
{
    if (!m || !m->vb || !m->ib) return;
    if (m->material_count <= 0 || !m->texture_slots) return;
    if (m->submesh_count <= 0) return;

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

        IDirect3DDevice8_DrawIndexedPrimitive(
            dev, D3DPT_TRIANGLELIST,
            0,
            (UINT)sm->vertex_count,
            (UINT)sm->index_offset,
            (UINT)(sm->index_count / 3));
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

void scene1_walker_pass_render_house(struct IDirect3DDevice8 *dev_in,
                                     int param_1)
{
    /* E.2 probe — FUN_00457714 @ 0x457714 (HOUSE furniture renderer). */
    CALL_TRACE_ENTER(0x457714u);

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

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

    int slot_count = g_mesh_tex_cache.count;
    if (slot_count <= 0) return;
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
}

#endif /* _WIN32 */
