/*
 * scene1_render.h — scene-1 (INGAME) render frame pre/post brackets.
 *
 * Three small engine functions sit around FUN_0040a765 (the 7558-byte
 * mesh walker) in the scene-1 render frame:
 *
 *     scene1_render_camera_setup    ← FUN_0045bbf9 (134 B, "pre-walker")
 *         ↓
 *     [ FUN_0040a765 mesh walker ]  ← C7j..C7n (not in this chip)
 *         ↓
 *     scene1_render_overlay         ← FUN_00417504 (506 B, "post-walker")
 *         ↓
 *     scene1_render_fx_tail         ← FUN_0045404b (326 B, "post-walker tail")
 *
 * This chip lands the three brackets (C7f/C7g/C7h). The walker between
 * them is the next climb. None of the three entry points are wired into
 * render_dispatch yet — wiring is part of the orchestrator chip after
 * the walker survey (C7i) splits out the per-sub-chip work.
 *
 * What lands as code today:
 *
 *   • Module-local mirrors of the four engine globals the brackets
 *     touch:
 *       DAT_073de29c → g_scene1_view   (view matrix, 64 B)
 *       DAT_073de2dc → g_scene1_proj   (proj matrix, 64 B, re-derived)
 *       DAT_073de3a0 → g_scene1_fov_deg(scalar fov in degrees, default
 *                                        45° per all.c:34225 / 0x42340000)
 *       DAT_073de648 → g_scene1_post_tex[…] (1 array slot — engine has
 *                                        post-process textures here;
 *                                        we keep the slot but read NULL
 *                                        until the source ports)
 *
 *   • A line-by-line mirror of each function's control flow. Sub-calls
 *     that are too large to port in this chip become explicit stubs
 *     with engine-line-number comments so the next porter knows what
 *     they cover.
 *
 * Sub-calls **deferred** by this chip (all reachable from the three
 * brackets, all >300 bytes — chip-sized work each):
 *
 *   FUN_00441c3e  (2217 B)  — camera-pose update, called from the
 *                             pre-walker when scene counters 998 + 6fa4
 *                             are both zero (the "no menu / no shake"
 *                             gate). Writes DAT_073de29c.
 *   FUN_004424e7  ( 429 B)  — angle / scene-rotation update, called
 *                             unconditionally after the camera-pose
 *                             update.  Reads DAT_073de31c..330 (player-
 *                             relative state) and writes DAT_0438cd78
 *                             (and downstream).  Not the view matrix
 *                             itself, but a sibling of the camera math.
 *   FUN_00454191  (1391 B)  — screen-effect overlays (shake + flash)
 *                             from the FX tail.  Reads counters 990 /
 *                             994 / 99c.  Dormant until any starter
 *                             ports (FUN_004532b1 / FUN_004532bc /
 *                             scene-transition stubs).
 *   FUN_00452f58  ( 491 B)  — HUD/scene-2 camera+projection switch for
 *                             the overlay pass.  Sets a separate
 *                             D3DTS_VIEW + D3DTS_PROJECTION on the
 *                             device before the layer dispatch.
 *   FUN_00414ee2  (4006 B)  — the per-layer 2D overlay dispatcher.
 *                             Called four times (layers 1, 0, 2, 3)
 *                             with distinct blend-mode presets around
 *                             each call.  Owns the per-layer quad
 *                             emission (text, sprites, HUD widgets).
 *
 * Each deferred sub-call is represented by an internal helper named
 * `scene1_<thing>_TODO` so a follow-up chip can replace the helper
 * body without touching the bracket structure.
 */

#ifndef OPENRECET_SCENE1_RENDER_H
#define OPENRECET_SCENE1_RENDER_H

#ifdef _WIN32

struct IDirect3DDevice8;

