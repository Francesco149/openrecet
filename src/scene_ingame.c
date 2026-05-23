/*
 * scene_ingame.c — see scene_ingame.h.
 *
 * Holds the placeholder clear color for the INGAME state. The actual
 * render chain (scene1_render_camera_setup → _overlay → _fx_tail) lives
 * in scene1_render.c and is invoked directly from main.c::render_dispatch
 * since Cr.1 (2026-05-23); the older scene_ingame_render placeholder is
 * gone.
 */

#include "scene_ingame.h"

#ifdef _WIN32

unsigned int scene_ingame_clear_argb(void)
{
    /* Dark navy — distinct from the title's 0xff17f0ff debug pink so
     * the cross-fade endpoint is visually unambiguous. Replace with
     * the engine's real per-stage palette clear (DAT_068dd2f0 + 0x1aa8)
     * when the stage system lands. */
    return 0xff203050u;
}

#endif /* _WIN32 */
