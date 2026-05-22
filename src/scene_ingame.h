/*
 * scene_ingame.h — placeholder for the post-title in-game scene
 * (engine scene_state == 1).
 *
 * The engine's scene-1 sim + render fan into a large family of
 * functions (FUN_0045bbf9 / FUN_0040a765 / FUN_00417504 / FUN_0045404b
 * / FUN_0040c962 / FUN_004358cc / FUN_00453d9c plus the sub-state
 * machine in FUN_004547ab L60-160) covering the player house / shop /
 * town / dungeons / dialogue overlays. None of that is ported yet.
 *
 * This module exists so the scene-state machine has somewhere to flip
 * to after the title fade-out. It draws a solid clear color + a
 * single debug label so the user can confirm visually that the
 * transition fired. As the real scene-1 pieces land they replace this
 * placeholder one renderer at a time.
 */

#ifndef OPENRECET_SCENE_INGAME_H
#define OPENRECET_SCENE_INGAME_H

#ifdef _WIN32
struct IDirect3DDevice8;

/* Render the placeholder ingame scene. Clear color + debug label.
 * Called from main.c::render_dispatch's SCENE_STATE_INGAME branch
 * AFTER the device clear; this function adds the label on top. */
void scene_ingame_render(struct IDirect3DDevice8 *dev);

/* Returns the placeholder clear color (ARGB) that main.c should pass
 * to IDirect3DDevice8_Clear when in SCENE_STATE_INGAME. Distinct from
 * the title-state clear (0xff17f0ff pink) so the transition is
 * visually unambiguous. */
unsigned int scene_ingame_clear_argb(void);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_INGAME_H */
