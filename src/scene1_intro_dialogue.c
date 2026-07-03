/*
 * scene1_intro_dialogue.c — see the header. Drives iv1_1.ivt → iv1_2.ivt
 * through the scene1_dialogue_run interpreter and surfaces the TEXT_ANIM
 * anchor state.
 */
#include "scene1_intro_dialogue.h"

#include "scene1_dialogue.h"
#include "scene1_dialogue_run.h"
#include "call_trace.h"
#include "skip_event.h"   /* skip_event_open — trace the ESC skip-box flag */

#include <stddef.h>   /* NULL */

/* The opening prologue is scene selector 1, sub 1 then 2 (DAT_005c7a2c=1,
 * DAT_005c7a30 = 1 → 2). Pinned via runs/intro-script-probe. */
#define IVE_OPENING_SCENE 1
#define IVE_OPENING_SUB1  1
#define IVE_OPENING_SUB2  2

/* Inter-script loading bracket (iv1_1 → iv1_2). Retail raises the load overlay
 * for 68 frames here (Frida `…retail-20260601T193256Z`: LOADING_START #2 @ 4581,
 * LOADING_END #2 @ 4649) → the 2nd LOADING/HOUSE_FREEROAM pair. See
 * docs/findings/opening-prologue.md "the script-load / gate / transition
 * subsystem". PORT-DEBT: the 68 is synthetic — iv1_2's bg/chr/se assets aren't
 * loaded or rendered yet, so there is no real async load to time against; the
 * faithful part is the *structure* (a loading bracket in the right place, after
 * iv1_1's last line and before iv1_2's first). The residual ~35 frames of
 * gap #16 (retail 389 vs this ~354) is the deferred shatter/melt transition
 * (FUN_0045281c/004526f5), which is render and lands with the visual pass. */
#define IVE_INTERSCRIPT_LOAD_FRAMES 68

/* D_TUT = a post-prologue tutorial dialogue (iv1_5/iv1_6), armed by the focused
 * event dispatcher (scene1_tutorial_dispatch, the FUN_0044bd0d iv1_5/iv1_6
 * branches) via scene1_intro_dialogue_start_single().  One arbitrary (scene,sub)
 * script through the SAME shared g_rt the prologue uses — matching retail's single
 * dialogue runtime + gate (DAT_0438b1c8) — so the existing freeze/pose/render path
 * (which reads this module's g_rt via the accessors) drives it for free. */
/* D_TUT_LOAD = the tutorial dialogue's load bracket (retail FUN_00452d07 spawns
 * the LAB_00452aab worker thread → a brief DAT_0438b1c8=1 LOADING window before
 * the script runs).  On the item-display-2 recording retail emits LOADING_START →
 * CONV_POSE_START → LOADING_END across ~2 frames at the activation; the user's Z
 * inputs that advance the lines are gated AFTER that {wait LOADING_END}, so the
 * port must reproduce the bracket or the replay desyncs and the dialogue stalls
 * on line 0.  The conversation pose starts DURING the bracket (scene1_intro_
 * dialogue_posing), the box/text render only once the script is loaded (D_TUT). */
/* D_TUT_DONE = a one-frame settle latch after a tutorial script NATURALLY
 * completes (not a skip).  Models retail's gate-clear lag: DAT_0438b1c8 drops
 * 1→0 in FUN_004536cb's outer-loop tail, AFTER that frame's FUN_0044bd0d
 * dispatch has already run and seen it still busy, so the scheduler arms the
 * NEXT tutorial only the following frame.  See the D_TUT_DONE case + §119. */
enum { D_IDLE = 0, D_SCRIPT1, D_LOAD, D_SCRIPT2, D_DONE, D_TUT_LOAD, D_TUT, D_TUT_DONE };

/* Tutorial load-bracket length (frames _loading() reports true).  Retail's
 * bracket is the LAB_00452aab worker THREAD's wall-time, not a frame-counted
 * state machine (engine-quirks §119): the SAME capture ran iv1_5's bracket in 2
 * frames (15213→15215) and iv1_6's in 5 (15947→15952).  2 matches the clean
 * cold-start measurement; do NOT tune to a per-run thread duration.  For trace
 * comparison the `{tutloadpin:N}` op overrides this on BOTH sides (the Frida
 * agent extends retail's real bracket to the same N), killing the post-bracket
 * label shift — shop-display-menu-RE.md follow-up #8. */
