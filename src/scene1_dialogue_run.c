/*
 * scene1_dialogue_run.c — opening-prologue dialogue runtime. See the header.
 *
 * Ports FUN_0046c320 (per-frame update) + the reveal-completion tail of
 * FUN_0046c9a2 (the DAT_073a3e04 END latch / DAT_073a3e00 START reset). The
 * draw calls / glyph rasterization are deferred; this drives the data model +
 * the TEXT_ANIM_START/END anchor signals. Handler ground truth: raw-disasm of
 * the 0x46d8xx–0x46ddxx stubs, see docs/findings/opening-prologue.md
 * §"handler bodies + runtime tick".
 */
#include "scene1_dialogue_run.h"

#include <math.h>

/* Audio bridge — NULL in the test build and before audio_init (see header).
 * audio.c installs audio_play_se_file here so se: commands play voice/SE. */
ive_se_play_fn_t g_ive_se_play_fn = NULL;

/* Input bits (g_input_state[N].buttons — binding slots, see input.h). */
#define IVE_BTN_ADVANCE   0x10   /* face button A (confirm/advance)          */
#define IVE_BTN_FF        0x60   /* held 0x20|0x40 = fast-forward / held-adv  */
#define IVE_BTN_FF_TURBO  0x40   /* held → 0x50 internal steps + reveal slam  */
#define IVE_BTN_FF_X2     0x20   /* held → 2 internal steps                   */

/* Reveal cap (DAT_073a3e00 ceiling) + the WAITKEY dwell gate. */
#define IVE_REVEAL_MAX    0x800
#define IVE_DWELL_GATE    15     /* DAT_073a3e08 >= 0xf before advance accepted */
#define IVE_BOX_OPEN_MAX  0xf    /* DAT_073a3e14 fully-open                    */

/*
 * Reveal-completion budget (the FUN_0046c9a2 tail). The reveal is CHARACTER-
 * based, not pixel-based (retail probe runs/dlg-reveal-probe: a 2-row ~42-char
 * line reveals — DAT_073a3e04 rises 0→1 — at reveal≈47 with speed 32, i.e.
 * ~1 char/frame). budget = (reveal-4)*speed/32 logical characters; each row
 * subtracts FUN_00405a52's return (the row's full logical char count, minus the
 * final = ive_row_count); the line is fully revealed (END) iff the budget still
 * clears every row by >2. `speed` is DAT_005c78dc = DAT_005c78e0[idx] =
 * {16,32,1024} (slow/normal/fast); normal (32) is the fresh-config default
 * confirmed live. (The earlier nominal-pixel metric made the typewriter take
 * ~640 frames, so a settled line never auto-completed — the player had to press
 * Z to slam the reveal, then again to advance.) */
#define IVE_REVEAL_SPEED  32     /* DAT_005c78dc, normal text speed           */

/* Walk-loop handler return codes (the 0x46c320 contract). */
enum { IVE_R_STOP = 0, IVE_R_CONTINUE = 1, IVE_R_YIELD = 2, IVE_R_COMPLETE = 3 };

/* FUN_0046c86f — the dialogue box open/close scale + alpha "wobble". Pure math
 * (the engine calls FUN_00503a44 = SIN twice with the same angle param1·3π/15).
 * `closing` is the engine's `DAT_073a6a38 < 0` (no current line → box shut).
 *   sx/sy = x/y scale (=1 at fully-open 15 since sin(3π)=0), alpha = fade 0..255.
 *
 * NB: FUN_00503a44 is sinf, NOT cosf (FUN_00503994 is cosf) — confirmed across
 * the corpus (scene1-char-sprite-render §, scene1-table-b-allocators §, etc.).
 * The original "cos" port produced sin(3π)=0→1.0 vs cos(3π)=-1→0.9875 at the
 * settled box, i.e. a ~5px-narrower box every line — the dialogue box-edge
 * "halo" (opening-prologue.md). box_open itself was already bit-1:1 vs retail
 * (0/46 over intro-dialogue-lines), so swapping cos→sin makes the box scale 1:1. */
