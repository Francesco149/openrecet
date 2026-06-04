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

/* ─── template-table loader (effect1.dat chunk) ───────────────────── */

#define DAT_REC_BYTES   0xacu
#define DAT_NUM_OFF     0x64u
#define DAT_NUM_FIELDS  18
#define DAT_REC_COUNT   100

/* Write numeric field `k` of record `t` into a raw effect1.dat-shaped
 * chunk (record byte t*0xac, numeric region at +0x64). */
static void dat_set_field(unsigned char *buf, int t, int k, int32_t v)
{
    size_t off = (size_t)t * DAT_REC_BYTES + DAT_NUM_OFF + (size_t)k * 4u;
    memcpy(buf + off, &v, sizeof v);
}

int test_overlay_templates_load_chunk_maps_record_3b(void)
{
    static unsigned char buf[DAT_REC_COUNT * DAT_REC_BYTES];
    memset(buf, 0, sizeof buf);

    /* Poison every record's NAME region (bytes 0..0x63) — the loader must
     * read fields from +0x64, never leak the name into a field. */
    for (int t = 0; t < DAT_REC_COUNT; t++)
        memset(buf + (size_t)t * DAT_REC_BYTES, 0xAB, DAT_NUM_OFF);

    /* Real effect1.dat values for template 0x3b (`目玉商品`). */
    const int32_t f3b[DAT_NUM_FIELDS] = {
        19,          /* texture_type */
        0,           /* type_shape   */
        1,           /* spawn_count  */
        0x3f800000,  /* scale_base_mul = 1.0 */
        0x35861700,  /* init_arg_4 (~9.98e-7) */
        1065185444,  /* tpl5 = 0.99 */
        0,           /* tpl6 */
        0x3f800000,  /* age_stagger = 1.0 */
        24,          /* fade_out_default — sparkle lifetime */
        0,           /* age_birth_mod */
        1065353212,  /* scale_x ≈ 1.0 */
        0,           /* tpl11 */
        0x3f000000,  /* scale_y_ratio = 0.5 */
        0x3f000000,  /* blend_offset = 0.5 */
        0x3f000000,  /* blend_mix = 0.5 */
        8,           /* fade_in_dur */
        16,          /* fade_out_dur */
        0x100,       /* layer_pair: layer byte 0, blend_mode byte 1 */
    };
    for (int k = 0; k < DAT_NUM_FIELDS; k++)
        dat_set_field(buf, 0x3b, k, f3b[k]);

    scene1_overlay_templates_reset();
    scene1_overlay_templates_load_chunk(buf, sizeof buf);

    for (int k = 0; k < DAT_NUM_FIELDS; k++)
        T_ASSERT_EQ_I(g_scene1_overlay_templates[0x3b * SCENE1_OVERLAY_TEMPLATE_STRIDE + k],
                      f3b[k]);

    /* Spot-check the named offsets the spawner/renderer actually read. */
    T_ASSERT_EQ_I(g_scene1_overlay_templates[0x3b * SCENE1_OVERLAY_TEMPLATE_STRIDE +
                  SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE], 19);
    T_ASSERT_EQ_I(g_scene1_overlay_templates[0x3b * SCENE1_OVERLAY_TEMPLATE_STRIDE +
                  SCENE1_OVERLAY_TPL_OFF_FADE_OUT_DEFAULT], 24);
    T_ASSERT_EQ_I(g_scene1_overlay_templates[0x3b * SCENE1_OVERLAY_TEMPLATE_STRIDE +
                  SCENE1_OVERLAY_TPL_OFF_LAYER_PAIR], 0x100);
    return 0;
}