#define IVE_TUT_LOAD_FRAMES 2

/* {tutloadpin:N} override (0 = unset → IVE_TUT_LOAD_FRAMES).  Set once at
 * segtrace load (main.c); harness-only, never written by game logic. */
static int g_tut_load_frames_pin = 0;

static int                g_state    = D_IDLE;
static int                g_load_ctr = 0;     /* frames elapsed in D_LOAD */
static struct ive_program g_prog;   /* reused per script (~160 KiB) */
static struct ive_runtime g_rt;
static unsigned           g_script_gen = 0;  /* bumps per loaded script (asset reload key) */
static int                g_tut_scene = 0;    /* D_TUT pending script (scene,sub) */
static int                g_tut_sub   = 0;

/* Sticky "the player has free control" latch — the FREEROAM_START anchor source.
 * Set the frame the opening PROLOGUE first completes (D_SCRIPT2 end or skip), never
 * cleared by the post-prologue tutorial dialogues (D_TUT) so FREEROAM_START fires
 * exactly once per prologue; cleared only by _arm/_reset (a fresh prologue).  On a
 * CONTINUE load the prologue never runs, so this stays 0 and the load path owns
 * FREEROAM_START — identical to the pre-D_TUT `g_state == D_DONE` behaviour. */
static int                g_freeroam_started = 0;

/* Per-frame standee-position probe for the execution-flow trace. Emits the
 * sliding standee's current+target X/Y at the engine dialogue-tick VA
 * (FUN_0046c320), read BEFORE this frame's tween runs — the same point retail's
 * Frida agent reads the standee table (DAT_073a3e70 + idx*0x1c, joined via
 * tools/flow/retail_fields.json). Settles whether the lines-5/44 standee
 * slide-in divergence is a frame-1:1 slide caught off-phase by the capture
 * (benign) or a real tween-cadence skew. Standee 5 = iv1_1's sliding Tear. */
static void emit_dialogue_calltrace(void)
{
    const struct ive_standee *s = &g_rt.scene.standees[5];
    CALL_TRACE_BEGIN(0x46c320);
    /* box-anim phase (the old --dlg-log columns, now flow-trace fields): box
     * open/close anim 0..15 (DAT_073a3e14), the per-line reveal counter
     * 0..0x800 (DAT_073a3e00), and the current line's first text row
     * (DAT_073a6a38). Read BEFORE this frame's tween, like the standee snapshot. */
    CALL_TRACE_I32("box_open", g_rt.box_open);
    CALL_TRACE_I32("reveal",   g_rt.reveal);
    CALL_TRACE_I32("line_row", g_rt.line_row);
    CALL_TRACE_I32("st5_active", s->field[IVE_ST_ACTIVE]);
    CALL_TRACE_F32("st5_x",  ive_word_f(s->field[1]));
    CALL_TRACE_F32("st5_y",  ive_word_f(s->field[2]));
    CALL_TRACE_F32("st5_tx", ive_word_f(s->field[3]));
    CALL_TRACE_F32("st5_ty", ive_word_f(s->field[4]));
    /* the ESC skip-event prompt timing (viewer note #3): the counter that
     * gates 'Do you want to skip this event?' + whether the box is up. */
    CALL_TRACE_I32("skip_prompt", g_rt.scene.skip_prompt);  /* DAT_073a3e18 */
    CALL_TRACE_I32("skipbox",     skip_event_open());        /* DAT_073a3dec */
    /* DIAG (prologue stall investigation): the input the runtime saw last tick +
     * the advance-gate inputs op_msg_waitkey checks (dwell>=15 && held&0x60). */
    CALL_TRACE_I32("prev_held",   g_rt.prev_held);
    CALL_TRACE_I32("dwell",       g_rt.dwell);
    CALL_TRACE_I32("revealed",    g_rt.revealed);
    CALL_TRACE_I32("dlg_wait",    g_rt.wait);
    CALL_TRACE_I32("dlg_cmd",     g_rt.cmd);
    {
        extern int g_segtrace_dbg_seg;   /* the trace segment the harness is parked on */
        CALL_TRACE_I32("seg", g_segtrace_dbg_seg);
    }
    CALL_TRACE_END();
}

