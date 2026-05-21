/*
 * test_tick.c — unit tests for the game-loop scheduler (FUN_0047be92).
 *
 * Drives `tick_step_with_now` with synthetic time stamps. The four
 * callees are mocked via the function-pointer hooks; tests count how
 * many times each fired and inspect g_tick state.
 *
 * Win32-specific glue (`tick_now_ms`, `tick_step_win32`) is built only
 * under mingw and excluded here — the scheduler math itself is pure C.
 */

#include "t.h"
#include "tick.h"

/* ─── callback mocks ─────────────────────────────────────────────────── */
static int s_input_calls;
static int s_sim_a_calls;
static int s_sim_b_calls;
static int s_render_calls;

static void m_input(void)  { s_input_calls++; }
static void m_sim_a(void)  { s_sim_a_calls++; }
static void m_sim_b(void)  { s_sim_b_calls++; }
static void m_render(void) { s_render_calls++; }

static const struct tick_callbacks k_cb = {
    .input_poll = m_input,
    .sim_a      = m_sim_a,
    .sim_b      = m_sim_b,
    .render     = m_render,
};

static void reset_counters(void) {
    s_input_calls = s_sim_a_calls = s_sim_b_calls = s_render_calls = 0;
}

/* ─── tests ──────────────────────────────────────────────────────────── */

int test_tick_init_zeros_state(void)
{
    /* Pre-dirty state. */
    g_tick.frame_count = 99;
    g_tick.prev_ms     = 12345;
    g_tick.speed       = 3;
    g_tick.state       = 2;
    tick_init();
    T_ASSERT_EQ_U(g_tick.frame_count, 0u);
    T_ASSERT_EQ_U(g_tick.prev_ms, 0u);
    T_ASSERT_EQ_I(g_tick.speed, 0);
    T_ASSERT_EQ_I(g_tick.state, 0);
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 0);
    return 0;
}

int test_tick_speed_thresholds_match_rdata(void)
{
    /* Byte-for-byte versus the values dumped from .rdata @ 0x005cbc58
     * via tools/analyze/pe.py. */
    T_ASSERT_EQ_I(g_tick_speed_thresholds[0], 0x32);
    T_ASSERT_EQ_I(g_tick_speed_thresholds[1], 0x64);
    T_ASSERT_EQ_I(g_tick_speed_thresholds[2], 0x96);
    T_ASSERT_EQ_I(g_tick_speed_thresholds[3], 0xfa);
    T_ASSERT_EQ_I(g_tick_speed_thresholds[4], 0x1f4);
    return 0;
}

int test_tick_first_frame_huge_delta_ticks_once(void)
{
    /* Boot state: prev=0, leftover=0, speed=0 → first sample of `now`
     * produces a massive delta. Engine path: input poll fires, sim runs
     * once (speed+1 = 1), render runs once. */
    tick_init();
    reset_counters();
    uint32_t sleep = 9999;
    enum tick_result r = tick_step_with_now(/*now=*/10000, /*device=*/1,
                                            &k_cb, &sleep);
    T_ASSERT_EQ_I(r, TICK_RESULT_TICKED);
    T_ASSERT_EQ_I(s_input_calls, 1);
    T_ASSERT_EQ_I(s_sim_a_calls, 1);
    T_ASSERT_EQ_I(s_sim_b_calls, 1);
    T_ASSERT_EQ_I(s_render_calls, 1);
    T_ASSERT_EQ_U(g_tick.prev_ms, 10000u);
    T_ASSERT_EQ_U(g_tick.frame_count, 1u);
    /* leftover = 30000 % 50 = 0. */
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 0);
    return 0;
}

int test_tick_speed_one_runs_sim_twice(void)
{
    tick_init();
    g_tick.pending_speed = 1;     /* 30 FPS, sim runs (1+1) = 2 times */
    reset_counters();
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(s_sim_a_calls, 2);
    T_ASSERT_EQ_I(s_sim_b_calls, 2);
    T_ASSERT_EQ_I(s_render_calls, 1);
    T_ASSERT_EQ_I(g_tick.speed, 1);    /* latched */
    return 0;
}

int test_tick_speed_four_runs_sim_five_times(void)
{
    tick_init();
    g_tick.pending_speed = 4;
    reset_counters();
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(s_sim_a_calls, 5);
    T_ASSERT_EQ_I(s_sim_b_calls, 5);
    T_ASSERT_EQ_I(s_render_calls, 1);
    return 0;
}

/* Note: there is no test for `speed == -1`. The engine's sim-loop guard
 * `if (speed != -1)` would skip the sim, but the threshold lookup
 * `table[speed]` happens BEFORE that guard and would read OOB at
 * index -1 — tripping ASan. In practice the F-key handler that writes
 * `pending_speed` only writes 0..4, so the edge case is unreachable;
 * we preserve the guard for byte-identical dispatcher logic without
 * exercising it. */

