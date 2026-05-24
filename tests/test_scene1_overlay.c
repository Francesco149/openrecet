/*
 * test_scene1_overlay.c — unit tests for O.2: FUN_00414345 spawn API +
 * the slot/template/per-shape storage in src/scene1_overlay.{c,h}.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "rng.h"
#include "scene1_overlay.h"
#include "scene1_wide_followup.h"   /* wf_pass_c_set_pre_matrix — for O.3 world matrix test */

#ifndef TWO_PI_F
#define TWO_PI_F 6.2831855f
#endif

/* ─── helpers ─────────────────────────────────────────────────────── */

static void reset_world(void)
{
    scene1_overlay_init();
    rng_seed(1);
}

static float bits_to_f(int32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}
static int32_t f_to_bits(float f)
{
    int32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return bits;
}

/* Convenience setters for templates (uniform "everything is a float" or
 * "everything is an int" — the test caller knows which fields are which). */
static void tpl_set_i(int t, int off, int32_t v)
{
    scene1_overlay_template_set_i(t, off, v);
}
static void tpl_set_f(int t, int off, float v)
{
    scene1_overlay_template_set_i(t, off, f_to_bits(v));
}
/* The layer/blend_mode_byte pair lives packed in dw 17. */
static void tpl_set_layer_pair(int t, int8_t layer, uint8_t blend_mode)
{
    int32_t pair = ((uint32_t)(uint8_t)layer) | ((uint32_t)blend_mode << 8);
    scene1_overlay_template_set_i(t, SCENE1_OVERLAY_TPL_OFF_LAYER_PAIR, pair);
}

static int32_t slot_i(int s, int off) { return scene1_overlay_slot_get_i(s, off); }
static float   slot_f(int s, int off) { return bits_to_f(scene1_overlay_slot_get_i(s, off)); }

static int count_live(void)
{
    int n = 0;
    for (int i = 0; i < SCENE1_OVERLAY_SLOT_COUNT; i++) {
        if (slot_i(i, SCENE1_OVERLAY_OFF_ACTIVE) != -1) n++;
    }
    return n;
}

/* ─── storage / reset ─────────────────────────────────────────────── */

int test_scene1_overlay_init_makes_all_slots_free(void)
{
    scene1_overlay_init();
    for (int i = 0; i < SCENE1_OVERLAY_SLOT_COUNT; i++) {
        T_ASSERT_EQ_I(slot_i(i, SCENE1_OVERLAY_OFF_ACTIVE), -1);
    }
    return 0;
}

int test_scene1_overlay_init_zeros_other_fields(void)
{
    /* Stamp slot 5 dw 30 with garbage, then init — should clear it. */
    scene1_overlay_slot_set_i(5, SCENE1_OVERLAY_OFF_SCALE_BASE, 0x12345678);
    scene1_overlay_init();
    T_ASSERT_EQ_I(slot_i(5, SCENE1_OVERLAY_OFF_SCALE_BASE), 0);
    return 0;
}

int test_scene1_overlay_init_zeros_templates_and_shapes(void)
{
    g_scene1_overlay_templates[0] = 99;
    g_scene1_overlay_shapes[0]    = 99;
    scene1_overlay_init();
    T_ASSERT_EQ_I(g_scene1_overlay_templates[0], 0);
    T_ASSERT_EQ_I(g_scene1_overlay_shapes[0], 0);
    return 0;
}

/* ─── spawn: preamble + claim ─────────────────────────────────────── */

int test_overlay_spawn_writes_first_free_slot(void)
{
    reset_world();
    /* template 1: spawn_count=1, type_shape=2, scale_base_mul=2.0,
     * layer=4, blend_mode_byte=7. */
    tpl_set_i(1, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(1, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_f(1, SCENE1_OVERLAY_TPL_OFF_SCALE_BASE_MUL, 2.0f);
    tpl_set_layer_pair(1, 4, 7);

    scene1_overlay_spawn(NULL, 1.5f, 2.5f, 3.5f,
                         /*template_id=*/1,
                         /*scale_base=*/3.0f,
                         /*override_dur=*/-5,
                         /*override_rot_y=*/0,
                         /*shape_mode=*/9,
                         /*mode=*/0x42);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_ACTIVE), 1);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_TYPE_SHAPE), 2);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_SHAPE_MODE), 9);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_MODE), 0x42);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_X) - 1.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_Y) - 2.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_Z) - 3.5f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_X_COPY) - 1.5f) < 1e-6f);
    /* scale_base = 3.0 * 2.0 = 6.0 (final write overrides initial 3.0). */
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_SCALE_BASE) - 6.0f) < 1e-6f);
    /* Layer (sign-extended) and blend_mode_byte (low byte of slot dw 34). */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_LAYER), 4);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE) & 0xff, 7);
    return 0;
}

int test_overlay_spawn_layer_is_sign_extended(void)
{
    /* template byte 0x44 holds a signed int8 — -1 should become slot.LAYER = -1. */
    reset_world();
    tpl_set_i(2, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(2, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_layer_pair(2, (int8_t)-1, 0);
    scene1_overlay_spawn(NULL, 0,0,0, 2, 0.0f, 0, 0, 0, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_LAYER), -1);
    return 0;
}

int test_overlay_spawn_skips_alive_slots(void)
{
    reset_world();
    scene1_overlay_slot_set_i(0, SCENE1_OVERLAY_OFF_ACTIVE, 99);
    scene1_overlay_slot_set_i(1, SCENE1_OVERLAY_OFF_ACTIVE, 99);
    tpl_set_i(3, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(3, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    scene1_overlay_spawn(NULL, 0,0,0, 3, 0.0f, 0,0,0,0);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_OVERLAY_OFF_ACTIVE), 3);
    /* Slots 0/1 untouched. */
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_ACTIVE), 99);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_OVERLAY_OFF_ACTIVE), 99);
    return 0;
}

