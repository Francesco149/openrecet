/*
 * scene1_overlay.h — typed storage + spawn API for the 2D-overlay
 * particle dispatcher (FUN_00414ee2).
 *
 * Chip O.2 (2026-05-24) — first chip after the O.1 survey at
 * docs/findings/scene1-overlay-dispatcher.md.  Lands:
 *
 *   1. The 4096-slot × 220-byte record table (engine DAT_0064e820..72a890).
 *   2. The 43-dw × N-entry template table (engine DAT_00733884..).
 *   3. The 8-dw × N-entry per-shape texture/UV table (engine DAT_00769750..).
 *   4. scene1_overlay_spawn() — port of FUN_00414345 (1057 B), which
 *      walks the slot table for the first free slot, copies template
 *      constants, runs per-shape RNG-driven init (shapes 3/5/7/8/9/10),
 *      applies the shape-6 yaw override (param_8), and repeats up to
 *      template[2] (spawn_count) times.
 *
 * Dormant in HOUSE: no caller exists in the port today.  The two
 * dispatcher call sites (wide_followup mid_block_1 and the four
 * scene1_render_overlay sites) still call the TODO stubs, and the table
 * parser FUN_00475040 (chip O.10, populates DAT_0076b948 + the per-shape
 * and per-template tables) is also unported, so even with a caller wired
 * the dispatcher would render nothing.
 *
 * Engine globals consumed:
 *   thunk_FUN_005041f6 → rng_next15()    (rng.h)
 *   FUN_00471089       → rng_next_unit() (rng.h)
 *   FUN_00503a44 / 994 → sinf / cosf     (math.h, via libc on host)
 *   __ftol (call 0x503954)               → (int)(float) — resolved by raw asm,
 *                                          see "Age-stagger __ftol" below.
 *
 * Age-stagger __ftol (resolved 2026-05-24 via raw asm at 0x414728):
 *   The Ghidra decomp shows `iVar9 = __ftol();` with no visible argument
 *   right before `piVar10[1] = iVar9` (the age write).  Raw asm shows
 *   the FPU top-of-stack at that call is `(float)local_4 * template[7]`
 *   where local_4 starts at 0 and is DEC'd once per claimed slot inside
 *   the do-while loop.  So the first particle in a spawn burst gets
 *   age=0, the second gets age=(int)(-1 * template[7]), the third
 *   age=(int)(-2 * template[7]), etc.  Combined with the renderer's
 *   "age < 0 → skip" gate, template[7] gives each burst a configurable
 *   per-particle visibility-delay stagger.
 *
 *   No new pending-human-check needed — asm at 0x414728..0x414737 is
 *   unambiguous (fild [ebp-0x4] ; fstp [ebp+0x8] ; fld [ebp+0x8] ;
 *   fmul [edi+0x80] ; call 0x503954).  edi+0x80 = template_base+0x1c
 *   = template dw 7.
 *
 * Slot byte layout (dw-indexed; full 55-dw / 220-B record):
 *   The spawner writes through these offsets.  See O.1 survey for the
 *   renderer's reads.  Field names in comments match the survey.
 */
#ifndef SCENE1_OVERLAY_H
#define SCENE1_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Slot table ---------------------------------------------------- */

#define SCENE1_OVERLAY_SLOT_COUNT  4096
#define SCENE1_OVERLAY_SLOT_STRIDE 55   /* dw count; 220 bytes per slot */

/* Slot field offsets (dw indices within a 55-dw slot).  Spawner-touched
 * fields are named; others are documented but use _UNUSED labels until a
 * later chip names them. */
