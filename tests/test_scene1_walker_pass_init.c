/*
 * test_scene1_walker_pass_init.c — PII.3a coverage.
 *
 * Tests scene1_walker_phase2_compute against the asm-decoded matrix
 * chain at engine 0x457e48..0x457fff (FUN_00457714 setup phase 2).
 *
 * Per-mesh chain:
 *
 *   world = S(-0.2, 0.2, 0.2) × [optional flip×N] × RotY(rot_y) × T(pos)
 *
 * where [flip] = RotY(π) × T(2,0,0) applied 0, 1, or 2 times depending
 * on mesh_type / flag_hook / rot_y / pos_y gates.
 *
 * Reference matrices are built directly via the same math3d primitives
 * the helper uses, so we're checking the COMPOSITION + GATING logic
 * (which branches fire when) — not the underlying matrix math
 * (covered by test_math3d).
 */

#include "t.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "math3d.h"
#include "scene1_walker_pass_init.h"

#define K_PI         3.1415927f
#define K_HALF_PI    1.5707964f

static int float_near(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

#define T_ASSERT_MAT_NEAR(actual, expected, tol) do {                       \
    for (int _r = 0; _r < 16; _r++) {                                       \
        if (!float_near((actual)[_r], (expected)[_r], (tol)))               \
            T_FAIL("matrix mismatch at index %d: got %.6f want %.6f",       \
                   _r, (actual)[_r], (expected)[_r]);                       \
    }                                                                       \
} while (0)

/* Build the "no-flip" reference matrix:
 *   world = S(-0.2, 0.2, 0.2) × RotY(rot_y) × T(pos) */
static void ref_world_no_flip(float out[16],
                              float rot_y, float px, float py, float pz)
{
    float t[16], r[16], s[16];
    mat4_translation(t, px, py, pz);
    mat4_rotation_y(r, rot_y);
    mat4_mul(out, r, t);     /* world = RotY × T */
    mat4_scaling(s, -0.2f, 0.2f, 0.2f);
    mat4_mul(out, s, out);   /* world = S × world */
}

/* Apply the flip chain in place:  world = RotY(π) × T(2,0,0) × world. */
static void ref_apply_flip(float world[16])
{
    float t[16], ry[16];
    mat4_translation(t, 2.0f, 0.0f, 0.0f);
    mat4_mul(world, t, world);
    mat4_rotation_y(ry, K_PI);
    mat4_mul(world, ry, world);
}

/* Reference: like ref_world_no_flip but with N flip chains inserted
 * BETWEEN the RotY × T and the final S(-0.2, 0.2, 0.2) — matching the
 * engine's ordering. */
static void ref_world_with_flips(float out[16], int flip_count,
                                 float rot_y, float px, float py, float pz)
{
    float t[16], r[16], s[16];
    mat4_translation(t, px, py, pz);
    mat4_rotation_y(r, rot_y);
    mat4_mul(out, r, t);
    for (int i = 0; i < flip_count; i++) {
        ref_apply_flip(out);
    }
    mat4_scaling(s, -0.2f, 0.2f, 0.2f);
    mat4_mul(out, s, out);
}

/* ─── flag-hook fixtures ─────────────────────────────────────────── */

static int32_t g_test_flag_value = 0;

static int32_t flag_hook_constant(int mesh_index)
{
    (void)mesh_index;
    return g_test_flag_value;
}

static int32_t flag_hook_per_index_passing(int mesh_index)
{
    /* Returns a passing value (0x514c0 satisfies the mask) when
     * mesh_index is even; 0 otherwise. */
    return (mesh_index % 2 == 0) ? (int32_t)0x000514c0 : 0;
}

/* ─── tests ──────────────────────────────────────────────────────── */

int test_scene1_walker_pass_init_count_zero_skips(void)
{
    float out[16] = { 99.0f };  /* sentinel */
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 0;

    int n = scene1_walker_phase2_compute(out);
    T_ASSERT_EQ_I(n, 0);
    T_ASSERT_EQ_I(out[0], 99);  /* untouched */
    return 0;
}

int test_scene1_walker_pass_init_null_out_returns_zero(void)
{
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    int n = scene1_walker_phase2_compute(NULL);
    T_ASSERT_EQ_I(n, 0);
    return 0;
}

