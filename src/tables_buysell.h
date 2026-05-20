/*
 * tables_buysell.h — parser for `data/buysell.txt` (block #7 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `buysell.txt` is a single-customer **debug override** file. When
 * `ok:` is uncommented at the top, the game forces the next encounter
 * with customer `客番号` (kyaku_number) to use kind `種類` (sell/buy/
 * about) and to pick fixed dialogue branches via the `msg%02d` /
 * `rmsg%02d` arrays — see docs/formats/data-text.md.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_BUYSELL_H
#define OPENRECET_TABLES_BUYSELL_H

#include <stddef.h>

/* Engine loop range: `puVar12 != &DAT_073b1a68` starting at
 * `&DAT_073b1a18` — 0x50 bytes / 20 ints per array. */
#define BUYSELL_MSG_COUNT 20

/*
 * Mirrors the engine's debug-override globals. In-memory layout matches
 * the engine exactly: the `rmsg` array sits at the LOWER address
 * (0x073b1a18) and `msg` at the higher one (0x073b1a68) — a minor
 * engine oddity worth preserving so cross-checks against the original
 * are bit-comparable when we ever look at runtime memory.
 *
 * The three scalars at 0x073dddb8/bc/c0 are *not* contiguous with the
 * arrays — they live in a separate .bss block — so we group them here
 * for ergonomic reasons, not byte-layout fidelity.
 */
struct buysell_config {
    int debug_mode;       /* 0x073dddb8 — set to 1 if `ok:` line present  */
    int kyaku_number;     /* 0x073dddbc — atoi of `客番号:` value          */
    int kind;             /* 0x073dddc0 — atoi of `種類:` value (0/1/2)    */
    int rmsg[BUYSELL_MSG_COUNT];  /* 0x073b1a18 — `rmsg%02d:` values      */
    int msg [BUYSELL_MSG_COUNT];  /* 0x073b1a68 — `msg%02d:` values       */
};

/*
 * Engine-global instance, populated by tables_parse_buysell() from
 * the dispatcher in src/tables.c. Tests get their own struct via the
 * out-parameter form below, so g_buysell stays untouched in unit
 * tests.
 */
extern struct buysell_config g_buysell;

/*
 * Parse a buysell.txt buffer into `out`. Zero-inits `out` first
 * (engine does the same). `data` is read as bytes; it does not need
 * to be null-terminated since `size` is authoritative.
 *
 * The engine reads char-by-char and terminates lines on \r, \n, or
 * \0 — we match that, including the early-exit on embedded \0.
 */
void tables_parse_buysell(const unsigned char *data, size_t size,
                          struct buysell_config *out);

#endif /* OPENRECET_TABLES_BUYSELL_H */