int test_overlay_spawn_table_full_is_noop(void)
{
    reset_world();
    for (int i = 0; i < SCENE1_OVERLAY_SLOT_COUNT; i++) {
        scene1_overlay_slot_set_i(i, SCENE1_OVERLAY_OFF_ACTIVE, 1);
    }
    tpl_set_i(4, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(4, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    scene1_overlay_spawn(NULL, 0,0,0, 4, 1.0f, 0,0,0,0);
    /* All slots still ACTIVE=1; none claimed by template 4. */
    for (int i = 0; i < SCENE1_OVERLAY_SLOT_COUNT; i++) {
        T_ASSERT_EQ_I(slot_i(i, SCENE1_OVERLAY_OFF_ACTIVE), 1);
    }
    return 0;
}

int test_overlay_spawn_count_caps_loop(void)
{
    reset_world();
    tpl_set_i(5, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 3);
    tpl_set_i(5, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    scene1_overlay_spawn(NULL, 0,0,0, 5, 0.0f, 0,0,0,0);
    T_ASSERT_EQ_I(count_live(), 3);
    for (int i = 0; i < 3; i++) {
        T_ASSERT_EQ_I(slot_i(i, SCENE1_OVERLAY_OFF_ACTIVE), 5);
    }
    T_ASSERT_EQ_I(slot_i(3, SCENE1_OVERLAY_OFF_ACTIVE), -1);
    return 0;
}

int test_overlay_spawn_zero_count_writes_one_slot(void)
{
    /* The engine writes the first slot BEFORE testing the cap (so
     * spawn_count==0 still produces 1 particle).  This is engine-
     * faithful per the do-while-loop shape.  See scene1_overlay.c
     * comment near the loop body. */
    reset_world();
    tpl_set_i(6, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 0);
    tpl_set_i(6, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    scene1_overlay_spawn(NULL, 0,0,0, 6, 0.0f, 0,0,0,0);
    T_ASSERT_EQ_I(count_live(), 1);
    return 0;
}

int test_overlay_spawn_invalid_template_id_noop(void)
{
    reset_world();
    scene1_overlay_spawn(NULL, 0,0,0,
                         /*template_id=*/SCENE1_OVERLAY_TEMPLATE_COUNT, /* OOB */
                         0.0f, 0,0,0,0);
    T_ASSERT_EQ_I(count_live(), 0);
    return 0;
}

/* ─── fade_out_offset (param_7) ───────────────────────────────────── */

int test_overlay_spawn_fade_out_uses_param7_when_positive(void)
{
    reset_world();
    tpl_set_i(7, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(7, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_i(7, SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DEFAULT, 99);
    scene1_overlay_spawn(NULL, 0,0,0, 7, 0.0f, /*override_dur=*/42, 0, 0, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET), 42);
    return 0;
}

int test_overlay_spawn_fade_out_uses_template_when_param7_zero(void)
{
    reset_world();
    tpl_set_i(8, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(8, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_i(8, SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DEFAULT, 99);
    scene1_overlay_spawn(NULL, 0,0,0, 8, 0.0f, /*override_dur=*/0, 0, 0, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET), 99);
    return 0;
}

int test_overlay_spawn_fade_out_uses_template_when_param7_negative(void)
{
    reset_world();
    tpl_set_i(9, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(9, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_i(9, SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DEFAULT, 77);
    scene1_overlay_spawn(NULL, 0,0,0, 9, 0.0f, /*override_dur=*/-3, 0, 0, 0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET), 77);
    return 0;
}

/* ─── age_birth (template[9] mod) ──────────────────────────────────── */

int test_overlay_spawn_age_birth_zero_when_template9_zero(void)
{
    reset_world();
    tpl_set_i(10, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 4);
    tpl_set_i(10, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_i(10, SCENE1_OVERLAY_TPL_OFF_AGE_BIRTH_MOD, 0);
    scene1_overlay_spawn(NULL, 0,0,0, 10, 0.0f, 0,0,0,0);
    for (int i = 0; i < 4; i++) {
        T_ASSERT_EQ_I(slot_i(i, SCENE1_OVERLAY_OFF_AGE_BIRTH), 0);
    }
    return 0;
}

int test_overlay_spawn_age_birth_in_range_when_template9_positive(void)
{
    reset_world();
    tpl_set_i(11, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 8);
    tpl_set_i(11, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_i(11, SCENE1_OVERLAY_TPL_OFF_AGE_BIRTH_MOD, 5);
    scene1_overlay_spawn(NULL, 0,0,0, 11, 0.0f, 0,0,0,0);
    for (int i = 0; i < 8; i++) {
        int v = slot_i(i, SCENE1_OVERLAY_OFF_AGE_BIRTH);
        T_ASSERT(v >= 0 && v < 5);
    }
    return 0;
}

/* ─── age stagger (PHC #17) ───────────────────────────────────────── */

int test_overlay_spawn_age_stagger_first_is_zero(void)
{
    /* local_4 starts at 0 → first particle's age = (int)(0 * stagger) = 0. */
    reset_world();
    tpl_set_i(12, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 4);
    tpl_set_i(12, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_f(12, SCENE1_OVERLAY_TPL_OFF_AGE_STAGGER, 7.0f);
    scene1_overlay_spawn(NULL, 0,0,0, 12, 0.0f, 0,0,0,0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_AGE), 0);
    return 0;
}

int test_overlay_spawn_age_stagger_subsequent_are_negative(void)
{
    /* age[i] = (int)(-i * stagger) for i = 1..spawn_count-1.
     * With stagger=7.0: -7, -14, -21. */
    reset_world();
    tpl_set_i(13, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 4);
    tpl_set_i(13, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_f(13, SCENE1_OVERLAY_TPL_OFF_AGE_STAGGER, 7.0f);
    scene1_overlay_spawn(NULL, 0,0,0, 13, 0.0f, 0,0,0,0);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_AGE),  0);
    T_ASSERT_EQ_I(slot_i(1, SCENE1_OVERLAY_OFF_AGE), -7);
    T_ASSERT_EQ_I(slot_i(2, SCENE1_OVERLAY_OFF_AGE), -14);
    T_ASSERT_EQ_I(slot_i(3, SCENE1_OVERLAY_OFF_AGE), -21);
    return 0;
}

int test_overlay_spawn_age_stagger_zero_keeps_all_at_zero(void)
{
    /* No stagger configured → every particle has age 0 (immediately
     * visible if the renderer's age >= 0 gate accepts). */
    reset_world();
    tpl_set_i(14, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 3);
    tpl_set_i(14, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);
    tpl_set_f(14, SCENE1_OVERLAY_TPL_OFF_AGE_STAGGER, 0.0f);
    scene1_overlay_spawn(NULL, 0,0,0, 14, 0.0f, 0,0,0,0);
    for (int i = 0; i < 3; i++) {
        T_ASSERT_EQ_I(slot_i(i, SCENE1_OVERLAY_OFF_AGE), 0);
    }
    return 0;
}

/* ─── shape-6 rot.y override ──────────────────────────────────────── */

int test_overlay_spawn_shape_6_uses_param8_for_rot_y(void)
{
    reset_world();
    tpl_set_i(15, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(15, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 6);
    /* override_rot_y=42 — stored as (float)42 in slot[ROT_Y]. */
    scene1_overlay_spawn(NULL, 0,0,0, 15, 0.0f, 0, /*override_rot_y=*/42, 0,0);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_ROT_Y) - 42.0f) < 1e-6f);
    /* rot.x and rot.z stay zero — shape 6 only writes rot.y. */
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_ROT_X) - 0.0f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_ROT_Z) - 0.0f) < 1e-6f);
    return 0;
}

/* ─── shape-3 / shape-7 rot.{x,y,z} RNG init ──────────────────────── */

int test_overlay_spawn_shape_3_rot_xyz_in_range(void)
{
    reset_world();
    tpl_set_i(16, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(16, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 3);
    scene1_overlay_spawn(NULL, 0,0,0, 16, 0.0f, 0,0,0,0);
    float rx = slot_f(0, SCENE1_OVERLAY_OFF_ROT_X);
    float ry = slot_f(0, SCENE1_OVERLAY_OFF_ROT_Y);
    float rz = slot_f(0, SCENE1_OVERLAY_OFF_ROT_Z);
    /* Each = u * 2π where u ∈ [0, 1) → result ∈ [0, 2π). */
    T_ASSERT(rx >= 0.0f && rx < TWO_PI_F);
    T_ASSERT(ry >= 0.0f && ry < TWO_PI_F);
    T_ASSERT(rz >= 0.0f && rz < TWO_PI_F);
    return 0;
}

int test_overlay_spawn_shape_7_rot_xyz_in_range(void)
{
    reset_world();
    tpl_set_i(17, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(17, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 7);
    scene1_overlay_spawn(NULL, 0,0,0, 17, 0.0f, 0,0,0,0);
    float rx = slot_f(0, SCENE1_OVERLAY_OFF_ROT_X);
    float ry = slot_f(0, SCENE1_OVERLAY_OFF_ROT_Y);
    float rz = slot_f(0, SCENE1_OVERLAY_OFF_ROT_Z);
    T_ASSERT(rx >= 0.0f && rx < TWO_PI_F);
    T_ASSERT(ry >= 0.0f && ry < TWO_PI_F);
    T_ASSERT(rz >= 0.0f && rz < TWO_PI_F);
    return 0;
}

/* ─── shape-{8,9,10} rot.y + bend.y init ──────────────────────────── */

int test_overlay_spawn_shape_8_rot_y_and_bend_y(void)
{
    reset_world();
    tpl_set_i(18, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(18, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 8);
    tpl_set_f(18, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4, 5.0f);
    scene1_overlay_spawn(NULL, 0,0,0, 18, 0.0f, 0,0,0,0);
    float ry = slot_f(0, SCENE1_OVERLAY_OFF_ROT_Y);
    float by = slot_f(0, SCENE1_OVERLAY_OFF_BEND_Y);
    T_ASSERT(ry >= 0.0f && ry < TWO_PI_F);
    T_ASSERT(fabsf(by - 0.5f) < 1e-5f);   /* 5.0 * 0.1 */
    return 0;
}

int test_overlay_spawn_shape_9_and_10_also_init_bend_y(void)
{
    reset_world();
    for (int shape = 9; shape <= 10; shape++) {
        int tpl = 19 + shape;
        scene1_overlay_init();   /* reset slots between shapes */
        tpl_set_i(tpl, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
        tpl_set_i(tpl, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, shape);
        tpl_set_f(tpl, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4, 3.0f);
        scene1_overlay_spawn(NULL, 0,0,0, tpl, 0.0f, 0,0,0,0);
        float by = slot_f(0, SCENE1_OVERLAY_OFF_BEND_Y);
        T_ASSERT(fabsf(by - 0.3f) < 1e-5f);
    }
    return 0;
}

/* ─── shape-5 sin/cos init around (pos, fVar1) ────────────────────── */

int test_overlay_spawn_shape_5_pos_and_vel_match_angle(void)
{
    /* For shape 5:
     *   slot[2] = sin(a) * t4 + px
     *   slot[3] = py
     *   slot[4] = cos(a) * t4 + pz
     *   slot[5] = sin(a) * t4
     *   slot[7] = cos(a) * t4
     * → slot[2] - px == slot[5], slot[4] - pz == slot[7]. */
    reset_world();
    tpl_set_i(30, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(30, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 5);
    tpl_set_f(30, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4, 10.0f);

    float px = 100.0f, py = 200.0f, pz = 300.0f;
    scene1_overlay_spawn(NULL, px, py, pz, 30, 0.0f, 0,0,0,0);

    float pos_x = slot_f(0, SCENE1_OVERLAY_OFF_POS_X);
    float pos_y = slot_f(0, SCENE1_OVERLAY_OFF_POS_Y);
    float pos_z = slot_f(0, SCENE1_OVERLAY_OFF_POS_Z);
    float vel_x = slot_f(0, SCENE1_OVERLAY_OFF_VEL_X);
    float vel_y = slot_f(0, SCENE1_OVERLAY_OFF_VEL_Y);
    float vel_z = slot_f(0, SCENE1_OVERLAY_OFF_VEL_Z);

    T_ASSERT(fabsf(pos_y - py)         < 1e-4f);   /* unchanged */
    T_ASSERT(fabsf((pos_x - px) - vel_x) < 1e-4f);
    T_ASSERT(fabsf((pos_z - pz) - vel_z) < 1e-4f);
    T_ASSERT(fabsf(vel_y - 0.0f)       < 1e-6f);
    /* Magnitude: sqrt(vel_x² + vel_z²) should be |t4| (=10).  u ∈ [0,1)
     * so sin/cos cover the full unit circle; mag is exactly |t4|. */
    float r = sqrtf(vel_x * vel_x + vel_z * vel_z);
    T_ASSERT(fabsf(r - 10.0f) < 1e-3f);
    return 0;
}

/* ─── shape <2 owner-driven branch (shape_mode == 2 / 3) ──────────── */

int test_overlay_spawn_shape_lt2_mode_2_reads_owner_904(void)
{
    reset_world();
    /* template[3] = scale_base_mul = 1, so scale_base in slot = param_6 * 1 = 4. */
    tpl_set_i(40, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(40, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 1);
    tpl_set_f(40, SCENE1_OVERLAY_TPL_OFF_SCALE_BASE_MUL, 1.0f);

    /* Fake owner blob — write floats at byte offsets 0x904/908/90c. */
    unsigned char owner[0x920];
    memset(owner, 0, sizeof owner);
    float fx = 0.5f, fy = -0.25f, fz = 2.0f;
    memcpy(owner + 0x904, &fx, 4);
    memcpy(owner + 0x908, &fy, 4);
    memcpy(owner + 0x90c, &fz, 4);

    scene1_overlay_spawn(owner, 10.0f, 20.0f, 30.0f, 40,
                         /*scale_base=*/4.0f,
                         /*override_dur=*/0, /*override_rot_y=*/0,
                         /*shape_mode=*/2, /*mode=*/0);

    /* bend.x/y = owner_field * scale_base; bend.z = owner+0x90c * scale_base. */
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BEND_X) - (0.5f  * 4.0f)) < 1e-5f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BEND_Y) - (-0.25f * 4.0f)) < 1e-5f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BEND_Z) - (2.0f  * 4.0f)) < 1e-5f);

    /* pos.{x,y,z} = bend.{x,y,z} * 3.0 + (px,py,pz). */
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_X) -
                   (2.0f * 3.0f + 10.0f)) < 1e-4f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_Y) -
                   (-1.0f * 3.0f + 20.0f)) < 1e-4f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_POS_Z) -
                   (8.0f * 3.0f + 30.0f)) < 1e-4f);
    return 0;
}

