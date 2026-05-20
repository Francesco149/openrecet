/*
 * tables_snews.c — `data/snews.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #12 in
 * docs/decompiled/by-address/475270.c (L2238..L2401). Identifies the
 * file via `s_data_snews_txt_005cb148` (size) and
 * `s_data_snews_txt_005cb158` (read); both interned copies hold the
 * same spelling "data/snews.txt", so there is no path-mismatch quirk
 * (unlike config.idx).
 *
 * The block populates two unrelated globals:
 *
 *   1. A 64-entry name table at `&DAT_073d8ee0` (stride 0x44) — every
 *      `NNN:<text>` line writes `<text>` into `names[atoi(NNN)]`.
 *      `_DAT_073dddc4` counts populated names (engine increments
 *      unconditionally; port matches).
 *
 *   2. A 10×30 grid of floor-range sections at `&DAT_073b2108` (stride
 *      0xa8) — six are reachable via the SJIS dungeon keys
 *      `ダンジョン1`..`ダンジョン6`, the other four stay all-empty.
 *      Each section is `(floor_start, floor_end)` plus up to 20 entry
 *      slots `(news_id, weight)`.
 *
 * Engine quirks faithfully reproduced:
 *
 *   - Dungeon-key match sets `local_20 = N` and `local_14 = 0`, but
 *     does NOT touch `local_c` (the current-section write pointer).
 *     The first `f:N-M` line in each new dungeon therefore overwrites
 *     the LAST section of the previous dungeon with its (N,M) before
 *     advancing — a benign off-by-one in the engine's writer that
 *     corrupts the floor info of the last section of every dungeon
 *     except the last. See docs/findings/engine-quirks.md and
 *     docs/formats/data-text.md for the trail.
 *
 *   - `f:` handler writes `(floor_start, floor_end)` to OLD `local_c`
 *     and THEN advances `local_c = base + (local_14 + local_20*30) *
 *     0xa8` using the PRE-INCREMENT `local_14` — so the first f: line
 *     of dungeon 0 writes to section [0][0] AND advances to [0][0]
 *     (no-op), but subsequent f: lines within the same dungeon advance
 *     by one section each.
 *
 *   - Name-table line dispatch is `pcVar16 = line + 3` (skip a fixed
 *     3-byte index prefix), then start reading at `pcVar16[1]` — i.e.
 *     the engine skips a 1-byte separator at line[3] (typically ':').
 *     Char-copy loop iterates 0x40 times max and writes a NUL at
 *     `name[k+1]` on post-write EOL, or `name[k]` on top-of-loop NUL.
 *     If the loop runs all 0x40 iterations, no NUL is written and the
 *     next entry's `active` byte gets clobbered (1-byte overrun bug).
 *     Port caps at SNEWS_NAME_LEN-1 chars and always writes a NUL.
 *
 *   - "NON" without a trailing comma would, in the engine, take the
 *     name-table path with `iVar1 = -2` and write a string at
 *     `names[-2]` (= `&DAT_073d8ee0 - 0x88`, somewhere in the snews
 *     globals' .bss neighbourhood). Vendor data never triggers this;
 *     port guards with `id >= 0 && id < SNEWS_NAME_COUNT`.
 *
 *   - Comment skip: line[0] ∈ { '/', '\r', '\n' } — same convention as
 *     every other tables_load_all block.
 *
 *   - Unknown lines (not a dungeon key, "f:", "NON", or starting with a
 *     digit): engine calls `MessageBoxA(... "不明なニュース", ...)`.
 *     Port silently skips.
 *
 * Safety divergences (documented, not present in the engine):
 *
 *   - Entry slot overflow: engine `local_18` increments past 20 and
 *     writes OOB into the next section. Port silently drops entries
 *     beyond SNEWS_ENTRY_COUNT (vendor max is 13 entries per section).
 *
 *   - Section overflow: engine `local_14` can advance past 30 within a
 *     single dungeon, walking into the next dungeon's slot. Port
 *     silently drops sections beyond SNEWS_SECTION_COUNT.
 *
 *   - Out-of-range `NNN:` ID >= 64: engine writes OOB past the name
 *     table. Port skips. Vendor IDs are 1..25 (well within range).
 *
 *   - Malformed `f:` line with no '-' separator: engine scans past the
 *     in-buffer NUL into the rest of the parse buffer hunting for '-',
 *     then atoi's whatever follows (could be anywhere in the file!).
 *     Port stops at the in-buffer NUL via strchr, then skips the line.
 *     Vendor data has '-' on every f: line.
 */

#include "tables_snews.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

snews_state_t g_snews;

/* The six SJIS-encoded dungeon keys `ダンジョン1`..`ダンジョン6`.
 * Each is 12 bytes (6 half-width SJIS double-byte codepoints).
 * The bytes are kept as octal/hex escapes so this source file can
 * stay pure ASCII regardless of editor or locale settings. */