int test_tick_delayed_far_from_frame_returns_5ms_sleep(void)
{
    /* Set up steady-state: do one tick, then call again with `now`
     * 1ms later. delta_thirds = 3. threshold=50. remaining=47. 47>=11
     * → engine returns Sleep(5). */
    tick_init();
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    reset_counters();
    uint32_t sleep = 0;
    enum tick_result r = tick_step_with_now(10001, 1, &k_cb, &sleep);
    T_ASSERT_EQ_I(r, TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 5u);
    T_ASSERT_EQ_I(s_input_calls, 0);   /* delta=3 < 50, no input poll */
    T_ASSERT_EQ_I(s_render_calls, 0);
    return 0;
}

int test_tick_delayed_close_to_frame_busy_spins(void)
{
    /* threshold=50, remaining = 50-delta-10. Busy-spin when
     * threshold-10 ≤ delta < threshold → delta ∈ [40, 50).
     * Use delta=45 (15ms after prev → delta_thirds = 45). */
    tick_init();
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    reset_counters();
    uint32_t sleep = 9999;
    enum tick_result r = tick_step_with_now(10015, 1, &k_cb, &sleep);
    T_ASSERT_EQ_I(r, TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 0u);          /* busy-spin */
    /* delta=45 >= table[0]=50? No → input poll DOES NOT fire here. */
    T_ASSERT_EQ_I(s_input_calls, 0);
    T_ASSERT_EQ_I(s_render_calls, 0);
    return 0;
}

int test_tick_delayed_input_polls_after_frame_boundary(void)
{
    /* delta exactly at table[0]=50: input poll fires (`<=` not `<`).
     * threshold for speed=0 is also 50, and `delta < threshold` is
     * false at delta=50 → ticked branch. To get "input polls but not
     * ticked", we need a speed > 0 so threshold is bigger than 50.
     *
     * speed=1, threshold=100. delta=50: input polls (50<=50), but
     * delta<100 so no tick. */
    tick_init();
    g_tick.pending_speed = 1;
    /* First call seeds prev_ms — use a delta of 50 directly. */
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    reset_counters();
    uint32_t sleep = 9999;
    enum tick_result r = tick_step_with_now(10000 + 50 / 3, 1, &k_cb, &sleep);
    /* now=10016, delta_thirds = 48 + leftover. After first tick at
     * speed=1: leftover = 30000 % 100 = 0, prev=10000. So delta = 48.
     * 48 < 50 — input poll does NOT fire. Need bigger now. */
    /* Use now that gives delta_thirds ≥ 50. 17ms → 51 thirds. */
    (void)r;
    reset_counters();
    r = tick_step_with_now(10017, 1, &k_cb, &sleep);
    T_ASSERT_EQ_I(r, TICK_RESULT_DELAYED);
    T_ASSERT_EQ_I(s_input_calls, 1);   /* delta=51 ≥ 50 */
    T_ASSERT_EQ_I(s_render_calls, 0);  /* but < 100 → no tick */
    return 0;
}

int test_tick_state_one_skips_sim_render(void)
{
    /* state==1 → sim/render skipped; leftover & prev still advance. */
    tick_init();
    g_tick.state = 1;
    reset_counters();
    enum tick_result r = tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(r, TICK_RESULT_SKIPPED);
    T_ASSERT_EQ_I(s_sim_a_calls, 0);
    T_ASSERT_EQ_I(s_render_calls, 0);
    /* But prev_ms and leftover did advance (engine commits these
     * before the state check). */
    T_ASSERT_EQ_U(g_tick.prev_ms, 10000u);
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 30000 % 50);
    return 0;
}

int test_tick_state_two_transitions_to_one(void)
{
    /* state==2 → runs one tick then auto-transitions to 1. */
    tick_init();
    g_tick.state = 2;
    reset_counters();
    enum tick_result r = tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(r, TICK_RESULT_TICKED);
    T_ASSERT_EQ_I(s_sim_a_calls, 1);
    T_ASSERT_EQ_I(s_render_calls, 1);
    T_ASSERT_EQ_I(g_tick.state, 1);    /* transitioned */
    return 0;
}

int test_tick_no_device_aborts_after_sim_before_render(void)
{
    /* has_device=0 → engine early-return between sim loop and render. */
    tick_init();
    reset_counters();
    enum tick_result r = tick_step_with_now(10000, 0, &k_cb, NULL);
    T_ASSERT_EQ_I(r, TICK_RESULT_NO_DEVICE);
    T_ASSERT_EQ_I(s_sim_a_calls, 1);   /* sim DID run */
    T_ASSERT_EQ_I(s_render_calls, 0);  /* render did NOT */
    /* Bookkeeping still committed. */
    T_ASSERT_EQ_U(g_tick.prev_ms, 10000u);
    /* Frame counter NOT incremented (engine increments after render). */
    T_ASSERT_EQ_U(g_tick.frame_count, 0u);
    return 0;
}

int test_tick_state_alt_copies_state_seed(void)
{
    /* DAT_073dfca8 = DAT_073dfcb0 is written every successful tick. */
    tick_init();
    g_tick.state_seed = 0xdeadbeef;
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_U((uint32_t)g_tick.state_alt, 0xdeadbeefu);
    return 0;
}