int test_overlay_spawn_shape_lt2_mode_5_reads_owner_3fc(void)
{
    reset_world();
    tpl_set_i(41, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(41, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 0);
    tpl_set_f(41, SCENE1_OVERLAY_TPL_OFF_SCALE_BASE_MUL, 1.0f);

    unsigned char owner[0x500];
    memset(owner, 0, sizeof owner);
    float fx = 1.0f, fy = 2.0f, fz = 3.0f;
    memcpy(owner + 0x3fc, &fx, 4);
    memcpy(owner + 0x400, &fy, 4);
    memcpy(owner + 0x404, &fz, 4);

    scene1_overlay_spawn(owner, 0,0,0, 41, /*scale_base=*/2.0f,
                         0, 0, /*shape_mode=*/5, 0);

    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BEND_X) - 2.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BEND_Y) - 4.0f) < 1e-5f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BEND_Z) - 6.0f) < 1e-5f);
    return 0;
}

int test_overlay_spawn_shape_lt2_default_branch_uses_rng(void)
{
    /* For shape_mode not in {2,3,5} (and type_shape<2), the else branch
     * fires 3 RNG draws + paired sin/cos.  Hard to assert exact values,
     * but we can verify pos differs from (px, py, pz) (almost surely
     * non-zero with template[4] != 0). */
    reset_world();
    tpl_set_i(42, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(42, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 0);
    tpl_set_f(42, SCENE1_OVERLAY_TPL_OFF_SCALE_BASE_MUL, 1.0f);
    tpl_set_f(42, SCENE1_OVERLAY_TPL_OFF_INIT_ARG_4, 10.0f);
    tpl_set_f(42, SCENE1_OVERLAY_TPL_OFF_SCALE_Y_RATIO, 0.5f);

    scene1_overlay_spawn(NULL, 0,0,0, 42, /*scale_base=*/1.0f,
                         0, 0, /*shape_mode=*/0, 0);

    /* slot[11..13] should differ from zero (with overwhelming prob). */
    float bx = slot_f(0, SCENE1_OVERLAY_OFF_BEND_X);
    float by = slot_f(0, SCENE1_OVERLAY_OFF_BEND_Y);
    float bz = slot_f(0, SCENE1_OVERLAY_OFF_BEND_Z);
    T_ASSERT(fabsf(bx) + fabsf(by) + fabsf(bz) > 1e-5f);
    return 0;
}

/* ─── template-copy fidelity ──────────────────────────────────────── */

int test_overlay_spawn_template_copies_int_and_float_fields(void)
{
    reset_world();
    tpl_set_i(50, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT,   1);
    tpl_set_i(50, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE,    2);
    tpl_set_i(50, SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE,  77);
    tpl_set_f(50, SCENE1_OVERLAY_TPL_OFF_TEMPLATE5,     -2.5f);
    tpl_set_f(50, SCENE1_OVERLAY_TPL_OFF_TEMPLATE6,      1.25f);
    tpl_set_f(50, SCENE1_OVERLAY_TPL_OFF_SCALE_X,        0.75f);
    tpl_set_f(50, SCENE1_OVERLAY_TPL_OFF_TEMPLATE11,    -0.5f);
    tpl_set_f(50, SCENE1_OVERLAY_TPL_OFF_BLEND_MIX,      0.3f);
    tpl_set_f(50, SCENE1_OVERLAY_TPL_OFF_SCALE_Y_RATIO,  0.6f);
    tpl_set_i(50, SCENE1_OVERLAY_TPL_OFF_FADE_IN_DUR,   11);
    tpl_set_i(50, SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DUR,  22);

    scene1_overlay_spawn(NULL, 0,0,0, 50, 0.0f, 0,0,0,0);

    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_TEXTURE_TYPE), 77);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_TEMPLATE5_COPY)  - -2.5f)  < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_UNK_48)          -  1.25f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_SCALE_X)         -  0.75f) < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_TEMPLATE11_COPY) - -0.5f)  < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_BLEND_MIX)       -  0.3f)  < 1e-6f);
    T_ASSERT(fabsf(slot_f(0, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO)   -  0.6f)  < 1e-6f);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_FADE_IN_DUR),  11);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_FADE_OUT_DUR), 22);
    return 0;
}

