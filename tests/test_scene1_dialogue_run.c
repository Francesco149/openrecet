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

/* ─── bg handler (bgset 0x46d912) ────────────────────────────────────────── */

/* bgset:<name> stores the name at parse time (prog->bg[slot], n_bg++) and, at
 * runtime, sets the active bg index (DAT_073a6d90) + clears the scroll px
 * (DAT_073a6d84). Two bgset commands → the last one wins. */
int test_dialogue_run_bgset_sets_active_index(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "bgset:room0.tga\r\nbgset:room1.tga\r\nmsg:0:1:A<KEY>\r\nend:\r\n",
        &prog) == 1);
    T_ASSERT_EQ_I(prog.n_bg, 2);

    ive_runtime_init(&rt, &prog);
    T_ASSERT_EQ_I(rt.scene.bg_index, 0);   /* reset default */

    /* One step runs the setup ops (both bgset, then the SPEAKER) up to the
     * first SHOW yield — so the second bgset (slot 1) is the active index. */
    ive_runtime_step(&rt, 0);
    T_ASSERT_EQ_I(rt.scene.bg_index, 1);
    T_ASSERT_EQ_I(rt.scene.bg_scroll, 0);
    return 0;
}

/* ─── se: voice / SE bridge (IVE_OP_SE → g_ive_se_play_fn) ─────────────────── */

/* Capture stub for the audio bridge. The Win32 backend installs
 * audio_play_se_file here at audio_init; in the host build it's NULL (silent
 * no-op) unless a test wires it, so we capture the path it would have played. */
static char  g_se_capture[8][256];
static int   g_se_capture_n;
static void  se_capture_stub(const char *path)
{
    if (g_se_capture_n < 8)
        snprintf(g_se_capture[g_se_capture_n], 256, "%s", path ? path : "(null)");
    g_se_capture_n++;
}

/* se:<bin> stores the path in prog->se[slot] at parse time and, at runtime,
 * fires the bridge with that exact path the frame the walk reaches it (ret 1,
 * same frame as the following SHOW). Each se: plays once per script run. */
int test_dialogue_run_se_fires_voice_bridge(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "se:bin/se/01ti/event/tea_mataku.bin\r\n"
        "msg:0:2:Good grief<KEY><C>\r\n"
        "se:bin/se/wav/piko.bin\r\n"
        "msg:0:2:Beep<KEY><C>\r\n"
        "end:\r\n", &prog) == 1);
    /* Both se: names captured at parse, in order. */
    T_ASSERT_EQ_I(prog.n_se, 2);
    T_ASSERT(strcmp(prog.se[0], "bin/se/01ti/event/tea_mataku.bin") == 0);
    T_ASSERT(strcmp(prog.se[1], "bin/se/wav/piko.bin") == 0);

    g_se_capture_n = 0;
    g_ive_se_play_fn = se_capture_stub;
    ive_runtime_init(&rt, &prog);

    struct drive_result r = drive(&rt, 400);
    g_ive_se_play_fn = NULL;     /* restore — don't leak into later tests */

    T_ASSERT_EQ_I(r.complete, 1);
    /* Exactly two voice/SE plays, each with the right path, in script order. */
    T_ASSERT_EQ_I(g_se_capture_n, 2);
    T_ASSERT(strcmp(g_se_capture[0], "bin/se/01ti/event/tea_mataku.bin") == 0);
    T_ASSERT(strcmp(g_se_capture[1], "bin/se/wav/piko.bin") == 0);
    return 0;
}

/* A NULL bridge (test build / before audio_init) is a silent no-op: se:
 * commands still parse + walk, the script completes, nothing crashes. */
int test_dialogue_run_se_null_bridge_is_noop(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "se:bin/se/wav/piko.bin\r\nmsg:0:1:A<KEY>\r\nend:\r\n", &prog) == 1);

    g_ive_se_play_fn = NULL;
    ive_runtime_init(&rt, &prog);
    struct drive_result r = drive(&rt, 200);
    T_ASSERT_EQ_I(r.complete, 1);
    return 0;
}

/* ─── chr standee handlers (settled-state subset) ─────────────────────────── */

