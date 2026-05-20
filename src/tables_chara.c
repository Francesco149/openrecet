/*
 * tables_chara.c — `data/chara.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #6 in
 * docs/decompiled/by-address/475270.c (L1030..L1146 plus the
 * continuation block at LAB_00477931, which the decompiler emits
 * non-adjacent at all.c L76547..L76593). Identifies the file via
 * `s_data_chara_txt_005cae6c` (size) and `s_data_chara_txt_005cae7c`
 * (read); both interned copies hold the same spelling
 * "data/chara.txt", so there is no path-mismatch quirk (unlike
 * config.idx).
 *
 * The block populates two interleaved field groups in each record:
 *
 *   "000:" .. "007:"  → 10 CSV fields (7 ints + 3 floats):
 *       level_threshold, AT, DF, MT, MF, HP, SP, move, dash, crit
 *     dispatched after a 1-byte pre-check that line[0] == '0', then
 *     a 4-byte exact match against sprintf("%03d:", idx).
 *
 *   "100:" .. "107:"  → 6 CSV ints: AT, DF, MT, MF, HP, SP
 *     dispatched after a 1-byte pre-check that line[0] == '1', then
 *     a 4-byte exact match against sprintf("%03d:", idx + 100).
 *
 * Field order on disk differs from in-memory layout for both groups:
 * the file lists stats in the human-readable order AT/DF/MT/MF/HP/SP
 * but the engine stores HP/SP at the lower offsets (the parser
 * permutes columns to record offsets via piVar13[3..6, 1..2] and
 * puVar12[0xc..0xf, 10, 0xb] respectively). See header for the
 * canonical layout.
 *
 * Engine quirks faithfully reproduced:
 *   - Init loop (L1035..L1047) seeds nine of the ten base fields
 *     (skipping crit_rate at +0x24) and zero of the lv100 fields.
 *     The port memsets the array to zero first so crit_rate and all
 *     lv100 stats start at 0 — a harmless superset of the engine's
 *     "leave uninit, then overwrite on parse" pattern.
 *   - `field1 - 1` write for the leading column: the file's first
 *     CSV column is the unlock level (1, 8, 10, 20, 15, 15, 30, 1
 *     for the vendor adventurers); the engine subtracts 1 to store
 *     a 0-based level threshold.
 *   - All ten "%03d:" keys are tested per line even though only 8
 *     records exist. Engine cap is 10 (a 2-record overrun bug); port
 *     caps at CHARA_COUNT to avoid clobbering the adjacent g_models
 *     globals at &DAT_073ae258. Vendor data never triggers the bug.
 *   - Comment skip: line[0] ∈ { '/', '\r', '\n' } — same convention
 *     as every other tables_load_all block.
 *   - Float storage path: atof → cast to float32, stored as 4 raw
 *     bytes (`(int)(float)` in the decompiler). The init values
 *     0x3e19999a and 0x3e4ccccd are the exact bit patterns of
 *     `0.15f` and `0.20f` in IEEE 754 single, which C float literals
 *     produce verbatim — so the port's `0.15f` / `0.20f` initializers
 *     are byte-identical to the engine's init.
 *
 * Safety divergences (documented, not present in the engine):
 *   - Out-of-range index 008/009 / 108/109: the engine's parse loop
 *     iterates 10 times and would write 64 bytes past the end of the
 *     8-record array into g_models[0..1]. The port caps the inner
 *     match loop at CHARA_COUNT and silently drops 008+/108+ lines.
 *     Vendor data has no such lines.
 *   - Malformed lines with fewer commas than expected: the engine
 *     would scan past the in-buffer NUL into adjacent stack memory
 *     and atoi/atof the result. The port stops every comma scan at
 *     the in-buffer NUL, so subsequent atoi/atof read a clean empty
 *     string and return 0. Vendor data is well-formed.
 */

#include "tables_chara.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

chara_def_t g_chara[CHARA_COUNT];

/* Advance past the next ',' in a NUL-terminated string. If no comma
 * is found before the NUL, returns a pointer to the NUL itself so
 * subsequent atoi/atof yield 0 (engine reads garbage; port reads ""). */
static const char *skip_to_next_field(const char *p)
{
    while (*p && *p != ',') p++;
    if (*p == ',') p++;
    return p;
}

/* Seed defaults into every record. Mirrors the engine init loop at
 * L1035..L1047, except crit_rate and the six lv100 stats stay zero
 * (the engine leaves them uninit; our memset 0 in the caller is a
 * harmless superset). */
static void seed_defaults(chara_def_t out[CHARA_COUNT])
{
    for (int i = 0; i < CHARA_COUNT; i++) {
        out[i].level_threshold = 1;
        out[i].hp_base         = 50;
        out[i].sp_base         = 30;
        out[i].at_base         = 10;
        out[i].df_base         = 13;
        out[i].mt_base         = 5;
        out[i].mf_base         = 10;
        out[i].move_speed      = 0.15f;
        out[i].dash_speed      = 0.20f;
        /* crit_rate, hp_lv100..mf_lv100 already zero from caller memset. */
    }
}

