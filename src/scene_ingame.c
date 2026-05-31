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
#include "stage_palette.h"

#ifdef _WIN32

unsigned int scene_ingame_clear_argb(void)
{
    /* Engine clear (FUN_004547ab L33-44): pack the stage palette's clear_r/g/b
     * low bytes into ARGB (alpha 0xff) and Clear() to it.  HOUSE's palette is
     * zero → black, matching retail (the old placeholder navy 0xff203050 showed
     * up as a full-frame delta in every port|retail diff).  Black fallback when
     * the palette pointer isn't set yet. */
    if (g_stage_palette) {
        unsigned r = (unsigned)g_stage_palette->clear_r & 0xffu;
        unsigned g = (unsigned)g_stage_palette->clear_g & 0xffu;
        unsigned b = (unsigned)g_stage_palette->clear_b & 0xffu;
        return 0xff000000u | (r << 16) | (g << 8) | b;
    }
    return 0xff000000u;
}

#endif /* _WIN32 */
