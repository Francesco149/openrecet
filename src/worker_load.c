/*
 * worker_load.c — see worker_load.h.
 *
 * Engine source: FUN_00452cde @ 0x452cde (41 bytes) + LAB_0045293d
 * @ 0x45293d (~302 bytes, including the 17-entry jump table at
 * 0x452a27). Plus FUN_00452917 (close) and FUN_00452911 (busy query).
 */

#include "worker_load.h"

#include "fade.h"
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

/* ─── secondary worker family ─────────────────────────────────────────── */

int32_t g_worker_sec_state_1c8   = 0;
int32_t g_worker_sec_state_1cc   = 0;
int32_t g_worker_sec_state_1d0   = 0;
int32_t g_worker_sec_state_1d4   = 0;
int32_t g_worker_sec_state_1d8   = 0;
int32_t g_worker_sec_state_984   = 0;
int32_t g_worker_sec_state_audio = 0;
int32_t g_worker_sec_param       = 0;

static worker_load_cb g_sec_bodies[WORKER_LOAD_SEC_BODY_COUNT];
static worker_load_cb g_sec_d07_pre_spawn = 0;

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

void worker_load_set_sec_body(int body_id, worker_load_cb cb)
{
    if (body_id < 0 || body_id >= WORKER_LOAD_SEC_BODY_COUNT) return;
    g_sec_bodies[body_id] = cb;
}

worker_load_cb worker_load_get_sec_body(int body_id)
{
    if (body_id < 0 || body_id >= WORKER_LOAD_SEC_BODY_COUNT) return 0;
    return g_sec_bodies[body_id];
}

void worker_load_set_sec_d07_pre_spawn(worker_load_cb cb)
{
    g_sec_d07_pre_spawn = cb;
}

worker_load_cb worker_load_get_sec_d07_pre_spawn(void)
{
    return g_sec_d07_pre_spawn;
}

void worker_load_begin_secondary(void)
{
    g_worker_busy_secondary = 1;
    nowloading_set_active(1);
}

void worker_load_end_secondary(void)
{
    g_worker_busy_secondary = 0;
}

int worker_load_dispatch_sec_pure(int body_id)
{
    if (body_id < 0 || body_id >= WORKER_LOAD_SEC_BODY_COUNT) return 0;
    worker_load_cb body = g_sec_bodies[body_id];
    if (body) body();
    return 1;
}

void worker_load_sec_post_body(int body_id)
{
    /* Per-LAB_* engine cleanup tail (after the shared CloseHandle +
     * zero handle + zero 4995c + zero 49960). The cluster of writes
     * here matches the byte sequence at each LAB_00452* tail.
     *
     * Fade-kick polarity (b3e/b82/bc6/c0a/c4e/c96): engine emits
     *     cmp [DAT_06a49980], 1   ; or `cmp [DAT_06a49980], esi` (esi=1)
     *     jne SKIP_FADE
     *     fade_phase_out_start(0, 0x11)
     *   SKIP_FADE:
     * jne is "jump if not equal", so the fade kick is the FALL-THROUGH
     * branch — engine fires fade when param == 1 (not != 1). */
    switch (body_id) {
        case WORKER_LOAD_SEC_BODY_AAB:
            /* LAB_00452aab: DAT_0438b1c8=1, FUN_00499579(0) (=DAT_09643120=0),
             * DAT_06a49984=1. No fade-kick.
             *
             * Engine assembly @ 0x452abd-0x452ae0:
             *     push $0x1 ; xor eax,eax ; pop esi      ; esi=1, eax=0
             *     mov eax,[handle]                        ; handle=0
             *     push eax                                ; push 0 for FUN_00499579
             *     mov eax,[busy_sec] ; mov eax,[now_sec]  ; both =0
             *     mov esi,[0x438b1c8]                     ; state_1c8 = 1
             *     call FUN_00499579                       ; DAT_09643120 = 0
             *     pop ecx
             *     mov esi,[0x6a49984]                     ; state_984 = 1
             * The pushed-eax-zero is what FUN_00499579 receives — engine
             * RESETS the audio LFO context, it doesn't raise it. */
            g_worker_sec_state_1c8   = 1;
            g_worker_sec_state_audio = 0;
            g_worker_sec_state_984   = 1;
            break;
        case WORKER_LOAD_SEC_BODY_AE8:
        case WORKER_LOAD_SEC_BODY_B13:
            /* LAB_00452ae8 / b13: DAT_0438b1cc=1. No fade-kick. */
            g_worker_sec_state_1cc = 1;
            break;
        case WORKER_LOAD_SEC_BODY_B3E:
        case WORKER_LOAD_SEC_BODY_B82:
        case WORKER_LOAD_SEC_BODY_BC6:
        case WORKER_LOAD_SEC_BODY_C0A:
            /* LAB_00452b3e / b82 / bc6 / c0a: DAT_0438b1d4=1, then fade-kick
             * fall-through gated on DAT_06a49980 == 1. */
            g_worker_sec_state_1d4 = 1;
            if (g_worker_sec_param == 1) fade_phase_out_start(0, 0x11);
            break;
        case WORKER_LOAD_SEC_BODY_C4E:
            /* LAB_00452c4e: DAT_0438b1d0=1, then fade-kick on param==1. */
            g_worker_sec_state_1d0 = 1;
            if (g_worker_sec_param == 1) fade_phase_out_start(0, 0x11);
            break;
        case WORKER_LOAD_SEC_BODY_C96:
            /* LAB_00452c96: DAT_0438b1d8=1, then fade-kick on param==1. */
            g_worker_sec_state_1d8 = 1;
            if (g_worker_sec_param == 1) fade_phase_out_start(0, 0x11);
            break;
        default:
            /* Out of range — no-op. */
            break;
    }
}

