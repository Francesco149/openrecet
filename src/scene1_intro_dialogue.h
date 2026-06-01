/*
 * scene1_intro_dialogue.h — engine-side driver for the opening-prologue
 * dialogue. Owns the two-script opening sequence (iv1_1.ivt → iv1_2.ivt,
 * selector 1,1 → 1,2 — see docs/findings/opening-prologue.md "Opening script
 * PINNED"): lazily loads each script via the storage layer, ticks the
 * scene1_dialogue_run interpreter, and exposes the TEXT_ANIM_START/END anchor
 * state (DAT_0438b1c8 / DAT_073a3e00 / DAT_073a3e04) for anchor_trace.
 *
 * Scope (additive, structural): this drives ONLY the dialogue interpreter +
 * its anchor signals. It deliberately does NOT touch g_scene_state, the
 * worker-load gate, or the HOUSE_FREEROAM edges — the scene1_intro_events stub
 * still produces those, so existing house-* traces are unaffected. The
 * inter-script load screen retail shows between iv1_1 and iv1_2 is also not
 * reproduced here (the per-line anchor rebase absorbs it); the box/text draws
 * are deferred to the visual pass. Retiring scene1_intro_events fully (so the
 * dialogue drives the real gate-drop + transition) is a later chip.
 */
#ifndef OPENRECET_SCENE1_INTRO_DIALOGUE_H
#define OPENRECET_SCENE1_INTRO_DIALOGUE_H

#include <stdint.h>

/* Arm the opening-dialogue sequence (call at NEW GAME, alongside
 * scene1_intro_events_arm). The first script loads lazily on the first tick. */
void scene1_intro_dialogue_arm(void);

/* Return to dormant (no script running, anchors report inactive). */
void scene1_intro_dialogue_reset(void);

/* Advance one frame. Call once per INGAME, non-loading frame with player 1's
 * held button mask (g_input_state[0].buttons). No-op while dormant/done. */
void scene1_intro_dialogue_tick(uint16_t held);

/* Anchor sources (feed anchor_world). Report zero/inactive while dormant. */
int     scene1_intro_dialogue_active(void);        /* DAT_0438b1c8 == 1     */
int32_t scene1_intro_dialogue_text_reveal(void);   /* DAT_073a3e00          */
int     scene1_intro_dialogue_text_revealed(void); /* DAT_073a3e04 != 0     */

#endif /* OPENRECET_SCENE1_INTRO_DIALOGUE_H */
