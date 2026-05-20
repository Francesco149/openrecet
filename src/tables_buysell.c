/*
 * tables_buysell.c — `data/buysell.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #7 in
 * docs/decompiled/by-address/475270.c (lines ~1296–1377). Identifies
 * the file by the `s_data_buysell_txt_005caf28` storage_get_size /
 * `…_005caf3c` storage_read pair.
 *
 * The engine reads bytes one at a time into a 0x20-offset line buffer
 * (so the first 32 bytes of the local frame stay free for the line-by-
 * line scratch state) and tries every known prefix on every non-comment
 * line. We mirror that "check every key on every line" structure —
 * even though the keys are byte-disjoint in practice, it costs nothing
 * and stays faithful to the source. See docs/findings/tables-loader.md
 * for the broader caller / file-list context.
 */

#include "tables_buysell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct buysell_config g_buysell;

/* Shift-JIS bytes for the two Japanese keys. The engine compares raw
 * bytes against .data strings at 0x005caf54 (`客番号:`, 7 bytes incl.
 * the trailing colon) and 0x005caf5c (`種類:`, 5 bytes). We extracted
 * these from the live binary's strings dump and confirmed them against
 * the vendor `data/buysell.txt` itself (od output).
 *   客=0x8B71  番=0x94D4  号=0x8D86
 *   種=0x9484  類=0x9483
 * Stored as anonymous byte arrays so the C-string \0 sentinel doesn't
 * collide with the count-prefixed memcmp. */
static const unsigned char KEY_KYAKU[7] = {
    0x8B, 0x71, 0x94, 0xD4, 0x8D, 0x86, ':'
};
static const unsigned char KEY_KIND[5] = {
    0x94, 0x84, 0x94, 0x83, ':'
};

void tables_parse_buysell(const unsigned char *data, size_t size,
                          struct buysell_config *out)
{
    memset(out, 0, sizeof *out);

    /* Line buffer. The engine's reservation is at local_27c[0x20..],
     * effectively unlimited in practice (frame is hundreds of bytes).
     * buysell.txt lines are tiny — keys + a small int — so 256 is
     * plenty. Anything longer just gets truncated, which the engine
     * also effectively does via its frame size. */
    char line[256];

    size_t pos = 0;
    while (pos < size) {
        /* Mirror the engine's "stop on embedded \0" behavior. The
         * engine relies on its malloc(size+10) + explicit final-byte
         * \0 write; we use `size` as authoritative and additionally
         * treat any in-band \0 as EOF, which matches engine intent
         * for malformed input. */
        if (data[pos] == '\0') break;

        /* Read one line into `line`. \r and \n both terminate (the
         * engine writes the terminator into the buffer then null-
         * terminates one byte later; we leave it out entirely — the
         * downstream atoi doesn't care about trailing whitespace). */
        size_t llen = 0;
        while (pos < size
               && data[pos] != '\0'
               && data[pos] != '\r'
               && data[pos] != '\n'
               && llen + 1 < sizeof line) {
            line[llen++] = (char)data[pos++];
        }
        line[llen] = '\0';

        /* Consume one line terminator. A \r\n pair is handled as two
         * separate iterations (the second one will see an empty line
         * and skip it via the llen==0 / leading-'/' check below), which
         * mirrors how the engine's outer loop walks one byte at a
         * time. */
        if (pos < size && (data[pos] == '\r' || data[pos] == '\n')) pos++;

        /* Engine: skip lines starting with \r, \n, or '/'. Empty lines
         * (llen == 0) collapse those three cases for us. */
        if (llen == 0 || line[0] == '/') continue;

        /* Engine quirk: every key is matched independently — no
         * else-if. Faithfully reproduced. */

        /* `ok:` (3 bytes) — debug-mode toggle. */
        if (llen >= 3 && memcmp(line, "ok:", 3) == 0) {
            out->debug_mode = 1;
        }
        /* `客番号:` (7 bytes) — customer number. atoi from offset 7,
         * matching the engine's `local_27c + 0x27` (= local_27c[0x20]
         * line base + 7). */
        if (llen >= 7 && memcmp(line, KEY_KYAKU, 7) == 0) {
            out->kyaku_number = atoi(line + 7);
        }
        /* `種類:` (5 bytes) — kind (0=sell, 1=buy, 2=about). atoi from
         * offset 5. */
        if (llen >= 5 && memcmp(line, KEY_KIND, 5) == 0) {
            out->kind = atoi(line + 5);
        }

        /* `msg%02d:` / `rmsg%02d:` arrays — 20 entries each, mirroring
         * the engine's `puVar12 != &DAT_073b1a68` loop range. */
        for (int i = 0; i < BUYSELL_MSG_COUNT; i++) {
            char key[10];
            int klen;

            klen = snprintf(key, sizeof key, "msg%02d:", i);
            if ((int)llen >= klen && memcmp(line, key, (size_t)klen) == 0) {
                out->msg[i] = atoi(line + klen);
            }
            klen = snprintf(key, sizeof key, "rmsg%02d:", i);
            if ((int)llen >= klen && memcmp(line, key, (size_t)klen) == 0) {
                out->rmsg[i] = atoi(line + klen);
            }
        }
    }
}
