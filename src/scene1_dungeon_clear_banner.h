/*
 * scene1_dungeon_clear_banner.h — port of FUN_0048fe43 @ 0x48fe43.
 *
 * Per-frame INGAME overlay that slides a 480×128 banner in from the
 * top of the screen, holds it, then slides it back up.  Used for the
 * "Dungeon Clear!" / "Mission Failed" / "Welcome Home" status messages.
 *
 * The texture is a 512×512 atlas (`bmp/dungeon_clear.tga`, engine
 * DAT_073cb8e0) split into three 128-wide vertical strips:
 *   slice 0 = (u=  0..128) — default
 *   slice 1 = (u=256..384) — variant 1
 *   slice 2 = (u=128..256) — variant 2
 * The slice picker `g_banner_slice` mirrors engine DAT_0438be98.
 *
 * The animation counter `g_banner_counter` (engine DAT_0438be94)
 * drives the Y coordinate via the engine's two-phase ramp:
 *
 *   y =  counter * 2 - 120         while counter <= 0x77 (119)
 *      → ramps from -120 (off-screen top) to a clamp at +96
 *   y =  96 - (counter * 3 - 360) * 2     while counter >  0x77
 *      → ramps from +96 back down, clamped at -48
 *
 * Both counter + slice are BSS-zero by default — the body short-
 * circuits without drawing.  When the dungeon-completion subsystem
 * lands and writes counter/slice via the engine setters, the banner
 * starts animating automatically.
 *
 * The function ALWAYS calls render_quad_state_setup() first, even
 * when the body short-circuits — that's a state-write side effect
 * the engine's render dispatch depends on.
 *
 * Caller: render_dispatch invokes this when state==INGAME or the
 * scene-transition counter is in (3..0xd).  See engine FUN_004547ab
 * L51237-51239.
 */
#ifndef OPENRECET_SCENE1_DUNGEON_CLEAR_BANNER_H
#define OPENRECET_SCENE1_DUNGEON_CLEAR_BANNER_H

#include <stdint.h>

/* ─── state (engine globals, pure-C accessors) ──────────────────────── */

/* Animation counter — drives Y position via the two-phase ramp.
 * Engine DAT_0438be94.  Default 0 → no draw. */
int32_t scene1_dungeon_clear_banner_get_counter(void);
void    scene1_dungeon_clear_banner_set_counter(int32_t v);

/* Slice picker (0 = default left strip, 1 = right, 2 = middle, else
 * → default).  Engine DAT_0438be98.  Default 0. */
int32_t scene1_dungeon_clear_banner_get_slice(void);
void    scene1_dungeon_clear_banner_set_slice(int32_t v);

/* Resets both globals to 0 (engine FUN_004360b6 et al — bank reset). */
void    scene1_dungeon_clear_banner_reset(void);

/* Pure-C Y-position formula — exposed for tests so the two-phase ramp
 * is verifiable without a D3D device.  Returns the engine clamps:
 *   counter <= 119 → max  +96
 *   counter >  119 → min  -48
 * Counter == 0 returns -120 (off-screen). */
float   scene1_dungeon_clear_banner_compute_y(int32_t counter);

/* Pure-C U0/U1 picker — returns the source-rect u-coords (in pixels,
 * texture-space) for the given slice. */
void    scene1_dungeon_clear_banner_compute_u(int32_t slice,
                                              float *u0, float *u1);

/* ─── render (Win32) ────────────────────────────────────────────────── */

#ifdef _WIN32

struct IDirect3DDevice8;

/* Engine FUN_0048fe43.  Fires render_quad_state_setup unconditionally
 * then early-returns when counter <= 0; otherwise binds the banner
 * texture + draws the animated 480×128 quad.  No-op when dev is NULL.
 *
 * The texture pointer is held in a private global, set by the loader
 * via scene1_dungeon_clear_banner_set_texture().  When NULL the body
 * still fires render_quad_state_setup() (engine parity) but skips the
 * SetTexture+quad emit. */
void scene1_dungeon_clear_banner_render(struct IDirect3DDevice8 *dev);

/* Texture setter — called by the HOUSE/DUNGEON preload chain when
 * `bmp/dungeon_clear.tga` loads.  Currently unwired (the asset isn't
 * in our scene1_preload_house list); the banner stays NULL-tex-skipped
 * until that loader lands. */
struct sprite_t;
void scene1_dungeon_clear_banner_set_texture(const struct sprite_t *spr);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_DUNGEON_CLEAR_BANNER_H */