/* "0NN:" branch — 10 fields (7 ints + 3 floats). */
static void parse_base_line(chara_def_t *rec, const char *fields)
{
    const char *p = fields;

    /* file field 1 → level_threshold (atoi(p) - 1) */
    rec->level_threshold = atoi(p) - 1;
    p = skip_to_next_field(p);

    /* file field 2 → at_base */
    rec->at_base = atoi(p);
    p = skip_to_next_field(p);

    /* file field 3 → df_base */
    rec->df_base = atoi(p);
    p = skip_to_next_field(p);

    /* file field 4 → mt_base */
    rec->mt_base = atoi(p);
    p = skip_to_next_field(p);

    /* file field 5 → mf_base */
    rec->mf_base = atoi(p);
    p = skip_to_next_field(p);

    /* file field 6 → hp_base */
    rec->hp_base = atoi(p);
    p = skip_to_next_field(p);

    /* file field 7 → sp_base */
    rec->sp_base = atoi(p);
    p = skip_to_next_field(p);

    /* file field 8 → move_speed (float) */
    rec->move_speed = (float)atof(p);
    p = skip_to_next_field(p);

    /* file field 9 → dash_speed (float) */
    rec->dash_speed = (float)atof(p);
    /* Engine does NOT advance pcVar16 past the final comma before
     * reading the last field — it scans to the comma, then atofs
     * pcVar16+1. Functionally identical to advancing past the
     * comma; we keep the helper for symmetry. */
    p = skip_to_next_field(p);

    /* file field 10 → crit_rate (float) */
    rec->crit_rate = (float)atof(p);
}

/* "1NN:" branch — 6 int fields, permuted into the lv100 slots. */
static void parse_lv100_line(chara_def_t *rec, const char *fields)
{
    const char *p = fields;

    /* file field 1 → at_lv100 */
    rec->at_lv100 = atoi(p);
    p = skip_to_next_field(p);

    /* file field 2 → df_lv100 */
    rec->df_lv100 = atoi(p);
    p = skip_to_next_field(p);

    /* file field 3 → mt_lv100 */
    rec->mt_lv100 = atoi(p);
    p = skip_to_next_field(p);

    /* file field 4 → mf_lv100 */
    rec->mf_lv100 = atoi(p);
    p = skip_to_next_field(p);

    /* file field 5 → hp_lv100 */
    rec->hp_lv100 = atoi(p);
    p = skip_to_next_field(p);

    /* file field 6 → sp_lv100 */
    rec->sp_lv100 = atoi(p);
}

void tables_parse_chara(const unsigned char *data, size_t size,
                        chara_def_t out[CHARA_COUNT])
{
    memset(out, 0, sizeof(chara_def_t) * CHARA_COUNT);
    seed_defaults(out);

    char line[512];

    size_t pos = 0;
    while (pos < size) {
        if (data[pos] == '\0') break;

        /* Collect one line (exclude the terminator). */
        size_t llen = 0;
        while (pos < size
               && data[pos] != '\0'
               && data[pos] != '\r'
               && data[pos] != '\n'
               && llen + 1 < sizeof line) {
            line[llen++] = (char)data[pos++];
        }
        line[llen] = '\0';

        /* Consume one terminator byte; CRLF produces a second iteration
         * with an empty line which is then skipped as blank. */
        if (pos < size && (data[pos] == '\r' || data[pos] == '\n')) pos++;

        /* Skip comment / blank lines. */
        if (llen == 0 || line[0] == '/') continue;

        /* "0NN:" branch — base stats. The 1-byte pre-check on '0' is
         * the engine's fast-skip; the 4-byte exact match against
         * "%03d:" then disambiguates the index. */
        if (line[0] == '0') {
            /* Engine iterates idx = 0..9 (10 records); port caps at
             * CHARA_COUNT to avoid clobbering g_models at +0x40 past
             * the chara array. Vendor data only uses 0..7. */
            for (int idx = 0; idx < CHARA_COUNT; idx++) {
                char key[8];
                snprintf(key, sizeof key, "%03d:", idx);
                if (llen >= 4 && memcmp(line, key, 4) == 0) {
                    parse_base_line(&out[idx], line + 4);
                    break;
                }
            }
            continue;
        }

        /* "1NN:" branch — level-100 overrides. Index in file is
         * (CHARA + 100); we subtract 100 to remap to [0, CHARA_COUNT). */
        if (line[0] == '1') {
            for (int idx = 0; idx < CHARA_COUNT; idx++) {
                char key[8];
                snprintf(key, sizeof key, "%03d:", idx + 100);
                if (llen >= 4 && memcmp(line, key, 4) == 0) {
                    parse_lv100_line(&out[idx], line + 4);
                    break;
                }
            }
            continue;
        }

        /* Lines starting with any other character are silently
         * skipped (engine: both 1-byte pre-checks miss → no parse). */
    }
}