int test_scene1_walker_pass_init_clamps_to_max(void)
{
    float out[SCENE1_WALKER_PHASE2_MAX * 16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = SCENE1_WALKER_PHASE2_MAX + 50;
    int n = scene1_walker_phase2_compute(out);
    T_ASSERT_EQ_I(n, SCENE1_WALKER_PHASE2_MAX);
    return 0;
}

int test_scene1_walker_pass_init_default_path_no_flip(void)
{
    /* mesh_type != 4 → no flip applied regardless of flag hook. */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 7;
    g_scene1_walker_phase2_rot_y[0]     = 0.3f;
    g_scene1_walker_phase2_pos_x[0]     = 10.0f;
    g_scene1_walker_phase2_pos_y[0]     = 2.0f;
    g_scene1_walker_phase2_pos_z[0]     = -5.0f;
    g_test_flag_value = (int32_t)0x000514c0;
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    int n = scene1_walker_phase2_compute(out);
    T_ASSERT_EQ_I(n, 1);
    ref_world_no_flip(ref, 0.3f, 10.0f, 2.0f, -5.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_flag_zero_no_flip(void)
{
    /* mesh_type == 4 but flag hook returns 0 → gate closed. */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = 0.0f;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 2.0f;
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = 0;
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_no_flip(ref, 0.0f, 1.0f, 2.0f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_flag_mask_fails_no_flip(void)
{
    /* mesh_type == 4 + flag != 0 but bit pattern doesn't match
     * 0x514c0 → gate closed. */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = 0.0f;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 2.0f;
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = (int32_t)0x12345678;  /* not in [0x514c0, 0x514ff] */
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_no_flip(ref, 0.0f, 1.0f, 2.0f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_rot_zero_one_flip(void)
{
    /* mesh_type==4 + gate open + rot==0 → flip applied once. */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = 0.0f;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 10.0f;   /* pos_y > 5 — but the
                                                    * rot==0 branch SKIPS
                                                    * the > 5 check (jmp
                                                    * 0x457fc5). */
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = (int32_t)0x000514c0;
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_with_flips(ref, /*flip_count=*/1, 0.0f, 1.0f, 10.0f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_rot_half_pi_pos_y_small_one_flip(void)
{
    /* rot==π/2 + pos_y ≤ 5 → one flip (the π/2 branch's own). */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = K_HALF_PI;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 5.0f;    /* == 5.0 fails `> 5.0`
                                                    * (asm uses jbe to skip
                                                    * the second flip). */
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = (int32_t)0x000514c0;
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_with_flips(ref, /*flip_count=*/1, K_HALF_PI, 1.0f, 5.0f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_rot_half_pi_pos_y_large_two_flips(void)
{
    /* rot==π/2 + pos_y > 5 → TWO flips (π/2 branch + fall-through). */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = K_HALF_PI;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 5.5f;
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = (int32_t)0x000514ff;  /* still passes the mask */
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_with_flips(ref, /*flip_count=*/2, K_HALF_PI, 1.0f, 5.5f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_other_rot_pos_y_large_one_flip(void)
{
    /* rot != 0, rot != π/2, pos_y > 5 → one flip (size-only branch). */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = 0.5f;   /* arbitrary non-special */
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 6.0f;
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = (int32_t)0x000514c1;
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_with_flips(ref, /*flip_count=*/1, 0.5f, 1.0f, 6.0f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_type_4_other_rot_pos_y_small_no_flip(void)
{
    /* rot != 0, rot != π/2, pos_y ≤ 5 → no flip. */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = 0.5f;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 4.99f;
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    g_test_flag_value = (int32_t)0x000514c0;
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);

    scene1_walker_phase2_compute(out);
    ref_world_no_flip(ref, 0.5f, 1.0f, 4.99f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_multi_mesh_per_index_hook(void)
{
    /* 4 meshes, all mesh_type==4, flag_hook returns passing for even
     * indices only.  Even idx → flip applied (rot==0 branch); odd idx
     * → no flip.  Verifies that hook receives the iter index and the
     * gate decision is per-mesh. */
    float out[4 * 16];
    float ref0[16], ref1[16], ref2[16], ref3[16];

    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 4;
    for (int i = 0; i < 4; i++) {
        g_scene1_walker_phase2_mesh_type[i] = 4;
        g_scene1_walker_phase2_rot_y[i]     = 0.0f;
        g_scene1_walker_phase2_pos_x[i]     = (float)i + 1.0f;
        g_scene1_walker_phase2_pos_y[i]     = 1.0f;
        g_scene1_walker_phase2_pos_z[i]     = 0.0f;
    }
    scene1_walker_phase2_set_flag_hook(flag_hook_per_index_passing);

    int n = scene1_walker_phase2_compute(out);
    T_ASSERT_EQ_I(n, 4);
    ref_world_with_flips(ref0, 1, 0.0f, 1.0f, 1.0f, 0.0f);  /* even idx */
    ref_world_no_flip   (ref1,    0.0f, 2.0f, 1.0f, 0.0f);  /* odd idx */
    ref_world_with_flips(ref2, 1, 0.0f, 3.0f, 1.0f, 0.0f);  /* even */
    ref_world_no_flip   (ref3,    0.0f, 4.0f, 1.0f, 0.0f);  /* odd */
    T_ASSERT_MAT_NEAR(out +  0, ref0, 1e-6f);
    T_ASSERT_MAT_NEAR(out + 16, ref1, 1e-6f);
    T_ASSERT_MAT_NEAR(out + 32, ref2, 1e-6f);
    T_ASSERT_MAT_NEAR(out + 48, ref3, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_flag_hook_null_treated_as_zero(void)
{
    /* No hook installed: mesh_type==4 case sees flag==0 and skips
     * the flip chain. */
    float out[16];
    float ref[16];
    scene1_walker_phase2_reset();
    g_scene1_walker_phase2_count = 1;
    g_scene1_walker_phase2_mesh_type[0] = 4;
    g_scene1_walker_phase2_rot_y[0]     = 0.0f;
    g_scene1_walker_phase2_pos_x[0]     = 1.0f;
    g_scene1_walker_phase2_pos_y[0]     = 6.0f;
    g_scene1_walker_phase2_pos_z[0]     = 3.0f;
    /* no scene1_walker_phase2_set_flag_hook(...) — defaults NULL */

    scene1_walker_phase2_compute(out);
    ref_world_no_flip(ref, 0.0f, 1.0f, 6.0f, 3.0f);
    T_ASSERT_MAT_NEAR(out, ref, 1e-6f);
    return 0;
}

int test_scene1_walker_pass_init_hook_getter_round_trip(void)
{
    scene1_walker_phase2_reset();
    T_ASSERT(scene1_walker_phase2_get_flag_hook() == NULL);
    scene1_walker_phase2_set_flag_hook(flag_hook_constant);
    T_ASSERT(scene1_walker_phase2_get_flag_hook() == flag_hook_constant);
    scene1_walker_phase2_set_flag_hook(NULL);
    T_ASSERT(scene1_walker_phase2_get_flag_hook() == NULL);
    return 0;
}

/* ═════════════════ PII.3b — outer loop + draw loop B ════════════════ */

/* ─── classify_slot — per-cache-slot flag dispatch ──────────────────── */

int test_scene1_walker_classify_slot_all_zero_param0_default(void)
{
    /* All 6 flags 0, param=0 → DEFAULT (sprite from cache slot). */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 0, 0, 0),
                  SCENE1_WALKER_SLOT_DEFAULT);
    return 0;
}

int test_scene1_walker_classify_slot_all_zero_param1_skips(void)
{
    /* All 6 flags 0, param=1 → SKIP (ext_tga arm wants ext_tga != 0). */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 0, 0, 1),
                  SCENE1_WALKER_SLOT_SKIP);
    return 0;
}

int test_scene1_walker_classify_slot_water_param2_water(void)
{
    /* water=1, param=2 → WATER (animated overlay arm). */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(1, 0, 0, 0, 0, 0, 2),
                  SCENE1_WALKER_SLOT_WATER);
    return 0;
}

int test_scene1_walker_classify_slot_water_other_params_skip(void)
{
    /* water=1, but param != 2 → SKIP regardless of other flags. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(1, 0, 0, 0, 0, 0, 0),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(1, 1, 1, 1, 1, 1, 1),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(1, 0, 0, 0, 0, 0, 3),
                  SCENE1_WALKER_SLOT_SKIP);
    return 0;
}

int test_scene1_walker_classify_slot_kabe_param0_kabe(void)
{
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 1, 0, 0, 0, 0, 0),
                  SCENE1_WALKER_SLOT_KABE);
    return 0;
}

int test_scene1_walker_classify_slot_kabe_other_params_skip(void)
{
    /* kabe != 0 but param != 0 → SKIP (per engine dispatch). */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 1, 0, 0, 0, 0, 1),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 1, 0, 0, 0, 0, 2),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 1, 0, 0, 0, 0, 3),
                  SCENE1_WALKER_SLOT_SKIP);
    return 0;
}

int test_scene1_walker_classify_slot_yuka_param0_yuka(void)
{
    /* kabe==0, yuka!=0, param=0 → YUKA. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 1, 0, 0, 0, 0),
                  SCENE1_WALKER_SLOT_YUKA);
    return 0;
}

int test_scene1_walker_classify_slot_jutan_param0_jutan(void)
{
    /* kabe==0, yuka==0, shop_jutan!=0, param=0 → JUTAN. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 1, 0, 0, 0),
                  SCENE1_WALKER_SLOT_JUTAN);
    return 0;
}

int test_scene1_walker_classify_slot_ext_tga_param1(void)
{
    /* All upper flags 0, ext_tga != 0, param == 1 → EXT_TGA. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 1, 0, 1),
                  SCENE1_WALKER_SLOT_EXT_TGA);
    /* param != 1 with ext_tga set → SKIP. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 1, 0, 0),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 1, 0, 2),
                  SCENE1_WALKER_SLOT_SKIP);
    return 0;
}

int test_scene1_walker_classify_slot_hikari_param3(void)
{
    /* All upper flags 0, ext_tga==0, hikari != 0, param == 3 → HIKARI. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 0, 1, 3),
                  SCENE1_WALKER_SLOT_HIKARI);
    /* hikari != 0 + param != 3 → SKIP. */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 0, 1, 0),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 0, 1, 1),
                  SCENE1_WALKER_SLOT_SKIP);
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 0, 0, 1, 2),
                  SCENE1_WALKER_SLOT_SKIP);
    return 0;
}

int test_scene1_walker_classify_slot_cascade_priority(void)
{
    /* When multiple flags are set, the engine's nested-if cascade
     * gives water > kabe > yuka > shop_jutan > ext_tga > hikari priority.
     * Verify a few combinations. */
    /* water + kabe + yuka set: water wins (param=2) */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(1, 1, 1, 0, 0, 0, 2),
                  SCENE1_WALKER_SLOT_WATER);
    /* water=0, kabe + yuka set: kabe wins (param=0) */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 1, 1, 0, 0, 0, 0),
                  SCENE1_WALKER_SLOT_KABE);
    /* water=0, kabe=0, yuka + shop_jutan set: yuka wins (param=0) */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 1, 1, 0, 0, 0),
                  SCENE1_WALKER_SLOT_YUKA);
    /* water=0, kabe=0, yuka=0, shop_jutan + ext_tga + hikari: shop_jutan wins (param=0) */
    T_ASSERT_EQ_I(scene1_walker_classify_slot(0, 0, 0, 1, 1, 1, 0),
                  SCENE1_WALKER_SLOT_JUTAN);
    return 0;
}

