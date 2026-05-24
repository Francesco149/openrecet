/*
 * test_scene1_overlay.c — unit tests for O.2: FUN_00414345 spawn API +
 * the slot/template/per-shape storage in src/scene1_overlay.{c,h}.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "rng.h"
#include "scene1_overlay.h"

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