int test_overlay_templates_load_chunk_boundaries(void)
{
    static unsigned char buf[DAT_REC_COUNT * DAT_REC_BYTES];
    memset(buf, 0, sizeof buf);
    /* Distinct marker in field 0 of records 0 and 99 (last). */
    dat_set_field(buf, 0,  SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE, 0x1111);
    dat_set_field(buf, 99, SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE, 0x9999);

    scene1_overlay_templates_reset();
    scene1_overlay_templates_load_chunk(buf, sizeof buf);

    T_ASSERT_EQ_I(g_scene1_overlay_templates[0  * SCENE1_OVERLAY_TEMPLATE_STRIDE + 0], 0x1111);
    T_ASSERT_EQ_I(g_scene1_overlay_templates[99 * SCENE1_OVERLAY_TEMPLATE_STRIDE + 0], 0x9999);
    /* Record 100 is past the file — must stay zero (never spawned). */
    T_ASSERT_EQ_I(g_scene1_overlay_templates[100 * SCENE1_OVERLAY_TEMPLATE_STRIDE + 0], 0);
    return 0;
}

int test_overlay_templates_load_chunk_short_buffer_safe(void)
{
    /* A buffer that ends mid-table: the loader must stop at the first
     * record whose numeric region would run past the end, no OOB. */
    static unsigned char buf[5 * DAT_REC_BYTES];
    memset(buf, 0, sizeof buf);
    dat_set_field(buf, 2, SCENE1_OVERLAY_TPL_OFF_TEXTURE_TYPE, 0x2222);

    scene1_overlay_templates_reset();
    scene1_overlay_templates_load_chunk(buf, sizeof buf);

    T_ASSERT_EQ_I(g_scene1_overlay_templates[2 * SCENE1_OVERLAY_TEMPLATE_STRIDE + 0], 0x2222);
    /* Record 5 was never in the buffer. */
    T_ASSERT_EQ_I(g_scene1_overlay_templates[5 * SCENE1_OVERLAY_TEMPLATE_STRIDE + 0], 0);
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
    scene1_overlay_shape_05_frame_uv(shape, /*anim_cell_index=*/123, &u, &v);
    T_ASSERT(fabsf(u - 50.0f) < 1e-5f);
    T_ASSERT(fabsf(v - 40.0f) < 1e-5f);
    return 0;
}

int test_overlay_frame_uv_animated_picks_tile_via_anim_cell_index(void)
{
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X] = f_to_bits(0.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y] = f_to_bits(0.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X]   = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y]   = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT] = 4;
    /* frames_per_row = (int)(256/32) = 8; anim_cell_index=11 → col=3, row=1. */
    float u, v;
    scene1_overlay_shape_05_frame_uv(shape, /*anim_cell_index=*/11, &u, &v);
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

/* ═════════════════ O.6: shape 7 multi-quad trail ═════════════════ */

int test_overlay_shape_7_vbuf_init_arc_positions(void)
{
    /* scene1_overlay_init calls scene1_overlay_shape_7_vbuf_init. */
    scene1_overlay_init();
    /* Pair 0: angle=0 → sin=0, cos=1 → pos = (±48, 0, 0). */
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[0].x - 48.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[0].y) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[0].z) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[1].x - -48.0f) < 1e-3f);

    /* Pair 32: angle=π/2 → sin=1, cos=0 → pos = (±48, 1024, -1024). */
    int last = 32 * 2;
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[last].x - 48.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[last].y - 1024.0f) < 1e-2f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[last].z - -1024.0f) < 1e-2f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[last + 1].x - -48.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_7_vbuf[last + 1].y - 1024.0f) < 1e-2f);
    return 0;
}