#define SCENE1_OVERLAY_OFF_TEXTURE_TYPE     0   /* +0x00 — template[0] copy */
#define SCENE1_OVERLAY_OFF_TYPE_SHAPE       1   /* +0x04 — template[1] copy; renderer dispatch */
#define SCENE1_OVERLAY_OFF_POS_X            2   /* +0x08 */
#define SCENE1_OVERLAY_OFF_POS_Y            3   /* +0x0c */
#define SCENE1_OVERLAY_OFF_POS_Z            4   /* +0x10 */
#define SCENE1_OVERLAY_OFF_VEL_X            5   /* +0x14 — type_shape==5 only */
#define SCENE1_OVERLAY_OFF_VEL_Y            6   /* +0x18 — type_shape==5 only */
#define SCENE1_OVERLAY_OFF_VEL_Z            7   /* +0x1c — type_shape==5 only */
#define SCENE1_OVERLAY_OFF_POS_X_COPY       8   /* +0x20 — second pos copy */
#define SCENE1_OVERLAY_OFF_POS_Y_COPY       9   /* +0x24 */
#define SCENE1_OVERLAY_OFF_POS_Z_COPY      10   /* +0x28 */
#define SCENE1_OVERLAY_OFF_BEND_X          11   /* +0x2c — zero-init; renderer "bend.x" */
#define SCENE1_OVERLAY_OFF_BEND_Y          12   /* +0x30 — zero-init; renderer "bend.y" */
#define SCENE1_OVERLAY_OFF_BEND_Z          13   /* +0x34 — zero-init; renderer "bend.z" */
#define SCENE1_OVERLAY_OFF_ROT_X           14   /* +0x38 — zero / RNG for shape 3, 7; param_8 for 6 */
#define SCENE1_OVERLAY_OFF_ROT_Y           15   /* +0x3c — see ROT_X note */
#define SCENE1_OVERLAY_OFF_ROT_Z           16   /* +0x40 — see ROT_X note */
#define SCENE1_OVERLAY_OFF_TEMPLATE5_COPY  17   /* +0x44 — template[5] copy */
#define SCENE1_OVERLAY_OFF_UNK_48          18   /* +0x48 — template[6] copy; renderer "unk_48" */
#define SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET 19   /* +0x4c — param_7 OR template[8] (param_7<1) */
#define SCENE1_OVERLAY_OFF_SCALE_X         20   /* +0x50 — template[10] copy */
#define SCENE1_OVERLAY_OFF_TEMPLATE11_COPY 21   /* +0x54 — template[11] copy */
#define SCENE1_OVERLAY_OFF_BLEND_MIX       22   /* +0x58 — template[14] copy */
#define SCENE1_OVERLAY_OFF_SCALE_Y_RATIO   23   /* +0x5c — template[12] copy */
#define SCENE1_OVERLAY_OFF_AGE_BIRTH       24   /* +0x60 — 0 OR (rng_next15 % template[9]) */
#define SCENE1_OVERLAY_OFF_FADE_IN_DUR     25   /* +0x64 — template[15] */
#define SCENE1_OVERLAY_OFF_FADE_OUT_DUR    26   /* +0x68 — template[16] */
#define SCENE1_OVERLAY_OFF_SHAPE_MODE      27   /* +0x6c — param_9 */
#define SCENE1_OVERLAY_OFF_ACTIVE          28   /* +0x70 — template_id (or -1 = empty) */
#define SCENE1_OVERLAY_OFF_AGE             29   /* +0x74 — (int)(local_4 * template[7]) */
#define SCENE1_OVERLAY_OFF_SCALE_BASE      30   /* +0x78 — (int)(param_6 * template[3]) */
#define SCENE1_OVERLAY_OFF_UNK_7C          31   /* +0x7c — zero-init */
#define SCENE1_OVERLAY_OFF_RNG_SEED        32   /* +0x80 — zero-init; renderer "rng_seed" */
#define SCENE1_OVERLAY_OFF_LAYER           33   /* +0x84 — sign-extended template byte 0x44 */
#define SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE 34   /* +0x88 byte 0 — template byte 0x45 */
#define SCENE1_OVERLAY_OFF_MODE            35   /* +0x8c — param_10 */
#define SCENE1_OVERLAY_OFF_OWNER_A         36   /* +0x90 — param_1 (template owner) */
#define SCENE1_OVERLAY_OFF_OWNER_B         37   /* +0x94 — param_1 (duplicated copy) */
/* dw 38..54 — integrator-side state (not touched by spawner). */

extern int32_t g_scene1_overlay_slots[SCENE1_OVERLAY_SLOT_COUNT *
                                      SCENE1_OVERLAY_SLOT_STRIDE];

/* Reset all slot ACTIVE fields to -1 (empty).  Other fields zeroed. */
void scene1_overlay_reset(void);

