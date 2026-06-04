/*
 * fade.c — see fade.h.
 *
 * Engine sources:
 *   FUN_004526f5 @ 0x4526f5 — phase-1 (fade-OUT begin) init
 *   FUN_0045281c @ 0x45281c — phase-(-1) (fade-IN begin) init
 *   FUN_004526ab @ 0x4526ab — per-tick counter advance
 *   FUN_004528b3 @ 0x4528b3 — "done?" query
 *   FUN_00453e8f @ 0x453e8f — alpha-quad render (with Ghidra
 *                              mis-decomp; see findings doc for the
 *                              recovered alpha formula)
 */

#include "fade.h"

#include "render_quad.h"
#include "call_trace.h"

int32_t g_fade_counter  = 0;
int32_t g_fade_phase    = 0;
int32_t g_fade_mode     = 0;
int32_t g_fade_duration = 0;

void fade_reset(void)
{
    g_fade_counter  = 0;
    g_fade_phase    = 0;
    g_fade_mode     = 0;
    g_fade_duration = 0;
}

void fade_phase1_start(int32_t mode, int32_t duration)
{
    /* E.2 probe — FUN_004526f5 @ 0x4526f5.  Engine body pre-rolls 100
     * float-vec scratch entries we skip (vestigial); the visible state
     * writes match. */
    CALL_TRACE_ENTER(0x4526f5u);

    g_fade_counter  = 1;
    g_fade_phase    = 1;
    g_fade_mode     = mode;
    g_fade_duration = duration;
}

void fade_phase_out_start(int32_t mode, int32_t duration)
{
    /* E.2 probe — FUN_0045281c @ 0x45281c.  Engine body zeroes the
     * same 100 scratch entries we skip. */
    CALL_TRACE_ENTER(0x45281cu);

    g_fade_phase    = -1;
    g_fade_mode     = mode;
    g_fade_duration = duration;
    g_fade_counter  = 0;
}

void fade_tick(void)
{
    /* E.2 probe — FUN_004526ab @ 0x4526ab.  Flow-trace seed: capture the
     * fade state this tick consumes (the "data used") as a declared payload
     * — retail mirror in tools/flow/retail_fields.json. */
    CALL_TRACE_BEGIN(0x4526abu);
    CALL_TRACE_I32("phase",    g_fade_phase);
    CALL_TRACE_I32("counter",  g_fade_counter);
    CALL_TRACE_I32("duration", g_fade_duration);
    CALL_TRACE_I32("mode",     g_fade_mode);
    CALL_TRACE_END();

    if (g_fade_phase == 0) return;

    if (g_fade_phase == 1) {
        g_fade_counter++;
        if (g_fade_counter > g_fade_duration + 1) {
            g_fade_counter = g_fade_duration + 1;
        }
    } else {
        g_fade_counter++;
        if (g_fade_counter > g_fade_duration) {
            g_fade_counter = 0;
            g_fade_phase   = 0;
        }
    }
}

int fade_is_done(void)
{
    /* E.2 probe — FUN_004528b3 @ 0x4528b3. */
    CALL_TRACE_ENTER(0x4528b3u);

    if (g_fade_phase != 1) return 0;
    if (g_fade_mode == 2) {
        return (g_fade_counter == 0x1f) ? 1 : 0;
    }
    return (g_fade_counter == g_fade_duration) ? 1 : 0;
}

/* ─── alpha-quad render (Win32 only) ─────────────────────────────────── */

#ifdef _WIN32

#include "sprite.h"

static sprite_t g_system_tex;
static int      g_system_tex_tried = 0;

int fade_load_system_texture(IDirect3DDevice8 *dev)
{
    if (g_system_tex.tex) return 1;
    if (g_system_tex_tried) return 0;
    g_system_tex_tried = 1;
    /* system.bmp is 128×128 — extracted from the bmpdata overlay. The
     * engine loads it at boot as part of an "always-loaded UI" group
     * alongside fps2.tga / nowloading.tga / savewindow.tga / item_win.tga;
     * we have no boot-time-textures module yet so it's lazy-loaded
     * inside fade_render. */
    return sprite_load(dev, "bmp/system.bmp", 128, 128, &g_system_tex);
}

void fade_unload_system_texture(void)
{
    sprite_destroy(&g_system_tex);
    g_system_tex_tried = 0;
}

void fade_render(IDirect3DDevice8 *dev)
{
    /* E.2 probe — FUN_00453e8f @ 0x453e8f. */
    CALL_TRACE_ENTER(0x453e8fu);

    if (!dev) return;
    if (g_fade_counter == 0) return;

    if (!fade_load_system_texture(dev)) return;

    /* Engine FUN_00453e8f: alpha formula (Ghidra mis-decomps the
     * `flds 0x519390 (= 256.0)` + `fdivs (duration-2)` pair). Recovered
     * by reading objdump — see docs/findings/title-fade-out.md.
     *
     * Phase 1 (fade darkening in over `duration-2` ticks):
     *   alpha = (int)(256.0f / (duration - 2) * counter)
     * Phase -1 (fade-in / quad reveals back-buffer):
     *   alpha = (int)(256.0f / (duration - 2) * (counter - 2))   ← NOTE:
     *   ... then take 255 - alpha
     *
     * The decomp branches between the two paths via a separate `__ftol`
     * + local-var rewrite at L24-26 of 453e8f.c; the formula above
     * collapses both into one computation. */
    int alpha;
    if (g_fade_duration > 2) {
        const float scale = 256.0f / (float)(g_fade_duration - 2);
        if (g_fade_phase == -1) {
            alpha = 255 - (int)(scale * (float)(g_fade_counter - 2));
        } else {
            alpha = (int)(scale * (float)g_fade_counter);
        }
    } else {
        /* Degenerate case — engine would divide by zero or negative.
         * No vendor data hits this; we clamp to fully-opaque so the
         * fade still completes visibly. */
        alpha = 255;
    }
    if (alpha < 0)   alpha = 0;
    if (alpha > 255) alpha = 255;

    render_quad_bind(dev, &g_system_tex);

    /* Engine reasserts a handful of render states before the quad —
     * fog OFF, alpha-blend ON, src/dst blend, alpha-test OFF, both
     * filters to LINEAR, alpha-op MODULATE. Most of these are already
     * set by render_quad_state_setup at the top of the frame; the
     * reasserts are defensive (the prior render pass may have changed
     * COLOROP for menu items / ADDSIGNED / etc.). We mirror the same
     * shape so the engine's quirks are reproduced verbatim. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE,        FALSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE,  FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);

    /* Source rect: mode 0 → (9,1)-(15,7) = pure-black 6×6 patch.
     * Other modes → (1,1)-(7,7) = pure-white 6×6 patch. */
    float src[4];
    if (g_fade_mode == 0) {
        src[0] = 9.0f;  src[1] = 1.0f;
        src[2] = 15.0f; src[3] = 7.0f;
    } else {
        src[0] = 1.0f;  src[1] = 1.0f;
        src[2] = 7.0f;  src[3] = 7.0f;
    }
    const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
    const uint32_t color = ((uint32_t)alpha << 24) | 0x00ffffffu;

    render_quad_add(dst, src, g_system_tex.width, g_system_tex.height, color);
    render_quad_flush(dev);
}

#endif /* _WIN32 */
