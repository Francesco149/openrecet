/*
 * scene1_intro_dialogue.h — engine-side driver for the opening-prologue
 * dialogue. Owns the two-script opening sequence (iv1_1.ivt → iv1_2.ivt,
 * selector 1,1 → 1,2 — see docs/findings/opening-prologue.md "Opening script
 * PINNED"): lazily loads each script via the storage layer, ticks the
 * scene1_dialogue_run interpreter, and exposes the TEXT_ANIM_START/END anchor
 * state (DAT_0438b1c8 / DAT_073a3e00 / DAT_073a3e04) for anchor_trace.
 *
 * Scope (structural): this drives the dialogue interpreter, its anchor signals,
 * AND the inter-script loading bracket (iv1_1 → iv1_2). Between the two scripts
 * it raises a loading flag (scene1_intro_dialogue_loading) for the retail-
 * measured 68-frame window — which main.c folds into anchor_world.loading_active
 * so the 2nd LOADING_START/END + HOUSE_FREEROAM fire at the faithful position
 * (after iv1_1's last line). This RETIRES the scene1_intro_events stub, which
 * faked that pair ~10 frames after the 1st HOUSE_FREEROAM (wrong place). It still
 * does NOT touch g_scene_state or the primary worker-load gate (the new-game
 * HOUSE scene load = LOADING/HOUSE_FREEROAM #1 stays with worker_load); iv1_1
 * runs under that load and has no bracket of its own, matching retail. The
 * box/text draws + the shatter/melt transition visuals are deferred to the
 * visual pass. See opening-prologue.md "the script-load / gate / transition
 * subsystem" for the full retail timeline + the synthetic-68 PORT-DEBT note.
 */
#ifndef OPENRECET_SCENE1_INTRO_DIALOGUE_H
#define OPENRECET_SCENE1_INTRO_DIALOGUE_H

#include <stdint.h>

/* Arm the opening-dialogue sequence (call at NEW GAME). The first script loads
 * lazily on the first tick. */
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

/* Nonzero during the iv1_1→iv1_2 loading bracket (gate==2 in the engine).
 * OR this into anchor_world.loading_active so the 2nd LOADING/HOUSE_FREEROAM
 * pair fires here. Zero outside the bracket. */
int     scene1_intro_dialogue_loading(void);

#endif /* OPENRECET_SCENE1_INTRO_DIALOGUE_H */
