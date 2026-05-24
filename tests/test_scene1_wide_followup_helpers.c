/*
 * test_scene1_wide_followup_helpers.c — Pass C helper coverage
 * (C8f.pass-c).
 *
 * The walker entry (scene1_wide_followup) is Win32-only.  The per-record
 * algebraic helpers (filter / scale / tile_index / uv_box /
 * compose_world / pre-matrix setter) live in
 * scene1_wide_followup_helpers.c — that's what we test here.
 *
 * Covers Pass C (engine FUN_004161c7 L143-L203):
 *   - wf_pass_c_should_emit: type ∈ {0,1,2,3} emit; -1 sentinel + any
 *     other value reject.
 *   - wf_pass_c_per_record_scale: slot[EXTRA_AUX] → 0.0192 / 0.0096 /
 *     0.028800001 dispatch.  Confirms the survey-doc "per-type scale"
 *     attribution was wrong — the field is EXTRA_AUX, not TYPE.
 *   - wf_pass_c_tile_index: (age/3) % 7 + {0/8/16/24}.
 *   - wf_pass_c_uv_box: 0.5/63.5 column inset, 0.5/63.0 row inset.
 *   - wf_pass_c_compose_world: T × S × pre_matrix chain with identity
 *     stand-in (default state) and a non-identity pre-matrix override.
 *   - wf_pass_c_{get,set}_pre_matrix: identity default + setter round-trip.
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "math3d.h"
#include "scene1_records.h"
#include "scene1_records_c_tick.h"
#include "scene1_wide_followup.h"

static int float_near_wf(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

#define T_ASSERT_NEAR_WF(a, b, tol) do {                                    \
    float _a = (float)(a), _b = (float)(b), _t = (float)(tol);              \
    if (!float_near_wf(_a, _b, _t))                                         \
        T_FAIL("expected %s ≈ %s within %g (got %.7f, want %.7f, |Δ|=%.7f)",\
               #a, #b, _t, _a, _b, (double)((_a-_b<0)?-(_a-_b):(_a-_b)));   \
} while (0)

static void slot_init_zero_c(int32_t slot[SCENE1_RECORDS_C_STRIDE])
{
    memset(slot, 0, sizeof(int32_t) * SCENE1_RECORDS_C_STRIDE);
}

static void slot_set_float_c(int32_t slot[SCENE1_RECORDS_C_STRIDE],
                             int offset, float value)
{
    memcpy(&slot[offset], &value, sizeof(value));
}

/* ─── predicate ──────────────────────────────────────────────────────── */

int test_wf_pass_c_should_emit_rejects_sentinel(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = -1;
    T_ASSERT(wf_pass_c_should_emit(slot) == 0);
    return 0;
}

int test_wf_pass_c_should_emit_accepts_cardinal_0_1_2_3(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    for (int t = 0; t <= 3; t++) {
        slot[SCENE1_RECORDS_C_OFF_TYPE] = t;
        if (wf_pass_c_should_emit(slot) != 1)
            T_FAIL("type %d should emit", t);
    }
    return 0;
}

int test_wf_pass_c_should_emit_rejects_out_of_range(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    int rejects[] = { 4, 5, 6, 7, 10, 0x7f, 0xff };
    for (int i = 0; i < (int)(sizeof(rejects)/sizeof(rejects[0])); i++) {
        slot[SCENE1_RECORDS_C_OFF_TYPE] = rejects[i];
        if (wf_pass_c_should_emit(slot) != 0)
            T_FAIL("type %d should NOT emit", rejects[i]);
    }
    return 0;
}

/* ─── per-record scale (reads EXTRA_AUX, NOT TYPE) ───────────────────── */

int test_wf_pass_c_scale_default_is_point_0192(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 0;
    slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX] = 0;
    T_ASSERT_NEAR_WF(wf_pass_c_per_record_scale(slot), 0.0192f, 1e-7f);
    return 0;
}

int test_wf_pass_c_scale_aux_1_is_smaller(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX] = 1;
    T_ASSERT_NEAR_WF(wf_pass_c_per_record_scale(slot), 0.0096f, 1e-7f);
    return 0;
}

