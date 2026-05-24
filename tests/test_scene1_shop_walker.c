/*
 * test_scene1_shop_walker.c — Pass D helper coverage (C8c follow-up).
 *
 * The walker entry (scene1_shop_walker) uses D3D, so it stays in the
 * mingw-only `#ifdef _WIN32` block of scene1_shop_walker.c.  The per-
 * record helpers (predicate + matrix composer) are D3D-free and live in
 * scene1_shop_walker_helpers.c — that's what we test here.
 *
 * Covers Pass D (engine FUN_004552d0 L239-L258, asm @ 0x455bc8..0x455cea):
 *   - sw_pass_d_should_emit:
 *       * type == -1 sentinel rejected (asm-first short-circuit)
 *       * type ∈ {0x74, 0x79, 0x96} accepted
 *       * adjacent values (0x73, 0x75, 0x78, 0x7a, 0x95, 0x97) rejected
 *   - sw_pass_d_compose_world:
 *       * Translation only (rotX=0, scale=1.0 → final 0.2)
 *           pos (1, 2, 3) → row 3 of M = (1, 2, 3)
 *           row 0 of M (X scale) = -0.2 (mirror), rows 1/2 diag = +0.2
 *       * rotX = π/2 (90° about X axis):
 *           applied after the (-0.2, 0.2, 0.2) scale; check Y/Z mixing
 *       * scale field read as FLOAT (Ghidra's int-cast was a quirk;
 *           asm `fld dword [edi+8]`)
 */

#include "t.h"

#include <math.h>
#include <string.h>

#include "math3d.h"
#include "scene1_records.h"
#include "scene1_shop_walker.h"

static int float_near_d(float a, float b, float tol)
{
    float d = a - b;
    if (d < 0) d = -d;
    return d <= tol;
}

#define T_ASSERT_NEAR_D(a, b, tol) do {                                     \
    float _a = (float)(a), _b = (float)(b), _t = (float)(tol);              \
    if (!float_near_d(_a, _b, _t))                                          \
        T_FAIL("expected %s ≈ %s within %g (got %.6f, want %.6f, |Δ|=%.6f)",\
               #a, #b, _t, _a, _b, (double)((_a-_b<0)?-(_a-_b):(_a-_b)));   \
} while (0)

static void slot_init_zero(int32_t slot[SCENE1_RECORDS_A_STRIDE])
{
    memset(slot, 0, sizeof(int32_t) * SCENE1_RECORDS_A_STRIDE);
}

static void slot_set_float(int32_t slot[SCENE1_RECORDS_A_STRIDE],
                           int offset, float value)
{
    memcpy(&slot[offset], &value, sizeof(value));
}

/* ─── predicate ──────────────────────────────────────────────────────── */

int test_scene1_pass_d_should_emit_rejects_sentinel(void)
{
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);
    slot[SCENE1_RECORDS_A_OFF_TYPE] = -1;
    T_ASSERT(sw_pass_d_should_emit(slot) == 0);
    return 0;
}

int test_scene1_pass_d_should_emit_accepts_match_types(void)
{
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);

    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x74;
    T_ASSERT(sw_pass_d_should_emit(slot) == 1);

    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x79;
    T_ASSERT(sw_pass_d_should_emit(slot) == 1);

    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x96;
    T_ASSERT(sw_pass_d_should_emit(slot) == 1);
    return 0;
}

int test_scene1_pass_d_should_emit_rejects_adjacent_types(void)
{
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);
    int adjacent[] = { 0x00, 0x01, 0x73, 0x75, 0x78, 0x7a,
                       0x92, 0x95, 0x97, 0xff };
    for (int i = 0; i < (int)(sizeof(adjacent)/sizeof(adjacent[0])); i++) {
        slot[SCENE1_RECORDS_A_OFF_TYPE] = adjacent[i];
        if (sw_pass_d_should_emit(slot) != 0) {
            T_FAIL("type 0x%x should not emit", adjacent[i]);
        }
    }
    return 0;
}

