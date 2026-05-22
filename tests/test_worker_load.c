/*
 * test_worker_load.c — pure-C tests for the asset-load worker
 * dispatcher (FUN_00452cde + LAB_0045293d). The Win32 thread spawn
 * isn't exercised here; we cover the state machine, the 17-slot
 * callback table, and the engine's "out-of-range scene state →
 * cleanup-only" branch.
 */
#include "t.h"
#include "fade.h"
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

/* ─── secondary worker family ─────────────────────────────────────────── */

static int g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_COUNT];
static int g_sec_body_fire_total;
static int g_sec_body_fire_last;
static int g_sec_d07_pre_fire_count;

static void sec_body_record_aab(void) { g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_AAB]++; g_sec_body_fire_total++; g_sec_body_fire_last = WORKER_LOAD_SEC_BODY_AAB; }
static void sec_body_record_ae8(void) { g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_AE8]++; g_sec_body_fire_total++; g_sec_body_fire_last = WORKER_LOAD_SEC_BODY_AE8; }
static void sec_body_record_b13(void) { g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_B13]++; g_sec_body_fire_total++; g_sec_body_fire_last = WORKER_LOAD_SEC_BODY_B13; }
static void sec_body_record_b3e(void) { g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_B3E]++; g_sec_body_fire_total++; g_sec_body_fire_last = WORKER_LOAD_SEC_BODY_B3E; }
static void sec_body_record_c4e(void) { g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_C4E]++; g_sec_body_fire_total++; g_sec_body_fire_last = WORKER_LOAD_SEC_BODY_C4E; }
static void sec_body_record_c96(void) { g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_C96]++; g_sec_body_fire_total++; g_sec_body_fire_last = WORKER_LOAD_SEC_BODY_C96; }
static void sec_d07_pre_record(void)  { g_sec_d07_pre_fire_count++; }

static void reset_sec_scratchpad(void)
{
    for (int i = 0; i < WORKER_LOAD_SEC_BODY_COUNT; i++) g_sec_body_fire_count[i] = 0;
    g_sec_body_fire_total    = 0;
    g_sec_body_fire_last     = -1;
    g_sec_d07_pre_fire_count = 0;
}

int test_worker_load_sec_body_count_is_nine(void)
{
    /* 8 spawners + 9 thread procs (d3e branches into ae8|b13). */
    T_ASSERT_EQ_I(WORKER_LOAD_SEC_BODY_COUNT, 9);
    return 0;
}

int test_worker_load_sec_body_set_get_round_trip(void)
{
    worker_load_reset();
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, sec_body_record_aab);
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C96, sec_body_record_c96);

    T_ASSERT(worker_load_get_sec_body(WORKER_LOAD_SEC_BODY_AAB) == sec_body_record_aab);
    T_ASSERT(worker_load_get_sec_body(WORKER_LOAD_SEC_BODY_C96) == sec_body_record_c96);
    T_ASSERT(worker_load_get_sec_body(WORKER_LOAD_SEC_BODY_B82) == 0);

    /* NULL clears. */
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, 0);
    T_ASSERT(worker_load_get_sec_body(WORKER_LOAD_SEC_BODY_AAB) == 0);
    return 0;
}

int test_worker_load_sec_body_out_of_range_silently_ignored(void)
{
    worker_load_reset();
    worker_load_set_sec_body(-1, sec_body_record_aab);
    worker_load_set_sec_body(9,  sec_body_record_aab);
    worker_load_set_sec_body(99, sec_body_record_aab);
    for (int i = 0; i < WORKER_LOAD_SEC_BODY_COUNT; i++) {
        T_ASSERT(worker_load_get_sec_body(i) == 0);
    }
    T_ASSERT(worker_load_get_sec_body(-1) == 0);
    T_ASSERT(worker_load_get_sec_body(9)  == 0);
    return 0;
}

int test_worker_load_sec_d07_pre_spawn_round_trip(void)
{
    worker_load_reset();
    T_ASSERT(worker_load_get_sec_d07_pre_spawn() == 0);

    worker_load_set_sec_d07_pre_spawn(sec_d07_pre_record);
    T_ASSERT(worker_load_get_sec_d07_pre_spawn() == sec_d07_pre_record);

    worker_load_set_sec_d07_pre_spawn(0);
    T_ASSERT(worker_load_get_sec_d07_pre_spawn() == 0);
    return 0;
}