/* ─── draw_b_mesh_index — per-mesh source selection ─────────────────── */

int test_scene1_walker_draw_b_mesh_index_shop_table_path(void)
{
    /* flag == 0 → shop_table path; index = mesh_type - 3 + selector*2. */
    int use_shop = 99;
    int idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/3, /*flag=*/0,
                                              /*selector=*/0, &use_shop);
    T_ASSERT_EQ_I(use_shop, 1);
    T_ASSERT_EQ_I(idx, 0);  /* 3 - 3 + 0*2 */

    idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/4, /*flag=*/0,
                                          /*selector=*/0, &use_shop);
    T_ASSERT_EQ_I(idx, 1);  /* 4 - 3 + 0 */

    /* selector=3 → slot = 4 - 3 + 6 = 7 */
    idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/4, /*flag=*/0,
                                          /*selector=*/3, &use_shop);
    T_ASSERT_EQ_I(idx, 7);
    return 0;
}

int test_scene1_walker_draw_b_mesh_index_shop_table_out_of_range(void)
{
    /* Index < 0 or >= 16 → -1. */
    int use_shop = 0;
    int idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/0, /*flag=*/0,
                                              /*selector=*/0, &use_shop);
    T_ASSERT_EQ_I(use_shop, 1);
    T_ASSERT_EQ_I(idx, -1);  /* 0 - 3 = -3 */

    idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/100, /*flag=*/0,
                                          /*selector=*/0, &use_shop);
    T_ASSERT_EQ_I(idx, -1);  /* 100 - 3 = 97, > 15 */
    return 0;
}

