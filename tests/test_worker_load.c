/*
 * test_worker_load.c — pure-C tests for the asset-load worker
 * dispatcher (FUN_00452cde + LAB_0045293d). The Win32 thread spawn
 * isn't exercised here; we cover the state machine, the 17-slot
 * callback table, and the engine's "out-of-range scene state →
 * cleanup-only" branch.
 */
#include "t.h"
#include "nowloading.h"
#include "scene.h"
#include "worker_load.h"

/* Test scratchpad — callbacks record into here so the test body can
 * verify which entries fired in what order. */
static int g_cb_fire_count[WORKER_LOAD_CASE_COUNT];
static int g_cb_fire_total;
static int g_cb_fire_last;

static void cb_record_0(void)  { g_cb_fire_count[0]++;  g_cb_fire_total++; g_cb_fire_last = 0;  }
static void cb_record_1(void)  { g_cb_fire_count[1]++;  g_cb_fire_total++; g_cb_fire_last = 1;  }
static void cb_record_4(void)  { g_cb_fire_count[4]++;  g_cb_fire_total++; g_cb_fire_last = 4;  }
static void cb_record_9(void)  { g_cb_fire_count[9]++;  g_cb_fire_total++; g_cb_fire_last = 9;  }
static void cb_record_12(void) { g_cb_fire_count[12]++; g_cb_fire_total++; g_cb_fire_last = 12; }
static void cb_record_16(void) { g_cb_fire_count[16]++; g_cb_fire_total++; g_cb_fire_last = 16; }

/* Alt-cb scratchpad — kept separate from the indexed cb counters
 * because the alt slot has no "case index" of its own. */
static int g_alt_cb_fire_count;
static void cb_record_alt(void) { g_alt_cb_fire_count++; }

static void reset_scratchpad(void)
{
    for (int i = 0; i < WORKER_LOAD_CASE_COUNT; i++) g_cb_fire_count[i] = 0;
    g_cb_fire_total     = 0;
    g_cb_fire_last      = -1;
    g_alt_cb_fire_count = 0;
}

int test_worker_load_case_count_is_seventeen(void)
{
    /* Engine `cmp $0x10` accepts 0..16 inclusive — 17 entries. */
    T_ASSERT_EQ_I(WORKER_LOAD_CASE_COUNT, 17);
    return 0;
}

int test_worker_load_reset_zeroes_all_state(void)
{
    nowloading_reset();
    worker_load_set_cb(3, cb_record_0);
    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(), 1);

    worker_load_reset();

    T_ASSERT_EQ_I(worker_load_busy(),           0);
    /* All 17 slots cleared. */
    for (int i = 0; i < WORKER_LOAD_CASE_COUNT; i++) {
        T_ASSERT(worker_load_get_cb(i) == 0);
    }
    return 0;
}

int test_worker_load_begin_raises_busy_and_nowloading(void)
{
    nowloading_reset();
    worker_load_reset();

    T_ASSERT_EQ_I(worker_load_busy(),     0);
    T_ASSERT_EQ_I(nowloading_is_active(), 0);

    worker_load_begin();

    T_ASSERT_EQ_I(worker_load_busy(),     1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);
    return 0;
}

int test_worker_load_end_clears_busy_only(void)
{
    /* Important: end() must NOT clear the nowloading gate — the
     * engine clears that separately at the top of FUN_004547ab, which
     * we haven't ported yet. */
    nowloading_reset();
    worker_load_reset();
    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(),     1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);

    worker_load_end();

    T_ASSERT_EQ_I(worker_load_busy(),     0);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);   /* still on */
    return 0;
}

int test_worker_load_set_cb_round_trip(void)
{
    worker_load_reset();
    worker_load_set_cb(0,  cb_record_0);
    worker_load_set_cb(9,  cb_record_9);
    worker_load_set_cb(16, cb_record_16);

    T_ASSERT(worker_load_get_cb(0)  == cb_record_0);
    T_ASSERT(worker_load_get_cb(9)  == cb_record_9);
    T_ASSERT(worker_load_get_cb(16) == cb_record_16);
    T_ASSERT(worker_load_get_cb(5)  == 0);
    return 0;
}

