/*
 * skip_event.c — see skip_event.h. The ESC "Do you want to skip this event?"
 * prompt: the FUN_0046c2cb gate + the FUN_0046c320 poll branch, wired onto the
 * generic choice box (src/choice_box.c). Pure C; host-tested.
 *
 * Engine flow (golden runs/skip-golden/arm485/frame_00514.png):
 *   WndProc ESC → FUN_0045337b → FUN_00453384 (b1c8==1) → FUN_0046c2cb:
 *       if (skip_prompt > 1 && !already-open)
 *           DAT_073a3dec = 1;                 // prompt open
 *           FUN_00434def("Do you want to skip this event?", 1, 0);
 *   per frame, FUN_0046c320 (the dialogue update) at the top:
 *       if (DAT_073a3dec == 1) { r = FUN_00434ed2();
 *           r==1 (Yes) → close, run the skip teardown (FUN_00435612)
 *           r==2 (No)  → close, resume the dialogue }
 */
#include "skip_event.h"
#include "choice_box.h"

#define SKIP_PROMPT_TEXT "Do you want to skip this event?"

int g_skip_event_enabled = 1;   /* Phase C render landed — choice box draws */

static int      g_open      = 0;   /* DAT_073a3dec — prompt-open flag */
static int      g_first     = 0;   /* swallow input on the first tick after arm */
static uint16_t g_prev_held = 0;   /* for edge = held & ~prev */

int skip_event_arm(int skippable)
{
    if (!g_skip_event_enabled)
        return 0;
    if (g_open)
        return 1;          /* DAT_073a3dec==1 → re-press is a no-op */
    if (!skippable)
        return 0;          /* FUN_0046c2cb gate: skip_prompt <= 1 → not yet */

    /* FUN_0046c2cb: DAT_073a3dec = 1; FUN_00434def(prompt, 1, 0). The engine
     * also snapshots the resume state (FUN_00435625/44 → DAT_073a3e2c/30/34);
     * at our altitude "Yes" always tears the prologue down (PORT-DEBT, see the
     * tick + scene1_intro_dialogue_skip_to_end). */
    choice_box_open(SKIP_PROMPT_TEXT, /*mode=*/1, /*sel=*/0);
    g_open  = 1;
    g_first = 1;           /* don't read a button held at open-time as input */
    return 1;
}

int skip_event_open(void)
{
    return g_open;
}

void skip_event_close(void)
{
    choice_box_reset();
    g_open      = 0;
    g_first     = 0;
    g_prev_held = 0;
}

skip_result_t skip_event_tick(uint16_t held)
{
    uint16_t edge;
    int      r;

    if (!g_open)
        return SKIP_EVENT_PENDING;

    /* First tick after arming: seed the edge baseline so a button already held
     * when ESC opened the prompt (e.g. an A held to fast-forward the dialogue)
     * isn't read as a fresh press. The choice box also needs an open-anim frame
     * before it accepts input, but the baseline seed is still correct here. */
    if (g_first) {
        g_first     = 0;
        g_prev_held = held;
        choice_box_poll(0, 1);
        return SKIP_EVENT_PENDING;
    }

    edge        = (uint16_t)(held & ~g_prev_held);
    g_prev_held = held;

    /* FUN_0046c320: poll the choice box, act on a committed option. */
    r = choice_box_poll(edge, 1);
    if (r == CB_OPT0) {            /* Yes → skip (FUN_00435612 teardown) */
        skip_event_close();
        return SKIP_EVENT_CONFIRMED;
    }
    if (r == CB_OPT1) {            /* No / B-cancel → resume the dialogue */
        skip_event_close();
        return SKIP_EVENT_CANCELLED;
    }
    return SKIP_EVENT_PENDING;     /* CB_BUSY — anim/nav this frame */
}