/* ─── matrix composer ────────────────────────────────────────────────── */

int test_scene1_pass_d_compose_translation_only(void)
{
    /* scale=1.0 → final scale 0.2; rotX=0 (no rotation).  Expect
     * X-mirror diagonal: (-0.2, 0.2, 0.2, 1).  Translation row 3 =
     * pos unchanged (translation is applied to the row vector AFTER
     * the scale-then-translate left-multiply, which in row-major
     * D3DX semantics means row 3 of T survives the scale because
     * S * T puts S's diag in cols 0..2 of rows 0..2 and T's row 3
     * in row 3). */
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);
    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x74;
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_POS_X, 1.0f);
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_POS_Y, 2.0f);
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_POS_Z, 3.0f);
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_ROT_X, 0.0f);
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_SCALE, 1.0f);

    float M[16];
    sw_pass_d_compose_world(M, slot);

    /* Row 0 (X axis post-scale-mirror, post-rotX=0): (-0.2, 0, 0, 0). */
    T_ASSERT_NEAR_D(M[0], -0.2f, 1e-6f);
    T_ASSERT_NEAR_D(M[1],  0.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[2],  0.0f, 1e-6f);

    /* Row 1 (Y axis): (0, 0.2, 0, 0). */
    T_ASSERT_NEAR_D(M[4],  0.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[5],  0.2f, 1e-6f);
    T_ASSERT_NEAR_D(M[6],  0.0f, 1e-6f);

    /* Row 2 (Z axis): (0, 0, 0.2, 0). */
    T_ASSERT_NEAR_D(M[8],  0.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[9],  0.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[10], 0.2f, 1e-6f);

    /* Row 3 (translation): preserved (1, 2, 3). */
    T_ASSERT_NEAR_D(M[12], 1.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[13], 2.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[14], 3.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[15], 1.0f, 1e-6f);
    return 0;
}

int test_scene1_pass_d_compose_scale_factor_is_point_2(void)
{
    /* SCALE field = 0.5 → final scale 0.1 (= 0.5 * 0.2).  Confirms
     * the .rdata 0x5198d8 == 0.2f multiplier read from asm. */
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);
    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x79;
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_SCALE, 0.5f);
    /* pos = (0,0,0), rotX = 0. */

    float M[16];
    sw_pass_d_compose_world(M, slot);

    T_ASSERT_NEAR_D(M[0],  -0.1f, 1e-6f);
    T_ASSERT_NEAR_D(M[5],   0.1f, 1e-6f);
    T_ASSERT_NEAR_D(M[10],  0.1f, 1e-6f);
    return 0;
}

int test_scene1_pass_d_compose_rotation_x_mixes_y_z(void)
{
    /* scale = 5.0 → final 1.0 (clean), rotX = π/2.  After scale:
     * diagonal (-1, 1, 1).  After Rx(π/2) * S in row-major:
     *   Rx * S has:
     *     row 0 = (-1, 0, 0)              (X column from S)
     *     row 1 = (0, cos·1, sin·1) = (0, 0, 1)
     *     row 2 = (0, -sin·1, cos·1) = (0, -1, 0)
     */
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);
    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x96;
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_SCALE, 5.0f);
    slot_set_float(slot, SCENE1_RECORDS_A_OFF_ROT_X, 1.5707963267948966f);

    float M[16];
    sw_pass_d_compose_world(M, slot);

    /* Row 0 (X) — unchanged by Rx. */
    T_ASSERT_NEAR_D(M[0], -1.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[1],  0.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[2],  0.0f, 1e-5f);
    /* Row 1 = (0, 0, 1) */
    T_ASSERT_NEAR_D(M[4],  0.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[5],  0.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[6],  1.0f, 1e-5f);
    /* Row 2 = (0, -1, 0) */
    T_ASSERT_NEAR_D(M[8],  0.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[9], -1.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[10], 0.0f, 1e-5f);
    return 0;
}

