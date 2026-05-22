/*
 * tick.h — game-loop scheduler (FUN_0047be92 "the game tick").
 *
 * Mirrors the fixed-timestep dispatcher at 0x47be92 in the unpacked
 * binary. The engine maintains its frame budget in *thirds of a
 * millisecond* (i.e. `now_ms * 3` arithmetic with a `% threshold`
 * residue carried across iterations). The threshold table lives at
 * `0x005cbc58` in .rdata; entries [0..4] are 60/30/20/12/6 FPS targets
 * in those 1/3 ms units.
 *
 * Loop body (in order):
 *
 *   1. latch pending speed:  speed = pending_speed                (DAT_0438ccd8 ← DAT_0438ccdc)
 *   2. now_ms = QPC*1000/freq  (or timeGetTime fallback)          (FUN_0047be2f)
 *   3. delta_thirds = (now_ms - prev_ms)*3 + leftover_thirds
 *   4. if delta_thirds ≥ table[0]  → input_poll                   (FUN_0047b73c)
 *   5. if delta_thirds < table[speed]:
 *        adaptive sleep / busy-spin and return
 *      else:
 *        leftover_thirds = delta_thirds % table[speed]
 *        prev_ms = now_ms
 *        if state ∈ {0, 2}:
 *           state_alt = state_seed
 *           for i in 0..speed:  sim_a(); sim_b();
 *           if !d3d || !device:  return 0   (engine early-exit)
 *           render()
 *           frame_count++
 *           if state == 2:  state = 1
 *           clear two per-frame flags
 *
 * Note: input polling happens in BOTH the "delayed" and "ticked"
 * branches whenever delta has accumulated past 1/60 s. That keeps
 * input responsive at the display rate while the simulation runs at
 * the configured fixed timestep.
 *
 * The state machine values (0/1/2) are inferred from the dispatcher
 * only:
 *   0 — normal tick (sim+render)
 *   1 — skip (paused-load-screen-ish; render still happens elsewhere?)
 *   2 — "just resumed" — runs one normal tick then auto-transitions to 1.
 *
 * Pure-C entry (`tick_step_with_now`) does no Win32 calls so the
 * scheduler math is testable under ASan/UBSan. Win32 wrapper bundles
 * QPC + Sleep on top.
 *
 * See docs/findings/winmain-and-bootstrap.md and
 * docs/decompiled/by-address/47be92.c.
 */

#ifndef OPENRECET_TICK_H
#define OPENRECET_TICK_H

#include <stdint.h>

/* Speed-threshold table at engine VA 0x005cbc58 (5 active entries,
 * values 0x32/0x64/0x96/0xfa/0x1f4 in 1/3 ms units = 60/30/20/12/6 FPS).
 * Higher indices in the engine fall off into adjacent unrelated globals
 * — DAT_0438ccd8 always lives in [0..4] in practice, set by an unmapped
 * F-key handler that we'll port later. */
#define TICK_SPEED_COUNT 5
extern const int32_t g_tick_speed_thresholds[TICK_SPEED_COUNT];

struct tick_state {
    /* Time math (all in milliseconds unless noted). */
    uint32_t now_ms;            /* DAT_073de618 — last sampled time */
    uint32_t prev_ms;           /* DAT_073de61c — last successful-tick time */
    int32_t  delta_thirds;      /* DAT_073de620 — delta in 1/3 ms units */
    int32_t  leftover_thirds;   /* DAT_073de624 — residue from previous tick */

    /* Speed selection. `pending_speed` is what an external (F-key)
     * handler writes; `speed` is what we latched at the top of the
     * current frame. */
    int32_t  speed;             /* DAT_0438ccd8 */
    int32_t  pending_speed;     /* DAT_0438ccdc */

    /* Three-valued state machine (see header comment). */
    int32_t  state;             /* DAT_073dfca4 */
    int32_t  state_alt;         /* DAT_073dfca8 — written each frame from state_seed */
    int32_t  state_seed;        /* DAT_073dfcb0 — source for state_alt */