int test_overlay_spawn_owner_pointer_written_twice(void)
{
    /* slot[36] and slot[37] both hold the template_owner pointer
     * (template_owner_a and _b in engine, presumably for fast-access
     * by the per-frame integrator vs renderer). */
    reset_world();
    tpl_set_i(60, SCENE1_OVERLAY_TPL_OFF_SPAWN_COUNT, 1);
    tpl_set_i(60, SCENE1_OVERLAY_TPL_OFF_TYPE_SHAPE, 2);

    unsigned char owner_blob[0x10];
    scene1_overlay_spawn(owner_blob, 0,0,0, 60, 0.0f, 0,0,0,0);

    int32_t expected = (int32_t)(intptr_t)owner_blob;
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_OWNER_A), expected);
    T_ASSERT_EQ_I(slot_i(0, SCENE1_OVERLAY_OFF_OWNER_B), expected);
    return 0;
}

/* ═════════════════ O.3: dispatcher shell + shape 0/5 ═════════════════ */
/* Tests below cover the D3D-free helpers in scene1_overlay_helpers.c.
 * The actual scene1_overlay_render entry is #ifdef _WIN32 and not
 * host-callable; we exercise each helper in isolation. */

/* ─── helpers for direct slot/shape setup ─────────────────────────── */

static void slot_set_f_dir(int s, int off, float v)
{
    scene1_overlay_slot_set_i(s, off, f_to_bits(v));
}

static void shape_set_i(int idx, int off, int32_t v)
{
    g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE + off] = v;
}

/* Build a live slot with sensible defaults — caller overrides what
 * matters per test.  Returns a pointer to the slot. */
static int32_t *fresh_slot(int s)
{
    scene1_overlay_init();
    scene1_overlay_slot_set_i(s, SCENE1_OVERLAY_OFF_ACTIVE, 0);     /* alive */
    scene1_overlay_slot_set_i(s, SCENE1_OVERLAY_OFF_LAYER,  0);
    scene1_overlay_slot_set_i(s, SCENE1_OVERLAY_OFF_MODE,   0);
    scene1_overlay_slot_set_i(s, SCENE1_OVERLAY_OFF_AGE,    0);
    return &g_scene1_overlay_slots[s * SCENE1_OVERLAY_SLOT_STRIDE];
}

/* ─── gate cascade ────────────────────────────────────────────────── */