int test_scene1_pass_d_compose_scale_field_is_float_not_int(void)
{
    /* If the SCALE field were read as INT then cast to float, a
     * SCALE bit-pattern of 0x3f800000 (= 1.0f) would read as the
     * integer 1065353216 and produce scale=213070643.  Reading as
     * float yields scale=1.0*0.2=0.2.  This test confirms the
     * asm-verified interpretation: fld (float-load), not fild
     * (int-to-float load).
     *
     * Engine asm @ 0x455bfc: `fld dword [edi+0x8]` — definitive. */
    int32_t slot[SCENE1_RECORDS_A_STRIDE];
    slot_init_zero(slot);
    slot[SCENE1_RECORDS_A_OFF_TYPE] = 0x74;
    /* Manually write IEEE 754 1.0f as raw int bits. */
    slot[SCENE1_RECORDS_A_OFF_SCALE] = (int32_t)0x3f800000;

    float M[16];
    sw_pass_d_compose_world(M, slot);

    /* Final scale = 1.0f * 0.2f = 0.2f.  If int-interpreted the
     * value would be in the 200-million range and trigger floating
     * inf — this assertion would fail either way. */
    T_ASSERT_NEAR_D(M[5], 0.2f, 1e-6f);
    return 0;
}

/* ─── Pass D mesh setter/getter (C8e.bridge) ────────────────────────────
 *
 * Stand-in for the engine's static &DAT_073a9680 (train_iwa.x mesh-
 * record).  Default NULL → sw_pass_d's call to scene1_emit_record
 * short-circuits inside (mirrors engine HOUSE dormancy).  --force-pass-
 * d-mesh sets the slot via the setter at boot.
 */

int test_scene1_pass_d_mesh_default_is_null(void)
{
    /* Reset to a known state first — other tests in this run might
     * have set it. */
    scene1_shop_walker_set_pass_d_mesh(NULL);
    T_ASSERT(scene1_shop_walker_get_pass_d_mesh() == NULL);
    return 0;
}

int test_scene1_pass_d_mesh_setter_round_trips(void)
{
    /* The setter takes a borrowed pointer; the getter returns
     * exactly what was set.  Empty mesh struct is fine — we only
     * test the pointer round-trip, not the deref. */
    mesh_t fake = {0};
    scene1_shop_walker_set_pass_d_mesh(&fake);
    T_ASSERT(scene1_shop_walker_get_pass_d_mesh() == &fake);
    /* Tidy up so subsequent tests start from NULL. */
    scene1_shop_walker_set_pass_d_mesh(NULL);
    T_ASSERT(scene1_shop_walker_get_pass_d_mesh() == NULL);
    return 0;
}

int test_scene1_pass_d_mesh_setter_replaces_previous(void)
{
    /* Subsequent sets overwrite — caller owns the lifetime, we just
     * hold a pointer. */
    mesh_t a = {0}, b = {0};
    scene1_shop_walker_set_pass_d_mesh(&a);
    T_ASSERT(scene1_shop_walker_get_pass_d_mesh() == &a);
    scene1_shop_walker_set_pass_d_mesh(&b);
    T_ASSERT(scene1_shop_walker_get_pass_d_mesh() == &b);
    scene1_shop_walker_set_pass_d_mesh(NULL);
    return 0;
}

int test_scene1_debug_pass_d_unlit_default_is_off(void)
{
    /* Default 0 keeps goldens bit-exact — sw_pass_d skips the override
     * block and runs the engine's L548-562 preamble verbatim. */
    scene1_shop_walker_set_debug_pass_d_unlit(0);
    T_ASSERT(scene1_shop_walker_get_debug_pass_d_unlit() == 0);
    return 0;
}

