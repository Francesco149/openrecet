/*
 * tables_gousei.c — `data/gousei.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #13 in
 * docs/decompiled/by-address/475270.c, starting at LAB_004790cd
 * (L2402..L2579). Identifies the file via the interned string
 * `s_data_gousei_txt_005cb1f4` (size path) and `_005cb204` (read
 * path); both copies hold "data/gousei.txt", so no path-mismatch
 * quirk to mirror (cf. config.idx).
 *
 * Per-line shape (vendor file):
 *
 *     0004:Gilded Sword:Longsword#1:Water Crystal#1:
 *     <id><out         ><ing1   ><c><ing2          ><c>
 *
 * The first 5 bytes (`NNNN:`) are skipped wholesale by the engine
 * (`pcVar16 = local_27c + 0x25`). The 4-digit numeric prefix is
 * therefore parsed-but-discarded — the output item is keyed by NAME,
 * not by the numeric prefix. This is most likely a documentation
 * affordance for the data designers (column = sprite slot, row = sub-
 * index within the slot).
 *
 * Recipe lines may be preceded by `ランク:N` header lines that update
 * the rolling `current_rank` state. Records before the first header
 * carry rank=0.
 *
 * Engine quirks faithfully reproduced:
 *
 *   - **Discarded 4-digit prefix.** Recipe lines start with `NNNN:`
 *     but the parser skips 5 bytes wholesale. The numeric prefix is
 *     never read.
 *
 *   - **ing1 write resets ing2..5 ID slots to -1.** When the parser
 *     writes the column-1 ingredient ID, it also writes 0xffffffff
 *     to the ing2..ing5 ID slots (engine: L2533..L2538). This is the
 *     ONLY place these slots are pre-initialised; if column 1 isn't
 *     reached on a given line, those slots stay at BSS-zero. Counts
 *     (ing*_count) are never pre-initialised — unused counts stay at
 *     zero.
 *
 *   - **No reset between records.** The engine's outer parser writes
 *     into `array[count]` without clearing the slot first; only fields
 *     touched by the line are populated. The ing1 quirk above
 *     compensates for ingredient IDs but not for output_id or counts.
 *     The port memsets the whole array once before parsing, so all
 *     unwritten slots cleanly read as zero.
 *
 *   - **Per-iteration rank write.** Engine writes `rank` into every
 *     record on every ':' boundary (L2484), so rank is touched ~6
 *     times per recipe with the same value. Port writes it once at
 *     record-start; observable end-state is identical.
 *
 *   - **Exact-name resolver.** Item-name lookup against the item.txt
 *     table uses a length-equality + prefix-equality probe (engine
 *     L2491..L2519). NOT longest-common-prefix (that's enemy.txt).
 *     Names must match exactly.
 *
 *   - **Resolver index-0 match still pops MessageBox.** Engine bug:
 *     if the matched item is at index 0 in the item table, the
 *     "match" path takes a `break` (not `goto skip_messagebox`) and
 *     the MessageBoxA fires anyway. The resolved ID does land in the
 *     record. Port doesn't pop MessageBox at all (resolver returns
 *     whatever it returns), so this quirk is moot in our port. See
 *     docs/findings/engine-quirks.md.
 *
 *   - **`#` count handler walks to ':'.** After atoi'ing the count
 *     digits, the engine seeks forward to the next ':' before falling
 *     into the ':' handler. The port mirrors this — without it, the
 *     count digits would be accumulated as part of the next field
 *     name.
 *
 *   - **`#count` at EOL with no trailing ':'.** Vendor data includes
 *     one line that ends `#1` without the otherwise-mandatory trailing
 *     ':' (`Master's Plate` recipe at rank 4). The engine's `#`
 *     handler does an UNBOUNDED scan for ':' after the count digits —
 *     so on this line it walks past `\r\n` into the rest of the
 *     buffer, eventually hitting some ':' far away and treating
 *     whatever happens between as garbage. The record IS still
 *     created (count++) and the four ingredients are written
 *     correctly. The port detects EOL/EOF inside the `:` hunt and
 *     finalises the last ingredient cleanly (resolves the accumulated
 *     name, writes the ID, then breaks out of the line loop). Net
 *     effect: same record contents as the engine, minus the
 *     undefined-behaviour scan. See engine-quirks.md #23.
 *
 *   - **Zero count warns.** `#0` triggers MessageBoxA "loop err 15".
 *     Vendor data has no zero counts (all `#N` are 1..). Port logs to
 *     stderr instead.
 *
 *   - **Stale-count probe.** After writing column N's ID, engine
 *     checks if the corresponding ingredient COUNT slot is 0 (i.e.
 *     `#` was never reached for that field). If so, MessageBoxA
 *     "合成素材アイテム数不明" fires. Vendor data with explicit
 *     counts on every ingredient field never trips this. Port logs
 *     to stderr.
 *
 * Safety divergences (documented, not present in the engine):
 *
 *   - **200-record hard cap.** Engine writes record 201 INTO adjacent
 *     globals before noticing it overflowed (`if (200 < count)` runs
 *     AFTER the write). Port refuses to write past slot 199 (count
 *     reaches 200) and logs to stderr. Vendor data ships 177
 *     recipes.
 *
 *   - **Line buffer bounded at 512.** Engine has no explicit per-line
 *     read cap; the outer post-prefix field walker bounds at 0x100
 *     chars. Vendor lines are well under both bounds.
 *
 *   - **MessageBoxA suppression.** Every "loop err 15" / "unknown
 *     ingredient" / "stale count" pop-up becomes a stderr log line.
 *     The engine's pop-ups blockUI; we never want that during boot.
 */