int test_overlay_should_emit_active_minus1_rejected(void)
{
    int32_t *slot = fresh_slot(7);
    slot[SCENE1_OVERLAY_OFF_ACTIVE] = -1;
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 0);
    return 0;
}

int test_overlay_should_emit_layer_mismatch_rejected(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_LAYER] = 3;
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 2, 0, 0), 0);
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 3, 0, 0), 1);
    return 0;
}

int test_overlay_should_emit_mode_mismatch_rejected(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_MODE] = 1;
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 0);
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 1, 0), 1);
    return 0;
}

int test_overlay_should_emit_tex_group_must_match_outer(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_TEXTURE_TYPE] = 5;
    shape_set_i(5, SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP, 2);
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 0);
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 2), 1);
    return 0;
}

int test_overlay_should_emit_negative_age_rejected(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_AGE] = -1;
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 0);
    slot[SCENE1_OVERLAY_OFF_AGE] = 0;
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 1);
    return 0;
}

int test_overlay_should_emit_oob_texture_type_rejected(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_TEXTURE_TYPE] = 999;   /* > 256 cap */
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 0);
    slot[SCENE1_OVERLAY_OFF_TEXTURE_TYPE] = -1;
    T_ASSERT_EQ_I(scene1_overlay_should_emit(slot, 0, 0, 0), 0);
    return 0;
}

/* ─── fade compute ────────────────────────────────────────────────── */

int test_overlay_fade_default_full_alpha(void)
{
    int32_t *slot = fresh_slot(0);
    /* All-zero: fade_in_dur=0, fade_out_dur=0, blend_byte=0 →
     * alpha=255, alpha_mix=1.0 (blend_byte 0 also makes color_val
     * follow alpha — so color is 255 and alpha mul is 1.0). */
    int   ai;
    float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 255);
    T_ASSERT(fabsf(am - 1.0f) < 1e-6f);
    return 0;
}

int test_overlay_fade_in_ramps(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_FADE_IN_DUR] = 10;
    slot[SCENE1_OVERLAY_OFF_AGE]         = 4;
    /* alpha = 4 * 255 / 10 = 102 */
    int   ai;
    float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 102);
    return 0;
}

int test_overlay_fade_in_clamps_to_255(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_FADE_IN_DUR] = 10;
    slot[SCENE1_OVERLAY_OFF_AGE]         = 100;
    int ai; float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 255);
    return 0;
}

int test_overlay_fade_out_kicks_in(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_FADE_OUT_DUR]    = 5;
    slot[SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 10;
    slot[SCENE1_OVERLAY_OFF_AGE]             = 8;
    slot[SCENE1_OVERLAY_OFF_AGE_BIRTH]       = 0;
    /* delta=8; (offset-dur)=5; 5 < 8 → fade-out active.
     * step = 255/5 = 51 (int div); adj = (8-10)+5 = 3.
     * alpha = 255 - 3*51 = 102. */
    int ai; float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 102);
    return 0;
}

int test_overlay_fade_out_skipped_when_shape_mode_4_and_unk_48_nonzero(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_FADE_OUT_DUR]    = 5;
    slot[SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 10;
    slot[SCENE1_OVERLAY_OFF_AGE]             = 100;
    slot[SCENE1_OVERLAY_OFF_SHAPE_MODE]      = 4;
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_UNK_48, 1.0f);
    int ai; float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 255);   /* fade-out skipped → full alpha */
    return 0;
}

int test_overlay_fade_returns_zero_when_alpha_negative(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_FADE_OUT_DUR]    = 1;
    slot[SCENE1_OVERLAY_OFF_FADE_OUT_OFFSET] = 0;
    slot[SCENE1_OVERLAY_OFF_AGE]             = 100;
    /* step = 255/1 = 255; adj = (100-0)+1 = 101; alpha = 255 - 101*255 < 0 */
    int ai; float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 0);
    return 0;
}

int test_overlay_fade_blend_byte_1_only_alpha_mix_follows(void)
{
    int32_t *slot = fresh_slot(0);
    /* blend_byte=1 → color stays 255, alpha_mix follows alpha/255. */
    scene1_overlay_slot_set_i(0, SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE,
                              (scene1_overlay_slot_get_i(0, SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE) & ~0xff) | 1);
    slot[SCENE1_OVERLAY_OFF_FADE_IN_DUR] = 10;
    slot[SCENE1_OVERLAY_OFF_AGE]         = 5;
    int ai; float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 255);       /* color stays 255 */
    T_ASSERT(fabsf(am - (127.5f / 255.0f)) < 1e-3f);
    return 0;
}

int test_overlay_fade_blend_byte_2_both_follow(void)
{
    int32_t *slot = fresh_slot(0);
    scene1_overlay_slot_set_i(0, SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE,
                              (scene1_overlay_slot_get_i(0, SCENE1_OVERLAY_OFF_BLEND_MODE_BYTE) & ~0xff) | 2);
    slot[SCENE1_OVERLAY_OFF_FADE_IN_DUR] = 10;
    slot[SCENE1_OVERLAY_OFF_AGE]         = 4;
    int ai; float am;
    T_ASSERT_EQ_I(scene1_overlay_compute_fade(slot, &ai, &am), 1);
    T_ASSERT_EQ_I(ai, 102);
    T_ASSERT(fabsf(am - (102.0f / 255.0f)) < 1e-3f);
    return 0;
}

/* ─── shape 0/5 scale ─────────────────────────────────────────────── */

int test_overlay_shape_05_scale_formula(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.25f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    3.0f);

    /* sx = (1-0.25)*2*1.0*3*0.003/0.5 = 0.75*2*3*0.006 = 0.027 */
    /* sy = 0.25*2*1.0*3*0.003/0.5 = 0.009 */
    float sx, sy;
    scene1_overlay_shape_05_scale(slot, 1.0f, &sx, &sy);
    T_ASSERT(fabsf(sx - 0.027f) < 1e-5f);
    T_ASSERT(fabsf(sy - 0.009f) < 1e-5f);
    return 0;
}

int test_overlay_shape_05_scale_alpha_mix_scales_both(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);

    float sx_full, sy_full, sx_half, sy_half;
    scene1_overlay_shape_05_scale(slot, 1.0f, &sx_full, &sy_full);
    scene1_overlay_shape_05_scale(slot, 0.5f, &sx_half, &sy_half);
    T_ASSERT(fabsf(sx_half - 0.5f * sx_full) < 1e-6f);
    T_ASSERT(fabsf(sy_half - 0.5f * sy_full) < 1e-6f);
    return 0;
}

/* ─── shape 0/5 world matrix ──────────────────────────────────────── */