int test_scene1_debug_pass_d_unlit_setter_round_trips(void)
{
    scene1_shop_walker_set_debug_pass_d_unlit(1);
    T_ASSERT(scene1_shop_walker_get_debug_pass_d_unlit() == 1);
    scene1_shop_walker_set_debug_pass_d_unlit(0);
    T_ASSERT(scene1_shop_walker_get_debug_pass_d_unlit() == 0);
    return 0;
}

int test_scene1_debug_pass_d_unlit_normalises_to_0_or_1(void)
{
    /* Any non-zero input normalises to 1 — keeps the sw_pass_d test
     * simple (`if (flag)` works regardless of the input bit pattern). */
    scene1_shop_walker_set_debug_pass_d_unlit(42);
    T_ASSERT(scene1_shop_walker_get_debug_pass_d_unlit() == 1);
    scene1_shop_walker_set_debug_pass_d_unlit(-7);
    T_ASSERT(scene1_shop_walker_get_debug_pass_d_unlit() == 1);
    scene1_shop_walker_set_debug_pass_d_unlit(0);
    T_ASSERT(scene1_shop_walker_get_debug_pass_d_unlit() == 0);
    return 0;
}

/* ─── Pass B (C8c.B) ───────────────────────────────────────────────────── */

static void slot_b_init_zero(int32_t slot[SCENE1_RECORDS_B_STRIDE])
{
    memset(slot, 0, sizeof(int32_t) * SCENE1_RECORDS_B_STRIDE);
}

static void slot_b_set_float(int32_t slot[SCENE1_RECORDS_B_STRIDE],
                             int offset, float value)
{
    memcpy(&slot[offset], &value, sizeof(value));
}

/* main predicate */

int test_scene1_pass_b_main_rejects_non_8c_types(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    int reject_types[] = { 0x00, 0x01, 0x8b, 0x8d, 0x9b, 0x9c, 0xa0, 0xff };
    for (int i = 0; i < (int)(sizeof(reject_types)/sizeof(reject_types[0])); i++) {
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject_types[i];
        if (sw_pass_b_should_emit_main(slot) != 0)
            T_FAIL("type 0x%x should not emit (main)", reject_types[i]);
    }
    return 0;
}

int test_scene1_pass_b_main_accepts_8c_when_part_idx_even(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x8c;

    slot[SCENE1_RECORDS_B_OFF_PART_IDX] = 0;
    T_ASSERT(sw_pass_b_should_emit_main(slot) == 1);
    slot[SCENE1_RECORDS_B_OFF_PART_IDX] = 2;
    T_ASSERT(sw_pass_b_should_emit_main(slot) == 1);
    slot[SCENE1_RECORDS_B_OFF_PART_IDX] = 8;
    T_ASSERT(sw_pass_b_should_emit_main(slot) == 1);
    return 0;
}

int test_scene1_pass_b_main_rejects_8c_when_part_idx_odd(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x8c;

    slot[SCENE1_RECORDS_B_OFF_PART_IDX] = 1;
    T_ASSERT(sw_pass_b_should_emit_main(slot) == 0);
    slot[SCENE1_RECORDS_B_OFF_PART_IDX] = 3;
    T_ASSERT(sw_pass_b_should_emit_main(slot) == 0);
    slot[SCENE1_RECORDS_B_OFF_PART_IDX] = 7;
    T_ASSERT(sw_pass_b_should_emit_main(slot) == 0);
    return 0;
}

/* outer predicate */

int test_scene1_pass_b_outer_accepts_9b_and_9c(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);

    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    T_ASSERT(sw_pass_b_should_emit_outer(slot) == 1);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9c;
    T_ASSERT(sw_pass_b_should_emit_outer(slot) == 1);
    return 0;
}