#include "tables_gousei.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

gousei_state_t g_gousei;

/* SJIS bytes for the "ランク:" header prefix at `&DAT_005cb214`.
 * ラ = 83 89, ン = 83 93, ク = 83 4e, ':' = 3a. Total 7 bytes.
 * Kept as octal/hex escapes so this source stays pure ASCII. */
static const unsigned char k_rank_prefix[7] = {
    0x83, 0x89,  /* ラ */
    0x83, 0x93,  /* ン */
    0x83, 0x4e,  /* ク */
    0x3a         /* ':' */
};

/* Resolve a field name to an item id via the caller-supplied callback.
 * NULL resolver → -1 (the convention used before item.txt lands). */
static int32_t resolve_name(const char *name,
                            gousei_resolve_fn resolve, void *user)
{
    if (resolve == NULL || name == NULL || name[0] == '\0') {
        return GOUSEI_EMPTY;
    }
    return resolve(name, user);
}

/* Write an ingredient ID into the column-`col` slot (col ∈ 1..5).
 * Replicates engine L2534..L2550. When col == 1, the ID handler also
 * stamps ing2..ing5 to -1 (the only pre-init those slots ever get). */
static void write_ingredient_id(gousei_record_t *r, int col, int32_t id)
{
    if (col < 1 || col > GOUSEI_INGREDIENT_COUNT) return;

    r->ingredient_id[col - 1] = id;

    if (col == 1) {
        /* Engine quirk: writing ing1 ID stamps ing2..ing5 IDs to -1.
         * Subsequent fields overwrite these as they arrive. */
        for (int k = 1; k < GOUSEI_INGREDIENT_COUNT; k++) {
            r->ingredient_id[k] = GOUSEI_EMPTY;
        }
    }
}

/* Write an ingredient count into the column-`col` slot (col ∈ 1..5). */
static void write_ingredient_count(gousei_record_t *r, int col, int32_t cnt)
{
    if (col < 1 || col > GOUSEI_INGREDIENT_COUNT) return;
    r->ingredient_count[col - 1] = cnt;
}

/* Write the output ID (column 0). */
static void write_output_id(gousei_record_t *r, int32_t id)
{
    r->output_id = id;
}

/* Parse one recipe line into `r`. Returns 0 on success, -1 on
 * malformed (e.g. EOL inside a `#count` group). The line text is at
 * `line[5..line_len-1]` — caller has already verified the line is a
 * recipe (not a comment, not a rank header) and supplied the full
 * SJIS line. `current_rank` is the rolling rank from the last
 * `ランク:` header (or 0 if none). */
