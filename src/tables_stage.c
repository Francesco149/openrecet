/*
 * tables_stage.c — `idx/stage.idx` parser.
 *
 * Source-level reference: FUN_00475270 block #1 in
 * docs/decompiled/by-address/475270.c (L55..L329 = header + record
 * default-init; L3174..L3957 = field-key dispatcher).
 *
 * Identifies the file via `s_idx_stage_idx_005ca748` (size) and
 * `s_idx_stage_idx_005ca758` (read); the two interned copies of
 * "idx/stage.idx" have the same spelling — no path-mismatch quirk
 * (unlike config.idx).
 *
 * Record layout, defaults, and the 57 field keys are documented in
 * docs/formats/data-text.md and tables_stage.h. Engine quirks
 * documented in docs/findings/engine-quirks.md (#34..#36).
 *
 * High-level structure:
 *
 *   1. Outer loop reads one line at a time into a 0x100-byte buffer
 *      (engine: `local_47c[0x100]`). Comments (`/…`), blank lines,
 *      and lines before the first `stage:X-Y` header are skipped.
 *
 *   2. `stage:X-Y` header → bumps `count`, sets `dungeon_id` for the
 *      newly-opened record, and reinitialises that record's defaults.
 *
 *   3. All other non-comment lines run through a sequential chain of
 *      57 prefix-match `if`s. Each tested key is a NUL-terminated
 *      literal (e.g. `"maptype:"`, `"map:"`, …). The chain is a *fall-
 *      through* — every key is tested on every line, but in practice
 *      each line matches at most one key because all keys terminate
 *      with `:` (or `_` for `maplight_*`) and the prefixes are
 *      disjoint up to that point. Multi-value parses bail out of the
 *      chain via the outer-loop continue once they hit EOL between
 *      sub-values; that early exit is benign because no later key
 *      could match anyway.
 *
 *   4. After the outer loop completes, the engine performs a block of
 *      unrelated game-state writes (player inventory defaults,
 *      `_DAT_0438cc6c` etc.) — these are NOT stage record state. The
 *      port elides them; they will move to a dedicated boot-state
 *      init when the surrounding subsystems get their own ports.
 */

#include "tables_stage.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

stage_state_t g_stage;

/* ── line buffer & helpers ────────────────────────────────────────── */

/* The engine reads a line into `local_47c[0x100]` (256 bytes). A line
 * that exceeds 0x100 - 1 data bytes gets truncated in-place (the loop
 * exits when `iVar6 == 0xff`, and the `local_47c[iVar6 + 1] = 0`
 * write at L78/L352 nul-terminates). Vendor lines stay well under
 * this — longest seen is ~80 bytes. Our buffer is sized to match. */
#define STAGE_LINE_CAP 0x100

/* Read one line from `data` starting at `*pos`, into `line[STAGE_LINE_CAP]`.
 * Advances `*pos` past the line's terminator (one \r or \n byte;
 * CRLF yields a subsequent empty line which the caller skips).
 * Returns the number of data bytes copied (excluding terminator
 * and NUL); writes a NUL at `line[returned_length]`. The line
 * NEVER overflows: at most STAGE_LINE_CAP - 1 data bytes are copied
 * and the NUL goes at index <= STAGE_LINE_CAP - 1. */
static size_t read_one_line(const unsigned char *data, size_t size,
                            size_t *pos, char line[STAGE_LINE_CAP])
{
    size_t llen = 0;
    while (*pos < size
           && data[*pos] != '\0'
           && data[*pos] != '\r'
           && data[*pos] != '\n'
           && llen + 1 < STAGE_LINE_CAP) {
        line[llen++] = (char)data[(*pos)++];
    }
    line[llen] = '\0';
    if (*pos < size && (data[*pos] == '\r' || data[*pos] == '\n'))
        (*pos)++;
    return llen;
}

/* Returns 1 if `line[0..keylen-1]` exactly equals `key`. `key` is the
 * key string (including trailing ':' for the colon-terminated keys;
 * `maplight_*` keys end in `r`/`g`/`b` and we compare on that). */
static int match_key(const char *line, size_t llen,
                     const char *key, size_t keylen)
{
    return llen >= keylen && memcmp(line, key, keylen) == 0;
}