/* ---- Template table ------------------------------------------------ */
/*
 * Template stride is 43 dw (0x2b dw = 172 B / template byte stride 0xac).
 * The capacity is unknown — the in-engine parser FUN_00475040 (chip O.10,
 * unported) populates an unbounded count; the spawner's template_id arg
 * is bounded only by the parser's loaded count.  We pick a generous
 * 256-entry cap (matches the byte index range the slot's LAYER field
 * can take after sign extension).
 *
 * Template dw layout (relevant fields; spawner-touched):
 *   dw 0  — texture_type   → slot.TEXTURE_TYPE
 *   dw 1  — type_shape     → slot.TYPE_SHAPE (0..10 renderer dispatch)
 *   dw 2  — spawn_count    → cap on inner loop (local_c < spawn_count)
 *   dw 3  — scale_base_mul → slot.SCALE_BASE = (int)(param_6 * template[3])
 *   dw 4  — init_arg_4     → shape 5 spread radius; fall-through jitter base
 *   dw 5  — template5_copy → slot.TEMPLATE5_COPY
 *   dw 6  — template6_copy → slot.UNK_48
 *   dw 7  — age_stagger    → __ftol multiplier (PHC #17 resolved 2026-05-24)
 *   dw 8  — fade_out_offset_default → slot.FADE_OUT_OFFSET when param_7<1
 *   dw 9  — age_birth_mod  → slot.AGE_BIRTH = rng_next15() % this when >0
 *   dw 10 — scale_x        → slot.SCALE_X
 *   dw 11 — template11_copy → slot.TEMPLATE11_COPY
 *   dw 12 — scale_y_ratio  → slot.SCALE_Y_RATIO; also fall-through blend factor
 *   dw 13 — blend_offset   → fall-through jitter cos additive
 *   dw 14 — blend_mix      → slot.BLEND_MIX
 *   dw 15 — fade_in_dur    → slot.FADE_IN_DUR
 *   dw 16 — fade_out_dur   → slot.FADE_OUT_DUR
 *   dw 17 byte 0 — layer_id (signed)  → slot.LAYER (sign-extended)
 *   dw 17 byte 1 — blend_mode_byte    → slot.BLEND_MODE_BYTE (unsigned)
 *   dw 18..42 — integrator-side / additional renderer constants
 *               (unused by spawner; left zero in default-init).
 */
#define SCENE1_OVERLAY_TEMPLATE_COUNT  256
#define SCENE1_OVERLAY_TEMPLATE_STRIDE 43

#define SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE      0
#define SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE        1
#define SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT       2
#define SCENE1_OVERLAY_TPL_OFF_SCALE_BASE_MUL    3
#define SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4        4
#define SCENE1_OVERLAY_TPL_OFF_TEMPLATE5         5
#define SCENE1_OVERLAY_TPL_OFF_TEMPLATE6         6
#define SCENE1_OVERLAY_TPL_OFF_AGE_STAGGER       7
#define SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DEFAULT  8
#define SCENE1_OVERLAY_TPL_OFF_AGE_BIRTH_MOD     9
#define SCENE1_OVERLAY_TPL_OFF_SCALE_X          10
#define SCENE1_OVERLAY_TPL_OFF_TEMPLATE11       11
#define SCENE1_OVERLAY_TPL_OFF_SCALE_Y_RATIO    12
#define SCENE1_OVERLAY_TPL_OFF_BLEND_OFFSET     13
#define SCENE1_OVERLAY_TPL_OFF_BLEND_MIX        14
#define SCENE1_OVERLAY_TPL_OFF_FADE_IN_DUR      15
#define SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DUR     16
/* dw 17 holds layer (byte 0, signed) + blend_mode_byte (byte 1, unsigned). */
#define SCENE1_OVERLAY_TPL_OFF_LAYER_PAIR       17

extern int32_t g_scene1_overlay_templates[SCENE1_OVERLAY_TEMPLATE_COUNT *
                                          SCENE1_OVERLAY_TEMPLATE_STRIDE];

/* Zero the template table (parser-equivalent reset). */
void scene1_overlay_templates_reset(void);

/* ---- Per-shape texture/UV table ------------------------------------ */
/*
 * 8-dw (32 B) per entry, indexed by slot.TEXTURE_TYPE.  Engine size
 * unknown — we provision 256 entries.  Not touched by the spawner;
 * declared here so O.3+ renderer chips can populate it without a
 * second storage TU.
 */
