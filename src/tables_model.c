/*
 * tables_model.c — `data/model.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #9 in
 * docs/decompiled/by-address/475270.c (L1422..L1520). Identifies the
 * file via `s_data_model_txt_005cafc0` (size) and
 * `s_data_model_txt_005cafd0` (read); the two interned copies of
 * "data/model.txt" have the same spelling, so there is no path-mismatch
 * quirk here (unlike config.idx).
 *
 * Engine record layout at base `&DAT_073ae258` (stride 0x2b8 bytes,
 * 20 records total — indices 9, 16, and 19 are unused in the vendor file):
 *   +0x000  fname[0x20]      — .x filename, copied from `fname:` line
 *   +0x020  count (u32)      — number of matched `NN:` lines
 *   +0x024  point[20][0x20]  — bone / attachment-point names
 *   +0x2a4  used[20] (u8)    — 1 if slot N was ever written
 *
 * Engine quirks faithfully reproduced:
 *   - Init loop zeros `count` (offset 0x20) and `used[]` (0x2a4..0x2b8)
 *     in every record before parsing; fname and point buffers are NOT
 *     zero-init by the engine (the parser overwrites them per record).
 *     Our port uses memset(out, 0, ...) which also zeroes the name fields
 *     — this is a harmless superset that the engine would produce for
 *     any record that gets an `fname:` line before reuse.
 *   - `local_c` (current model index) is initialised to 0 at L1429, so
 *     a `fname:` or `NN:` line before any `no:` writes to record 0.
 *   - A `no:N` line only updates `local_c`; the subsequent `fname:` and
 *     `NN:` checks against the same line do not match the `no:` prefix
 *     and fall through harmlessly.
 *   - `used[slot]` is set to 1 on every matching `NN:` line regardless
 *     of whether it was already 1 (engine: unconditional byte write).
 *   - `count` increments once per matched `NN:` line — even if the slot
 *     is redefined (engine does not gate the increment on `!used[slot]`).
 *   - All 20 slot prefixes ("00:" .. "19:") are tested on every non-
 *     comment line; at most one matches in practice (disjoint prefixes).
 *   - atoi is used for `no:` parsing (CRT: skips leading whitespace,
 *     stops at first non-digit).
 *
 * Safety divergences (documented, not present in the engine):
 *   - Engine fname write cap is 0x100, but only the first 0x20 bytes of
 *     the record are addressable as `fname` before `count` at offset
 *     0x20. An overlong fname (>= 0x20 bytes incl. NUL) would corrupt
 *     `count` and the point-name slots in the engine. Our port truncates
 *     fname at MODEL_DEF_NAME_MAX-1 data chars + NUL at fname[31].
 *     Vendor data has fnames <= 12 chars; this divergence is dormant.
 *   - Same for point-name slots: engine cap 0x100, but slot stride is
 *     0x20; overlong point names would spill into the next slot. Port
 *     truncates at MODEL_DEF_NAME_MAX-1 data chars + NUL.
 *   - `no:N` where N < 0 or N >= MODEL_DEF_COUNT: engine would compute
 *     an out-of-bounds pointer and corrupt heap or stack. Port skips
 *     fname/slot writes for that model index (sets current to -1 as a
 *     sentinel and guards subsequent writes). Engine has no such guard.
 */

#include "tables_model.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

tables_model_t g_models[MODEL_DEF_COUNT];

/*
 * Copy at most `max_bytes` data bytes from `src` into `dst`, stopping
 * at '\0', '\r', '\n', or `max_bytes` bytes copied. Always writes a
 * terminating NUL at dst[n] where n is the number of bytes copied. The
 * caller must ensure dst has room for at least max_bytes + 1 bytes.
 */
static void copy_until_eol(char *dst, const char *src, size_t max_bytes)
{
    size_t n = 0;
    while (n < max_bytes) {
        unsigned char c = (unsigned char)src[n];
        if (c == '\0' || c == '\r' || c == '\n') break;
        dst[n] = (char)c;
        n++;
    }
    dst[n] = '\0';
}

