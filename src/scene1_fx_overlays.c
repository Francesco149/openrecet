/*
 * scene1_fx_overlays.c — port of FUN_00454191 @ 0x454191 (the fade / scene-
 * capture overlay).  Called every frame at the head of scene1_render_fx_tail
 * (FUN_0045404b).  Dispatches the pause / scene-transition BACKDROP off the
 * slide ramp DAT_06a4999c (= sim's c99c) plus a 990 white-flash counter.
 *
 * The pause backdrop is the captured-screen render target (RT#56), radial-
 * blurred once at open and then sampled full-screen every rest frame as the
 * pause menu's draw [0].  Mechanism (decoded + verified bit-exact off the
 * retail D3D8 command stream — docs/plans/pause-menu.md M3):
 *
 *   c99c==2  the live scene has just re-rendered into RT#56 (the redirect in
 *            render_dispatch / engine FUN_004547ab) — restore the backbuffer.
 *   c99c==3  build the 2-pass blur composite ONCE: downsample RT#56 into the
 *            left 640x256 of RT#57, then accumulate 12 progressively zoomed-in
 *            copies back into RT#56 (the radial smear).  Then draw [0].
 *   c99c>3   draw [0]: sample the finished RT#56 full-screen with a fade
 *            alpha ramp (min(c99c*0x16,0xff), attenuated past 0xc on close).
 *
 * The 990 white-flash branch is still PORT-DEBT (no starter ported).
 */

#include "scene1_fx_overlays.h"

#include "call_trace.h"
#include "sim.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "screen_rt.h"
#include "render_quad.h"
#include "scene_pause.h"   /* g_pause_action — the blur clear-colour variant */
#include "sysassets.h"            /* g_sysassets.system_bmp (DAT_073aa188) */
#include "scene1_intro_dialogue.h" /* the DAT_0438bf74 blackout-active gate   */
#include "scene1_tutorial_dispatch.h" /* the b928/b924 beat that drives the day card */
#include "save_work.h"            /* save_work_dwords_at — the live day field    */
#include "save_bank.h"            /* SAVE_BANK_FIELD_CARD_DAY (0xb0fb)           */
#include "font_draw.h"            /* font_draw_text_centered (FUN_0047d14c)      */
#include <stdio.h>                /* snprintf — the "Day %d" format              */

/* The 2-pass radial-blur composite (engine FUN_00454191 c99c==3 block,
 * L50874-50943), built ONCE at pause open.  Leaves RT#56 holding the finished
 * blurred backdrop.  Per-pass geometry/colours verified against the retail
 * command stream (orv3_rt / orv3_draws on house-pause). */
