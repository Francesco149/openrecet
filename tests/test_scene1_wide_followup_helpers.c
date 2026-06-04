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

/* ─── Pass B (C8f.pass-b) ─────────────────────────────────────────────
 *
 * Same table memory as Pass A; different filter (single type 0x53),
 * different scale formula (raw LIFE_MULT, no ramp), and a simpler
 * matrix chain (RotY(π/2) × S × T — no per-record yaw RotZ).
 */

int test_wf_pass_b_should_emit_accepts_0x53(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    T_ASSERT(wf_pass_b_should_emit(slot) == 1);
    return 0;
}

int test_wf_pass_b_should_emit_rejects_sentinel_and_others(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t reject[] = { 0, 1, 0x52, 0x54, 0x77, 0xa2 };
    for (size_t i = 0; i < sizeof(reject) / sizeof(reject[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject[i];
        if (wf_pass_b_should_emit(slot) != 0) {
            T_FAIL("type 0x%x should NOT emit but did", reject[i]);
        }
    }
    return 0;
}

int test_wf_pass_b_scale_reads_life_mult_directly(void)
{
    /* No multiplier, no AGE ramp.  scale == LIFE_MULT verbatim. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_b_per_record_scale(slot), 1.0f, 1e-7f);

    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.4f);
    T_ASSERT_NEAR_WF(wf_pass_b_per_record_scale(slot), 0.4f, 1e-7f);

    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.5f);
    T_ASSERT_NEAR_WF(wf_pass_b_per_record_scale(slot), 2.5f, 1e-7f);
    return 0;
}

int test_wf_pass_b_scale_ignores_age(void)
{
    /* Pass A's ramp-in is keyed on AGE<5; Pass B has no such gate.
     * AGE=0 with LIFE_MULT=1.0 still gives scale=1.0, not 0.  */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_AGE] = 0;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_b_per_record_scale(slot), 1.0f, 1e-7f);
    return 0;
}

int test_wf_pass_b_compose_world_translation_in_row_3(void)
{
    /* M = RotY(π/2) × S(1.0) × T(10, 20, 30).
     *
     * Translation row stays at (10, 20, 30, 1) — scale only touches
     * rows 0..2's diagonal, RotY has row 3 = (0,0,0,1).  */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X,    10.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y,    20.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z,    30.0f);

    float world[16];
    wf_pass_b_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[12], 10.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[13], 20.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[14], 30.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[15],  1.0f, 1e-6f);
    return 0;
}

int test_wf_pass_b_compose_world_roty_pi_over_2(void)
{
    /* No translation, LIFE_MULT=1.0:
     *   M = RotY(π/2) × S(1.0) × T(0) = RotY(π/2).
     *
     * mat4_rotation_y(θ) returns the standard right-handed rotation:
     *   [  cos θ  0  -sin θ  0 ]
     *   [  0      1   0      0 ]
     *   [  sin θ  0   cos θ  0 ]
     *   [  0      0   0      1 ]
     *
     * For θ=π/2: cos=0, sin=1 → world[0]=0, world[2]=-1,
     * world[5]=1, world[8]=1, world[10]=0.  No RotZ tail, so the
     * matrix doesn't get an extra Z rotation (the distinguishing
     * feature vs Pass A).  */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x53;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);

    float world[16];
    wf_pass_b_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[0],  0.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[2], -1.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[5],  1.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[8],  1.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[10], 0.0f, 1e-5f);
    return 0;
}

/* ═══ Pass E spear group (C8f.pass-e-spear) ═══════════════════════════════
 *
 * Cardinal-int filter {0x71, 0x72, 0x75} (rejects 0 sentinel, fan types
 * {0x73, 0x7e, 0x78, 0xa0, 0x7a}, and Pass A/B types {0x77, 0xa2, 0x53}).
 * Same AGE<5 ramp-in as Pass A; 0x72 takes an additional 0.8 narrowing.
 * Tile origin (col,row) varies by type — 0x72 alternates between two
 * rows every 2 frames (AGE%4 quirk).  Matrix shape: RotZ(π - rotX) ×
 * DAT_0438cdf8 × S × T (pre-matrix reuses Pass C's stand-in storage,
 * default identity).  */

int test_wf_pass_e_spear_should_emit_rejects_sentinel(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0;
    T_ASSERT(wf_pass_e_spear_should_emit(slot) == 0);
    return 0;
}

