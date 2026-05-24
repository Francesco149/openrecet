/*
 * scene1_overlay.c — port of FUN_00414345 (1057 B) + storage for the
 * 2D-overlay particle dispatcher's slot / template / per-shape tables.
 *
 * See scene1_overlay.h for the chip writeup, slot/template layout, and
 * the age-stagger __ftol resolution.
 *
 * Engine globals consumed (all wired through rng.h / libc):
 *   thunk_FUN_005041f6 / 0x471084 (jmp to it) → rng_next15()
 *   FUN_00471089                              → rng_next_unit()
 *   FUN_00503a44 / 0x503994                   → sinf / cosf
 *   call 0x503954                             → __ftol (int)(float-on-FPU-TOS)
 *
 * Constants from .rdata (confirmed via objdump):
 *   0x519398 = 6.2831855f  (2π)
 *   0x5193a0 = 0.1f
 *   0x51935c = 0.5f
 *   0x519438 = 3.0f
 *   0x519924 = 1.2f
 */

#include "scene1_overlay.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "rng.h"

#ifndef TWO_PI_F
#define TWO_PI_F 6.2831855f
#endif

/* ---- Storage ------------------------------------------------------- */

int32_t g_scene1_overlay_slots[SCENE1_OVERLAY_SLOT_COUNT *
                               SCENE1_OVERLAY_SLOT_STRIDE];
int32_t g_scene1_overlay_templates[SCENE1_OVERLAY_TEMPLATE_COUNT *
                                   SCENE1_OVERLAY_TEMPLATE_STRIDE];
int32_t g_scene1_overlay_shapes[SCENE1_OVERLAY_SHAPE_COUNT *
                                SCENE1_OVERLAY_SHAPE_STRIDE];

void scene1_overlay_reset(void)
{
    /* The engine's pre-spawn state has every slot's ACTIVE field = -1
     * and every other dw = 0.  Easiest: zero the table then write the
     * sentinel into each slot's ACTIVE field. */
    memset(g_scene1_overlay_slots, 0, sizeof g_scene1_overlay_slots);
    for (int i = 0; i < SCENE1_OVERLAY_SLOT_COUNT; i++) {
        g_scene1_overlay_slots[i * SCENE1_OVERLAY_SLOT_STRIDE +
                               SCENE1_OVERLAY_OFF_ACTIVE] = -1;
    }
}

void scene1_overlay_templates_reset(void)
{
    memset(g_scene1_overlay_templates, 0, sizeof g_scene1_overlay_templates);
}

void scene1_overlay_shapes_reset(void)
{
    memset(g_scene1_overlay_shapes, 0, sizeof g_scene1_overlay_shapes);
}

void scene1_overlay_init(void)
{
    scene1_overlay_reset();
    scene1_overlay_templates_reset();
    scene1_overlay_shapes_reset();
}

/* ---- Slot / template accessors ------------------------------------- */

static inline int32_t slot_get_i(int s, int off)
{
    return g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off];
}
static inline void slot_set_i(int s, int off, int32_t v)
{
    g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off] = v;
}
static inline void slot_set_f(int s, int off, float v)
{
    int32_t bits;
    memcpy(&bits, &v, sizeof bits);
    g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off] = bits;
}
static inline float slot_get_f(int s, int off)
{
    int32_t bits = g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static inline int slot_is_free(int s)
{
    return slot_get_i(s, SCENE1_OVERLAY_OFF_ACTIVE) == -1;
}
/* Write a single byte to slot[off]'s low byte (used for blend_mode_byte). */
static inline void slot_set_b0(int s, int off, uint8_t v)
{
    int32_t cur = g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off];
    cur = (cur & ~0xff) | v;
    g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE + off] = cur;
}