/* grp registers a graphic + slot, disp activates (a2=1), moveto sets the TARGET
 * only (current tweens toward it — engine 0x46da6e), dir sets the mirror flag. */
int test_dialogue_run_chr_disp_grp_moveto_dir(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "chr:1:grp:tear.tga 128,256\r\n"
        "chr:1:moveto:300,400\r\n"
        "chr:1:dir:right\r\n"
        "chr:1:disp\r\n"
        "msg:0:1:A<KEY>\r\nend:\r\n", &prog) == 1);
    T_ASSERT_EQ_I(prog.n_chrname, 1);
    T_ASSERT_EQ_I(prog.chr_w[0], 128);
    T_ASSERT_EQ_I(prog.chr_h[0], 256);

    ive_runtime_init(&rt, &prog);
    ive_runtime_step(&rt, 0);   /* run the setup ops up to the first SHOW */

    const struct ive_standee *s = &rt.scene.standees[1];
    T_ASSERT_EQ_I(s->field[IVE_ST_ACTIVE],  1);   /* disp → active            */
    T_ASSERT_EQ_I(s->field[IVE_ST_GRAPHIC], 0);   /* grp slot 0               */
    T_ASSERT_EQ_I(s->field[IVE_ST_MIRROR],  1);   /* dir:right                */
    T_ASSERT(ive_word_f(s->field[3]) == 300.0f);  /* moveto x → TARGET only   */
    T_ASSERT(ive_word_f(s->field[4]) == 400.0f);  /* moveto y → TARGET only   */
    /* disp runs in the command walk AFTER the tween loop, so on this first
     * step the standee isn't tweened yet — current stays at the reset 0. */
    T_ASSERT(ive_word_f(s->field[1]) == 0.0f);
    T_ASSERT(ive_word_f(s->field[2]) == 0.0f);
    /* an untouched standee stays inactive */
    T_ASSERT_EQ_I(rt.scene.standees[0].field[IVE_ST_ACTIVE], 0);
    return 0;
}

/* col:r,g,b,a unpacks (engine pack a<<24|r<<16|g<<8|b) into the 4 CURRENT colour
 * floats (field15=b,16=g,17=r,18=a); col does NOT touch the delta fields
 * (19-22), which stay at their reset 255 (only chr:colto writes them). */
int test_dialogue_run_chr_col_channels(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "chr:1:col:10,20,30,40\r\nmsg:0:1:A<KEY>\r\nend:\r\n", &prog) == 1);
    ive_runtime_init(&rt, &prog);
    ive_runtime_step(&rt, 0);

    const struct ive_standee *s = &rt.scene.standees[1];
    T_ASSERT(ive_word_f(s->field[15]) == 30.0f);  /* b */
    T_ASSERT(ive_word_f(s->field[16]) == 20.0f);  /* g */
    T_ASSERT(ive_word_f(s->field[17]) == 10.0f);  /* r */
    T_ASSERT(ive_word_f(s->field[18]) == 40.0f);  /* a */
    T_ASSERT(ive_word_f(s->field[19]) == 255.0f); /* delta untouched by col */
    return 0;
}

/* chr:colto fade: col sets the current colour, fadeframe sets the frame count,
 * colto computes the per-frame delta toward the target (field19-22) + the
 * countdown (field10 = field9), and the tween applies it. This is the kuro
 * black-overlay fade-from-black + the sigh/zzz effect fades. */
int test_dialogue_run_chr_colto_fade(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "chr:1:col:255,255,255,255\r\n"
        "chr:1:fadeframe:10\r\n"
        "chr:1:colto:255,255,255,0\r\n"
        "chr:1:disp\r\n"
        "msg:0:1:A<KEY>\r\nend:\r\n", &prog) == 1);
    ive_runtime_init(&rt, &prog);
    ive_runtime_step(&rt, 0);   /* setup ops; disp activates AFTER the tween */

    struct ive_standee *s = &rt.scene.standees[1];
    T_ASSERT(ive_word_f(s->field[18]) == 255.0f); /* alpha: full, not yet faded */
    T_ASSERT_EQ_I(s->field[9], 10);               /* fadeframe count            */
    T_ASSERT_EQ_I(s->field[10], 10);              /* colto countdown = field9   */
    /* delta a (field22) = (0 - 255) / 10 = -25.5 */
    T_ASSERT(ive_word_f(s->field[22]) < -25.0f && ive_word_f(s->field[22]) > -26.0f);

    /* 10 more steps drain the countdown → alpha fades to ~0 (255 - 10*25.5). */
    for (int i = 0; i < 10; i++) ive_runtime_step(&rt, 0);
    T_ASSERT_EQ_I(s->field[10], 0);
    T_ASSERT(ive_word_f(s->field[18]) < 1.0f);
    return 0;
}

