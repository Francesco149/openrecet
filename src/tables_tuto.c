/*
 * tables_tuto.c — parser for `data/tuto1.txt`..`tuto3.txt`.
 *
 * Source reference: FUN_00475270 block #15 in
 * docs/decompiled/by-address/475270.c (lines ~2898–3123). The outer
 * 3-file loop lives there; each file feeds this parser exactly once.
 *
 * Line format (Shift-JIS): comma-separated, leading int is the record
 * `id` (jump target), second token is one of 16 opcode keywords from
 * `.data` (`CHR0`/`CHR1`/`TAGD`/`PRID`/`PRIA`/`BUN0`/`GOTO`/`TAGN`/
 * `TOUT`/`値段`/`高く`/`値引`/`値上`/`アイテム`/`剣選択`/`初期金額決定`).
 * Trailing fields depend on the opcode — see docs/formats/data-text.md.
 *
 * Engine quirks faithfully reproduced (and documented in
 * docs/findings/engine-quirks.md):
 *  - 50-record parser stride per file vs the consumer's 200-record
 *    stride → tuto2/tuto3 land in tuto1's address region.
 *  - No bound check on `local_8` → vendor files overflow the 50-slot
 *    cap, with tuto3 walking 10 slots past the array.
 *  - `id < -1` lines store text but don't set `opcode` (stays BSS-zero
 *    = CHR0, which is harmless because the gameplay-side dispatcher
 *    accesses these via `g_tuto[base + (-1 - id)]` rather than the
 *    opcode field).
 *  - The 7-int reader for short lines (e.g. `0,GOTO,9,...`) walks
 *    past the line buffer's NUL, picking up garbage. We use a
 *    zeroed-between-lines buffer, so missing fields read as 0 — a
 *    benign divergence (gameplay code uses only args[0] for GOTO).
 *
 * Pure C; compiles under host gcc for unit testing.
 */

#include "tables_tuto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tuto_record g_tuto[TUTO_RECORD_COUNT];

/* ─── opcode keyword table ─────────────────────────────────────────────
 *
 * Each entry: opcode value, byte sequence, byte count. Strings 6–9
 * are Shift-JIS Japanese; everything else is plain ASCII. Order
 * mirrors the engine's nested-if dispatch chain at L2977–3067, so the
 * "first match wins" precedence is identical (matters for 値段/高く,
 * which share opcode 12).
 *
 * Anonymous byte arrays — not C strings — because the SJIS keywords
 * are byte-disjoint memcmp targets, and we want the exact length the
 * engine compares (no implicit NUL).
 */
static const struct {
    int opcode;
    const unsigned char *bytes;
    int len;
} TUTO_OPCODES[] = {
    /* 4-byte ASCII opcodes */
    { TUTO_OP_CHR0, (const unsigned char *)"CHR0", 4 },
    { TUTO_OP_CHR1, (const unsigned char *)"CHR1", 4 },
    { TUTO_OP_TAGD, (const unsigned char *)"TAGD", 4 },
    { TUTO_OP_PRID, (const unsigned char *)"PRID", 4 },
    { TUTO_OP_PRIA, (const unsigned char *)"PRIA", 4 },
    { TUTO_OP_BUN0, (const unsigned char *)"BUN0", 4 },
    /* SJIS 値段 (4 bytes) and 高く (4 bytes) share opcode 12 */
    { TUTO_OP_PRICE,    (const unsigned char *)"\x92\x6c\x92\x69", 4 },  /* 値段 */
    { TUTO_OP_PRICE,    (const unsigned char *)"\x8d\x82\x82\xad", 4 },  /* 高く */
    /* SJIS 値引 / 値上 */
    { TUTO_OP_DISCOUNT, (const unsigned char *)"\x92\x6c\x88\xf8", 4 },  /* 値引 */
    { TUTO_OP_MARKUP,   (const unsigned char *)"\x92\x6c\x8f\xe3", 4 },  /* 値上 */
    /* More 4-byte ASCII opcodes */
    { TUTO_OP_GOTO, (const unsigned char *)"GOTO", 4 },
    { TUTO_OP_TAGN, (const unsigned char *)"TAGN", 4 },
    { TUTO_OP_TOUT, (const unsigned char *)"TOUT", 4 },
    /* Multi-byte SJIS opcodes */
    { TUTO_OP_SET_INITIAL,
      (const unsigned char *)"\x8f\x89\x8a\xfa\x8b\xe0\x8a\x7a\x8c\x88\x92\xe8", 12 },  /* 初期金額決定 */
    { TUTO_OP_ITEM,
      (const unsigned char *)"\x83\x41\x83\x43\x83\x65\x83\x80", 8 },                    /* アイテム */
    { TUTO_OP_SWORD,
      (const unsigned char *)"\x8c\x95\x91\x49\x91\xf0", 6 },                            /* 剣選択 */
};