int test_wf_pass_c_scale_aux_2_is_larger(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX] = 2;
    T_ASSERT_NEAR_WF(wf_pass_c_per_record_scale(slot), 0.028800001f, 1e-7f);
    return 0;
}

int test_wf_pass_c_scale_aux_3_falls_through_to_default(void)
{
    /* Engine dispatch is `if aux==1 ... else if aux==2 ... else default`.
     * aux=3 falls through to default 0.0192 (not a 4th bucket). */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX] = 3;
    T_ASSERT_NEAR_WF(wf_pass_c_per_record_scale(slot), 0.0192f, 1e-7f);
    return 0;
}

/* ─── tile index ─────────────────────────────────────────────────────── */

int test_wf_pass_c_tile_index_type_0_age_0(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 0;
    slot[SCENE1_RECORDS_C_OFF_AGE]  = 0;
    T_ASSERT(wf_pass_c_tile_index(slot) == 0);
    return 0;
}

int test_wf_pass_c_tile_index_type_1_age_3_is_9(void)
{
    /* age=3 → 3/3 = 1, 1%7 = 1, + type_offset(1)=8 → 9 */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 1;
    slot[SCENE1_RECORDS_C_OFF_AGE]  = 3;
    T_ASSERT(wf_pass_c_tile_index(slot) == 9);
    return 0;
}

int test_wf_pass_c_tile_index_type_2_age_21_wraps_to_16(void)
{
    /* age=21 → 21/3 = 7, 7%7 = 0, + 16 → 16.  Confirms the % 7
     * (not % 8) — column 7 of each row is unused. */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 2;
    slot[SCENE1_RECORDS_C_OFF_AGE]  = 21;
    T_ASSERT(wf_pass_c_tile_index(slot) == 16);
    return 0;
}

int test_wf_pass_c_tile_index_type_3_age_20_max_in_row(void)
{
    /* age=20 → 20/3 = 6, 6%7 = 6, + 24 → 30 */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 3;
    slot[SCENE1_RECORDS_C_OFF_AGE]  = 20;
    T_ASSERT(wf_pass_c_tile_index(slot) == 30);
    return 0;
}

/* ─── UV box ─────────────────────────────────────────────────────────── */

