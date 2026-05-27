/*
 * tests/test_scene1_dungeon_clear_banner.c — covers FUN_0048fe43 port.
 *
 * Focuses on the pure-C surface (no D3D device required):
 *   - Y-position two-phase ramp + clamps
 *   - U-slice cascade (engine's literal 1/2/else cascade)
 *   - State get/set/reset round-trips
 */

#include "t.h"

#include <math.h>

#include "scene1_dungeon_clear_banner.h"

#define T_ASSERT_EQ_F(a, b) \
    T_ASSERT(fabsf((float)(a) - (float)(b)) < 1e-4f)

/* ─── Y position — phase 1 (counter <= 119) ─────────────────────────── */

int test_banner_compute_y_counter_0_is_off_screen(void)
{
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(0), -120.0f);
    return 0;
}

int test_banner_compute_y_counter_1(void)
{
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(1), -118.0f);
    return 0;
}

int test_banner_compute_y_counter_60_mid_ramp(void)
{
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(60), 0.0f);
    return 0;
}

int test_banner_compute_y_clamps_at_96_when_overshoot(void)
{
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(108), 96.0f);
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(119), 96.0f);
    return 0;
}

/* ─── Y position — phase 2 (counter > 119) ──────────────────────────── */

int test_banner_compute_y_phase2_at_120_still_clamped(void)
{
    /* counter=120: 96 - (120*3-360)*2 = 96 - 0 = 96 (hand-off frame) */
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(120), 96.0f);
    return 0;
}

int test_banner_compute_y_phase2_starts_descending(void)
{
    /* counter=121: 96 - 6 = 90 */
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(121), 90.0f);
    /* counter=132: 96 - 72 = 24 */
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(132), 24.0f);
    return 0;
}

int test_banner_compute_y_phase2_clamps_at_minus_48(void)
{
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(144), -48.0f);
    T_ASSERT_EQ_F(scene1_dungeon_clear_banner_compute_y(200), -48.0f);
    return 0;
}

/* ─── U slice picker — engine literal cascade ───────────────────────── */

int test_banner_compute_u_slice_0_default_strip(void)
{
    float u0 = -1.0f, u1 = -1.0f;
    scene1_dungeon_clear_banner_compute_u(0, &u0, &u1);
    T_ASSERT_EQ_F(u0,   0.0f);
    T_ASSERT_EQ_F(u1, 128.0f);
    return 0;
}

int test_banner_compute_u_slice_1_right_strip(void)
{
    float u0 = -1.0f, u1 = -1.0f;
    scene1_dungeon_clear_banner_compute_u(1, &u0, &u1);
    T_ASSERT_EQ_F(u0, 256.0f);
    T_ASSERT_EQ_F(u1, 384.0f);
    return 0;
}

int test_banner_compute_u_slice_2_middle_strip(void)
{
    /* slice 2 → MIDDLE (u=128..256) — engine's literal cascade order. */
    float u0 = -1.0f, u1 = -1.0f;
    scene1_dungeon_clear_banner_compute_u(2, &u0, &u1);
    T_ASSERT_EQ_F(u0, 128.0f);
    T_ASSERT_EQ_F(u1, 256.0f);
    return 0;
}

int test_banner_compute_u_unknown_slice_falls_to_default(void)
{
    float u0 = 0, u1 = 0;
    scene1_dungeon_clear_banner_compute_u(3, &u0, &u1);
    T_ASSERT_EQ_F(u0, 0.0f); T_ASSERT_EQ_F(u1, 128.0f);

    scene1_dungeon_clear_banner_compute_u(-5, &u0, &u1);
    T_ASSERT_EQ_F(u0, 0.0f); T_ASSERT_EQ_F(u1, 128.0f);

    scene1_dungeon_clear_banner_compute_u(99, &u0, &u1);
    T_ASSERT_EQ_F(u0, 0.0f); T_ASSERT_EQ_F(u1, 128.0f);
    return 0;
}

int test_banner_compute_u_null_pointers_safe(void)
{
    scene1_dungeon_clear_banner_compute_u(0, NULL, NULL);
    scene1_dungeon_clear_banner_compute_u(1, NULL, NULL);
    float u = 42.0f;
    scene1_dungeon_clear_banner_compute_u(1, &u, NULL);
    T_ASSERT_EQ_F(u, 256.0f);
    return 0;
}

/* ─── state accessors ───────────────────────────────────────────────── */

int test_banner_state_defaults_zero(void)
{
    scene1_dungeon_clear_banner_reset();
    T_ASSERT_EQ_I(scene1_dungeon_clear_banner_get_counter(), 0);
    T_ASSERT_EQ_I(scene1_dungeon_clear_banner_get_slice(),   0);
    return 0;
}

int test_banner_set_get_round_trip(void)
{
    scene1_dungeon_clear_banner_set_counter(42);
    scene1_dungeon_clear_banner_set_slice(2);
    T_ASSERT_EQ_I(scene1_dungeon_clear_banner_get_counter(), 42);
    T_ASSERT_EQ_I(scene1_dungeon_clear_banner_get_slice(),   2);

    scene1_dungeon_clear_banner_reset();
    T_ASSERT_EQ_I(scene1_dungeon_clear_banner_get_counter(), 0);
    T_ASSERT_EQ_I(scene1_dungeon_clear_banner_get_slice(),   0);
    return 0;
}
