/*
 * tables_config.c — `data/config.idx` parser.
 *
 * Source-level reference: FUN_00475270 block #2 in
 * docs/decompiled/by-address/475270.c (lines ~330–425). Identifies the
 * file via `s_config_idx_005cac78` (size) and `s_config_idx_005cac84`
 * (read) — these two .data strings have **different spellings** in the
 * original engine; see docs/findings/tables-loader.md for the
 * 940-byte-overrun quirk that produces. Our `tables.c` dispatcher
 * sidesteps it by calling storage_get_size and storage_read with the
 * same `"data/config.idx"` spelling; we don't reproduce the overrun.
 *
 * The keys themselves are byte-disjoint, so the engine's "match every
 * prefix on every non-comment line" approach has no overlap in
 * practice — but we mirror that structure faithfully.
 *
 * Engine quirk: `makefont` is checked but **does nothing**. The .data
 * string at 0x005cacbc is the bare word "makefont" (no trailing
 * colon — confirmed by `strings` on the unpacked binary), and the
 * matched-8-bytes path falls through without setting any global.
 * Likely a stub for a feature that was never implemented. We mirror
 * the check so the engine's behavior on a `makefont:...` line is the
 * same (effectively a no-op).
 */

#include "tables_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct config_idx g_config;

/* Match-prefix helper. `prefix` is a literal C string (length
 * computed via sizeof - 1 at the call site). Returns 1 on match. */
static int line_starts_with(const char *line, size_t llen,
                            const char *prefix, size_t plen)
{
    return llen >= plen && memcmp(line, prefix, plen) == 0;
}

void tables_parse_config(const unsigned char *data, size_t size,
                         struct config_idx *out)
{
    memset(out, 0, sizeof *out);

    char line[CONFIG_FONT_NAME_CAP + 16]; /* room for "font:" + max name */

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

        if (llen == 0 || line[0] == '/') continue;

        /* Five live keys + one dead one, in the engine's order. */

        if (line_starts_with(line, llen, "kanjioff:", 9)) {
            out->kanjioff = 1;
        }
        if (line_starts_with(line, llen, "edgewi:", 7)) {
            out->edgewi = atoi(line + 7);
        }
        if (line_starts_with(line, llen, "effectmode:", 11)) {
            out->effectmode = 1;
        }
        if (line_starts_with(line, llen, "edgedel:", 8)) {
            out->edgedel = atoi(line + 8);
        }
        /* Engine quirk: matches 8 bytes against "makefont" (no colon)
         * but assigns to nothing. We mirror the dead check for
         * documentation; remove the if-block if you want to drop the
         * fidelity in favor of a smaller binary footprint. */
        if (line_starts_with(line, llen, "makefont", 8)) {
            /* deliberate no-op */
        }
        if (line_starts_with(line, llen, "font:", 5)) {
            out->font_set = 1;
            /* Engine zeros the 256-byte buffer then inline-strcpys
             * from line+5. Our line buffer already excludes the
             * \r/\n terminator, so it's a clean C string. */
            memset(out->font_name, 0, sizeof out->font_name);
            size_t name_len = llen - 5;
            if (name_len >= sizeof out->font_name)
                name_len = sizeof out->font_name - 1;
            memcpy(out->font_name, line + 5, name_len);
            /* memset above already left the trailing \0 in place. */
        }
    }
}
