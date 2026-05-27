/*
 * scene1_fx_overlays.h — port of FUN_00454191 @ 0x454191 (1391 bytes).
 *
 * Per-frame head call of scene1_render_fx_tail (FUN_0045404b).  Fires
 * every frame and dispatches up to three full-screen alpha quads based
 * on two scene-effect counters:
 *
 *   counter_99c — scene-transition fade (DAT_06a4999c)
 *     value 2 + sfnouse=0:  cleanup phase — GetDepthStencilSurface +
 *                           SetRenderTarget(saved, depth) dance + Release
 *                           the two saved refs.
 *     value 3 (sfnouse=0):  full render-to-texture screen capture
 *                           (uses DAT_073de630/073de634).
 *     value 3+ otherwise:   fullscreen dim quad with alpha = counter*0x16
 *                           (clamped to 0xff; late-frame dim factor at
 *                           counter > 0xc).
 *
 *   counter_990 — white-flash overlay (DAT_06a49990, gated sfnouse=0)
 *     value 2:              cleanup phase (mirrors the 99c==2 dance).
 *     value 3+:             full alpha quad via DAT_073de648 post-tex,
 *                           alpha = (0x10..0xff cyclic).
 *
 * Engine source: docs/decompiled/by-address/454191.c.
 *
 * SCAFFOLD PORT — body deferred.  Both counter_99c and counter_990 are
 * BSS-zero in title / cutscene / HOUSE today.  The starters
 * (FUN_004532b1 / FUN_004532bc / a cluster of scene-transition helpers)
 * are unported, so the engine's gates always short-circuit.  This chip
 * lands the entry probe + the gate scaffold so call_trace_diff sees the
 * function fire (closing the last real frame-1 port-gap in pre-3D); the
 * three inner render branches port when the counter starters do.
 *
 * Why mark CALL_TRACE_ENTER_STUB: per call_trace.h, the marker is for
 * "function body is partially ported AND the unimplemented portion is
 * load-bearing for the current scene's behaviour".  Today the gates
 * always fail so the inner branches are NOT load-bearing — but the
 * minute a scene-transition starter wires up, the un-ported branches
 * become a visible-pixel regression vs retail.  STUB keeps the partial
 * port honest so the diff tool surfaces it as `≈` instead of clean `=`.
 */

#ifndef OPENRECET_SCENE1_FX_OVERLAYS_H
#define OPENRECET_SCENE1_FX_OVERLAYS_H

struct IDirect3DDevice8;

/* Per-frame screen-effect overlay dispatch.  Engine FUN_00454191.
 * Called at the head of scene1_render_fx_tail every frame; body
 * short-circuits when both counter gates are dormant (the default
 * state today). */
void scene1_fx_overlays(struct IDirect3DDevice8 *dev);

#endif /* OPENRECET_SCENE1_FX_OVERLAYS_H */