#define SCENE1_OVERLAY_SHAPE_COUNT  256
#define SCENE1_OVERLAY_SHAPE_STRIDE 8

#define SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP    0
#define SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X  1
#define SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y  2
#define SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X    3
#define SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y    4
#define SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT  5
#define SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD 6
#define SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE    7

extern int32_t g_scene1_overlay_shapes[SCENE1_OVERLAY_SHAPE_COUNT *
                                       SCENE1_OVERLAY_SHAPE_STRIDE];

void scene1_overlay_shapes_reset(void);

/* Convenience: reset all three tables at once. */
void scene1_overlay_init(void);

/* ---- Per-layer texture table + count (mirrors engine globals) -----
 *
 * `g_scene1_overlay_layer_count` mirrors engine `DAT_0076b948` — the
 * number of GRP_02d_* texture groups parsed from the table file by
 * FUN_00475040 (chip O.10, unported).  Default 0 → dispatcher outer
 * loop never enters → render is a no-op (matches HOUSE dormancy).
 *
 * `g_scene1_overlay_layer_textures[i]` mirrors engine `DAT_073cc780 +
 * i * 0x10`'s first dw (the IDirect3DTexture8 *).  The engine's 16-B
 * per-entry stride wraps 3 unused trailing dwords; we model only the
 * texture pointer field.  Default NULL → SetTexture sees NULL on
 * every outer slot.  Capacity 256 matches the byte-index range of
 * `tex_group` (slot.texture_type → shape_entry.tex_group, byte). */
#define SCENE1_OVERLAY_LAYER_COUNT_MAX 256
extern int   g_scene1_overlay_layer_count;
extern void *g_scene1_overlay_layer_textures[SCENE1_OVERLAY_LAYER_COUNT_MAX];

/* Convenience: zero the layer count + texture slots (host-test helper). */
void scene1_overlay_layers_reset(void);

/* ---- D3D-free helpers (O.3) ---------------------------------------
 *
 * These live in scene1_overlay_helpers.c so host unit tests can link
 * them without pulling in <d3d8.h>.  scene1_overlay.c itself stays
 * #ifdef _WIN32 for the actual dispatcher entry (SetTransform /
 * SetTexture / DrawPrimitiveUP).  Same split as
 * scene1_wide_followup_helpers.c.
 *
 * Slot fields are read via SCENE1_OVERLAY_OFF_* constants from this
 * header.  Shape table fields use SCENE1_OVERLAY_SHAPE_OFF_*.  */

/* FVF 0x142 = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1 — 6 dw
 * (24 B) per vertex.  Matches engine DrawPrimitiveUP stride 0x18.  */
typedef struct {
    float    x, y, z;
    uint32_t diffuse;
    float    u, v;
} scene1_overlay_vertex;

#define SCENE1_OVERLAY_VBUF_VERT_COUNT 4

/* Full 5-gate cascade matching engine FUN_00414ee2 L75-L84 (asm
 * 0x414f12..0x414f57):
 *   1. slot[ACTIVE]                    != -1
 *   2. slot[LAYER]                     == param_layer
 *   3. slot[MODE]                      == param_mode
 *   4. shape_table[slot[TEXTURE_TYPE]].tex_group == outer_idx
 *   5. slot[AGE]                       >= 0
 *
 * Returns 1 if all gates pass, 0 otherwise.  Reads
 * `g_scene1_overlay_shapes` for gate 4. */
int scene1_overlay_should_emit(const int32_t *slot,
                               int param_layer, int param_mode,
                               int outer_idx);

/* Fade compute — engine FUN_00414ee2 L86-L117 (asm 0x414f6b..0x415056).
 *
 *   alpha = 255.0
 *   if fade_in_dur > 0:
 *       alpha = (age*255) / fade_in_dur,  clamp to 255
 *   if !(shape_mode == 4 && unk_48 != 0) && fade_out_dur > 0:
 *       delta = age - age_birth
 *       if (fade_out_offset - fade_out_dur) < delta:
 *           step  = 255 / fade_out_dur          (INT division, then to float)
 *           adj   = (delta - fade_out_offset) + fade_out_dur
 *           alpha -= adj * step
 *   if alpha < 0:  RETURN 0
 *   color_val = (blend_byte ∈ {0,2}) ? alpha : 255
 *   alpha_mix = (blend_byte ∈ {1,2}) ? alpha/255 : 1.0
 *   out_alpha_int = (int)color_val           ← __ftol truncates toward 0
 *   out_alpha_mix = alpha_mix
 *
 * The truncated `color_val` (out_alpha_int, expected 0..255) is the
 * gray byte for the diffuse encoding.  The 1.0 alpha_mix default
 * (blend_byte == 0) means shape 0/5's scale isn't dimmed by the
 * fade; the diffuse-gray channel carries the brightness instead. */