void worker_load_reset(void)
{
    g_worker_busy           = 0;
    g_worker_busy_secondary = 0;
    g_alt_callback          = 0;
    for (int i = 0; i < WORKER_LOAD_CASE_COUNT; i++) {
        g_callbacks[i] = 0;
    }
    for (int i = 0; i < WORKER_LOAD_SEC_BODY_COUNT; i++) {
        g_sec_bodies[i] = 0;
    }
    g_sec_d07_pre_spawn      = 0;
    g_worker_sec_state_1c8   = 0;
    g_worker_sec_state_1cc   = 0;
    g_worker_sec_state_1d0   = 0;
    g_worker_sec_state_1d4   = 0;
    g_worker_sec_state_1d8   = 0;
    g_worker_sec_state_984   = 0;
    g_worker_sec_state_audio = 0;
    g_worker_sec_param       = 0;
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

/* ─── secondary Win32 thread procs ──────────────────────────────────────
 *
 * Each LAB_00452* thread proc shares the same shape:
 *
 *   inner_body();                              // registered cb (NULL = no-op)
 *   secondary_thread_cleanup();                // CloseHandle + zero handle + zero 4995c/49960
 *   worker_load_sec_post_body(<body_id>);      // per-LAB_* state writes + fade-kick
 *   return 1;
 *
 * We factor the cleanup into one helper and dispatch on body_id for
 * the post-body work. The 9 thread procs become 9 thin wrappers
 * around dispatch_sec_pure + secondary_thread_cleanup + post_body. */

static void secondary_thread_cleanup(void)
{
    /* Engine LAB_00452a** common tail. Unconditional close
     * (CloseHandle(NULL) is a no-op), then unconditional clears of
     * handle + 4995c + 49960. Distinct from worker_load_close
     * (FUN_00452917) which gates the whole sequence on handle != 0. */
    HANDLE h = g_worker_handle;
    if (h) CloseHandle(h);
    g_worker_handle         = NULL;
    g_worker_thread_id      = 0;
    g_worker_busy_secondary = 0;
    /* DAT_06a49960 (secondary nowloading gate) collapsed into the
     * nowloading module's `g_active` via OR semantics — same
     * deliberate gap as worker_load_close. The per-tick clear at
     * FUN_004547ab handles the actual overlay drop. */
}

static DWORD WINAPI worker_load_thread_proc_sec(LPVOID arg, int body_id)
{
    (void)arg;
    (void)worker_load_dispatch_sec_pure(body_id);
    secondary_thread_cleanup();
    worker_load_sec_post_body(body_id);
    return 1;
}

/* gcc -m32 -mno-stdcall-fixup doesn't let us partial-apply CreateThread's
 * LPTHREAD_START_ROUTINE signature directly, so each of the 9 thread
 * procs is a single-line wrapper that hands the body_id to the shared
 * thread_proc_sec helper. */
static DWORD WINAPI thread_proc_sec_aab(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_AAB); }
static DWORD WINAPI thread_proc_sec_ae8(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_AE8); }
static DWORD WINAPI thread_proc_sec_b13(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_B13); }
static DWORD WINAPI thread_proc_sec_b3e(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_B3E); }
static DWORD WINAPI thread_proc_sec_b82(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_B82); }
static DWORD WINAPI thread_proc_sec_bc6(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_BC6); }
static DWORD WINAPI thread_proc_sec_c0a(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_C0A); }
static DWORD WINAPI thread_proc_sec_c4e(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_C4E); }
static DWORD WINAPI thread_proc_sec_c96(LPVOID arg) { return worker_load_thread_proc_sec(arg, WORKER_LOAD_SEC_BODY_C96); }

