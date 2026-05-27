/*
 * tick.c — game-loop scheduler (FUN_0047be92).
 *
 * The pure-C `tick_step_with_now` mirrors the engine's dispatcher
 * byte-for-byte on the arithmetic, with the four big callees factored
 * out as function pointers so this module can stand alone (and so
 * tests can mock them). The Win32 wrapper at the bottom adds the QPC
 * timer + Sleep glue.
 */

#include "tick.h"

#include "call_trace.h"

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <mmsystem.h>
#endif

/* Threshold table from .rdata at 0x005cbc58 — values verified via
 * `tools/analyze/pe.py bytes 0x005cbc58 32`. */
const int32_t g_tick_speed_thresholds[TICK_SPEED_COUNT] = {
    0x32,   /* 60 FPS  — 16.67 ms */
    0x64,   /* 30 FPS  — 33.33 ms */
    0x96,   /* 20 FPS  — 50.00 ms */
    0xfa,   /* 12 FPS  — 83.33 ms */
    0x1f4,  /*  6 FPS  — 166.67 ms */
};

struct tick_state g_tick;

/* Turbo state (see tick.h header comment). Defaults: disabled, 17 ms
 * virtual step per tick — one 60 FPS frame budget rounded up from
 * 16.67. */
static int      g_turbo_enabled = 0;
static uint32_t g_turbo_step_ms = 17;
static uint32_t g_turbo_virtual_now_ms = 0;

void tick_set_turbo(int enabled, uint32_t step_ms)
{
    g_turbo_enabled = enabled ? 1 : 0;
    if (step_ms > 0) g_turbo_step_ms = step_ms;
}

int tick_turbo_enabled(void)
{
    return g_turbo_enabled;
}

void tick_init(void)
{
    g_tick = (struct tick_state){0};
    g_turbo_virtual_now_ms = 0;
}

enum tick_result tick_step_with_now(uint32_t now_ms,
                                    int      has_device,
                                    const struct tick_callbacks *cb,
                                    uint32_t *out_sleep_ms)
{
    /* E.2 probe — FUN_0047be92 @ 0x47be92. */
    CALL_TRACE_ENTER(0x47be92u);

    /* 1. latch pending speed. The engine writes through DAT_0438ccdc;
     *    we mirror via g_tick.pending_speed which an F-key handler will
     *    set in a later port. */
    g_tick.speed = g_tick.pending_speed;

    /* 2. sample wall clock. */
    g_tick.now_ms = now_ms;

    /* 3. compute delta in 1/3 ms units. Done in uint32 throughout so
     *    that mod-2^32 wraparound is well-defined under UBSan (the engine
     *    relies on x86's two's-complement wrap when timeGetTime crosses
     *    its ~49-day boundary, or when QPC's `*3` overflows int32 at
     *    ~715M ms uptime). The final cast to int32 is implementation-
     *    defined but compiles to a no-op on mingw32 (bit reinterpret). */
    {
        uint32_t now3  = (uint32_t)g_tick.now_ms  * 3u;
        uint32_t prev3 = (uint32_t)g_tick.prev_ms * 3u;
        uint32_t resid = (uint32_t)g_tick.leftover_thirds;
        g_tick.delta_thirds = (int32_t)(now3 - prev3 + resid);
    }

    /* 4. input poll always runs once we've accumulated past the 60 FPS
     *    boundary — keeps input responsive at display rate while sim
     *    runs at the configured timestep. */
    if (g_tick_speed_thresholds[0] <= g_tick.delta_thirds) {
        if (cb && cb->input_poll) cb->input_poll();
    }

    /* 5. compare to the speed-indexed threshold. The engine doesn't
     *    bounds-check the index; we mirror that — bounds are a property
     *    of the (unmapped) F-key handler that writes pending_speed. */
    int32_t threshold = g_tick_speed_thresholds[g_tick.speed];

    if (g_tick.delta_thirds < threshold) {
        /* Not time to tick yet.
         *
         *   delta < threshold-10  → adaptive sleep
         *   threshold-10 ≤ delta  → busy-spin (no Sleep)
         */
        uint32_t sleep_ms = 0;
        if (g_tick.delta_thirds < threshold - 10) {
            int32_t remaining_thirds = (threshold - g_tick.delta_thirds) - 10;
            if (remaining_thirds < 0xb) {
                /* Engine has a dead clamp here:
                 *     if (0x1e < remaining_thirds) remaining_thirds = 0x1e;
                 * The outer `< 0xb` makes that branch unreachable. We
                 * omit it intentionally; the comment is the record. */
                sleep_ms = (uint32_t)(remaining_thirds / 3 + 1);
            } else {
                sleep_ms = 5;
            }
        }
        if (out_sleep_ms) *out_sleep_ms = sleep_ms;
        return TICK_RESULT_DELAYED;
    }

    /* Delta exceeded the budget — time to tick. */
    g_tick.leftover_thirds = g_tick.delta_thirds % threshold;
    g_tick.prev_ms         = g_tick.now_ms;

    /* State machine: 0 and 2 run sim+render; anything else is a no-op
     * for this dispatcher. */
    if (g_tick.state != 0 && g_tick.state != 2) {
        return TICK_RESULT_SKIPPED;
    }

    g_tick.state_alt = g_tick.state_seed;

    /* speed == -1 skips the sim entirely (debug path). The engine's
     * `if (DAT_0438ccd8 != -1)` guards the do/while. */
    if (g_tick.speed != -1) {
        int32_t i = 0;
        do {
            if (cb && cb->sim_a) cb->sim_a();
            if (cb && cb->sim_b) cb->sim_b();
            i++;
        } while (i != g_tick.speed + 1);
    }

    /* Engine early-return: if either d3d8 factory or device pointer is
     * null at this point, return 0 from FUN_0047be92. The leftover/prev
     * bookkeeping above has already been committed — mirror that. */
    if (!has_device) return TICK_RESULT_NO_DEVICE;

    if (cb && cb->render) cb->render();
    g_tick.frame_count++;

    /* state 2 (just-resumed) auto-transitions to 1 after one tick. */
    if (g_tick.state == 2) g_tick.state = 1;

    /* Per-frame flag resets. */
    g_tick.flag_dddd0 = 0;
    g_tick.flag_dddfa = 0;

    return TICK_RESULT_TICKED;
}