static inline int32_t tpl_get_i(int t, int off)
{
    return g_scene1_overlay_templates[t * SCENE1_OVERLAY_TEMPLATE_STRIDE + off];
}
static inline float tpl_get_f(int t, int off)
{
    int32_t bits = g_scene1_overlay_templates[t * SCENE1_OVERLAY_TEMPLATE_STRIDE + off];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Template byte 0x44 (layer; signed int8) and 0x45 (blend_mode_byte;
 * unsigned int8) live in the first 2 bytes of dw 17. */
static inline int8_t tpl_get_layer_byte(int t)
{
    int32_t pair = tpl_get_i(t, SCENE1_OVERLAY_TPL_OFF_LAYER_PAIR);
    return (int8_t)(pair & 0xff);
}
static inline uint8_t tpl_get_blend_mode_byte(int t)
{
    int32_t pair = tpl_get_i(t, SCENE1_OVERLAY_TPL_OFF_LAYER_PAIR);
    return (uint8_t)((pair >> 8) & 0xff);
}

/* Read a float at byte offset N inside an owner blob.  Engine treats
 * the owner as a raw int pointer + reads via fld DWORD PTR [reg+N]. */
static inline float owner_get_f(const void *owner, int byte_off)
{
    if (!owner) return 0.0f;
    float f;
    memcpy(&f, (const char *)owner + byte_off, sizeof f);
    return f;
}

/* ---- Per-shape RNG init helpers ------------------------------------ */

static void init_shape_3_or_7_rot_xyz(int s)
{
    /* 3 RNG draws, each → rot[axis] = u * 2π (engine treats rot fields
     * as float). */
    slot_set_f(s, SCENE1_OVERLAY_OFF_ROT_X, rng_next_unit() * TWO_PI_F);
    slot_set_f(s, SCENE1_OVERLAY_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(s, SCENE1_OVERLAY_OFF_ROT_Z, rng_next_unit() * TWO_PI_F);
}

static void init_shape_8_9_10(int s, int tpl_id)
{
    /* slot[15] (rot.y) = rng_next_unit() * 2π
     * slot[12] (bend.y) = template[4] * 0.1f */
    slot_set_f(s, SCENE1_OVERLAY_OFF_ROT_Y, rng_next_unit() * TWO_PI_F);
    slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
               tpl_get_f(tpl_id, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4) * 0.1f);
}

static void init_shape_5(int s, int tpl_id,
                         float px, float py, float pz)
{
    /* fVar1 = template[4]; u = rng_unit(); angle = u * 2π;
     * slot[11..13] = 0;
     * sin/cos paired at angle (engine calls each twice; results
     * identical for the same angle):
     *   slot[2] = sin(angle) * fVar1 + px
     *   slot[3] = py (unchanged)
     *   slot[4] = cos(angle) * fVar1 + pz
     *   slot[5] = sin(angle) * fVar1
     *   slot[6] = 0
     *   slot[7] = cos(angle) * fVar1 */
    float fVar1 = tpl_get_f(tpl_id, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4);
    float u     = rng_next_unit();
    float angle = u * TWO_PI_F;

    slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_X, 0.0f);
    slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y, 0.0f);
    slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Z, 0.0f);

    float sa = sinf(angle);
    float ca = cosf(angle);
    slot_set_f(s, SCENE1_OVERLAY_OFF_POS_X, sa * fVar1 + px);
    slot_set_f(s, SCENE1_OVERLAY_OFF_POS_Y, py);
    slot_set_f(s, SCENE1_OVERLAY_OFF_POS_Z, ca * fVar1 + pz);
    slot_set_f(s, SCENE1_OVERLAY_OFF_VEL_X, sa * fVar1);
    slot_set_f(s, SCENE1_OVERLAY_OFF_VEL_Y, 0.0f);
    slot_set_f(s, SCENE1_OVERLAY_OFF_VEL_Z, ca * fVar1);
}

