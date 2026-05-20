/*
 * tables_news.c — `data/news.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #11 in
 * docs/decompiled/by-address/475270.c (L1583..L2236). Identifies the
 * file via `s_data_news_txt_005cb090` (size) and
 * `s_data_news_txt_005cb0a0` (read); both interned copies hold the
 * same spelling "data/news.txt", so there is no path-mismatch quirk.
 *
 * One global is populated: an array of 188-byte records at
 * &DAT_056e0e00, counted by `_DAT_06a46f88`.
 *
 * Engine quirks faithfully reproduced:
 *
 *   - Name buffer overflow (#27). The parser writes up to
 *     NEWS_NAME_PARSE_CAP (20) bytes at +0x80+, but the structural
 *     name field is only 16 bytes — bytes 16..19 spill into the
 *     `rate` field, and the NUL terminator lands at +0x80+name_len
 *     (potentially in price_lo / category). Vendor names are all
 *     <= 12 bytes so this is dormant. Port reproduces via a uint8_t*
 *     write into the record.
 *
 *   - Prefix-match by name length (#28). All three name lookups
 *     (special, category, item) use `FUN_00479f4d(name, candidate,
 *     name_len)` — i.e. `memcmp(name, candidate, name_len)` — which
 *     is a prefix-of-candidate match. A short name like "武" (2 bytes)
 *     would falsely match category "武器" / item "武器屋" / SJIS attr
 *     tag prefix "武". Vendor names always exactly equal their
 *     candidates so this is dormant; port preserves the engine's
 *     prefix-match semantics.
 *
 *   - "-" row leaves target_group at 0 (#29). The "-" data-row branch
 *     skips the `(+0xa8) = local_14` write that the non-"-" branch
 *     does. Records initialised via memset stay at target_group=0
 *     regardless of the most recent "対象者:" header. Vendor data
 *     uses "対象者,0" before its "-" rows so this is dormant.
 *
 *   - "-" row leaves days_lo / days_hi at 0 (#29 cont). The non-"-"
 *     branch inits these to -1 before the parse; the "-" branch never
 *     touches them, so they stay at BSS-zero. Consumers that look for
 *     -1 as "no days range" will see (0, 0) instead.
 *
 *   - Body trailing '\r' (#30). The line-read loop INCLUDES the
 *     terminating '\r' / '\n' character in the line buffer, and the
 *     body-copy loop only stops at '\0' / '\n' (not '\r'). So
 *     CRLF-terminated source lines produce bodies with a trailing '\r'
 *     byte just before the NUL. LF-only and EOF-terminated lines do
 *     not. Vendor file is CRLF so every body record has '\r' at the
 *     end. Port preserves this for byte-identical behaviour.
 *
 *   - Lookup chain precedence: "特殊" check → SJIS attr-tag mask →
 *     category resolver → item resolver. Each step is gated on the
 *     previous returning "no match" (the engine tests `iVar6 == 0` /
 *     `category == -1` between steps).
 *
 *   - Empty name vacuously matches anything (FUN_00479f4d returns 1
 *     when name_len == 0). Vendor data never produces empty names
 *     because the parser only enters the name-write loop for
 *     non-"-" rows where line[0] != ','. Port preserves the vacuous
 *     match for parity.
 *
 *   - "時期" parser ignores missing '-'. Engine: if there's no '-'
 *     in the line, the parser hits "loop err 6" and continues with
 *     period_end unchanged. Port matches.
 *
 *   - Period range default at file start: (0, 100). Engine: local_18=0,
 *     local_20=0x64 written before the first line.
 */

#include "tables_news.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tables_oder.h"   /* oder_attr_hash — engine FUN_0049e9a7 */

news_state_t g_news;

/* "対象者" — 6 bytes (3 SJIS chars). Header for target_group. */
static const unsigned char K_TARGET[6] = {
    0x91, 0xce, 0x8f, 0xdb, 0x8e, 0xd2
};

/* "時期" — 4 bytes. Header for period_start/period_end. */
static const unsigned char K_PERIOD[4] = {
    0x8e, 0x9e, 0x8a, 0xfa
};

/* "特殊" — 4 bytes. Special-attribute sentinel name. */
static const unsigned char K_SPECIAL[NEWS_SPECIAL_NAME_LEN] = {
    0x93, 0xc1, 0x8e, 0xea
};

/* Engine FUN_00479f4d: returns 1 iff name[0..name_len) == candidate
 * [0..name_len). `candidate` is a NUL-terminated C string in .data;
 * if it's shorter than name_len, the compare reads past its NUL
 * (typically into the next .data literal) and almost always
 * mismatches. We short-circuit on `strlen(candidate) < name_len` to
 * avoid the engine's out-of-bounds read while preserving the match
 * semantics (since the engine read would also mismatch in practice). */
static int prefix_match(const char *name, size_t name_len,
                        const unsigned char *candidate,
                        size_t candidate_len)
{
    if (name_len > candidate_len) return 0;
    return memcmp(name, candidate, name_len) == 0;
}

