/*
 * test_scene1_player_ctrl.c — Cpop.1 coverage.
 *
 * Exercises the pure leaf math of the HOUSE per-frame player controller
 * (engine FUN_0048b850): the 8→4 facing-octant snap with its sticky
 * horizontal bias, the camera zoom-bias decay + floor clamp, and the
 * camera-shake magnitude clamp.  The stateful controller body itself is
 * dormant (its caller chain FUN_00442cef→FUN_0048670f is unported) and not
 * host-testable; these leaves are where the decoded branch structure lives.
 */
#include "t.h"

#include <math.h>

#include "scene1_player_ctrl.h"

static int near_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b));
}
#define T_ASSERT_NEAR(a, b) do { \
    float _a = (a), _b = (b); \
    if (!near_f(_a, _b)) \
        T_FAIL("expected %s ≈ %s (got %.7f, want %.7f)", #a, #b, _a, _b); \
} while (0)

/* ── player_ctrl_facing_snap ─────────────────────────────────────────────── */

int test_player_facing_snap_horizontal_sets_sticky(void)
{
    int s = 0;
    /* octant 2 passes through AND sets the sticky bias to 1. */
    if (player_ctrl_facing_snap(2, &s) != 2) T_FAIL("oct2 should stay 2");
    if (s != 1) T_FAIL("oct2 should set sticky=1");
    s = 0;
    if (player_ctrl_facing_snap(6, &s) != 6) T_FAIL("oct6 should stay 6");
    if (s != 1) T_FAIL("oct6 should set sticky=1");
    return 0;
}

int test_player_facing_snap_vertical_clears_sticky(void)
{
    int s = 1;
    /* octant 0 / 4 pass through AND clear the sticky bias to 0. */
    if (player_ctrl_facing_snap(0, &s) != 0) T_FAIL("oct0 should stay 0");
    if (s != 0) T_FAIL("oct0 should clear sticky");
    s = 1;
    if (player_ctrl_facing_snap(4, &s) != 4) T_FAIL("oct4 should stay 4");
    if (s != 0) T_FAIL("oct4 should clear sticky");
    return 0;
}

int test_player_facing_snap_diagonals_sticky0(void)
{
    /* sticky=0 (right/down... vertical-biased): {1,7}→0, {3,5}→4. */
    int s = 0; if (player_ctrl_facing_snap(1, &s) != 0) T_FAIL("1→0");
    s = 0;     if (player_ctrl_facing_snap(7, &s) != 0) T_FAIL("7→0");
    s = 0;     if (player_ctrl_facing_snap(3, &s) != 4) T_FAIL("3→4");
    s = 0;     if (player_ctrl_facing_snap(5, &s) != 4) T_FAIL("5→4");
    /* diagonals must NOT change the sticky bit. */
    s = 0;     player_ctrl_facing_snap(1, &s);
    if (s != 0) T_FAIL("diagonal must not touch sticky");
    return 0;
}

int test_player_facing_snap_diagonals_sticky1(void)
{
    /* sticky=1 (horizontal-biased): {1,3}→2, {5,7}→6. */
    int s = 1; if (player_ctrl_facing_snap(1, &s) != 2) T_FAIL("1→2");
    s = 1;     if (player_ctrl_facing_snap(3, &s) != 2) T_FAIL("3→2");
    s = 1;     if (player_ctrl_facing_snap(5, &s) != 6) T_FAIL("5→6");
    s = 1;     if (player_ctrl_facing_snap(7, &s) != 6) T_FAIL("7→6");
    s = 1;     player_ctrl_facing_snap(5, &s);
    if (s != 1) T_FAIL("diagonal must not touch sticky");
    return 0;
}

int test_player_facing_snap_masks_input(void)
{
    /* high bits are masked off (& 7); 0xA == 2. */
    int s = 0;
    if (player_ctrl_facing_snap(0xA, &s) != 2) T_FAIL("0xA & 7 == 2");
    if (s != 1) T_FAIL("0xA→oct2 sets sticky");
    return 0;
}

/* ── player_ctrl_camera_z_decay ──────────────────────────────────────────── */

int test_player_cam_z_decay_step(void)
{
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(0.0f), -0.03f);
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(1.0f), 0.97f);
    return 0;
}

int test_player_cam_z_decay_floor(void)
{
    /* already at/below the -2.0 floor → clamped, never runs away. */
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(-1.99f), -2.0f);
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(-5.0f),  -2.0f);
    return 0;
}

/* ── player_ctrl_camera_shake_clamp ──────────────────────────────────────── */

int test_player_cam_shake_below_target_noop(void)
{
    /* magnitude (0.3) < target (0.5) → untouched (engine guard target<=mag). */
    float x = 0.3f, y = 0.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 0.5f);
    T_ASSERT_NEAR(x, 0.3f);
    T_ASSERT_NEAR(y, 0.0f);
    return 0;
}

int test_player_cam_shake_above_target_scaled(void)
{
    /* magnitude (5.0 along x) > target (1.0) → scaled to length 1.0. */
    float x = 5.0f, y = 0.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 1.0f);
    T_ASSERT_NEAR(x, 1.0f);
    T_ASSERT_NEAR(y, 0.0f);

    /* 3-4-5 triangle: mag 5, target 2.5 → halve both components. */
    x = 3.0f; y = 4.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 2.5f);
    T_ASSERT_NEAR(x, 1.5f);
    T_ASSERT_NEAR(y, 2.0f);
    return 0;
}

int test_player_cam_shake_zero_mag_no_div(void)
{
    /* mag==0, target>0 → guard false, no division, stays zero. */
    float x = 0.0f, y = 0.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 0.175f);
    T_ASSERT_NEAR(x, 0.0f);
    T_ASSERT_NEAR(y, 0.0f);
    return 0;
}
