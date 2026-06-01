/*
 * scene1_dialogue_run.h — opening-prologue dialogue RUNTIME (the per-frame
 * sequencer). Ports FUN_0046c320 (update: reveal counter + advance button +
 * command walk) and the reveal-completion tail of FUN_0046c9a2 (the
 * DAT_073a3e04 END latch / DAT_073a3e00 START reset). The D3D draw calls,
 * glyph rasterization and exact per-glyph advance widths in FUN_0046c9a2 are
 * DEFERRED to the later visual pass (see docs/findings/opening-prologue.md
 * "defer boundary" + §handler bodies) — this drives the data model + the
 * TEXT_ANIM_START/END anchor signals only.
 *
 * Pure C, host-testable: ive_runtime_step() takes a pre-loaded program and a
 * 14-bit held-button mask and advances one engine frame. The engine-side glue
 * (load iv1_1/iv1_2 via the storage layer, feed the anchor_world fields, drive
 * the new-game→prologue→free-roam sequence) lives in the caller.
 *
 * Handler ground truth (raw-disasm of the 0x46d8xx–0x46ddxx stubs):
 *   - setup ops (chr / bg / se / color / fade / light / music / rmb / window /
 *     skip / speaker /
 *     clear) → ret 1 (advance + run next command the same frame). Their visual
 *     /audio effects are deferred no-ops here.
 *   - wait / msg-show → ret 2 (yield a frame).
 *   - msg-waitkey → ret 0 (block) until dwell DAT_073a3e08 >= 15 AND advance
 *     (held 0x20/0x40 OR fresh edge 0x10), then ret 2.
 *   - end: → ret 3 → script complete.
 */
#ifndef OPENRECET_SCENE1_DIALOGUE_RUN_H
#define OPENRECET_SCENE1_DIALOGUE_RUN_H

#include "scene1_dialogue.h"

#include <stdint.h>
#include <string.h>

/* The standee position/colour fields hold float bit patterns in int32 storage
 * (the engine touches them as both). Bit-cast helpers shared by the handlers
 * (scene1_dialogue_run.c) and the draw (scene1_dialogue_draw.c). */
static inline float   ive_word_f(int32_t w) { float f; memcpy(&f, &w, 4); return f; }
static inline int32_t ive_f_word(float f)   { int32_t w; memcpy(&w, &f, 4); return w; }

/* ─── scene-render state (FUN_0046c0ae reset / FUN_0046c9a2 draw) ─────────
 *
 * The standee character table + the scene-render scalars the dialogue DRAW
 * (FUN_0046c9a2) consumes. Per-line scalars (reveal / box_open / line_row …)
 * live on `struct ive_runtime` below; this holds the ones that don't. Reset by
 * ive_scene_state_reset (the FUN_0046c0ae init the loader FUN_0046c295 runs).
 */
#define IVE_STANDEE_COUNT  200   /* engine standee table size (FUN_0046c0ae)   */
#define IVE_STANDEE_FIELDS 28    /* 0x1c-int per-entry stride                  */

/* One character-standee record — the raw 28-int engine layout (struct base =
 * engine &DAT_073a3e70). The draw walks the table from &DAT_073a3ea8 =
 * &field[14]; the semantic offsets below come from the FUN_0046c0ae init
 * defaults + the FUN_0046c9a2 draw loop. Stored as raw ints because the engine
 * touches the colour/position words as both int and float (bit-cast at use). */
struct ive_standee { int32_t field[IVE_STANDEE_FIELDS]; };

/* Semantic field offsets within ive_standee.field[]. */
enum {
    IVE_ST_X       = 1,   /* x position          (draw piVar5[-0xd])             */
    IVE_ST_W       = 3,   /* init 800.0f                                         */
    IVE_ST_ACTIVE  = 11,  /* active/displayed    (draw piVar5[-3], chr:disp)     */
    IVE_ST_MIRROR  = 12,  /* mirror flag (==1)   (draw piVar5[-2], chr:dir)      */
    IVE_ST_GRAPHIC = 14,  /* chrname graphic idx (draw *piVar5,    chr:grp/anim) */
    IVE_ST_COL_R   = 15,  /* current colour rgba float (15..18)                  */
    IVE_ST_COL_TR  = 19,  /* target  colour rgba float (19..22, chr:colto fade)  */
    IVE_ST_BLEND   = 27   /* blend mode          (draw piVar5[0xd], chr:blend)   */
};