int test_worker_load_begin_secondary_raises_busy_and_nowloading(void)
{
    nowloading_reset();
    worker_load_reset();

    T_ASSERT_EQ_I(worker_load_busy_secondary(), 0);
    T_ASSERT_EQ_I(nowloading_is_active(),       0);

    worker_load_begin_secondary();

    T_ASSERT_EQ_I(worker_load_busy_secondary(), 1);
    T_ASSERT_EQ_I(nowloading_is_active(),       1);
    /* Primary busy unaffected. */
    T_ASSERT_EQ_I(worker_load_busy(),           0);

    worker_load_end_secondary();
    nowloading_reset();
    return 0;
}

int test_worker_load_end_secondary_clears_busy_only(void)
{
    /* Same per-tick-clear quirk as primary's end(): nowloading stays
     * raised after the secondary worker finishes. */
    nowloading_reset();
    worker_load_reset();
    worker_load_begin_secondary();
    T_ASSERT_EQ_I(worker_load_busy_secondary(), 1);
    T_ASSERT_EQ_I(nowloading_is_active(),       1);

    worker_load_end_secondary();

    T_ASSERT_EQ_I(worker_load_busy_secondary(), 0);
    T_ASSERT_EQ_I(nowloading_is_active(),       1);   /* still on */
    nowloading_reset();
    return 0;
}

int test_worker_load_dispatch_sec_invokes_registered_cb(void)
{
    worker_load_reset();
    reset_sec_scratchpad();
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, sec_body_record_aab);

    int ok = worker_load_dispatch_sec_pure(WORKER_LOAD_SEC_BODY_AAB);

    T_ASSERT_EQ_I(ok,                                              1);
    T_ASSERT_EQ_I(g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_AAB], 1);
    T_ASSERT_EQ_I(g_sec_body_fire_total,                           1);
    return 0;
}

int test_worker_load_dispatch_sec_out_of_range_returns_zero(void)
{
    worker_load_reset();
    reset_sec_scratchpad();
    worker_load_set_sec_body(0, sec_body_record_aab);
    worker_load_set_sec_body(8, sec_body_record_c96);

    T_ASSERT_EQ_I(worker_load_dispatch_sec_pure(9),   0);
    T_ASSERT_EQ_I(worker_load_dispatch_sec_pure(99),  0);
    T_ASSERT_EQ_I(worker_load_dispatch_sec_pure(-1),  0);
    T_ASSERT_EQ_I(g_sec_body_fire_total,              0);
    return 0;
}

int test_worker_load_dispatch_sec_unregistered_slot_is_noop(void)
{
    worker_load_reset();
    reset_sec_scratchpad();
    /* No callbacks registered; dispatch is still in range → returns 1. */
    int ok = worker_load_dispatch_sec_pure(WORKER_LOAD_SEC_BODY_B3E);
    T_ASSERT_EQ_I(ok,                    1);
    T_ASSERT_EQ_I(g_sec_body_fire_total, 0);
    return 0;
}

int test_worker_load_sec_post_body_aab_writes_three_flags(void)
{
    /* LAB_00452aab cleanup tail (objdump @ 0x452abd-0x452ae0):
     * DAT_0438b1c8=1, FUN_00499579(0) → DAT_09643120=0,
     * DAT_06a49984=1. No fade-kick (param read is suppressed). */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 0;   /* would normally fade-kick if checked */

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_AAB);

    T_ASSERT_EQ_I(g_worker_sec_state_1c8,   1);
    T_ASSERT_EQ_I(g_worker_sec_state_984,   1);
    T_ASSERT_EQ_I(g_worker_sec_state_audio, 0);   /* engine pushes XOR'd eax = 0 */
    /* No fade started. */
    T_ASSERT_EQ_I(g_fade_phase, 0);
    return 0;
}

int test_worker_load_sec_post_body_ae8_writes_1cc_no_fade(void)
{
    /* LAB_00452ae8 cleanup tail: DAT_0438b1cc=1. No fade-kick. */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 0;

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_AE8);

    T_ASSERT_EQ_I(g_worker_sec_state_1cc, 1);
    T_ASSERT_EQ_I(g_worker_sec_state_1c8, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 0);
    T_ASSERT_EQ_I(g_fade_phase,           0);
    return 0;
}