static int parse_recipe_line(gousei_record_t *r, int32_t current_rank,
                             const char *line, size_t line_len,
                             gousei_resolve_fn resolve, void *user)
{
    /* Engine: pcVar16 = local_27c + 0x25 — skip the 5-byte "NNNN:"
     * prefix. */
    if (line_len < 5) return -1;
    const char *p     = line + 5;
    const char *p_end = line + line_len;

    /* Engine writes rank at every ':' boundary — we set it once. */
    r->rank = current_rank;

    /* Per-field name buffer (engine local_57c, 0x100 bytes). */
    char field_name[GOUSEI_FIELD_NAME_LEN];
    int  field_len = 0;
    field_name[0]  = '\0';

    int col = 0;        /* engine local_18: 0 = output, 1..5 = ingredients */
    int scanned = 0;    /* engine local_8: per-line scan cap (0x100) */

    while (p < p_end && scanned < GOUSEI_LINE_SCAN_CAP) {
        char c = *p;
        if (c == '\r' || c == '\n' || c == '\0') break;

        if (c == '#') {
            /* Engine: pcVar16++; count = atoi; if 0 → MessageBox. */
            p++;
            int32_t count = (int32_t)atoi(p);
            if (count == 0) {
                fprintf(stderr,
                        "gousei: '%.*s' — '#0' count (engine: 'loop err 15')\n",
                        (int)line_len, line);
            }

            /* Engine: peek char at the post-'#' cursor. If EOL/EOF
             * before reaching a ':', fatal_err("loop err 15") — but
             * fatal_err is a no-op (FUN_0047aa31 returns immediately),
             * so the engine continues. Port treats `#` at EOL/EOF the
             * same way we treat `#count` at EOL/EOF below: write the
             * count (0) and finalise the field as if a ':' was there. */
            int hit_eol_in_count = (p >= p_end
                                    || *p == '\0'
                                    || *p == '\r'
                                    || *p == '\n');

            /* Engine: if (*p != ':') advance until *p == ':'. The
             * engine's scan is unbounded — on a line ending with
             * `#N\r\n` it walks past the line terminator into the
             * surrounding buffer hunting for ':'. Port stops at EOL/
             * EOF and treats that as an implicit field terminator. */
            if (!hit_eol_in_count && *p != ':') {
                while (p < p_end
                       && *p != ':'
                       && *p != '\r'
                       && *p != '\n'
                       && *p != '\0') {
                    p++;
                }
            }

            /* Write count to slot col (engine: only col 1..5; col 0
             * has no count so the engine silently ignores it for
             * column 0). */
            if (col >= 1 && col <= GOUSEI_INGREDIENT_COUNT) {
                write_ingredient_count(r, col, count);
            }

            /* If the count was followed by EOL/EOF (no ':' on this
             * line), synthesise a ':' fall-through: resolve the field
             * name we accumulated, advance col, then break out of the
             * loop. This is the bug-bypassing behaviour described in
             * the file header. */
            if (p >= p_end || *p == '\0' || *p == '\r' || *p == '\n') {
                if (field_len > 1) {
                    field_name[field_len] = '\0';
                    int32_t id = resolve_name(field_name, resolve, user);
                    if (col == 0) {
                        write_output_id(r, id);
                    } else if (col >= 1 && col <= GOUSEI_INGREDIENT_COUNT) {
                        write_ingredient_id(r, col, id);
                    }
                    if (id == GOUSEI_EMPTY && resolve != NULL) {
                        fprintf(stderr,
                                "gousei: '%s' — unknown item (col=%d)\n",
                                field_name, col);
                    }
                }
                break;
            }
            /* Fall through to the ':' handler below by NOT
             * consuming the ':'. */
        }

        /* `:` field separator. */
        if (*p == ':') {
            if (field_len > 1) {
                /* Resolve the accumulated field name. Engine path is
                 * gated on `1 < local_c` (i.e. >= 2 chars), so a
                 * single-char field name doesn't get resolved. */
                field_name[field_len] = '\0';
                int32_t id = resolve_name(field_name, resolve, user);

                if (col == 0) {
                    write_output_id(r, id);
                } else if (col >= 1 && col <= GOUSEI_INGREDIENT_COUNT) {
                    write_ingredient_id(r, col, id);
                }

                if (id == GOUSEI_EMPTY && resolve != NULL) {
                    /* Engine pops MessageBox on lookup miss (and also
                     * on index-0 match — see file header). Port logs.
                     * Suppressed when no resolver is wired up — that
                     * means item.txt's parser hasn't landed yet, and
                     * the misses are expected boilerplate rather than
                     * a real diagnostic. */
                    fprintf(stderr,
                            "gousei: '%s' — unknown item (col=%d)\n",
                            field_name, col);
                }
            }

            field_len     = 0;
            field_name[0] = '\0';
            col++;

            /* Stale-count probe: engine checks if the count slot for
             * the column we JUST wrote (now col-1) is still 0. We
             * mirror as a stderr log. The check guard `1 < local_18`
             * (after increment) limits to col 2..6 inclusive — i.e.
             * after writing ing1..ing5 ID, NOT after writing output
             * ID at column 0. Engine bug: the bounds check at
             * L2556 reads `ing_count[col]` (one slot too high), but
             * the practical effect is the same — it catches a stale
             * zero count when an ingredient was specified without a
             * `#N` qualifier. */
            if (col > 1 && col <= GOUSEI_INGREDIENT_COUNT + 1) {
                int slot = col - 1;
                if (r->ingredient_count[slot - 1] == 0
                    && r->ingredient_id[slot - 1] != GOUSEI_EMPTY) {
                    fprintf(stderr,
                            "gousei: '%.*s' — ing%d has no '#count'\n",
                            (int)line_len, line, slot);
                }
            }

            p++;
            scanned++;
            continue;
        }

        /* Default: accumulate into field_name. Engine: local_57c[c++]
         * with implicit NUL at c+1. Cap at FIELD_NAME_LEN - 1 chars. */
        if (field_len < (int)sizeof field_name - 1) {
            field_name[field_len++] = c;
            field_name[field_len]   = '\0';
        }
        p++;
        scanned++;
    }

    return 0;
}