void scene1_intro_dialogue_arm(void)
{
    g_state            = D_SCRIPT1;
    g_load_ctr         = 0;
    g_rt.active        = 0;     /* lazy-load iv1_1 on the first tick */
    g_rt.complete      = 0;
    g_freeroam_started = 0;     /* fresh prologue → FREEROAM_START not yet reached */
}

void scene1_intro_dialogue_reset(void)
{
    g_state            = D_IDLE;
    g_load_ctr         = 0;
    g_rt.active        = 0;
    g_rt.complete      = 0;
    g_freeroam_started = 0;
}

/* Load script (scene, sub) and arm the interpreter on it.  Returns 1 on success;
 * 0 (and the caller gives up) if the script is missing.  The opening prologue
 * passes IVE_OPENING_SCENE; the tutorial dispatcher passes its own (scene, sub). */
static int start_script(int scene, int sub)
{
#ifdef _WIN32
    /* scene1_dialogue_load pulls the .ivt from the storage layer (Win32-only).
     * In the host test build there is no storage, so the driver stays dormant;
     * the interpreter itself is exercised directly in test_scene1_dialogue_run. */
    if (!scene1_dialogue_load(scene, sub, &g_prog))
        return 0;
    ive_runtime_init(&g_rt, &g_prog);
    g_script_gen++;   /* the render pass reloads bg/chr assets on a new gen */
    return 1;
#else
    (void)scene; (void)sub;
    return 0;
#endif
}

void scene1_intro_dialogue_tick(uint16_t held)
{
    switch (g_state) {
    case D_SCRIPT1:
        if (!g_rt.active && !g_rt.complete &&
            !start_script(IVE_OPENING_SCENE, IVE_OPENING_SUB1)) {
            g_state = D_DONE;
            g_freeroam_started = 1;
            return;
        }
        emit_dialogue_calltrace();   /* standee snapshot BEFORE the tween */
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete) {       /* end: → ret 3; ive sets active=0 too */
            g_state       = D_LOAD; /* raise the inter-script loading bracket */
            g_load_ctr    = 0;
            g_rt.complete = 0;     /* clear so the script-2 lazy-load fires */
        }
        break;

    case D_LOAD:
        /* The iv1_1→iv1_2 load overlay. scene1_intro_dialogue_loading() reads
         * D_LOAD, which main.c folds into anchor_world.loading_active — so this
         * window drives LOADING_START/END #2 and (via the in-game-and-load-free
         * predicate) HOUSE_FREEROAM #2, replacing the scene1_intro_events stub.
         * The interpreter is idle here (dlg inactive), matching retail's gate==2
         * loading state. `held` is unused while loading. */
        (void)held;
        if (++g_load_ctr >= IVE_INTERSCRIPT_LOAD_FRAMES)
            g_state = D_SCRIPT2;   /* next tick lazy-loads iv1_2 */
        break;

    case D_SCRIPT2:
        if (!g_rt.active && !g_rt.complete &&
            !start_script(IVE_OPENING_SCENE, IVE_OPENING_SUB2)) {
            g_state = D_DONE;
            g_freeroam_started = 1;
            return;
        }
        emit_dialogue_calltrace();   /* standee snapshot BEFORE the tween */
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete) {
            g_state = D_DONE;
            g_freeroam_started = 1;   /* prologue complete → FREEROAM_START fires */
        }
        break;

    case D_TUT_LOAD:
        /* The tutorial dialogue's load bracket (retail's FUN_00452d07 worker).
         * _loading() reports true here → LOADING_START/END (+ HOUSE_FREEROAM,
         * matching retail's re-fire at the bracket), and the conversation pose
         * starts (_posing()) so CONV_POSE_START lands between START and END.  The
         * script is NOT loaded yet, so the box/text do not render this window. */
        (void)held;
        if (++g_load_ctr >= (g_tut_load_frames_pin > 0 ? g_tut_load_frames_pin
                                                       : IVE_TUT_LOAD_FRAMES))
            g_state = D_TUT;   /* next tick lazy-loads the script */
        break;

    case D_TUT:
        /* A post-prologue tutorial dialogue (iv1_5/iv1_6).  Lazily loads its
         * (scene,sub) on the first tick (like the prologue scripts), runs ONE
         * script, then drops back to dormant free-roam — the latch keeps _done()
         * set so FREEROAM_START does not re-fire.  Gated/serialised by the
         * dispatcher (one dialogue active at a time, retail DAT_0438b1c8). */
        if (!g_rt.active && !g_rt.complete &&
            !start_script(g_tut_scene, g_tut_sub)) {
            g_state = D_IDLE;   /* host build / missing script → dormant */
            return;
        }
        emit_dialogue_calltrace();
        ive_runtime_step(&g_rt, held);
        if (g_rt.complete) {
            g_state       = D_TUT_DONE;   /* one settle frame (retail gate lag) */
            g_rt.active   = 0;
            g_rt.complete = 0;
        }
        break;

    case D_TUT_DONE:
        /* The settle frame: iv1_5's script has completed but, like retail's gate
         * (DAT_0438b1c8) which clears 1→0 only AFTER FUN_0044bd0d ran that frame,
         * we keep _busy() true (and the pose on via _posing()) for ONE frame so
         * scene1_tutorial_dispatch_tick — which runs AFTER this tick in the sim —
         * sees "still busy" and defers the iv1_6 arm to the next frame.  Without
         * it the port arms iv1_6 the SAME frame iv1_5 completes, firing
         * LOADING_START 1 frame early (the iv1_5-tail slip: last CONV_POSE_BLINK→
         * CONV_POSE_END = 8f port vs 9f retail).  Retail ground truth from the
         * item-display-2 call-trace: iv1_5 FUN_0046c320-done @f15933, iv1_6
         * FUN_00452d07 load-spawn @f15934 — a 1-frame gap.  Dropping to D_IDLE
         * here lets the dispatch arm next frame (+9 from the blink, aligned), and
         * the same latch on iv1_6's own completion closes the matching iv1_6-tail
         * d=−2.  shop-display-menu-RE.md follow-up #8 / conversation-pose-driver.md. */
        (void)held;
        g_state = D_IDLE;
        break;

    case D_IDLE:
    case D_DONE:
    default:
        break;
    }
}

