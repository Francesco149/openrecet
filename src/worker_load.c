/*
 * worker_load.c — see worker_load.h.
 *
 * Engine source: FUN_00452cde @ 0x452cde (41 bytes) + LAB_0045293d
 * @ 0x45293d (~302 bytes, including the 17-entry jump table at
 * 0x452a27). Plus FUN_00452917 (close) and FUN_00452911 (busy query).
 */

#include "worker_load.h"

#include "nowloading.h"
#include "scene.h"

/* ─── shared state ────────────────────────────────────────────────────── */

/* Engine DAT_06a49954 (primary busy) + DAT_06a4995c (secondary busy).
 * Volatile because the worker thread and the main thread both touch
 * them on Win32. The secondary busy is read-only from this module
 * until the DAT_06a49960 spawner family ports — it's declared here
 * so worker_load_close can match FUN_00452917's three-flag wipe. */
static volatile int  g_worker_busy           = 0;
static volatile int  g_worker_busy_secondary = 0;

/* Engine 17-entry jump table at 0x452a27. */
static worker_load_cb g_callbacks[WORKER_LOAD_CASE_COUNT];

/* Engine LAB_00452a6b body (a fixed sequence of 5 calls gated by
 * DAT_06a4996c) collapsed into a single registered callback. */
static worker_load_cb g_alt_callback = 0;

/* ─── pure-C API ──────────────────────────────────────────────────────── */

void worker_load_set_cb(int case_idx, worker_load_cb cb)
{
    if (case_idx < 0 || case_idx >= WORKER_LOAD_CASE_COUNT) return;
    g_callbacks[case_idx] = cb;
}

worker_load_cb worker_load_get_cb(int case_idx)
{
    if (case_idx < 0 || case_idx >= WORKER_LOAD_CASE_COUNT) return 0;
    return g_callbacks[case_idx];
}

int worker_load_busy(void)
{
    return g_worker_busy ? 1 : 0;
}

int worker_load_busy_secondary(void)
{
    return g_worker_busy_secondary ? 1 : 0;
}

void worker_load_set_alt_cb(worker_load_cb cb)
{
    g_alt_callback = cb;
}

worker_load_cb worker_load_get_alt_cb(void)
{
    return g_alt_callback;
}

int worker_load_dispatch_alt_pure(void)
{
    /* Engine LAB_00452a6b unconditionally reaches its cleanup tail
     * with eax=1 — there's no "out of range" short-circuit because
     * there's no input to range-check. */
    if (g_alt_callback) g_alt_callback();
    return 1;
}

int worker_load_dispatch_pure(int scene_state)
{
    /* Engine: `cmp $0x10, %eax; ja cleanup` — unsigned compare, so
     * negatives also fall through to cleanup. Match by casting both
     * sides to unsigned. */
    if ((unsigned)scene_state >= (unsigned)WORKER_LOAD_CASE_COUNT) {
        return 0;
    }
    worker_load_cb cb = g_callbacks[scene_state];
    if (cb) cb();
    return 1;
}

void worker_load_begin(void)
{
    g_worker_busy = 1;
    nowloading_set_active(1);
}

void worker_load_end(void)
{
    g_worker_busy = 0;
}

void worker_load_reset(void)
{
    g_worker_busy           = 0;
    g_worker_busy_secondary = 0;
    g_alt_callback          = 0;
    for (int i = 0; i < WORKER_LOAD_CASE_COUNT; i++) {
        g_callbacks[i] = 0;
    }
    /* Win32-only handle reset + secondary-flag clear folded into
     * worker_load_close below. */
    worker_load_close();
}

/* ─── Win32 spawn + thread proc ───────────────────────────────────────── */

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* Engine DAT_06a49950 + DAT_06a496cc. Only used on Win32. */
static HANDLE g_worker_handle    = NULL;
static DWORD  g_worker_thread_id = 0;

