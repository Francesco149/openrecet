#ifndef SCENE1_FPS_H
#define SCENE1_FPS_H

#include <stdint.h>

/*
 * scene1_fps.{c,h} — the bottom-right "Fps NN" debug overlay.
 *
 * Two engine pieces:
 *   - FUN_004523e6 (the renderer) draws a "Fps" label sprite + up to two
 *     digit glyphs from bmp/fps2.tga (g_sysassets.fps2_tga, DAT_073d9fe0).
 *   - The frame-rate value it reads (DAT_073de63c) is computed in
 *     FUN_004547ab's tail (decomp all.c L51311-51324): a once-per-second
 *     average of frames-rendered over the elapsed millisecond window.
 *
 * The engine gates the renderer on `DAT_0438cce0 == 0` (recet.ini
 * [setup] dispfps, default 0 → shown).  The caller (render_dispatch)
 * owns that gate via g_ini.dispfps.
 */

/* Per-frame frame-rate update (FUN_004547ab L51311-51324, DAT_073de63c
 * branch).  Call once per rendered frame with the current virtual-clock
 * time in ms (tick_now_ms) — the virtual clock keeps the value
 * deterministic under --turbo so capture goldens stay reproducible,
 * while in realtime it tracks wall time exactly like the engine's
 * timeGetTime() source. */
void scene1_fps_tick(uint32_t now_ms);

/* The current displayed FPS value (DAT_073de63c). */
int  scene1_fps_value(void);

/* Reset the counter state (call on scene (re)load if desired). */
void scene1_fps_reset(void);

#ifdef _WIN32
struct IDirect3DDevice8;
/* Render the overlay (FUN_004523e6).  Caller gates on dispfps == 0. */
void scene1_fps_render(struct IDirect3DDevice8 *dev);
#endif

#endif /* SCENE1_FPS_H */