/* Copy at most `max_bytes` data bytes from `src` into `dst`, stopping
 * at '\0', '\r', '\n', or `max_bytes` bytes copied. Always writes a
 * terminating NUL at dst[n]. Caller ensures dst has room for
 * max_bytes + 1 (NUL).
 *
 * Engine: the per-key string copy loops at L3189..L3196 (map),
 * L3209..L3216 (mapbg), etc., all stop at '\r'/'\n' and cap at
 * 0x100 iterations; they write the NUL in-place at the terminator
 * position. We do the same with one helper. */
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

/* Scan `p` forward until the byte after the next ':' separator.
 * Returns NULL if EOL is reached before finding a ':'. Engine
 * pattern at L3404..L3408 (and many copies): walks until ':',
 * then increments past it before the next atoi/atof. */
static const char *advance_after_colon(const char *p)
{
    while (*p != ':') {
        if (*p == '\0' || *p == '\r' || *p == '\n') return NULL;
        p++;
    }
    return p + 1;
}

/* Scan `p` forward until the byte after the next ' ' separator.
 * Same shape as advance_after_colon but for the maplight_* keys
 * whose paired values are space-separated (engine: L3607..L3610). */
static const char *advance_after_space(const char *p)
{
    while (*p != ' ') {
        if (*p == '\0' || *p == '\r' || *p == '\n') return NULL;
        p++;
    }
    return p + 1;
}

/* ── per-line parse helpers ───────────────────────────────────────── */

/* Pair of int32 separated by ':'. Engine pattern for startpos/
 * fogcolor/smokecolor/backcolor (3 values) — second/third writes
 * skipped if EOL hits between values. */
static void parse_int_triple(const char *p, int32_t out[3])
{
    out[0] = atoi(p);
    if (*p == '\0' || *p == '\r' || *p == '\n') return;
    const char *q = advance_after_colon(p);
    if (!q) return;
    out[1] = atoi(q);
    if (*q == '\0' || *q == '\r' || *q == '\n') return;
    /* Engine bug-for-bug match: after the second atoi, the pcVar16
     * pointer is already at the byte after the first ':'. The EOL
     * check above uses *q (i.e. the first byte of the second number),
     * not the byte where the second ':' would land. Then the second
     * scan walks from q (NOT q+1!) until the next ':'. Reproduce. */
    q = advance_after_colon(q);
    if (!q) return;
    out[2] = atoi(q);
}

/* Float triple, colon-separated. Engine pattern for lightdir/
 * lightcolor/lightamb. */
static void parse_float_triple_colon(const char *p, float out[3])
{
    out[0] = (float)atof(p);
    if (*p == '\0' || *p == '\r' || *p == '\n') return;
    const char *q = advance_after_colon(p);
    if (!q) return;
    out[1] = (float)atof(q);
    if (*q == '\0' || *q == '\r' || *q == '\n') return;
    q = advance_after_colon(q);
    if (!q) return;
    out[2] = (float)atof(q);
}

/* Float pair, colon-separated. Engine pattern for fog. */
static void parse_float_pair_colon(const char *p, float out[2])
{
    out[0] = (float)atof(p);
    if (*p == '\0' || *p == '\r' || *p == '\n') return;
    const char *q = advance_after_colon(p);
    if (!q) return;
    out[1] = (float)atof(q);
}

/* Float pair, space-separated. Engine pattern for maplight_dr/dg/db/
 * ar/ag/ab. */
static void parse_float_pair_space(const char *p, float out[2])
{
    out[0] = (float)atof(p);
    if (*p == '\0' || *p == '\r' || *p == '\n') return;
    const char *q = advance_after_space(p);
    if (!q) return;
    out[1] = (float)atof(q);
}

/* ── stage-key (`stage:X-Y`) dispatch ─────────────────────────────── */