/* Arm a single arbitrary (scene,sub) dialogue script through the shared runtime —
 * the focused FUN_0044bd0d activation (set DAT_005c7a2c/30 + DAT_0438b1c8=2): the
 * tutorial dispatcher (scene1_tutorial_dispatch) calls this when iv1_5/iv1_6 trips.
 * The script loads lazily on the next _tick (the same point the prologue loads),
 * so _active() reports false until then.  Only meaningful post-prologue (the
 * dispatcher gates on no-dialogue-active); does not touch the FREEROAM_START
 * latch. */
void scene1_intro_dialogue_start_single(int scene, int sub)
{
    g_tut_scene   = scene;
    g_tut_sub     = sub;
    g_state       = D_TUT_LOAD;   /* the load bracket first (LOADING_START) */
    g_load_ctr    = 0;
    g_rt.active   = 0;     /* lazy-load when the bracket ends (D_TUT) */
    g_rt.complete = 0;
}

void scene1_intro_dialogue_skip_to_end(void)
{
    /* The ESC skip-event "Yes" teardown at port altitude. CORRECTION (user-
     * confirmed 2026-06-02): a skip ends only the *current* script, not the
     * whole prologue. The opening is a two-script sequence (iv1_1 → iv1_2);
     * skipping iv1_1 advances to iv1_2 (the 2nd dialogue, which plays over the
     * HOUSE free-roam map), and only skipping iv1_2 drops into free control.
     *
     * Engine mechanism: the skip teardown forces the running script to its
     * end:-equivalent — the dialogue gate (DAT_0438b1c8) drops to 0, then
     * FUN_0044baad arms the queued next script (DAT_06a4706c), exactly as a
     * natural end: does. So at this altitude a skip == "the current script
     * just completed": we replay the same state-machine transition the normal
     * g_rt.complete path takes in scene1_intro_dialogue_tick (D_SCRIPT1 →
     * D_LOAD → D_SCRIPT2, D_SCRIPT2 → D_DONE). Idempotent across states.
     *
     * PORT-DEBT(simplified, FUN_00473c03): the engine teardown also restores the
     * snapshot resume state (DAT_06a499a8 + FUN_00473c03's camera/player reseat),
     * irrelevant here — the dialogue plays over the already-spawned HOUSE. */
    switch (g_state) {
    case D_SCRIPT1:
        /* End iv1_1 → raise the inter-script load bracket → iv1_2 (same as the
         * natural g_rt.complete transition in _tick). */
        g_state       = D_LOAD;
        g_load_ctr    = 0;
        g_rt.active   = 0;
        g_rt.complete = 0;
        break;
    case D_LOAD:
        /* Skipping during the inter-script load → go straight to iv1_2. */
        g_state    = D_SCRIPT2;
        g_load_ctr = 0;
        break;
    case D_TUT:
        /* Skipping a RUNNING post-prologue tutorial (iv1_5/6/7) ends the script,
         * but — like the NATURAL g_rt.complete path (D_TUT → D_TUT_DONE) — keep
         * retail's 1-frame gate-clear settle: the engine's DAT_0438b1c8 clears 1→0
         * only the frame AFTER FUN_0044bd0d, so _busy()/_posing() hold one more
         * frame and the master tick (cc08 leave/dissolve completion) resumes the
         * frame AFTER, like retail.  Without it the port's wrap-up cutscene ends 1f
         * early ⇒ the cc08 exit + the WHOLE first-customer region run 1f ahead
         * (free-roam anim / bg_npc phase drift, viewer notes #20/#21/#11; RE §21.17). */
        g_state       = D_TUT_DONE;
        g_load_ctr    = 0;
        g_rt.active   = 0;
        g_rt.complete = 0;
        break;
    case D_TUT_LOAD:
    case D_TUT_DONE:
        /* Skipping the load bracket (no script ran yet) or the settle frame itself →
         * straight to dormant free-roam; the FREEROAM_START latch is unchanged (it
         * was already set by the prologue, or 0 on a load — a tutorial never owns it).
         * (D_TUT_DONE can't actually be skipped — _skippable() needs _active(), which
         * is false on the settle frame — but handle it as a tutorial-end so it never
         * falls through to the D_SCRIPT2 default's D_DONE/FREEROAM.) */
        g_state       = D_IDLE;
        g_load_ctr    = 0;
        g_rt.active   = 0;
        g_rt.complete = 0;
        break;
    case D_SCRIPT2:
    default:
        /* End iv1_2 (the last script) → free-roam. */
        g_state            = D_DONE;
        g_load_ctr         = 0;
        g_rt.active        = 0;
        g_rt.complete      = 0;
        g_freeroam_started = 1;
        break;
    }
}