void ive_box_scale(int n, float *sx, float *sy, int *alpha, int closing)
{
    float lim = (float)n * 0.2f + 0.4f;
    if (lim > 1.0f) lim = 1.0f;

    float amp;
    if (n < 6)        amp = 0.8f;
    else if (n < 0xb) amp = 0.3f;
    else if (n < 0x10) amp = 0.1f;
    else              amp = 0.0f;

    int a = n * 0x56;
    if (a > 0xff) a = 0xff;
    *alpha = a;

    float c = sinf((float)n * 9.424778f / 15.0f);
    *sx = c * amp * 0.125f + 1.0f;
    *sy = (1.0f - c * amp * 0.125f) * lim;

    if (closing) {                       /* box shutting (no current line) */
        *sx = 1.0f;
        float t = 1.0f - (float)(0xf - n) * 0.15f;
        *sy = (t < 0.0f) ? 0.0f : t;
        int ia = n * 0x32 - 0x1ef;
        *alpha = (ia < 0) ? 0 : ia;
    }
}

/* Bounds-checked standee accessor (the chr handlers index by the chr number). */
static struct ive_standee *ive_standee_at(struct ive_runtime *rt, int n)
{
    if (n < 0 || n >= IVE_STANDEE_COUNT) return NULL;
    return &rt->scene.standees[n];
}

/* FUN_0046c0ae — per-script reset of the standee table + scene-render scalars.
 * The loader (FUN_0046c295) runs this before parsing. Field indices/bit patterns
 * are verbatim from the init loop (docs/decompiled/by-address/46c0ae.c). */
void ive_scene_state_reset(struct ive_scene_state *s)
{
    for (int i = 0; i < IVE_STANDEE_COUNT; i++) {
        int32_t *f = s->standees[i].field;
        for (int k = 0; k < IVE_STANDEE_FIELDS; k++)
            f[k] = 0;
        f[3]  = 0x44480000;  /* 800.0f                              */
        f[5]  = 0x40000000;  /* 2.0f                                */
        f[6]  = 0x40000000;  /* 2.0f                                */
        f[15] = 0x437f0000;  /* current colour r = 255.0f           */
        f[16] = 0x437f0000;  /* g                                   */
        f[17] = 0x437f0000;  /* b                                   */
        f[18] = 0x437f0000;  /* a                                   */
        f[19] = 0x437f0000;  /* colour delta b (chr:colto, per-frame) */
        f[20] = 0x437f0000;  /* g — reset 255.0 verbatim per FUN_0046c0ae, */
        f[21] = 0x437f0000;  /* r   but dead until a colto recomputes them */
        f[22] = 0x437f0000;  /* a   (field10 countdown gates the apply)    */
    }
    s->bg_active       = 0;   /* DAT_073a3df0 */
    s->bg_fade         = 0;   /* DAT_073a3df4 */
    s->bg_scroll       = 0;   /* DAT_073a6d84 */
    s->bg_index        = 0;   /* DAT_073a6d90 */
    s->bg_mode         = 0;   /* DAT_073a6d94 */
    s->shake_bg        = 0;   /* DAT_073a6d98 */
    s->shake_chr       = 0;   /* DAT_073a6d9c */
    s->choice_fade     = 0;   /* DAT_073a6da4 */
    s->skip_prompt     = 0;   /* DAT_073a3e18 */
    s->blink           = 0;   /* DAT_073a3e0c */
    s->window_open_ctr = -1;  /* DAT_005c797c */
    s->choice_mode     = -1;  /* DAT_073a6bcc */
    /* PORT-DEBT(deferred, FUN_0046c9a2): box_pos_mode/off (DAT_005c7984/80) are
     * NOT reset by FUN_0046c0ae and their defaults + the windowpos handler body
     * are an unresolved RE gap — zeroed here, pinned in Layer 1. */
    s->box_pos_mode    = 0;
    s->box_pos_off     = 0;
}

void ive_runtime_init(struct ive_runtime *rt, const struct ive_program *prog)
{
    /* Mirror the engine's per-script reset (the FUN near all.c:67085):
     * DAT_073a6bd0/DAT_073a3e14 cleared, line state empty, gate up. */
    rt->prog      = prog;
    rt->active    = 1;
    rt->complete  = 0;
    rt->cmd       = 0;
    rt->reveal    = 0;
    rt->revealed  = 0;
    rt->dwell     = 0;
    rt->wait      = 0;
    rt->box_open  = 0;
    rt->new_line  = 0;
    rt->line_row  = -1;          /* no current line (box closed) */
    rt->line_rows = 0;
    rt->line_idx  = 0;
    rt->accum     = 0;
    rt->speaker   = 0;
    rt->portrait  = 0;
    rt->prev_held = 0;
    ive_scene_state_reset(&rt->scene);
}