int test_scene1_walker_draw_b_mesh_index_wall_floor_path(void)
{
    /* flag != 0 → wall/floor path; index = mesh_type - 0x28a0 + (flag>>6)*2.
     * Wall/floor path doesn't bound-check (engine layout is huge);
     * returns the raw computed offset. */
    int use_shop = 99;
    /* flag = 0x80, shift=2; mesh_type = 0x28a0; offset = 0 + 2*2 = 4 */
    int idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/0x28a0,
                                              /*flag=*/0x80,
                                              /*selector=*/0, &use_shop);
    T_ASSERT_EQ_I(use_shop, 0);
    T_ASSERT_EQ_I(idx, 4);
    return 0;
}

int test_scene1_walker_draw_b_mesh_index_null_out_ok(void)
{
    /* NULL out param: still computes index, just doesn't store path tag. */
    int idx = scene1_walker_draw_b_mesh_index(/*mesh_type=*/5, /*flag=*/0,
                                              /*selector=*/0, NULL);
    T_ASSERT_EQ_I(idx, 2);
    return 0;
}

/* ─── PII.3b hook + state defaults via reset ───────────────────────── */

int test_scene1_walker_phase2b_reset_defaults(void)
{
    /* Set everything to non-default, then reset, then verify defaults. */
    g_scene1_walker_status_screen_open = 1;
    scene1_walker_set_kabe_texture_hook((scene1_walker_stage_texture_fn)(uintptr_t)0x1234);
    scene1_walker_set_yuka_texture_hook((scene1_walker_stage_texture_fn)(uintptr_t)0x5678);
    scene1_walker_set_jutan_texture_hook((scene1_walker_stage_texture_fn)(uintptr_t)0xabcd);
    scene1_walker_set_animated_texture_hook((scene1_walker_stage_texture_fn)(uintptr_t)0xdcba);

    scene1_walker_phase2_reset();

    T_ASSERT_EQ_I(g_scene1_walker_status_screen_open, 0);
    /* No hook getters for the stage-texture hooks; instead, the
     * post-reset state is observable through compute() being a no-op
     * (count == 0) and the count/flag defaults. */
    T_ASSERT_EQ_I(g_scene1_walker_phase2_count, 0);
    T_ASSERT(scene1_walker_phase2_get_flag_hook() == NULL);
    return 0;
}