int test_tick_per_frame_flags_clear_on_ticked(void)
{
    tick_init();
    g_tick.flag_dddd0 = 42;
    g_tick.flag_dddfa = 99;
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.flag_dddd0, 0);
    T_ASSERT_EQ_I(g_tick.flag_dddfa, 0);
    return 0;
}

int test_tick_per_frame_flags_not_cleared_on_delayed(void)
{
    /* Flags only clear when the ticked block runs. */
    tick_init();
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    g_tick.flag_dddd0 = 42;
    g_tick.flag_dddfa = 99;
    (void)tick_step_with_now(10001, 1, &k_cb, NULL);   /* delayed */
    T_ASSERT_EQ_I(g_tick.flag_dddd0, 42);
    T_ASSERT_EQ_I(g_tick.flag_dddfa, 99);
    return 0;
}

int test_tick_null_callbacks_are_safe(void)
{
    /* Shell scaffolding before the four big ports land: every callback
     * may be NULL. Scheduler must still update state correctly. */
    tick_init();
    enum tick_result r = tick_step_with_now(10000, 1, NULL, NULL);
    T_ASSERT_EQ_I(r, TICK_RESULT_TICKED);
    T_ASSERT_EQ_U(g_tick.frame_count, 1u);
    return 0;
}

int test_tick_adaptive_sleep_scales_with_remaining(void)
{
    /* Walk through the adaptive-sleep band:
     *   threshold=50, remaining_thirds = (50 - delta) - 10
     *   if remaining < 11:  sleep = remaining/3 + 1   (in 1..4 ms)
     *   else:               sleep = 5
     *
     * delta=29 → remaining=11 → sleep=5  (boundary)
     * delta=30 → remaining=10 → sleep=10/3+1 = 4
     * delta=38 → remaining=2  → sleep=2/3+1 = 1
     * delta=39 → remaining=1  → sleep=1
     * delta=40 → remaining=0  → busy-spin (sleep=0)
     */
    struct { uint32_t now; uint32_t expect_sleep; } cases[] = {
        { 10000 + 29/3, 5 },        /* now=10009: delta = 27 — actually < 29; use the integer math */
    };
    (void)cases;
    /* Easier: drive deltas directly by manipulating leftover_thirds. */
    tick_init();
    g_tick.prev_ms         = 10000;
    /* delta = (now-prev)*3 + leftover. Pick now=10000 so (now-prev)*3 = 0;
     * vary leftover to control delta_thirds exactly. */
    uint32_t sleep;

    g_tick.leftover_thirds = 29; sleep = 999;
    T_ASSERT_EQ_I(tick_step_with_now(10000, 1, &k_cb, &sleep), TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 5u);

    g_tick.leftover_thirds = 30; sleep = 999;
    T_ASSERT_EQ_I(tick_step_with_now(10000, 1, &k_cb, &sleep), TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 4u);

    g_tick.leftover_thirds = 38; sleep = 999;
    T_ASSERT_EQ_I(tick_step_with_now(10000, 1, &k_cb, &sleep), TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 1u);

    g_tick.leftover_thirds = 39; sleep = 999;
    T_ASSERT_EQ_I(tick_step_with_now(10000, 1, &k_cb, &sleep), TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 1u);

    g_tick.leftover_thirds = 40; sleep = 999;
    T_ASSERT_EQ_I(tick_step_with_now(10000, 1, &k_cb, &sleep), TICK_RESULT_DELAYED);
    T_ASSERT_EQ_U(sleep, 0u);            /* busy-spin */
    return 0;
}

int test_tick_steady_state_60fps_carries_residue(void)
{
    /* At 60 FPS target (threshold=50), with samples every 17ms (true
     * frame time 16.67), we should accumulate 1/3-ms residue each
     * tick and skip-burst correctly.
     *
     * Tick 1: now=10000, prev=0 → delta=30000 → leftover=0
     * Tick 2: now=10017, prev=10000 → delta=51, leftover=51%50=1
     * Tick 3: now=10034, prev=10017 → delta=51+1=52, leftover=2
     * Tick 4: now=10051, prev=10034 → delta=51+2=53, leftover=3
     * ...
     */
    tick_init();
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 0);

    (void)tick_step_with_now(10017, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 1);
    T_ASSERT_EQ_U(g_tick.prev_ms, 10017u);

    (void)tick_step_with_now(10034, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 2);

    (void)tick_step_with_now(10051, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.leftover_thirds, 3);
    return 0;
}

int test_tick_pending_speed_latches_at_top_of_frame(void)
{
    /* The engine writes `speed = pending_speed` as the very first
     * statement. So a change to pending_speed mid-frame doesn't take
     * effect until the next call. */
    tick_init();
    g_tick.pending_speed = 2;
    (void)tick_step_with_now(10000, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.speed, 2);
    g_tick.pending_speed = 4;
    /* still speed=2 inside that earlier call; the new value latches
     * on the next call. */
    (void)tick_step_with_now(10500, 1, &k_cb, NULL);
    T_ASSERT_EQ_I(g_tick.speed, 4);
    return 0;
}