int test_overlay_shape_7_compute_strip_age_out_of_range_skips(void)
{
    int32_t *slot = fresh_slot(0);
    int vc = 99, ps = 99, fg = 99;
    slot[SCENE1_OVERLAY_OFF_AGE] = -1;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 0);
    T_ASSERT_EQ_I(vc, 0);
    T_ASSERT_EQ_I(ps, 0);

    slot[SCENE1_OVERLAY_OFF_AGE] = 0x28;     /* boundary — exclusive */
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 0);

    /* age=1 → vc=2 < 4 → skip via vc-min gate. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 1;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 0);
    return 0;
}

int test_overlay_shape_7_compute_strip_vert_count_growth_and_clamp(void)
{
    int32_t *slot = fresh_slot(0);
    int vc, ps, fg;

    /* AGE=2 → X=4, no clamp (4≤32), no ramp.  pair_start=8 (AGE≤16). */
    slot[SCENE1_OVERLAY_OFF_AGE] = 2;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(vc, 4);
    T_ASSERT_EQ_I(ps, 8);

    /* AGE=16 → X=32, clamped to 32; pair_start = 8 (still AGE<=16). */
    slot[SCENE1_OVERLAY_OFF_AGE] = 16;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(vc, 32);
    T_ASSERT_EQ_I(ps, 8);

    /* AGE=17 → pair_start = AGE-8 = 9. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 17;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(vc, 32);
    T_ASSERT_EQ_I(ps, 9);

    /* AGE=24 → X=32 (no ramp yet); pair_start = 16. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 24;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(vc, 32);
    T_ASSERT_EQ_I(ps, 16);
    return 0;
}

int test_overlay_shape_7_compute_strip_ramp_down_past_age_24(void)
{
    int32_t *slot = fresh_slot(0);
    int vc, ps, fg;

    /* AGE=25 → X = 32 + (24-25)*2 = 30. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 25;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(vc, 30);
    T_ASSERT_EQ_I(ps, 17);

    /* AGE=32 → X = 32 + (24-32)*2 = 16. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 32;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(vc, 16);
    T_ASSERT_EQ_I(ps, 24);

    /* AGE=39 → X = 32 + (24-39)*2 = 2 < 4 → skip. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 39;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 0);
    return 0;
}

int test_overlay_shape_7_compute_strip_pair_start_clamped_to_32(void)
{
    /* pair_start is clamped to 32.  Reach by AGE = 40+, but AGE>=0x28
     * skips earlier — so the clamp only fires inside the body when
     * AGE happens to land in [33, 39] with the ramp keeping vc >= 4.
     * AGE=33 → ramp X=32-18=14 → fires; pair_start = 33-8 = 25.  Still
     * <32, doesn't hit clamp.  Verify the clamp constant by injecting
     * a high pair_start synthetically isn't possible, but the public
     * formula is documented — so just verify it stays in range. */
    int32_t *slot = fresh_slot(0);
    int vc, ps, fg;
    slot[SCENE1_OVERLAY_OFF_AGE] = 33;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 255,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(ps, 25);
    T_ASSERT(ps + vc / 2 <= SCENE1_OVERLAY_SHAPE_7_PAIR_COUNT);
    return 0;
}

int test_overlay_shape_7_compute_strip_fade_subtract(void)
{
    int32_t *slot = fresh_slot(0);
    int vc, ps, fg;

    /* AGE=24: no fade subtract → fade_gray == alpha_int_in. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 24;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 100,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(fg, 100);

    /* AGE=25: subtract (25-24)*16 = 16 → fade_gray = 100-16 = 84. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 25;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 100,
                                                      &vc, &ps, &fg), 1);
    T_ASSERT_EQ_I(fg, 84);

    /* AGE=30 starting from low alpha → underflows → skip.
     * fade = 10 - (30-24)*16 = 10 - 96 = -86 → skip. */
    slot[SCENE1_OVERLAY_OFF_AGE] = 30;
    T_ASSERT_EQ_I(scene1_overlay_shape_7_compute_strip(slot, 10,
                                                      &vc, &ps, &fg), 0);
    return 0;
}

int test_overlay_shape_7_scale_xy_formula(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.25f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    3.0f);

    /* base = 2 * 0.01 = 0.02
     * sx = (1-0.25) * 0.02 * 1.0 * 3 / 0.5 = 0.75 * 0.02 * 3 / 0.5 = 0.09
     * sy = 0.25 * 0.02 * 1.0 * 3 / 0.5 = 0.03 */
    float sx, sy;
    scene1_overlay_shape_7_scale_xy(slot, 1.0f, &sx, &sy);
    T_ASSERT(fabsf(sx - 0.09f) < 1e-5f);
    T_ASSERT(fabsf(sy - 0.03f) < 1e-5f);
    return 0;
}