void tables_parse_model(const unsigned char *data, size_t size,
                        tables_model_t out[MODEL_DEF_COUNT])
{
    memset(out, 0, sizeof(tables_model_t) * MODEL_DEF_COUNT);

    /* `current` tracks the most-recent `no:N` value. Engine initialises
     * `local_c` to 0 at L1429, so lines before any `no:` write to
     * record 0. We use -1 only as a sentinel for an out-of-range `no:`
     * index (safety divergence — engine has no such guard). */
    int current = 0;

    /* Scratch buffer for sprintf slot prefix generation. */
    char slot_prefix[4];

    /* Line buffer. */
    char line[512];

    size_t pos = 0;
    while (pos < size) {
        /* Engine: outer loop exits on '\0' at the current position. */
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

        /* Skip comment / blank lines.  Engine checks line[0] for '\r',
         * '\n', or '/'. Our buffer excludes \r/\n so an empty line
         * collapses to llen == 0. */
        if (llen == 0 || line[0] == '/') continue;

        /* `no:` dispatch — 3-byte prefix match (engine: L1462..L1465). */
        if (llen >= 3
            && line[0] == 'n' && line[1] == 'o' && line[2] == ':') {
            int idx = atoi(line + 3);
            if (idx < 0 || idx >= MODEL_DEF_COUNT) {
                /* Safety divergence: engine would compute an out-of-
                 * bounds pointer. Use -1 as a sentinel to suppress
                 * subsequent fname/slot writes until the next valid no:. */
                current = -1;
            } else {
                current = idx;
            }
            /* Engine: `no:` line does NOT fall through to fname: or slot
             * checks — but those checks are against the SAME line which
             * starts with "no:", so they simply won't match. */
        }

        /* Guard: skip fname/slot writes for an out-of-range index. */
        if (current < 0) continue;

        tables_model_t *rec = &out[current];

        /* `fname:` dispatch — 6-byte prefix match (engine: L1471..L1486).
         * Engine cap is 0x100 but fname field is only 0x20 bytes before
         * `count` at offset 0x20 — overlong fname would corrupt the
         * record. Port truncates at MODEL_DEF_NAME_MAX - 1 chars. */
        if (llen >= 6
            && line[0] == 'f' && line[1] == 'n' && line[2] == 'a'
            && line[3] == 'm' && line[4] == 'e' && line[5] == ':') {
            copy_until_eol(rec->fname, line + 6, MODEL_DEF_NAME_MAX - 1);
        }

        /* Slot dispatch: test all 20 two-digit prefixes ("00:"..)"19:").
         * Engine: inner do-while over local_18 = 0..0x13 (L1488..L1517).
         * `puVar14` starts at record base and advances by 0x20 per slot
         * iteration, so point[slot] is at puVar14 + 0x24 within the
         * per-slot iteration — equivalent to rec->point[slot] directly.
         * We use a direct index loop here for clarity. */
        for (int slot = 0; slot < MODEL_DEF_POINT_SLOTS; slot++) {
            sprintf(slot_prefix, "%02d:", slot);
            /* 3-byte prefix compare (engine: do-while iVar1 != 3). */
            if (llen < 3) continue;
            if (line[0] != slot_prefix[0]
                || line[1] != slot_prefix[1]
                || line[2] != slot_prefix[2]) continue;

            /* Match. Copy point name (line+3) into the slot.
             * Engine cap is 0x100 but slot stride is 0x20; overlong
             * names would spill into the next slot. Port truncates. */
            copy_until_eol(rec->point[slot], line + 3,
                           MODEL_DEF_NAME_MAX - 1);
            /* Engine: unconditional byte write (L1499). */
            rec->used[slot] = 1;
            /* Engine: count increment (L1512) — happens even on
             * redefinition of an already-used slot. */
            rec->count++;
            /* Only one slot can match per line (prefixes are disjoint),
             * but the engine checks all 20 anyway. We break early for
             * efficiency while preserving identical observable behavior. */
            break;
        }
    }
}