int test_scene1_pass_b_outer_rejects_other_types(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    int reject_types[] = { 0x00, 0x8c, 0x9a, 0x9d, 0xa0, 0xf7, 0xf8, 0xff };
    for (int i = 0; i < (int)(sizeof(reject_types)/sizeof(reject_types[0])); i++) {
        slot[SCENE1_RECORDS_B_OFF_TYPE] = reject_types[i];
        if (sw_pass_b_should_emit_outer(slot) != 0)
            T_FAIL("type 0x%x should not emit (outer)", reject_types[i]);
    }
    return 0;
}

/* main matrix composer */

int test_scene1_pass_b_main_compose_propagates_matrix0(void)
{
    /* If MATRIX0 is identity and POS=(0,0,0), ROT_X=0, scale=0 (s=0),
     * the chain reduces to Identity × Rx(0) × S(0) × T(0) = S(0).
     * With LIFE_MULT=0, scale=0 ⇒ row 0/1/2 = 0; row 3 = (0,0,0,1). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x8c;
    /* MATRIX0 identity at offset 50 */
    float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
    memcpy(&slot[SCENE1_RECORDS_B_OFF_MATRIX0], identity, sizeof(identity));
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_POS_X, 1.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_POS_Y, 2.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_POS_Z, 3.0f);

    float M[16];
    sw_pass_b_compose_world_main(M, slot);

    /* scale = 1.0 * 0.06 = 0.06 → diag (-0.06, 0.06, 0.06).
     * MATRIX0=I means the chain is unchanged by the final mul. */
    T_ASSERT_NEAR_D(M[0],  -0.06f, 1e-6f);
    T_ASSERT_NEAR_D(M[5],   0.06f, 1e-6f);
    T_ASSERT_NEAR_D(M[10],  0.06f, 1e-6f);
    T_ASSERT_NEAR_D(M[12],  1.0f,  1e-6f);
    T_ASSERT_NEAR_D(M[13],  2.0f,  1e-6f);
    T_ASSERT_NEAR_D(M[14],  3.0f,  1e-6f);
    return 0;
}

int test_scene1_pass_b_main_compose_matrix0_translates(void)
{
    /* Set MATRIX0 to a pure translation T(10,20,30) (row 3 = 10,20,30,1).
     * If slot POS=(0,0,0), the inner chain is just S × identity = S,
     * and Multiply(out, MATRIX0, out) = MATRIX0 × S = a row-major
     * matrix whose row 3 stays unchanged from MATRIX0's row 3 (translation
     * survives the right-multiply by S).
     */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x8c;
    float t_mat[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        10, 20, 30, 1,
    };
    memcpy(&slot[SCENE1_RECORDS_B_OFF_MATRIX0], t_mat, sizeof(t_mat));
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);

    float M[16];
    sw_pass_b_compose_world_main(M, slot);

    /* MATRIX0_translation × (Rx(0) × S × T(0,0,0)):
     * inner = S(-0.06, 0.06, 0.06) (4×4 with diag scale, row 3 = 0).
     * MATRIX0_t × inner: row 3 of (MATRIX0_t × inner) = MATRIX0_t.row3 × inner
     *   = (10, 20, 30, 1) × inner = (10*-0.06, 20*0.06, 30*0.06, 1) =
     *     (-0.6, 1.2, 1.8, 1).
     */
    T_ASSERT_NEAR_D(M[12], -0.6f, 1e-6f);
    T_ASSERT_NEAR_D(M[13],  1.2f, 1e-6f);
    T_ASSERT_NEAR_D(M[14],  1.8f, 1e-6f);
    return 0;
}

int test_scene1_pass_b_main_compose_scale_uses_0_06_factor(void)
{
    /* LIFE_MULT * 0.06 — verifies the .rdata 0x519d6c constant. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x8c;
    float identity[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    memcpy(&slot[SCENE1_RECORDS_B_OFF_MATRIX0], identity, sizeof(identity));
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);

    float M[16];
    sw_pass_b_compose_world_main(M, slot);

    /* scale = 2.0 * 0.06 = 0.12 */
    T_ASSERT_NEAR_D(M[0],  -0.12f, 1e-6f);
    T_ASSERT_NEAR_D(M[5],   0.12f, 1e-6f);
    T_ASSERT_NEAR_D(M[10],  0.12f, 1e-6f);
    return 0;
}