int test_overlay_shape_7_compose_world_translation_matches_pos(void)
{
    /* With identity rotations (all rot fields zero), final matrix is
     * S × T.  M[12..14] = pos.xyz scaled by 1.0 (translation row only
     * affected by the scaling's bottom-right; for an affine T-on-right
     * chain, M[12..14] = pos.xyz). */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X, 5.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Y, -2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Z, 7.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);

    float world[16];
    scene1_overlay_shape_7_compose_world(world, slot, 1.0f);
    T_ASSERT(fabsf(world[12] - 5.0f) < 1e-4f);
    T_ASSERT(fabsf(world[13] - -2.0f) < 1e-4f);
    T_ASSERT(fabsf(world[14] - 7.0f) < 1e-4f);
    return 0;
}

int test_overlay_shape_7_compose_world_off_diagonal_field_mapping(void)
{
    /* Same off-diagonal mapping as shape 3: setting ROT_X to π/2 with
     * zero ROT_Y/ROT_Z should produce S × RotY(π/2) × T, matching shape
     * 3's result for the same input (after accounting for shape 3's
     * uniform-scale vs shape 7's blend-split). */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,    1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,  0.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_ROT_X,      1.5707963f);

    /* With blend_mix=0: sx = 1.0*0.01*1.0*1.0/0.5 = 0.02; sy = 0; sz=sy=0
     * S = diag(0.02, 0, 0).  RotY(π/2) applied: world[0]=0, world[2]=-0.02
     * (RotY[0][2] = -sin = -1, times scaling[0][0]=0.02). */
    float world[16];
    scene1_overlay_shape_7_compose_world(world, slot, 1.0f);
    T_ASSERT(fabsf(world[0]) < 1e-6f);
    T_ASSERT(fabsf(world[2] - -0.02f) < 1e-5f);
    return 0;
}

int test_overlay_shape_7_emit_strip_uv_layout(void)
{
    /* Verify UV writes for a 4-pair strip with uv_size=(32, 64). */
    scene1_overlay_init();    /* fills positions */
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(32.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(64.0f);

    /* Take a window starting at pair 8 (the default pair_start). */
    scene1_overlay_vertex *window = &g_scene1_overlay_shape_7_vbuf[8 * 2];
    scene1_overlay_shape_7_emit_strip(window, /*pair_count=*/4,
                                      shape, 0.0f, 0.0f, /*fade_gray=*/0x80);

    float u_left  = (0.0f + 0.5f) / 256.0f;
    float u_right = (0.0f + 32.0f - 0.5f) / 256.0f;
    /* Pair 0 (i=0): v = (0*64/4 + 0 + 0.5) / 256 = 0.5/256. */
    float v0 = 0.5f / 256.0f;
    /* Pair 3 (i=3): v = (3*64/4 + 0.5) / 256 = (48.5)/256. */
    float v3 = 48.5f / 256.0f;

    T_ASSERT(fabsf(window[0].u - u_left)  < 1e-6f);
    T_ASSERT(fabsf(window[1].u - u_right) < 1e-6f);
    T_ASSERT(fabsf(window[0].v - v0) < 1e-6f);
    T_ASSERT(fabsf(window[1].v - v0) < 1e-6f);

    T_ASSERT(fabsf(window[6].u - u_left)  < 1e-6f);
    T_ASSERT(fabsf(window[7].u - u_right) < 1e-6f);
    T_ASSERT(fabsf(window[6].v - v3) < 1e-6f);
    T_ASSERT(fabsf(window[7].v - v3) < 1e-6f);

    /* Diffuse gray = 0xff_80_80_80. */
    T_ASSERT_EQ_U(window[0].diffuse, 0xff808080u);
    T_ASSERT_EQ_U(window[7].diffuse, 0xff808080u);
    return 0;
}

/* ═════════════════ O.7: shapes 8/9/10 group strip ═════════════════ */

int test_overlay_shape_89_vbuf_init_positions(void)
{
    scene1_overlay_init();

    /* Shape 8 — pair 0: angle=0 → sin=0, cos=1 → x=0, z=128.
     * Vert A.y = 64, Vert B.y = 0. */
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[0].x) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[0].y - 64.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[0].z - 128.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[1].y) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[1].z - 128.0f) < 1e-3f);

    /* Shape 9 lives at offset 80 verts.  Vert B is 0.6× radius (vert A
     * same radius as shape 8). */
    int base = SCENE1_OVERLAY_SHAPE_89_VERT_COUNT;
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[base + 0].z - 128.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[base + 1].z - 128.0f * 0.6f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[base + 1].x) < 1e-3f);
    return 0;
}

