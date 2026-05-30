/*
 * scene1_intro_events.c — see scene1_intro_events.h.
 *
 * STUB. A four-state frame counter that injects a second asset-load gate
 * cycle after the first new-game load clears, so HOUSE_FREEROAM fires the
 * two times the retail intro sequence produces. No dialogue is rendered —
 * this only reproduces the anchor edges the TAS traces wait on.
 */
#include "scene1_intro_events.h"

#include "nowloading.h"
#include "worker_load.h"

/* Sequencer states. */
enum {
    IE_DORMANT = 0,   /* not armed */
    IE_WAIT_FREEROAM, /* armed; waiting for the first load to fully clear */
    IE_DELAY,         /* first HOUSE_FREEROAM reached; brief "intro event 1" beat */
    IE_LOADING,       /* second load gate raised; "intro event 2" hidden load */
    IE_DONE,          /* second HOUSE_FREEROAM dispatched; sequence complete */
};

/* Frame budgets. Deliberately short — the stub renders nothing, so these
 * just need to (a) let the first HOUSE_FREEROAM anchor be sampled before the
 * second load raises (DELAY), and (b) hold the overlay long enough that the
 * LOADING_START edge is observed before it drops (LOADING). The real intro
 * events run much longer; the segtrace `wait` short-circuits the instant the
 * anchor fires, so trace length is independent of these. */
#define IE_DELAY_FRAMES   6
#define IE_LOADING_FRAMES 4

static int g_ie_state = IE_DORMANT;
static int g_ie_ctr   = 0;

void scene1_intro_events_arm(void)
{
    g_ie_state = IE_WAIT_FREEROAM;
    g_ie_ctr   = 0;
}

void scene1_intro_events_reset(void)
{
    g_ie_state = IE_DORMANT;
    g_ie_ctr   = 0;
}

int scene1_intro_events_pending(void)
{
    return (g_ie_state != IE_DORMANT && g_ie_state != IE_DONE) ? 1 : 0;
}

void scene1_intro_events_tick(void)
{
    switch (g_ie_state) {
    case IE_WAIT_FREEROAM:
        /* Hold until the first (real) load has fully cleared — busy down and
         * the overlay gate down. That is exactly the HOUSE_FREEROAM predicate
         * (in-game + load-free), so when this becomes true the first anchor
         * has just fired. */
        if (!worker_load_busy() && !nowloading_is_active()) {
            g_ie_state = IE_DELAY;
            g_ie_ctr   = 0;
        }
        break;

    case IE_DELAY:
        /* A brief free-roam beat standing in for "intro event 1". Lets the
         * first HOUSE_FREEROAM anchor be sampled + the segtrace enter its
         * second `wait` segment before we raise the next load. */
        if (++g_ie_ctr >= IE_DELAY_FRAMES) {
            /* Raise the gate for the second (hidden) intro load. Using the
             * gate primitives directly — not worker_load_spawn — keeps the
             * window deterministic (the Win32 spawn's worker thread self-
             * completes on an unpredictable boundary). */
            worker_load_begin();
            g_ie_state = IE_LOADING;
            g_ie_ctr   = 0;
        }
        break;

    case IE_LOADING:
        /* Hold the overlay up for a few frames, then drop it. sim_step_a
         * sees worker_load_busy() this whole window and pumps the loading
         * counters; the frame after worker_load_end() it clears the gate,
         * producing LOADING_END + the second HOUSE_FREEROAM. */
        if (++g_ie_ctr >= IE_LOADING_FRAMES) {
            worker_load_end();
            g_ie_state = IE_DONE;
            g_ie_ctr   = 0;
        }
        break;

    case IE_DORMANT:
    case IE_DONE:
    default:
        break;
    }
}