    /* Per-frame counters / flags. */
    uint32_t frame_count;       /* DAT_073dfcfc — incremented after render */
    int32_t  flag_dddd0;        /* DAT_073dddd0 — cleared each ticked frame */
    int32_t  flag_dddfa;        /* DAT_073dddfa — cleared each ticked frame */
};

extern struct tick_state g_tick;

/* Result of one tick_step pass. */
enum tick_result {
    /* delta < threshold — sim/render did NOT run this pass. `out_sleep_ms`
     * is the engine's adaptive sleep hint: 1..5 when delta is well below
     * the budget, 0 to busy-spin when within ~3.33 ms of frame time. */
    TICK_RESULT_DELAYED,

    /* delta ≥ threshold — sim ran (speed+1) times, render ran once. */
    TICK_RESULT_TICKED,

    /* `has_device` was 0 (engine: d3d8 or device8 pointer is NULL) and
     * the engine would have early-returned mid-tick. Bookkeeping for
     * leftover/prev/state_alt has already been written (matches engine
     * order); sim still ran, but render did not. */
    TICK_RESULT_NO_DEVICE,

    /* state ∉ {0, 2} — skip-tick state. Nothing ran; no sleep hint. */
    TICK_RESULT_SKIPPED,
};

struct tick_callbacks {
    void (*input_poll)(void);   /* FUN_0047b73c */
    void (*sim_a)(void);        /* FUN_004536cb */
    void (*sim_b)(void);        /* FUN_0049966a */
    void (*render)(void);       /* FUN_004547ab */
};

/* Reset scheduler state. After this, prev_ms=0 and leftover_thirds=0,
 * which matches the engine's BSS-zero at boot. */
void tick_init(void);

/* Pure-C dispatcher. Takes the current wall time in ms as a parameter
 * (no Win32 calls). Any callback may be NULL — that's how the shell
 * runs before the four big ports land. `has_device` should be 1
 * whenever both `IDirect3D8*` and `IDirect3DDevice8*` are non-null.
 *
 * On TICK_RESULT_DELAYED, *out_sleep_ms is set (0 = busy-spin / do not
 * Sleep, 1..5 = Sleep duration). On other results, *out_sleep_ms is
 * left untouched (caller must not Sleep). out_sleep_ms may be NULL if
 * the caller doesn't care about the hint. */
enum tick_result tick_step_with_now(uint32_t now_ms,
                                    int      has_device,
                                    const struct tick_callbacks *cb,
                                    uint32_t *out_sleep_ms);

#ifdef _WIN32
/* QPC-based ms timer with timeGetTime fallback. Mirrors FUN_0047be2f. */
uint32_t tick_now_ms(void);

/* Win32 convenience wrapper: sample now via QPC, dispatch, Sleep if the
 * scheduler asks. `has_device` semantics as above. */
enum tick_result tick_step_win32(int has_device,
                                 const struct tick_callbacks *cb);
#endif

/* Turbo mode (frame-limiter bypass).
 *
 * When enabled, `tick_step_win32` ignores QPC and feeds the dispatcher
 * a virtual clock that advances by `step_ms` per call. With step_ms=17
 * the dispatcher sees delta_thirds = 51 ≥ 50 (the 60 FPS threshold)
 * on every call, so it always takes the sim+render branch and never
 * Sleeps — the game runs as fast as the host can chew the loop. The
 * engine's per-tick wall-clock still advances by exactly the 60 FPS
 * budget though, so animations / audio fades / RNG that key off
 * `tick_now_ms` stay consistent with what would have happened at 60
 * FPS, just compressed in wall time.
 *
 * Mirrors the retail-side turbo in tools/frida/openrecet-agent.js so
 * scenario captures (--target both) can be regenerated quickly without
 * waiting on real frame budget. step_ms <= 0 keeps the previous value
 * (default 17). */
void tick_set_turbo(int enabled, uint32_t step_ms);
int  tick_turbo_enabled(void);

#endif /* OPENRECET_TICK_H */
