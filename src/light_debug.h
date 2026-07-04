/*
 * light_debug.h — hikari light-plane visualization + free-fly camera.
 *
 * A recording/inspection mode for the HOUSE god-ray sheets (the five
 * untextured vertex-lit planes of shop_1st.x — see
 * docs/findings/scene1-house-render-gaps.md and the recettear-study
 * lighting doc for the ground truth).  Toggled with F5 at runtime:
 *
 *   - every hikari plane draws OPAQUE in a unique colour pair — the
 *     normally-transparent (black-vertex) regions become a solid fill
 *     hue and the glow regions brighten toward the pair's highlight —
 *     so the full extent and fold structure of each sheet is readable;
 *   - game input is frozen and a free dolly camera takes over: W/S dolly,
 *     A/D truck, Q/E pedestal, MOUSE look (cursor captured + hidden while
 *     the mode is on) or arrows pan/tilt, SHIFT fast, CTRL slow.  All
 *     motion runs through eased velocities so clips look smooth;
 *   - F6 cycles visualization modes:
 *       0 tint    — fill hue + lit vertex colour (two-tone, additive-off)
 *       1 flat    — solid silhouette per plane (max extent readability)
 *       2 border  — tint fill + wireframe overlay in the pair's second
 *                   colour (reads fold edges when hues alone don't)
 *
 * Wiring: main.c WM_KEYDOWN (F5/F6) → toggle/cycle; input.c input_poll
 * zeroes the game buttons while active; scene1_render.c camera setup
 * defers to light_debug_camera_view; scene1_walker_pass_init.c wraps the
 * pass-3 hikari draws with the per-plane state overrides.
 */
#ifndef LIGHT_DEBUG_H
#define LIGHT_DEBUG_H

#ifdef _WIN32

struct IDirect3DDevice8;

/* ─── mode control (main.c hotkeys) ─────────────────────────────────── */

/* Toggle the mode.  `current_view` = the live scene-1 view matrix (16
 * floats, row-vector D3D convention); on activation the free camera
 * starts from exactly that pose so the toggle is seamless. */
void light_debug_toggle(const float current_view[16]);
void light_debug_cycle_mode(void);
int  light_debug_active(void);

/* --light-debug CLI flag: arm auto-activation; the camera-setup hook
 * calls maybe_autostart once per frame and the first call toggles the
 * mode on from the live game camera pose (scene1 must be up by then). */
void light_debug_set_autostart(void);
void light_debug_maybe_autostart(const float current_view[16]);

/* --light-debug-mode N: preselect the visualization mode (0 tint,
 * 1 flat, 2 border) — same as cycling with F6 after F5. */
void light_debug_set_mode(int mode);

/* Game window handle (mouse-look recentre target + cursor hide scope);
 * void* to keep windows.h out of this header. */
void light_debug_set_hwnd(void *hwnd);

/* ─── free camera (scene1_render.c) ─────────────────────────────────── */

/* Per-frame: read keys, move the camera, write the view matrix into
 * `out_view` (same convention as scene1_camera_build_view_matrix). */
void light_debug_camera_tick(float out_view[16]);

/* ─── hikari plane draw overrides (scene1_walker_pass_init.c) ───────── */

/* Bracket the pass-3 hikari slot loop.  begin() resets the per-frame
 * plane counter; end() restores the render states the overrides touch
 * (blend, fillmode, combiner args, cull). */
void light_debug_hikari_begin(void);
void light_debug_hikari_end(struct IDirect3DDevice8 *dev);

/* Apply the fill state for the next plane (call right before its
 * DrawIndexedPrimitive; advances the plane counter). */
void light_debug_plane_predraw(struct IDirect3DDevice8 *dev);

/* Wireframe border overlay: when enabled by the current mode, bracket a
 * SECOND identical DrawIndexedPrimitive with wire_begin/wire_end. */
int  light_debug_wire_enabled(void);
void light_debug_wire_begin(struct IDirect3DDevice8 *dev);
void light_debug_wire_end(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */
#endif /* LIGHT_DEBUG_H */
