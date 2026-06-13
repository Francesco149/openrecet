/*
 * screen_rt.h — screen-capture render targets (engine FUN_0047ae65 @ 0x47ae65).
 *
 * The fade / pause-backdrop system captures the live screen into a
 * render-target texture, blurs it through a 1280x256 intermediate, then
 * samples it full-screen as the pause-menu backdrop (the resting menu's
 * draw [0]).  Two RTs, created once at render init:
 *
 *   capture (RT#56)  screen-sized, backbuffer format   tex DAT_073de648 /
 *                                                       surf DAT_073de630
 *   blur    (RT#57)  1280x256, A8R8G8B8                 tex DAT_073de64c /
 *                                                       surf DAT_073de634
 *
 * Engine FUN_0047ae65 creates both (the capture-RT loop reads screen dims
 * DAT_005cbc04/08; the blur RT is the fixed 0x500x0x100 = 1280x256 A8R8G8B8).
 * Right after, FUN_00472f5d sets the sampling dims the engine pairs with each
 * when drawing the full-screen quads (DAT_073cb904/908 = 640x480 for the
 * capture RT, DAT_073d868c/8690 = 1280x256 for the blur RT) — so the capture
 * RT is sampled as 640x480-logical (UV 0..1 across the whole RT) and the blur
 * RT as 1280x256.  See docs/plans/pause-menu.md M3.
 */
#ifndef OPENRECET_SCREEN_RT_H
#define OPENRECET_SCREEN_RT_H

#include <stdint.h>

/* Sampling dims the engine pairs with each RT (DAT_073cb904/908,
 * DAT_073d868c/8690) — passed as tex_w/tex_h to render_quad_add so the UVs
 * span the whole RT regardless of its physical size. */
#define SCREEN_RT_CAPTURE_SAMPLE_W 640
#define SCREEN_RT_CAPTURE_SAMPLE_H 480
#define SCREEN_RT_BLUR_W           1280
#define SCREEN_RT_BLUR_H           256

#ifdef _WIN32
#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

/* Create both render targets (engine FUN_0047ae65).  screen_w/h = the
 * backbuffer dims (DAT_005cbc04/08).  Returns 1 on full success.  Idempotent:
 * a call with the RTs already live is a no-op success. */
int  screen_rt_init(IDirect3DDevice8 *dev, uint32_t screen_w, uint32_t screen_h);

/* Release both RTs + their surfaces (shutdown / device loss). */
void screen_rt_reset(void);

/* 1 once both RTs are live. */
int  screen_rt_ready(void);

/* Accessors — NULL until screen_rt_init succeeds. */
IDirect3DTexture8 *screen_rt_capture_tex(void);   /* RT#56 tex  (DAT_073de648) */
IDirect3DSurface8 *screen_rt_capture_surf(void);  /* RT#56 surf (DAT_073de630) */
IDirect3DTexture8 *screen_rt_blur_tex(void);      /* RT#57 tex  (DAT_073de64c) */
IDirect3DSurface8 *screen_rt_blur_surf(void);     /* RT#57 surf (DAT_073de634) */

/* Begin a screen capture: save the current render target + depth, bind the
 * capture RT (#56) with the saved depth, and clear it opaque black.  Engine
 * FUN_004547ab else-branch (L51086-51096), fired at the pause open ramp
 * c99c==2.  The whole scene then re-renders into RT#56. */
void screen_rt_capture_begin(IDirect3DDevice8 *dev);

/* End the capture: rebind the saved render target with a fresh depth surface
 * and release the saved ref.  Engine FUN_00454191 c99c==2 cleanup
 * (L50840-50851), fired after the scene has drawn into RT#56. */
void screen_rt_capture_end(IDirect3DDevice8 *dev);

#endif /* _WIN32 */
#endif /* OPENRECET_SCREEN_RT_H */
