/*
 * scene_ingame.h — clear-color helper for the post-title in-game scene
 * (engine scene_state == 1).
 *
 * The real scene-1 render chain lives in scene1_render.{c,h}
 * (scene1_render_camera_setup → scene1_render_overlay → scene1_render_fx_tail)
 * and is invoked from main.c::render_dispatch directly since Cr.1.
 *
 * This module survives only because main.c still needs a placeholder
 * clear color for the INGAME branch until the per-stage palette
 * (DAT_068dd2f0 + 0x1aa8) feeds Clear directly.
 */

#ifndef OPENRECET_SCENE_INGAME_H
#define OPENRECET_SCENE_INGAME_H

#ifdef _WIN32

/* Returns the placeholder clear color (ARGB) that main.c should pass
 * to IDirect3DDevice8_Clear when in SCENE_STATE_INGAME. Distinct from
 * the title-state clear (0xff17f0ff pink) so the transition is
 * visually unambiguous. */
unsigned int scene_ingame_clear_argb(void);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE_INGAME_H */
