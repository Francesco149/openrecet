/*
 * tables_event.c — `data/event.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #10 in
 * docs/decompiled/by-address/475270.c (L1521..L2235). Both interned
 * paths spell "data/event.txt" — no path-mismatch quirk.
 *
 * Per-record engine stride is 0x32 dwords = 200 bytes; categories sit
 * 0x4e20 bytes apart at &DAT_06a49b80 / &DAT_06a4e9a0 / &DAT_06a537c0 /
 * &DAT_06a585e0. The port mirrors the byte-for-byte field layout in
 * `event_record_t` so a `counts[cat]` integer + a `records[cat][slot]`
 * fan-out has the same offsets per record the consumer FUN_0045de68
 * walks (`piVar5 = &DAT_06a49c44 + cat*5000` then negative-offset
 * field reads at -0x31..-0x28 + pair walk at -0x28..-0x01).
 *
 * Engine quirks faithfully reproduced (cross-ref docs/findings/
 * engine-quirks.md):
 *
 *   - **Pre-baked record 0 of category 0.** Before any parsing, the
 *     engine hand-writes 16 dword fields into the very first slot of
 *     広場 and sets `local_4c[0] = 1`. So the first parsed 広場 line
 *     lands at slot 1, not slot 0. The seed describes a "default
 *     event" with id=0x0b, flag=1, prereq[0]=0xa3, time 0..1, day
 *     range (0,40), loop_min=0, decay=100000. The other three
 *     categories start empty (count=0).
 *
 *   - **Lines before any header dispatch to category 0.** The init
 *     leaves `local_18 = 0`, so a data line that appears before the
 *     first `広場`/`市場`/`教会`/`酒場` header is written into
 *     category 0. Vendor data has 広場 as the first header so this
 *     fall-through is never exercised in production.
 *
 *   - **Prereq's `-` promotes the whole field to -1.** The parser
 *     scans the prereq line char-by-char looking for `0`..`9`/`a`..`f`
 *     (lowercase only), `-`, or `:`. A `-` ANYWHERE in a field's tail
 *     sets the "this field is -1" sticky flag for the field's commit
 *     write. So `-1`, `-2`, `0-`, and `f-f` all commit as -1. The
 *     accumulated hex value is discarded when the flag is set.
 *
 *   - **Weekday-tag mismatch advances 1 byte (not 2).** The four
 *     known tokens 朝/昼/夕/夜 are each 2 bytes. On a 2-byte mismatch
 *     against all four, the parser advances the cursor by 1 byte and
 *     retries — NOT 2. This means an unknown 2-byte SJIS char gets
 *     scanned twice (once starting at its first byte, once at its
 *     second), which can produce a phantom match if the second byte
 *     of one char + first byte of the next happens to spell a known
 *     token. Vendor data sidesteps this with full-width-space padding
 *     (0x81 0x40) and well-formed tag sequences; the quirk is
 *     dormant.
 *
 *   - **Cap-by-cursor on tags + pairs.** Tag scan caps at 40 cursor
 *     steps (not 40 successful matches). Pair scan caps at 20 pairs.
 *     The pair-scan terminator is a `:` byte found inside the inner
 *     `,`/`:` scan; reaching the cap without a `:` terminator stops
 *     the loop without raising any error.
 *
 *   - **time_first / time_max defaults.** Both fields are 0 by
 *     memset; the parser writes time_first only on the FIRST tag
 *     match (engine: `if (local_c == 0)`) and time_max only when the
 *     new match index is strictly greater than the current value
 *     (engine: `if (DAT_x < iVar17)`). So a record whose tag-list
 *     has no recognised tokens keeps time_first=0, time_max=0 — the
 *     same as a record whose only tag was `朝`. The consumer is
 *     happy with this ambiguity; we don't paper over it in the port.
 *
 *   - **End-of-list sentinel.** After the main loop, the loader
 *     writes -1 to `records[cat][counts[cat]].id` for each category,
 *     giving the consumer FUN_0045de68 its loop terminator. This
 *     overrun-of-1 is benign because per-category capacity is 100
 *     and vendor data tops out at ~50 records in 広場.
 *
 * Safety divergences (documented, not present in the engine):
 *
 *   - **Line buffer bounded.** Port collects up to 511 bytes per line.
 *     Engine writes into local_27c (~604 bytes); vendor lines are
 *     under 100 bytes.
 *
 *   - **Per-category capacity enforced.** Engine doesn't bounds-check
 *     `local_4c[i] < 100`; an overflow would smash the next category's
 *     record region. The port caps at EVENT_RECORDS_PER_CATEGORY and
 *     logs to stderr.
 */