/* Advance `p` past chars that are not in {',', '\0', '\r', '\n'}.
 * Returns the pointer to the next comma, or to the terminator if
 * the line ended before a comma was found. */
static char *scan_to_comma(char *p)
{
    while (*p != '\0' && *p != '\r' && *p != '\n' && *p != ',') p++;
    return p;
}

/* Same as scan_to_comma but stops at '-' instead. */
static char *scan_to_dash(char *p)
{
    while (*p != '\0' && *p != '\r' && *p != '\n' && *p != '-') p++;
    return p;
}

/* Copy body text from `src` into `body` (size NEWS_BODY_LEN). Stops on
 * '\0' or '\n' — NOT '\r'. Always NUL-terminates. */
static void copy_body(char *body, const char *src)
{
    int i = 0;
    while (*src != '\0' && *src != '\n' && i < NEWS_BODY_LEN - 1) {
        body[i++] = *src++;
    }
    body[i] = '\0';
}

/* Resolve the name field of a data row to (attr_mask, category, item_id).
 * Mirrors L1625..L1675 of the engine block: "特殊" → attr=-1 / no
 * further lookup; else attr-tag hash; else category resolver; else
 * item resolver. */
static void resolve_name(news_record_t *rec, const char *name, size_t name_len,
                         news_category_resolve_fn cat_resolve,
                         news_item_resolve_fn item_resolve,
                         void *resolve_user)
{
    if (prefix_match(name, name_len, K_SPECIAL, NEWS_SPECIAL_NAME_LEN)) {
        /* "特殊" prefix → attr_mask = -1, skip attr/cat/item lookups. */
        rec->attr_mask = -1;
        return;
    }

    /* SJIS 4-byte attr tag. oder_attr_hash short-circuits on
     * strlen(name)<4 and returns the bitmask of the last matching tag
     * (or 0 if none). The name buffer is NUL-terminated at
     * name[name_len] (possibly overflowed into rate/etc — see #27 —
     * but the NUL is still in place for strlen to find). */
    rec->attr_mask = (int32_t)oder_attr_hash(name);
    if (rec->attr_mask != 0) return;

    /* Category resolver. */
    if (cat_resolve != NULL) {
        rec->category = cat_resolve(name, name_len, resolve_user);
    }
    if (rec->category != -1) return;

    /* Item resolver — last fallback. Engine MessageBoxA's "syn error"
     * if this also misses; port logs to stderr. */
    if (item_resolve != NULL) {
        rec->item_id = item_resolve(name, name_len, resolve_user);
    }
    if (rec->item_id == -1) {
        fprintf(stderr,
                "tables_news: syn error — unknown name '%.*s'\n",
                (int)name_len, name);
    }
}

