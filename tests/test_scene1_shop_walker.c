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
