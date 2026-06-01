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
 * Reveal-completion budget (the FUN_0046c9a2 tail). budget_px =
 * (reveal-4)*speed/32; each of the line's rows subtracts its rendered width;
 * the line is fully revealed (END) iff the budget still clears every row by
 * >2px. `speed` is the text-speed table DAT_005c78e0[idx] = {16,32,1024}
 * (slow/normal/fast) — normal is the fresh-config default.
 *
 * PORT-DEBT(deferred, FUN_0046c9a2): the real per-row width comes from the
 * glyph rasterizer (FUN_00405a52), deferred to the visual pass. We use a
 * nominal row width; this only affects the *typewriter speed* of a naturally-
 * revealing line. Under the validated advance-spam trace the reveal is slammed
 * to IVE_REVEAL_MAX, so the END edge is exact regardless of the metric.
 */
#define IVE_REVEAL_SPEED  32     /* DAT_005c78dc, normal text speed           */
#define IVE_ROW_PX        320    /* nominal rendered row width (deferred)     */

/* Walk-loop handler return codes (the 0x46c320 contract). */
enum { IVE_R_STOP = 0, IVE_R_CONTINUE = 1, IVE_R_YIELD = 2, IVE_R_COMPLETE = 3 };

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
        f[19] = 0x437f0000;  /* target colour r (chr:colto)         */
        f[20] = 0x437f0000;  /* g                                   */
        f[21] = 0x437f0000;  /* b                                   */
        f[22] = 0x437f0000;  /* a                                   */
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
    case IVE_OP_CHR_MOVETO_X: { /* 0x46da6e — target(3); snap current(1) [settled] */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[3] = s->field[1] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_MOVETO_Y: { /* 0x46dc30 — target(4); snap current(2) [settled] */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[4] = s->field[2] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_CENTER: {   /* 0x46da59 — centering offset (field 7) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[7] = ive_f_word((float)c->a2);
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_COL:        /* 0x46da83 — set current (15-18) + target (19-22) */
    case IVE_OP_CHR_COLTO: {    /* 0x46db20 — colour fade target; snap [settled] */
        /* col sets current+target; colto sets only the target and the update
         * loop tweens current toward it. At the settled anchor the tween has
         * finished (current==target), so for both we snap current=target. This
         * matters for full-screen fade overlays (a fade-to-transparent must
         * settle invisible, not at the reset opaque-white). */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) {
            uint32_t p = (uint32_t)c->a2;            /* a<<24|r<<16|g<<8|b */
            int32_t ch0 = ive_f_word((float)( p        & 0xff));  /* b */
            int32_t ch1 = ive_f_word((float)((p >> 8)  & 0xff));  /* g */
            int32_t ch2 = ive_f_word((float)((p >> 16) & 0xff));  /* r */
            int32_t ch3 = ive_f_word((float)((p >> 24) & 0xff));  /* a */
            s->field[15] = s->field[19] = ch0;
            s->field[16] = s->field[20] = ch1;
            s->field[17] = s->field[21] = ch2;
            s->field[18] = s->field[22] = ch3;
        }
        return IVE_R_CONTINUE;
    }
    case IVE_OP_CHR_BLEND: {    /* 0x46dcac — blend mode (field 27) */
        struct ive_standee *s = ive_standee_at(rt, c->a1);
        if (s) s->field[IVE_ST_BLEND] = c->a2;
        return IVE_R_CONTINUE;
    }
    /* DEFERRED (PORT-DEBT): CHR_SPEED / CHR_FADEFRAME + the move/col tween
     * intermediate frames — animated, irrelevant at the settled capture anchor. */
    default:                    /* every other setup op (se/fade/light/music/...): ret 1 */
        return IVE_R_CONTINUE;
    }
}

/* The FUN_0046c9a2 completion tail: latch the START reset + the END flag.
 * Draws/glyph raster deferred. */
static void ive_completion(struct ive_runtime *rt)
{
    if (rt->line_row < 0)            /* no current line — box clearing */
        return;

    if (rt->new_line) {
        rt->reveal   = 1;            /* START edge (DAT_073a3e00 → 1) */
        rt->new_line = 0;
    }

    /* budget = (reveal-4)*speed/32; revealed iff it clears every row by >2px. */
    long budget = (long)(rt->reveal - 4) * IVE_REVEAL_SPEED / 32;
    long total  = (long)rt->line_rows * IVE_ROW_PX;
    rt->revealed = (budget > total + 2) ? 1 : 0;
}

void ive_runtime_step(struct ive_runtime *rt, uint16_t held)
{
    uint16_t edge = (uint16_t)(held & ~rt->prev_held);
    rt->prev_held = held;

    if (!rt->active || rt->complete)
        return;

    /* Internal step count (DAT_005c78ec): held 0x20 → 2, held 0x40 → 0x50
     * (the prologue scene permits fast-forward, local_104 ≡ 1), else 1. */
    int steps = 1;
    if (held & IVE_BTN_FF_X2)        steps = 2;
    else if (held & IVE_BTN_FF_TURBO) steps = 0x50;

    /* Reveal + dwell loop (the inner do-while, run `steps` times). */
    for (int s = 0; s < steps; s++) {
        if (rt->reveal > 0) {
            rt->reveal++;
            if (edge & IVE_BTN_ADVANCE)  rt->reveal = IVE_REVEAL_MAX;  /* edge 0x10 slam */
            if (held & IVE_BTN_FF_TURBO) rt->reveal = IVE_REVEAL_MAX;  /* held FF slam   */
            if (rt->reveal > IVE_REVEAL_MAX) rt->reveal = IVE_REVEAL_MAX;
        }
        if (rt->revealed)
            rt->dwell++;
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
