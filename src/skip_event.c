/*
 * skip_event.c — see skip_event.h. The ESC "skip this event?" prompt state
 * machine. Pure C; host-tested.
 */
#include "skip_event.h"

/* Button bits (src/input.c input_binding_mask[]): the engine's 14-bit mask. */
#define SKIP_BTN_RIGHT  0x0001
#define SKIP_BTN_LEFT   0x0002
#define SKIP_BTN_UP     0x0004
#define SKIP_BTN_DOWN   0x0008
#define SKIP_BTN_A      0x0010   /* confirm */
#define SKIP_BTN_B      0x0020   /* cancel  */

#define SKIP_PHASE_MAX  0x0c     /* DAT_06a4999c cap (FUN_004532df / FUN_004536cb) */

enum { SEL_YES = 0, SEL_NO = 1 };

int g_skip_event_enabled = 0;   /* off until Phase C render — see header */

static int      g_open      = 0;   /* DAT_06a499a0 */
static int      g_phase     = 0;   /* DAT_06a4999c — open/render anim (0..0xc) */
static int      g_sel       = SEL_YES;
static int      g_first     = 0;   /* swallow input on the first tick after arm */
static uint16_t g_prev_held = 0;   /* for edge = held & ~prev                  */

int skip_event_arm(int skippable)
{
    if (!g_skip_event_enabled)
        return 0;
    if (g_open)
        return 1;          /* idempotent re-press — retail re-arm is a no-op */
    if (!skippable)
        return 0;          /* FUN_00453384 gate rejected (not interruptible) */

    /* Open. Retail: DAT_06a4999c = 1, DAT_06a499a0 = 1, DAT_06a4997c = 0,
     * + the prompt-open SE FUN_00499519(0x16b) (deferred — audio hook). */
    g_open  = 1;
    g_phase = 1;
    g_sel   = SEL_YES;     /* cursor defaults to Yes (user screenshot) */
    g_first = 1;           /* don't let a button held at open-time act as input */
    return 1;
}

int skip_event_open(void)
{
    return g_open;
}

void skip_event_close(void)
{
    g_open      = 0;
    g_phase     = 0;
    g_sel       = SEL_YES;
    g_first     = 0;
    g_prev_held = 0;
}

skip_result_t skip_event_tick(uint16_t held)
{
    uint16_t edge;

    if (!g_open)
        return SKIP_EVENT_PENDING;

    /* Open animation: climb the render phase toward the cap (the banner fades
     * in; the retail render gates on DAT_06a4999c > 1). */
    if (g_phase < SKIP_PHASE_MAX)
        g_phase++;

    /* First tick after arming: seed the edge baseline from the current mask so
     * a button already held when ESC opened the prompt isn't read as a fresh
     * press (e.g. an A held to fast-forward dialogue must not instant-confirm). */
    if (g_first) {
        g_first     = 0;
        g_prev_held = held;
        return SKIP_EVENT_PENDING;
    }

    edge        = (uint16_t)(held & ~g_prev_held);
    g_prev_held = held;

    /* PORT-DEBT(simplified, FUN_00453384): the Yes/No selection + A-confirm /
     * B-cancel below is modelled to the user-confirmed observable behavior. The
     * engine's real selection global + the confirm/cancel counter choreography
     * aren't statically legible (the cancel counter DAT_06a499c8 is never set
     * positive in the corpus; the auto-confirm climbs only while ESC is
     * disabled) — reconcile against a live golden when Phase C lands. */
    /* Yes/No cursor — Left/Right (or Up/Down) toggles between the two. */
    if (edge & (SKIP_BTN_LEFT | SKIP_BTN_UP))
        g_sel = SEL_YES;
    else if (edge & (SKIP_BTN_RIGHT | SKIP_BTN_DOWN))
        g_sel = SEL_NO;

    /* B = cancel outright (resume the event). */
    if (edge & SKIP_BTN_B) {
        skip_event_close();
        return SKIP_EVENT_CANCELLED;
    }

    /* A = commit the current selection. */
    if (edge & SKIP_BTN_A) {
        int yes = (g_sel == SEL_YES);
        skip_event_close();
        return yes ? SKIP_EVENT_CONFIRMED : SKIP_EVENT_CANCELLED;
    }

    return SKIP_EVENT_PENDING;
}

int skip_event_phase(void)
{
    return g_phase;
}

int skip_event_selection(void)
{
    return g_sel;
}