static void init_shape_lt2(int s, int tpl_id,
                           const void *owner, int shape_mode,
                           float px, float py, float pz)
{
    /* type_shape ∈ {0, 1} — three branches:
     *   shape_mode ∈ {2, 3}: read owner+0x904/0x908/0x90c floats
     *   shape_mode == 5    : read owner+0x3fc/0x400/0x404 floats
     *   else (default)     : 3 RNG draws + paired sin/cos for random
     *                        jitter (the "real" type_shape<2 init).
     *
     * Common tail computes fVar1 then writes:
     *   slot[13] (bend.z) = fVar1 * scale_base
     *   slot[2..4] (pos)  = slot[11..13] * 3.0 + (px, py, pz)
     */
    float scale_base = slot_get_f(s, SCENE1_OVERLAY_OFF_SCALE_BASE);
    float fVar1;

    if (shape_mode == 2 || shape_mode == 3) {
        slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_X,
                   owner_get_f(owner, 0x904) * scale_base);
        slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                   owner_get_f(owner, 0x908) * scale_base);
        fVar1 = owner_get_f(owner, 0x90c);
    } else if (shape_mode == 5) {
        slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_X,
                   owner_get_f(owner, 0x3fc) * scale_base);
        slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                   owner_get_f(owner, 0x400) * scale_base);
        fVar1 = owner_get_f(owner, 0x404);
    } else {
        /* Random pos jitter — 3 RNG draws, paired sin/cos at angles 1
         * and 3, single cos at angle 1.  See asm 0x414609..0x4146d5. */
        float u1     = rng_next_unit();
        float angle1 = u1 * TWO_PI_F;
        float u2     = rng_next_unit();
        float t4     = tpl_get_f(tpl_id, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4);
        float t12    = tpl_get_f(tpl_id, SCENE1_OVERLAY_TPL_OFF_SCALE_Y_RATIO);
        float t13    = tpl_get_f(tpl_id, SCENE1_OVERLAY_TPL_OFF_BLEND_OFFSET);
        float fVar2  = (u2 + 1.2f) * t4 * 0.1f;
        float s1     = sinf(angle1);
        float fVar1_a = ((1.0f - t12) * s1 * fVar2) / 0.5f;
        float c1     = cosf(angle1);
        float u3     = rng_next_unit();
        float angle3 = u3 * TWO_PI_F;
        float s3     = sinf(angle3);

        slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_X,
                   s3 * fVar1_a * scale_base);
        slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Y,
                   (t12 * (c1 + t13) * fVar2 / 0.5f) * scale_base);

        float c3 = cosf(angle3);
        fVar1 = c3 * fVar1_a;
    }

    /* Common tail. */
    slot_set_f(s, SCENE1_OVERLAY_OFF_BEND_Z, fVar1 * scale_base);
    slot_set_f(s, SCENE1_OVERLAY_OFF_POS_X,
               slot_get_f(s, SCENE1_OVERLAY_OFF_BEND_X) * 3.0f + px);
    slot_set_f(s, SCENE1_OVERLAY_OFF_POS_Y,
               slot_get_f(s, SCENE1_OVERLAY_OFF_BEND_Y) * 3.0f + py);
    slot_set_f(s, SCENE1_OVERLAY_OFF_POS_Z,
               slot_get_f(s, SCENE1_OVERLAY_OFF_BEND_Z) * 3.0f + pz);
}

/* ---- Spawn API ----------------------------------------------------- */

