/*
 * scene1_dialogue_draw.c — opening-prologue dialogue RENDER pass. See the
 * header. Port of the DRAW body of FUN_0046c9a2.
 *
 * Win32-only: the body is D3D8 draw calls (via render_quad + font_draw). The
 * pure-C helpers it depends on (the mirror quad, the box open/close wobble, the
 * reveal-truncation char count) live in render_quad.c / scene1_dialogue_run.c /
 * font_draw.c so the unit suite exercises them on the host.
 */
#include "scene1_dialogue_draw.h"

#ifdef _WIN32

#include "render_quad.h"   /* render_quad_* + <d3d8.h> (IDirect3DDevice8) */
#include "scene1_intro_dialogue.h"
#include "scene1_dialogue_run.h"

void scene1_dialogue_draw(IDirect3DDevice8 *dev)
{
    const struct ive_runtime *rt = scene1_intro_dialogue_runtime();
    if (rt == NULL)
        return;   /* no active script — nothing to draw */

    /* Layer 0: scaffold only. The bg / box / nameplate / text / standee draw
     * body lands in Layers 1-4 (docs/plans/vectorized-scribbling-backus.md). */
    (void)dev;
}

#endif /* _WIN32 */