int test_overlay_shape_05_world_has_translation_row(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X, 7.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Y, -3.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Z, 11.0f);
    /* Identity pre-matrix + zero scale → row 3 of M holds translation
     * (D3DX row-major: M[12..14] = T). */
    float world[16];
    scene1_overlay_shape_05_compose_world(world, slot, 1.0f);
    T_ASSERT(fabsf(world[12] -  7.0f) < 1e-5f);
    T_ASSERT(fabsf(world[13] - -3.5f) < 1e-5f);
    T_ASSERT(fabsf(world[14] - 11.0f) < 1e-5f);
    return 0;
}

int test_overlay_shape_05_world_uses_pre_matrix(void)
{
    /* Verify pre-matrix is actually applied by setting a non-identity
     * pre (X-translate +10) and checking it shows up in the final
     * world matrix.  In row-major D3D, the pre-matrix's row 3 (its
     * own translation) interacts with S*T's row 0 — so we need a
     * non-zero scale for the contribution to be visible at M[12].
     * With sx = (1-mix)*sb*am*sx_slot*0.003/0.5, pick values that
     * give a clean sx == 0.003: mix=0, sb=1, am=1, slot_sx=1 →
     * sx = 1*1*1*1*0.003/0.5 = 0.006.  Then
     *   (pre*S*T)[3] = pre[3][0]*ST[0] + pre[3][3]*ST[3]
     *               = 10 * [0.006, 0, 0, 0] + 1 * [tx, ty, tz, 1]
     *   M[12] = 10*0.006 + tx = 0.06 + 5 = 5.06. */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X,      5.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);

    float pre[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        10, 0, 0, 1
    };
    wf_pass_c_set_pre_matrix(pre);

    float world[16];
    scene1_overlay_shape_05_compose_world(world, slot, 1.0f);
    T_ASSERT(fabsf(world[12] - 5.06f) < 1e-4f);

    /* Restore identity for downstream tests. */
    float id[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    wf_pass_c_set_pre_matrix(id);
    return 0;
}

/* ─── frame UV selection ──────────────────────────────────────────── */

int test_overlay_frame_uv_static_when_frame_count_le_1(void)
{
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X] = f_to_bits(50.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y] = f_to_bits(40.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]   = f_to_bits(16.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]   = f_to_bits(16.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT] = 1;

    float u, v;
    scene1_overlay_shape_05_frame_uv(shape, /*rng_seed=*/123, &u, &v);
    T_ASSERT(fabsf(u - 50.0f) < 1e-5f);
    T_ASSERT(fabsf(v - 40.0f) < 1e-5f);
    return 0;
}

int test_overlay_frame_uv_animated_picks_tile_via_rng_seed(void)
{
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X] = f_to_bits(0.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y] = f_to_bits(0.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]   = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]   = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT] = 4;
    /* frames_per_row = (int)(256/32) = 8; rng_seed=11 → col=3, row=1. */
    float u, v;
    scene1_overlay_shape_05_frame_uv(shape, /*rng_seed=*/11, &u, &v);
    T_ASSERT(fabsf(u - 96.0f) < 1e-5f);   /* 3 * 32 + 0 */
    T_ASSERT(fabsf(v - 32.0f) < 1e-5f);   /* 1 * 32 + 0 */
    return 0;
}

int test_overlay_frame_uv_null_shape_returns_zero(void)
{
    float u = 99, v = 99;
    scene1_overlay_shape_05_frame_uv(NULL, 0, &u, &v);
    T_ASSERT(fabsf(u) < 1e-6f);
    T_ASSERT(fabsf(v) < 1e-6f);
    return 0;
}

/* ─── diffuse gray encoding ───────────────────────────────────────── */

int test_overlay_diffuse_gray_encodes_correctly(void)
{
    T_ASSERT_EQ_I((int)scene1_overlay_diffuse_gray(0),     (int)0xff000000u);
    T_ASSERT_EQ_I((int)scene1_overlay_diffuse_gray(0x7f),  (int)0xff7f7f7fu);
    T_ASSERT_EQ_I((int)scene1_overlay_diffuse_gray(0xff),  (int)0xffffffffu);
    T_ASSERT_EQ_I((int)scene1_overlay_diffuse_gray(0x80),  (int)0xff808080u);
    return 0;
}

/* ─── shape 0/5 quad emit ─────────────────────────────────────────── */

int test_overlay_shape_05_emit_even_slot_idx_uv_layout(void)
{
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(16.0f);
    scene1_overlay_vertex vbuf[4] = {0};

    scene1_overlay_shape_05_emit_quad(vbuf, shape, /*uv_origin*/0, 0,
                                      /*slot_idx=*/0,
                                      /*alpha_int=*/128);
    float u_a = (0.0f + 0.5f) / 256.0f;
    float u_b = (0.0f + 32.0f - 0.5f) / 256.0f;
    float v_t = (0.0f + 0.5f) / 256.0f;
    float v_b = (0.0f + 16.0f - 0.5f) / 256.0f;

    /* Even slot_idx: v0 = u_right (= u_b), v2 = u_left (= u_a). */
    T_ASSERT(fabsf(vbuf[0].u - u_b) < 1e-6f);
    T_ASSERT(fabsf(vbuf[2].u - u_a) < 1e-6f);
    T_ASSERT(fabsf(vbuf[1].u - u_b) < 1e-6f);  /* v1 shares U with v0 */
    T_ASSERT(fabsf(vbuf[3].u - u_a) < 1e-6f);  /* v3 shares U with v2 */

    T_ASSERT(fabsf(vbuf[0].v - v_t) < 1e-6f);
    T_ASSERT(fabsf(vbuf[1].v - v_b) < 1e-6f);
    T_ASSERT(fabsf(vbuf[2].v - v_t) < 1e-6f);
    T_ASSERT(fabsf(vbuf[3].v - v_b) < 1e-6f);

    T_ASSERT_EQ_I((int)vbuf[0].diffuse, (int)0xff808080u);
    return 0;
}

int test_overlay_shape_05_emit_odd_slot_idx_horizontal_flip(void)
{
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(16.0f);
    scene1_overlay_vertex vbuf[4] = {0};

    scene1_overlay_shape_05_emit_quad(vbuf, shape, 0, 0,
                                      /*slot_idx=*/1,
                                      /*alpha_int=*/64);
    float u_a = (0.0f + 0.5f) / 256.0f;
    float u_b = (0.0f + 32.0f - 0.5f) / 256.0f;
    /* Odd slot_idx: swap. */
    T_ASSERT(fabsf(vbuf[0].u - u_a) < 1e-6f);
    T_ASSERT(fabsf(vbuf[2].u - u_b) < 1e-6f);
    return 0;
}

/* ─── layer storage reset ─────────────────────────────────────────── */

int test_overlay_layers_reset_clears_count_and_pointers(void)
{
    g_scene1_overlay_layer_count = 17;
    g_scene1_overlay_layer_textures[3] = (void *)(intptr_t)0xdeadbeef;
    scene1_overlay_layers_reset();
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 0);
    T_ASSERT(g_scene1_overlay_layer_textures[3] == NULL);
    return 0;
}