/* outer matrix composer */

int test_scene1_pass_b_outer_compose_scale_uses_0_05_factor(void)
{
    /* LIFE_MULT * 0.05 — verifies the .rdata 0x5198f8 constant.  No
     * MATRIX0 multiply for the outer body. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    /* POS=0, ROT_X=0, ROT_SCR=0 → only S × T(0) = S survives. */

    float M[16];
    sw_pass_b_compose_world_outer(M, slot);

    /* scale = 2.0 * 0.05 = 0.10 */
    T_ASSERT_NEAR_D(M[0],  -0.10f, 1e-6f);
    T_ASSERT_NEAR_D(M[5],   0.10f, 1e-6f);
    T_ASSERT_NEAR_D(M[10],  0.10f, 1e-6f);
    return 0;
}

int test_scene1_pass_b_outer_compose_translation_in_row_3(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9c;
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_POS_X, 5.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_POS_Y, 6.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_POS_Z, 7.0f);

    float M[16];
    sw_pass_b_compose_world_outer(M, slot);

    /* Translation survives all rotation/scaling left-mults — its row
     * stays in row 3. */
    T_ASSERT_NEAR_D(M[12], 5.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[13], 6.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[14], 7.0f, 1e-6f);
    T_ASSERT_NEAR_D(M[15], 1.0f, 1e-6f);
    return 0;
}

int test_scene1_pass_b_outer_compose_rot_x_negates(void)
{
    /* Outer body uses RotX(-ROT_X) (engine asm: fld then fchs).
     * With LIFE_MULT=5 → scale=0.25 (diag -0.25, 0.25, 0.25).
     * ROT_X = π/2 → engine builds Rx(-π/2).  Result row 1 = (0, 0, -1),
     * row 2 = (0, 1, 0) — opposite sign from Pass D's Rx(+ROT_X). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_LIFE_MULT, 20.0f);
    slot_b_set_float(slot, SCENE1_RECORDS_B_OFF_ROT_X, 1.5707963267948966f);
    /* ROT_SCR=0 → Ry(0) = identity. */

    float M[16];
    sw_pass_b_compose_world_outer(M, slot);

    /* scale = 20 * 0.05 = 1.0 (clean diag).
     * After Rx(-π/2) × S(-1, 1, 1):
     *   row 0 = (-1, 0, 0)
     *   row 1 = (0, cos(-π/2)*1, sin(-π/2)*1) = (0, 0, -1)
     *   row 2 = (0, -sin(-π/2)*1, cos(-π/2)*1) = (0, 1, 0)
     */
    T_ASSERT_NEAR_D(M[0], -1.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[5],  0.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[6], -1.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[9],  1.0f, 1e-5f);
    T_ASSERT_NEAR_D(M[10], 0.0f, 1e-5f);
    return 0;
}

/* spoke pose */

int test_scene1_pass_b_spoke_pose_default_age_radius_is_0_1(void)
{
    /* AGE below both thresholds (0x9b: 60, 0x9c: 20) → default
     * radius 0.1, angle = spoke_idx * π/2. */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 60;  /* exactly threshold → no extra */

    float radius, angle;
    for (int i = 0; i < 4; i++) {
        sw_pass_b_spoke_pose(&radius, &angle, slot, i);
        T_ASSERT_NEAR_D(radius, 0.1f, 1e-6f);
        T_ASSERT_NEAR_D(angle, (float)i * 1.5707963267948966f, 1e-6f);
    }
    return 0;
}