/* ───────────────────────────────────────────────────────────────────────
 * C7f — scene-1 PRE-walker
 *
 * Engine source: FUN_0045bbf9 @ 0x45bbf9 (134 bytes).
 *
 * Sequence (engine line numbers are 1-based from the .c file in
 * docs/decompiled/by-address/45bbf9.c):
 *
 *   L8   if (counter_998 == 0 && counter_6fa4 == 0)
 *   L9       FUN_00441c3e()         ← camera-pose update (deferred)
 *   L11  FUN_004424e7()             ← angle update (deferred)
 *   L12  SetTransform(D3DTS_VIEW, &g_scene1_view)
 *   L13  FUN_004a3ee8(&g_scene1_proj,
 *                     g_scene1_fov_deg * π/180,
 *                     4/3,                    // aspect (fixed in engine)
 *                     1.0,                    // z_near
 *                     350.0)                  // z_far
 *   L14  SetTransform(D3DTS_PROJECTION, &g_scene1_proj)
 *   L15  FUN_00459dfd(dev)          ← mesh_set_default_render_state
 *
 * Today the two deferred camera helpers leave g_scene1_view at its
 * boot value (identity from scene1_render_reset_view, mirroring the
 * engine's BSS-zero where the matrix is initialized once by the first
 * camera-pose tick).  The projection is rebuilt every frame from the
 * three constants + g_scene1_fov_deg.
 *
 * No-op when dev is NULL.
 */
void scene1_render_camera_setup(struct IDirect3DDevice8 *dev);

/* ───────────────────────────────────────────────────────────────────────
 * C7g — scene-1 POST-walker tail
 *
 * Engine source: FUN_0045404b @ 0x45404b (326 bytes).
 *
 * Sequence:
 *
 *   L19  FUN_00454191()                       ← FX overlays (deferred)
 *   L20  if (DAT_0438b1b0 == 0 && counter_994 > 0) {
 *   L21      FUN_0049b425()                   ← render_quad_state_setup
 *   L22-31   GetDepthStencilSurface + SetRenderTarget + Release dance
 *            (restores the render target + depth surface saved into
 *            DAT_073dfce0 / 4 earlier in the frame, then releases the
 *            saved refs — no-op for us until the redirect-source ports)
 *   L32      SetTexture(0, &post_tex[counter_99d0])
 *   L33      alpha = 0xff - ftol(sin(counter_994 * π / threshold94))
 *   L34-46   build src + dst rects (full 640×480) + render_quad_add
 *   L47      render_quad_flush
 *   }
 *
 * Counter_994 is sim_get_counter_994(); threshold94 is
 * sim_get_threshold94() (sim.h).  Both stay at 0 / 0 until a scene
 * starter (FUN_004532b1 / FUN_004532bc, unported) writes them, so
 * today this whole branch is dormant — `if (... && 994 > 0)` is false.
 *
 * Implementation note: the engine's `ftol()` of sin's [-1, 1] domain
 * just truncates to {-1, 0, 1}, which is almost certainly Ghidra
 * having eaten an intermediate `* 0xff` or `* 0x80` scale.  Today we
 * implement the formula as written; once a starter populates the
 * counters and we see a real shake, the scale will surface in the
 * Frida side-by-side and we can correct it in a follow-up.
 *
 * No-op when dev is NULL.
 */
void scene1_render_fx_tail(struct IDirect3DDevice8 *dev);

