/*
 * tables_oder.c — `data/oder.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #8 in
 * docs/decompiled/by-address/475270.c (outer dispatch L1378..L1421,
 * inner CSV loop L1813..L1931 via `goto LAB_00477ffe`). Identifies the
 * file via `s_data_oder_txt_005caf7c` (size) and `…_005caf8c` (read);
 * the two .data interned copies of "data/oder.txt" are identical, so
 * there is no path-mismatch quirk to mirror.
 *
 * Engine record stride is 0x4c bytes at base `&DAT_06a5db98`. Per
 * record:
 *   +0x00  name_singular[32]    — written in-place at column position
 *   +0x20  name_plural[32]      — written sequentially after first ','
 *   +0x40  attr_mask (u32)      — FUN_0049e9a7 result over field 3
 *   +0x44  attr_index (i32)     — item-name lookup index (-1 = unset)
 *   +0x48  level_minus_1 (i32)  — pending LV value minus 1
 *
 * Engine inner-loop quirks faithfully reproduced:
 *   - 100-char per-line cap (`local_14 == 0x64` at L1842)
 *   - tab bytes are skipped without advancing any field position
 *   - phase 0 ("first field") writes at COLUMN position in the record,
 *     not sequentially — so a comma at column N terminates with the
 *     preceding N bytes intact and a NUL at column N
 *   - phase 1 ("second field") writes sequentially starting at +0x20
 *   - phase 2 ("attribute") accumulates in a separate scratch buffer
 *     that is then hashed via oder_attr_hash and never stored verbatim
 *
 * Engine behaviors NOT reproduced (intentional divergence):
 *   - On `attr_mask == 0` the engine linearly searches the
 *     `&DAT_0963e5f8` item-name table (populated by item.txt, block
 *     #3) and falls back to MessageBoxA "属性不明な登録" on no match.
 *     `item.txt` isn't ported yet, so the port stores attr_index = -1
 *     unconditionally for the mask==0 case and suppresses the
 *     MessageBoxA. See docs/formats/data-text.md for the follow-up plan.
 *   - An over-32-char first field would spill into the +0x20 region in
 *     the engine; the port truncates safely at 31 bytes (the trailing
 *     NUL slot). Vendor data fits in ≤16 chars per field, so this is
 *     dormant in practice.
 */

#include "tables_oder.h"

#include <stdlib.h>
#include <string.h>

struct oder_table g_oder;

/* The 16 four-byte SJIS attribute tags at `&DAT_005fd7fc` (stride 8
 * bytes: 4 chars + 4 zero-pad). Extracted by direct hex-dump of the
 * unpacked binary — see docs/formats/data-text.md for the readable
 * romaji / kanji names. Index N → bit (1u << N) in the returned mask.
 */
static const unsigned char ODER_ATTRS[ODER_ATTR_COUNT][4] = {
    /* 0x0001 */ { 0x95, 0x90, 0x8a, 0xed },  /* 武器  bukI  - weapon          */
    /* 0x0002 */ { 0x96, 0x68, 0x8b, 0xef },  /* 防具  bougu - armor           */
    /* 0x0004 */ { 0x92, 0xb2, 0x93, 0x78 },  /* 調度  choudo - decor          */
    /* 0x0008 */ { 0x95, 0x9e, 0x8f, 0xfc },  /* 服飾  fukushoku - clothing    */
    /* 0x0010 */ { 0x83, 0x41, 0x83, 0x4e },  /* アク  aku - accessory         */
    /* 0x0020 */ { 0x8b, 0x4d, 0x8b, 0xe0 },  /* 貴金  kikin - precious metal  */
    /* 0x0040 */ { 0x8b, 0xe0, 0x91, 0xae },  /* 金属  kinzoku - metal         */
    /* 0x0080 */ { 0x97, 0x5b, 0x94, 0xd1 },  /* 夕飯  yuuhan - dinner         */
    /* 0x0100 */ { 0x8a, 0xc3, 0x82, 0xa2 },  /* 甘い  amai - sweet            */
    /* 0x0200 */ { 0x94, 0x68, 0x8e, 0xe8 },  /* 派手  hade - fancy            */
    /* 0x0400 */ { 0x92, 0x6e, 0x96, 0xa1 },  /* 地味  jimi - plain            */
    /* 0x0800 */ { 0x92, 0xbf, 0x95, 0x69 },  /* 珍品  chinpin - rare          */
    /* 0x1000 */ { 0x96, 0x68, 0x8a, 0xa6 },  /* 防寒  boukan - cold-weather   */
    /* 0x2000 */ { 0x90, 0x48, 0x95, 0x69 },  /* 食品  shokuhin - food         */
    /* 0x4000 */ { 0x90, 0xb9, 0x91, 0xae },  /* 聖属  seizoku - holy          */
    /* 0x8000 */ { 0x96, 0x82, 0x91, 0xae },  /* 魔属  mazoku - sinister       */
};