int scene1_overlay_compute_fade(const int32_t *slot,
                                int *out_alpha_int,
                                float *out_alpha_mix);

/* Shape 0/5 scale axes — engine asm 0x415916..0x41594f.
 *
 *   sx = ((1 - blend_mix) * scale_base * alpha_mix * scale_x * 0.003) / 0.5
 *   sy = (    blend_mix   * scale_base * alpha_mix * scale_x * 0.003) / 0.5
 *
 * sz == sx (engine calls scaling(sx, sy, sx)). */
void scene1_overlay_shape_05_scale(const int32_t *slot,
                                   float alpha_mix,
                                   float *out_sx, float *out_sy);

/* Shape 0/5 world matrix: T × S × pre_matrix (left-mul chain).
 * Reuses wf_pass_c_get_pre_matrix() — same DAT_0438cdf8 stand-in. */
void scene1_overlay_shape_05_compose_world(float out[16],
                                           const int32_t *slot,
                                           float alpha_mix);

/* Shape 0/5 (and most shapes) frame-UV selection — engine asm
 * 0x4159ca..0x415a22 (also at 0x415803..0x415852 in shape 1, etc).
 *
 *   if frame_count > 1:
 *       frames_per_row = (int)(256.0 / uv_size_x)       (__ftol trunc)
 *       u_origin = (rng_seed % frames_per_row) * uv_size_x + uv_origin_x
 *       v_origin = (rng_seed / frames_per_row) * uv_size_y + uv_origin_y
 *   else:
 *       (u_origin, v_origin) = (uv_origin_x, uv_origin_y)
 *
 * shape_entry: 8-dw shape table entry (NULL ok → returns zeros).
 * rng_seed: slot[+0x80] (OFF_RNG_SEED), read as signed int.  */
void scene1_overlay_shape_05_frame_uv(const int32_t *shape_entry,
                                      int rng_seed,
                                      float *out_uv_origin_x,
                                      float *out_uv_origin_y);

/* Shape 0/5 UV + diffuse emit into a 4-vert vbuf.  Mirrors engine
 * asm 0x415a25..0x415aff:
 *
 *   v0..v3 UV layout (after the slot_idx&1 horizontal flip):
 *     even slot_idx: v0=(u_right, v_top), v2=(u_left, v_top)
 *     odd  slot_idx: v0=(u_left,  v_top), v2=(u_right, v_top)
 *   (v1 mirrors v0's U / takes v_bottom; v3 mirrors v2's U / v_bottom.)
 *
 * Diffuse: 0xff_gg_gg_gg where gg = alpha_int & 0xff (engine's
 * 3× shl+or trick; see scene1_overlay_diffuse_gray).
 *
 * shape_entry supplies uv_size_x/y for the box span; pass NULL to
 * use zero (degenerate quad — useful for host tests). */
void scene1_overlay_shape_05_emit_quad(scene1_overlay_vertex vbuf[4],
                                       const int32_t *shape_entry,
                                       float uv_origin_x, float uv_origin_y,
                                       int slot_idx,
                                       int alpha_int);

/* Gray-diffuse encoding from a 0..255 int.  Verbatim engine trick at
 * 0x41586e..0x41587a / 0x415aa6..0x415ab5:
 *   return (((g | 0xffffff00) << 8 | g) << 8 | g) = 0xff_gg_gg_gg. */
uint32_t scene1_overlay_diffuse_gray(int alpha_int);

/* ---- Win32 dispatcher entry (O.3) ---------------------------------- */
#ifdef _WIN32
struct IDirect3DDevice8;