int test_overlay_init_also_resets_layers(void)
{
    g_scene1_overlay_layer_count = 99;
    scene1_overlay_init();
    T_ASSERT_EQ_I(g_scene1_overlay_layer_count, 0);
    return 0;
}

/* ═════════════════ O.4: shapes 2/3/4/6 matrix variants ═════════════════ */

int test_overlay_shape_2346_uniform_scale_formula(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    3.0f);
    /* alpha_mix=0.5 → s = 2*3*0.5*0.003 = 0.009 */
    float s = scene1_overlay_shape_2346_uniform_scale(slot, 0.5f);
    T_ASSERT(fabsf(s - 0.009f) < 1e-6f);
    return 0;
}

int test_overlay_shape_2_uses_pre_matrix(void)
{
    /* Verify shape 2 multiplies by pre_matrix.  Set pre = X-translate
     * by 100, slot pos.x=2, scale=1 (manufacture via sb=sx=am=1, then
     * s = 1*1*1*0.003 = 0.003).  M[12] = 100*0.003 + 2 = 2.3. */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X, 2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);

    float pre[16] = {
        1,0,0,0,
        0,1,0,0,
        0,0,1,0,
        100,0,0,1
    };
    wf_pass_c_set_pre_matrix(pre);

    float world[16];
    scene1_overlay_shape_2_compose_world(world, slot, 1.0f);
    T_ASSERT(fabsf(world[12] - 2.3f) < 1e-4f);

    float id[16] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    wf_pass_c_set_pre_matrix(id);
    return 0;
}

int test_overlay_shape_4_applies_roty_quarter(void)
{
    /* Shape 4: RotY(π/2) × (S × T).  With a unit-scale ST and a unit
     * pos at (0,0,0), the world matrix should be pure RotY(π/2).
     * RotY(π/2):
     *   [cos, 0, -sin, 0]   [0, 0, -1, 0]
     *   [0,   1,  0,   0] = [0, 1,  0, 0]
     *   [sin, 0,  cos, 0]   [1, 0,  0, 0]
     *   [0,   0,  0,   1]   [0, 0,  0, 1]
     * S × T with S=scaling(s) and T=trans(0): M = scaling(s).
     * Then RotY × scaling(s) leaves:
     *   row 0: [0*s, 0, -1*s, 0] = [0, 0, -s, 0]
     *   row 2: [1*s, 0, 0, 0] = [s, 0, 0, 0]
     *
     * Actually let me check actual D3DXMatrixRotationY convention.
     * Our mat4_rotation_y produces row-major:
     *   [cos, 0, -sin, 0]
     *   [0,   1,  0,   0]
     *   [sin, 0,  cos, 0]
     *   [0,   0,  0,   1]
     * For π/2: cos=0, sin=1. So M_world[2] = -s (since RotY[0][2] = -sin = -1,
     * times scaling[2][2] = s).
     */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    float s = 0.003f;   /* matches formula */
    float world[16];
    scene1_overlay_shape_4_compose_world(world, slot, 1.0f);
    /* world[0..3] = RotY[0] * scaling = [0, 0, -s, 0] */
    T_ASSERT(fabsf(world[0]) < 1e-6f);
    T_ASSERT(fabsf(world[2] - -s) < 1e-6f);
    /* world[8..11] = RotY[2] * scaling = [s, 0, 0, 0] */
    T_ASSERT(fabsf(world[8] - s) < 1e-6f);
    T_ASSERT(fabsf(world[10]) < 1e-6f);
    return 0;
}

int test_overlay_shape_6_uses_rot_x_field(void)
{
    /* Shape 6 reads slot[+0x3c] (= OFF_ROT_Y, Ghidra-named "rot.y")
     * and applies it via RotationX.  Set the "rot.x" field (OFF_ROT_X)
     * to something nonzero to prove the engine doesn't read it. */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_ROT_X, 5.0f);   /* should be IGNORED */
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_ROT_Y, 1.5707963f);  /* π/2 → RotX */

    float world[16];
    scene1_overlay_shape_6_compose_world(world, slot, 1.0f);
    /* RotX(π/2) row 1: [0, cos, sin, 0] = [0, 0, 1, 0]
     * After scaling(s,s,s) × T(0): M = scaling(s).
     * Then RotX × scaling(s):
     *   row 1: [0*s, 0, 1*s, 0] = [0, 0, s, 0]
     *   row 2: [0*s, -1*s, 0, 0] = [0, -s, 0, 0] */
    float s = 0.003f;
    T_ASSERT(fabsf(world[5]) < 1e-6f);          /* row 1 col 1 */
    T_ASSERT(fabsf(world[6] - s) < 1e-6f);      /* row 1 col 2 */
    T_ASSERT(fabsf(world[9] - -s) < 1e-6f);     /* row 2 col 1 */
    return 0;
}

int test_overlay_shape_3_off_diagonal_field_mapping(void)
{
    /* Shape 3 applies:
     *   slot[ROT_Z] → about Z axis  (canonical)
     *   slot[ROT_X] → about Y axis  (off-diagonal)
     *   slot[ROT_Y] → about X axis  (off-diagonal)
     *
     * To verify: set ROT_X to π/2, leave ROT_Y/ROT_Z at 0.  Expected
     * result is RotY(π/2) × (S × T) — same as shape 4's matrix.  */
    int32_t *slot4 = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    float world4[16];
    scene1_overlay_shape_4_compose_world(world4, slot4, 1.0f);

    int32_t *slot3 = fresh_slot(1);
    slot_set_f_dir(1, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(1, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    slot_set_f_dir(1, SCENE1_OVERLAY_OFF_ROT_X,      1.5707963f);  /* drives RotY in shape 3 */
    float world3[16];
    scene1_overlay_shape_3_compose_world(world3, slot3, 1.0f);

    for (int i = 0; i < 16; i++) {
        T_ASSERT(fabsf(world3[i] - world4[i]) < 1e-5f);
    }
    (void)slot3;   /* used via slot_set_f_dir */
    return 0;
}

int test_overlay_shape_1346_emit_no_flip(void)
{
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(16.0f);
    scene1_overlay_vertex vbuf[4] = {0};

    /* Non-flipped: v0/v1 always u_left, v2/v3 always u_right, for
     * ANY slot_idx (no parity dependence). */
    scene1_overlay_shape_1346_emit_quad(vbuf, shape, 0, 0,
                                        /*alpha_int=*/0x40);
    float u_left  = (0.0f + 0.5f) / 256.0f;
    float u_right = (0.0f + 32.0f - 0.5f) / 256.0f;
    T_ASSERT(fabsf(vbuf[0].u - u_left)  < 1e-6f);
    T_ASSERT(fabsf(vbuf[1].u - u_left)  < 1e-6f);
    T_ASSERT(fabsf(vbuf[2].u - u_right) < 1e-6f);
    T_ASSERT(fabsf(vbuf[3].u - u_right) < 1e-6f);
    T_ASSERT_EQ_I((int)vbuf[0].diffuse, (int)0xff404040u);
    return 0;
}

int test_overlay_shape_2346_uniform_scale_ignores_blend_mix(void)
{
    /* The shape 2/3/4/6 scale formula does NOT involve blend_mix —
     * unlike shape 0/5.  Verify by changing blend_mix and confirming
     * the result is unchanged. */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.0f);
    float s_a = scene1_overlay_shape_2346_uniform_scale(slot, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.7f);
    float s_b = scene1_overlay_shape_2346_uniform_scale(slot, 1.0f);
    T_ASSERT(fabsf(s_a - s_b) < 1e-9f);
    return 0;
}

/* ═════════════════ O.5: shape 1 lookat billboard ═════════════════ */

int test_overlay_shape_1_extra_scale_default(void)
{
    /* delta=0..8 → extra = 0.02. */
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_AGE]       = 5;
    slot[SCENE1_OVERLAY_OFF_AGE_BIRTH] = 0;
    T_ASSERT(fabsf(scene1_overlay_shape_1_extra_scale(slot) - 0.02f) < 1e-6f);
    slot[SCENE1_OVERLAY_OFF_AGE]       = 8;
    T_ASSERT(fabsf(scene1_overlay_shape_1_extra_scale(slot) - 0.02f) < 1e-6f);
    return 0;
}