/* Shared spawn helper. Each spawn entry point picks its thread proc
 * and an optional pre-spawn "pending=2" flag pointer; the rest of the
 * sequence is identical. */
static void sec_spawn_common(int param, LPTHREAD_START_ROUTINE proc)
{
    worker_load_begin_secondary();
    g_worker_sec_param = param;

    g_worker_handle = CreateThread(NULL, 0, proc, NULL, 0,
                                    &g_worker_thread_id);
    if (!g_worker_handle) {
        worker_load_end_secondary();
    }
}

void worker_load_spawn_d07(int param)
{
    /* FUN_00452d07: no pending flag, but has unique pre-spawn (engine
     * calls FUN_0046c01e before CreateThread). */
    worker_load_begin_secondary();
    g_worker_sec_param = param;
    if (g_sec_d07_pre_spawn) g_sec_d07_pre_spawn();
    g_worker_handle = CreateThread(NULL, 0, thread_proc_sec_aab, NULL, 0,
                                    &g_worker_thread_id);
    if (!g_worker_handle) worker_load_end_secondary();
}

void worker_load_spawn_d3e(int param)
{
    /* FUN_00452d3e: pending=2 on 1cc, then sub-dispatch on param. */
    g_worker_sec_state_1cc = 2;
    sec_spawn_common(param,
                     param == 0 ? thread_proc_sec_ae8 : thread_proc_sec_b13);
}

void worker_load_spawn_d85(int param)
{
    /* FUN_00452d85: pending=2 on 1d4 → LAB_00452b3e. */
    g_worker_sec_state_1d4 = 2;
    sec_spawn_common(param, thread_proc_sec_b3e);
}

void worker_load_spawn_dc1(int param)
{
    /* FUN_00452dc1: pending=2 on 1d4 → LAB_00452b82. */
    g_worker_sec_state_1d4 = 2;
    sec_spawn_common(param, thread_proc_sec_b82);
}

void worker_load_spawn_dfd(int param)
{
    /* FUN_00452dfd: pending=2 on 1d4 → LAB_00452bc6. */
    g_worker_sec_state_1d4 = 2;
    sec_spawn_common(param, thread_proc_sec_bc6);
}

void worker_load_spawn_e39(int param)
{
    /* FUN_00452e39: pending=2 on 1d4 → LAB_00452c0a. */
    g_worker_sec_state_1d4 = 2;
    sec_spawn_common(param, thread_proc_sec_c0a);
}

void worker_load_spawn_e75(int param)
{
    /* FUN_00452e75: pending=2 on 1d0 → LAB_00452c4e. Unreferenced in
     * vendor's compiled code paths — included for completeness. */
    g_worker_sec_state_1d0 = 2;
    sec_spawn_common(param, thread_proc_sec_c4e);
}

void worker_load_spawn_eb1(int param)
{
    /* FUN_00452eb1: pending=2 on 1d8 → LAB_00452c96. Also unreferenced
     * — included for completeness. */
    g_worker_sec_state_1d8 = 2;
    sec_spawn_common(param, thread_proc_sec_c96);
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

/* Non-Win32 secondary spawn stubs — same gates-only pattern. Pending
 * flag and param latching still happen so observers see the engine-
 * faithful state. The d07 pre-spawn hook also fires (it's a callback,
 * not Win32-specific). The d3e variant doesn't pick a thread proc
 * because none runs — but the pending flag write is still observable. */

void worker_load_spawn_d07(int param)
{
    worker_load_begin_secondary();
    g_worker_sec_param = param;
    if (g_sec_d07_pre_spawn) g_sec_d07_pre_spawn();
}

void worker_load_spawn_d3e(int param)
{
    g_worker_sec_state_1cc = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

void worker_load_spawn_d85(int param)
{
    g_worker_sec_state_1d4 = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

void worker_load_spawn_dc1(int param)
{
    g_worker_sec_state_1d4 = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

void worker_load_spawn_dfd(int param)
{
    g_worker_sec_state_1d4 = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

void worker_load_spawn_e39(int param)
{
    g_worker_sec_state_1d4 = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

void worker_load_spawn_e75(int param)
{
    g_worker_sec_state_1d0 = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

void worker_load_spawn_eb1(int param)
{
    g_worker_sec_state_1d8 = 2;
    worker_load_begin_secondary();
    g_worker_sec_param = param;
}

#endif  /* _WIN32 */