void tables_parse_gousei(const unsigned char *data, size_t size,
                         gousei_state_t *out,
                         gousei_resolve_fn resolve, void *user)
{
    memset(out, 0, sizeof(*out));

    int32_t current_rank = 0;  /* engine local_24 */
    size_t  pos          = 0;

    char line[512];

    while (pos < size && data[pos] != '\0') {
        /* Collect one line into `line` (excluding EOL). */
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

        /* Comment / blank skip — engine: line[0] ∈ {'\r','\n','/'}.
         * With our buffer excluding EOL bytes, a blank line is llen==0. */
        if (llen == 0 || line[0] == '/') continue;

        /* Rank header — 7-byte "ランク:" prefix. */
        if (llen >= 7 && memcmp(line, k_rank_prefix, 7) == 0) {
            current_rank = (int32_t)atoi(line + 7);
            continue;
        }

        /* Recipe line. Refuse to write past the 200-record cap (engine
         * writes-then-warns; we draw the line earlier). */
        if (out->count >= GOUSEI_MAX_RECORDS) {
            fprintf(stderr,
                    "gousei: record cap %d reached "
                    "(engine warning: '合成アイテム登録オーバー') — "
                    "remaining lines dropped\n",
                    GOUSEI_MAX_RECORDS);
            break;
        }

        gousei_record_t *r = &out->records[out->count];
        if (parse_recipe_line(r, current_rank, line, llen,
                              resolve, user) == 0) {
            out->count++;
        }
        /* On malformed line we skip the increment (slot stays zeroed),
         * matching the engine's "warn and continue" stance for the
         * fatal_err sub-cases — minus the actual fatal_err call. */
    }
}
