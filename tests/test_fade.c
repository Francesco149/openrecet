/*
 * test_fade.c — counter machinery for the scene fade-out.
 *
 * Covers the four pure-C functions in src/fade.c:
 *   fade_phase1_start  — FUN_004526f5 init
 *   fade_phase_out_start — FUN_0045281c init
 *   fade_tick          — FUN_004526ab per-tick advance
 *   fade_is_done       — FUN_004528b3 done-query
 *
 * The alpha-quad renderer (fade_render) is Win32-only and not exercised
 * here; faithful operation is verified by the harness comparison.
 */
#include "t.h"
#include "fade.h"

int test_fade_reset_zeroes_state(void)
{
    fade_phase1_start(1, 0x11);
    fade_reset();
    T_ASSERT_EQ_I(g_fade_counter,  0);
    T_ASSERT_EQ_I(g_fade_phase,    0);
    T_ASSERT_EQ_I(g_fade_mode,     0);
    T_ASSERT_EQ_I(g_fade_duration, 0);
    return 0;
}

int test_fade_phase1_start_seeds_state(void)
{
    fade_reset();
    fade_phase1_start(0, 0x11);
    T_ASSERT_EQ_I(g_fade_counter,  1);
    T_ASSERT_EQ_I(g_fade_phase,    1);
    T_ASSERT_EQ_I(g_fade_mode,     0);
    T_ASSERT_EQ_I(g_fade_duration, 0x11);
    return 0;
}

int test_fade_phase_out_start_seeds_state(void)
{
    fade_reset();
    fade_phase_out_start(1, 0x11);
    T_ASSERT_EQ_I(g_fade_counter,  0);
    T_ASSERT_EQ_I(g_fade_phase,    -1);
    T_ASSERT_EQ_I(g_fade_mode,     1);
    T_ASSERT_EQ_I(g_fade_duration, 0x11);
    return 0;
}

int test_fade_tick_idle_is_noop(void)
{
    fade_reset();
    for (int i = 0; i < 100; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0);
    T_ASSERT_EQ_I(g_fade_phase,   0);
    return 0;
}

int test_fade_tick_phase1_clamps_at_duration_plus_one(void)
{
    /* NEW-GAME flow: phase 1, duration 17. Counter starts at 1 (set by
     * fade_phase1_start). After (duration) ticks it's at 18 = duration+1,
     * then ticks no further. */
    fade_reset();
    fade_phase1_start(0, 0x11);
    T_ASSERT_EQ_I(g_fade_counter, 1);
    for (int i = 0; i < 0x11; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x12);  /* 1 + 17 = 18 = duration+1 */
    /* Further ticks pinned. */
    for (int i = 0; i < 10; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x12);
    T_ASSERT_EQ_I(g_fade_phase,   1);
    return 0;
}

int test_fade_tick_phase_out_resets_at_end(void)
{
    /* Phase -1, duration 17. Counter starts at 0. After duration+1 ticks
     * (because the check is `counter > duration`, the reset fires when
     * counter would become duration+1) the state resets to fully idle. */
    fade_reset();
    fade_phase_out_start(0, 0x11);
    T_ASSERT_EQ_I(g_fade_counter, 0);
    /* 17 ticks → counter 17 = duration, not yet past. */
    for (int i = 0; i < 0x11; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x11);
    T_ASSERT_EQ_I(g_fade_phase,   -1);
    /* One more tick → counter 18 > duration → reset to idle. */
    fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0);
    T_ASSERT_EQ_I(g_fade_phase,   0);
    /* Further ticks are no-ops. */
    for (int i = 0; i < 10; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0);
    return 0;
}

int test_fade_is_done_idle_returns_zero(void)
{
    fade_reset();
    T_ASSERT_EQ_I(fade_is_done(), 0);
    return 0;
}

int test_fade_is_done_phase1_matches_duration(void)
{
    fade_reset();
    fade_phase1_start(0, 0x11);
    /* Not done until counter hits duration exactly. */
    T_ASSERT_EQ_I(fade_is_done(), 0);  /* counter==1 */
    /* Tick (duration-1) times → counter==duration. */
    for (int i = 0; i < 0x10; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x11);
    T_ASSERT_EQ_I(fade_is_done(), 1);
    /* One more tick → counter goes to duration+1; engine's done-test
     * checks `counter == duration` strictly, so the latch reverts. */
    fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x12);
    T_ASSERT_EQ_I(fade_is_done(), 0);
    return 0;
}

int test_fade_is_done_phase_out_never_returns_one(void)
{
    /* Phase -1 fades never satisfy fade_is_done — that check is
     * exclusively for phase-1 (the "scene start" half). */
    fade_reset();
    fade_phase_out_start(0, 0x11);
    for (int i = 0; i < 0x20; i++) {
        T_ASSERT_EQ_I(fade_is_done(), 0);
        fade_tick();
    }
    return 0;
}

int test_fade_is_done_mode2_uses_0x1f_pin(void)
{
    /* Engine quirk: mode 2 ignores duration and latches done at
     * counter == 0x1f. Duration is still respected by the tick clamp;
     * a typical caller would pass duration=0x1e so the clamp pins at
     * duration+1 = 0x1f and stays. */
    fade_reset();
    fade_phase1_start(2, 0x1e);
    T_ASSERT_EQ_I(fade_is_done(), 0);
    for (int i = 0; i < 0x1e; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x1f);
    T_ASSERT_EQ_I(fade_is_done(), 1);
    /* Pinned. */
    for (int i = 0; i < 10; i++) fade_tick();
    T_ASSERT_EQ_I(g_fade_counter, 0x1f);
    T_ASSERT_EQ_I(fade_is_done(), 1);
    return 0;
}