/* Sequential prefix-match against the 21 stage IDs ("0-1".."1-16").
 * Returns the matched dungeon_id (0..20) or the engine's fallback
 * for unknown IDs (which is 0x14 = 20, i.e. "1-16" — see quirk #34
 * in docs/findings/engine-quirks.md).
 *
 * Engine: L93..L240 — `uVar5 = 0x14` is set as the chain's default,
 * then 21 length-prefixed compares overwrite it on match. The
 * first compare ("0-1") is special-cased: starts with `uVar5 = 0x14`
 * pre-loaded, so a successful "0-1" match writes 0, "0-2" writes 1,
 * etc., up to "1-16" writes 0x14 (which IS the default — meaning an
 * unknown ID is silently treated as the last stage).
 *
 * The 21 entries split into 3-byte ("0-1".."1-9") and 4-byte ("1-10"
 * .."1-16") keys. Engine compares each as a fixed length; the
 * 3-byte keys would technically also match the first 3 bytes of a
 * 4-byte input ("1-10" starts with "1-1") but the dispatch is
 * sequential and "1-1" comes BEFORE "1-10" in the chain — so for
 * "1-10" input, "1-1" matches first (writing 5), then "1-10" also
 * matches (overwriting with 0xe = 14)... actually since "1-10" is
 * the 16th key tested, and writes go to the same `uVar5` slot,
 * the LAST match wins. Last match in chain order for "1-10" input
 * is the "1-10" compare itself. Correct behavior. */
static int dispatch_stage_id(const char *id)
{
    /* 3-byte short IDs first (in chain order). */
    static const char *const k3[] = {
        "0-1", "0-2", "0-3", "0-4", "0-5",
        "1-1", "1-2", "1-3", "1-4", "1-5",
        "1-6", "1-7", "1-8", "1-9",
    };
    /* 4-byte long IDs ("1-10".."1-16"). */
    static const char *const k4[] = {
        "1-10", "1-11", "1-12", "1-13", "1-14", "1-15", "1-16",
    };

    /* Engine default — last entry's index, returned when nothing
     * matches (quirk #34). */
    int dungeon_id = 0x14;

    for (size_t i = 0; i < sizeof k3 / sizeof k3[0]; i++) {
        if (id[0] == k3[i][0] && id[1] == k3[i][1] && id[2] == k3[i][2])
            dungeon_id = (int)i;
    }
    for (size_t i = 0; i < sizeof k4 / sizeof k4[0]; i++) {
        if (id[0] == k4[i][0] && id[1] == k4[i][1]
            && id[2] == k4[i][2] && id[3] == k4[i][3])
            dungeon_id = (int)(i + 14);
    }
    return dungeon_id;
}

/* ── record-default init ──────────────────────────────────────────── */

/* Apply the engine's per-record default-init to `r` (L243..L312 of
 * the decomp). Most fields stay at 0; this writes the non-zero
 * defaults explicitly. */
static void stage_record_init_defaults(stage_record_t *r)
{
    memset(r, 0, sizeof *r);

    /* Floats default to 1.0f unless the engine writes them — listed
     * below. The defaults form the "no override" identity for the
     * stage's lighting / fog setup. */
    r->fog[0]            = 1.0f;
    r->fog[1]            = 1.0f;
    r->wateralpha        = 0x7f;        /* 127 */
    r->wateralpha_fish   = -1;
    r->farclip           = 600;
    r->wateranimnum      = 1;
    r->watersize         = 0x40;        /* 64 */
    r->wateranimspeed    = 4;
    r->smokecolor[0]     = 0xff;
    r->smokecolor[1]     = 0xcc;
    r->smokecolor[2]     = 0xb2;
    r->lightdir[0]       = 1.0f;
    r->lightdir[1]       = 1.0f;
    r->lightdir[2]       = 1.0f;
    /* maplight_d / maplight_a all default to 1.0f. */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 2; j++) {
            r->maplight_d[i][j] = 1.0f;
            r->maplight_a[i][j] = 1.0f;
        }
    }
    r->waterheight       = -1000.0f;    /* engine: 0xc47a0000 */
    r->deathheight       = -70;         /* engine: 0xffffffba (signed) */
    r->unk_b20           = 1;           /* flag of unknown purpose, init = 1 */
    /* All other fields stay at memset-0. */
}

/* ── field-key dispatch on one line ──────────────────────────────── */

/* Dispatch one non-comment, non-`stage:` line against the 57 field
 * keys. `line` is NUL-terminated; `llen` is its length. `r` is the
 * currently-open record (caller guarantees this is non-NULL since
 * the outer loop skips lines before any stage record opens). */