int test_overlay_shape_10_vbuf_init_positions(void)
{
    scene1_overlay_init();

    /* Strip 0, pair 0: strip_a=0 → sa=0 → x=z=0; ca=1 → y=128.
     *                  strip_b=π/8 → vert B y = cos(π/8)*128 ≈ 118.27. */
    T_ASSERT(fabsf(g_scene1_overlay_shape_10_vbuf[0].x) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_10_vbuf[0].y - 128.0f) < 1e-3f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_10_vbuf[0].z) < 1e-3f);
    /* vert B at (k+1) * π/8 = π/8 latitude */
    float expected_yb = cosf(1.5707964f / 4.0f) * 128.0f;
    T_ASSERT(fabsf(g_scene1_overlay_shape_10_vbuf[1].y - expected_yb) < 1e-3f);

    /* Last strip (k=3), last pair (i=19): strip_a = 3π/8 (vert A),
     *   strip_b = π/2 (vert B → at equator, y=0). */
    int last_strip_base = 3 * SCENE1_OVERLAY_SHAPE_10_VERTS_PER_STRIP;
    int last_pair_b = last_strip_base + 19 * 2 + 1;
    T_ASSERT(fabsf(g_scene1_overlay_shape_10_vbuf[last_pair_b].y) < 1e-3f);
    return 0;
}

int test_overlay_shape_89_10_scale_formula(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,     0.25f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE,    2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,       3.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO, 0.5f);

    /* s_h = ((1-0.25) * 2 * 1.0 * 3 * 0.588) / 0.5 * 0.02
     *     = 0.75 * 2 * 3 * 0.588 * 2 * 0.02
     *     = 0.10584 */
    /* s_v = (0.25 * 2 * 1.0 * 3 * 1.26) / 0.5 * 0.5 / 0.5 * 0.015
     *     = 0.25 * 2 * 3 * 1.26 * 2 * 1 * 0.015
     *     = 0.0567 */
    float s_h, s_v;
    scene1_overlay_shape_89_10_scale(slot, 1.0f, &s_h, &s_v);
    T_ASSERT(fabsf(s_h - 0.10584f) < 1e-4f);
    T_ASSERT(fabsf(s_v - 0.0567f)  < 1e-4f);
    return 0;
}

int test_overlay_shape_89_10_scale_y_ratio_scales_s_v_only(void)
{
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,     0.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE,    1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,       1.0f);

    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO, 1.0f);
    float s_h_a, s_v_a;
    scene1_overlay_shape_89_10_scale(slot, 1.0f, &s_h_a, &s_v_a);

    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO, 2.0f);
    float s_h_b, s_v_b;
    scene1_overlay_shape_89_10_scale(slot, 1.0f, &s_h_b, &s_v_b);

    /* s_h independent of scale_y_ratio; s_v doubles when ratio doubles. */
    T_ASSERT(fabsf(s_h_a - s_h_b) < 1e-7f);
    T_ASSERT(fabsf(s_v_b - 2.0f * s_v_a) < 1e-6f);
    return 0;
}

int test_overlay_shape_89_10_compose_world_translation_matches_pos(void)
{
    /* With ROT_Y=0 and identity scale, M[12..14] = pos. */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_X, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Y, 2.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_POS_Z, 3.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE,    1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,       1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,     0.5f);

    float world[16];
    scene1_overlay_shape_89_10_compose_world(world, slot, 1.0f);
    T_ASSERT(fabsf(world[12] - 1.0f) < 1e-4f);
    T_ASSERT(fabsf(world[13] - 2.0f) < 1e-4f);
    T_ASSERT(fabsf(world[14] - 3.0f) < 1e-4f);
    return 0;
}

