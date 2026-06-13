/*
 * screen_rt.c — see screen_rt.h.
 *
 * Engine source: FUN_0047ae65 @ 0x47ae65 (237 bytes), the render-init helper
 * that creates the screen-capture + blur render targets.  The capture-RT loop
 * (L78060-78074) creates a screen-sized RENDERTARGET texture in DEFAULT pool
 * using the backbuffer format DAT_073dfc9c and pulls its level-0 surface; the
 * blur RT (L78078-78085) is the fixed 1280x256 A8R8G8B8 RT.  (The engine loop
 * is written as if it could create an array of capture RTs indexed by
 * DAT_06a499d0, but the bound is 1 — a single capture RT, index 0.)
 */

#include "screen_rt.h"

#ifdef _WIN32

#include <stdio.h>

static IDirect3DTexture8 *g_capture_tex;   /* RT#56 — DAT_073de648 */
static IDirect3DSurface8 *g_capture_surf;  /* RT#56 — DAT_073de630 */
static IDirect3DTexture8 *g_blur_tex;      /* RT#57 — DAT_073de64c */
static IDirect3DSurface8 *g_blur_surf;     /* RT#57 — DAT_073de634 */
static IDirect3DSurface8 *g_saved_rt;      /* DAT_073dfce0 — backbuffer held across a capture */

IDirect3DTexture8 *screen_rt_capture_tex(void)  { return g_capture_tex; }
IDirect3DSurface8 *screen_rt_capture_surf(void) { return g_capture_surf; }
IDirect3DTexture8 *screen_rt_blur_tex(void)     { return g_blur_tex; }
IDirect3DSurface8 *screen_rt_blur_surf(void)    { return g_blur_surf; }

int screen_rt_ready(void) { return g_capture_surf != NULL && g_blur_surf != NULL; }

void screen_rt_reset(void)
{
    if (g_saved_rt)     { IDirect3DSurface8_Release(g_saved_rt);     g_saved_rt     = NULL; }
    if (g_capture_surf) { IDirect3DSurface8_Release(g_capture_surf); g_capture_surf = NULL; }
    if (g_capture_tex)  { IDirect3DTexture8_Release(g_capture_tex);  g_capture_tex  = NULL; }
    if (g_blur_surf)    { IDirect3DSurface8_Release(g_blur_surf);    g_blur_surf    = NULL; }
    if (g_blur_tex)     { IDirect3DTexture8_Release(g_blur_tex);     g_blur_tex     = NULL; }
}

void screen_rt_capture_begin(IDirect3DDevice8 *dev)
{
    if (!dev || !screen_rt_ready() || g_saved_rt) return;

    /* Save the backbuffer RT (held until screen_rt_capture_end), grab the
     * current depth, bind RT#56 + that depth, then clear black.  The depth
     * ref we hold is released right after SetRenderTarget AddRefs it. */
    IDirect3DSurface8 *depth = NULL;
    if (FAILED(IDirect3DDevice8_GetRenderTarget(dev, &g_saved_rt)) || !g_saved_rt) {
        g_saved_rt = NULL;
        return;
    }
    IDirect3DDevice8_GetDepthStencilSurface(dev, &depth);
    IDirect3DDevice8_SetRenderTarget(dev, g_capture_surf, depth);
    if (depth) IDirect3DSurface8_Release(depth);
    IDirect3DDevice8_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           0xff000000u, 1.0f, 0);
}

void screen_rt_capture_end(IDirect3DDevice8 *dev)
{
    if (!dev || !g_saved_rt) return;

    IDirect3DSurface8 *depth = NULL;
    IDirect3DDevice8_GetDepthStencilSurface(dev, &depth);
    IDirect3DDevice8_SetRenderTarget(dev, g_saved_rt, depth);
    if (depth) IDirect3DSurface8_Release(depth);
    IDirect3DSurface8_Release(g_saved_rt);
    g_saved_rt = NULL;
}

int screen_rt_init(IDirect3DDevice8 *dev, uint32_t screen_w, uint32_t screen_h)
{
    if (!dev) return 0;
    if (screen_rt_ready()) return 1;   /* idempotent */

    /* Backbuffer format (engine DAT_073dfc9c) = the current render target's
     * format at init time.  Default to X8R8G8B8 if the query fails. */
    D3DFORMAT bb_fmt = D3DFMT_X8R8G8B8;
    IDirect3DSurface8 *cur = NULL;
    if (SUCCEEDED(IDirect3DDevice8_GetRenderTarget(dev, &cur)) && cur) {
        D3DSURFACE_DESC d;
        if (SUCCEEDED(IDirect3DSurface8_GetDesc(cur, &d))) bb_fmt = d.Format;
        IDirect3DSurface8_Release(cur);
    }

    /* RT#56 — the screen-sized capture target (engine FUN_0047ae65 L78061). */
    HRESULT hr = IDirect3DDevice8_CreateTexture(
        dev, screen_w, screen_h, 1, D3DUSAGE_RENDERTARGET, bb_fmt,
        D3DPOOL_DEFAULT, &g_capture_tex);
    if (FAILED(hr) || !g_capture_tex) {
        fprintf(stderr, "screen_rt: capture CreateTexture failed (0x%08lx)\n",
                (unsigned long)hr);
        screen_rt_reset();
        return 0;
    }
    hr = IDirect3DTexture8_GetSurfaceLevel(g_capture_tex, 0, &g_capture_surf);
    if (FAILED(hr) || !g_capture_surf) {
        fprintf(stderr, "screen_rt: capture GetSurfaceLevel failed (0x%08lx)\n",
                (unsigned long)hr);
        screen_rt_reset();
        return 0;
    }

    /* RT#57 — the 1280x256 A8R8G8B8 blur intermediate (engine L78078). */
    hr = IDirect3DDevice8_CreateTexture(
        dev, SCREEN_RT_BLUR_W, SCREEN_RT_BLUR_H, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_blur_tex);
    if (FAILED(hr) || !g_blur_tex) {
        fprintf(stderr, "screen_rt: blur CreateTexture failed (0x%08lx)\n",
                (unsigned long)hr);
        screen_rt_reset();
        return 0;
    }
    hr = IDirect3DTexture8_GetSurfaceLevel(g_blur_tex, 0, &g_blur_surf);
    if (FAILED(hr) || !g_blur_surf) {
        fprintf(stderr, "screen_rt: blur GetSurfaceLevel failed (0x%08lx)\n",
                (unsigned long)hr);
        screen_rt_reset();
        return 0;
    }

    fprintf(stderr,
            "screen_rt: capture %ux%u fmt=%d + blur %dx%d A8R8G8B8 created\n",
            (unsigned)screen_w, (unsigned)screen_h, (int)bb_fmt,
            SCREEN_RT_BLUR_W, SCREEN_RT_BLUR_H);
    return 1;
}

#endif /* _WIN32 */
