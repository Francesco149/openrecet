/*
 * test_choice_box.c — the engine's generic choice box (src/choice_box.c,
 * FUN_00434def / FUN_00434ed2 / FUN_00434dbf). Pure-C state machine: open
 * lays the prompt out + starts the grow-in anim; poll runs nav / confirm /
 * close and returns the committed option once the close anim finishes.
 */
#include "t.h"
#include "choice_box.h"

#include <string.h>

#define B_RIGHT CB_BTN_RIGHT
#define B_LEFT  CB_BTN_LEFT
#define B_A     CB_BTN_A
#define B_B     CB_BTN_B

/* Poll with no input until the box becomes interactive (anim caps at 4).
 * Returns the number of polls it took. */
static int run_to_interactive(void)
{
    int n = 0;
    while (choice_box_anim() < 4 && n < 16) {
        choice_box_poll(0, 1);
        n++;
    }
    return n;
}

/* Poll `edge` once, then poll with no input until the box closes (or cap).
 * Returns the terminal code (CB_OPT0 / CB_OPT1) — or CB_BUSY if it never
 * closed within the cap. */
static int commit_and_drain(uint16_t edge)
{
    int r = choice_box_poll(edge, 1);
    int n = 0;
    while (r == CB_BUSY && choice_box_active() && n < 32) {
        r = choice_box_poll(0, 1);
        n++;
    }
    return r;
}

int test_choice_box_open_lays_state(void)
{
    choice_box_reset();
    choice_box_open("Do you want to skip this event?", 1, 0);
    T_ASSERT_EQ_I(choice_box_active(), 1);
    T_ASSERT_EQ_I(choice_box_selection(), 0);
    T_ASSERT_EQ_I(choice_box_options(), 1);   /* single prompt row */
    T_ASSERT(strcmp(choice_box_text(), "Do you want to skip this event?") == 0);
    return 0;
}

int test_choice_box_open_anim_then_interactive(void)
{
    choice_box_reset();
    choice_box_open("Q", 1, 0);
    /* grows 1→4 over the first polls; not yet committable */
    int polls = run_to_interactive();
    T_ASSERT_EQ_I(choice_box_anim(), 4);
    T_ASSERT(polls >= 3 && polls <= 5);
    return 0;
}

int test_choice_box_yes_returns_opt0(void)
{
    choice_box_reset();
    choice_box_open("Q", 1, 0);
    run_to_interactive();
    T_ASSERT_EQ_I(commit_and_drain(B_A), CB_OPT0);
    T_ASSERT_EQ_I(choice_box_active(), 0);   /* auto-closed */
    return 0;
}

int test_choice_box_no_returns_opt1(void)
{
    choice_box_reset();
    choice_box_open("Q", 1, 0);
    run_to_interactive();
    /* move cursor to No (RIGHT while on Yes), then confirm */
    choice_box_poll(B_RIGHT, 1);
    T_ASSERT_EQ_I(choice_box_selection(), 1);
    T_ASSERT_EQ_I(commit_and_drain(B_A), CB_OPT1);
    return 0;
}

int test_choice_box_b_cancels_to_opt1(void)
{
    choice_box_reset();
    choice_box_open("Q", /*mode=*/1, 0);
    run_to_interactive();
    T_ASSERT_EQ_I(commit_and_drain(B_B), CB_OPT1);
    T_ASSERT_EQ_I(choice_box_active(), 0);
    return 0;
}

int test_choice_box_b_ignored_when_mode0(void)
{
    choice_box_reset();
    choice_box_open("Q", /*mode=*/0, 0);   /* B is not a cancel in mode 0 */
    run_to_interactive();
    int r = choice_box_poll(B_B, 1);
    T_ASSERT_EQ_I(r, CB_BUSY);
    T_ASSERT_EQ_I(choice_box_active(), 1);
    return 0;
}

int test_choice_box_cursor_toggles_both_ways(void)
{
    choice_box_reset();
    choice_box_open("Q", 1, 0);
    run_to_interactive();
    choice_box_poll(B_RIGHT, 1);
    T_ASSERT_EQ_I(choice_box_selection(), 1);   /* Yes → No */
    choice_box_poll(B_LEFT, 1);
    T_ASSERT_EQ_I(choice_box_selection(), 0);   /* No → Yes */
    /* RIGHT again from Yes works (not a one-shot) */
    choice_box_poll(B_RIGHT, 1);
    T_ASSERT_EQ_I(choice_box_selection(), 1);
    return 0;
}

int test_choice_box_default_sel_honoured(void)
{
    choice_box_reset();
    choice_box_open("Q", 1, /*sel=*/1);   /* open with No preselected */
    run_to_interactive();
    T_ASSERT_EQ_I(choice_box_selection(), 1);
    T_ASSERT_EQ_I(commit_and_drain(B_A), CB_OPT1);
    return 0;
}

int test_choice_box_reset_closes(void)
{
    choice_box_reset();
    choice_box_open("Q", 1, 0);
    run_to_interactive();
    choice_box_reset();
    T_ASSERT_EQ_I(choice_box_active(), 0);
    T_ASSERT_EQ_I(choice_box_poll(B_A, 1), CB_INACTIVE);
    return 0;
}

int test_choice_box_poll_when_closed_is_inactive(void)
{
    choice_box_reset();
    T_ASSERT_EQ_I(choice_box_poll(B_A, 1), CB_INACTIVE);
    return 0;
}
