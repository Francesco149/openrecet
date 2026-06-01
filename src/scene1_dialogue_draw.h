/*
 * scene1_dialogue_draw.h — opening-prologue dialogue RENDER pass.
 *
 * Port of the DRAW body of FUN_0046c9a2 (docs/decompiled/by-address/46c9a2.c):
 * the painted 2D background, character standees, the dialogue window box, the
 * speaker nameplate/portrait, and the typewriter-revealed glyph text. Reads the
 * live interpreter state borrowed via scene1_intro_dialogue_runtime() and draws
 * over the INGAME scene (the prologue's bg covers the 3D HOUSE). The reveal-
 * completion latch itself lives in scene1_dialogue_run.c (ive_completion); this
 * is the pixel side.
 *
 * Engine call path: FUN_004547ab → FUN_0046c090 → FUN_0046c9a2. The port hooks
 * scene1_dialogue_draw() into main.c's INGAME render dispatch, gated on
 * scene1_intro_dialogue_active().
 *
 * Built incrementally (see docs/plans + PROGRESS): Layer 0 = inert scaffold;
 * Layer 1 = bg + box + nameplate; Layer 2 = glyph text; Layer 3 = standees;
 * Layer 4 = fades / skip-prompt / dust-RNG-stream.
 */
#ifndef OPENRECET_SCENE1_DIALOGUE_DRAW_H
#define OPENRECET_SCENE1_DIALOGUE_DRAW_H

#ifdef _WIN32
struct IDirect3DDevice8;

/* Draw the active prologue dialogue (bg / standees / box / nameplate / text).
 * No-op when no script is active. Caller is inside BeginScene; this sets up its
 * own 2D render state (render_quad_state_setup) and flushes its own batches. */
void scene1_dialogue_draw(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_DIALOGUE_DRAW_H */