struct ive_scene_state {
    struct ive_standee standees[IVE_STANDEE_COUNT];

    int32_t bg_active;        /* DAT_073a3df0 — bg name count; >0 = draw bg      */
    int32_t bg_fade;          /* DAT_073a3df4 — bg fade-overlay alpha driver     */
    int32_t bg_scroll;        /* DAT_073a6d84 — horizontal scroll (÷1000)        */
    int32_t bg_index;         /* DAT_073a6d90 — active bg slot                   */
    int32_t bg_mode;          /* DAT_073a6d94 — 0 = static, else scroll/shake    */
    int32_t shake_bg;         /* DAT_073a6d98 — rmb: bg-shake countdown          */
    int32_t shake_chr;        /* DAT_073a6d9c — rmb: chr-shake countdown         */
    int32_t choice_fade;      /* DAT_073a6da4 — choice/menu fade state           */
    int32_t skip_prompt;      /* DAT_073a3e18 — ESC skip-event prompt counter    */
    int32_t blink;            /* DAT_073a3e0c — next-line arrow blink phase      */
    int32_t window_open_ctr;  /* DAT_005c797c — init -1                          */
    int32_t choice_mode;      /* DAT_073a6bcc — init -1                          */
    int32_t box_pos_mode;     /* DAT_005c7984 — windowpos mode (RE gap: see .c)  */
    int32_t box_pos_off;      /* DAT_005c7980 — windowpos offset (RE gap)        */
};

/* Reproduce FUN_0046c0ae's standee-table + scalar reset. Pure C; called from
 * ive_runtime_init (the engine runs it in the per-script loader FUN_0046c295). */
void ive_scene_state_reset(struct ive_scene_state *s);

/* FUN_0046c86f — dialogue box open/close scale + alpha. `n` = box_open (0..15),
 * `closing` = no current line. Outputs x/y scale + alpha (0..255). */
void ive_box_scale(int n, float *sx, float *sy, int *alpha, int closing);

/* Live interpreter state. One per running script. Field comments give the
 * engine global each mirrors. Zero-initialise then ive_runtime_init(). */
struct ive_runtime {
    const struct ive_program *prog;  /* the compiled script (borrowed)        */

    int      active;        /* DAT_0438b1c8 == 1 — dialogue running           */
    int      complete;      /* end: (ret 3) reached — script done             */
    int      cmd;           /* DAT_073a6bd4 — command-walk index              */

    int32_t  reveal;        /* DAT_073a3e00 — per-line reveal counter (0..0x800) */
    int      revealed;      /* DAT_073a3e04 — current line fully revealed (END) */
    int32_t  dwell;         /* DAT_073a3e08 — frames since END (the WAITKEY gate) */
    int32_t  wait;          /* DAT_073a6d7c — `wait:` frame countdown         */
    int      box_open;      /* DAT_073a3e14 — box open/close anim (0..15)      */
    int      new_line;      /* DAT_073a6d74 — a new line was just shown (→START) */

    int32_t  line_row;      /* DAT_073a6a38 — current line's first text row (-1 = none) */
    int32_t  line_rows;     /* DAT_073a6bd0 — current line's row count         */
    int32_t  line_idx;      /* DAT_073a6a30 — running line index               */
    int32_t  accum;         /* DAT_005c7a28 — running row-offset accumulator   */

    int      speaker;       /* DAT_073a6da0 — active speaker index             */
    int      portrait;      /* DAT_073a3e10 — active portrait/face index       */

    uint16_t prev_held;     /* previous frame's held mask (for edge = cur&~prev) */

    struct ive_scene_state scene;  /* render state (bg / standees / box pos)   */
};

/* Arm `rt` to run `prog` from its first command. `prog` is borrowed (must
 * outlive the runtime). Sets active=1; the dialogue gate is now up. */
void ive_runtime_init(struct ive_runtime *rt, const struct ive_program *prog);

/* Advance one engine frame. `held` is the 14-bit button mask
 * (g_input_state[0].buttons); the advance edge is derived internally. No-op
 * once complete or inactive. Mirrors one FUN_0046c320 + FUN_0046c9a2-tail
 * pass. */
void ive_runtime_step(struct ive_runtime *rt, uint16_t held);

#endif /* OPENRECET_SCENE1_DIALOGUE_RUN_H */