int test_worker_load_set_cb_out_of_range_silently_ignored(void)
{
    worker_load_reset();
    /* No crash, no segfault, no write to any slot. */
    worker_load_set_cb(-1, cb_record_0);
    worker_load_set_cb(17, cb_record_0);
    worker_load_set_cb(99, cb_record_0);
    for (int i = 0; i < WORKER_LOAD_CASE_COUNT; i++) {
        T_ASSERT(worker_load_get_cb(i) == 0);
    }
    /* And reading out of range returns NULL. */
    T_ASSERT(worker_load_get_cb(-1) == 0);
    T_ASSERT(worker_load_get_cb(17) == 0);
    T_ASSERT(worker_load_get_cb(99) == 0);
    return 0;
}

int test_worker_load_set_cb_overwrites_existing(void)
{
    worker_load_reset();
    worker_load_set_cb(5, cb_record_0);
    worker_load_set_cb(5, cb_record_1);
    T_ASSERT(worker_load_get_cb(5) == cb_record_1);
    /* And NULL clears. */
    worker_load_set_cb(5, 0);
    T_ASSERT(worker_load_get_cb(5) == 0);
    return 0;
}

int test_worker_load_dispatch_invokes_registered_cb(void)
{
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_cb(0, cb_record_0);

    int ok = worker_load_dispatch_pure(0);

    T_ASSERT_EQ_I(ok, 1);
    T_ASSERT_EQ_I(g_cb_fire_total,    1);
    T_ASSERT_EQ_I(g_cb_fire_count[0], 1);
    T_ASSERT_EQ_I(g_cb_fire_last,     0);
    return 0;
}

int test_worker_load_dispatch_unregistered_slot_is_noop(void)
{
    /* Engine cases 4 and 12 jump straight to cleanup — no per-scene
     * loader is called. We model that as "no callback registered".
     * Same observable, simpler dispatcher. */
    worker_load_reset();
    reset_scratchpad();

    int ok4  = worker_load_dispatch_pure(4);
    int ok12 = worker_load_dispatch_pure(12);

    T_ASSERT_EQ_I(ok4,  1);   /* still "in range" — engine cleanup ran */
    T_ASSERT_EQ_I(ok12, 1);
    T_ASSERT_EQ_I(g_cb_fire_total, 0);
    return 0;
}

int test_worker_load_dispatch_out_of_range_returns_zero(void)
{
    /* Engine: `cmp $0x10; ja cleanup` — values > 16 short-circuit to
     * cleanup. Negative values via the unsigned compare also hit the
     * same branch. */
    worker_load_reset();
    reset_scratchpad();
    /* Register every slot so we'd notice if anything fired. */
    worker_load_set_cb(0,  cb_record_0);
    worker_load_set_cb(16, cb_record_16);

    T_ASSERT_EQ_I(worker_load_dispatch_pure(17),   0);
    T_ASSERT_EQ_I(worker_load_dispatch_pure(100),  0);
    T_ASSERT_EQ_I(worker_load_dispatch_pure(-1),   0);
    T_ASSERT_EQ_I(worker_load_dispatch_pure(-999), 0);
    T_ASSERT_EQ_I(g_cb_fire_total, 0);
    return 0;
}

int test_worker_load_dispatch_each_slot_independent(void)
{
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_cb(0,  cb_record_0);
    worker_load_set_cb(1,  cb_record_1);
    worker_load_set_cb(4,  cb_record_4);
    worker_load_set_cb(9,  cb_record_9);
    worker_load_set_cb(12, cb_record_12);
    worker_load_set_cb(16, cb_record_16);

    /* Walk every slot. Slots without callbacks (2/3/5/6/7/8/10/11/
     * 13/14/15) dispatch successfully but invoke nothing. */
    for (int s = 0; s < WORKER_LOAD_CASE_COUNT; s++) {
        T_ASSERT_EQ_I(worker_load_dispatch_pure(s), 1);
    }
    T_ASSERT_EQ_I(g_cb_fire_count[0],  1);
    T_ASSERT_EQ_I(g_cb_fire_count[1],  1);
    T_ASSERT_EQ_I(g_cb_fire_count[4],  1);
    T_ASSERT_EQ_I(g_cb_fire_count[9],  1);
    T_ASSERT_EQ_I(g_cb_fire_count[12], 1);
    T_ASSERT_EQ_I(g_cb_fire_count[16], 1);
    T_ASSERT_EQ_I(g_cb_fire_total,     6);
    return 0;
}

