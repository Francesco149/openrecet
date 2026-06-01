/*
 * test_scene1_dialogue.c — the .ivt script compiler (src/scene1_dialogue.c,
 * port of FUN_0046ddea). Pure: feeds synthetic scripts (no game assets) and
 * asserts the compiled command stream + dialogue text rows.
 */
#include "t.h"
#include "scene1_dialogue.h"

#include <string.h>

/* Find the i-th command of opcode `op`; -1 if fewer than i+1 exist. */
static int nth_op(const struct ive_program *p, enum ive_op op, int i)
{
    for (int k = 0; k < p->n_cmds; k++)
        if (p->cmds[k].op == op && i-- == 0) return k;
    return -1;
}

static int count_op(const struct ive_program *p, enum ive_op op)
{
    int n = 0;
    for (int k = 0; k < p->n_cmds; k++) if (p->cmds[k].op == op) n++;
    return n;
}

/* Comments (/), blanks, and tab-indented lines are skipped; a real command
 * compiles; the stream is END-terminated. */
int test_dialogue_skips_comments_and_terminates(void)
{
    static struct ive_program prog;
    const char *s =
        "// a comment\r\n"
        "\r\n"
        "\twait:30\r\n"          /* tab-indented → skipped */
        "wait:60\r\n";
    int ok = scene1_dialogue_parse(s, &prog);
    T_ASSERT(ok == 1);
    /* Exactly one WAIT (the tab-indented one is skipped) + the END. */
    T_ASSERT_EQ_I(count_op(&prog, IVE_OP_WAIT), 1);
    T_ASSERT_EQ_I(prog.cmds[prog.n_cmds - 1].op, IVE_OP_END);
    int w = nth_op(&prog, IVE_OP_WAIT, 0);
    T_ASSERT_EQ_I(prog.cmds[w].a1, 60);
    return 0;
}

/* An empty / all-comment script yields no real command (engine: parse-fail
 * → dialogue-disabled). */
int test_dialogue_empty_script_fails(void)
{
    static struct ive_program prog;
    int ok = scene1_dialogue_parse("// nothing here\r\n\r\n", &prog);
    T_ASSERT_EQ_I(ok, 0);
    T_ASSERT_EQ_I(prog.n_cmds, 1);          /* just END */
    T_ASSERT_EQ_I(prog.cmds[0].op, IVE_OP_END);
    return 0;
}

/* A msg line emits SPEAKER + SHOW [+ WAITKEY] [+ CLEAR], writes one text row
 * per <BR> segment, and the SHOW command spans the right rows. */
int test_dialogue_msg_rows_and_commands(void)
{
    static struct ive_program prog;
    const char *s = "msg:0:2:Hello<BR>there<KEY><C>\r\n";
    int ok = scene1_dialogue_parse(s, &prog);
    T_ASSERT(ok == 1);

    int sp = nth_op(&prog, IVE_OP_MSG_SPEAKER, 0);
    T_ASSERT(sp >= 0);
    T_ASSERT_EQ_I(prog.cmds[sp].a1, 0);     /* speaker a */
    T_ASSERT_EQ_I(prog.cmds[sp].a2, 2);     /* speaker b */

    int sh = nth_op(&prog, IVE_OP_MSG_SHOW, 0);
    T_ASSERT(sh >= 0);
    T_ASSERT_EQ_I(prog.cmds[sh].a1, 0);     /* row_start */
    T_ASSERT_EQ_I(prog.cmds[sh].a2, 2);     /* 2 rows (one <BR>) */

    /* SPEAKER, SHOW, WAITKEY, CLEAR all present, in that order. */
    T_ASSERT(sp < sh);
    T_ASSERT(sh < nth_op(&prog, IVE_OP_MSG_WAITKEY, 0));
    T_ASSERT(nth_op(&prog, IVE_OP_MSG_WAITKEY, 0) < nth_op(&prog, IVE_OP_MSG_CLEAR, 0));

    /* Text rows captured (markup stripped). */
    T_ASSERT_EQ_I(prog.n_rows, 2);
    T_ASSERT(strcmp(prog.glyph[0], "Hello") == 0);
    T_ASSERT(strcmp(prog.glyph[1], "there") == 0);
    return 0;
}