void scene1_overlay_spawn(const void *template_owner,
                          float pos_x, float pos_y, float pos_z,
                          int template_id,
                          float scale_base,
                          int override_dur,
                          int override_rot_y,
                          int shape_mode,
                          int mode)
{
    /* Engine bounds-checks via the do-while loop's `piVar10 ==
     * &DAT_0072a890` exit (i.e. one past the last slot).  No bounds
     * check on template_id — the engine indexes blindly into
     * DAT_00733884 with stride 0x2b.  Mirror that, but clamp here so
     * we don't UB on a bogus caller. */
    if (template_id < 0 || template_id >= SCENE1_OVERLAY_TEMPLATE_COUNT) {
        return;
    }

    int local_c = 0;   /* slots claimed this call */
    int local_4 = 0;   /* age-stagger counter; DEC'd per claim */

    /* Read template fields used by the preamble + dispatch. */
    int   t_type_shape    = tpl_get_i(template_id,
                                      SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE);
    int   t_spawn_count   = tpl_get_i(template_id,
                                      SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT);
    int   t_fade_def      = tpl_get_i(template_id,
                                      SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DEFAULT);
    int   t_age_birth_mod = tpl_get_i(template_id,
                                      SCENE1_OVERLAY_TPL_OFF_AGE_BIRTH_MOD);
    float t_scale_base_mul = tpl_get_f(template_id,
                                       SCENE1_OVERLAY_TPL_OFF_SCALE_BASE_MUL);
    float t_age_stagger    = tpl_get_f(template_id,
                                       SCENE1_OVERLAY_TPL_OFF_AGE_STAGGER);

    for (int slot = 0; slot < SCENE1_OVERLAY_SLOT_COUNT; slot++) {
        if (!slot_is_free(slot)) continue;

        /* --- Preamble (engine asm 0x414367..0x41446a) --- */

        /* Position, pos copy. */
        slot_set_f(slot, SCENE1_OVERLAY_OFF_POS_X, pos_x);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_POS_Y, pos_y);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_POS_Z, pos_z);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_POS_X_COPY, pos_x);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_POS_Y_COPY, pos_y);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_POS_Z_COPY, pos_z);

        /* Owner duplicated copies. */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_OWNER_A, (int32_t)(intptr_t)template_owner);
        slot_set_i(slot, SCENE1_OVERLAY_OFF_OWNER_B, (int32_t)(intptr_t)template_owner);

        /* Zero bend / rot triplets. */
        slot_set_f(slot, SCENE1_OVERLAY_OFF_BEND_X, 0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_BEND_Y, 0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_BEND_Z, 0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_ROT_X,  0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_ROT_Y,  0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_ROT_Z,  0.0f);

        /* Initial age, template-derived dispatch fields. */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_AGE, 0);
        slot_set_i(slot, SCENE1_OVERLAY_OFF_ACTIVE, template_id);   /* claim */
        slot_set_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE, scale_base); /* overwritten below */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_SHAPE_MODE, shape_mode);
        slot_set_i(slot, SCENE1_OVERLAY_OFF_UNK_7C, 0);
        slot_set_i(slot, SCENE1_OVERLAY_OFF_RNG_SEED, 0);
        slot_set_i(slot, SCENE1_OVERLAY_OFF_MODE, mode);

        /* Template field copies (mix of int mov + float fld/fstp). */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_TEXTURE_TYPE,
                   tpl_get_i(template_id, SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE));
        slot_set_i(slot, SCENE1_OVERLAY_OFF_TYPE_SHAPE, t_type_shape);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_TEMPLATE5_COPY,
                   tpl_get_f(template_id, SCENE1_OVERLAY_TPL_OFF_TEMPLATE5));
        slot_set_f(slot, SCENE1_OVERLAY_OFF_UNK_48,
                   tpl_get_f(template_id, SCENE1_OVERLAY_TPL_OFF_TEMPLATE6));

        /* fade_out_offset: param_7 if > 0, else template[8]. */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET,
                   (override_dur > 0) ? override_dur : t_fade_def);

        /* scale_x, template[11] copy, blend_mix, scale_y_ratio (floats). */
        slot_set_f(slot, SCENE1_OVERLAY_OFF_SCALE_X,
                   tpl_get_f(template_id, SCENE1_OVERLAY_TPL_OFF_SCALE_X));
        slot_set_f(slot, SCENE1_OVERLAY_OFF_TEMPLATE11_COPY,
                   tpl_get_f(template_id, SCENE1_OVERLAY_TPL_OFF_TEMPLATE11));

        /* scale_base FINAL value (overwrites earlier write). */
        slot_set_f(slot, SCENE1_OVERLAY_OFF_SCALE_BASE,
                   scale_base * t_scale_base_mul);

        slot_set_f(slot, SCENE1_OVERLAY_OFF_BLEND_MIX,
                   tpl_get_f(template_id, SCENE1_OVERLAY_TPL_OFF_BLEND_MIX));
        slot_set_i(slot, SCENE1_OVERLAY_OFF_FADE_IN_DUR,
                   tpl_get_i(template_id, SCENE1_OVERLAY_TPL_OFF_FADE_IN_DUR));
        slot_set_i(slot, SCENE1_OVERLAY_OFF_FADE_OUT_DUR,
                   tpl_get_i(template_id, SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DUR));
        slot_set_f(slot, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO,
                   tpl_get_f(template_id, SCENE1_OVERLAY_TPL_OFF_SCALE_Y_RATIO));

        /* layer (sign-extended byte 0x44) and blend_mode_byte (byte
         * 0x45 unsigned, written into the LSB of slot[34]). */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_LAYER,
                   (int32_t)tpl_get_layer_byte(template_id));
        slot_set_b0(slot, SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE,
                    tpl_get_blend_mode_byte(template_id));

        /* Zero vel triplet (overwritten by shape 5 / type_shape<2 init). */
        slot_set_f(slot, SCENE1_OVERLAY_OFF_VEL_X, 0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_VEL_Y, 0.0f);
        slot_set_f(slot, SCENE1_OVERLAY_OFF_VEL_Z, 0.0f);

        /* age_birth: 0 by default; rng_next15() % template[9] when > 0. */
        slot_set_i(slot, SCENE1_OVERLAY_OFF_AGE_BIRTH, 0);
        if (t_age_birth_mod > 0) {
            uint32_t r = (uint32_t)rng_next15();
            slot_set_i(slot, SCENE1_OVERLAY_OFF_AGE_BIRTH,
                       (int32_t)(r % (uint32_t)t_age_birth_mod));
        }

        /* --- Per-shape RNG / owner-driven init --- */

        if (t_type_shape == 3) {
            init_shape_3_or_7_rot_xyz(slot);
        }
        if (t_type_shape == 6) {
            /* slot[15] (rot.y) = param_8.  Engine writes via fild then
             * fstp — but the input is an int, so the float bits encode
             * (float)override_rot_y. */
            slot_set_f(slot, SCENE1_OVERLAY_OFF_ROT_Y, (float)override_rot_y);
        }
        if (t_type_shape == 7) {
            init_shape_3_or_7_rot_xyz(slot);
        }
        /* shape ∈ {8,9,10}: re-reads slot[1] (type_shape, already set above). */
        if (t_type_shape == 8 || t_type_shape == 9 || t_type_shape == 10) {
            init_shape_8_9_10(slot, template_id);
        }

        if (t_type_shape == 5) {
            init_shape_5(slot, template_id, pos_x, pos_y, pos_z);
        } else if (t_type_shape < 2) {
            init_shape_lt2(slot, template_id, template_owner, shape_mode,
                           pos_x, pos_y, pos_z);
        }

        /* --- Age stagger (PHC #17 resolved 2026-05-24) --- */
        /* engine asm 0x414728..0x414737:
         *   fild [local_4] ; fmul [template[7]] ; call __ftol → eax
         *   slot[+0x74] = eax */
        float age_f = (float)local_4 * t_age_stagger;
        slot_set_i(slot, SCENE1_OVERLAY_OFF_AGE, (int32_t)age_f);

        local_c++;
        local_4--;

        if (t_spawn_count <= local_c) return;
    }
    /* Outer loop exhausted (4096 slots scanned, table full or
     * fragmented) → silent return matching engine fallthrough. */
}
