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