int test_worker_load_sec_post_body_b13_writes_1cc_no_fade(void)
{
    /* LAB_00452b13 cleanup tail: same as ae8 (DAT_0438b1cc=1). */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 7;   /* still no fade-kick */

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_B13);

    T_ASSERT_EQ_I(g_worker_sec_state_1cc, 1);
    T_ASSERT_EQ_I(g_fade_phase,           0);
    return 0;
}

int test_worker_load_sec_post_body_b3e_writes_1d4_and_fade_kicks_on_param_eq_one(void)
{
    /* LAB_00452b3e: DAT_0438b1d4=1; fade-kick fall-through fires when
     * param == 1 (engine pattern: `cmp [param], 1 ; jne SKIP_FADE`). */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 1;   /* == 1 → fall-through → fade-kick fires */

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_B3E);

    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 1);
    /* fade_phase_out_start(0, 0x11) sets phase=-1, mode=0, duration=0x11. */
    T_ASSERT_EQ_I(g_fade_phase,           -1);
    T_ASSERT_EQ_I(g_fade_mode,             0);
    T_ASSERT_EQ_I(g_fade_duration,      0x11);
    return 0;
}

int test_worker_load_sec_post_body_b3e_no_fade_when_param_ne_one(void)
{
    /* jne taken when param != 1 → skips fade. */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 0;   /* jne taken */

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_B3E);

    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 1);
    T_ASSERT_EQ_I(g_fade_phase,           0);   /* fade untouched */
    return 0;
}

int test_worker_load_sec_post_body_b82_bc6_c0a_share_1d4_fade_pattern(void)
{
    /* LAB_00452b82/bc6/c0a share the b3e pattern exactly. */
    for (int body_id = WORKER_LOAD_SEC_BODY_B82;
         body_id <= WORKER_LOAD_SEC_BODY_C0A; body_id++) {
        worker_load_reset();
        fade_reset();
        g_worker_sec_param = 1;   /* triggers fade-kick fall-through */
        worker_load_sec_post_body(body_id);
        T_ASSERT_EQ_I(g_worker_sec_state_1d4, 1);
        T_ASSERT_EQ_I(g_fade_phase,          -1);
    }
    return 0;
}

int test_worker_load_sec_post_body_c4e_writes_1d0_with_fade(void)
{
    /* LAB_00452c4e: DAT_0438b1d0=1; fade-kick gate same as b3e family. */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 1;

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_C4E);

    T_ASSERT_EQ_I(g_worker_sec_state_1d0, 1);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 0);  /* separate slot */
    T_ASSERT_EQ_I(g_fade_phase,          -1);
    return 0;
}

int test_worker_load_sec_post_body_c96_writes_1d8_with_fade(void)
{
    /* LAB_00452c96: DAT_0438b1d8=1; fade-kick gate same as b3e family. */
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 1;   /* == 1 → fall-through */

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_C96);

    T_ASSERT_EQ_I(g_worker_sec_state_1d8, 1);
    T_ASSERT_EQ_I(g_fade_phase,          -1);
    return 0;
}

int test_worker_load_sec_post_body_out_of_range_is_noop(void)
{
    worker_load_reset();
    fade_reset();
    g_worker_sec_param = 0;

    worker_load_sec_post_body(-1);
    worker_load_sec_post_body(9);
    worker_load_sec_post_body(99);

    T_ASSERT_EQ_I(g_worker_sec_state_1c8, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1cc, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d0, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d8, 0);
    T_ASSERT_EQ_I(g_fade_phase,            0);
    return 0;
}

int test_worker_load_spawn_d07_raises_gates_latches_param_runs_pre_spawn(void)
{
    nowloading_reset();
    worker_load_reset();
    reset_sec_scratchpad();
    worker_load_set_sec_d07_pre_spawn(sec_d07_pre_record);

    worker_load_spawn_d07(42);

    T_ASSERT_EQ_I(worker_load_busy_secondary(), 1);
    T_ASSERT_EQ_I(nowloading_is_active(),       1);
    T_ASSERT_EQ_I(g_worker_sec_param,          42);
    T_ASSERT_EQ_I(g_sec_d07_pre_fire_count,     1);
    /* d07 doesn't pre-write any "pending=2" flag. */
    T_ASSERT_EQ_I(g_worker_sec_state_1cc, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d0, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d8, 0);
    /* No body fires under unit-test build (no thread). */
    T_ASSERT_EQ_I(g_sec_body_fire_total, 0);

    worker_load_end_secondary();
    nowloading_reset();
    return 0;
}