int scene1_intro_dialogue_active(void)
{
    return ((g_state == D_SCRIPT1 || g_state == D_SCRIPT2 || g_state == D_TUT)
            && g_rt.active) ? 1 : 0;
}

int scene1_intro_dialogue_done(void)
{
    /* Sticky: set the frame the opening prologue first completes/skips (the moment
     * free control begins), so the FREEROAM_START rising edge fires once per
     * prologue.  Post-prologue tutorial dialogues (D_TUT) deliberately do NOT
     * clear it — they bounce g_state to D_IDLE on completion, which (pre-D_TUT)
     * would have read "not done" and re-fired the edge.  0 while dormant pre-arm
     * and on a CONTINUE load (no prologue → the load path owns FREEROAM_START). */
    return g_freeroam_started;
}

int scene1_intro_dialogue_covers_screen(void)
{
    /* Exact FUN_0046c869 gate: FUN_004547ab suppresses the scene + HUD block
     * (FUN_0040a765) behind an *active* dialogue iff DAT_073a3df0 — the count of
     * `bgset:` directives parsed from the ACTIVE script — is non-zero.  This is
     * per-SCRIPT, not per-phase: the opening iv1_1 carries a `bgset:` (full-
     * screen painted bg) → n_bg=1 → HUD suppressed; iv1_2 is an overlay over the
     * live HOUSE map (no `bgset:`) → n_bg=0 → HUD drawn behind it; every other
     * dialogue gates the same way.  When no dialogue is active the engine takes
     * the free-roam branch and always draws the HUD (DAT_0438b1c8 == 0), so we
     * report "not covering".  (`polybg:` is a *different* counter, DAT_073a3dfc /
     * n_polybg, and does NOT gate the HUD — only `bgset:` does.) */
    if (!scene1_intro_dialogue_active() || g_rt.prog == NULL)
        return 0;
    return (g_rt.prog->n_bg > 0) ? 1 : 0;
}

