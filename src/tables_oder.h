/*
 * tables_oder.h — parser for `data/oder.txt` (block #8 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `oder.txt` defines the *order requests* that customers can ask Recet
 * to fulfill: each entry pairs a difficulty tier (`LV:N`) with a
 * singular phrase, a plural phrase, and an attribute. When a customer
 * walks in, the engine picks an order whose level matches the current
 * game difficulty and shows the singular/plural strings; the
 * attribute (a 4-byte SJIS tag like `武器`/`防具` or a fall-through
 * English item name) is used to match shop inventory items against
 * the order.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_ODER_H
#define OPENRECET_TABLES_ODER_H

#include <stddef.h>
#include <stdint.h>

/* Per-record field width. The engine's record stride is 0x4c bytes
 * with the singular phrase at +0x00..+0x1F and the plural phrase at
 * +0x20..+0x3F — i.e. 32 bytes each (including trailing NUL). The
 * inner-loop's per-line 0x64-byte cap means a single oversized field
 * cannot extend past the record boundary in practice; we keep the
 * width and add a clean truncation guard to match anyway. */
#define ODER_FIELD_SIZE 32

/* Upper bound on entries. The engine has no explicit cap (the outer
 * loop runs to EOF, blindly indexing &DAT_06a5db98 + count * 0x4c),
 * so this is a defensive ceiling for the port. Vendor `oder.txt`
 * ships ~24 entries, leaving plenty of headroom. */
#define ODER_MAX_ENTRIES 256

/* Number of 4-byte SJIS attribute tags FUN_0049e9a7 matches against.
 * Engine returns a bitmask, one bit per attribute; bit N (1u<<N) is
 * set when the field's first 4 bytes match attribute index N. */
#define ODER_ATTR_COUNT 16

/* Mirrors `&DAT_06a5db98 + count * 0x4c` — one record per data line. */
struct oder_entry {
    char     name_singular[ODER_FIELD_SIZE]; /* +0x00 — "a treasure"        */
    char     name_plural  [ODER_FIELD_SIZE]; /* +0x20 — "treasures"         */
    uint32_t attr_mask;                      /* +0x40 — FUN_0049e9a7 result */
    int32_t  attr_index;                     /* +0x44 — item-name lookup    */
    int32_t  level_minus_1;                  /* +0x48 — pending_lv - 1      */
};

/*
 * `attr_index` is the result of the engine's linear search through
 * the item-name table at `&DAT_0963e5f8` when `attr_mask == 0` (i.e.
 * the attribute field didn't match any of the 16 SJIS tags). That
 * table is populated by `item.txt` (block #3) which hasn't been ported
 * yet, so the port currently stores -1 here unconditionally for the
 * mask==0 case. When `item.txt` lands, the parser should be extended
 * to accept a name-lookup callback. The engine's MessageBoxA on
 * not-found is also intentionally suppressed in the port.
 */

struct oder_table {
    struct oder_entry entries[ODER_MAX_ENTRIES];
    int count;
};

/* Engine-global instance, populated from src/tables.c. Tests get
 * their own struct via the out-parameter form, so g_oder stays
 * untouched in unit tests. */
extern struct oder_table g_oder;

/*
 * Parse an oder.txt buffer into `out`. Zero-inits `out` first.
 * `data` is read as bytes; it does not need to be null-terminated
 * since `size` is authoritative.
 *
 * Line shape:
 *   /…           — comment (engine: line[0] == '/' or '\r' or '\n')
 *   LV:N         — sets pending level for subsequent data lines
 *   f1,f2,f3     — record; uses the most-recently-seen LV value (minus 1)
 *
 * The inner loop caps each data line at 100 chars (engine's
 * `local_14 == 0x64` break at L1842 of FUN_00475270). Tabs are
 * skipped without advancing the field position. The first field is
 * written in-place at column position (so a comma at column N
 * effectively terminates with N bytes preceding); the second and
 * third are written sequentially.
 */
void tables_parse_oder(const unsigned char *data, size_t size,
                       struct oder_table *out);

/*
 * Compute the 4-byte SJIS attribute bitmask. Mirrors FUN_0049e9a7 —
 * memcmps the first 4 bytes of `s` against 16 .data SJIS tags and
 * returns the bit (1u << N) of the last matching one, or 0 if none
 * match. The 4-byte tags are listed in docs/formats/data-text.md.
 */
uint32_t oder_attr_hash(const char *s);

#endif /* OPENRECET_TABLES_ODER_H */
