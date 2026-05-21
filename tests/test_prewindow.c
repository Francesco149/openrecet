/*
 * test_prewindow.c — FUN_00451790 port verification.
 *
 * Checks all six named globals, the object table init pattern, the
 * particle randomization (with deterministic seed=1, matching engine
 * boot), and the matrix outputs (degenerate view → NaN; proj is finite).
 */

#include "t.h"
#include "prewindow.h"
#include "rng.h"

#include <math.h>

int test_prewindow_named_globals_set(void)
{
    rng_seed(1);
    prewindow_init();

    T_ASSERT_EQ_I(g_prewindow.flag_b1c4, 0);
    T_ASSERT_EQ_I(g_prewindow.flag_b8cc, 0);
    T_ASSERT_EQ_I(g_prewindow.flag_b1c0, 1);
    T_ASSERT_EQ_I(g_prewindow.flag_bf84, 0);
    T_ASSERT_EQ_I(g_prewindow.flag_bf88, 0);

    if (g_prewindow.camera[0] != 10.0f)  T_FAIL("camera.x=%f want 10", g_prewindow.camera[0]);
    if (g_prewindow.camera[1] != 61.0f)  T_FAIL("camera.y=%f want 61", g_prewindow.camera[1]);
    if (g_prewindow.camera[2] != -203.0f) T_FAIL("camera.z=%f want -203", g_prewindow.camera[2]);
    return 0;
}

int test_prewindow_object_table_y_set_first_last(void)
{
    rng_seed(1);
    prewindow_init();

    if (g_prewindow.objects[0].y != 1.0f)
        T_FAIL("objects[0].y=%f want 1.0", g_prewindow.objects[0].y);
    if (g_prewindow.objects[PREWINDOW_OBJECT_COUNT - 1].y != 1.0f)
        T_FAIL("objects[last].y=%f want 1.0",
               g_prewindow.objects[PREWINDOW_OBJECT_COUNT - 1].y);
    if (g_prewindow.objects[4321].y != 1.0f)
        T_FAIL("objects[mid].y=%f want 1.0", g_prewindow.objects[4321].y);
    return 0;
}

int test_prewindow_object_table_other_fields_zero(void)
{
    rng_seed(1);
    prewindow_init();

    /* field0 and field12 explicitly written to 0; the other dwords
     * remain BSS-zero (we initialise the whole struct to zero before
     * call, so all uninit fields read as 0 in the test). */
    for (int i = 0; i < 16; i++) {   /* sample first 16 entries */
        if (g_prewindow.objects[i].field0  != 0.0f) T_FAIL("objects[%d].field0", i);
        if (g_prewindow.objects[i].pad08   != 0.0f) T_FAIL("objects[%d].pad08", i);
        if (g_prewindow.objects[i].field12 != 0.0f) T_FAIL("objects[%d].field12", i);
    }
    return 0;
}

int test_prewindow_particles_alive_flag_all_one(void)
{
    rng_seed(1);
    prewindow_init();
    for (int i = 0; i < PREWINDOW_PARTICLE_COUNT; i++) {
        if (g_prewindow.particle_alive[i] != 1)
            T_FAIL("particle[%d].alive=%d want 1", i, g_prewindow.particle_alive[i]);
    }
    return 0;
}

int test_prewindow_particles_pos_within_expected_range(void)
{
    /* After init: pos.x/y in (rand-0.5)*20 then halved → range (-5, 5).
     *             pos.z in (rand+2.5)*-10 then halved → range (-17.5, -12.5).
     *             rot.x/y/z in (rand-0.5)*0.31415927 then halved → ±0.078. */
    rng_seed(1);
    prewindow_init();

    for (int i = 0; i < PREWINDOW_PARTICLE_COUNT; i++) {
        float px = g_prewindow.particle_pos[i][0];
        float py = g_prewindow.particle_pos[i][1];
        float pz = g_prewindow.particle_pos[i][2];
        if (px < -5.0f || px >= 5.0f) T_FAIL("particle[%d].pos.x=%f", i, px);
        if (py < -5.0f || py >= 5.0f) T_FAIL("particle[%d].pos.y=%f", i, py);
        if (pz < -17.5f || pz >= -12.5f) T_FAIL("particle[%d].pos.z=%f", i, pz);

        float rx = g_prewindow.particle_rot[i][0];
        float ry = g_prewindow.particle_rot[i][1];
        float rz = g_prewindow.particle_rot[i][2];
        float rotbound = 0.31415927f * 0.5f;
        if (rx < -rotbound || rx >= rotbound) T_FAIL("particle[%d].rot.x=%f", i, rx);
        if (ry < -rotbound || ry >= rotbound) T_FAIL("particle[%d].rot.y=%f", i, ry);
        if (rz < -rotbound || rz >= rotbound) T_FAIL("particle[%d].rot.z=%f", i, rz);
    }
    return 0;
}