/* ─── PII.3c — phase-1 (wall/floor/jutan) matrix builder ───────────── */

int test_scene1_walker_phase1_reset_defaults(void)
{
    g_scene1_walker_phase1_count = 5;
    g_scene1_walker_phase1_pos_x[0] = 9.0f;
    g_scene1_walker_phase1_mesh_index[0] = 7;
    scene1_walker_phase1_reset();
    T_ASSERT_EQ_I(g_scene1_walker_phase1_count, 0);
    T_ASSERT(g_scene1_walker_phase1_pos_x[0] == 0.0f);
    T_ASSERT_EQ_I(g_scene1_walker_phase1_mesh_index[0], 0);
    return 0;
}

int test_scene1_walker_phase1_compute_house_room_and_carpet(void)
{
    /* HOUSE: instance 0 = room at the origin, instance 1 = carpet at
     * (-2, 0, -1), both rot 0.  Matrix chain = S(-0.2,0.2,0.2) × RotY ×
     * T — identical to phase 2's no-flip path (ref_world_no_flip). */
    scene1_walker_phase1_reset();
    g_scene1_walker_phase1_count = 2;
    g_scene1_walker_phase1_pos_x[0] = 0.0f;
    g_scene1_walker_phase1_pos_y[0] = 0.0f;
    g_scene1_walker_phase1_pos_z[0] = 0.0f;
    g_scene1_walker_phase1_rot_y[0] = 0.0f;
    g_scene1_walker_phase1_pos_x[1] = -2.0f;
    g_scene1_walker_phase1_pos_y[1] =  0.0f;
    g_scene1_walker_phase1_pos_z[1] = -1.0f;
    g_scene1_walker_phase1_rot_y[1] =  0.0f;

    float out[2 * 16];
    int n = scene1_walker_phase1_compute(out);
    T_ASSERT_EQ_I(n, 2);

    float ref0[16], ref1[16];
    ref_world_no_flip(ref0, 0.0f, 0.0f, 0.0f, 0.0f);
    ref_world_no_flip(ref1, 0.0f, -2.0f, 0.0f, -1.0f);
    T_ASSERT_MAT_NEAR(&out[0],  ref0, 1e-5f);
    T_ASSERT_MAT_NEAR(&out[16], ref1, 1e-5f);
    return 0;
}

int test_scene1_walker_phase1_compute_count_zero_and_null(void)
{
    scene1_walker_phase1_reset();
    float out[16];
    T_ASSERT_EQ_I(scene1_walker_phase1_compute(out), 0);
    g_scene1_walker_phase1_count = 2;
    T_ASSERT_EQ_I(scene1_walker_phase1_compute(NULL), 0);
    return 0;
}