/* msg-show (0x46d97b): set up the current line, reset the reveal counter, raise
 * the new-line flag. a1 = row_start (-1 ⇒ continue from accumulator), a2 = rows. */
static void op_msg_show(struct ive_runtime *rt, int32_t a1, int32_t a2)
{
    if (a1 == -1) {
        rt->line_row = rt->accum;
    } else {
        rt->line_row = a1;
        rt->accum    = a1;
    }
    rt->reveal    = 0;
    rt->dwell     = 0;
    rt->accum    += a2;
    rt->line_rows = a2;
    rt->new_line  = 1;
    rt->line_idx++;
}

/* msg-waitkey (0x46d93c): block until the line has dwelt >=15 frames AND the
 * player asks to advance (held fast-forward, or a fresh advance edge). */
static int op_msg_waitkey(const struct ive_runtime *rt, uint16_t held, uint16_t edge)
{
    if (rt->dwell < IVE_DWELL_GATE) return IVE_R_STOP;
    if (held & IVE_BTN_FF)          return IVE_R_YIELD;      /* held 0x20/0x40 */
    if (edge & IVE_BTN_ADVANCE)     return IVE_R_YIELD;      /* fresh A-press; SE 0x144 deferred */
    return IVE_R_STOP;
}

/* Execute one command; returns the walk return code. Visual/audio handlers are
 * deferred no-ops (ret 1 / advance) — only the state model that the anchors
 * observe is driven here. */