int test_prewindow_particle_zero_deterministic_from_seed_1(void)
{
    /* With seed=1 the very first six rng_next_unit calls produce:
     *   u0 = 41/32768       (≈ 0.001251)
     *   u1 = 18467/32768    (≈ 0.563568)
     *   u2 = 6334/32768     (≈ 0.193298)
     *   u3 = 26500/32768    (≈ 0.808594)
     *   u4 = 19169/32768    (≈ 0.585022)
     *   u5 = 15724/32768    (≈ 0.479858)
     *
     * Particle 0 fields after the two-step (compute → halve) sequence:
     *   pos.x = (u0 - 0.5) * 20 * 0.5  = (0.001251 - 0.5) * 10 ≈ -4.987488
     *   pos.y = (u1 - 0.5) * 20 * 0.5  = (0.563568 - 0.5) * 10 ≈  0.635680
     *   pos.z = (u2 + 2.5) * -10 * 0.5 = (0.193298 + 2.5) * -5 ≈ -13.466492
     *   rot.x = (u3 - 0.5) * π/10 * 0.5 ≈ 0.048527
     *   rot.y = (u4 - 0.5) * π/10 * 0.5 ≈ 0.013357
     *   rot.z = (u5 - 0.5) * π/10 * 0.5 ≈ -0.003162
     */
    rng_seed(1);
    prewindow_init();

    float px = g_prewindow.particle_pos[0][0];
    float py = g_prewindow.particle_pos[0][1];
    float pz = g_prewindow.particle_pos[0][2];

    if (fabsf(px - (-4.987488f)) > 1e-3f) T_FAIL("pos.x=%f", px);
    if (fabsf(py -   0.635681f)  > 1e-3f) T_FAIL("pos.y=%f", py);
    if (fabsf(pz - (-13.466492f)) > 1e-3f) T_FAIL("pos.z=%f", pz);
    return 0;
}

int test_prewindow_advances_rng_by_600(void)
{
    /* 100 particles × 6 rand calls each = 600 LCG steps. The RNG state
     * after prewindow_init starting from seed=1 must equal what we'd
     * get by stepping the LCG 600 times from seed=1 manually. */
    rng_seed(1);
    prewindow_init();
    uint32_t after_init = g_rng_seed;

    rng_seed(1);
    for (int i = 0; i < 600; i++) (void)rng_next15();
    T_ASSERT_EQ_U(after_init, g_rng_seed);
    return 0;
}

int test_prewindow_proj_matrix_is_finite(void)
{
    /* The lookat is degenerate (eye=target=(0,0,0)) → view is NaN/inf;
     * but the perspective call is fed concrete finite numbers, so proj
     * itself must be finite. Note however that g_prewindow.view ends up
     * being view*proj — multiplying through NaN — so we check proj
     * specifically, not view. */
    rng_seed(1);
    prewindow_init();

    /* Engine inputs: fov=π/4, aspect=4/3, near=10, far=2000.
     *   h = cot(π/8) ≈ 2.41421
     *   w = h / (4/3) ≈ 1.81066
     *   z_range = 2000/(10-2000) = -1.00502
     *   m[14] = z_range * 10 = -10.0502
     */
    if (fabsf(g_prewindow.proj[ 0] - 1.81066f) > 1e-3f) T_FAIL("proj[0]=%f",  g_prewindow.proj[0]);
    if (fabsf(g_prewindow.proj[ 5] - 2.41421f) > 1e-3f) T_FAIL("proj[5]=%f",  g_prewindow.proj[5]);
    if (fabsf(g_prewindow.proj[10] - (-1.00503f)) > 1e-3f) T_FAIL("proj[10]=%f", g_prewindow.proj[10]);
    if (g_prewindow.proj[11] != -1.0f)               T_FAIL("proj[11]=%f", g_prewindow.proj[11]);
    if (fabsf(g_prewindow.proj[14] - (-10.0503f)) > 1e-3f) T_FAIL("proj[14]=%f", g_prewindow.proj[14]);
    return 0;
}

int test_prewindow_view_is_degenerate_nan_or_inf(void)
{
    /* With eye=target=(0,0,0), zaxis tries to normalise (0,0,0) →
     * 0/0 = NaN.  Engine reproduces the same garbage; we should too. */
    rng_seed(1);
    prewindow_init();

    /* At minimum the diagonal rotation entries shouldn't be finite
     * non-zero values — they're either inf or NaN. (We don't pin the
     * exact bit pattern since 0/0 vs 0/-0 depends on the host.) */
    int saw_bad = 0;
    for (int i = 0; i < 16; i++) {
        if (isnan(g_prewindow.view[i]) || isinf(g_prewindow.view[i])) {
            saw_bad = 1;
            break;
        }
    }
    if (!saw_bad) T_FAIL("expected NaN/inf somewhere in degenerate view");
    return 0;
}