void tables_parse_news(const unsigned char *data, size_t size,
                       news_state_t *out,
                       news_category_resolve_fn cat_resolve,
                       news_item_resolve_fn     item_resolve,
                       void *resolve_user)
{
    memset(out, 0, sizeof *out);

    /* Parser state mirrors engine locals at L1588..L1593:
     *   local_14 = target_group   (default 0)
     *   local_18 = period_start   (default 0)
     *   local_20 = period_end     (default 0x64 = 100)
     *
     * Note: target_group is sticky-but-only-applied-to-non-"-"-rows.
     * The engine writes local_14 to +0xa8 only inside the non-"-"
     * data-row branch (LAB_00478d0a) — see quirk #29. */
    int32_t target_group = 0;
    int32_t period_start = NEWS_PERIOD_START_DEFAULT;
    int32_t period_end   = NEWS_PERIOD_END_DEFAULT;

    /* Line buffer mirrors the engine's local_27c[0x20..] stack slot.
     * We include the terminating \r/\n in the buffer to match the
     * engine's "body copy doesn't stop at \r" quirk (#30). */
    char line[512];

    size_t pos = 0;
    while (pos < size && data[pos] != '\0') {
        /* Read one line into `line`. Engine: collect until '\r' / '\n' /
         * '\0', AND store the terminator char in the buffer.  */
        size_t llen = 0;
        while (pos < size
               && data[pos] != '\0'
               && llen + 2 < sizeof line) {
            char c = (char)data[pos++];
            line[llen++] = c;
            if (c == '\r' || c == '\n') break;
        }
        line[llen] = '\0';

        if (llen == 0) continue;

        /* Comment / blank-line skip (engine: line[0] in {/\r\n}). */
        if (line[0] == '/' || line[0] == '\r' || line[0] == '\n') continue;

        /* "対象者,N" — sticky target_group header. Engine reads atoi at
         * +7 (= 6 header bytes + 1 comma). */
        if (llen >= 6 && memcmp(line, K_TARGET, 6) == 0) {
            target_group = (int32_t)atoi(line + 7);
            continue;
        }

        /* "時期,A-B" — sticky period_start/period_end header. Engine
         * reads atoi at +5 (= 4 header bytes + 1 comma) for start, then
         * scans for '-' and atoi's for end. If '-' is missing, the
         * engine's `while (cVar11 != '-')` would walk past line end —
         * the port strchr's the in-buffer NUL and skips on miss. */
        if (llen >= 4 && memcmp(line, K_PERIOD, 4) == 0) {
            period_start = (int32_t)atoi(line + 5);
            char *dash = strchr(line + 5, '-');
            if (dash != NULL) {
                period_end = (int32_t)atoi(dash + 1);
            }
            continue;
        }

        /* Data row. Safety cap on the record count. */
        if (out->count >= NEWS_MAX_RECORDS) continue;
        news_record_t *rec = &out->records[out->count];

        if (line[0] == '-') {
            /* "-" generic-news row. Engine: category=-100, attr_mask=0;
             * skip first comma (the "-" name), then check next byte,
             * then skip second comma, then copy body. */
            rec->category  = NEWS_CATEGORY_DASH;
            rec->attr_mask = 0;

            char *p = line;
            p = scan_to_comma(p);
            if (*p != ',') continue;  /* loop_err_7 */

            /* Engine: peek pcVar16[1] — if EOL, "loop err 8".  */
            if (p[1] == '\0' || p[1] == '\r' || p[1] == '\n') continue;

            p++;
            p = scan_to_comma(p);
            if (*p != ',') continue;  /* defensive — engine has no
                                       * explicit check here, vendor
                                       * data always has 2 commas */

            copy_body(rec->body, p + 1);

            rec->period_start = period_start;
            rec->period_end   = period_end;
            /* target_group, days_lo, days_hi left at memset 0 — quirk #29 */
            out->count++;
            continue;
        }

        /* Non-"-" data row. */
        /* Parse name: up to NEWS_NAME_PARSE_CAP bytes, terminated by
         * ',', written via uint8_t* into the record so writes past
         * NEWS_NAME_LEN overflow into rate (engine quirk #27). */
        uint8_t *rec_bytes = (uint8_t *)rec;
        size_t name_len = 0;
        char *p = line;
        while (name_len < NEWS_NAME_PARSE_CAP
               && *p != ','
               && *p != '\0' && *p != '\r' && *p != '\n') {
            rec_bytes[0x80 + name_len] = (uint8_t)*p;
            name_len++;
            p++;
        }
        /* NUL terminator at name[name_len]. May overflow into rate
         * (name_len 16..19) or further (name_len == 20 → category's
         * first byte) — overwritten by later writes regardless. */
        rec_bytes[0x80 + name_len] = 0;

        /* Init the four -1 sentinels (engine: L1636..L1639). Must
         * happen AFTER the name write so name overflow's NUL doesn't
         * clobber them again. */
        rec->item_id  = -1;
        rec->category = -1;
        rec->days_lo  = -1;
        rec->days_hi  = -1;

        resolve_name(rec, (const char *)(rec_bytes + 0x80), name_len,
                     cat_resolve, item_resolve, resolve_user);

        /* Need a ',' to continue to rate. If we hit EOL during name
         * scan, the engine's downstream code would walk past line end —
         * the port stops cleanly. */
        if (*p != ',') continue;

        /* rate = atoi(p+1). */
        p++;
        rec->rate = (int32_t)atoi(p);
        if (*p == '\0' || *p == '\r' || *p == '\n') continue;  /* loop_err_9 */

        /* Skip to next ','. */
        p = scan_to_comma(p);
        if (*p != ',') continue;
        p++;

        /* price_lo = atoi. */
        rec->price_lo = (int32_t)atoi(p);
        if (*p == '\0' || *p == '\r' || *p == '\n') continue;  /* loop_err_10 */

        /* Skip to '-'. */
        p = scan_to_dash(p);
        if (*p != '-') continue;
        p++;

        /* price_hi = atoi. */
        rec->price_hi = (int32_t)atoi(p);
        if (*p == '\0' || *p == '\r' || *p == '\n') continue;  /* loop_err_11 */

        /* Skip to ','. */
        p = scan_to_comma(p);
        if (*p != ',') continue;
        p++;

        /* Peek next byte: if a digit, this is a days_lo-days_hi range;
         * if not, the body starts here. Engine: `(pcVar16[1] < '0') ||
         * ('9' < pcVar16[1])` — signed char compare. */
        if (*p >= '0' && *p <= '9') {
            rec->days_lo = (int32_t)atoi(p);
            if (*p == '\0' || *p == '\r' || *p == '\n') continue;  /* loop_err_12 */

            p = scan_to_dash(p);
            if (*p != '-') continue;
            p++;

            rec->days_hi = (int32_t)atoi(p);
            if (*p == '\0' || *p == '\r' || *p == '\n') continue;  /* loop_err_13 */

            p = scan_to_comma(p);
            if (*p != ',') continue;
            p++;
        }

        /* Body: copy from `p` until '\0' / '\n' (NOT '\r' — quirk #30). */
        copy_body(rec->body, p);

        rec->target_group = target_group;
        rec->period_start = period_start;
        rec->period_end   = period_end;
        out->count++;
    }
}
