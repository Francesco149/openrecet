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

/* Engine DAT_06a49954. Volatile because the worker thread and the
 * main thread both touch it on Win32. */
static volatile int  g_worker_busy = 0;

static worker_load_cb g_callbacks[WORKER_LOAD_CASE_COUNT];

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
    g_worker_busy = 0;
    for (int i = 0; i < WORKER_LOAD_CASE_COUNT; i++) {
        g_callbacks[i] = 0;
    }
    /* Win32-only handle reset folded into worker_load_close below. */
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
     * pointless CloseHandle(NULL); zeroes the handle + the secondary
     * worker's two flags (we don't have those yet, so omitted). */
    if (g_worker_handle) {
        CloseHandle(g_worker_handle);
        g_worker_handle = NULL;
    }
    g_worker_thread_id = 0;
}

static DWORD WINAPI worker_load_thread_proc(LPVOID arg)
{
    (void)arg;

    /* Engine LAB_0045293d L1-3: jump-table dispatch on g_scene_state. */
    (void)worker_load_dispatch_pure((int)g_scene_state);

    /* Engine LAB_0045293d L4-8 cleanup: close own handle, zero busy,
     * return 1. We zero busy AFTER the handle the same way the engine
     * does (`andl $0x0, 0x6a49950; andl $0x0, 0x6a49954`). */
    HANDLE h = g_worker_handle;
    if (h) {
        CloseHandle(h);
    }
    g_worker_handle    = NULL;
    g_worker_thread_id = 0;
    worker_load_end();
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

#else  /* !_WIN32 ── non-Win32 build (unit tests on Linux) ───────────── */

void worker_load_close(void)
{
    /* No handle to close. */
}

void worker_load_spawn(void)
{
    /* Tests drive dispatch_pure + end explicitly. Spawn just raises
     * the gates so callers can observe the "busy + nowloading set"
     * window. */
    worker_load_begin();
}

#endif  /* _WIN32 */