void worker_load_close(void)
{
    /* Engine FUN_00452917: gated on `handle != NULL` to avoid a
     * pointless CloseHandle(NULL); on the inside zeroes the handle,
     * the secondary busy (DAT_06a4995c), and the secondary
     * nowloading-gate (DAT_06a49960). The primary busy/gate are
     * left alone — that's the engine's contract. */
    if (g_worker_handle) {
        CloseHandle(g_worker_handle);
        g_worker_handle         = NULL;
        g_worker_busy_secondary = 0;
        /* DAT_06a49960 (secondary nowloading gate) lives inside the
         * nowloading module's collapsed-OR g_active. The primary
         * gate may still legitimately be raised on a parallel
         * transition, so we don't blanket-clear nowloading here —
         * the engine writes only the secondary slot to 0 and relies
         * on the per-tick consult-OR at FUN_004547ab. Our merged
         * model matches when only one side is in flight; if both
         * fired in lockstep (which the engine code paths don't
         * appear to do) we'd lose one bit of state until the
         * per-tick clear lands. */
    }
    g_worker_thread_id = 0;
}

/* Shared finalizer for both primary thread procs (LAB_0045293d and
 * LAB_00452a6b). The engine inlines this identical 4-instruction
 * cleanup tail at both labels:
 *     push 0x6a49950 ; call *0x51505c   ; CloseHandle(handle)
 *     andl $0x0, 0x6a49950              ; handle = 0
 *     andl $0x0, 0x6a49954              ; primary busy = 0
 *     push $0x1 ; pop %eax ; ret        ; return 1
 *
 * Note: the secondary thread procs (LAB_00452aab..c96) use a different
 * cleanup that also zeroes the secondary flags; they get their own
 * helper when those spawners port. */
static void primary_thread_cleanup(void)
{
    HANDLE h = g_worker_handle;
    if (h) {
        CloseHandle(h);
    }
    g_worker_handle    = NULL;
    g_worker_thread_id = 0;
    worker_load_end();
}

static DWORD WINAPI worker_load_thread_proc(LPVOID arg)
{
    (void)arg;

    /* Engine LAB_0045293d L1-3: jump-table dispatch on g_scene_state. */
    (void)worker_load_dispatch_pure((int)g_scene_state);

    primary_thread_cleanup();
    return 1;
}

static DWORD WINAPI worker_load_thread_proc_alt(LPVOID arg)
{
    (void)arg;

    /* Engine LAB_00452a6b L1-7: fixed 5-call body (with the first two
     * gated on DAT_06a4996c). Collapsed into a single callback that
     * the scene owner registers — the gate-check lives there. */
    (void)worker_load_dispatch_alt_pure();

    primary_thread_cleanup();
    return 1;
}

void worker_load_spawn(void)
{
    /* Engine writes 49954/49958 first, THEN CreateThread. We mirror
     * the order so the thread sees the gates raised the moment it's
     * scheduled. */
    worker_load_begin();

    g_worker_handle = CreateThread(NULL, 0, worker_load_thread_proc,
                                    NULL, 0, &g_worker_thread_id);
    if (!g_worker_handle) {
        /* Engine has no fallback — but if CreateThread fails we'd
         * leak the busy state forever, blocking any future scene
         * transition. Clear it so the next attempt can proceed. */
        worker_load_end();
    }
}

void worker_load_spawn_alt(void)
{
    /* Engine FUN_00452eed: identical structure to FUN_00452cde, but
     * targets the alt thread proc (LAB_00452a6b). Same primary gates;
     * the engine's literal write order is the same too — 49954/49958
     * then CreateThread. */
    worker_load_begin();

    g_worker_handle = CreateThread(NULL, 0, worker_load_thread_proc_alt,
                                    NULL, 0, &g_worker_thread_id);
    if (!g_worker_handle) {
        worker_load_end();
    }
}

#else  /* !_WIN32 ── non-Win32 build (unit tests on Linux) ───────────── */

void worker_load_close(void)
{
    /* No handle to close. The engine's secondary-flag clears are
     * gated on `handle != NULL`, which is never true here, so this
     * is a faithful no-op. Tests that want to verify the secondary
     * clear should drive that state via reset(). */
}

void worker_load_spawn(void)
{
    /* Tests drive dispatch_pure + end explicitly. Spawn just raises
     * the gates so callers can observe the "busy + nowloading set"
     * window. */
    worker_load_begin();
}

void worker_load_spawn_alt(void)
{
    /* Mirrors worker_load_spawn's gates-only behaviour on non-Win32.
     * Tests that want to exercise the alt body call
     * worker_load_dispatch_alt_pure + worker_load_end directly. */
    worker_load_begin();
}

#endif  /* _WIN32 */