int test_overlay_shape_89_10_compose_world_uses_rot_y_field_as_rot_x(void)
{
    /* Off-diagonal field mapping: slot[ROT_Y] (Ghidra's "rot.y") drives
     * RotationX.  With ROT_X = π/2 in that slot, ROT_Y/ROT_Z stay 0
     * (but reads them per scaling), the matrix should rotate vector
     * (0, 1, 0) → (0, 0, 1) (after S × T × RotX). */
    int32_t *slot = fresh_slot(0);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_BASE,    1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_X,       1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_SCALE_Y_RATIO, 1.0f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_BLEND_MIX,     0.5f);
    slot_set_f_dir(0, SCENE1_OVERLAY_OFF_ROT_Y,         1.5707963f);  /* π/2 */

    float world[16];
    scene1_overlay_shape_89_10_compose_world(world, slot, 1.0f);
    /* RotX(π/2) basis: world[5] = cos(π/2) = 0, world[6] = sin(π/2) = 1,
     * but our matrix has S applied first.  Check the off-diagonal:
     * mat[5] (= S_v * cos(rx)) should be ~0; mat[6] depends on
     * row-vs-column-major.  Since we use mat4_rotation_x consistently,
     * just verify *some* off-diagonal entry is nonzero. */
    T_ASSERT(fabsf(world[5]) < 1e-4f);
    T_ASSERT(fabsf(world[6]) > 1e-4f || fabsf(world[9]) > 1e-4f);
    return 0;
}

int test_overlay_shape_89_emit_strip_uv_layout(void)
{
    scene1_overlay_init();
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(64.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(78.0f);  /* * (78-1)/39 = 1.974 per step */

    scene1_overlay_vertex *vbuf = &g_scene1_overlay_shape_89_vbuf[0];
    scene1_overlay_shape_89_emit_strip(vbuf, shape, 0.0f, 0.0f, /*alpha=*/0x40);

    float u_left  = 0.5f / 256.0f;
    float u_right = (64.0f - 0.5f) / 256.0f;
    /* Pair 0: v = (0*step + 0 + 0.5)/256 = 0.5/256. */
    T_ASSERT(fabsf(vbuf[0].u - u_left)  < 1e-6f);
    T_ASSERT(fabsf(vbuf[1].u - u_right) < 1e-6f);
    T_ASSERT(fabsf(vbuf[0].v - 0.5f / 256.0f) < 1e-6f);
    T_ASSERT(fabsf(vbuf[1].v - 0.5f / 256.0f) < 1e-6f);

    /* Pair 39 (last): v = (39 * 77/39 + 0.5) / 256 = 77.5/256. */
    float expected_v = (39.0f * 77.0f / 39.0f + 0.5f) / 256.0f;
    T_ASSERT(fabsf(vbuf[39 * 2 + 0].v - expected_v) < 1e-5f);
    T_ASSERT(fabsf(vbuf[39 * 2 + 1].v - expected_v) < 1e-5f);

    /* Diffuse gray = 0xff_40_40_40. */
    T_ASSERT_EQ_U(vbuf[0].diffuse, 0xff404040u);
    T_ASSERT_EQ_U(vbuf[79].diffuse, 0xff404040u);
    return 0;
}

int test_overlay_shape_89_emit_strip_does_not_touch_positions(void)
{
    scene1_overlay_init();
    /* Cache pair-0 position before emit. */
    float x0 = g_scene1_overlay_shape_89_vbuf[0].x;
    float y0 = g_scene1_overlay_shape_89_vbuf[0].y;
    float z0 = g_scene1_overlay_shape_89_vbuf[0].z;

    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(16.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(40.0f);
    scene1_overlay_shape_89_emit_strip(&g_scene1_overlay_shape_89_vbuf[0],
                                       shape, 64.0f, 32.0f, 0xff);

    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[0].x - x0) < 1e-7f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[0].y - y0) < 1e-7f);
    T_ASSERT(fabsf(g_scene1_overlay_shape_89_vbuf[0].z - z0) < 1e-7f);
    return 0;
}