#ifdef _WIN32

uint32_t tick_now_ms(void)
{
    /* E.2 probe — FUN_0047be2f @ 0x47be2f (QPC time-now). */
    CALL_TRACE_ENTER(0x47be2fu);

    /* FUN_0047be2f: QPC*1000/QPF, fall back to timeGetTime when either
     * frequency or counter reads zero (e.g., legacy hardware). */
    LARGE_INTEGER freq = {0}, count = {0};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    if (freq.QuadPart != 0 && count.QuadPart != 0) {
        /* count * 1000 / freq — engine uses __allmul/__alldiv which is
         * 64-bit arithmetic, matched here. The cast to uint32_t at the
         * end mirrors the engine's `EAX` truncation: the result is
         * returned via a 32-bit register. */
        unsigned long long ms = (unsigned long long)count.QuadPart * 1000ull
                              / (unsigned long long)freq.QuadPart;
        return (uint32_t)ms;
    }
    return (uint32_t)timeGetTime();
}

enum tick_result tick_step_win32(int has_device,
                                 const struct tick_callbacks *cb)
{
    uint32_t now_ms;
    if (g_turbo_enabled) {
        /* Advance the virtual clock by one step before dispatching.
         * The dispatcher sees now_ms = prev_step_ms + step (so delta
         * = step*3 thirds = 51 ≥ 50 with the default 17 ms step → the
         * 60 FPS threshold trips every iteration → no Sleep). */
        g_turbo_virtual_now_ms += g_turbo_step_ms;
        now_ms = g_turbo_virtual_now_ms;
    } else {
        now_ms = tick_now_ms();
    }

    uint32_t sleep_ms = 0;
    enum tick_result r = tick_step_with_now(now_ms, has_device, cb, &sleep_ms);
    if (!g_turbo_enabled && r == TICK_RESULT_DELAYED && sleep_ms > 0) {
        Sleep(sleep_ms);
    }
    return r;
}

#endif /* _WIN32 */