static void fx_build_pause_blur(IDirect3DDevice8 *d)
{
    IDirect3DSurface8 *cap_surf  = screen_rt_capture_surf();   /* RT#56 */
    IDirect3DSurface8 *blur_surf = screen_rt_blur_surf();      /* RT#57 */
    IDirect3DTexture8 *cap_tex   = screen_rt_capture_tex();
    IDirect3DTexture8 *blur_tex  = screen_rt_blur_tex();
    if (!cap_surf || !blur_surf || !cap_tex || !blur_tex) return;

    /* The depth-less RTs carry no z-buffer; make sure z-test is off for the
     * blit draws, then restore (engine relies on the 2D state having z off). */
    DWORD saved_zenable = D3DZB_TRUE;
    IDirect3DDevice8_GetRenderState(d, D3DRS_ZENABLE, &saved_zenable);
    IDirect3DDevice8_SetRenderState(d, D3DRS_ZENABLE, D3DZB_FALSE);

    /* action 0 (ESC) clears RT#56 to 0xff173c8c; the status/encyclopedia pause
     * variants (action 1/2) use 0xff3c3c3c and are PORT-DEBT — only the ESC
     * path is exercised, but the select is matched. */
    const uint32_t rt56_clear = (g_pause_action == 0) ? 0xff173c8cu : 0xff3c3c3cu;

    IDirect3DSurface8 *save_rt, *save_depth;

    /* ── Pass A — downsample RT#56 → the left 640x256 of RT#57 (L50874-50890).
     * Bind RT#57 (no depth), clear dark blue, draw the whole captured screen
     * into a literal (unscaled) 640x256 rect. */
    save_rt = NULL; save_depth = NULL;
    IDirect3DDevice8_GetRenderTarget(d, &save_rt);
    IDirect3DDevice8_GetDepthStencilSurface(d, &save_depth);
    IDirect3DDevice8_SetRenderTarget(d, blur_surf, NULL);
    IDirect3DDevice8_Clear(d, 0, NULL, D3DCLEAR_TARGET, 0xff0000c8u, 1.0f, 0);
    IDirect3DDevice8_SetTexture(d, 0, (IDirect3DBaseTexture8 *)cap_tex);
    {
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 256.0f };
        const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_add_unscaled(dst, src, SCREEN_RT_CAPTURE_SAMPLE_W,
                                 SCREEN_RT_CAPTURE_SAMPLE_H, 0xffffffffu);
        render_quad_flush(d);
    }
    IDirect3DDevice8_SetRenderTarget(d, save_rt, save_depth);
    if (save_rt)    IDirect3DSurface8_Release(save_rt);
    if (save_depth) IDirect3DSurface8_Release(save_depth);

    /* ── Pass B — accumulate 12 zoomed copies RT#57 → RT#56 (L50901-50933).
     * Bind RT#56 (no depth), clear, then 12 full-screen quads sampling
     * progressively inset rects of RT#57 at alpha 0x14 (SRCALPHA blend). */
    save_rt = NULL; save_depth = NULL;
    IDirect3DDevice8_GetRenderTarget(d, &save_rt);
    IDirect3DDevice8_GetDepthStencilSurface(d, &save_depth);
    IDirect3DDevice8_SetRenderTarget(d, cap_surf, NULL);
    IDirect3DDevice8_Clear(d, 0, NULL, D3DCLEAR_TARGET, rt56_clear, 1.0f, 0);
    IDirect3DDevice8_SetTexture(d, 0, (IDirect3DBaseTexture8 *)blur_tex);
    for (int i = 0; i < 12; i++) {
        /* action 0: the inset steps 4px/tap (engine local_10 = 0,4,..,44);
         * s = i*4 + 4.  Center fixed, src shrinks inward (radial zoom). */
        const float s = (float)(i * 4) + 4.0f;
        const float src[4] = { s, s * 0.5f, 640.0f - s, 256.0f - s * 0.5f };
        const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
        render_quad_add(dst, src, SCREEN_RT_BLUR_W, SCREEN_RT_BLUR_H, 0x14dcdcdcu);
    }
    render_quad_flush(d);
    IDirect3DDevice8_SetRenderTarget(d, save_rt, save_depth);
    if (save_rt)    IDirect3DSurface8_Release(save_rt);
    if (save_depth) IDirect3DSurface8_Release(save_depth);

    IDirect3DDevice8_SetRenderState(d, D3DRS_ZENABLE, saved_zenable);
}

