/*
 * test_scene1_dialogue_run.c — the opening-prologue dialogue RUNTIME
 * (src/scene1_dialogue_run.c). Compiles a synthetic script, drives it with a
 * pulsed advance button (mirroring the intro-dialogue-lines trace), and
 * asserts the TEXT_ANIM_START/END anchor sequence + script completion.
 */
#include "t.h"
#include "scene1_dialogue.h"
#include "scene1_dialogue_run.h"

#define IVE_BTN_ADVANCE 0x10

/* One full drive: pulse A every other frame (edge 0x10 each even frame, like
 * the trace), counting START (reveal rises to 1) and END (revealed rises 0->1)
 * edges until the script completes or the frame cap is hit. */
struct drive_result { int starts; int ends; int complete; int frames; };

static struct drive_result drive(struct ive_runtime *rt, int cap)
{
    struct drive_result r = {0, 0, 0, 0};
    int32_t prev_reveal = rt->reveal;
    int     prev_revealed = rt->revealed;
    for (int f = 0; f < cap; f++) {
        uint16_t held = (f % 2 == 0) ? IVE_BTN_ADVANCE : 0;
        ive_runtime_step(rt, held);
        if (rt->reveal == 1 && prev_reveal != 1) r.starts++;
        if (rt->revealed && !prev_revealed)      r.ends++;
        prev_reveal   = rt->reveal;
        prev_revealed = rt->revealed;
        r.frames = f + 1;
        if (rt->complete) { r.complete = 1; break; }
    }
    return r;
}

/* A two-line script (msg + msg + end:) yields exactly two START and two END
 * edges and then completes; the gate drops on completion. */
int test_dialogue_run_two_lines_anchor_sequence(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    const char *s =
        "msg:0:2:Hello there<KEY>\r\n"
        "msg:1:1:Bye now<KEY>\r\n"
        "end:\r\n";
    T_ASSERT(scene1_dialogue_parse(s, &prog) == 1);

    ive_runtime_init(&rt, &prog);
    T_ASSERT(rt.active == 1);

    struct drive_result r = drive(&rt, 400);
    T_ASSERT_EQ_I(r.complete, 1);
    T_ASSERT_EQ_I(r.starts, 2);     /* one reveal-start per dialogue line */
    T_ASSERT_EQ_I(r.ends, 2);       /* one reveal-complete per line       */
    T_ASSERT_EQ_I(rt.active, 0);    /* end: dropped the dialogue gate      */
    T_ASSERT_EQ_I(rt.line_idx, 2);  /* two lines walked                    */
    return 0;
}

/* START fires the frame the line is shown (reveal latches to 1); END only
 * after the reveal counter has climbed — never on the same frame as START. */
int test_dialogue_run_start_precedes_end(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse("msg:0:2:Hi<KEY>\r\nend:\r\n", &prog) == 1);
    ive_runtime_init(&rt, &prog);

    /* Frame 1 (no input): walks SPEAKER+SHOW, completion latches START. */
    ive_runtime_step(&rt, 0);
    T_ASSERT_EQ_I(rt.reveal, 1);        /* START */
    T_ASSERT_EQ_I(rt.revealed, 0);      /* not yet fully revealed */
    T_ASSERT_EQ_I(rt.line_row, 0);      /* line shown */

    /* Frame 2 (advance edge): reveal slams to max → END latches. */
    ive_runtime_step(&rt, IVE_BTN_ADVANCE);
    T_ASSERT_EQ_I(rt.reveal, 0x800);
    T_ASSERT_EQ_I(rt.revealed, 1);      /* END */
    return 0;
}

/* The WAITKEY gate holds for >=15 dwell frames after END before an advance is
 * accepted (DAT_073a3e08 >= 0xf). */
int test_dialogue_run_waitkey_dwell_gate(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    /* Two lines so we can detect the advance to line 2. */
    T_ASSERT(scene1_dialogue_parse(
        "msg:0:2:A<KEY>\r\nmsg:1:1:B<KEY>\r\nend:\r\n", &prog) == 1);
    ive_runtime_init(&rt, &prog);

    /* Pulse advance every other frame (edge recurs; a held button only edges
     * once). The first press reveals + ENDs line 1, but the gate must still
     * wait out the dwell before line 2 is shown. */
    int line2_at = -1;
    for (int f = 0; f < 60 && line2_at < 0; f++) {
        uint16_t held = (f % 2 == 0) ? IVE_BTN_ADVANCE : 0;
        ive_runtime_step(&rt, held);
        if (rt.line_idx == 2) line2_at = f;   /* SHOW of line 2 ran */
    }
    T_ASSERT(line2_at >= 0);
    /* END latches frame 1 (dwell starts), so line 2 cannot appear until the
     * dwell gate (15) is cleared — well past the first few frames. */
    T_ASSERT(line2_at >= 15);
    return 0;
}