static const unsigned char k_dungeon_keys[SNEWS_DUNGEON_KEY_COUNT][12] = {
    /* ダ\x83_  ン\x83\x93  ジ\x83W  ョ\x83\x87  ン\x83\x93  N\x82PN */
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x50 },
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x51 },
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x52 },
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x53 },
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x54 },
    { 0x83,0x5f, 0x83,0x93, 0x83,0x57, 0x83,0x87, 0x83,0x93, 0x82,0x55 },
};

/* Initialise every section to (-1, -1, entries all-empty). Engine does
 * this via two nested loops at L2243..L2267 — the outer puVar12 stride
 * is 0xa8 (one section), and the inner writes `0xffffffff` to
 * floor_start/floor_end + 20 entry id slots (entry weight slots are
 * left uninitialised by the engine; the port memset zeroes them). */
static void init_sections(snews_state_t *out)
{
    for (int d = 0; d < SNEWS_DUNGEON_SLOT_COUNT; d++) {
        for (int s = 0; s < SNEWS_SECTION_COUNT; s++) {
            out->sections[d][s].floor_start = SNEWS_EMPTY;
            out->sections[d][s].floor_end   = SNEWS_EMPTY;
            for (int e = 0; e < SNEWS_ENTRY_COUNT; e++) {
                out->sections[d][s].entries[e].id = SNEWS_EMPTY;
                /* weight stays 0 from caller memset; engine leaves it
                 * uninit — but only reads weight when id != -1, so the
                 * difference is unobservable. */
            }
        }
    }
}

/* Look up a flat section index across the contiguous section array,
 * matching the engine's `&DAT_073b2108 + idx * 0xa8` pointer arithmetic.
 * Returns NULL if idx is out of range (port safety; engine has no
 * such check). */
static snews_section_t *section_at(snews_state_t *out, int idx)
{
    int max = SNEWS_DUNGEON_SLOT_COUNT * SNEWS_SECTION_COUNT;
    if (idx < 0 || idx >= max) return NULL;
    /* The 2D array is laid out as 10 × 30 sections contiguously, so
     * indexing via the flat base is identical to sections[d][s] with
     * d = idx/30 and s = idx%30. */
    return &out->sections[idx / SNEWS_SECTION_COUNT][idx % SNEWS_SECTION_COUNT];
}

/* Copy a name into names[id]. Mirrors the inner char-copy loop at
 * L2342..L2353. Engine reads from `pcVar16[1]` (i.e., skips the 1-byte
 * separator at line[3]) and writes up to 0x40 chars. The engine has
 * two terminator paths and one no-terminator path; see file header. */
static void copy_name(snews_state_t *out, int id, const char *trailer)
{
    out->names[id].active = 1;
    out->name_count++;

    /* Engine: cVar11 = pcVar16[1]; pCVar15 = pcVar16 + 1.
     * I.e., the leading separator at trailer[0] (typically ':') is
     * skipped and the name proper starts at trailer[1]. */
    int k = 0;
    const char *p = trailer + 1;
    char c = (trailer[0] == '\0') ? '\0' : *p;

    while (k < SNEWS_NAME_LEN - 1) {
        if (c == '\0') {
            /* Engine: MessageBoxA, then NUL at name[k]. */
            break;
        }
        out->names[id].name[k] = c;
        p++;
        c = *p;
        if (c == '\0' || c == '\r' || c == '\n') {
            /* Engine: NUL at name[k+1]. We pre-advance k so the
             * post-loop terminator lands at the same byte. */
            k++;
            break;
        }
        k++;
    }
    out->names[id].name[k] = '\0';
}

/* Append a (id, weight) entry to section *sec, mirroring the engine's
 * stride-8 sub-record writes at L2321..L2329. `*entries_in_current` is
 * advanced regardless of cap (engine: `local_18` unconditional
 * increment); port silently drops the write if the section is full. */
static void push_entry(snews_section_t *sec, int id, int weight,
                       int *entries_in_current)
{
    int k = *entries_in_current;
    if (sec != NULL && k >= 0 && k < SNEWS_ENTRY_COUNT) {
        sec->entries[k].id     = id;
        sec->entries[k].weight = weight;
    }
    (*entries_in_current)++;
}