uint32_t oder_attr_hash(const char *s)
{
    /* Engine sequence at FUN_0049e9a7: tests each tag in index order
     * and overwrites the result with the latest match's bit. So if
     * two tags share a 4-byte prefix the higher-index one wins —
     * harmless in practice since the 16 tags are byte-disjoint. We
     * mirror "latest match wins" anyway. The hash on a string
     * shorter than 4 bytes would memcmp past the end in the engine
     * (reading whatever .bss bytes follow `s`); for the port we
     * short-circuit on strlen < 4 and return 0, since the engine's
     * over-read is undefined behavior we deliberately avoid. */
    if (s == NULL) return 0;
    if (strlen(s) < 4) return 0;

    uint32_t mask = 0;
    for (int i = 0; i < ODER_ATTR_COUNT; i++) {
        if (memcmp(s, ODER_ATTRS[i], 4) == 0) {
            mask = 1u << i;
        }
    }
    return mask;
}

/* Inner data-row parser. `line` excludes the \r/\n terminator;
 * `llen` is its length. Writes one entry into `out`. */
static void parse_data_line(const char *line, size_t llen,
                            struct oder_entry *out, int pending_level,
                            oder_resolve_fn resolve, void *user)
{
    memset(out, 0, sizeof *out);
    out->attr_index = -1;
    out->level_minus_1 = pending_level - 1;

    /* Engine inner loop: phase 0 = first field (in-place column
     * writes into the record), phase 1 = second field (sequential
     * starting at +0x20), phase 2 = attribute name (scratch). */
    int   phase   = 0;
    int   seq_idx = 0;
    char  attr[ODER_FIELD_SIZE];
    int   attr_len = 0;

    /* Cap at min(llen, 100) — mirrors `local_14 == 0x64` at L1842. */
    size_t cap = llen < 100 ? llen : 100;
    for (size_t i = 0; i < cap; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c == '\0' || c == '\r' || c == '\n') break;
        if (c == '\t') continue;

        if (phase == 0) {
            /* Column-position write. Truncate at the per-field cap
             * so an oversized first field can't spill into the
             * plural slot. */
            if (i < (size_t)(ODER_FIELD_SIZE - 1)) {
                out->name_singular[i] = (c == ',') ? '\0' : (char)c;
            }
            if (c == ',') {
                phase = 1;
                /* seq_idx unchanged — engine doesn't reset it here
                 * either, but it's still 0 from entry. */
            }
        } else if (phase == 1) {
            if (seq_idx < ODER_FIELD_SIZE - 1) {
                out->name_plural[seq_idx] = (c == ',') ? '\0' : (char)c;
            }
            if (c == ',') {
                phase = 2;
                seq_idx = 0;
            } else {
                seq_idx++;
            }
        } else {
            /* Phase 2 — accumulate attribute into scratch. */
            if (attr_len < ODER_FIELD_SIZE - 1) {
                attr[attr_len++] = (char)c;
            }
        }
    }
    attr[attr_len] = '\0';

    out->attr_mask = oder_attr_hash(attr);
    /* When the token isn't an attribute tag (mask==0) the engine resolves
     * it as an item-CATEGORY name (&DAT_0963e5f8); mirror that via the
     * injected resolver.  NULL resolver ⇒ attr_index stays -1. */
    if (out->attr_mask == 0 && resolve != NULL) {
        int32_t cat = resolve(attr, user);
        if (cat >= 0) out->attr_index = cat;
    }
}

void tables_parse_oder(const unsigned char *data, size_t size,
                       struct oder_table *out)
{
    tables_parse_oder_resolved(data, size, out, NULL, NULL);
}

void tables_parse_oder_resolved(const unsigned char *data, size_t size,
                                struct oder_table *out,
                                oder_resolve_fn resolve, void *user)
{
    memset(out, 0, sizeof *out);

    /* `pending_level` carries the last `LV:N` value across data
     * lines. Engine leaves the stack slot uninitialized; we start at
     * 0 so the first data line (if any precedes an LV: line) gets
     * level_minus_1 = -1. Vendor file always begins with an LV: line. */
    int pending_level = 0;

    /* Line buffer. Engine reservation is `local_27c[0x20..]` —
     * effectively a few hundred bytes; the inner loop's 100-char
     * cap means we never use more than ~104 in practice. 256 leaves
     * plenty of safety margin. */
    char line[256];

    size_t pos = 0;
    while (pos < size) {
        if (data[pos] == '\0') break;  /* engine's outer EOF check */

        size_t llen = 0;
        while (pos < size
               && data[pos] != '\0'
               && data[pos] != '\r'
               && data[pos] != '\n'
               && llen + 1 < sizeof line) {
            line[llen++] = (char)data[pos++];
        }
        line[llen] = '\0';

        /* Consume one terminator byte; a \r\n pair runs through as
         * two iterations (the second sees an empty line and skips). */
        if (pos < size && (data[pos] == '\r' || data[pos] == '\n')) pos++;

        /* Comment / blank lines: engine checks line[0] for '\r',
         * '\n', or '/'. Our buffer excludes \r/\n, so an empty line
         * collapses to llen == 0. */
        if (llen == 0 || line[0] == '/') continue;

        /* "LV:" 3-byte dispatch (`&DAT_005caf9c`). The do-while at
         * L1406-1417 walks 3 bytes; matched-all → atoi the rest. */
        if (llen >= 3
            && line[0] == 'L' && line[1] == 'V' && line[2] == ':') {
            pending_level = atoi(line + 3);
            continue;
        }

        /* Data line — append a new record. Defensively skip if we'd
         * exceed ODER_MAX_ENTRIES (engine has no such guard). */
        if (out->count >= ODER_MAX_ENTRIES) continue;
        parse_data_line(line, llen, &out->entries[out->count],
                        pending_level, resolve, user);
        out->count++;
    }
}
