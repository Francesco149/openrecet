/*
 * settings.h — non-audio settings sliders (rows 3 & 4 of the title
 * settings submenu).
 *
 * Engine globals:
 *   DAT_056e5784 — "slider3" — int in [0, 2]. Default 1. Read by
 *                  text-display / animation-speed sites (e.g.
 *                  FUN_004... line 4969 of all.c: rate-multiplier
 *                  table lookup at &DAT_0052970c, and FUN_004... line
 *                  67098: rate-multiplier table lookup at
 *                  &DAT_005c78e0). Most likely the "Text speed"
 *                  / "Game speed" tri-state.
 *   DAT_056e5782 — "slider4" — int in [0, 1]. Default 0. Read by
 *                  FUN_004... line 67189 as a boolean (`if (... != 0)`).
 *                  Likely a toggle (auto-advance, skip, similar).
 *
 * The actual semantics surface when the downstream consumers port —
 * see docs/findings/title-settings-submenu.md for the current
 * understanding. For now the state simply persists across the
 * settings submenu's adjust loop; future ports of the consumer
 * functions read the same globals via the accessors below.
 *
 * The audio sliders (BGM/SE-A/SE-B) live in audio_fade.{c,h} —
 * settings.{c,h} covers only the non-audio half.
 *
 * Engine defaults are seeded by FUN_004901c2 ("save-arena init"):
 *   DAT_056e5784 = 1; (no save-data init for DAT_056e5782 — BSS-zero)
 * which we mirror here. The eventual save-load port will overwrite
 * these once a save file is parsed.
 */
#ifndef OPENRECET_SETTINGS_H
#define OPENRECET_SETTINGS_H

/* Tri-state slider. Engine range [0, 2], default 1. */
#define SETTINGS_SLIDER3_MAX  2
#define SETTINGS_SLIDER3_DEFAULT  1

/* Boolean slider. Engine range [0, 1], default 0. */
#define SETTINGS_SLIDER4_MAX  1
#define SETTINGS_SLIDER4_DEFAULT  0

/* Seed both sliders to their engine defaults. Idempotent. Called
 * from settings init and from test setup. */
void settings_reset(void);

/* Accessors with clamping. Out-of-range value-set is clamped to the
 * slider's [0, max] range. */
int  settings_get_slider3(void);
void settings_set_slider3(int value);

int  settings_get_slider4(void);
void settings_set_slider4(int value);

#endif /* OPENRECET_SETTINGS_H */