int test_worker_load_spawn_d3e_pending_cc_eq_two(void)
{
    /* FUN_00452d3e: pending=2 on 1cc, param latched. */
    nowloading_reset();
    worker_load_reset();
    reset_sec_scratchpad();

    worker_load_spawn_d3e(0);

    T_ASSERT_EQ_I(g_worker_sec_state_1cc,       2);
    T_ASSERT_EQ_I(worker_load_busy_secondary(), 1);
    T_ASSERT_EQ_I(g_worker_sec_param,           0);

    worker_load_end_secondary();
    nowloading_reset();
    return 0;
}

int test_worker_load_spawn_d85_dc1_dfd_e39_share_pending_1d4(void)
{
    /* All four spawners share the 1d4 pending=2 write. param differs. */
    static const struct {
        void (*fn)(int);
        int  param;
    } cases[] = {
        { worker_load_spawn_d85, 100 },
        { worker_load_spawn_dc1, 101 },
        { worker_load_spawn_dfd, 102 },
        { worker_load_spawn_e39, 103 },
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        nowloading_reset();
        worker_load_reset();
        cases[i].fn(cases[i].param);
        T_ASSERT_EQ_I(g_worker_sec_state_1d4,       2);
        T_ASSERT_EQ_I(g_worker_sec_state_1d0,       0);   /* not touched */
        T_ASSERT_EQ_I(g_worker_sec_state_1d8,       0);
        T_ASSERT_EQ_I(g_worker_sec_param,           cases[i].param);
        T_ASSERT_EQ_I(worker_load_busy_secondary(), 1);
        worker_load_end_secondary();
        nowloading_reset();
    }
    return 0;
}

int test_worker_load_spawn_e75_pending_1d0(void)
{
    nowloading_reset();
    worker_load_reset();
    worker_load_spawn_e75(5);
    T_ASSERT_EQ_I(g_worker_sec_state_1d0, 2);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 0);
    T_ASSERT_EQ_I(g_worker_sec_param,     5);
    worker_load_end_secondary();
    nowloading_reset();
    return 0;
}

int test_worker_load_spawn_eb1_pending_1d8(void)
{
    nowloading_reset();
    worker_load_reset();
    worker_load_spawn_eb1(6);
    T_ASSERT_EQ_I(g_worker_sec_state_1d8, 2);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 0);
    T_ASSERT_EQ_I(g_worker_sec_param,     6);
    worker_load_end_secondary();
    nowloading_reset();
    return 0;
}

int test_worker_load_reset_zeroes_secondary_state(void)
{
    /* Reset must clear all secondary callbacks + state bytes + param. */
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, sec_body_record_aab);
    worker_load_set_sec_d07_pre_spawn(sec_d07_pre_record);
    g_worker_sec_state_1c8   = 9;
    g_worker_sec_state_1cc   = 9;
    g_worker_sec_state_1d0   = 9;
    g_worker_sec_state_1d4   = 9;
    g_worker_sec_state_1d8   = 9;
    g_worker_sec_state_984   = 9;
    g_worker_sec_state_audio = 9;
    g_worker_sec_param       = 9;

    worker_load_reset();

    for (int i = 0; i < WORKER_LOAD_SEC_BODY_COUNT; i++) {
        T_ASSERT(worker_load_get_sec_body(i) == 0);
    }
    T_ASSERT(worker_load_get_sec_d07_pre_spawn() == 0);
    T_ASSERT_EQ_I(g_worker_sec_state_1c8,   0);
    T_ASSERT_EQ_I(g_worker_sec_state_1cc,   0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d0,   0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4,   0);
    T_ASSERT_EQ_I(g_worker_sec_state_1d8,   0);
    T_ASSERT_EQ_I(g_worker_sec_state_984,   0);
    T_ASSERT_EQ_I(g_worker_sec_state_audio, 0);
    T_ASSERT_EQ_I(g_worker_sec_param,       0);
    return 0;
}