void scene1_fx_overlays(struct IDirect3DDevice8 *dev)
{
    CALL_TRACE_ENTER(0x454191u);
    IDirect3DDevice8 *d = (IDirect3DDevice8 *)dev;
    const int32_t c99c = sim_get_counter_99c();

    /* ── the pause / scene-transition backdrop (engine c99c block
     * L50839-50964).  c99c is non-zero only during a pause today. */
    if (1 < c99c) {
        /* c99c==2: the scene just re-rendered into RT#56 — restore the
         * backbuffer (engine cleanup dance L50840-50851). */
        if (c99c == 2)
            screen_rt_capture_end(d);

        render_quad_state_setup(d);                 /* FUN_0049b425 */
        /* LINEAR RT sampling (engine L50853-50854; render_quad_state_setup
         * already sets LINEAR, re-stated for draw-program fidelity). */
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
        IDirect3DDevice8_SetTextureStageState(d, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);

        if (2 < c99c && screen_rt_ready()) {
            /* (b1b0==1 system.bmp fade path = PORT-DEBT; b1b0==0 here.) */
            if (c99c == 3)
                fx_build_pause_blur(d);             /* build the composite once */

            /* [0] backdrop: sample RT#56 full-screen with the fade alpha ramp
             * (engine L50944-50961). */
            int32_t alpha = c99c * 0x16;
            if (alpha > 0xff) alpha = 0xff;
            if (c99c > 0xc) alpha += (0xc - c99c) * 0x20;    /* close fade-out */
            if (alpha < 0) alpha = 0;
            IDirect3DDevice8_SetTexture(d, 0,
                (IDirect3DBaseTexture8 *)screen_rt_capture_tex());
            const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
            const float src[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
            render_quad_add(dst, src, SCREEN_RT_CAPTURE_SAMPLE_W,
                            SCREEN_RT_CAPTURE_SAMPLE_H,
                            ((uint32_t)alpha << 24) | 0xffffffu);
            render_quad_flush(d);
        }
    }

    /* ── 990 white-flash counter (engine L50965-50996) — PORT-DEBT, dormant
     * (no starter ported; the gate is BSS-zero). */
    if (1 < sim_get_counter_990()) {
        /* Body deferred — see header. */
    }
}

void scene1_fx_screen_blackout(struct IDirect3DDevice8 *dev)
{
    /* Port of FUN_00453d9c @ 0x453d9c — the screen-blackout layer.  Called in
     * the render root (FUN_004547ab LAB_00454a90) AFTER the scene block and
     * BEFORE the dialogue (FUN_0046c090): for the iv1_1 opening (full-screen bg,
     * covers_screen ⇒ scene block skipped) this is retail's draw [0]; for iv1_2
     * (overlay) it sits after the scene.  Draws a FULL-SCREEN opaque-black quad
     * from bmp/system.bmp when the blackout flag (DAT_0438bf74) is set — under
     * the opaque cutscene bg/scene ⇒ 0 net px, but part of retail's render
     * PROGRAM (the v3 draw-program parity gap; pixels stay bit-identical).
     *
     * Geometry/blend from the disasm: dst (0,0,640,480) [render_quad_add scales
     * to the active backbuffer], src (9,1)-(15,7) of system.bmp, diffuse
     * 0xff000000, SRCALPHA/INVSRCALPHA, COLOROP=MODULATE — i.e. the standard 2D
     * preset (render_quad_state_setup = FUN_0049b425), which the engine here
     * sets via the individual SetRenderState calls to the same net state. */
    IDirect3DDevice8 *d = dev;
    const sprite_t *sys = &g_sysassets.system_bmp;
    if (!scene1_intro_dialogue_blackout_active() || sys->tex == NULL)
        return;
    render_quad_state_setup(d);                 /* FUN_0049b425 net state */
    render_quad_bind(d, sys);
    const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
    const float src[4] = { 9.0f, 1.0f, 15.0f, 7.0f };
    render_quad_add(dst, src, sys->width, sys->height, 0xff000000u);
    render_quad_flush(d);
}

void scene1_day_card_render(struct IDirect3DDevice8 *dev)
{
    /* Port of the "Day N" DAY-TRANSITION CARD — a self-contained block inside the
     * shop-foreground aggregator FUN_0040a765 (all.c:7500-7559 / 0x40c209).  Retail
     * gate: DAT_0438b1c0==1 && *DAT_068dd2f0==0 && DAT_0438b928==1 && DAT_0438b924<0x8c.
     * The b928/b924 pair IS the "Recette looks up at Tear" beat (ported as the
     * tutorial-dispatch beat, findings/cutscene-replay-anchor-drift.md Residual B):
     * armed at iv2_5, counted 0→190 once per free-roam frame, gates iv2_6 at b924<190.
     * For the first 140f of that beat the engine draws a full-screen OPAQUE-BLACK
     * backdrop + centred "Day N" text (alpha b924*8), then a WHITE exit-fade — the
     * black→white→reveal day-1→day-2 transition.
     *
     * The port does NOT arm the bf74 blackout for the iv2 chain (PORT-DEBT(blackout-
     * tut-dispatch)); it doesn't need to — retail's bf74 (FUN_00453d9c) merely draws a
     * redundant opaque black UNDER this card during the async load, and THIS card's own
     * backdrop is what turns the DAY2-transition screen black.  Called from the HOUSE
     * free-roam render tail AFTER the top-HUD aggregator, so the card covers the HUD
     * (retail draws it at FUN_0040a765 — draw [2] — over the scene, the same relative
     * order).  Colours/alphas/scale objdump-verified at 0x40c209-0x40c431. */
    if (!scene1_tutorial_dispatch_iv2_beat_active())          /* DAT_0438b928 != 1 */
        return;
    const int b924 = scene1_tutorial_dispatch_iv2_beat_ctr();
    if (b924 >= 0x8c)                                          /* b924 >= 140 → card gone */
        return;
    const sprite_t *sys = &g_sysassets.system_bmp;            /* DAT_073aa188 (system.bmp) */
    if (sys->tex == NULL)
        return;

    const float dst[4] = { 0.0f, 0.0f, 640.0f, 480.0f };      /* full-screen (xywh) */
    const float src[4] = { 0.0f, 0.0f, 7.0f, 7.0f };          /* solid patch of system.bmp */

    /* ── Block 1 (b924 < 0x7a): opaque-black backdrop + centred "Day N". ── */
    if (b924 < 0x7a) {
        render_quad_state_setup(dev);
        render_quad_bind(dev, sys);
        render_quad_add(dst, src, sys->width, sys->height, 0xff000000u); /* black @0x40c247 */
        render_quad_flush(dev);

        int alpha = b924 * 8;                                 /* @0x40c2a8 shl 3 */
        if (alpha > 0xff) alpha = 0xff;                       /* cap 255 (32f fade-in) */
        /* Survival/Endless mode labels (@0x40c2b4/40c2f3) are story-mode-inert — skipped. */
        const uint32_t *wb = save_work_dwords_at(save_work_active_slot());
        int day = (wb ? (int)wb[SAVE_BANK_FIELD_CARD_DAY] : 0) + 1;  /* save[0x2c3ec]+1 */
        if (day > 9999) day = 9999;
        char buf[32];
        snprintf(buf, sizeof buf, "Day %d", day);            /* s_Day__d_0052a7cc */
        font_draw_text_centered(dev, 320.0f, 208.0f, buf,
                                ((uint32_t)alpha << 24) | 0xffffffu, 2.2f); /* white @0x40c383 */
    }

    /* ── Block 2 (b924 > 0x5a): WHITE exit-fade quad (@0x40c396). ── */
    if (b924 > 0x5a) {
        int a = b924 * 8 - 0x2d0;                             /* @0x40c3a4 lea -0x2d0(,eax,8) */
        if (a > 0xff) a = 0xff;
        if (b924 > 0x7a)                                      /* @0x40c3b1 */
            a += (0x7a - b924) * 0x10;                        /* @0x40c3bb shl 4 (ramp back down) */
        if (a > 0) {
            render_quad_state_setup(dev);
            render_quad_bind(dev, sys);
            render_quad_add(dst, src, sys->width, sys->height,
                            ((uint32_t)a << 24) | 0xffffffu); /* WHITE @0x40c3e4 */
            render_quad_flush(dev);
        }
    }
}

#else  /* !_WIN32 — Linux host-test build (no d3d8) */

void scene1_fx_overlays(struct IDirect3DDevice8 *dev)
{
    /* Host build: the backdrop draws are Win32-only.  Keep the counter reads
     * so call-trace parity / gate behaviour is unchanged. */
    CALL_TRACE_ENTER(0x454191u);
    (void)dev;
    if (1 < sim_get_counter_99c()) { /* Win32-only draws */ }
    if (1 < sim_get_counter_990()) { /* Win32-only draws */ }
}

void scene1_fx_screen_blackout(struct IDirect3DDevice8 *dev)
{
    /* Host build: the blackout quad is Win32-only (see the _WIN32 body). */
    (void)dev;
}

void scene1_day_card_render(struct IDirect3DDevice8 *dev)
{
    /* Host build: the day-transition card is Win32-only (see the _WIN32 body). */
    (void)dev;
}

#endif /* _WIN32 */