/* chr:move sets current AND target; chr:speed sets the per-frame step (×1000
 * fixed-point → /1000); the tween slides current toward target by speed. This
 * is Tear's slide-in (move:-390 → moveto:-100 speed:5 → 5 px/frame). */
int test_dialogue_run_chr_move_tween(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse(
        "chr:1:move:-390,0\r\n"
        "chr:1:moveto:-100,0\r\n"
        "chr:1:speed:5\r\n"
        "chr:1:disp\r\n"
        "msg:0:1:A<KEY>\r\nend:\r\n", &prog) == 1);
    ive_runtime_init(&rt, &prog);
    ive_runtime_step(&rt, 0);   /* setup ops; current = -390, target = -100 */

    struct ive_standee *s = &rt.scene.standees[1];
    T_ASSERT(ive_word_f(s->field[1]) == -390.0f); /* move → current */
    T_ASSERT(ive_word_f(s->field[3]) == -100.0f); /* moveto → target */
    T_ASSERT(ive_word_f(s->field[5]) == 5.0f);    /* speed:5 → 5.0 px/frame */

    /* Each step slides current +5 toward -100; -390→-100 = 290 px / 5 = 58. */
    ive_runtime_step(&rt, 0);
    T_ASSERT(ive_word_f(s->field[1]) == -385.0f);
    for (int i = 0; i < 80; i++) ive_runtime_step(&rt, 0);
    T_ASSERT(ive_word_f(s->field[1]) == -100.0f); /* settled at target */
    return 0;
}

/* Natural (no-input) typewriter reveal is CHARACTER-based: budget =
 * (reveal-4)*32/32 chars; the line is END (revealed) once the budget clears
 * every row's ive_row_count with >2 to spare. For a 1-row "ABCDE" (count 4),
 * revealed iff (reveal-4) - 4 > 2, i.e. reveal >= 11. Locks the fix that lets a
 * settled line auto-complete (book icon appears) so ONE advance press moves on,
 * instead of the old nominal-pixel metric that never completed. */
int test_dialogue_run_natural_reveal_char_budget(void)
{
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse("msg:0:1:ABCDE<KEY>\r\nend:\r\n", &prog) == 1);
    ive_runtime_init(&rt, &prog);

    ive_runtime_step(&rt, 0);              /* SHOW yields; reveal latches to 1 */
    T_ASSERT_EQ_I(rt.revealed, 0);

    /* Climb the reveal counter with no input; it must NOT complete before
     * reveal 11, and must be complete at 11. */
    int flip = -1;
    for (int i = 0; i < 40 && flip < 0; i++) {
        ive_runtime_step(&rt, 0);
        if (rt.revealed) flip = rt.reveal;
    }
    T_ASSERT_EQ_I(flip, 11);
    return 0;
}

/* ─── box open/close scale (FUN_0046c86f) ─────────────────────────────────── */

int test_dialogue_box_scale_open_and_closing(void)
{
    float sx, sy; int a;

    /* Fully open (15), not closing: alpha clamps to 255, scale ≈ 1. */
    ive_box_scale(15, &sx, &sy, &a, 0);
    T_ASSERT_EQ_I(a, 0xff);
    T_ASSERT(sx > 0.95f && sx < 1.05f);
    T_ASSERT(sy > 0.95f && sy < 1.05f);

    /* Closing path at open 15: sx=1, sy = 1-(15-15)*0.15 = 1, alpha = 15*50-495 = 255. */
    ive_box_scale(15, &sx, &sy, &a, 1);
    T_ASSERT(sx == 1.0f);
    T_ASSERT(sy == 1.0f);
    T_ASSERT_EQ_I(a, 0xff);

    /* Closing, nearly shut (open 1): sy = 1-(15-1)*0.15 = -1.1 → clamped 0. */
    ive_box_scale(1, &sx, &sy, &a, 1);
    T_ASSERT(sy == 0.0f);
    return 0;
}

