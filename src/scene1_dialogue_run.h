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