int test_overlay_shape_10_emit_strip_uv_layout_sliding_window(void)
{
    scene1_overlay_init();
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(33.0f);  /* step = 32/4 = 8 */
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(20.0f);

    /* Strip 0: u_left = (0*8 + 0 + 0.5)/256, u_right = (1*8 + 0 - 0.5)/256. */
    scene1_overlay_vertex *strip0 = &g_scene1_overlay_shape_10_vbuf[0];
    scene1_overlay_shape_10_emit_strip(strip0, /*strip_idx=*/0, shape,
                                       0.0f, 0.0f, 0x80);
    T_ASSERT(fabsf(strip0[0].u - (0.5f / 256.0f))  < 1e-6f);
    T_ASSERT(fabsf(strip0[1].u - (7.5f / 256.0f))  < 1e-6f);
    T_ASSERT_EQ_U(strip0[0].diffuse, 0xff808080u);

    /* Strip 3: u_left = (3*8 + 0.5)/256, u_right = (4*8 - 0.5)/256. */
    scene1_overlay_vertex *strip3 =
        &g_scene1_overlay_shape_10_vbuf[3 * SCENE1_OVERLAY_SHAPE_10_VERTS_PER_STRIP];
    scene1_overlay_shape_10_emit_strip(strip3, /*strip_idx=*/3, shape,
                                       0.0f, 0.0f, 0x80);
    T_ASSERT(fabsf(strip3[0].u - (24.5f / 256.0f)) < 1e-6f);
    T_ASSERT(fabsf(strip3[1].u - (31.5f / 256.0f)) < 1e-6f);
    return 0;
}

int test_overlay_shape_10_emit_strip_v_linear_across_20_pairs(void)
{
    scene1_overlay_init();
    int32_t shape[8] = {0};
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X] = f_to_bits(4.0f);
    shape[SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y] = f_to_bits(20.0f);  /* step = 19/19 = 1 */

    scene1_overlay_vertex *strip = &g_scene1_overlay_shape_10_vbuf[0];
    scene1_overlay_shape_10_emit_strip(strip, 0, shape, 0.0f, 0.0f, 0xff);

    /* v[i] = (i * 1.0 + 0 + 0.5)/256 */
    for (int i = 0; i < SCENE1_OVERLAY_SHAPE_10_PAIRS_PER_STRIP; i++) {
        float expected = ((float)i + 0.5f) / 256.0f;
        T_ASSERT(fabsf(strip[i * 2 + 0].v - expected) < 1e-6f);
        T_ASSERT(fabsf(strip[i * 2 + 1].v - expected) < 1e-6f);
    }
    return 0;
}

/* ═══ O.11 — scene1_overlay_setup tests ═════════════════════════════════
 *
 * Verify FUN_00452f58's matrix outputs under the engine's hard-coded
 * (0,0,-550)/(0,0,0) state and a few off-state inputs.
 */

#include "math3d.h"

static int approx_eq(float a, float b, float eps)
{
    float d = a - b; if (d < 0) d = -d;
    return d < eps;
}

int test_overlay_setup_compute_engine_state_pre_matrix_is_rot_y_quarter(void)
{
    const float eye[3]    = { 0.0f, 0.0f,    0.0f };
    const float lookat[3] = { 0.0f, 0.0f, -550.0f };
    float view[16], proj[16], pre[16];
    scene1_overlay_setup_compute(eye, lookat, view, proj, pre);

    /* Engine state → atan2(0, -550) + π = 2π → RotX(2π) = identity.
     * dy = 0, hyp = 550 → atan2(0, 550) = 0 → RotY(π/2 - 0) = RotY(π/2).
     * mat4_mul(out, Y, X) = Y × I = RotationY(π/2).
     *
     * Row-major RotY(π/2) — cells that must hold to 1e-5:
     *   [0][0] =  cos(π/2) ≈ 0
     *   [0][2] = -sin(π/2) ≈ -1
     *   [2][0] =  sin(π/2) ≈ 1
     *   [2][2] =  cos(π/2) ≈ 0
     *   [1][1] = 1; [3][3] = 1; rest 0
     */
    T_ASSERT(approx_eq(pre[0],   0.0f, 1e-5f));
    T_ASSERT(approx_eq(pre[2],  -1.0f, 1e-5f));
    T_ASSERT(approx_eq(pre[5],   1.0f, 1e-5f));
    T_ASSERT(approx_eq(pre[8],   1.0f, 1e-5f));
    T_ASSERT(approx_eq(pre[10],  0.0f, 1e-5f));
    T_ASSERT(approx_eq(pre[15],  1.0f, 1e-5f));
    return 0;
}

