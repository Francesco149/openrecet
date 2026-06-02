/*
 * scene1_fps.c — see scene1_fps.h.
 *
 * FUN_004523e6 (the renderer) + the FUN_004547ab-tail frame-rate
 * computation that feeds it (DAT_073de63c).
 */

#include "scene1_fps.h"

#include <stdio.h>

/* ─── engine-state mirrors (FUN_004547ab tail) ─────────────────────────
 *
 *   DAT_073de644  total frames rendered (monotonic).
 *   DAT_073dde40  frame total at the start of the current ~1 s window.
 *   _DAT_073de640 virtual-clock ms at the start of the current window.
 *   DAT_073de63c  the displayed fps value.
 */
static uint32_t g_frame_total   = 0;
static uint32_t g_window_frames = 0;
static uint32_t g_window_ms     = 0;
static int      g_fps_value     = 0;

void scene1_fps_reset(void)
{
    g_frame_total   = 0;
    g_window_frames = 0;
    g_window_ms     = 0;
    g_fps_value     = 0;
}

void scene1_fps_tick(uint32_t now_ms)
{
    /* L51311: bump the total frame counter every rendered frame. */
    g_frame_total++;

    /* L51319-51324: refresh the displayed fps once more than 1000 ms has
     * elapsed since the last window start.  The engine writes
     *   fps = (frame_total*1000 + window_frames*-1000) / (now - window_ms)
     * which is just (frames-this-window * 1000) / elapsed_ms — an integer
     * average framerate over the window, evaluated as an unsigned divide. */
    uint32_t elapsed = now_ms - g_window_ms;
    if (elapsed > 1000u) {
        g_fps_value = (int)(((g_frame_total - g_window_frames) * 1000u) / elapsed);
        g_window_frames = g_frame_total;
        g_window_ms     = now_ms;
    }
}

int scene1_fps_value(void) { return g_fps_value; }

/* ─── Win32 render body (FUN_004523e6) ─────────────────────────────────── */

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "render_quad.h"
#include "sysassets.h"
#include "call_trace.h"

void scene1_fps_render(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* E.2 probe — FUN_004523e6 @ 0x4523e6. */
    CALL_TRACE_ENTER(0x4523e6u);

    /* L49560: master render gate DAT_005c570c (.data == 1, never written
     * elsewhere) — reproduced by always proceeding.  Bail only if the
     * texture failed to load (engine has no such guard; it would draw
     * garbage, we no-op). */
    const sprite_t *fps = &g_sysassets.fps2_tga;
    if (!fps->tex) return;

    /* L49563-49572: bind bmp/fps2.tga + the 2D alpha-blend state.
     *   SetTexture(0, fps2.tga)
     *   ALPHABLENDENABLE(0x1b)=1, SRCBLEND(0x13)=SRCALPHA, DESTBLEND(0x14)=INVSRCALPHA
     *   MAGFILTER(0x10)=LINEAR, MINFILTER(0x11)=LINEAR  (via SetTextureStageState) */
    IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)fps->tex);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);

    /* L49573-49577: the "Fps" label.  src corners (0,0)-(23,12); dst
     * (594,468) size 23x12.  render_quad_add takes dst{x,y,w,h} +
     * src{left,top,right,bottom}; engine's FUN_00404efc(dst, src, &tex). */
    {
        const float dst[4] = { 594.0f, 468.0f, 23.0f, 12.0f };
        const float src[4] = {   0.0f,   0.0f, 23.0f, 12.0f };
        render_quad_add(dst, src, fps->width, fps->height, 0xffffffffu);
    }

    /* L49578-49598: the value digits.  The engine formats the fps with a
     * two-wide field (the loop reads exactly two chars and skips spaces),
     * then draws each non-space char as a 16x32 glyph sampled from the
     * texture at x = digit*0x12 + 0x21 .. digit*0x12 + 0x31 (16 px wide,
     * full 32 px tall).  dst is (x, 462) size 10x20, advancing x by 8 per
     * drawn glyph from a start of 0x268 (616). */
    {
        char buf[256];
        snprintf(buf, sizeof buf, "%2d", scene1_fps_value());
        float dst_x = 616.0f;            /* 0x268 */
        for (int i = 0; i < 2; i++) {
            char c = buf[i];
            if (c == ' ' || c < '0' || c > '9') continue;
            int   digit = c - '0';
            float sx = (float)(digit * 0x12 + 0x21);   /* digit*18 + 33 */
            float sr = (float)(digit * 0x12 + 0x31);   /* digit*18 + 49 */
            const float dst[4] = { dst_x, 462.0f, 10.0f, 20.0f };
            const float src[4] = { sx,      0.0f, sr,    32.0f };
            render_quad_add(dst, src, fps->width, fps->height, 0xffffffffu);
            dst_x += 8.0f;
        }
    }

    /* L49600: single flush for the whole overlay (label + digits share
     * the bmp/fps2.tga texture, so they batch). */
    render_quad_flush(dev);
}

#endif /* _WIN32 */