int test_scene1_pass_b_spoke_pose_type_9b_grows_past_60(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 70;  /* extra = 10 */

    float radius, angle;
    sw_pass_b_spoke_pose(&radius, &angle, slot, 0);
    /* extra = 10 → radius = 10 * 0.1 = 1.0; angle = 0 + 10 * 0.2 = 2.0. */
    T_ASSERT_NEAR_D(radius, 1.0f, 1e-6f);
    T_ASSERT_NEAR_D(angle,  2.0f, 1e-6f);

    sw_pass_b_spoke_pose(&radius, &angle, slot, 2);
    /* base = 2 * π/2 = π; extra adds 2.0. */
    T_ASSERT_NEAR_D(radius, 1.0f, 1e-6f);
    T_ASSERT_NEAR_D(angle,  3.14159265358979f + 2.0f, 1e-5f);
    return 0;
}

int test_scene1_pass_b_spoke_pose_type_9c_grows_past_20(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9c;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 30;  /* extra = 10 */

    float radius, angle;
    sw_pass_b_spoke_pose(&radius, &angle, slot, 1);
    /* base = 1 * π/2; extra=10 → radius=1.0, angle += 2.0. */
    T_ASSERT_NEAR_D(radius, 1.0f, 1e-6f);
    T_ASSERT_NEAR_D(angle, 1.5707963267948966f + 2.0f, 1e-6f);
    return 0;
}

int test_scene1_pass_b_spoke_pose_radius_caps_at_2_5(void)
{
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 200;  /* extra = 140 → unclamped radius = 14 */

    float radius, angle;
    sw_pass_b_spoke_pose(&radius, &angle, slot, 0);
    T_ASSERT_NEAR_D(radius, 2.5f, 1e-6f);
    /* Angle is unaffected by the radius clamp. */
    T_ASSERT_NEAR_D(angle, 0.0f + 140.0f * 0.2f, 1e-4f);
    return 0;
}

/* spoke compose */

int test_scene1_pass_b_spoke_compose_uses_70_z(void)
{
    /* With outer = identity, default radius 0.1 + spoke_idx=0 ⇒
     * angle=0; sin(0)=0, cos(0)=1; tx = 0/0.05 = 0, ty = 0.1/0.05 = 2.0,
     * tz = 70.0.  spoke_world = T(0, 2.0, 70) × I = T(0, 2.0, 70). */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 50;  /* < 60 → default radius */

    float outer[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1,
    };
    float spoke[16];
    sw_pass_b_compose_world_spoke(spoke, outer, slot, 0);

    T_ASSERT_NEAR_D(spoke[12], 0.0f,  1e-5f);
    T_ASSERT_NEAR_D(spoke[13], 2.0f,  1e-5f);
    T_ASSERT_NEAR_D(spoke[14], 70.0f, 1e-5f);
    return 0;
}

int test_scene1_pass_b_spoke_compose_uses_outer_matrix(void)
{
    /* outer is a pure translation T(100, 200, 300).
     * t_spoke × outer applies t_spoke first (in row-vec semantics), then
     * outer.  Row 3 of (t_spoke × outer) = t_spoke.row3 × outer + ...
     * but since both are translations, the result row 3 = sum of
     * translations = (t_spoke.tx + 100, t_spoke.ty + 200, 70 + 300).
     */
    int32_t slot[SCENE1_RECORDS_B_STRIDE];
    slot_b_init_zero(slot);
    slot[SCENE1_RECORDS_B_OFF_TYPE] = 0x9b;
    slot[SCENE1_RECORDS_B_OFF_AGE]  = 50;  /* < 60 → default radius 0.1 */

    float outer[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        100, 200, 300, 1,
    };
    float spoke[16];
    sw_pass_b_compose_world_spoke(spoke, outer, slot, 0);

    /* spoke_idx 0, default: tx = 0, ty = 2.0, tz = 70.0. */
    T_ASSERT_NEAR_D(spoke[12], 100.0f, 1e-5f);
    T_ASSERT_NEAR_D(spoke[13], 202.0f, 1e-5f);
    T_ASSERT_NEAR_D(spoke[14], 370.0f, 1e-5f);
    return 0;
}