int scene1_intro_dialogue_skippable(void)
{
    /* FUN_0046c2cb gate: a line is up and skip_prompt (DAT_073a3e18, bumped
     * every dialogue frame by FUN_0046c320) is past 1. */
    return (scene1_intro_dialogue_active() && g_rt.scene.skip_prompt > 1) ? 1 : 0;
}

int scene1_intro_dialogue_loading(void)
{
    /* D_LOAD = the prologue iv1_1→iv1_2 bracket; D_TUT_LOAD = a tutorial
     * dialogue's activation bracket (retail FUN_00452d07 worker).  Both fold into
     * anchor_world.loading_active → LOADING_START/END (+ HOUSE_FREEROAM). */
    return (g_state == D_LOAD || g_state == D_TUT_LOAD) ? 1 : 0;
}

int scene1_intro_dialogue_busy(void)
{
    /* Retail's DAT_0438b1c8 != 0: a dialogue is armed/loading/active and NOT yet
     * finished — the whole lifecycle from activation to completion.  Unlike
     * _active() (which needs g_rt.active and so reads FALSE during the 1-frame
     * lazy-load gap at D_TUT_LOAD→D_TUT) this stays true across that gap, so the
     * tutorial dispatcher won't fire the next dialogue into the seam and clobber
     * the running one.  False only when dormant (D_IDLE) or the prologue is done
     * (D_DONE) — exactly the free-roam frames a tutorial may be activated. */
    return (g_state != D_IDLE && g_state != D_DONE) ? 1 : 0;
}

int scene1_intro_dialogue_blackout_active(void)
{
    /* Engine DAT_0438bf74 (armed by FUN_00452809): the screen-blackout flag.
     * Set at the opening-prologue dialogue DISPATCH (the iv1_1 fade-transition
     * entry — the dispatcher's FUN_00452d07(1) path falls through to
     * FUN_00452809) and cleared only at the FINAL cutscene-end gate-clear
     * (FUN_004547ab L50517/50633: DAT_0438b1c8 1->0) at the iv1_1 dialogue end.
     * It is the OPAQUE full-screen black quad of a scene transition, active ONLY
     * during iv1_1 (D_SCRIPT1) — the transition INTO the bedroom cutscene, whose
     * covering bg occludes it ([0], 0 net px).
     *
     * NOT re-armed for iv1_2 (D_SCRIPT2): iv1_2 is an OVERLAY over the live HOUSE
     * map (covers_screen==0), so the opaque blackout would draw AFTER the scene
     * and cover it black.  Retail's iv1_2 plainly renders the HOUSE scene behind
     * the Tear/Recette portraits (intro-iv2-gap golden cap_31), so retail does
     * not draw it there; an earlier gate that included D_SCRIPT2 BLACKED OUT the
     * port's iv1_2 — the intro-iv2-v3 window caught the regression.  D_LOAD (the
     * iv1_1->iv1_2 seam) draws nothing anyway (nowloading skips the render
     * dispatch), so the gate is simply D_SCRIPT1.
     *
     * The tutorial-dialogue path (D_TUT*) dispatches separately (start_single /
     * FUN_0044bd0d) and is excluded — those scenes (guild cutscenes etc.) get
     * the blackout wired when they're v3 draw-program verified.
     * PORT-DEBT(blackout-tut-dispatch). */
    return (g_state == D_SCRIPT1) ? 1 : 0;
}

int scene1_intro_dialogue_posing(void)
{
    /* The conversation-pose gate.  Equals _active() for the prologue (so that
     * behaviour is unchanged), PLUS the WHOLE tutorial (D_TUT_LOAD + D_TUT): retail
     * fires CONV_POSE_START during the load bracket, before the box/text render, and
     * holds the pose continuously until the script ends.  Covering D_TUT explicitly
     * (not via _active()) also bridges the 1-frame lazy-load seam where g_rt.active
     * is still 0 — without it the pose blips off there and fires a spurious
     * CONV_POSE_END/START pair.  (D_LOAD stays excluded — the prologue's inter-script
     * load deliberately blips the pose off.)  D_TUT_DONE (the one-frame post-script
     * settle latch) keeps the pose on for the completion frame too: retail still
     * poses on that frame (the blip-off lands at the NEXT dialogue's LOADING_START,
     * not at script-completion), so without it the pose would blip a frame early. */
    if (g_state == D_TUT_LOAD || g_state == D_TUT || g_state == D_TUT_DONE)
        return 1;
    return scene1_intro_dialogue_active();
}