#include "tables_event.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

event_state_t g_event;

/* Layout invariants — keep the C struct byte-identical to the engine's
 * record so the negative-offset reads in FUN_0045de68 (and any future
 * direct-memory consumers we port) line up exactly. */
_Static_assert(offsetof(event_record_t, id)              == 0,   "event.id @ +0");
_Static_assert(offsetof(event_record_t, flag_on_trigger) == 4,   "event.flag @ +4");
_Static_assert(offsetof(event_record_t, prereq)          == 8,   "event.prereq @ +8");
_Static_assert(offsetof(event_record_t, time_first)      == 24,  "event.time_first @ +24");
_Static_assert(offsetof(event_record_t, time_max)        == 28,  "event.time_max @ +28");
_Static_assert(offsetof(event_record_t, day_pairs)       == 32,  "event.day_pairs @ +32");
_Static_assert(offsetof(event_record_t, loop_min)        == 192, "event.loop_min @ +192");
_Static_assert(offsetof(event_record_t, decay_or_max)    == 196, "event.decay_or_max @ +196");
_Static_assert(sizeof(event_record_t) == 200, "event_record_t is 50 dwords");

/* ── Category header tokens ──────────────────────────────────────────── */
static const uint8_t EVENT_HEADER_BYTES[EVENT_CATEGORY_COUNT][4] = {
    { 0x8d, 0x4c, 0x8f, 0xea },  /* 広場 — DAT_005cb014 */
    { 0x8e, 0x73, 0x8f, 0xea },  /* 市場 — DAT_005cb01c */
    { 0x8b, 0xb3, 0x89, 0xef },  /* 教会 — DAT_005cb024 */
    { 0x8e, 0xf0, 0x8f, 0xea },  /* 酒場 — DAT_005cb02c */
};

/* ── Weekday-of-day tokens (DAT_005cb054..005cb060) ─────────────────── */
static const uint8_t EVENT_TIME_TOKENS[4][2] = {
    { 0x92, 0xa9 },  /* 朝 — index 0 */
    { 0x92, 0x8b },  /* 昼 — index 1 */
    { 0x97, 0x5b },  /* 夕 — index 2 */
    { 0x96, 0xe9 },  /* 夜 — index 3 */
};

/* ── Pre-baked record 0 of category 0 ────────────────────────────────── */
static void seed_default_record(event_state_t *out)
{
    event_record_t *r = &out->records[EVENT_CAT_HIROBA][0];
    r->id              = 0xb;
    r->flag_on_trigger = 1;
    r->prereq[0]       = 0xa3;
    r->prereq[1]       = -1;
    r->prereq[2]       = -1;
    r->prereq[3]       = -1;
    r->time_first      = 0;
    r->time_max        = 1;
    r->day_pairs[0][0] = 0;
    r->day_pairs[0][1] = 0x28;  /* 40 */
    r->day_pairs[1][0] = -1;
    r->day_pairs[1][1] = -1;
    /* day_pairs[2..19] left at 0 — matches BSS init in engine; the
     * consumer stops at the first pair with start == -1, which is
     * pair[1] here, so the (0,0) padding never gets read. */
    r->loop_min      = 0;
    r->decay_or_max  = 100000;
    out->counts[EVENT_CAT_HIROBA] = 1;
}