int test_overlay_shape_1_extra_scale_negative_age_returns_zero(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_AGE]       = 0;
    slot[SCENE1_OVERLAY_OFF_AGE_BIRTH] = 5;   /* delta = -5 */
    T_ASSERT(fabsf(scene1_overlay_shape_1_extra_scale(slot)) < 1e-9f);
    return 0;
}

int test_overlay_shape_1_extra_scale_ramps_down(void)
{
    int32_t *slot = fresh_slot(0);
    slot[SCENE1_OVERLAY_OFF_AGE_BIRTH] = 0;
    slot[SCENE1_OVERLAY_OFF_AGE]       = 12;   /* delta=12; ramp = 0.02 - 4*0.001 = 0.016 */
    T_ASSERT(fabsf(scene1_overlay_shape_1_extra_scale(slot) - 0.016f) < 1e-6f);
    slot[SCENE1_OVERLAY_OFF_AGE]       = 28;   /* delta=28; ramp = 0.02 - 20*0.001 = 0 → skip */
    T_ASSERT(fabsf(scene1_overlay_shape_1_extra_scale(slot)) < 1e-9f);
    slot[SCENE1_OVERLAY_OFF_AGE]       = 30;   /* delta=30; ramp negative → skip */
    T_ASSERT(fabsf(scene1_overlay_shape_1_extra_scale(slot)) < 1e-9f);
    return 0;
}

int test_overlay_shape_1_scale_xyz_z_is_2y(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    float sx, sy, sz;
    scene1_overlay_shape_1_scale_xyz(slot, /*alpha_mix=*/1.0f,
                                     /*extra=*/0.02f,
                                     &sx, &sy, &sz);
    /* sx = 0.5*1*1*1*0.0588/0.5*0.02 = 0.5 * 0.0588 / 0.5 * 0.02 = 0.00118 */
    T_ASSERT(fabsf(sx - 0.5f * 0.0588f / 0.5f * 0.02f) < 1e-6f);
    /* sy = 0.5*1*1*1*1.386/0.5*0.015 = 0.5 * 1.386 / 0.5 * 0.015 = 0.02079 */
    T_ASSERT(fabsf(sy - 0.5f * 1.386f / 0.5f * 0.015f) < 1e-6f);
    /* sz = 2 * sy */
    T_ASSERT(fabsf(sz - 2.0f * sy) < 1e-6f);
    return 0;
}

int test_overlay_shape_1_scale_sy_does_not_use_extra(void)
{
    /* Engine asm shows sy is computed WITHOUT the * extra factor that
     * sx receives — sy uses the unconditional 0.015 multiplier only. */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    float sx_a, sy_a, sz_a;
    float sx_b, sy_b, sz_b;
    scene1_overlay_shape_1_scale_xyz(slot, 1.0f, 0.02f, &sx_a, &sy_a, &sz_a);
    scene1_overlay_shape_1_scale_xyz(slot, 1.0f, 0.01f, &sx_b, &sy_b, &sz_b);
    /* sy should be identical for different extras. */
    T_ASSERT(fabsf(sy_a - sy_b) < 1e-9f);
    /* sx should halve when extra halves. */
    T_ASSERT(fabsf(sx_b - 0.5f * sx_a) < 1e-6f);
    return 0;
}

int test_overlay_shape_1_world_uses_pos_for_eye(void)
{
    /* Smoke test: with camera at (0, 10, 0) and pos at (0, 0, 0),
     * the lookat is degenerate (target-eye = bend, up = camera - pos =
     * (0, 10, 0)).  Setting bend = (0, 0, 1) and verifying the matrix
     * inverts cleanly is hard analytically — just check that the
     * function doesn't crash + produces a finite matrix.  */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X, 0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Y, 0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Z, 0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BEND_X, 0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BEND_Y, 0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BEND_Z, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    float camera_eye[3] = { 0.0f, 10.0f, 0.0f };

    float world[16];
    scene1_overlay_shape_1_compose_world(world, slot, 1.0f, 0.02f, camera_eye);
    /* All 16 entries should be finite. */
    for (int i = 0; i < 16; i++) {
        T_ASSERT(world[i] == world[i]);   /* NaN check */
    }
    /* M[15] should be 1.0 (affine bottom-right). */
    T_ASSERT(fabsf(world[15] - 1.0f) < 1e-3f);
    return 0;
}

int test_overlay_shape_1_world_translation_matches_pos(void)
{
    /* The lookat+inverse should restore the world translation to pos
     * (engine builds lookat from pos as eye, inverts it; the inverse
     * lookat moves the local origin to world pos).  Verify M[12..14]
     * roughly equals pos after the additional RotY × scale layers
     * (rotations + scales don't move the translation row, so M[12..14]
     * stays = pos). */
    int32_t *slot = fresh_slot(0);
    float test_pos[3] = { 3.0f, -2.5f, 7.0f };
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X, test_pos[0]);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Y, test_pos[1]);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Z, test_pos[2]);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BEND_Z, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    float camera_eye[3] = { 0.0f, 20.0f, 0.0f };

    float world[16];
    scene1_overlay_shape_1_compose_world(world, slot, 1.0f, 0.02f, camera_eye);
    T_ASSERT(fabsf(world[12] - test_pos[0]) < 1e-3f);
    T_ASSERT(fabsf(world[13] - test_pos[1]) < 1e-3f);
    T_ASSERT(fabsf(world[14] - test_pos[2]) < 1e-3f);
    return 0;
}
