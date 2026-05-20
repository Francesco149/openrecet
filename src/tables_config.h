/*
 * tables_config.h — parser for `data/config.idx` (block #2 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `config.idx` configures the engine's font and text-rendering
 * subsystem: font face name, edge/outline width and falloff,
 * whether to skip kanji generation, whether to use the "effect"
 * rendering mode. Most of the file's keys are flags; only `edgewi`
 * and `edgedel` carry numeric values.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_CONFIG_H
#define OPENRECET_TABLES_CONFIG_H

#include <stddef.h>

/* Engine's font-name buffer is at &DAT_073de168 sized 0x40 dwords =
 * 256 bytes. We keep the same capacity so over-long names truncate
 * the same way the engine would. */
#define CONFIG_FONT_NAME_CAP 256

/*
 * Mirrors the engine's config.idx globals. Each scalar maps to a
 * specific .bss address — see the table in
 * docs/formats/data-text.md for the engine-side names.
 *
 * `font_set` is true if a `font:` line was seen; the engine uses it
 * as a "regenerate font cache" trigger downstream.
 */
struct config_idx {
    int  kanjioff;     /* 0x005cbc70 — set to 1 if `kanjioff:` present  */
    int  edgewi;       /* 0x005cbc74 — atoi of `edgewi:` value          */
    int  edgedel;      /* 0x005cbc78 — atoi of `edgedel:` value         */
    int  effectmode;   /* 0x073dddb4 — set to 1 if `effectmode:` present*/
    int  font_set;     /* 0x073dfd00 — set to 1 if `font:` was seen     */
    char font_name[CONFIG_FONT_NAME_CAP]; /* 0x073de168 — Shift-JIS, NUL-terminated */
};

extern struct config_idx g_config;

/*
 * Parse a config.idx buffer into `out`. Zero-inits `out` first
 * (engine clears `effectmode` explicitly at line 334 before parsing,
 * but the other globals are .bss so they happen to start at 0 too —
 * our memset matches that net effect).
 *
 * Same line-by-line shape as tables_parse_buysell, but with config.idx's
 * five keys. Lines starting with '/', '\r', or '\n' are skipped.
 */
void tables_parse_config(const unsigned char *data, size_t size,
                         struct config_idx *out);

#endif /* OPENRECET_TABLES_CONFIG_H */
