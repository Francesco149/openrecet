/*
 * scene1_dungeon_clear_banner.c — see scene1_dungeon_clear_banner.h.
 *
 * Faithful port of FUN_0048fe43 @ 0x48fe43 (315 bytes).  Engine asm
 * verified against `docs/decompiled/all.c` L92539-92589.
 */

#include "scene1_dungeon_clear_banner.h"

#include "call_trace.h"

/* ─── state ─────────────────────────────────────────────────────────── */

/* Engine DAT_0438be94 / DAT_0438be98.  Both BSS-zero by default. */
static int32_t g_banner_counter = 0;
static int32_t g_banner_slice   = 0;

int32_t scene1_dungeon_clear_banner_get_counter(void) { return g_banner_counter; }
void    scene1_dungeon_clear_banner_set_counter(int32_t v) { g_banner_counter = v; }

int32_t scene1_dungeon_clear_banner_get_slice(void) { return g_banner_slice; }
void    scene1_dungeon_clear_banner_set_slice(int32_t v) { g_banner_slice = v; }

void    scene1_dungeon_clear_banner_reset(void)
{
    g_banner_counter = 0;
    g_banner_slice   = 0;
}

/* ─── pure-C helpers (testable without a D3D device) ────────────────── */

float scene1_dungeon_clear_banner_compute_y(int32_t counter)
{
    /* Engine L92556-92565 verbatim.
     *
     * Phase 1 (counter in 1..119): y ramps from -118 (counter=1) toward
     * +96, clamped at +96 once counter * 2 - 120 >= 96 (counter >= 108).
     *
     * Phase 2 (counter >= 120): y ramps DOWN from +96, with a different
     * slope: y = 96 - (counter * 3 - 360) * 2 = 96 - counter*6 + 720
     *         = 816 - counter * 6.
     * Clamped at -48 once counter >= 144.
     *
     * Counter == 0: phase-1 formula yields -120, the off-screen start. */
    float y = (float)(counter * 2) - 120.0f;
    if (96.0f < y) {
        y = 96.0f;
    }
    if (0x77 < counter) {
        y = 96.0f - (float)((counter * 3 + -0x168) * 2);
        if (y < -48.0f) {
            y = -48.0f;
        }
    }
    return y;
}

void scene1_dungeon_clear_banner_compute_u(int32_t slice,
                                           float *u0, float *u1)
{
    /* Engine L92567-92578.  The slice-1 vs slice-2 u-ranges are
     * intentionally swapped relative to numeric order:
     *   slice 0 (or any non-1/2): u = [  0, 128]   left strip
     *   slice 1:                   u = [256, 384]  right strip
     *   slice 2:                   u = [128, 256]  middle strip
     * That's the engine's literal cascade — preserved verbatim. */
    if (slice == 1) {
        if (u0) *u0 = 256.0f;
        if (u1) *u1 = 384.0f;
    } else if (slice == 2) {
        if (u0) *u0 = 128.0f;
        if (u1) *u1 = 256.0f;
    } else {
        if (u0) *u0 =   0.0f;
        if (u1) *u1 = 128.0f;
    }
}

/* ─── render (Win32) ────────────────────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "render_quad.h"
#include "sprite.h"

/* Texture is owned by the asset loader (engine DAT_073cb8e0 — populated
 * by FUN_00474a9a's INGAME branch via FUN_0047193c).  Until the
 * dungeon_clear.tga loader wires in, this stays NULL and the body's
 * draw path is skipped (engine parity: it would crash on SetTexture
 * with a NULL texture anyway, but our render_quad_add is also a no-op
 * with NULL tex_w/h so the safe path is to skip when the asset isn't
 * loaded). */
static const sprite_t *g_banner_tex = NULL;

void scene1_dungeon_clear_banner_set_texture(const struct sprite_t *spr)
{
    g_banner_tex = (const sprite_t *)spr;
}

void scene1_dungeon_clear_banner_render(struct IDirect3DDevice8 *dev_in)
{
    /* E.2 probe — FUN_0048fe43 @ 0x48fe43. */
    CALL_TRACE_ENTER(0x48fe43u);

    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* Engine L92554 — UNCONDITIONAL render_quad_state_setup, fires even
     * when the counter gate short-circuits the draw.  Required for
     * trace parity (the call shows up on retail every frame the banner
     * func fires, regardless of whether the body executes). */
    render_quad_state_setup(dev);

    if (g_banner_counter <= 0) return;

    /* Body fires only when counter > 0.  Texture-NULL skip preserves
     * crash safety until the loader wires in. */
    if (!g_banner_tex || !g_banner_tex->tex) return;

    const float y    = scene1_dungeon_clear_banner_compute_y(g_banner_counter);
    float u0, u1;
    scene1_dungeon_clear_banner_compute_u(g_banner_slice, &u0, &u1);

    /* L92566 — SetTexture(stage 0, dungeon_clear_tex). */
    IDirect3DDevice8_SetTexture(dev, 0,
                                (IDirect3DBaseTexture8 *)g_banner_tex->tex);

    /* Dst (80, y, 480, 128) — engine literals 0x42a00000 (80) /
     * 0x43f00000 (480) / 0x43000000 (128).  Y is the animated value. */
    const float dst[4] = { 80.0f, y, 480.0f, 128.0f };

    /* Src (u0, 0, u1-u0, 480) — engine literals: v0=0, v1=480, u-range
     * from compute_u above.  The engine packs the src rect as
     * {x, y, w, h} in render_quad_add's calling convention. */
    const float src[4] = { u0, 0.0f, u1 - u0, 480.0f };

    render_quad_add(dst, src,
                    g_banner_tex->width, g_banner_tex->height,
                    0xffffffffu);
    render_quad_flush(dev);
}

#endif /* _WIN32 */