int test_worker_load_dispatch_does_not_touch_busy(void)
{
    /* Dispatch is busy-flag-agnostic; busy management is the
     * spawn/thread-proc's job. */
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_cb(2, cb_record_0);

    /* Busy stays 0 across a dispatch when nothing called begin. */
    T_ASSERT_EQ_I(worker_load_busy(), 0);
    worker_load_dispatch_pure(2);
    T_ASSERT_EQ_I(worker_load_busy(), 0);

    /* And stays 1 across a dispatch when begin was called. */
    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(), 1);
    worker_load_dispatch_pure(2);
    T_ASSERT_EQ_I(worker_load_busy(), 1);
    return 0;
}

int test_worker_load_close_is_idempotent_without_handle(void)
{
    /* On non-Win32 this is a no-op; on Win32 the handle is NULL
     * after reset(). Either way: must not crash on repeat calls. */
    worker_load_reset();
    worker_load_close();
    worker_load_close();
    worker_load_close();
    T_ASSERT_EQ_I(worker_load_busy(), 0);
    return 0;
}

int test_worker_load_spawn_non_win32_only_raises_gates(void)
{
    /* Under the unit-test build (non-Win32), spawn just calls begin
     * — no real thread is created. Tests that want to exercise the
     * dispatch must do so explicitly. */
    nowloading_reset();
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_cb(0, cb_record_0);
    g_scene_state = 0;

    worker_load_spawn();

    T_ASSERT_EQ_I(worker_load_busy(),     1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);
    /* No callback fired — spawn doesn't dispatch in the test build. */
    T_ASSERT_EQ_I(g_cb_fire_total, 0);

    /* Clean up so scene_post_fade_init tests downstream of this run
     * don't see a stale busy=1. */
    worker_load_end();
    nowloading_reset();
    return 0;
}

int test_worker_load_secondary_busy_defaults_zero(void)
{
    /* Until any DAT_06a49960 spawner ports, the secondary busy slot
     * is unreachable by any worker code — but the accessor must
     * exist so callers can be wired today. */
    worker_load_reset();
    T_ASSERT_EQ_I(worker_load_busy_secondary(), 0);
    return 0;
}

int test_worker_load_alt_cb_round_trip(void)
{
    worker_load_reset();
    T_ASSERT(worker_load_get_alt_cb() == 0);

    worker_load_set_alt_cb(cb_record_alt);
    T_ASSERT(worker_load_get_alt_cb() == cb_record_alt);

    /* NULL clears. */
    worker_load_set_alt_cb(0);
    T_ASSERT(worker_load_get_alt_cb() == 0);

    /* Last write wins. */
    worker_load_set_alt_cb(cb_record_0);
    worker_load_set_alt_cb(cb_record_alt);
    T_ASSERT(worker_load_get_alt_cb() == cb_record_alt);
    return 0;
}

int test_worker_load_alt_dispatch_invokes_registered_cb(void)
{
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_alt_cb(cb_record_alt);

    int ok = worker_load_dispatch_alt_pure();

    /* Engine LAB_00452a6b unconditionally falls through to its
     * cleanup tail with eax=1 — there's no "out of range" branch. */
    T_ASSERT_EQ_I(ok,                  1);
    T_ASSERT_EQ_I(g_alt_cb_fire_count, 1);
    return 0;
}

int test_worker_load_alt_dispatch_with_no_cb_returns_one(void)
{
    /* No registered cb is the engine's "alt body but everything
     * unported" state — still returns 1, just doesn't run anything. */
    worker_load_reset();
    reset_scratchpad();

    int ok = worker_load_dispatch_alt_pure();

    T_ASSERT_EQ_I(ok,                  1);
    T_ASSERT_EQ_I(g_alt_cb_fire_count, 0);
    return 0;
}