int test_worker_load_sec_full_cycle_simulation_b3e(void)
{
    /* End-to-end simulation of FUN_00452d85 → LAB_00452b3e on non-Win32:
     *   1. spawn_d85(param) — pending(1d4)=2, busy_sec=1, nowloading=1,
     *      param latched.
     *   2. dispatch_sec_pure(B3E) — fires registered inner body.
     *   3. (Win32: secondary_thread_cleanup) — skipped on Linux.
     *   4. sec_post_body(B3E) — ready(1d4)=1, fade-kick if param!=1.
     *   5. end_secondary — busy_sec=0; nowloading STAYS on.
     */
    nowloading_reset();
    worker_load_reset();
    fade_reset();
    reset_sec_scratchpad();
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_B3E, sec_body_record_b3e);

    worker_load_spawn_d85(1);   /* param==1 → fade-kick fall-through */
    T_ASSERT_EQ_I(g_worker_sec_state_1d4,       2);
    T_ASSERT_EQ_I(worker_load_busy_secondary(), 1);

    int ok = worker_load_dispatch_sec_pure(WORKER_LOAD_SEC_BODY_B3E);
    T_ASSERT_EQ_I(ok,                                              1);
    T_ASSERT_EQ_I(g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_B3E], 1);

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_B3E);
    T_ASSERT_EQ_I(g_worker_sec_state_1d4, 1);    /* 2 → 1 */
    T_ASSERT_EQ_I(g_fade_phase,          -1);    /* fade-kick fired */

    worker_load_end_secondary();
    T_ASSERT_EQ_I(worker_load_busy_secondary(), 0);
    T_ASSERT_EQ_I(nowloading_is_active(),       1);   /* quirk */
    nowloading_reset();
    fade_reset();
    return 0;
}

int test_worker_load_sec_full_cycle_simulation_c4e(void)
{
    /* End-to-end simulation of FUN_00452e75 → LAB_00452c4e (unreferenced
     * in vendor but still well-formed). Verifies 1d0 slot + fade-kick. */
    nowloading_reset();
    worker_load_reset();
    fade_reset();
    reset_sec_scratchpad();
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_C4E, sec_body_record_c4e);

    worker_load_spawn_e75(1);   /* param==1 → fade-kick fall-through */
    T_ASSERT_EQ_I(g_worker_sec_state_1d0, 2);

    worker_load_dispatch_sec_pure(WORKER_LOAD_SEC_BODY_C4E);
    T_ASSERT_EQ_I(g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_C4E], 1);

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_C4E);
    T_ASSERT_EQ_I(g_worker_sec_state_1d0, 1);
    T_ASSERT_EQ_I(g_fade_phase,          -1);

    worker_load_end_secondary();
    nowloading_reset();
    fade_reset();
    return 0;
}

int test_worker_load_sec_full_cycle_simulation_aab_no_fade(void)
{
    /* End-to-end simulation of FUN_00452d07 → LAB_00452aab. d07 has the
     * pre-spawn hook and aab is one of the three "no fade-kick" bodies. */
    nowloading_reset();
    worker_load_reset();
    fade_reset();
    reset_sec_scratchpad();
    worker_load_set_sec_body(WORKER_LOAD_SEC_BODY_AAB, sec_body_record_aab);
    worker_load_set_sec_d07_pre_spawn(sec_d07_pre_record);

    worker_load_spawn_d07(0);
    T_ASSERT_EQ_I(g_sec_d07_pre_fire_count, 1);

    worker_load_dispatch_sec_pure(WORKER_LOAD_SEC_BODY_AAB);
    T_ASSERT_EQ_I(g_sec_body_fire_count[WORKER_LOAD_SEC_BODY_AAB], 1);

    worker_load_sec_post_body(WORKER_LOAD_SEC_BODY_AAB);
    /* aab's three writes — audio=0 (engine RESETS the LFO via XOR'd eax). */
    T_ASSERT_EQ_I(g_worker_sec_state_1c8,   1);
    T_ASSERT_EQ_I(g_worker_sec_state_984,   1);
    T_ASSERT_EQ_I(g_worker_sec_state_audio, 0);
    /* No fade-kick — aab's cleanup tail doesn't consult param. */
    T_ASSERT_EQ_I(g_fade_phase, 0);

    worker_load_end_secondary();
    nowloading_reset();
    return 0;
}
