/*
 * test_skip_event.c — the ESC "Do you want to skip this event?" glue
 * (src/skip_event.c): the FUN_0046c2cb arm gate + the FUN_0046c320 poll
 * branch, driving the generic choice box (the box's own state machine is
 * covered by test_choice_box.c). Plus the prologue skip teardown
 * (scene1_intro_dialogue_skip_to_end).
 */
#include "t.h"
#include "skip_event.h"
#include "scene1_intro_dialogue.h"

/* Button bits (mirror src/input.c input_binding_mask[]). */
#define B_RIGHT 0x0001
#define B_LEFT  0x0002
#define B_A     0x0010
#define B_B     0x0020

/* Each test arms from a clean slate; the machine is process-global static. */
static void reset_enabled(void)
{
    skip_event_close();
    g_skip_event_enabled = 1;
}

/* Tick with no input long enough for the choice box's grow-in anim to finish
 * (it ignores input until it caps) — leaves the prompt open + interactive. */
static void warmup(void)
{
    for (int i = 0; i < 5; i++)
        skip_event_tick(0);
}

/* Press `edge`, then drain the close anim (no input) until the prompt resolves
 * or the cap is hit. Returns the terminal skip_result_t. */
static skip_result_t commit(uint16_t edge)
{
    skip_result_t r = skip_event_tick(edge);
    for (int i = 0; i < 32 && r == SKIP_EVENT_PENDING && skip_event_open(); i++)
        r = skip_event_tick(0);
    return r;
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
    return 0;
}

int test_skip_event_arm_opens(void)
{
    reset_enabled();
    T_ASSERT_EQ_I(skip_event_arm(1), 1);
    T_ASSERT_EQ_I(skip_event_open(), 1);
    skip_event_close();
    return 0;
}

int test_skip_event_rearm_is_idempotent(void)
{
    reset_enabled();
    skip_event_arm(1);
    warmup();
    T_ASSERT_EQ_I(skip_event_arm(1), 1);   /* re-press → no-op, stays open */
    T_ASSERT_EQ_I(skip_event_open(), 1);
    /* and it can still be confirmed afterwards */
    T_ASSERT_EQ_I((int)commit(B_A), (int)SKIP_EVENT_CONFIRMED);
    return 0;
}

int test_skip_event_first_tick_swallows_input(void)
{
    /* A held at open-time (e.g. fast-forwarding dialogue) must NOT instant-
     * confirm: the first tick only seeds the edge baseline, and the box also
     * needs its grow-in frames before it reads input. */
    reset_enabled();
    skip_event_arm(1);
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_PENDING);
    T_ASSERT_EQ_I((int)skip_event_tick(B_A), (int)SKIP_EVENT_PENDING); /* held, no edge */
    T_ASSERT_EQ_I(skip_event_open(), 1);
    skip_event_close();
    return 0;
}

int test_skip_event_yes_confirms(void)
{
    reset_enabled();
    skip_event_arm(1);
    warmup();
    T_ASSERT_EQ_I((int)commit(B_A), (int)SKIP_EVENT_CONFIRMED);
    T_ASSERT_EQ_I(skip_event_open(), 0);                 /* auto-closed */
    return 0;
}

int test_skip_event_no_then_a_cancels(void)
{
    reset_enabled();
    skip_event_arm(1);
    warmup();
    skip_event_tick(B_RIGHT);                            /* move cursor to No */
    T_ASSERT_EQ_I((int)commit(B_A), (int)SKIP_EVENT_CANCELLED);
    T_ASSERT_EQ_I(skip_event_open(), 0);
    return 0;
}

int test_skip_event_b_cancels(void)
{
    reset_enabled();
    skip_event_arm(1);
    warmup();
    T_ASSERT_EQ_I((int)commit(B_B), (int)SKIP_EVENT_CANCELLED);
    T_ASSERT_EQ_I(skip_event_open(), 0);
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
    /* scene1_intro_dialogue_skip_to_end() ends only the CURRENT script (user-
     * confirmed: skipping iv1_1 advances to iv1_2, the 2nd dialogue over the
     * free-roam map; only skipping iv1_2 reaches free control). So a skip from
     * iv1_1 (D_SCRIPT1) lands in the inter-script load bracket (D_LOAD) — the
     * dialogue is not active, but it is loading the next script, NOT dormant. */
    scene1_intro_dialogue_arm();          /* → D_SCRIPT1 */
    scene1_intro_dialogue_skip_to_end();
    T_ASSERT_EQ_I(scene1_intro_dialogue_active(), 0);
    T_ASSERT_EQ_I(scene1_intro_dialogue_loading(), 1);   /* → iv1_2, not free-roam */

    /* A second skip (during the load) jumps straight to iv1_2 (D_SCRIPT2). The
     * host build can't lazy-load a script (storage is Win32-only), so iv1_2's
     * runtime stays inactive — active()/loading() both 0 from D_SCRIPT2. */
    scene1_intro_dialogue_skip_to_end();
    T_ASSERT_EQ_I(scene1_intro_dialogue_loading(), 0);

    /* Skipping the last script (iv1_2) → fully dormant (free control). */
    scene1_intro_dialogue_skip_to_end();
    T_ASSERT_EQ_I(scene1_intro_dialogue_active(), 0);
    T_ASSERT_EQ_I(scene1_intro_dialogue_loading(), 0);
    scene1_intro_dialogue_reset();
    return 0;
}