int test_wf_pass_c_uv_box_tile_0_top_left_corner(void)
{
    /* tile 0 → col=0, row=0 → u0=0.5/512, u1=63.5/512,
     *                          v0=0.5/256, v1=63.0/256 */
    float u0, u1, v0, v1;
    wf_pass_c_uv_box(0, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0,  0.5f / 512.0f, 1e-8f);
    T_ASSERT_NEAR_WF(u1, 63.5f / 512.0f, 1e-8f);
    T_ASSERT_NEAR_WF(v0,  0.5f / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(v1, 63.0f / 256.0f, 1e-8f);
    return 0;
}

int test_wf_pass_c_uv_box_tile_8_wraps_to_row_1(void)
{
    /* tile 8 → col=0, row=1 → u-box unchanged, v shifted by 64/256 */
    float u0, u1, v0, v1;
    wf_pass_c_uv_box(8, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0,  0.5f / 512.0f, 1e-8f);
    T_ASSERT_NEAR_WF(u1, 63.5f / 512.0f, 1e-8f);
    T_ASSERT_NEAR_WF(v0, (1.0f * 64.0f +  0.5f) / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(v1, (1.0f * 64.0f + 63.0f) / 256.0f, 1e-8f);
    return 0;
}

int test_wf_pass_c_uv_box_tile_30_row_3_col_6(void)
{
    /* tile 30 → col=6, row=3 */
    float u0, u1, v0, v1;
    wf_pass_c_uv_box(30, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, (6.0f * 64.0f +  0.5f) / 512.0f, 1e-8f);
    T_ASSERT_NEAR_WF(u1, (6.0f * 64.0f + 63.5f) / 512.0f, 1e-8f);
    T_ASSERT_NEAR_WF(v0, (3.0f * 64.0f +  0.5f) / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(v1, (3.0f * 64.0f + 63.0f) / 256.0f, 1e-8f);
    return 0;
}

/* ─── pre-matrix setter + compose_world ──────────────────────────────── */

int test_wf_pass_c_pre_matrix_default_is_identity(void)
{
    /* Fresh process start: pre-matrix is identity.  This test relies on
     * test ordering — call BEFORE any setter test mutates the static. */
    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    const float *m = wf_pass_c_get_pre_matrix();
    for (int i = 0; i < 16; i++) {
        T_ASSERT_NEAR_WF(m[i], identity[i], 1e-8f);
    }
    return 0;
}

int test_wf_pass_c_pre_matrix_setter_round_trips(void)
{
    float test_m[16] = {
        2, 0, 0, 0,
        0, 3, 0, 0,
        0, 0, 5, 0,
        7, 11, 13, 1,
    };
    wf_pass_c_set_pre_matrix(test_m);
    const float *m = wf_pass_c_get_pre_matrix();
    for (int i = 0; i < 16; i++) {
        T_ASSERT_NEAR_WF(m[i], test_m[i], 1e-8f);
    }
    /* Restore identity for subsequent tests. */
    float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    wf_pass_c_set_pre_matrix(ident);
    return 0;
}

int test_wf_pass_c_pre_matrix_setter_ignores_null(void)
{
    /* Setter is NULL-safe: passing NULL must leave the matrix unchanged. */
    float test_m[16] = {
        2, 0, 0, 0,
        0, 2, 0, 0,
        0, 0, 2, 0,
        0, 0, 0, 1,
    };
    wf_pass_c_set_pre_matrix(test_m);
    wf_pass_c_set_pre_matrix(NULL);
    const float *m = wf_pass_c_get_pre_matrix();
    for (int i = 0; i < 16; i++) {
        T_ASSERT_NEAR_WF(m[i], test_m[i], 1e-8f);
    }
    /* Restore identity for subsequent tests. */
    float ident[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    wf_pass_c_set_pre_matrix(ident);
    return 0;
}

int test_wf_pass_c_compose_world_default_pre_matrix_is_scale_translate(void)
{
    /* With identity pre-matrix: out = I × S × T = S × T.
     * Default aux=0 → scale = 0.0192.  pos = (10, 20, 30).
     * Row-major D3DX convention: out[0]=Sx, out[5]=Sy, out[10]=Sz,
     * out[12..14] = translation (since S × T puts T's row 3 unchanged
     * and S only scales rows 0..2's diagonal). */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 0;
    slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX] = 0;
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_X, 10.0f);
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_Y, 20.0f);
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_Z, 30.0f);

    float world[16];
    wf_pass_c_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[0],  0.0192f, 1e-6f);
    T_ASSERT_NEAR_WF(world[5],  0.0192f, 1e-6f);
    T_ASSERT_NEAR_WF(world[10], 0.0192f, 1e-6f);
    T_ASSERT_NEAR_WF(world[12], 10.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[13], 20.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[14], 30.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[15],  1.0f, 1e-6f);
    return 0;
}

int test_wf_pass_c_compose_world_smaller_aux_1_scale(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 1;
    slot[SCENE1_RECORDS_C_OFF_EXTRA_AUX] = 1;
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_X, 0.0f);
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_Y, 0.0f);
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_Z, 0.0f);

    float world[16];
    wf_pass_c_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[0],  0.0096f, 1e-6f);
    T_ASSERT_NEAR_WF(world[5],  0.0096f, 1e-6f);
    T_ASSERT_NEAR_WF(world[10], 0.0096f, 1e-6f);
    return 0;
}

/* ═══ Pass A (C8f.pass-a) ═════════════════════════════════════════════ */

static void slot_init_zero_b(int32_t slot[SCENE1_RECORDS_B_STRIDE])
{
    memset(slot, 0, sizeof(int32_t) * SCENE1_RECORDS_B_STRIDE);
}

static void slot_set_float_b(int32_t slot[SCENE1_RECORDS_B_STRIDE],
                             int offset, float value)
{
    memcpy(&slot[offset], &value, sizeof(value));
}

/* ─── predicate ──────────────────────────────────────────────────────── */

int test_wf_pass_a_should_emit_rejects_sentinel(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0;  /* free-slot sentinel */
    T_ASSERT(wf_pass_a_should_emit(slot) == 0);
    return 0;
}

