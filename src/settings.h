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
 *
 * Storage: all five config values are save-arena HEADER fields in retail
 * (DAT_056e5774 Sound, _5778 Music, _577c Voice, s16 _5782 Unread Text
 * Skip, _5784 Message Speed). Both Options pages (title FUN_0049a59e's
 * tail, pause FUN_0047fc44) edit them in place and FUN_004905a8 /
 * FUN_004902fe write / load them with save.dat. settings_bind_save_header
 * (main.c, right after the arena init) points these accessors and
 * audio_fade's at the header, so the port has one copy too. Unbound (host
 * tests) the two values are module-local with the defaults above.
 */
#ifndef OPENRECET_SETTINGS_H
#define OPENRECET_SETTINGS_H

#include <stdint.h>

/* Tri-state slider. Engine range [0, 2], default 1. */
#define SETTINGS_SLIDER3_MAX  2
#define SETTINGS_SLIDER3_DEFAULT  1

/* Boolean slider. Engine range [0, 1], default 0. */
#define SETTINGS_SLIDER4_MAX  1
#define SETTINGS_SLIDER4_DEFAULT  0

/* Back to the module-local store (any binding dropped) with both sliders
 * at their engine defaults. Idempotent. Test setup. */
void settings_reset(void);

/* Point the two sliders at external storage (NULL → the module-local one). */
void settings_bind(int32_t *slider3, int16_t *slider4);

/* Bind the whole config — these two sliders and audio_fade's three — to
 * the save-arena header fields (save_bank_header_bytes() + 0x04 / 0x08 /
 * 0x0c / 0x12 / 0x14). The values then are whatever the arena holds: the
 * init defaults, a loaded save.dat, an Options edit. */
void settings_bind_save_header(void);

/* Accessors with clamping. Out-of-range value-set is clamped to the
 * slider's [0, max] range. */
int  settings_get_slider3(void);
void settings_set_slider3(int value);

int  settings_get_slider4(void);
void settings_set_slider4(int value);

#endif /* OPENRECET_SETTINGS_H */