void tables_parse_snews(const unsigned char *data, size_t size,
                        snews_state_t *out)
{
    memset(out, 0, sizeof(*out));
    init_sections(out);

    /* Parser state mirrors the engine's locals at L2243..L2271. */
    int current_dungeon     = 0;  /* local_20 */
    int sections_in_current = 0;  /* local_14 */
    int entries_in_current  = 0;  /* local_18 */
    int section_idx         = 0;  /* local_c = &DAT_073b2108 + idx*0xa8 */

    char line[512];
    size_t pos = 0;
    while (pos < size) {
        if (data[pos] == '\0') break;

        size_t llen = 0;
        while (pos < size
               && data[pos] != '\0'
               && data[pos] != '\r'
               && data[pos] != '\n'
               && llen + 1 < sizeof line) {
            line[llen++] = (char)data[pos++];
        }
        line[llen] = '\0';

        if (pos < size && (data[pos] == '\r' || data[pos] == '\n')) pos++;

        /* Engine: the line-collect loop exits on '\r'/'\n'/'\0' and the
         * outer dispatcher restarts on lines where the FIRST byte is
         * '\r', '\n', or '/'. With our buffer excluding the EOL bytes,
         * a blank line is llen == 0. */
        if (llen == 0 || line[0] == '/') continue;

        /* Dungeon-key dispatch — 12-byte SJIS prefix.  Engine tests the
         * keys in order ダンジョン1..6 (L2293..L2303). */
        int matched_dungeon = -1;
        if (llen >= 12) {
            for (int d = 0; d < SNEWS_DUNGEON_KEY_COUNT; d++) {
                if (memcmp(line, k_dungeon_keys[d], 12) == 0) {
                    matched_dungeon = d;
                    break;
                }
            }
        }
        if (matched_dungeon >= 0) {
            current_dungeon = matched_dungeon;
            sections_in_current = 0;
            /* Engine quirk: local_c and local_18 NOT reset here.
             * The first subsequent `f:N-M` line therefore overwrites
             * the last-written section of the previous dungeon. */
            continue;
        }

        /* `f:N-M` — section header. Engine: 2-byte prefix match
         * against "f:" at L2305. */
        if (llen >= 2 && line[0] == 'f' && line[1] == ':') {
            const char *p = line + 2;
            int floor_start = atoi(p);
            const char *dash = strchr(p, '-');
            if (dash == NULL) {
                /* Engine: fatal_err("loop err 14") via FUN_0047aa31.
                 * Port: drop the line and continue (no recoverable
                 * state to restore; vendor data has '-' on every f:). */
                continue;
            }
            int floor_end = atoi(dash + 1);

            /* Engine: writes (floor_start, floor_end) to OLD local_c
             * BEFORE advancing. This is the off-by-one that corrupts
             * the last section of every dungeon except the last when
             * the next dungeon's first f: line arrives. */
            snews_section_t *old = section_at(out, section_idx);
            if (old != NULL) {
                old->floor_start = floor_start;
                old->floor_end   = floor_end;
            }

            /* Advance local_c = base + (sections_in_current +
             * current_dungeon*30) * 0xa8; then bump section counter. */
            int new_idx = current_dungeon * SNEWS_SECTION_COUNT
                          + sections_in_current;
            section_idx = new_idx;
            sections_in_current++;
            entries_in_current = 0;
            continue;
        }

        /* Entry / name dispatch. Engine: 3-byte "NON" match at L2308,
         * falling through to a "first char is digit" probe at L2310. */
        int id;
        const char *trailer;
        if (llen >= 3 && line[0] == 'N' && line[1] == 'O' && line[2] == 'N') {
            id = SNEWS_NON_ID;
            trailer = line + 3;  /* engine: pcVar16 = local_27c + 0x23 */
        } else if (line[0] >= '0' && line[0] <= '9') {
            id = atoi(line);
            trailer = line + 3;  /* engine: pcVar16 = local_27c + 0x23 */
            /* Note: the engine reads only line[0]; lines like "1abc"
             * are treated as id=1 with a 2-char trailer "bc". Vendor
             * data uses 3-digit zero-padded IDs exclusively. */
        } else {
            /* Engine: MessageBoxA "不明なニュース" then continues. */
            continue;
        }

        /* Engine guards against reading past the line on a short
         * trailer — atoi/atof would tolerate a NUL byte but the
         * trailer dispatch reads trailer[0] without a length check. */
        if ((size_t)(trailer - line) > llen) {
            /* Line shorter than the 3-byte prefix consumed (e.g. "NO"
             * — but that would have failed the NON match anyway, so
             * this guard is purely defensive). */
            continue;
        }

        if (trailer[0] == ',') {
            /* Comma path: append (id, weight=atoi(trailer+1)) to the
             * current section's entry list. */
            int weight = atoi(trailer + 1);
            snews_section_t *sec = section_at(out, section_idx);
            push_entry(sec, id, weight, &entries_in_current);
            continue;
        }

        /* Name-table path: copy `<separator><text>` into names[id].
         * Port guard: bounds-check id against the 64-slot table
         * (engine would write OOB for id < 0 or id >= 64). */
        if (id >= 0 && id < SNEWS_NAME_COUNT) {
            copy_name(out, id, trailer);
        }
    }
}