#define TUTO_OPCODE_COUNT  (sizeof TUTO_OPCODES / sizeof TUTO_OPCODES[0])

/* Try every opcode entry against `p` in dispatch order. Returns the
 * opcode value on first match, or -2 on no match (-2 distinguishes
 * "no match" from "id == -1 sentinel"; the caller checks for -2). */
static int match_opcode(const char *p, size_t avail)
{
    for (size_t i = 0; i < TUTO_OPCODE_COUNT; i++) {
        int len = TUTO_OPCODES[i].len;
        if ((int)avail < len) continue;
        if (memcmp(p, TUTO_OPCODES[i].bytes, (size_t)len) == 0) {
            return TUTO_OPCODES[i].opcode;
        }
    }
    return -2;
}

/* Walk pcVar16 forward through `line` until we hit ',' / '\r' / '\n' /
 * end-of-buffer. Returns the new index. Matches the engine's
 *   `for (; cVar11 != ',' && cVar11 != '\r' && cVar11 != '\n'; pcVar16++) {}`
 * with the addition of a `< llen` bound — the engine has no such
 * bound and happily walks past NUL into stack garbage; we use a
 * zero-padded buffer so this is benign but still bounded. */
static size_t walk_to_delim(const char *line, size_t llen, size_t i)
{
    while (i < llen) {
        char c = line[i];
        if (c == ',' || c == '\r' || c == '\n') break;
        i++;
    }
    return i;
}

int tables_parse_tuto(int file_index,
                      const unsigned char *data, size_t size,
                      struct tuto_record *records)
{
    /* Slot index within the shared array. The engine computes
     * `(local_8 + local_c * 50) * 0x128`; here we increment a slot
     * counter that starts at `file_index * 50` and grows monotonically. */
    int slot = file_index * TUTO_PARSER_STRIDE;
    int records_written = 0;

    /* Per-line buffer. Engine reserves local_27c[0x20..0x100] = 224
     * bytes; we keep 256 to absorb the engine's missing-bound bug on
     * unusually long lines. Zero-init per line so the 7-int post-NUL
     * reader produces predictable zeros instead of stack junk. */
    char line[256];