int test_worker_load_alt_dispatch_does_not_touch_busy(void)
{
    /* Alt dispatch, like primary dispatch, is busy-flag-agnostic. */
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_alt_cb(cb_record_alt);

    T_ASSERT_EQ_I(worker_load_busy(), 0);
    worker_load_dispatch_alt_pure();
    T_ASSERT_EQ_I(worker_load_busy(), 0);

    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(), 1);
    worker_load_dispatch_alt_pure();
    T_ASSERT_EQ_I(worker_load_busy(), 1);
    return 0;
}

int test_worker_load_alt_spawn_non_win32_only_raises_gates(void)
{
    /* Same as worker_load_spawn — gates rise, but no actual thread
     * runs the alt cb under the unit-test build. */
    nowloading_reset();
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_alt_cb(cb_record_alt);

    worker_load_spawn_alt();

    T_ASSERT_EQ_I(worker_load_busy(),       1);
    T_ASSERT_EQ_I(nowloading_is_active(),   1);
    T_ASSERT_EQ_I(g_alt_cb_fire_count,      0);

    /* Cleanup. */
    worker_load_end();
    nowloading_reset();
    return 0;
}

int test_worker_load_alt_full_cycle_simulation(void)
{
    /* Simulate what the Win32 alt thread proc does, end-to-end:
     *   1. spawn_alt (begin) — busy=1, nowloading=1
     *   2. dispatch_alt_pure — runs registered cb if any
     *   3. close — handle no-op on non-Win32
     *   4. end — busy=0; nowloading STAYS on (engine quirk shared
     *      with the primary worker).
     */
    nowloading_reset();
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_alt_cb(cb_record_alt);

    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(),     1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);

    int ok = worker_load_dispatch_alt_pure();
    T_ASSERT_EQ_I(ok,                  1);
    T_ASSERT_EQ_I(g_alt_cb_fire_count, 1);

    worker_load_close();
    worker_load_end();

    T_ASSERT_EQ_I(worker_load_busy(),     0);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);   /* gate quirk */
    nowloading_reset();
    return 0;
}

int test_worker_load_reset_clears_alt_cb(void)
{
    /* Alt cb must clear alongside the 17-slot table on reset. */
    worker_load_reset();
    worker_load_set_alt_cb(cb_record_alt);
    T_ASSERT(worker_load_get_alt_cb() == cb_record_alt);

    worker_load_reset();
    T_ASSERT(worker_load_get_alt_cb() == 0);
    return 0;
}

int test_worker_load_engine_dispatch_full_cycle_simulation(void)
{
    /* Simulate what the Win32 thread proc does, end-to-end, in pure C:
     *   1. spawn (begin) — busy=1, nowloading=1
     *   2. dispatch_pure(g_scene_state) — runs cb if any
     *   3. close — handle no-op on non-Win32
     *   4. end — busy=0; nowloading STAYS on (engine quirk)
     */
    nowloading_reset();
    worker_load_reset();
    reset_scratchpad();
    worker_load_set_cb(1, cb_record_1);
    g_scene_state = 1;

    /* 1 — engine spawner writes. */
    worker_load_begin();
    T_ASSERT_EQ_I(worker_load_busy(),     1);
    T_ASSERT_EQ_I(nowloading_is_active(), 1);

    /* 2 — thread proc dispatch. */
    int ok = worker_load_dispatch_pure((int)g_scene_state);
    T_ASSERT_EQ_I(ok,                  1);
    T_ASSERT_EQ_I(g_cb_fire_count[1],  1);

    /* 3 + 4 — thread cleanup. */
    worker_load_close();
    worker_load_end();

    T_ASSERT_EQ_I(worker_load_busy(),     0);
    /* nowloading deliberately still raised — see header. */
    T_ASSERT_EQ_I(nowloading_is_active(), 1);
    nowloading_reset();
    return 0;
}
