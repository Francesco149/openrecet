/*
 * scene_ingame.c — placeholder INGAME scene. See scene_ingame.h.
 */

#include "scene_ingame.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "font_draw.h"
#include "render_quad.h"

unsigned int scene_ingame_clear_argb(void)
{
    /* Dark navy — distinct from the title's 0xff17f0ff debug pink so
     * the cross-fade endpoint is visually unambiguous. Replace with
     * the engine's real per-stage palette clear (DAT_068dd2f0 + 0x1aa8)
     * when the stage system lands. */
    return 0xff203050u;
}

void scene_ingame_render(struct IDirect3DDevice8 *dev)
{
    if (!dev) return;

    render_quad_state_setup(dev);

    font_draw_text(dev, 320.0f - 120.0f, 220.0f,
                   "openrecet: ingame placeholder",
                   0xffffffffu, 1.0f);
    font_draw_text(dev, 320.0f - 140.0f, 260.0f,
                   "scene_state == 1 (port WIP)",
                   0xffc0c0c0u, 1.0f);

    render_quad_flush(dev);
}

#endif /* _WIN32 */