/* scene1_overlay_render — port of FUN_00414ee2 dispatcher shell.
 *
 * Walks `g_scene1_overlay_layer_count` outer iterations × 4096-slot
 * inner scan, gating on (slot.active != -1, slot.layer == layer,
 * slot.mode == mode, shape_table[slot.texture_type].tex_group ==
 * outer_idx, slot.age >= 0).  Per surviving slot: sticky SetTexture
 * via FUN_00415e90, alpha compute, per-shape draw dispatch.
 *
 * O.3 implements shape 0 and shape 5 (the simplest single-quad
 * T × S × pre_matrix path).  Other shapes (1/2/3/4/6/7/8/9/10) are
 * stubbed to skip — chips O.4..O.7 will fill them in.
 *
 * Caller must have already issued the mid-pass state setup
 * (SetVertexShader(0x142), LIGHTING=FALSE, MAGFILTER=5, MINFILTER=6,
 * LightEnable(0, FALSE), and TSS COLOROP per pass).  This function
 * issues only SetTransform(WORLD), SetTexture(0), and
 * DrawPrimitiveUP — same scope as the engine's body.
 *
 * No-op when dev is NULL or layer_count is 0. */
void scene1_overlay_render(struct IDirect3DDevice8 *dev,
                           int layer, int mode);
#endif

/* ---- Spawn API (FUN_00414345) -------------------------------------- */
/*
 * Walk the 4096 slots for the first ACTIVE == -1, claim it, copy
 * template_id's constants in, run per-shape RNG/owner-driven init, and
 * write age = (int)(local_4 * template[7]) (the age-stagger).  Then
 * dec local_4, inc claim count, and repeat until template[2] particles
 * have been committed or the table is exhausted.
 *
 * Engine signature (10 args):
 *   FUN_00414345(int    template_owner,
 *                float  pos_x, pos_y, pos_z,
 *                int    template_id,
 *                float  scale_base,
 *                int    override_dur,    // param_7; 0 → template[8]
 *                int    override_rot_y,  // param_8; only shape 6 reads
 *                int    shape_mode,      // param_9 → slot.SHAPE_MODE
 *                int    mode)            // param_10 → slot.MODE
 *
 * `template_owner` (param_1) is the engine's struct pointer used by the
 * type_shape < 2 / param_9 ∈ {2, 3, 5} branches that read fields at
 * owner+0x904/+0x908/+0x90c (param_9 ∈ {2, 3}) or owner+0x3fc/+0x400/
 * +0x404 (param_9 == 5).  Port keeps this as `const void *` and reads
 * the same byte offsets; engine treats it as an int.  Owner-driven
 * branches are inactive when template_owner is NULL (all-zero owner
 * blob → all zero fields → zero pos contributions).
 *
 * Returns void (engine signature) — callers don't check.
 *
 * Template / shape table contents must be populated by the caller (or
 * test) before calling.  All-zero templates produce no observable side
 * effect: type_shape == 0 (the < 2 branch), no RNG calls, and
 * spawn_count == 0 → inner loop terminates immediately after the first
 * (and only) particle's writes.  scale_base = 0; layer = 0;
 * blend_mode_byte = 0.
 */
void scene1_overlay_spawn(const void *template_owner,
                          float pos_x, float pos_y, float pos_z,
                          int template_id,
                          float scale_base,
                          int override_dur,
                          int override_rot_y,
                          int shape_mode,
                          int mode);

/* ---- Test helpers -------------------------------------------------- */
/*
 * Inline accessors mirror the records_c_spawn convention (see
 * scene1_records_c_spawn.c for the pattern).  Useful in tests to set
 * up templates / inspect spawned slots without exposing the raw stride
 * arithmetic.
 */
static inline int32_t scene1_overlay_slot_get_i(int slot, int off)
{
    return g_scene1_overlay_slots[slot * SCENE1_OVERLAY_SLOT_STRIDE + off];
}
static inline void scene1_overlay_slot_set_i(int slot, int off, int32_t v)
{
    g_scene1_overlay_slots[slot * SCENE1_OVERLAY_SLOT_STRIDE + off] = v;
}
static inline int32_t scene1_overlay_template_get_i(int tpl, int off)
{
    return g_scene1_overlay_templates[tpl * SCENE1_OVERLAY_TEMPLATE_STRIDE + off];
}
static inline void scene1_overlay_template_set_i(int tpl, int off, int32_t v)
{
    g_scene1_overlay_templates[tpl * SCENE1_OVERLAY_TEMPLATE_STRIDE + off] = v;
}

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_OVERLAY_H */