/* ─── scene-state reset (FUN_0046c0ae) ───────────────────────────────────── */

/* Every standee in the 200-entry table gets the exact init bit patterns: all
 * fields zero except f[3]=800.0f, f[5]=f[6]=2.0f, f[15..22]=255.0f. A layout
 * slip in struct ive_standee (the negative-offset draw indices) fails here
 * before any visual pass. */
int test_dialogue_scene_state_reset_standee_defaults(void)
{
    static struct ive_scene_state s;
    /* Poison first so we know reset clears it. */
    for (int i = 0; i < IVE_STANDEE_COUNT; i++)
        for (int k = 0; k < IVE_STANDEE_FIELDS; k++)
            s.standees[i].field[k] = (int32_t)0xDEADBEEF;

    ive_scene_state_reset(&s);

    for (int i = 0; i < IVE_STANDEE_COUNT; i++) {
        const int32_t *f = s.standees[i].field;
        for (int k = 0; k < IVE_STANDEE_FIELDS; k++) {
            int32_t want = 0;
            if (k == 3)                 want = 0x44480000;  /* 800.0f */
            else if (k == 5 || k == 6)  want = 0x40000000;  /* 2.0f   */
            else if (k >= 15 && k <= 22) want = 0x437f0000; /* 255.0f */
            T_ASSERT_EQ_I(f[k], want);
        }
        /* Semantic accessors land on the right fields. */
        T_ASSERT_EQ_I(f[IVE_ST_X],       0);
        T_ASSERT_EQ_I(f[IVE_ST_ACTIVE],  0);
        T_ASSERT_EQ_I(f[IVE_ST_MIRROR],  0);
        T_ASSERT_EQ_I(f[IVE_ST_GRAPHIC], 0);
        T_ASSERT_EQ_I(f[IVE_ST_BLEND],   0);
        T_ASSERT_EQ_I(f[IVE_ST_W],       0x44480000);
        T_ASSERT_EQ_I(f[IVE_ST_COL_R],   0x437f0000);
        T_ASSERT_EQ_I(f[IVE_ST_COL_TR],  0x437f0000);
    }
    return 0;
}

/* The scene scalars get their FUN_0046c0ae values; window_open_ctr/choice_mode
 * init to -1, the rest to 0. ive_runtime_init must wire the reset through. */
int test_dialogue_scene_state_reset_scalars(void)
{
    static struct ive_scene_state s;
    s.bg_active = 9; s.bg_index = 9; s.window_open_ctr = 9; s.choice_mode = 9;
    ive_scene_state_reset(&s);
    T_ASSERT_EQ_I(s.bg_active,       0);
    T_ASSERT_EQ_I(s.bg_fade,         0);
    T_ASSERT_EQ_I(s.bg_scroll,       0);
    T_ASSERT_EQ_I(s.bg_index,        0);
    T_ASSERT_EQ_I(s.shake_bg,        0);
    T_ASSERT_EQ_I(s.shake_chr,       0);
    T_ASSERT_EQ_I(s.window_open_ctr, -1);
    T_ASSERT_EQ_I(s.choice_mode,     -1);

    /* And ive_runtime_init runs the reset on its embedded scene state. */
    static struct ive_program prog;
    static struct ive_runtime rt;
    T_ASSERT(scene1_dialogue_parse("msg:0:1:A<KEY>\r\nend:\r\n", &prog) == 1);
    rt.scene.bg_index = 7;
    ive_runtime_init(&rt, &prog);
    T_ASSERT_EQ_I(rt.scene.bg_index,        0);
    T_ASSERT_EQ_I(rt.scene.window_open_ctr, -1);
    return 0;
}