static void dispatch_field_line(const char *line, size_t llen,
                                stage_record_t *r)
{
    /* Helper aliases for compactness. Each block:
     *  - tests for an exact-prefix match of a literal key
     *  - if matched, parses the value out of the appropriate suffix
     *  - never `return`s — falls through to the next key, mirroring
     *    the engine's chain.
     *
     * For most keys the prefixes are disjoint and at most one block
     * fires per line. For multi-value keys we just write the parsed
     * values and continue; subsequent keys won't match the prefix
     * either, so no double-writes. */

    /* maptype:N  (int)                                              */
    if (match_key(line, llen, "maptype:", 8)) {
        r->maptype = atoi(line + 8);
    }

    /* map:<filename>  (slot string — append) */
    if (match_key(line, llen, "map:", 4)) {
        /* Engine appends to slot indexed by current map_count, with
         * no overflow check — a 21st `map:` line would clobber the
         * minimap field at +0x1714. Port stops writing past slot
         * STAGE_MAP_SLOTS-1 to keep ASan quiet; vendor max is ~12. */
        if (r->map_count >= 0 && r->map_count < STAGE_MAP_SLOTS) {
            copy_until_eol(r->map[r->map_count], line + 4,
                           STAGE_NAME_MAX - 1);
        }
        /* Engine bumps map_count UNCONDITIONALLY at L3197 (no slot
         * cap), then `*(int *)(&DAT_068ded24 + …) += 1`. The post-
         * loop game logic iterates `0..map_count` so silent overflow
         * would corrupt subsequent reads. We bump too. */
        r->map_count++;
    }

    /* mapbg:<filename>  (string, also sets mapbg_set=1)             */
    if (match_key(line, llen, "mapbg:", 6)) {
        copy_until_eol(r->mapbg, line + 6, STAGE_NAME_MAX - 1);
        r->mapbg_set = 1;
    }

    /* minimap:<filename>  (string)                                  */
    if (match_key(line, llen, "minimap:", 8)) {
        copy_until_eol(r->minimap, line + 8, STAGE_NAME_MAX - 1);
    }

    /* fishmap:<filename>  (string)                                  */
    if (match_key(line, llen, "fishmap:", 8)) {
        copy_until_eol(r->fishmap, line + 8, STAGE_NAME_MAX - 1);
    }

    /* mapcamera:<filename>  (slot string — append)                  */
    if (match_key(line, llen, "mapcamera:", 10)) {
        if (r->mapcamera_count >= 0
            && r->mapcamera_count < STAGE_MAPCAMERA_SLOTS) {
            copy_until_eol(r->mapcamera[r->mapcamera_count],
                           line + 10, STAGE_NAME_MAX - 1);
        }
        r->mapcamera_count++;
    }

    /* loopcamera:  (flag, no value)                                 */
    if (match_key(line, llen, "loopcamera:", 11)) {
        r->loopcamera = 1;
    }

    /* waterfile:<filename>  (string)                                */
    if (match_key(line, llen, "waterfile:", 10)) {
        copy_until_eol(r->waterfile, line + 10, STAGE_NAME_MAX - 1);
    }

    /* deathheight:N  (int — but stored as float? engine stores as
     * undefined4. Init at L296 is 0xffffffba (-70 as int32).
     * Engine: `*(undefined4 *)(&DAT_068dee14 + …) = uVar5;`
     * where `uVar5 = atoi(line+0xc)`. So stored as int32. */
    if (match_key(line, llen, "deathheight:", 12)) {
        r->deathheight = atoi(line + 12);
    }

    /* watersize:N         */
    if (match_key(line, llen, "watersize:", 10))    r->watersize = atoi(line + 10);
    /* wateranimnum:N      */
    if (match_key(line, llen, "wateranimnum:", 13)) r->wateranimnum = atoi(line + 13);
    /* wateranimspeed:N    */
    if (match_key(line, llen, "wateranimspeed:", 15)) r->wateranimspeed = atoi(line + 15);
    /* mapx:N (atoi→float) */
    if (match_key(line, llen, "mapx:", 5))          r->mapx = (float)atoi(line + 5);
    /* mapz:N (atoi→float) */
    if (match_key(line, llen, "mapz:", 5))          r->mapz = (float)atoi(line + 5);
    /* mapnumx:N           */
    if (match_key(line, llen, "mapnumx:", 8))       r->mapnumx = atoi(line + 8);
    /* mapnumz:N           */
    if (match_key(line, llen, "mapnumz:", 8))       r->mapnumz = atoi(line + 8);
    /* scroll:F (atof)     */
    if (match_key(line, llen, "scroll:", 7))        r->scroll = (float)atof(line + 7);
    /* mapposy:F (atof)    */
    if (match_key(line, llen, "mapposy:", 8))       r->mapposy = (float)atof(line + 8);

    /* fog:near:far  (float×2 colon)                                 */
    if (match_key(line, llen, "fog:", 4)) {
        parse_float_pair_colon(line + 4, r->fog);
    }

    /* startpos:X:Y:Z  (int×3 colon)                                 */
    if (match_key(line, llen, "startpos:", 9)) {
        parse_int_triple(line + 9, r->startpos);
    }

    /* fogcolor:R:G:B  (int×3 colon)                                 */
    if (match_key(line, llen, "fogcolor:", 9)) {
        parse_int_triple(line + 9, r->fogcolor);
    }

    /* gakecheck:  (flag, no value)                                  */
    if (match_key(line, llen, "gakecheck:", 10)) {
        r->gakecheck = 1;
    }

    /* smokecolor:R:G:B  (int×3 colon)                               */
    if (match_key(line, llen, "smokecolor:", 11)) {
        parse_int_triple(line + 11, r->smokecolor);
    }

    /* backcolor:R:G:B  (int×3 colon)                                */
    if (match_key(line, llen, "backcolor:", 10)) {
        parse_int_triple(line + 10, r->backcolor);
    }

    /* lightdir:X:Y:Z  (float×3 colon)                               */
    if (match_key(line, llen, "lightdir:", 9)) {
        parse_float_triple_colon(line + 9, r->lightdir);
    }

    /* lightcolor:R:G:B  (float×3 colon)                             */
    if (match_key(line, llen, "lightcolor:", 11)) {
        parse_float_triple_colon(line + 11, r->lightcolor);
    }

    /* chrlightoffset:F  (atof)                                      */
    if (match_key(line, llen, "chrlightoffset:", 15)) {
        r->chrlightoffset = (float)atof(line + 15);
    }

    /* maplight:N         */
    if (match_key(line, llen, "maplight:", 9))      r->maplight = atoi(line + 9);
    /* chrlight:N         */
    if (match_key(line, llen, "chrlight:", 9))      r->chrlight = atoi(line + 9);
    /* maplightspeed:F (atof) */
    if (match_key(line, llen, "maplightspeed:", 14)) r->maplightspeed = (float)atof(line + 14);

    /* maplight_d[r/g/b]:F F  (float×2 space)                        */
    if (match_key(line, llen, "maplight_dr:", 12)) parse_float_pair_space(line + 12, r->maplight_d[0]);
    if (match_key(line, llen, "maplight_dg:", 12)) parse_float_pair_space(line + 12, r->maplight_d[1]);
    if (match_key(line, llen, "maplight_db:", 12)) parse_float_pair_space(line + 12, r->maplight_d[2]);

    /* maplight_a[r/g/b]:F F  (float×2 space)                        */
    if (match_key(line, llen, "maplight_ar:", 12)) parse_float_pair_space(line + 12, r->maplight_a[0]);
    if (match_key(line, llen, "maplight_ag:", 12)) parse_float_pair_space(line + 12, r->maplight_a[1]);
    if (match_key(line, llen, "maplight_ab:", 12)) parse_float_pair_space(line + 12, r->maplight_a[2]);

    /* windlerf:    (flag)                                           */
    if (match_key(line, llen, "windlerf:", 9))    r->windlerf = 1;
    /* windbouble:  (flag)                                           */
    if (match_key(line, llen, "windbouble:", 11)) r->windbouble = 1;
    /* windsnow:    (flag)                                           */
    if (match_key(line, llen, "windsnow:", 9))    r->windsnow = 1;
    /* houshi:      (flag)                                           */
    if (match_key(line, llen, "houshi:", 7))      r->houshi = 1;
    /* windfire:    (flag)                                           */
    if (match_key(line, llen, "windfire:", 9))    r->windfire = 1;
    /* smallwater:  (flag)                                           */
    if (match_key(line, llen, "smallwater:", 11)) r->smallwater = 1;

    /* lightamb:R:G:B  (float×3 colon)                               */
    if (match_key(line, llen, "lightamb:", 9)) {
        parse_float_triple_colon(line + 9, r->lightamb);
    }

    /* sunpos:X:Y:Z  (float×3 colon, mode=1) — or sunpos:off (mode=0).
     *
     * Engine: tests for the LITERAL "sunpos:off" (10 chars) as a
     * special case before falling through to the numeric parse.
     * Match → sunpos_mode = 0; no value writes. */
    if (match_key(line, llen, "sunpos:", 7)) {
        if (match_key(line, llen, "sunpos:off", 10)) {
            r->sunpos_mode = STAGE_SUN_OFF;
        } else {
            parse_float_triple_colon(line + 7, r->sun_pos);
            r->sunpos_mode = STAGE_SUN_SUNPOS;
        }
    }

    /* sunset:X:Y:Z  (float×3 colon, mode=2) — or sunset:off (broken).
     *
     * Engine BUG (quirk #36): the "off" sentinel comparison checks
     * for the LITERAL "sunpos:off" (NOT "sunset:off"!) — both
     * interned copies in the binary at 0x005cab4c and 0x005cab80
     * spell "sunpos:off". A real `sunset:off` line therefore does
     * NOT short-circuit; it falls through to the numeric parse,
     * which atofs "off" → 0.0f for X, then walks past the (nonexistent)
     * ':' separator into uninitialised line-buffer bytes for Y and Z.
     * Dormant in vendor (no `sunset:` keys present at all). We
     * preserve the broken behavior — but only as far as is safe: we
     * still atof("off") → 0, but the subsequent colon walks stop at
     * EOL and leave Y/Z untouched (since the engine reads from
     * `local_47c` past the line terminator, our equivalent reads
     * stop at our buffer's NUL and leave the existing sun_pos[1]/[2]
     * values in place). */
    if (match_key(line, llen, "sunset:", 7)) {
        /* Engine compares against "sunpos:off" not "sunset:off" — so
         * this branch is effectively unreachable for well-formed
         * input. We reproduce the broken comparison so a test can
         * pin the behavior. */
        if (match_key(line, llen, "sunpos:off", 10)) {
            r->sunpos_mode = STAGE_SUN_OFF;
        } else {
            parse_float_triple_colon(line + 7, r->sun_pos);
            r->sunpos_mode = STAGE_SUN_SUNSET;
        }
    }

    /* moonpos:X:Y:Z  (float×3 colon, moonpos_set=1 once all 3 read).
     *
     * Engine quirk: writes the SAME sun_pos fields used by sunpos/
     * sunset (no separate moon coordinates) and sets a distinct
     * moonpos_set flag at +0x1a8c. It does NOT touch sunpos_mode —
     * so a stage with both `sunpos:` and `moonpos:` ends up with the
     * sun's coords AND mode=1, plus moonpos_set=1 (quirk #35). */
    if (match_key(line, llen, "moonpos:", 8)) {
        /* Engine writes X first unconditionally, then Y and Z only
         * if EOL hasn't hit between values. moonpos_set is only set
         * when all 3 values successfully read. */
        r->sun_pos[0] = (float)atof(line + 8);
        const char *p = line + 8;
        if (*p != '\0' && *p != '\r' && *p != '\n') {
            const char *q = advance_after_colon(p);
            if (q) {
                r->sun_pos[1] = (float)atof(q);
                if (*q != '\0' && *q != '\r' && *q != '\n') {
                    q = advance_after_colon(q);
                    if (q) {
                        r->sun_pos[2] = (float)atof(q);
                        r->moonpos_set = 1;
                    }
                }
            }
        }
    }

    /* waterheight:N  (atoi → float)                                 */
    if (match_key(line, llen, "waterheight:", 12)) {
        r->waterheight = (float)atoi(line + 12);
    }
    /* farclip:N                                                     */
    if (match_key(line, llen, "farclip:", 8))         r->farclip = atoi(line + 8);
    /* wateralpha:N                                                  */
    if (match_key(line, llen, "wateralpha:", 11))     r->wateralpha = atoi(line + 11);
    /* wateralpha_fish:N                                             */
    if (match_key(line, llen, "wateralpha_fish:", 16)) r->wateralpha_fish = atoi(line + 16);
    /* wateradd:N                                                    */
    if (match_key(line, llen, "wateradd:", 9))        r->wateradd = atoi(line + 9);
    /* waterdrawcode:N                                               */
    if (match_key(line, llen, "waterdrawcode:", 14))  r->waterdrawcode = atoi(line + 14);
    /* hikarialpha:N                                                 */
    if (match_key(line, llen, "hikarialpha:", 12))    r->hikarialpha = atoi(line + 12);
    /* hikariadd:N                                                   */
    if (match_key(line, llen, "hikariadd:", 10))      r->hikariadd = atoi(line + 10);
    /* hikaridrawcode:N                                              */
    if (match_key(line, llen, "hikaridrawcode:", 15)) r->hikaridrawcode = atoi(line + 15);
    /* drawcode:N                                                    */
    if (match_key(line, llen, "drawcode:", 9))        r->drawcode = atoi(line + 9);
    /* mapviewarea:N                                                 */
    if (match_key(line, llen, "mapviewarea:", 12))    r->mapviewarea = atoi(line + 12);
}

