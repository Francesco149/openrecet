/*
 * settings_panel.h — the shared config-panel render (FUN_0049c050).
 *
 * The "Options" panel: a dungeonbord.tga backdrop + a column of
 * setting rows (Music · Sound · Voice volumes as 0..8 numbers;
 * Message Speed as SLOW/MED/FAST; Unread Text Skip as OFF/ON) with
 * the selected row drawn yellow (0xff7f7f00) and the rest grey
 * (0xff7f7f7f), all under COLOROP=MODULATE2X so the grey/yellow
 * diffuses brighten against the backdrop.  An optional "Saving"
 * overlay (savewindow.tga + the word) draws during a dirty-exit save.
 *
 * The engine has ONE render shared by two callers (matching the
 * save_picker precedent):
 *   - the in-game PAUSE Options submenu  (FUN_0048150c → here),
 *     scene_state == 9 → 5 rows
 *   - the TITLE settings submenu         (FUN_0049c644 tail → here),
 *     scene_state == 0 → 6 rows (adds a centered "Clear Save Data")
 *
 * The 5-vs-6 row split is decided HERE from g_scene_state (engine
 * FUN_0049c050 L40 `if (DAT_0438b1c0 == 0) row_count = 6`), so both
 * callers pass the same args and get the right layout.
 *
 * The slider VALUES are read live from the port's config model:
 *   Music/Sound/Voice → audio_fade_get_slider(BGM/SE_A/SE_B)
 *   Message Speed     → settings_get_slider3()  (0..2)
 *   Unread Text Skip  → settings_get_slider4()  (0..1)
 *
 * Engine: FUN_0049c050 @ 0x49c050 (1001 B), transcribed 1:1 (the
 * decompile dropped the .rdata label/format strings + the register-
 * built diffuse args — recovered from the PE .data/.rdata).
 */
#ifndef OPENRECET_SETTINGS_PANEL_H
#define OPENRECET_SETTINGS_PANEL_H

#ifdef _WIN32

#include "sprite.h"

struct IDirect3DDevice8;

/* Render the config panel.
 *
 *   dev          — the d3d device.
 *   dungeonbord  — bmp/dungeonbord.tga (the 320×360 panel backdrop is
 *                  src (0,0)-(320,360) of it).  Required.
 *   savewindow   — bmp/savewindow.tga for the "Saving" overlay, or NULL
 *                  to skip it (the title scene doesn't load it).
 *   slide_x      — the slide-in x origin (engine param_1; caller-derived,
 *                  rests at 0 when fully open).
 *   base_y       — the panel y origin (engine param_2; always 48.0).
 *   cursor_row   — the highlighted row 0..5 (engine param_3).
 *   saving_flag  — non-zero ⇒ draw the "Saving" overlay (engine param_4,
 *                  the dirty-exit save state).
 */
void settings_panel_render(struct IDirect3DDevice8 *dev,
                           const sprite_t *dungeonbord,
                           const sprite_t *savewindow,
                           float slide_x, float base_y,
                           int cursor_row, int saving_flag);

#endif /* _WIN32 */

#endif /* OPENRECET_SETTINGS_PANEL_H */