int32_t scene1_intro_dialogue_text_reveal(void)
{
    return scene1_intro_dialogue_active() ? g_rt.reveal : 0;
}

int scene1_intro_dialogue_line_present(void)
{
    /* DAT_073a6a38 >= 0 — a dialogue line is currently shown (the box is open /
     * opening). Goes < 0 the frame a line is cleared (<C> / MSG_CLEAR), i.e. the
     * box-dismissed edge. Gated on dialogue-active. Feeds the DLG_LINE_CLEAR /
     * DLG_LINE_SHOW anchors that frame the between-lines (box-gone) gap. */
    return (scene1_intro_dialogue_active() && g_rt.line_row >= 0) ? 1 : 0;
}

int32_t scene1_intro_dialogue_fx_alpha(void)
{
    /* Max alpha (0-255) over the active "extra/effect" standees — index >= 2
     * (chr 0/1 are the persistent speaker characters; 4/5/6+ are the sigh / zzz
     * / sweat-drop pop-ups + the kuro fade-from-black). Gated on dialogue-active
     * so the EXTRA_SPRITE_* anchors don't fire on stale state. The scan range
     * mirrors the retail agent (anchorTick) so both sides agree. */
    if (!scene1_intro_dialogue_active())
        return 0;
    int maxa = 0;
    for (int i = 2; i <= 31 && i < IVE_STANDEE_COUNT; i++) {
        const int32_t *f = g_rt.scene.standees[i].field;
        if (f[IVE_ST_ACTIVE] == 0)
            continue;
        int a = (int)ive_word_f(f[18]);   /* current alpha */
        if (a < 0)   a = 0;
        if (a > 255) a = 255;
        if (a > maxa) maxa = a;
    }
    return maxa;
}

int scene1_intro_dialogue_text_revealed(void)
{
    return scene1_intro_dialogue_active() ? (g_rt.revealed ? 1 : 0) : 0;
}

const struct ive_runtime *scene1_intro_dialogue_runtime(void)
{
    return scene1_intro_dialogue_active() ? &g_rt : NULL;
}

/* Trace-harness `{phasepin}` for the dialogue screen-shake (rmb): zero the
 * bg/chr-shake countdowns so a fixed-offset capture lands on the UN-shaken base
 * pose on both sides.  The shake jitter is a per-frame RNG/load-phase artifact
 * (engine-quirks §105 + §85): retail's own value at a frozen frame is
 * load-dependent, so we normalize it in the TAS harness (the Frida side zeros
 * DAT_073a6d98/9c the same way) rather than re-seeding the shipped game, which
 * keeps shaking faithfully.  No-op when no dialogue runtime is live. */
void scene1_intro_dialogue_phasepin(void)
{
    g_rt.scene.shake_bg  = 0;
    g_rt.scene.shake_chr = 0;
}

/* Trace-harness `{tutloadpin:N}`: pin the tutorial load-bracket (D_TUT_LOAD)
 * length to N frames so it matches the retail bracket the Frida agent extends
 * to the same N (engine-quirks §119: retail's length is worker-thread wall-
 * time, so only a pinned EQUAL length makes the post-bracket label axes — and
 * the db054/wing-emit consumption inside the bracket — comparable).  N <= 0
 * clears the pin (back to IVE_TUT_LOAD_FRAMES). */
void scene1_intro_dialogue_set_tut_load_frames(int n)
{
    g_tut_load_frames_pin = (n > 0) ? n : 0;
}

const struct ive_program *scene1_intro_dialogue_program(void)
{
    return scene1_intro_dialogue_active() ? g_rt.prog : NULL;
}

unsigned scene1_intro_dialogue_generation(void)
{
    return g_script_gen;
}