static int ive_exec(struct ive_runtime *rt, const struct ive_cmd *c,
                    uint16_t held, uint16_t edge)
{
    switch (c->op) {
    case IVE_OP_END:            /* NULL fn — idle (walk stops, no advance) */
        return IVE_R_STOP;
    case IVE_OP_END_SCRIPT:     /* end: (0x46dd76) → ret 3 */
        return IVE_R_COMPLETE;
    case IVE_OP_WAIT:           /* wait:n (0x46dcd6) → ret 2, sets the gate */
        rt->wait = c->a1;
        return IVE_R_YIELD;
    case IVE_OP_MSG_SHOW:       /* (0x46d97b) → ret 2 */
        op_msg_show(rt, c->a1, c->a2);
        return IVE_R_YIELD;
    case IVE_OP_MSG_WAITKEY:    /* (0x46d93c) → ret 0/2 */
        return op_msg_waitkey(rt, held, edge);
    case IVE_OP_MSG_CLEAR:      /* <C> (0x46d9e1) → DAT_073a6a38=-1, ret 1 */
        rt->line_row = -1;
        return IVE_R_CONTINUE;
    case IVE_OP_MSG_SPEAKER:    /* msg:a:b (0x46d9f3) → ret 1 */
        rt->speaker  = c->a1;
        rt->portrait = c->a2;
        return IVE_R_CONTINUE;
    case IVE_OP_BG:             /* bgset (0x46d912): active bg + clear scroll px */
        rt->scene.bg_index  = c->a1;   /* DAT_073a6d90 */
        rt->scene.bg_scroll = 0;       /* DAT_073a6d84 */
        return IVE_R_CONTINUE;
    case IVE_OP_BGSCROLL:       /* bgscroll:f (0x46d8a5): scroll mode/speed */
        rt->scene.bg_mode = c->a1;     /* DAT_073a6d94 */
        return IVE_R_CONTINUE;
    case IVE_OP_WINDOWPOS:      /* windowpos:x,mode (0x46d8e6) */
        rt->scene.box_pos_off  = c->a1;  /* DAT_005c7980 */
        rt->scene.box_pos_mode = c->a2;  /* DAT_005c7984 */
        return IVE_R_CONTINUE;
    case IVE_OP_WINDOWSET:      /* windowset (0x46d8c6): top-banner counter */
        rt->scene.window_open_ctr = c->a1;  /* DAT_005c797c */
        return IVE_R_CONTINUE;
    case IVE_OP_RMB:            /* rmb:a,b (0x46d926) — screen-shake countdowns.
                                 * Handler stores a1→bg (DAT_073a6d98), a2→chr
                                 * (DAT_073a6d9c); both are already atoi+1 from
                                 * parse. While >0 the per-frame tick decrements
                                 * them and the draw jitters bg/standee Y by
                                 * (rng_next15()&0x1f)-16 (engine 0x46d926 +
                                 * FUN_0046c9a2 L67507/L67606). */
        rt->scene.shake_bg  = c->a1;
        rt->scene.shake_chr = c->a2;
        return IVE_R_CONTINUE;

    /* ── character-standee setup (settled-state subset; tweens deferred) ──
     * Each writes standees[a1] (the chr index N). The animated paths
     * (move-tween, colto fade, fadeframe, speed) are PORT-DEBT: the goldens
     * are captured at the settled per-line anchor, so the final static pose
     * matches without porting the per-frame interpolation. For move/moveto we
     * SNAP the current field to the target so the settled pose is correct
     * without the update-loop tween. Handler ground truth: raw-disasm of the
     * 0x46da09–0x46dcac stubs (docs/findings/opening-prologue.md §handler bodies). */
    case IVE_OP_CHR_DISP: {     /* 0x46da09 — active flag (field 11) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[IVE_ST_ACTIVE] = c->a2;
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_DIR: {      /* 0x46da1e — mirror flag (field 12) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[IVE_ST_MIRROR] = c->a2;
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_ANIM: {     /* 0x46dc97 (grp/anim) — graphic index (field 14) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[IVE_ST_GRAPHIC] = c->a2;
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_MOVE_X: {   /* 0x46da33 — x: sets current(1) AND target(3) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[1] = s->field[3] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_MOVE_Y: {   /* 0x46dc0a — y: current(2) AND target(4) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[2] = s->field[4] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_MOVETO_X: { /* 0x46da6e — target(3) ONLY; current(1) tweens toward it */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[3] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_MOVETO_Y: { /* 0x46dc30 — target(4) ONLY */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[4] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_CENTER: {   /* 0x46da59 — centering offset (field 7) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[7] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_SPEED: {    /* 0x46dc45 — move step field5=field6=(a2&0xffff)/1000 */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) {
            float v = (float)(c->a2 & 0xffff) / 1000.0f;
            s->field[5] = s->field[6] = ive_f_word(v);
        }
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_FADEFRAME: { /* 0x46dc82 — field9 = colour-fade frame count */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[9] = c->a2;
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_COL: {      /* 0x46da83 — set CURRENT colour (15-18) only */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) {
            uint32_t p = (uint32_t)c->a2;            /* a<<24|r<<16|g<<8|b */
            s->field[15] = ive_f_word((float)( p        & 0xff));  /* B */
            s->field[16] = ive_f_word((float)((p >> 8)  & 0xff));  /* G */
            s->field[17] = ive_f_word((float)((p >> 16) & 0xff));  /* R */
            s->field[18] = ive_f_word((float)((p >> 24) & 0xff));  /* A */
        }
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_COLTO: {    /* 0x46db20 — per-frame deltas (19-22) toward target */
        /* colto sets field10 = fadeframe count (field9) and the per-frame delta
         * field19-22 = (target_ch - current_ch)/field9 (channels B,G,R,A); the
         * tween loop adds the delta to current(15-18) each step for field10
         * frames. The kuro black-overlay fade (col 255a → colto 0a over
         * fadeframe:240) and Tear's "sigh"/zzz effect fades all ride this. */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) {
            uint32_t p = (uint32_t)c->a2;
            float tb = (float)( p        & 0xff);    /* target B */
            float tg = (float)((p >> 8)  & 0xff);    /* target G */
            float tr = (float)((p >> 16) & 0xff);    /* target R */
            float ta = (float)((p >> 24) & 0xff);    /* target A */
            int frames = s->field[9];                /* field9 fadeframe */
            s->field[10] = frames;                   /* countdown = field9 */
            if (frames > 0) {
                float f = (float)frames;
                s->field[19] = ive_f_word((tb - ive_word_f(s->field[15])) / f);
                s->field[20] = ive_f_word((tg - ive_word_f(s->field[16])) / f);
                s->field[21] = ive_f_word((tr - ive_word_f(s->field[17])) / f);
                s->field[22] = ive_f_word((ta - ive_word_f(s->field[18])) / f);
            }
        }
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_BLEND: {    /* 0x46dcac — blend mode (field 27) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[IVE_ST_BLEND] = c->a2;
        return IVE_R_CONTINUE;
    }
    case IVE_OP_SE:             /* se:<bin> (0x46d885) → FUN_0049933c voice/SE */
        /* a1 indexes the parsed se name table. The engine fires the clip the
         * instant the walk reaches the command (ret 1, same frame), so the
         * voice lands as the following msg line shows. The backend is
         * single-slot, so a new line's voice stops the previous one. NULL
         * bridge (test build / pre-audio_init) = silent no-op.
         *
         * MUTE WHILE FAST-FORWARDING (asm 0x46d885: `cmpl $0x1,DAT_005c78ec; jne
         * skip`): the voice plays ONLY when the internal step count == 1, i.e. at
         * normal speed.  Holding X (0x20 → 2 steps) or button-3 (0x40 → 0x50
         * steps) to skip mutes the line so the skip doesn't garble overlapping
         * voices.  steps==1 ⟺ no FF button held ⟺ (held & IVE_BTN_FF)==0 (the port
         * models DAT_005c78ec as `steps`, prologue/tutorial local_104≡1). */
        if ((held & IVE_BTN_FF) == 0 &&
            g_ive_se_play_fn && rt->prog &&
            c->a1 >= 0 && c->a1 < IVE_MAX_NAMES) {
            g_ive_se_play_fn(rt->prog->se[c->a1]);
        }
        return IVE_R_CONTINUE;
    default:                    /* remaining setup ops (fade/light/music/...): ret 1 */
        return IVE_R_CONTINUE;
    }
}

/* The per-frame standee tween (FUN_0046c320 lines 107-134). For each active
 * standee: move its current position (field1/2) toward the target (field3/4) by
 * `speed` (field5/6) with a ±(speed-1) deadband, then — while field10 > 0 —
 * advance its current colour (field15-18) by the per-frame delta (field19-22)
 * and decrement field10. Runs once per internal step (DAT_005c78ec), gated in
 * the engine on choice_mode==-1 (always true in the prologue). This is what
 * slides Tear in from the left and fades the kuro black overlay / effect
 * sprites; previously snapped (PORT-DEBT, now retired). */
static void ive_run_tween(struct ive_runtime *rt)
{
    for (int i = 0; i < IVE_STANDEE_COUNT; i++) {
        int32_t *f = rt->scene.standees[i].field;
        if (f[IVE_ST_ACTIVE] == 0)            /* field11 — not displayed */
            continue;

        float spx = ive_word_f(f[5]), spy = ive_word_f(f[6]);
        float cx = ive_word_f(f[1]), tx = ive_word_f(f[3]);
        float cy = ive_word_f(f[2]), ty = ive_word_f(f[4]);
        if ((spx - 1.0f) + tx < cx) cx -= spx;
        if (cx < tx - (spx - 1.0f)) cx += spx;
        if ((spy - 1.0f) + ty < cy) cy -= spy;
        if (cy < ty - (spy - 1.0f)) cy += spy;
        f[1] = ive_f_word(cx);
        f[2] = ive_f_word(cy);

        if (f[10] > 0) {                      /* field10 colour-fade countdown */
            f[10]--;
            f[15] = ive_f_word(ive_word_f(f[15]) + ive_word_f(f[19]));
            f[16] = ive_f_word(ive_word_f(f[16]) + ive_word_f(f[20]));
            f[17] = ive_f_word(ive_word_f(f[17]) + ive_word_f(f[21]));
            f[18] = ive_f_word(ive_word_f(f[18]) + ive_word_f(f[22]));
        }
    }
}

/* FUN_00405a52's char-count return: the row's full logical character count,
 * minus the final char (the engine's iVar3 — it stops incrementing before the
 * last). SJIS-aware: any byte < 0x20 / >= 0x80 (signed < 1) starts a 2-byte
 * pair. Font-independent, so the reveal-completion latch lives here in the
 * (host-tested) runtime rather than the Win32 draw, matching the engine's
 * char-based budget. */
static int ive_row_count(const char *row)
{
    if (row == NULL || row[0] == '\0')
        return 0;
    int i1 = 0, i3 = 0;
    for (;;) {
        i1 += ((signed char)row[i1] < 1) ? 2 : 1;   /* advance past 1 glyph */
        if (row[i1] == '\0')
            break;
        i3 += 1;
    }
    return i3;
}

/* The FUN_0046c9a2 completion tail: latch the START reset (DAT_073a3e00 → 1)
 * + the END flag (DAT_073a3e04). Char-based reveal budget — see the comment on
 * IVE_REVEAL_SPEED. The line is fully revealed iff the running budget clears
 * every row's char count with >2 to spare. */
static void ive_completion(struct ive_runtime *rt)
{
    if (rt->line_row < 0)            /* no current line — box clearing */
        return;

    if (rt->new_line) {
        rt->reveal   = 1;            /* START edge (DAT_073a3e00 → 1) */
        rt->new_line = 0;
    }

    /* budget = (reveal-4)*speed/32 logical chars (the engine's local_10). */
    float budget = (float)(rt->reveal - 4) * (float)IVE_REVEAL_SPEED / 32.0f;
    if (budget <= 0.0f) {
        rt->revealed = 0;
        return;
    }
    rt->revealed = 1;
    for (int r = 0; r < rt->line_rows; r++) {
        int gi = rt->line_row + r;
        if (gi < 0 || gi >= IVE_MAX_ROWS)
            break;
        budget -= (float)ive_row_count(rt->prog->glyph[gi]);
        if (budget <= 2.0f) rt->revealed = 0;   /* row not fully cleared → still revealing */
        if (budget <= 0.0f) break;
    }
}

void ive_runtime_step(struct ive_runtime *rt, uint16_t held)
{
    uint16_t edge = (uint16_t)(held & ~rt->prev_held);
    rt->prev_held = held;

    if (!rt->active || rt->complete)
        return;

    /* DAT_073a3e18++ — the free-running per-tick counter FUN_0046c320 bumps at
     * the top of every frame (before the reveal/wait logic). Gates the
     * "ESC Key Event Skip" prompt, drawn once it exceeds 1 — see
     * scene1_dialogue_draw's draw_skip_tip. */
    rt->scene.skip_prompt++;

    /* Internal step count (DAT_005c78ec): held 0x20 → 2, held 0x40 → 0x50
     * (the prologue scene permits fast-forward, local_104 ≡ 1), else 1. */
    int steps = 1;
    if (held & IVE_BTN_FF_X2)        steps = 2;
    else if (held & IVE_BTN_FF_TURBO) steps = 0x50;

    /* Reveal + dwell + standee tween loop (the inner do-while, run `steps`
     * times). The engine runs the reveal counter, the dwell, the shake
     * countdowns and the standee position/colour tween in the same uVar6
     * iteration (FUN_0046c320 lines 73-160). */
    for (int s = 0; s < steps; s++) {
        if (rt->reveal > 0) {
            rt->reveal++;
            if (edge & IVE_BTN_ADVANCE)  rt->reveal = IVE_REVEAL_MAX;  /* edge 0x10 slam */
            if (held & IVE_BTN_FF_TURBO) rt->reveal = IVE_REVEAL_MAX;  /* held FF slam   */
            if (rt->reveal > IVE_REVEAL_MAX) rt->reveal = IVE_REVEAL_MAX;
        }
        if (rt->revealed)
            rt->dwell++;
        /* screen-shake countdowns (engine FUN_0046c320 L67241-67245): tick down
         * once per internal step while active. The draw consumes the jitter. */
        if (rt->scene.shake_bg > 0)  rt->scene.shake_bg--;
        if (rt->scene.shake_chr > 0) rt->scene.shake_chr--;
        ive_run_tween(rt);
    }

    /* Box open/close + the `wait:` countdown — the wait only ticks down while
     * the box is fully open (line shown) or fully closed (no line). */
    for (int s = 0; s < steps; s++) {
        if (rt->line_row < 0) {
            if (rt->box_open < 1) {
                if (rt->wait > 0) rt->wait--;
            } else {
                rt->box_open--;
            }
        } else if (rt->box_open < IVE_BOX_OPEN_MAX) {
            rt->box_open++;
        } else {
            if (rt->wait > 0) rt->wait--;
        }
    }

    if (rt->wait > 0) {              /* wait gate — hold the walk */
        ive_completion(rt);
        return;
    }

    /* Command walk: run setup ops same-frame, stop on yield/block/complete. */
    for (;;) {
        if (rt->cmd < 0 || rt->cmd >= rt->prog->n_cmds)
            break;                  /* defensive — table is END-terminated */
        const struct ive_cmd *c = &rt->prog->cmds[rt->cmd];
        int r = ive_exec(rt, c, held, edge);
        if (r == IVE_R_STOP)
            break;                  /* block (no advance) */
        if (r == IVE_R_COMPLETE) {
            rt->complete = 1;
            rt->active   = 0;       /* dialogue gate drops */
            break;
        }
        rt->cmd++;                  /* CONTINUE and YIELD both advance past the cmd */
        if (r == IVE_R_YIELD)
            break;
    }

    ive_completion(rt);
}