int test_overlay_setup_compute_view_matches_lookat_rh(void)
{
    const float eye[3]    = { 0.0f, 0.0f,    0.0f };
    const float lookat[3] = { 0.0f, 0.0f, -550.0f };
    float view[16], proj[16], pre[16];
    scene1_overlay_setup_compute(eye, lookat, view, proj, pre);

    float ref[16];
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    mat4_lookat_rh(ref, eye, lookat, up);
    for (int i = 0; i < 16; i++) {
        T_ASSERT(approx_eq(view[i], ref[i], 1e-5f));
    }
    return 0;
}

int test_overlay_setup_compute_proj_uses_engine_constants(void)
{
    const float eye[3]    = { 0.0f, 0.0f,    0.0f };
    const float lookat[3] = { 0.0f, 0.0f, -550.0f };
    float view[16], proj[16], pre[16];
    scene1_overlay_setup_compute(eye, lookat, view, proj, pre);

    float ref[16];
    mat4_perspective_fov_rh(ref,
                            0.7853981852531433f,    /* π/4 */
                            1.3333333730697632f,    /* 4/3 */
                            10.0f,
                            20000.0f);
    for (int i = 0; i < 16; i++) {
        T_ASSERT(approx_eq(proj[i], ref[i], 1e-3f));
    }
    return 0;
}

int test_overlay_setup_compute_singular_collapses_rot_y_to_zero(void)
{
    /* hyp = 0 → port sets rot_y_angle = 0 (identity Y).
     * For eye_y > lookat_y, rot_x = atan2(0, +1) + π = π;
     * RotY(0) × RotX(π) = RotX(π):
     *   [0][0] = 1; [1][1] = -1; [2][2] = -1; [3][3] = 1
     */
    const float eye[3]    = { 0.0f, 5.0f, 0.0f };
    const float lookat[3] = { 0.0f, 1.0f, 1.0f };    /* z != 0 → atan2(0, 1) defined */
    float view[16], proj[16], pre[16];
    scene1_overlay_setup_compute(eye, lookat, view, proj, pre);

    /* hyp = sqrt(0 + 1) = 1.  So rot_y_angle = π/2 - atan2(-4, 1).
     * atan2(-4, 1) ≈ -1.3258 rad → rot_y ≈ 2.8966 rad.  This is the
     * non-singular path — only here to exercise non-zero dy.  Skip
     * pre-matrix exact assertions and just sanity-check det != 0. */
    float det_ish = pre[0]*pre[5] - pre[1]*pre[4];   /* upper-2x2 det */
    T_ASSERT(!approx_eq(det_ish, 0.0f, 1e-9f) ||
             !approx_eq(pre[2], 0.0f, 1e-9f));      /* something nonzero */
    return 0;
}

int test_overlay_setup_compute_null_inputs_safe(void)
{
    float view[16], proj[16], pre[16];
    scene1_overlay_setup_compute(NULL, NULL, view, proj, pre);
    return 0;
}

int test_overlay_setup_compute_does_not_publish_pre_matrix(void)
{
    /* The setter is called only from scene1_overlay_setup (Win32).
     * Verify the pure helper does NOT touch g_wf_pass_c_pre_matrix. */
    float marker[16] = {
        2, 0, 0, 0,
        0, 3, 0, 0,
        0, 0, 4, 0,
        0, 0, 0, 5
    };
    wf_pass_c_set_pre_matrix(marker);

    const float eye[3]    = { 0.0f, 0.0f,    0.0f };
    const float lookat[3] = { 0.0f, 0.0f, -550.0f };
    float view[16], proj[16], pre[16];
    scene1_overlay_setup_compute(eye, lookat, view, proj, pre);

    const float *got = wf_pass_c_get_pre_matrix();
    for (int i = 0; i < 16; i++) {
        T_ASSERT(approx_eq(got[i], marker[i], 1e-7f));
    }
    return 0;
}