/* Header match: returns category index 0..3 if `line` exactly starts
 * with one of the 4 location tags, else -1. Engine checks each tag in
 * turn (広場 → 市場 → 教会 → 酒場), so the cascade order matters only
 * for diagnostics. */
static int match_header(const char *line)
{
    for (int c = 0; c < EVENT_CATEGORY_COUNT; c++) {
        if (memcmp(line, EVENT_HEADER_BYTES[c], 4) == 0) return c;
    }
    return -1;
}

/* Convert a single hex digit to its 0..15 nibble value, or -1 on a
 * non-hex byte. Engine accepts lowercase only (its lookup string
 * "0123456789abcdef" has no uppercase form). */
static int hex_nibble(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

/* Parse the 4-prereq segment starting at `*pp` (positioned just past
 * the leading ':' that follows id-flag). Advances `*pp` past the last
 * `:` of the prereq block. Returns 1 on success, 0 if the scan hit a
 * line terminator before committing all 4 fields. */
static int parse_prereqs(const char **pp, event_record_t *r)
{
    const char *p = *pp;
    int fields = 0;
    int hex_acc = 0;
    int is_minus = 0;
    int steps = 0;
    while (fields < EVENT_PREREQ_COUNT && steps < 50) {
        unsigned char c = (unsigned char)*p;
        if (c == '\0' || c == '\r' || c == '\n') {
            /* Premature EOL — engine reads junk past the line tail in
             * this case; port stops cleanly. The remaining prereq
             * fields keep their memset-zero initial values. */
            *pp = p;
            return 0;
        }
        int n = hex_nibble(c);
        if (!is_minus && n >= 0) hex_acc = (hex_acc << 4) | n;
        if (c == '-') is_minus = 1;
        if (c == ':') {
            r->prereq[fields] = is_minus ? -1 : hex_acc;
            fields++;
            hex_acc = 0;
            is_minus = 0;
            if (fields == EVENT_PREREQ_COUNT) {
                p++;
                break;
            }
        }
        p++;
        steps++;
    }
    *pp = p;
    return fields == EVENT_PREREQ_COUNT;
}

/* Parse the weekday-of-day tag block starting at `*pp`. Advances `*pp`
 * past the terminating `:`. Updates r->time_first (on first match) and
 * r->time_max (max-so-far). Stops at `:` or after 40 cursor steps.
 *
 * Mirrors the engine's quirky 1-byte-on-mismatch advance: when none of
 * the 4 known tokens match the 2 bytes at the cursor, the cursor
 * advances by 1, not 2. */
static void parse_time_tags(const char **pp, event_record_t *r)
{
    const char *p = *pp;
    int matches = 0;
    int steps = 0;
    while (steps < 40) {
        unsigned char c = (unsigned char)*p;
        if (c == '\0' || c == '\r' || c == '\n') break;
        if (c == ':') { p++; break; }
        int matched_idx = -1;
        for (int t = 0; t < 4; t++) {
            if ((unsigned char)p[0] == EVENT_TIME_TOKENS[t][0]
             && (unsigned char)p[1] == EVENT_TIME_TOKENS[t][1]) {
                matched_idx = t;
                break;
            }
        }
        if (matched_idx >= 0) {
            if (matches == 0) r->time_first = matched_idx;
            if (matched_idx > r->time_max) r->time_max = matched_idx;
            matches++;
            p += 2;
            steps += 2;
        } else {
            p += 1;
            steps += 1;
        }
    }
    *pp = p;
}

/* Parse the trailing day-range pair list `start-end[, start-end]*:`.
 * Caller advances `*pp` to the first digit of the first pair. */
static void parse_day_pairs(const char **pp, event_record_t *r)
{
    /* Init all 20 pairs to (-1, -1) — engine writes this block before
     * the parse loop. */
    for (int i = 0; i < EVENT_DAY_PAIRS; i++) {
        r->day_pairs[i][0] = -1;
        r->day_pairs[i][1] = -1;
    }
    const char *p = *pp;
    for (int i = 0; i < EVENT_DAY_PAIRS; i++) {
        unsigned char c = (unsigned char)*p;
        if (c == '\0' || c == '\r' || c == '\n') break;
        r->day_pairs[i][0] = atoi(p);
        while (*p != '-' && *p != '\0' && *p != '\r' && *p != '\n') p++;
        if (*p != '-') break;
        p++;  /* skip '-' */
        r->day_pairs[i][1] = atoi(p);
        while (*p != ',' && *p != ':' && *p != '\0'
            && *p != '\r' && *p != '\n') p++;
        if (*p == ':') { p++; break; }
        if (*p != ',') break;
        p++;  /* skip ',' */
    }
    *pp = p;
}

/* Parse one data line — engine's data-line block at L1956..L2235. */
static void parse_data_line(const char *line, int cat, event_state_t *out)
{
    if (cat < 0 || cat >= EVENT_CATEGORY_COUNT) return;
    if (out->counts[cat] >= EVENT_RECORDS_PER_CATEGORY) {
        fprintf(stderr,
                "tables_event: category %d overflow (cap=%d) on '%s'\n",
                cat, EVENT_RECORDS_PER_CATEGORY, line);
        return;
    }
    event_record_t *r = &out->records[cat][out->counts[cat]];

    /* Field 0: id (atoi). */
    const char *p = line;
    r->id = atoi(p);
    while (*p != '-' && *p != '\0' && *p != '\r' && *p != '\n') p++;
    if (*p != '-') return;  /* malformed — no flag_on_trigger separator */
    p++;

    /* Field 1: flag_on_trigger (atoi after the '-'). */
    r->flag_on_trigger = atoi(p);
    while (*p != ':' && *p != '\0' && *p != '\r' && *p != '\n') p++;
    if (*p != ':') return;
    p++;

    /* Init the prereq slots and decay_or_max to their parser-side
     * defaults (matching the engine's pre-prereq-parse writes). */
    for (int i = 0; i < EVENT_PREREQ_COUNT; i++) r->prereq[i] = -1;
    r->decay_or_max = 0;
    r->time_first = 0;
    r->time_max = 0;

    /* Fields 2..5: 4 hex-or-FF prereqs separated by ':'. */
    if (!parse_prereqs(&p, r)) {
        /* Premature EOL — commit the partial record. The engine would
         * keep scanning; the port stops to avoid running off the line
         * buffer. */
        out->counts[cat]++;
        return;
    }

    /* Field 6: weekday tags up to ':'. */
    parse_time_tags(&p, r);

    /* Field 7: loop_min (atoi), then scan to next ':'. */
    r->loop_min = atoi(p);
    while (*p != ':' && *p != '\0' && *p != '\r' && *p != '\n') p++;
    if (*p == ':') p++;

    /* Field 8: day-range pairs, terminated by ':'. */
    parse_day_pairs(&p, r);

    out->counts[cat]++;
}

void tables_parse_event(const unsigned char *data, size_t size,
                        event_state_t *out)
{
    memset(out, 0, sizeof *out);
    seed_default_record(out);

    char line[512];
    size_t pos = 0;
    int current_cat = EVENT_CAT_HIROBA;  /* engine: local_18 = 0 */

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

        if (llen == 0) continue;
        if (line[0] == '/' || line[0] == '\r' || line[0] == '\n') continue;

        /* Header dispatch. The engine cascades 広場→市場→教会→酒場; a
         * miss falls through to the data-line branch using the prior
         * `local_18`. We replicate that with match_header(). */
        if (llen >= 4) {
            int cat = match_header(line);
            if (cat >= 0) {
                current_cat = cat;
                continue;
            }
        }

        parse_data_line(line, current_cat, out);
    }

    /* End-of-list sentinel: write -1 to the id field of the next-
     * available slot per category. Engine: 4 writes immediately after
     * the parse loop. */
    for (int c = 0; c < EVENT_CATEGORY_COUNT; c++) {
        int slot = out->counts[c];
        if (slot < EVENT_RECORDS_PER_CATEGORY) {
            out->records[c][slot].id = -1;
        }
    }
}