/* ── top-level parse ──────────────────────────────────────────────── */

void tables_parse_stage(const unsigned char *data, size_t size,
                        stage_state_t *out)
{
    memset(out, 0, sizeof *out);

    /* Engine: `local_10` starts at NULL pointer (the engine's
     * `(int *)0x0`); the outer-loop guard at L87 treats this as "no
     * record open yet". We use -1 for the same meaning. */
    int current = -1;

    char line[STAGE_LINE_CAP];
    size_t pos = 0;
    while (pos < size) {
        if (data[pos] == '\0') break;
        size_t llen = read_one_line(data, size, &pos, line);

        /* Comments / blank lines (engine L83: line[0] in '/', \r, \n).
         * Our read_one_line strips \r/\n into NUL, so an empty line
         * has llen == 0. */
        if (llen == 0 || line[0] == '/') continue;

        /* `stage:X-Y` header — opens (or re-opens) a record.
         *
         * Engine: prefix-match on "stage:" (6 bytes), then dispatch
         * on bytes [6..] against the 21-entry ID table. On match:
         *   - bump local_10 (++)
         *   - reset local_c (map slot counter) to 0
         *   - write uVar5 (dungeon_id) at +0x104
         *   - apply the 60+ default writes (L245..L312)
         *
         * Note the engine doesn't require any of "0-1".."1-16" to
         * match — the fallback `uVar5 = 0x14` opens a record even
         * for an unknown ID (quirk #34). */
        if (match_key(line, llen, "stage:", 6)) {
            /* Need at least "stage:X" worth of bytes. The 3-byte
             * IDs need line[6..8]; the 4-byte IDs need line[6..9].
             * Engine reads bytes 6,7,8 (and 9 for 4-byte keys)
             * even on a 6-byte line — it would read past the NUL
             * and likely fail every compare → fallback to 0x14.
             * Our read_one_line NUL-terminates at llen, so the
             * dispatcher's reads are at-most one byte past NUL —
             * still safe (line buffer has STAGE_LINE_CAP > llen+1).
             */
            int dungeon_id = dispatch_stage_id(line + 6);

            /* Cap at the table size. Engine has no cap — but only
             * STAGE_KEY_COUNT records can be safely populated; a 22nd
             * `stage:` header would overrun the record array. We
             * stop here. */
            if (out->count >= STAGE_KEY_COUNT) {
                fprintf(stderr,
                        "stage.idx: too many stage: headers (cap %d, line=\"%s\")\n",
                        STAGE_KEY_COUNT, line);
                /* Skip rest of file rather than risk OOB. */
                break;
            }
            current = out->count;
            out->count++;
            stage_record_t *r = &out->records[current];
            stage_record_init_defaults(r);
            r->dungeon_id = (int32_t)dungeon_id;
            continue;
        }

        /* Non-`stage:` lines before any `stage:` header are silently
         * skipped (engine L87 guards via `local_10 < 0`). */
        if (current < 0) continue;

        dispatch_field_line(line, llen, &out->records[current]);
    }
}