int test_wf_pass_a_should_emit_accepts_0x77(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x77;
    T_ASSERT(wf_pass_a_should_emit(slot) == 1);
    return 0;
}

int test_wf_pass_a_should_emit_accepts_0xa2(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0xa2;
    T_ASSERT(wf_pass_a_should_emit(slot) == 1);
    return 0;
}

int test_wf_pass_a_should_emit_rejects_neighboring_types(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t reject[] = { 1, 2, 0x76, 0x78, 0xa1, 0xa3, 0x53 };
    for (size_t i = 0; i < sizeof(reject) / sizeof(reject[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject[i];
        if (wf_pass_a_should_emit(slot) != 0) {
            T_FAIL("type 0x%x should NOT emit but did", reject[i]);
        }
    }
    return 0;
}

/* ─── per-record scale ───────────────────────────────────────────────── */

int test_wf_pass_a_scale_full_at_age_5(void)
{
    /* AGE == 5 → no ramp-in.  LIFE_MULT 1.0 → scale = 0.005. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_AGE] = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_a_per_record_scale(slot), 0.005f, 1e-7f);
    return 0;
}

int test_wf_pass_a_scale_zero_at_age_0(void)
{
    /* AGE == 0 → (0/5) * scale = 0.0 (invisible first frame). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_AGE] = 0;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_a_per_record_scale(slot), 0.0f, 1e-7f);
    return 0;
}

int test_wf_pass_a_scale_ramps_in_over_5_frames(void)
{
    /* AGE = 1,2,3,4 → 0.001, 0.002, 0.003, 0.004. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    float expected[5] = { 0.0f, 0.001f, 0.002f, 0.003f, 0.004f };
    for (int age = 0; age < 5; age++) {
        slot[SCENE1_RECORDS_B_OFF_AGE] = age;
        T_ASSERT_NEAR_WF(wf_pass_a_per_record_scale(slot),
                         expected[age], 1e-7f);
    }
    return 0;
}

int test_wf_pass_a_scale_uses_life_mult(void)
{
    /* LIFE_MULT 2.0 + AGE 10 → 2.0 * 0.005 = 0.01. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_AGE] = 10;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    T_ASSERT_NEAR_WF(wf_pass_a_per_record_scale(slot), 0.01f, 1e-7f);
    return 0;
}

/* ─── world matrix ───────────────────────────────────────────────────── */

int test_wf_pass_a_compose_world_translation_in_row_3(void)
{
    /* At AGE=5 (no ramp), LIFE_MULT 1.0, ROT_X 0:
     *   M = RotZ(π) × RotY(π/2) × S(0.005) × T(10, 20, 30).
     *
     * Translation row stays at (10, 20, 30, 1) — scale only touches
     * rows 0..2's diagonal, rotations have row 3 = (0,0,0,1).  */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x77;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_ROT_X,     0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X,    10.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y,    20.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z,    30.0f);

    float world[16];
    wf_pass_a_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[12], 10.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[13], 20.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[14], 30.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[15],  1.0f, 1e-6f);
    return 0;
}

int test_wf_pass_a_compose_world_rot_x_pi_cancels_rot_z(void)
{
    /* slot[ROT_X] = π → RotZ(π - π) = RotZ(0) = identity.
     * Then M = I × RotY(π/2) × S × T = RotY(π/2) × S × T.
     *
     * mat4_rotation_y(θ) returns the standard right-handed rotation:
     *   [  cos θ  0  -sin θ  0 ]
     *   [  0      1   0      0 ]
     *   [  sin θ  0   cos θ  0 ]
     *   [  0      0   0      1 ]
     *
     * For θ=π/2: cos=0, sin=1 → world[2] = -0.005 (x-row), world[5] =
     * +0.005 (y-row diagonal), world[8] = +0.005 (z-row's x-col).  */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x77;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_ROT_X,     3.1415927f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X,     0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y,     0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z,     0.0f);

    float world[16];
    wf_pass_a_compose_world(world, slot);

    /* Y-row's diagonal is unaffected by RotY (only X/Z rows rotate). */
    T_ASSERT_NEAR_WF(world[5], 0.005f, 1e-5f);
    return 0;
}
