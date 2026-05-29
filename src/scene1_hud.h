/*
 * scene1_hud.h — scene-1 (INGAME) 2D HUD aggregator.
 *
 * Port of FUN_0040a765 (0x40a765, 7558 B) — the big 2D HUD / overlay
 * aggregator that the engine calls between the 3D walker
 * (scene1_render_camera_setup → scene1_render_meshes) and the overlay
 * dispatcher (scene1_render_overlay).  Engine render-frame order
 * (FUN_004547ab L70-73, the common INGAME path):
 *
 *     FUN_0045bbf9();   scene1_render_camera_setup
 *     FUN_0040a765();   ← THIS MODULE — 2D HUD aggregator
 *     FUN_00417504();   scene1_render_overlay
 *     FUN_0045404b();   scene1_render_fx_tail
 *
 * FUN_0040a765 groups into nine "passes" (see
 * docs/findings/scene1-walker.md).  This is the C7j chip — the entry
 * shell + the first three passes:
 *
 *   • Pass 1 — entry guards + 2D state preset + stamina/HP backdrop.
 *     Backdrop is gated `*DAT_068dd2f0 > 0` (DUNGEON only) → dormant
 *     in HOUSE.
 *   • Pass 2 — letterbox / cinema bars, height keyed off DAT_0438b1dc
 *     (BSS-zero) → dormant.
 *   • Pass 3 — status-screen takeover: when DAT_073dddb4 != 0, draw the
 *     status screen (FUN_004141c0, stubbed) and return early.  DAT_073dddb4
 *     is BSS-zero outside the Q-menu → dormant in normal HOUSE play.
 *
 * The remaining passes (4-9: item tooltip, HOUSE/DUNGEON sub-walkers,
 * speech bubbles, shop terminal, chr render, dialog/sub-menu panels,
 * day-counter flash) land as later chips (C7k..C7p).  Because every gate
 * past Pass 1 short-circuits on a BSS-zero global today, wiring this
 * shell produces no visible change in HOUSE — it lands the structure +
 * 2D state preset every later pass inherits.
 */

#ifndef OPENRECET_SCENE1_HUD_H
#define OPENRECET_SCENE1_HUD_H

#include <stdint.h>

/* ─── pure helpers (host-testable; no D3D device required) ─────────── */

/* Pass 2 letterbox-bar height (DAT_0438b1dc) with the engine's
 * ±0.1 dead-zone clamp applied: |h| <= 0.1 → 0.  The Pass 2 bars draw
 * only when the result is > 0. */
float scene1_hud_letterbox_height(void);

/* Setter for the letterbox height (engine DAT_0438b1dc writer is the
 * cinema-bars animator, not yet ported). */
void scene1_hud_set_letterbox_height(float h);

/* Pass 3 status-screen-open flag (DAT_073dddb4): when nonzero the HUD
 * renders the status screen and returns before any later pass. */
int  scene1_hud_status_screen_open(void);
void scene1_hud_set_status_screen_open(int open);

/* Pass 1 stamina/HP backdrop diffuse colour.  `pred` is the result of
 * the engine predicate FUN_0043647f(0x10): nonzero → full intensity
 * (uVar7 = 0xff), zero → dimmed (uVar7 = 200).  Reproduces the engine
 * bit expression `((uVar7 | 0x3700) << 8 | uVar7) << 8 | uVar7`. */
uint32_t scene1_hud_pass1_backdrop_color(int pred);

/* Pass 1 backdrop active predicate: INGAME (scene_mode == 1) AND a
 * DUNGEON stage (stage_type > 0) AND (FUN_0043647f(0x10) || DAT_056db104).
 * `pred` is the FUN_0043647f(0x10) result. */
int scene1_hud_pass1_backdrop_active(int scene_mode, int stage_type, int pred);

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

/* Render the scene-1 2D HUD for the current INGAME frame.  Call after
 * scene1_render_camera_setup and before scene1_render_overlay. */
void scene1_hud_render(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_HUD_H */
