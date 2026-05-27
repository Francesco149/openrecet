/*
 * nowloading.c — see nowloading.h.
 *
 * Engine source: FUN_00453147 @ 0x453147 (362 bytes). The function
 * fuses the per-frame tick (alpha counter decay or rotation advance)
 * with the per-frame render; we preserve that fusion but expose the
 * pure tick path for tests.
 */

#include "nowloading.h"

#include "call_trace.h"

/* ─── state ──────────────────────────────────────────────────────────── */

/* Engine `_DAT_06a49988`. Drops by 32 per tick when the gate is 0,
 * clamped at 0. The engine never sets it positive in this function
 * — the rise comes from sibling UI code that we haven't ported yet.
 * Tracked here so the decay path behaves identically. */
static int   g_alpha_counter = 0;

/* Engine `_DAT_06a4998c`. Rises by 0.3 radians per tick while the
 * gate is set. Free to grow unbounded — sinf/cosf range-reduce. */
static float g_rotation_rad  = 0.0f;

/* Engine `DAT_06a49958` (worker "still loading") and `DAT_06a49960`
 * (secondary load gate). Our port collapses both into a single
 * `g_active` because every site that consults them does the OR. The
 * setter exposes only the primary; a future port of FUN_0049de24
 * (the secondary gate's producer) can grow this to two fields. */
static int   g_active        = 0;

/* ─── accessors ──────────────────────────────────────────────────────── */

int   nowloading_get_alpha_counter(void) { return g_alpha_counter; }
float nowloading_get_rotation(void)      { return g_rotation_rad;  }
int   nowloading_is_active(void)         { return g_active != 0;   }

void  nowloading_set_active(int active)  { g_active = active ? 1 : 0; }

void  nowloading_reset(void)
{
    g_alpha_counter = 0;
    g_rotation_rad  = 0.0f;
    g_active        = 0;
}

/* ─── tick ───────────────────────────────────────────────────────────── */

int nowloading_tick(void)
{
    const int prev_active = g_active;

    if (!g_active) {
        /* Engine L17-22 — alpha counter decays toward 0. */
        g_alpha_counter -= 0x20;
        if (g_alpha_counter < 0) g_alpha_counter = 0;
    } else {
        /* Engine L40 — `_DAT_06a4998c = _DAT_06a4998c + 0.3`. */
        g_rotation_rad += 0.3f;
    }
    return prev_active;
}

/* ─── render (Win32) ─────────────────────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "render_quad.h"
#include "sprite.h"
#include "sysassets.h"

void nowloading_render(IDirect3DDevice8 *dev)
{
    /* E.2 probe — FUN_00453147 @ 0x453147 (engine fuses tick + render). */
    CALL_TRACE_ENTER(0x453147u);

    if (!dev) return;

    /* Always tick first — the engine fuses this with the render. */
    nowloading_tick();

    if (!g_active) {
        /* Gate is 0: the engine returns without drawing. Counter was
         * already decayed by nowloading_tick(). */
        return;
    }

    /* Defensive — sysassets.nowloading_tga may not be loaded yet if
     * sysassets_load_all hasn't run (boot before scene_title init).
     * The engine's `SetTexture(DAT_073cc770)` accepts NULL but the
     * subsequent draw produces only the diffuse colour; rather than
     * paint a magenta rectangle on the user's screen, just bail. */
    const sprite_t *tga = &g_sysassets.nowloading_tga;
    if (!tga->tex) return;

    /* Engine L24-29 — render state setup. Matches what we already do
     * in render_quad_state_setup() (alpha blend SRCALPHA/INVSRCALPHA,
     * linear filtering); call it for parity even though preceding
     * scene renders almost certainly already set the same. */
    render_quad_state_setup(dev);

    /* L28 — SetTexture(stage 0, nowloading_tga). */
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)tga->tex);

    /* L29-38 — static 128×64 "Now Loading…" panel at (512, 400).
     * Source rect (64, 0, 192, 64) — the right half of the 256×64
     * texture (the spinner lives in the left 64-pixel column). */
    {
        const float dst[4] = { 512.0f, 400.0f, 128.0f, 64.0f };
        const float src[4] = {  64.0f,   0.0f, 192.0f, 64.0f };
        render_quad_add(dst, src, tga->width, tga->height, 0xffffffffu);
        render_quad_flush(dev);
    }

    /* L40 already advanced rotation via nowloading_tick(). */

    /* L41-49 — rotating 64×64 spinner at (496, 440).
     *
     * UV bit patterns from the engine literals:
     *   0x3a4ccccd = 0.5/640 ≈ 0.00078125 (half-texel inset on U)
     *   0x3b4ccccd = 0.5/160 = 0.003125   (half-texel inset on V)
     *   0x3e800000 = 0.25                 (right edge of 64-pixel column)
     *   0x3f800000 = 1.0                  (full height of 64-pixel texture)
     *
     * Engine literal 0x42000000 = 32.0 is the param_1[2] arg to
     * FUN_004063c7 — the function squares it, doubles, and sqrts,
     * yielding a diagonal radius of 32 * sqrt(2) ≈ 45.25. The
     * unrotated bounding box is therefore 64×64. */
    {
        const float center_x   = 496.0f;
        const float center_y   = 440.0f;
        const float half_size  =  32.0f;
        const float uv[4]      = { 0.00078125f, 0.003125f, 0.25f, 1.0f };
        render_quad_draw_rotated(dev, center_x, center_y, half_size,
                                 g_rotation_rad, uv, 0xffffffffu);
    }
}

#endif /* _WIN32 */