/* ───────────────────────────────────────────────────────────────────────
 * C7h — scene-1 POST-walker overlay layer dispatch
 *
 * Engine source: FUN_00417504 @ 0x417504 (506 bytes).
 *
 * Sequence (each block ends with one FUN_00414ee2(layer, 1) call,
 * which is itself a 4006-byte function deferred to a follow-up chip):
 *
 *   L6   FUN_00452f58()              ← HUD camera+proj (deferred)
 *
 *   L7-15   per-frame render-state reset for 2D RHW + alpha:
 *     COLORVERTEX=0      VertexShader=0x142 (XYZRHW|DIFFUSE|TEX1)
 *     ZENABLE=0          ZWRITEENABLE=0
 *     TSS0 TEXCOORDINDEX=2    TSS0 TEXTURETRANSFORMFLAGS=COUNT2
 *     FOGENABLE=0        ALPHABLENDENABLE=1
 *     TSS0 COLOROP=MODULATE
 *     AMBIENT=0xff101010 SHADEMODE=GOURAUD
 *     TSS0 COLORARG2=4 (D3DTA_SPECULAR)
 *     LightEnable(0, 0)  LIGHTING=0
 *     CULLMODE=NONE
 *
 *   L16-18  SRCBLEND=SRCALPHA DESTBLEND=INVSRCCOLOR (engine quirk; the
 *           non-standard INVSRCCOLOR dest is reproduced verbatim).
 *   L19      FUN_00414ee2(1, 1)      ← layer 1 (deferred)
 *
 *   L20-22  SRCBLEND=ONE DESTBLEND=ONE   (additive)
 *   L23      FUN_00414ee2(0, 1)      ← layer 0 (deferred)
 *
 *   L24-26  ALPHAREF=0 SRCBLEND=SRCALPHA DESTBLEND=INVSRCCOLOR
 *   L27      FUN_00414ee2(2, 1)      ← layer 2 (deferred)
 *
 *   L28     SRCBLEND=ZERO            (mask-by-dest)
 *   L29      FUN_00414ee2(3, 1)      ← layer 3 (deferred)
 *
 *   L30-31  SRCBLEND=SRCALPHA DESTBLEND=INVSRCCOLOR (reset to layer-1)
 *
 * The four FUN_00414ee2(N, 1) calls are the 2D overlay dispatcher —
 * a 4006-byte function that walks per-layer queues (text / sprites /
 * HUD widgets), each populated by the gameplay code.  Until that
 * function ports, the dispatcher stub here is a no-op + log marker so
 * the per-layer blend transitions are observable in a trace.
 *
 * Note on dest blend: D3DBLEND_INVSRCCOLOR (4) is unusual for a 2D UI
 * pass — typical alpha would use INVSRCALPHA (6).  The engine value
 * (4) is what the original .exe writes, so we preserve it; once the
 * dispatcher ports and a real layer emits visible output, the Frida
 * side-by-side will confirm the engine's blend choice.
 *
 * No-op when dev is NULL.
 */
void scene1_render_overlay(struct IDirect3DDevice8 *dev);

/* ───────────────────────────────────────────────────────────────────────
 * Camera + FOV accessors — for the camera-helper ports (FUN_00441c3e
 * + FUN_004424e7) when they land.  Tests + manual smokes can also use
 * these to inject a known view matrix without running the full camera
 * stack.
 *
 * Storage convention: row-major float[16], same as math3d.h /
 * mat4_lookat_rh — direct cast to D3DMATRIX is sound on D3D8.
 */

/* Get a writable pointer to the 16-float view matrix
 * (DAT_073de29c).  The pre-walker SetTransform reads this every
 * frame. */
float *scene1_render_view_matrix(void);

/* Get a read-only pointer to the 16-float projection matrix
 * (DAT_073de2dc).  Re-derived from g_scene1_fov_deg + aspect +
 * 1.0/350.0 near/far inside scene1_render_camera_setup; reads outside
 * that path observe the result of the most recent setup call. */
const float *scene1_render_proj_matrix(void);

/* Read the current FOV in degrees (DAT_073de3a0 at 0x42340000 = 45°
 * default). */
float scene1_render_fov_deg(void);

/* Override the FOV in degrees.  Engine writes this from various
 * sources (camera punch, dialog zoom, etc.) — the value is read at
 * pre-walker time. */
void scene1_render_set_fov_deg(float deg);

/* Reset the view matrix to identity. Useful for tests / smokes that
 * want to inject a known starting matrix before scene1_render_camera_
 * setup runs (the unported camera-pose helper would otherwise write
 * its own pose into the matrix). */
void scene1_render_reset_view(void);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_RENDER_H */
