/*
 * test_skip_event.c — the ESC "skip this event?" prompt state machine
 * (src/skip_event.c) + the prologue skip teardown
 * (scene1_intro_dialogue_skip_to_end).
 *
 * The machine models the user-confirmed OBSERVABLE behavior (ESC → prompt,
 * cursor defaults to Yes, Left/Right toggles, A confirms, B cancels); the
 * exact engine counter choreography is PORT-DEBT pending a live golden — see
 * src/skip_event.h + docs/findings/esc-skip-event.md.
 */
#include "t.h"
#include "skip_event.h"
#include "scene1_intro_dialogue.h"

/* Button bits (mirror src/input.c input_binding_mask[]). */
#define B_RIGHT 0x0001
#define B_LEFT  0x0002
#define B_UP    0x0004
#define B_DOWN  0x0008
#define B_A     0x0010
#define B_B     0x0020

/* Each test arms from a clean slate; the machine is process-global static. */
static void reset_enabled(void)
{
    skip_event_close();
    g_skip_event_enabled = 1;
}

int test_skip_event_disabled_is_noop(void)
{
    skip_event_close();
    g_skip_event_enabled = 0;
    T_ASSERT_EQ_I(skip_event_arm(/*skippable=*/1), 0);
    T_ASSERT_EQ_I(skip_event_open(), 0);
    return 0;
}

int test_skip_event_arm_requires_skippable(void)
{
    reset_enabled();
    T_ASSERT_EQ_I(skip_event_arm(/*skippable=*/0), 0);
    T_ASSERT_EQ_I(skip_event_open(), 0);
    skip_event_close();
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_arm_opens(void)
{
    reset_enabled();
    T_ASSERT_EQ_I(skip_event_arm(1), 1);
    T_ASSERT_EQ_I(skip_event_open(), 1);
    T_ASSERT_EQ_I(skip_event_phase(), 1);        /* DAT_06a4999c = 1 on open */
    T_ASSERT_EQ_I(skip_event_selection(), 0);    /* cursor defaults to Yes   */
    skip_event_close();
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_rearm_is_idempotent(void)
{
    reset_enabled();
    skip_event_arm(1);
    skip_event_tick(0);          /* advance past the first-tick baseline */
    int phase = skip_event_phase();
    T_ASSERT_EQ_I(skip_event_arm(1), 1);   /* re-press → no-op, stays open */
    T_ASSERT_EQ_I(skip_event_open(), 1);
    T_ASSERT_EQ_I(skip_event_phase(), phase);  /* not re-reset to 1 */
    skip_event_close();
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_first_tick_swallows_input(void)
{
    /* A held at open-time (e.g. fast-forwarding dialogue) must NOT instant-
     * confirm: the first tick only seeds the edge baseline. */
    reset_enabled();
    skip_event_arm(1);
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_PENDING);
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_PENDING); /* held, no edge */
    T_ASSERT_EQ_I(skip_event_open(), 1);
    skip_event_close();
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_yes_confirms(void)
{
    reset_enabled();
    skip_event_arm(1);
    skip_event_tick(0);                                  /* baseline */
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_CONFIRMED);
    T_ASSERT_EQ_I(skip_event_open(), 0);                 /* auto-closed */
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_no_then_a_cancels(void)
{
    reset_enabled();
    skip_event_arm(1);
    skip_event_tick(0);                                  /* baseline */
    skip_event_tick(B_RIGHT);                            /* move cursor to No */
    T_ASSERT_EQ_I(skip_event_selection(), 1);
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_CANCELLED);
    T_ASSERT_EQ_I(skip_event_open(), 0);
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_b_cancels(void)
{
    reset_enabled();
    skip_event_arm(1);
    skip_event_tick(0);                                  /* baseline */
    T_ASSERT_EQ_I((int)skip_event_tick(B_B), (int)SKIP_EVENT_CANCELLED);
    T_ASSERT_EQ_I(skip_event_open(), 0);
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_cursor_toggles_back(void)
{
    reset_enabled();
    skip_event_arm(1);
    skip_event_tick(0);                                  /* baseline */
    skip_event_tick(B_RIGHT);                            /* → No  */
    T_ASSERT_EQ_I(skip_event_selection(), 1);
    skip_event_tick(0);                                  /* release */
    skip_event_tick(B_LEFT);                             /* → Yes */
    T_ASSERT_EQ_I(skip_event_selection(), 0);
    skip_event_close();
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_phase_climbs_to_cap(void)
{
    reset_enabled();
    skip_event_arm(1);
    for (int i = 0; i < 40; i++)
        skip_event_tick(0);
    T_ASSERT_EQ_I(skip_event_phase(), 0x0c);             /* capped */
    skip_event_close();
    g_skip_event_enabled = 0;
    return 0;
}

int test_skip_event_tick_closed_is_noop(void)
{
    skip_event_close();
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_PENDING);
    return 0;
}

int test_skip_to_end_forces_dormant(void)
{
    /* scene1_intro_dialogue_skip_to_end() drives the prologue dialogue to
     * D_DONE — active()/loading() report 0 (free-roam takes the tick). The
     * host build can't lazy-load a script (storage is Win32-only), so we
     * assert the post-condition holds from the armed state. */
    scene1_intro_dialogue_arm();          /* → D_SCRIPT1 */
    scene1_intro_dialogue_skip_to_end();
    T_ASSERT_EQ_I(scene1_intro_dialogue_active(), 0);
    T_ASSERT_EQ_I(scene1_intro_dialogue_loading(), 0);
    scene1_intro_dialogue_reset();
    return 0;
}