    size_t pos = 0;
    while (pos < size && data[pos] != '\0') {
        /* ─── line scanner ───
         * Read one line into `line`, NUL-terminated. CR/LF/NUL/EOB end
         * the line. The engine doesn't strip trailing \r\n, but does
         * NUL-terminate one byte past the last char written — so the
         * line buffer content INCLUDES the \r or \n if either was the
         * stop byte. We follow that: include the terminator. */
        memset(line, 0, sizeof line);
        size_t llen = 0;
        char stop = '\0';
        while (pos < size && data[pos] != '\0' && llen + 1 < sizeof line) {
            char c = (char)data[pos++];
            line[llen++] = c;
            if (c == '\r' || c == '\n') {
                stop = c;
                break;
            }
        }
        line[llen] = '\0';

        /* Engine skips lines whose first char is \r, \n, or '/'. The
         * scanner re-iterates from the next byte in the source stream,
         * which means a CRLF appears as two iterations (CR then LF),
         * each skipped by this rule. */
        if (llen == 0) continue;
        if (line[0] == '\r' || line[0] == '\n' || line[0] == '/') continue;
        (void)stop;

        /* ─── per-line parsing ───
         * Engine:  iVar1 = atoi(line); *piVar4 = iVar1;
         * The first int is the record's `id` (jump target). */
        struct tuto_record *rec = &records[slot];
        int id = atoi(line);
        rec->id = id;

        if (id < 0) {
            if (id == -1) {
                /* id == -1: sentinel terminator line. Engine writes
                 * opcode = -1 and moves on; no text copy. */
                rec->opcode = TUTO_OP_SENTINEL;
            } else {
                /* id < -1: text-only line, copied from offset +3
                 * (i.e. past "-N,") into rec->text. opcode is NOT
                 * set — stays at whatever it was (BSS-zero = CHR0
                 * on a fresh slot, which is benign here because the
                 * gameplay dispatcher addresses these by negative id,
                 * not by opcode). */
                size_t src = 3;
                size_t dst = 0;
                while (src < llen && dst < TUTO_TEXT_SIZE - 1) {
                    char c = line[src];
                    if (c == '\0' || c == '\r' || c == '\n') break;
                    rec->text[dst++] = c;
                    src++;
                }
                rec->text[dst] = '\0';
            }
            slot++;
            records_written++;
            continue;
        }

        /* Find the first comma. The engine has no bound check; if the
         * line has no comma it walks into garbage. We bound. */
        size_t comma = 0;
        while (comma < llen && line[comma] != ',') comma++;
        if (comma >= llen) {
            /* No comma — engine's "loop err 17" path. Log and skip
             * (engine logs to its debug pipe and loops to the next line
             * without incrementing the record slot). */
            fprintf(stderr,
                    "tables: tuto%d.txt — loop err 17 (no comma): %.60s\n",
                    file_index + 1, line);
            continue;
        }

        /* Opcode dispatch — tokens start at line[comma + 1]. */
        size_t opcode_start = comma + 1;
        int op = match_opcode(line + opcode_start, llen - opcode_start);
        if (op == -2) {
            /* No opcode match: engine pops MessageBoxA "syntax error"
             * and continues to the next line. We log to stderr. */
            fprintf(stderr,
                    "tables: tuto%d.txt — syntax error: %.60s\n",
                    file_index + 1, line);
            slot++;
            records_written++;
            continue;
        }
        rec->opcode = op;

        /* Post-dispatch payload reads. Two families:
         *   {CHR0, CHR1} → +6 from first comma, 1 int + text
         *   {BUN0, GOTO, 剣選択, PRICE, DISCOUNT, MARKUP} → +5, 7 ints
         * Everything else takes no further reads. */
        if (op == TUTO_OP_CHR0 || op == TUTO_OP_CHR1) {
            /* +6 from comma lands past "CHRx," (5 bytes) on the digit. */
            size_t i = comma + 6;
            if (i >= llen) goto done;

            rec->chr_arg = atoi(line + i);
            i = walk_to_delim(line, llen, i);
            if (i >= llen || line[i] != ',') goto done;

            /* Engine reads pcVar16[1] first, then advances pcVar16,
             * then loops. That copies the char AFTER the comma. */
            i++;  /* past comma */
            size_t dst = 0;
            while (i < llen && dst < TUTO_TEXT_SIZE - 1) {
                char c = line[i];
                if (c == '\0' || c == '\r' || c == '\n') break;
                rec->text[dst++] = c;
                i++;
            }
            rec->text[dst] = '\0';
        } else if (op == TUTO_OP_BUN0 || op == TUTO_OP_GOTO
                || op == TUTO_OP_SWORD
                || op == TUTO_OP_PRICE
                || op == TUTO_OP_DISCOUNT
                || op == TUTO_OP_MARKUP) {
            /* Engine: pcVar16 += 5 from first comma. For 4-byte tokens
             * that lands on the comma right after the token; for the
             * 6-byte 剣選択 it lands one byte inside the trailing comma
             * area — the per-iteration "walk to delim" loop catches up
             * either way. */
            size_t i = comma + 5;
            for (int k = 0; k < 7; k++) {
                i = walk_to_delim(line, llen, i);
                if (i >= llen) {
                    /* Engine quirk: walks past NUL into stack garbage,
                     * atoi'ing whatever's there. Our zeroed line buffer
                     * produces 0 — see header for divergence note. */
                    rec->args[k] = 0;
                    continue;
                }
                /* Engine: pcVar16++ THEN atoi — so we advance past the
                 * delimiter regardless of which one it is (',', \r, \n). */
                i++;
                rec->args[k] = atoi(line + i);
            }
        }

    done:
        slot++;
        records_written++;
    }

    /* End-of-file: engine stamps opcode = -1 at the next (unwritten)
     * slot as a record-list terminator. */
    records[slot].opcode = TUTO_OP_SENTINEL;
    return records_written;
}