/* <W> appends a WAIT(10) after the msg; consecutive msgs stack rows. */
int test_dialogue_two_msgs_stack_rows(void)
{
    static struct ive_program prog;
    const char *s =
        "msg:1:1:one<KEY><W>\r\n"
        "msg:0:2:two<KEY>\r\n";
    int ok = scene1_dialogue_parse(s, &prog);
    T_ASSERT(ok == 1);
    T_ASSERT_EQ_I(count_op(&prog, IVE_OP_MSG_SHOW), 2);
    T_ASSERT_EQ_I(count_op(&prog, IVE_OP_MSG_WAITKEY), 2);
    T_ASSERT_EQ_I(count_op(&prog, IVE_OP_WAIT), 1);   /* the <W> on msg 1 */

    /* second SHOW starts on row 1 (msg 1 used row 0). */
    int sh1 = nth_op(&prog, IVE_OP_MSG_SHOW, 1);
    T_ASSERT_EQ_I(prog.cmds[sh1].a1, 1);
    T_ASSERT_EQ_I(prog.n_rows, 2);
    T_ASSERT(strcmp(prog.glyph[0], "one") == 0);
    T_ASSERT(strcmp(prog.glyph[1], "two") == 0);
    return 0;
}

/* bgset / se register names by slot and emit the matching command. */
int test_dialogue_bgset_and_se_name_tables(void)
{
    static struct ive_program prog;
    const char *s =
        "bgset:bmp/ivent/room.bmp\r\n"
        "se:bin/se/test.bin\r\n";
    int ok = scene1_dialogue_parse(s, &prog);
    T_ASSERT(ok == 1);
    T_ASSERT_EQ_I(prog.n_bg, 1);
    T_ASSERT(strcmp(prog.bg[0], "bmp/ivent/room.bmp") == 0);
    T_ASSERT_EQ_I(prog.n_se, 1);
    T_ASSERT(strcmp(prog.se[0], "bin/se/test.bin") == 0);
    int bg = nth_op(&prog, IVE_OP_BG, 0);
    T_ASSERT(bg >= 0 && prog.cmds[bg].a1 == 0);
    return 0;
}

/* rmb:a,b emits IVE_OP_RMB with both args incremented (engine `atoi+1`);
 * end: emits the IVE_OP_END_SCRIPT terminator (handler 0x46dd76, ret 3) ahead
 * of the always-appended NULL IVE_OP_END idle row. */
int test_dialogue_rmb_and_end_keywords(void)
{
    static struct ive_program prog;
    const char *s =
        "rmb:40,40\r\n"
        "end:\r\n";
    int ok = scene1_dialogue_parse(s, &prog);
    T_ASSERT(ok == 1);

    int r = nth_op(&prog, IVE_OP_RMB, 0);
    T_ASSERT(r >= 0);
    T_ASSERT_EQ_I(prog.cmds[r].a1, 41);     /* 40 + 1 */
    T_ASSERT_EQ_I(prog.cmds[r].a2, 41);

    int e = nth_op(&prog, IVE_OP_END_SCRIPT, 0);
    T_ASSERT(e >= 0);
    T_ASSERT(r < e);                        /* rmb before end */
    /* The compiler still appends the NULL idle terminator after end:. */
    T_ASSERT_EQ_I(prog.cmds[prog.n_cmds - 1].op, IVE_OP_END);
    T_ASSERT(e < prog.n_cmds - 1);
    return 0;
}

/* chr sub-ops: dir, the move:x,y pair, blend mode, disp. */
int test_dialogue_chr_subops(void)
{
    static struct ive_program prog;
    const char *s =
        "chr:1:dir:left\r\n"
        "chr:1:move:480,0\r\n"
        "chr:1:normal_shade\r\n"
        "chr:1:disp\r\n";
    int ok = scene1_dialogue_parse(s, &prog);
    T_ASSERT(ok == 1);

    int d = nth_op(&prog, IVE_OP_CHR_DIR, 0);
    T_ASSERT(d >= 0 && prog.cmds[d].a1 == 1 && prog.cmds[d].a2 == 0); /* left=0 */

    int mx = nth_op(&prog, IVE_OP_CHR_MOVE_X, 0);
    int my = nth_op(&prog, IVE_OP_CHR_MOVE_Y, 0);
    T_ASSERT(mx >= 0 && my == mx + 1);                 /* x then y, adjacent */
    T_ASSERT_EQ_I(prog.cmds[mx].a2, 480);
    T_ASSERT_EQ_I(prog.cmds[my].a2, 0);

    int bl = nth_op(&prog, IVE_OP_CHR_BLEND, 0);
    T_ASSERT(bl >= 0 && prog.cmds[bl].a2 == 0);        /* normal_shade=0 */
    T_ASSERT(nth_op(&prog, IVE_OP_CHR_DISP, 0) >= 0);
    return 0;
}
