/*
 * test_nowloading.c — pure-C state machine of the "Now Loading…"
 * overlay (FUN_00453147). The D3D render path is Win32-only and not
 * exercised here; we cover the gate semantics + alpha-decay /
 * rotation tick.
 */
#include "t.h"
#include "nowloading.h"

int test_nowloading_reset_zeroes_state(void)
{
    nowloading_set_active(1);
    nowloading_tick();  /* advance rotation off zero */
    T_ASSERT(nowloading_get_rotation() > 0.0f);

    nowloading_reset();
    T_ASSERT_EQ_I(nowloading_get_alpha_counter(), 0);
    T_ASSERT_EQ_I(nowloading_is_active(),         0);
    T_ASSERT_EQ_I((int)(nowloading_get_rotation() * 1000.0f), 0);
    return 0;
}

int test_nowloading_set_active_normalises_to_zero_one(void)
{
    nowloading_reset();
    nowloading_set_active(42);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);
    nowloading_set_active(0);
    T_ASSERT_EQ_I(nowloading_is_active(), 0);
    nowloading_set_active(-1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);
    return 0;
}

int test_nowloading_tick_idle_decays_alpha_by_32(void)
{
    /* Mirrors engine L17-22: when gate is 0, alpha drops by 0x20 per
     * frame. There's no public setter for the alpha counter, but a
     * fresh reset leaves it at 0, so we use that as the floor case and
     * focus on the clamp behaviour. */
    nowloading_reset();
    T_ASSERT_EQ_I(nowloading_get_alpha_counter(), 0);
    /* Tick while gate is 0 — counter stays clamped at 0. */
    for (int i = 0; i < 10; i++) {
        int prev = nowloading_tick();
        T_ASSERT_EQ_I(prev, 0);
        T_ASSERT_EQ_I(nowloading_get_alpha_counter(), 0);
    }
    return 0;
}

int test_nowloading_tick_active_advances_rotation_by_0_3(void)
{
    nowloading_reset();
    nowloading_set_active(1);
    /* Each tick adds 0.3. Run 10 ticks → 3.0 ± float-fudge. */
    for (int i = 0; i < 10; i++) {
        int prev = nowloading_tick();
        T_ASSERT_EQ_I(prev, 1);
    }
    float r = nowloading_get_rotation();
    /* Allow ±0.001 for accumulated float error over 10 0.3 adds. */
    T_ASSERT(r > 2.999f && r < 3.001f);
    return 0;
}

int test_nowloading_tick_active_does_not_decay_alpha(void)
{
    /* When the gate is set, the alpha-decay branch is not entered.
     * Counter stays put. */
    nowloading_reset();
    nowloading_set_active(1);
    for (int i = 0; i < 20; i++) nowloading_tick();
    T_ASSERT_EQ_I(nowloading_get_alpha_counter(), 0);
    return 0;
}

int test_nowloading_tick_idle_does_not_advance_rotation(void)
{
    nowloading_reset();
    /* Pre-spin to non-zero. */
    nowloading_set_active(1);
    nowloading_tick();
    float r0 = nowloading_get_rotation();
    T_ASSERT(r0 > 0.29f && r0 < 0.31f);
    /* Now turn off and tick — rotation must NOT advance. */
    nowloading_set_active(0);
    for (int i = 0; i < 10; i++) nowloading_tick();
    float r1 = nowloading_get_rotation();
    T_ASSERT_EQ_I((int)(r0 * 1000.0f), (int)(r1 * 1000.0f));
    return 0;
}

int test_nowloading_tick_returns_previous_gate_state(void)
{
    nowloading_reset();
    /* First tick: gate is 0 → return 0. */
    T_ASSERT_EQ_I(nowloading_tick(), 0);
    nowloading_set_active(1);
    /* Now: gate is 1 → return 1. */
    T_ASSERT_EQ_I(nowloading_tick(), 1);
    T_ASSERT_EQ_I(nowloading_tick(), 1);
    nowloading_set_active(0);
    T_ASSERT_EQ_I(nowloading_tick(), 0);
    return 0;
}