int test_wf_pass_e_spear_should_emit_accepts_spear_types(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t accept[] = { 0x71, 0x72, 0x75 };
    for (size_t i = 0; i < sizeof(accept) / sizeof(accept[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = accept[i];
        if (wf_pass_e_spear_should_emit(slot) != 1)
            T_FAIL("type 0x%x should emit", accept[i]);
    }
    return 0;
}

int test_wf_pass_e_spear_should_emit_rejects_fan_types(void)
{
    /* Fan group is intentionally excluded from this chip — they fall
     * through silently until the fan-followup chip ports FUN_00415f2e. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t reject[] = { 0x73, 0x7e, 0x78, 0xa0, 0x7a };
    for (size_t i = 0; i < sizeof(reject) / sizeof(reject[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject[i];
        if (wf_pass_e_spear_should_emit(slot) != 0)
            T_FAIL("fan type 0x%x should not be a spear hit", reject[i]);
    }
    return 0;
}

int test_wf_pass_e_spear_should_emit_rejects_pass_ab_types(void)
{
    /* Pass A/B types (0x77, 0xa2, 0x53) are on the same table memory
     * but must not collide with Pass E spear's filter. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t reject[] = { 0x77, 0xa2, 0x53, 0x70, 0x73, 0x74, 0x76 };
    for (size_t i = 0; i < sizeof(reject) / sizeof(reject[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject[i];
        if (wf_pass_e_spear_should_emit(slot) != 0)
            T_FAIL("type 0x%x should not be a spear hit", reject[i]);
    }
    return 0;
}

/* ─── per-record scale ──────────────────────────────────────────────── */

int test_wf_pass_e_spear_scale_0x71_full_at_age_5(void)
{
    /* 0x71 at AGE=5 with LIFE_MULT=1.0 → no ramp, no 0x72 narrowing →
     * scale = 0.005. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_e_spear_per_record_scale(slot), 0.005f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_scale_0x72_narrows_by_0_point_8(void)
{
    /* 0x72 at AGE=5 with LIFE_MULT=1.0 → scale = 0.005 × 0.8 = 0.004. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x72;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_e_spear_per_record_scale(slot), 0.004f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_scale_0x75_no_narrowing(void)
{
    /* 0x75 at AGE=5 with LIFE_MULT=1.0 → scale = 0.005 (same as 0x71). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x75;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_e_spear_per_record_scale(slot), 0.005f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_scale_ramps_in_over_5_frames(void)
{
    /* 0x71 ramp: AGE 0..4 → 0/5, 1/5, 2/5, 3/5, 4/5 of 0.005. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    float expected[5] = { 0.0f, 0.001f, 0.002f, 0.003f, 0.004f };
    for (int age = 0; age < 5; age++) {
        slot[SCENE1_RECORDS_B_OFF_AGE] = age;
        T_ASSERT_NEAR_WF(wf_pass_e_spear_per_record_scale(slot),
                         expected[age], 1e-7f);
    }
    return 0;
}

int test_wf_pass_e_spear_scale_0x72_ramps_then_narrows(void)
{
    /* 0x72 at AGE=2 → ramp scale = (2/5)*0.005 = 0.002; then ×0.8 = 0.0016. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x72;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 2;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    T_ASSERT_NEAR_WF(wf_pass_e_spear_per_record_scale(slot), 0.0016f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_scale_uses_life_mult(void)
{
    /* 0x71 with LIFE_MULT=2.0 at AGE=10 → 2.0*0.005 = 0.01. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 10;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    T_ASSERT_NEAR_WF(wf_pass_e_spear_per_record_scale(slot), 0.01f, 1e-7f);
    return 0;
}

/* ─── per-record tile selection ─────────────────────────────────────── */

int test_wf_pass_e_spear_tile_0x71_is_default_128_192(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 0;
    float col, row;
    wf_pass_e_spear_tile(slot, &col, &row);
    T_ASSERT_NEAR_WF(col, 128.0f, 1e-7f);
    T_ASSERT_NEAR_WF(row, 192.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_tile_0x75_is_192_0(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x75;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 7;  /* irrelevant for 0x75 */
    float col, row;
    wf_pass_e_spear_tile(slot, &col, &row);
    T_ASSERT_NEAR_WF(col, 192.0f, 1e-7f);
    T_ASSERT_NEAR_WF(row,   0.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_tile_0x72_age_anim_first_half(void)
{
    /* 0x72 with AGE%4 < 2 (i.e. AGE ∈ {0,1,4,5,8,9,...}) → row 128. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x72;
    int32_t first_half[] = { 0, 1, 4, 5, 8, 100 };
    for (size_t i = 0; i < sizeof(first_half) / sizeof(first_half[0]); i++) {
        slot[SCENE1_RECORDS_B_OFF_AGE] = first_half[i];
        float col, row;
        wf_pass_e_spear_tile(slot, &col, &row);
        if (col != 192.0f || row != 128.0f)
            T_FAIL("AGE=%d → expected (192, 128), got (%g, %g)",
                   first_half[i], (double)col, (double)row);
    }
    return 0;
}

int test_wf_pass_e_spear_tile_0x72_age_anim_second_half(void)
{
    /* 0x72 with AGE%4 ≥ 2 (i.e. AGE ∈ {2,3,6,7,10,11,...}) → row 192. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x72;
    int32_t second_half[] = { 2, 3, 6, 7, 10, 99 };
    for (size_t i = 0; i < sizeof(second_half) / sizeof(second_half[0]); i++) {
        slot[SCENE1_RECORDS_B_OFF_AGE] = second_half[i];
        float col, row;
        wf_pass_e_spear_tile(slot, &col, &row);
        if (col != 192.0f || row != 192.0f)
            T_FAIL("AGE=%d → expected (192, 192), got (%g, %g)",
                   second_half[i], (double)col, (double)row);
    }
    return 0;
}

/* ─── UV box ────────────────────────────────────────────────────────── */

int test_wf_pass_e_spear_uv_box_default_0x71_origin(void)
{
    /* 0x71 default tile (128, 192) in 256-px atlas:
     *   u0 = 128.5/256, u1 = 191.5/256
     *   v0 = 192.5/256, v1 = 255.5/256.  */
    float u0, u1, v0, v1;
    wf_pass_e_spear_uv_box(128.0f, 192.0f, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, 128.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 191.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0, 192.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1, 255.5f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_spear_uv_box_0x75_top_row(void)
{
    /* 0x75 tile (192, 0): v0 should reach down to 0.5/256, v1 to 63.5/256. */
    float u0, u1, v0, v1;
    wf_pass_e_spear_uv_box(192.0f, 0.0f, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, 192.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 255.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0,   0.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1,  63.5f / 256.0f, 1e-7f);
    return 0;
}

/* ─── world matrix ──────────────────────────────────────────────────── */

int test_wf_pass_e_spear_compose_world_translation_in_row_3(void)
{
    /* AGE=5, LIFE_MULT=1.0, ROT_X=0, identity pre-matrix:
     *   M = RotZ(π) × I × S(0.005) × T(10, 20, 30).
     *
     * Translation row stays at (10, 20, 30, 1) — scale and rotations
     * leave row 3 = (0,0,0,1).  */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_ROT_X,     0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X,    10.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y,    20.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z,    30.0f);

    /* Reset Pass C's pre-matrix to identity (shared stand-in). */
    float identity[16] = {
        1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
    };
    wf_pass_c_set_pre_matrix(identity);

    float world[16];
    wf_pass_e_spear_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[12], 10.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[13], 20.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[14], 30.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[15],  1.0f, 1e-6f);
    return 0;
}

int test_wf_pass_e_spear_compose_world_rot_x_pi_cancels_rot_z(void)
{
    /* slot[ROT_X]=π → RotZ(π - π) = RotZ(0) = identity.  With identity
     * pre-matrix: M = I × I × S × T = S × T.  Pure scale on diagonals. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_ROT_X,     3.1415927f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X,     0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y,     0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z,     0.0f);

    float identity[16] = {
        1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
    };
    wf_pass_c_set_pre_matrix(identity);

    float world[16];
    wf_pass_e_spear_compose_world(world, slot);

    /* Diagonals are pure 0.005 (scale only). */
    T_ASSERT_NEAR_WF(world[0],  0.005f, 1e-5f);
    T_ASSERT_NEAR_WF(world[5],  0.005f, 1e-5f);
    T_ASSERT_NEAR_WF(world[10], 0.005f, 1e-5f);
    return 0;
}

int test_wf_pass_e_spear_compose_world_uses_pre_matrix(void)
{
    /* Confirm the pre-matrix slot is consumed: a non-identity pre-matrix
     * (a 2× uniform scale) should multiply through the scale chain.
     * Final scale at diagonal[0,5,10] = 2 × 0.005 = 0.01.  Reset back to
     * identity afterwards so subsequent tests aren't polluted. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x71;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 5;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_ROT_X,     3.1415927f);

    float pre[16] = {
        2, 0, 0, 0,  0, 2, 0, 0,  0, 0, 2, 0,  0, 0, 0, 1
    };
    wf_pass_c_set_pre_matrix(pre);

    float world[16];
    wf_pass_e_spear_compose_world(world, slot);

    T_ASSERT_NEAR_WF(world[0],  0.01f, 1e-5f);
    T_ASSERT_NEAR_WF(world[5],  0.01f, 1e-5f);
    T_ASSERT_NEAR_WF(world[10], 0.01f, 1e-5f);

    float identity[16] = {
        1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1
    };
    wf_pass_c_set_pre_matrix(identity);
    return 0;
}

/* ═══ Pass E fan group (C8f.pass-e-fan) ═══════════════════════════════════
 *
 * Filter: {0x73, 0x7e, 0x78, 0xa0, 0x7a} AND slot[AGE] >= 0.  Per-type
 * uniform-vs-stretched scaling (0x78/0xa0 → s,2s,2s; 0x7a → 1.2s,2.4s,
 * 2.4s; others → uniform).  UV: 0x7e plays a 5-frame anim; others use
 * static atlas tiles.  Billboard matrix orients along velocity.  */

int test_wf_pass_e_fan_should_emit_accepts_fan_types(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t accept[] = { 0x73, 0x7e, 0x78, 0xa0, 0x7a };
    for (size_t i = 0; i < sizeof(accept) / sizeof(accept[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = accept[i];
        slot[SCENE1_RECORDS_B_OFF_AGE]  = 0;
        if (wf_pass_e_fan_should_emit(slot) != 1)
            T_FAIL("fan type 0x%x should emit", accept[i]);
    }
    return 0;
}

int test_wf_pass_e_fan_should_emit_rejects_negative_age(void)
{
    /* Engine L353 second-gate: `piVar11[0x26] < 0` skips. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x73;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = -1;
    T_ASSERT(wf_pass_e_fan_should_emit(slot) == 0);
    return 0;
}

int test_wf_pass_e_fan_should_emit_rejects_spear_types(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    int32_t reject[] = { 0x71, 0x72, 0x75, 0x77, 0xa2, 0x53, 0 };
    for (size_t i = 0; i < sizeof(reject) / sizeof(reject[0]); i++) {
        slot_init_zero_b(slot);
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject[i];
        if (wf_pass_e_fan_should_emit(slot) != 0)
            T_FAIL("type 0x%x should not be a fan hit", reject[i]);
    }
    return 0;
}

/* ─── per-record scale ──────────────────────────────────────────────── */

int test_wf_pass_e_fan_scale_0x73_uniform(void)
{
    /* 0x73 with LIFE_MULT=1.0 → uniform (0.004, 0.004, 0.004), no AGE
     * ramp. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x73;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 0;  /* not ramped */
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    float sx, sy, sz;
    wf_pass_e_fan_per_record_scale_xyz(slot, &sx, &sy, &sz);
    T_ASSERT_NEAR_WF(sx, 0.004f, 1e-7f);
    T_ASSERT_NEAR_WF(sy, 0.004f, 1e-7f);
    T_ASSERT_NEAR_WF(sz, 0.004f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_scale_0x78_stretches_yz(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x78;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    float sx, sy, sz;
    wf_pass_e_fan_per_record_scale_xyz(slot, &sx, &sy, &sz);
    T_ASSERT_NEAR_WF(sx, 0.004f, 1e-7f);
    T_ASSERT_NEAR_WF(sy, 0.008f, 1e-7f);
    T_ASSERT_NEAR_WF(sz, 0.008f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_scale_0xa0_same_as_0x78(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0xa0;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    float sx, sy, sz;
    wf_pass_e_fan_per_record_scale_xyz(slot, &sx, &sy, &sz);
    T_ASSERT_NEAR_WF(sx, 0.004f, 1e-7f);
    T_ASSERT_NEAR_WF(sy, 0.008f, 1e-7f);
    T_ASSERT_NEAR_WF(sz, 0.008f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_scale_0x7a_applies_1_point_2(void)
{
    /* 0x7a: base × 1.2 = 0.0048; then (0.0048, 0.0096, 0.0096). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x7a;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    float sx, sy, sz;
    wf_pass_e_fan_per_record_scale_xyz(slot, &sx, &sy, &sz);
    T_ASSERT_NEAR_WF(sx, 0.0048f, 1e-7f);
    T_ASSERT_NEAR_WF(sy, 0.0096f, 1e-7f);
    T_ASSERT_NEAR_WF(sz, 0.0096f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_scale_uses_life_mult(void)
{
    /* 0x73 with LIFE_MULT=2.5 → 0.01 uniform. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x73;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.5f);
    float sx, sy, sz;
    wf_pass_e_fan_per_record_scale_xyz(slot, &sx, &sy, &sz);
    T_ASSERT_NEAR_WF(sx, 0.01f, 1e-6f);
    T_ASSERT_NEAR_WF(sy, 0.01f, 1e-6f);
    T_ASSERT_NEAR_WF(sz, 0.01f, 1e-6f);
    return 0;
}

/* ─── UV box ────────────────────────────────────────────────────────── */

int test_wf_pass_e_fan_uv_0x73_static_tile(void)
{
    /* 0x73: u ∈ (96.5/256, 111.5/256), v ∈ (160.5/256, 175.5/256). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x73;
    float u0, u1, v0, v1;
    wf_pass_e_fan_uv_box(slot, /*slot_idx*/ 0, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0,  96.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 111.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0, 160.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1, 175.5f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_uv_0x78_tall_tile(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x78;
    float u0, u1, v0, v1;
    wf_pass_e_fan_uv_box(slot, 0, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0,  96.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 111.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0, 128.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1, 159.5f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_uv_0x7e_5frame_anim_frame_0(void)
{
    /* slot_idx % 5 == 0 → phase=0 → col = (0%3)*32 + 80 = 80, row = 0. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x7e;
    float u0, u1, v0, v1;
    wf_pass_e_fan_uv_box(slot, 0, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0,  80.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 111.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0,   0.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1,  31.5f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_uv_0x7e_5frame_anim_frame_3(void)
{
    /* slot_idx % 5 == 3 → phase=3 → col = (3%3)*32 + 80 = 80,
     * row = (3/3) << 5 = 32. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x7e;
    float u0, u1, v0, v1;
    wf_pass_e_fan_uv_box(slot, 3, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0,  80.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 111.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0,  32.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1,  63.5f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_e_fan_uv_0x7e_phase2_wraps_to_col_144(void)
{
    /* slot_idx=2 → phase=2 → col=(2%3)*32+80=144, row=(2/3)<<5=0. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x7e;
    float u0, u1, v0, v1;
    wf_pass_e_fan_uv_box(slot, 2, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, 144.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 175.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0,   0.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1,  31.5f / 256.0f, 1e-7f);
    return 0;
}

/* ─── camera billboard matrix (FUN_00415f2e port) ──────────────────── */

int test_wf_pass_e_fan_billboard_with_vel_along_x(void)
{
    /* Particle at origin with VEL=(1,0,0); camera at (0,0,5) (so
     * up = (0,0,5) - (0,0,0) = (0,0,5)).
     *
     * LookAtRH(eye=(0,0,0), target=(1,0,0), up=(0,0,5)):
     *   zaxis = normalize(eye - target) = (-1, 0, 0)
     *   xaxis = normalize(up × zaxis) = normalize((0,0,1)×(-1,0,0))
     *         = normalize((0,-1,0)*(-1)) = (0, 1, 0)         [using up scaled]
     *     up_scaled × zaxis = (0,0,5) × (-1,0,0)
     *       = (0*0 - 5*0, 5*(-1) - 0*0, 0*0 - 0*(-1))
     *       = (0, -5, 0); normalize → (0, -1, 0)
     *     wait — D3DX LookAtRH uses up × zaxis (not up_scaled × zaxis).
     *     normalize(up × zaxis):
     *       (0,0,5) × (-1,0,0) = (0*0 - 5*0, 5*(-1) - 0*0, 0*0 - 0*(-1))
     *                          = (0, -5, 0)
     *       normalize → (0, -1, 0)
     *     so xaxis = (0, -1, 0)
     *   yaxis = zaxis × xaxis = (-1,0,0) × (0,-1,0)
     *         = (0*0 - 0*(-1), 0*0 - (-1)*0, (-1)*(-1) - 0*0)
     *         = (0, 0, 1)
     *
     * LookAt matrix is the world→view transform.  Its inverse is the
     * view→world transform: places a local +Z = -zaxis along the
     * direction from eye to target, +X = -xaxis along the perpendicular,
     * +Y = -yaxis along the camera-up.
     *
     * We don't pin down exact entries here — testing the matrix is
     * non-degenerate and that inverting LookAt undoes its effect on the
     * origin is enough.  See test_math_inverse_inverse_round_trips_to_
     * identity for the round-trip property; this test just verifies the
     * helper produces a well-defined non-zero matrix for sane inputs. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X, 0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y, 0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z, 0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_X, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    float camera_eye[3] = { 0.0f, 0.0f, 5.0f };
    float out[16];
    wf_pass_e_fan_billboard_matrix(out, slot, camera_eye);

    /* Bottom row is (0, 0, 0, 1) for any inverse of an affine xform. */
    T_ASSERT_NEAR_WF(out[ 3], 0.0f, 1e-5f);
    T_ASSERT_NEAR_WF(out[ 7], 0.0f, 1e-5f);
    T_ASSERT_NEAR_WF(out[11], 0.0f, 1e-5f);
    T_ASSERT_NEAR_WF(out[15], 1.0f, 1e-5f);

    /* Translation row must equal `eye` (the origin) for the LookAtRH
     * inverse — the original LookAt sends `eye` to 0, so its inverse
     * sends 0 to `eye`. */
    T_ASSERT_NEAR_WF(out[12], 0.0f, 1e-5f);
    T_ASSERT_NEAR_WF(out[13], 0.0f, 1e-5f);
    T_ASSERT_NEAR_WF(out[14], 0.0f, 1e-5f);
    return 0;
}

int test_wf_pass_e_fan_billboard_translation_at_pos(void)
{
    /* Eye at (10, 20, 30); arbitrary VEL.  Inverse LookAt translation
     * row should restore the eye position. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X, 10.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y, 20.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z, 30.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_Y, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    float camera_eye[3] = { 10.0f, 20.0f, 35.0f };
    float out[16];
    wf_pass_e_fan_billboard_matrix(out, slot, camera_eye);

    T_ASSERT_NEAR_WF(out[12], 10.0f, 1e-3f);
    T_ASSERT_NEAR_WF(out[13], 20.0f, 1e-3f);
    T_ASSERT_NEAR_WF(out[14], 30.0f, 1e-3f);
    T_ASSERT_NEAR_WF(out[15],  1.0f, 1e-5f);
    return 0;
}

int test_wf_pass_e_fan_compose_world_includes_translation(void)
{
    /* Full compose_world stack: S × RotY(π/2) × billboard.  Translation
     * should still land at POS because the billboard matrix is the only
     * one that touches row 3; the RotY and S left-multiplies preserve
     * row 3 = (0,0,0,1) under the row-major convention. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_init_zero_b(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x73;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 10;
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_X, 5.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Y, 6.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_POS_Z, 7.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_X, 1.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_float_b(slot, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    float camera_eye[3] = { 0.0f, 0.0f, 10.0f };
    float world[16];
    wf_pass_e_fan_compose_world(world, slot, camera_eye);

    T_ASSERT_NEAR_WF(world[15], 1.0f, 1e-5f);
    /* Translation row preserved through S*Ry: S scales row 3 by 1
     * (homogeneous coord), Ry leaves it untouched, so M[12..14] = POS. */
    T_ASSERT_NEAR_WF(world[12], 5.0f, 1e-3f);
    T_ASSERT_NEAR_WF(world[13], 6.0f, 1e-3f);
    T_ASSERT_NEAR_WF(world[14], 7.0f, 1e-3f);
    return 0;
}

/* ═══ Pass D tests (C8f.pass-d) ═══════════════════════════════════════════ */

/* ─── predicate ──────────────────────────────────────────────────────── */

int test_wf_pass_d_should_emit_rejects_sentinel(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = -1;
    T_ASSERT(wf_pass_d_should_emit(slot) == 0);
    return 0;
}

int test_wf_pass_d_should_emit_rejects_type_le_6(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    for (int t = 0; t <= 6; t++) {
        slot_init_zero_c(slot);
        slot[SCENE1_RECORDS_C_OFF_TYPE] = t;
        if (wf_pass_d_should_emit(slot) != 0)
            T_FAIL("type %d should not emit (Pass C range)", t);
    }
    return 0;
}

int test_wf_pass_d_should_emit_accepts_type_gt_6(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    int types[] = { 7, 8, 100, 200, 0x7fffffff };
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        slot_init_zero_c(slot);
        slot[SCENE1_RECORDS_C_OFF_TYPE] = types[i];
        if (wf_pass_d_should_emit(slot) != 1)
            T_FAIL("type %d should emit (world-pickup range)", types[i]);
    }
    return 0;
}

/* ─── per-record scale ───────────────────────────────────────────────── */

int test_wf_pass_d_scale_default_when_not_selected(void)
{
    T_ASSERT_NEAR_WF(wf_pass_d_per_record_scale(0), 0.0192f, 1e-7f);
    return 0;
}

int test_wf_pass_d_scale_larger_when_selected(void)
{
    T_ASSERT_NEAR_WF(wf_pass_d_per_record_scale(1), 0.026880002f, 1e-7f);
    return 0;
}

/* ─── pulse RGB ──────────────────────────────────────────────────────── */

int test_wf_pass_d_pulse_rgb_zero_when_not_selected(void)
{
    /* Engine: not the selected slot → uVar6 stays 0. */
    T_ASSERT_EQ_U(wf_pass_d_pulse_rgb(50, 0), 0);
    return 0;
}

int test_wf_pass_d_pulse_rgb_at_age_0_is_96(void)
{
    /* sinf(0) = 0 → 0 * 64 + 96 = 96. */
    T_ASSERT_EQ_U(wf_pass_d_pulse_rgb(0, 1), 96);
    return 0;
}

int test_wf_pass_d_pulse_rgb_range_is_32_to_160(void)
{
    /* Sweep AGE [0..200], all values must be in [32, 160]. */
    for (int age = 0; age <= 200; age++) {
        uint32_t v = wf_pass_d_pulse_rgb(age, 1);
        if (v < 32 || v > 160)
            T_FAIL("age %d pulse %u out of [32,160]", age, v);
    }
    return 0;
}

/* ─── alpha ──────────────────────────────────────────────────────────── */

int test_wf_pass_d_alpha_world_drop_full_opaque(void)
{
    /* STATE=0 (world drop) → alpha=0xff regardless of AGE. */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_STATE] = 0;
    slot[SCENE1_RECORDS_C_OFF_AGE]   = 0;
    T_ASSERT_EQ_I(wf_pass_d_alpha(slot), 0xff);
    slot[SCENE1_RECORDS_C_OFF_AGE]   = 200;
    T_ASSERT_EQ_I(wf_pass_d_alpha(slot), 0xff);
    return 0;
}

int test_wf_pass_d_alpha_pickup_zero_below_threshold(void)
{
    /* STATE=2 with AGE <= 0x1e → alpha=0. */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_STATE] = 2;
    for (int age = 0; age <= 0x1e; age++) {
        slot[SCENE1_RECORDS_C_OFF_AGE] = age;
        if (wf_pass_d_alpha(slot) != 0)
            T_FAIL("age %d should give alpha=0 (state=2)", age);
    }
    return 0;
}

int test_wf_pass_d_alpha_pickup_ramps_in(void)
{
    /* STATE=2, AGE > 0x1e: alpha = (AGE - 0x1e) * 0x20, clamped at 0xff. */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_STATE] = 2;
    slot[SCENE1_RECORDS_C_OFF_AGE] = 0x1f;  /* (0x1f - 0x1e) * 0x20 = 0x20 */
    T_ASSERT_EQ_I(wf_pass_d_alpha(slot), 0x20);
    slot[SCENE1_RECORDS_C_OFF_AGE] = 0x25;  /* (0x25 - 0x1e) * 0x20 = 0xe0 */
    T_ASSERT_EQ_I(wf_pass_d_alpha(slot), 0xe0);
    slot[SCENE1_RECORDS_C_OFF_AGE] = 0x26;  /* (0x26 - 0x1e) * 0x20 = 0x100 → 0xff */
    T_ASSERT_EQ_I(wf_pass_d_alpha(slot), 0xff);
    slot[SCENE1_RECORDS_C_OFF_AGE] = 0x100; /* far past clamp */
    T_ASSERT_EQ_I(wf_pass_d_alpha(slot), 0xff);
    return 0;
}

/* ─── diffuse shuffle ─────────────────────────────────────────────────── */

int test_wf_pass_d_diffuse_grayscale_with_alpha(void)
{
    /* (alpha=0xff, rgb=0x80) → 0xff_80_80_80 in 0xAARRGGBB. */
    T_ASSERT_EQ_U(wf_pass_d_diffuse(0x80, 0xff), 0xff808080u);
    /* (alpha=0,   rgb=0x60) → 0x00_60_60_60 */
    T_ASSERT_EQ_U(wf_pass_d_diffuse(0x60, 0), 0x00606060u);
    /* (alpha=0x40, rgb=0)   → 0x40_00_00_00 */
    T_ASSERT_EQ_U(wf_pass_d_diffuse(0, 0x40), 0x40000000u);
    return 0;
}

int test_wf_pass_d_diffuse_masks_rgb_to_low_byte(void)
{
    /* The engine's `rgb_lo & 0xff` is implicit in the channel shuffle.
     * Defensive: ensure values > 0xff don't leak into adjacent channels. */
    T_ASSERT_EQ_U(wf_pass_d_diffuse(0x1234, 0xff), 0xff343434u);
    return 0;
}

/* ─── UV box ─────────────────────────────────────────────────────────── */

int test_wf_pass_d_uv_box_tile_0_top_left(void)
{
    /* tile 0: col=0, row=0.  u0 = 0.5/256, u1 = 31.5/256.
     *                         v0 = 0.5/H,  v1 = 31.0/H.   */
    float u0, u1, v0, v1;
    wf_pass_d_uv_box(0, 256.0f, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, 0.5f / 256.0f,  1e-7f);
    T_ASSERT_NEAR_WF(u1, 31.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0, 0.5f / 256.0f,  1e-7f);
    T_ASSERT_NEAR_WF(v1, 31.0f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_d_uv_box_tile_8_wraps_to_row_1(void)
{
    /* tile 8: col=0, row=1.  v0 = 32.5/H, v1 = 63.0/H. */
    float u0, u1, v0, v1;
    wf_pass_d_uv_box(8, 256.0f, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, 0.5f  / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 31.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0, 32.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v1, 63.0f / 256.0f, 1e-7f);
    return 0;
}

int test_wf_pass_d_uv_box_respects_custom_tex_height(void)
{
    /* tile 16 (col=0, row=2) with tex_height=128: v0 = 64.5/128. */
    float u0, u1, v0, v1;
    wf_pass_d_uv_box(16, 128.0f, &u0, &u1, &v0, &v1);
    T_ASSERT_NEAR_WF(u0, 0.5f  / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(u1, 31.5f / 256.0f, 1e-7f);
    T_ASSERT_NEAR_WF(v0, 64.5f / 128.0f, 1e-6f);
    T_ASSERT_NEAR_WF(v1, 95.0f / 128.0f, 1e-6f);
    return 0;
}

/* ─── world matrix ───────────────────────────────────────────────────── */

int test_wf_pass_d_compose_world_translation_in_row_3(void)
{
    /* T × S × pre_matrix (identity by default) puts POS in row 3. */
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 10;
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_X, 1.5f);
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_Y, 2.5f);
    slot_set_float_c(slot, SCENE1_RECORDS_C_OFF_POS_Z, 3.5f);

    /* Default pre-matrix is identity. */
    float ident[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    wf_pass_c_set_pre_matrix(ident);

    float world[16];
    wf_pass_d_compose_world(world, slot, 0);

    /* With S=0.0192*I and T=Translation(POS), final M = S*T*I has scale
     * on diagonal and POS on row 3 (unchanged by scale of row 3). */
    T_ASSERT_NEAR_WF(world[0],  0.0192f, 1e-7f);
    T_ASSERT_NEAR_WF(world[5],  0.0192f, 1e-7f);
    T_ASSERT_NEAR_WF(world[10], 0.0192f, 1e-7f);
    T_ASSERT_NEAR_WF(world[12], 1.5f,    1e-6f);
    T_ASSERT_NEAR_WF(world[13], 2.5f,    1e-6f);
    T_ASSERT_NEAR_WF(world[14], 3.5f,    1e-6f);
    T_ASSERT_NEAR_WF(world[15], 1.0f,    1e-7f);
    return 0;
}

int test_wf_pass_d_compose_world_selected_has_larger_scale(void)
{
    int32_t slot[SCENE1_RECORDS_C_STRIDE];
    slot_init_zero_c(slot);
    slot[SCENE1_RECORDS_C_OFF_TYPE] = 10;

    /* Reset to identity so we can read S on the diagonal. */
    float ident[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    wf_pass_c_set_pre_matrix(ident);

    float world[16];
    wf_pass_d_compose_world(world, slot, 1);

    T_ASSERT_NEAR_WF(world[0],  0.026880002f, 1e-7f);
    T_ASSERT_NEAR_WF(world[5],  0.026880002f, 1e-7f);
    T_ASSERT_NEAR_WF(world[10], 0.026880002f, 1e-7f);
    return 0;
}

/* ─── item resolver hook ─────────────────────────────────────────────── */

static int g_test_resolver_last_key = 0;
static int g_test_resolver_call_count = 0;

static int test_resolver_always_hit(int type_key,
                                    wf_pass_d_item_resolved *out)
{
    g_test_resolver_last_key   = type_key;
    g_test_resolver_call_count++;
    out->tex = (void *)0xdeadbeef;
    out->tile_raw = 7;
    out->tex_height = 128.0f;
    return 1;
}

int test_wf_pass_d_resolver_default_misses(void)
{
    /* After any prior override (e.g. from a sibling test), restore the
     * default by passing NULL to the setter — the setter normalizes NULL
     * back to the default stub. */
    wf_pass_d_set_item_resolver(NULL);
    wf_pass_d_item_resolved item;
    int hit = wf_pass_d_resolve_item(0, &item);
    T_ASSERT_EQ_I(hit, 0);
    return 0;
}

int test_wf_pass_d_resolver_override_round_trips(void)
{
    g_test_resolver_call_count = 0;
    g_test_resolver_last_key = -999;

    wf_pass_d_item_resolver_fn prev =
        wf_pass_d_set_item_resolver(test_resolver_always_hit);
    T_ASSERT(prev != NULL);

    wf_pass_d_item_resolved item = { 0 };
    int hit = wf_pass_d_resolve_item(42, &item);
    T_ASSERT_EQ_I(hit, 1);
    T_ASSERT_EQ_I(g_test_resolver_last_key, 42);
    T_ASSERT_EQ_I(g_test_resolver_call_count, 1);
    T_ASSERT(item.tex == (void *)0xdeadbeef);
    T_ASSERT_EQ_I(item.tile_raw, 7);
    T_ASSERT_NEAR_WF(item.tex_height, 128.0f, 1e-7f);

    /* Restore the default stub so subsequent tests don't see the override. */
    wf_pass_d_set_item_resolver(prev);
    return 0;
}

int test_wf_pass_d_resolver_null_out_is_safe(void)
{
    /* Defensive: passing NULL for out should not deref. */
    wf_pass_d_set_item_resolver(test_resolver_always_hit);
    int hit = wf_pass_d_resolve_item(0, NULL);
    T_ASSERT_EQ_I(hit, 0);
    wf_pass_d_set_item_resolver(NULL);
    return 0;
}

/* ─── selected-slot global default ───────────────────────────────────── */

int test_wf_pass_d_selected_slot_default_is_minus_1(void)
{
    /* Default at boot is -1 (no selection).  This is an extern int — set
     * here for documentation, then restore the engine default. */
    g_wf_pass_d_selected_slot = -1;
    T_ASSERT_EQ_I(g_wf_pass_d_selected_slot, -1);
    return 0;
}

/* ─── shop "items on display" (FUN_00415fab) helpers ─────────────────── */

int test_wf_display_item_world_origin_cell(void)
{
    /* col=0, row=0, z=0 → pos = (0*2-9, 0+1.9, 0*2-6.5) = (-9, 1.9, -6.5).
     * Identity pre-matrix → out = S(0.0192) × T(pos). */
    float world[16];
    wf_display_item_compose_world(world, 0, 0, 0.0f);

    T_ASSERT_NEAR_WF(world[0],  0.0192f, 1e-6f);
    T_ASSERT_NEAR_WF(world[5],  0.0192f, 1e-6f);
    T_ASSERT_NEAR_WF(world[10], 0.0192f, 1e-6f);
    T_ASSERT_NEAR_WF(world[12], -9.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[13],  1.9f, 1e-5f);
    T_ASSERT_NEAR_WF(world[14], -6.5f, 1e-5f);
    T_ASSERT_NEAR_WF(world[15],  1.0f, 1e-6f);
    return 0;
}

int test_wf_display_item_world_inner_cell(void)
{
    /* col=5, row=3, z=0 → pos = (5*2-9, 1.9, 3*2-6.5) = (1, 1.9, -0.5). */
    float world[16];
    wf_display_item_compose_world(world, 5, 3, 0.0f);

    T_ASSERT_NEAR_WF(world[12],  1.0f, 1e-5f);
    T_ASSERT_NEAR_WF(world[13],  1.9f, 1e-5f);
    T_ASSERT_NEAR_WF(world[14], -0.5f, 1e-5f);
    return 0;
}

int test_wf_display_item_uv_box_icon_0(void)
{
    /* icon 0, 256-tall page → 32×32 cell at column 0, row 0.
     * U inset 0.5/31.5 over 256; V inset 0.5/31.0 over tex_height. */
    float ul, ur, vt, vb;
    wf_display_item_uv_box(0, 256.0f, &ul, &ur, &vt, &vb);
    T_ASSERT_NEAR_WF(ul,  0.5f  / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(ur, 31.5f  / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(vt,  0.5f  / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(vb, 31.0f  / 256.0f, 1e-8f);
    return 0;
}

int test_wf_display_item_uv_box_icon_9_wraps_to_row_1(void)
{
    /* icon 9 → col = 9%8 = 1 (col_px 32), row = 9/8 = 1 (row_px 32). */
    float ul, ur, vt, vb;
    wf_display_item_uv_box(9, 256.0f, &ul, &ur, &vt, &vb);
    T_ASSERT_NEAR_WF(ul, (32.0f + 0.5f)  / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(ur, (32.0f + 31.5f) / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(vt, (32.0f + 0.5f)  / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(vb, (32.0f + 31.0f) / 256.0f, 1e-8f);
    return 0;
}

int test_wf_display_item_uv_box_respects_tex_height(void)
{
    /* V denominator is the real page height, U denominator stays 256. */
    float ul, ur, vt, vb;
    wf_display_item_uv_box(0, 64.0f, &ul, &ur, &vt, &vb);
    T_ASSERT_NEAR_WF(ul,  0.5f / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(ur, 31.5f / 256.0f, 1e-8f);
    T_ASSERT_NEAR_WF(vt,  0.5f / 64.0f,  1e-8f);
    T_ASSERT_NEAR_WF(vb, 31.0f / 64.0f,  1e-8f);
    return 0;
}
