/*
 * light_debug.h — free-fly camera + optional hikari light-plane overlay.
 *
 * A recording/inspection mode for the HOUSE shop.  Toggled with F5 at
 * runtime:
 *
 *   - game input is frozen and a free dolly camera takes over: W/S dolly,
 *     A/D truck, Q/E pedestal, MOUSE look (cursor captured + hidden while
 *     the mode is on) or arrows pan/tilt, SHIFT fast, CTRL slow.  All
 *     motion runs through eased velocities so clips look smooth.  By
 *     DEFAULT the scene renders NORMALLY — pulling off the locked ¾ angle
 *     shows the real diorama breaking apart in the void, which is the
 *     striking reveal we want on camera (owner direction 2026-07-06).
 *
 *   - F7 plays a canned, eased flyoff dolly: the first press pulls the
 *     camera back-and-up off the game's locked pose to the "diorama in a
 *     black void" reveal; press again to ease back to the locked pose
 *     (re-assemble).  Scripted so a clean reveal is one keypress, not a
 *     hand-flown take.  Touching any WASD/QE key drops back to manual.
 *
 *   - F6 toggles the hikari-plane VISUALIZATION overlay (off by default)
 *     and cycles its modes: off → tint → flat → border → off.  The
 *     overlay draws every god-ray plane OPAQUE in a unique colour pair so
 *     the fold structure of each sheet is readable (this is the coloured-
 *     planes look from the earlier light-debug footage).
 *       0 tint    — fill hue + lit vertex colour (two-tone, additive-off)
 *       1 flat    — solid silhouette per plane (max extent readability)
 *       2 border  — tint fill + wireframe overlay in the pair's second
 *                   colour (reads fold edges when hues alone don't)
 *
 * Wiring: main.c WM_KEYDOWN (F5/F6/F7) → toggle/overlay/flyoff; input.c
 * input_poll zeroes the game buttons while active; scene1_render.c camera
 * setup defers to light_debug_camera_tick; scene1_walker_pass_init.c wraps
 * the pass-3 hikari draws with the per-plane overrides ONLY while the
 * overlay is active.
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
void light_debug_cycle_mode(void);      /* F6: overlay off→tint→flat→border→off */
int  light_debug_active(void);          /* free camera engaged */

/* Hikari-plane recolour overlay — separate from the camera.  The camera
 * can be flown with the scene rendering normally (default); the overlay
 * only recolours the planes once F6 (or --light-debug-mode) turns it on. */
int  light_debug_overlay_active(void);

/* F7: play the canned flyoff dolly.  First call eases the camera off the
 * game's locked pose to the diorama-in-void reveal; the next call eases
 * back.  Auto-engages the free camera if it isn't already on.  `current_view`
 * seeds the "assembled" reference pose the first time it engages. */
void light_debug_flyoff(const float current_view[16]);

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
