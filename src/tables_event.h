/*
 * tables_event.h — parser for `data/event.txt` (block #10 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `event.txt` defines the in-town vignette-trigger table: each record
 * is a short event that can fire when the player visits one of the
 * four town locations 広場 (plaza), 市場 (market), 教会 (church), or
 * 酒場 (saloon) at a particular day/time/loop combination, given the
 * required prerequisite-flag set.
 *
 * Per-line shape (vendor file):
 *
 *     広場                                       ← location header (one of 4)
 *     14- 4:  100:-1:-1:-1  :朝昼　　:  0:  3-9,36-999:   //comment
 *      \  \    \ \  \  \    \         \   \
 *       \  \    \ \  \  \    \         \   loop_min (atoi)
 *        \  \    \ \  \  \    \         weekday-of-day mask (朝/昼/夕/夜 → 0/1/2/3)
 *         \  \    \ \  \  \    prereq[3] (hex or "-1")
 *          \  \    \ \  \  prereq[2]
 *           \  \    \ \  prereq[1]
 *            \  \    \ prereq[0]   ← first numeric after the leading id/flag pair
 *             \  flag_on_trigger (atoi after '-')
 *              id (atoi)
 *
 *     // followed by 1..20 day-range pairs `start-end[, start-end]*` and
 *     // terminating ':'.
 *
 * Lines starting with `/`, `\r`, or `\n` are comments / blanks. Lines
 * that don't match a known header keep the current category — except
 * the very first chunk of the file is dispatched to category 0 (広場)
 * by default (engine: `local_18 = 0` at init).
 *
 * Pure C, no Win32 surface, so this module compiles under host gcc for
 * unit testing.
 */

#ifndef OPENRECET_TABLES_EVENT_H
#define OPENRECET_TABLES_EVENT_H

#include <stddef.h>
#include <stdint.h>

/* The four in-town locations the parser dispatches on. Engine reference:
 * the four 4-byte SJIS literals at &DAT_005cb014..&DAT_005cb02c. */
#define EVENT_CATEGORY_COUNT  4
#define EVENT_CAT_HIROBA  0  /* 広場 — plaza */
#define EVENT_CAT_ICHIBA  1  /* 市場 — market */
#define EVENT_CAT_KYOKAI  2  /* 教会 — church */
#define EVENT_CAT_SAKABA  3  /* 酒場 — saloon */

/* Engine record layout: 100 records per category at stride 0x32 ints =
 * 200 bytes each = 20000 bytes per category. Categories sit at a
 * 0x4e20-byte stride from each other (DAT_06a49b80 / DAT_06a4e9a0 /
 * DAT_06a537c0 / DAT_06a585e0). */
#define EVENT_RECORDS_PER_CATEGORY 100

/* Per-record prereq slots; one byte/hex-value or "-1" each. */
#define EVENT_PREREQ_COUNT 4

/* Per-record day-range pair slots (`start-end`). Engine inits all 20
 * pairs to (-1,-1) before parsing and stops the parse loop at either
 * the 20th pair or the line-terminating ':'. The consumer iterates
 * pairs until it sees `start == -1`. */
#define EVENT_DAY_PAIRS 20

/* Day-end sentinel — the consumer treats `end == 999` as "no upper
 * bound on the calendar day index". */
#define EVENT_DAY_END_ALL 999

/* Weekday-of-day token indices (parser side). The line carries up to
 * a few of these 2-byte SJIS tokens; the parser tracks both the FIRST
 * token's index (`time_first`) and the MAX index seen (`time_max`),
 * mirroring the engine's `if (local_c == 0)` first-write gate and the
 * `if (DAT_x < iVar17)` max-write conditional. The consumer then
 * gates the event with `time_first <= current_tod <= time_max`.
 *
 * This is NOT the same encoding as kyaku.txt's `活動時間:` mask, which
 * uses bits 1/2/4/8 — event.txt stores plain 0..3 indices, and tokens
 * are matched in cursor order rather than position-independently. */
#define EVENT_TIME_MORNING 0  /* 朝 0x92 0xA9 */
#define EVENT_TIME_NOON    1  /* 昼 0x92 0x8B */
#define EVENT_TIME_EVENING 2  /* 夕 0x97 0x5B */
#define EVENT_TIME_NIGHT   3  /* 夜 0x96 0xE9 */

/*
 * One event record. Layout matches the engine's 50-dword record byte-
 * for-byte (32-bit ints, little-endian). The named offsets in comments
 * are relative to the per-record start; the per-category base addresses
 * are listed in the module header.
 */
typedef struct {
    int32_t  id;                          /* +0   atoi of first field. -1 = end-of-list sentinel. */
    int32_t  flag_on_trigger;             /* +4   atoi after '-'; flag the engine sets when the event fires. */
    int32_t  prereq[EVENT_PREREQ_COUNT];  /* +8..+20  prereq flag indices, or -1 if the field begins with '-'. */
    int32_t  time_first;                  /* +24  first weekday-of-day index matched in this record's line. */
    int32_t  time_max;                    /* +28  max weekday-of-day index matched in this record's line. */
    int32_t  day_pairs[EVENT_DAY_PAIRS][2]; /* +32..+188  (start, end) per pair; (-1,-1) = unused slot. */
    int32_t  loop_min;                    /* +192 atoi just before the floor-pair list; minimum required loop count. */
    int32_t  decay_or_max;                /* +196 always 0 for parsed records; pre-baked seed has 100000. Tested as `+196 <= some-counter` by the consumer FUN_0045de68. */
} event_record_t;

typedef struct {
    /* Engine layout: 4 contiguous 20000-byte regions at the four
     * &DAT_06a49b80 / b4e9a0 / 537c0 / 585e0 bases. We mirror it as a
     * 2D array; the parser writes to records[cat][counts[cat]++] and
     * caps writes at EVENT_RECORDS_PER_CATEGORY. */
    event_record_t records[EVENT_CATEGORY_COUNT][EVENT_RECORDS_PER_CATEGORY];
    /* Count of populated records per category — engine's `local_4c[i]`.
     * `records[cat][counts[cat]].id == -1` after parsing (sentinel). */
    int32_t        counts[EVENT_CATEGORY_COUNT];
} event_state_t;

/* Engine-global state, populated from src/tables.c. */
extern event_state_t g_event;

/*
 * Parse an event.txt buffer into `*out`. Mirrors FUN_00475270 block
 * #10 (L1521..L2235 of docs/decompiled/by-address/475270.c).
 *
 * Pre-conditions: `*out` need not be initialised; this function memsets
 * it before parsing, then seeds the pre-baked record 0 of category 0
 * (the "default 広場 event" the engine hard-codes at boot) before
 * processing the file content.
 *
 * Pre-baked record 0 of category 0 (DAT_06a49b80..DAT_06a49c44 init):
 *   id=0x0b, flag_on_trigger=1, prereq={0xa3,-1,-1,-1},
 *   time_first=0, time_max=1, day_pairs[0]=(0,40), pairs[1..]=(-1,-1),
 *   loop_min=0, decay_or_max=100000.
 * After init, `counts[0] = 1` — the first parsed record under 広場
 * lands at records[0][1], not [0][0].
 *
 * Line dispatch (engine):
 *   '/', '\r', '\n'  — comment / blank → skipped
 *   `広場\n` / `市場\n` / `教会\n` / `酒場\n`  — set current category
 *   other            — parse as data line for the current category
 *
 * Data-line shape: `ID-FLAG: HEX:HEX:HEX:HEX :TIMETAGS: LOOP : RANGES :`
 *   - ID, FLAG, LOOP: atoi (decimal — the CRT one)
 *   - HEX prereqs: lowercase 0..9/a..f accumulated as a single hex
 *     value; a `-` anywhere in the field promotes it to -1 (i.e. `-1`,
 *     `-2`, `f-f` all parse to -1). `:` terminates each field; 4
 *     fields per line, scan capped at 50 chars.
 *   - TIMETAGS: cursor scan up to 40 chars or until `:`; each 2-byte
 *     match against 朝/昼/夕/夜 (in that order) sets index 0..3 →
 *     `time_first` = first match, `time_max` = max match. Tokens not in
 *     the set advance the cursor 1 byte (NOT 2 — quirk).
 *   - RANGES: 1..20 `start-end` pairs separated by `,`; the list is
 *     terminated by `:`. start/end are atoi, end may be 999 (wildcard).
 *
 * After consuming the whole file, the loader writes -1 to the `id`
 * field of `records[cat][counts[cat]]` for each category, matching the
 * end-of-list sentinel the consumer FUN_0045de68 looks for.
 */
void tables_parse_event(const unsigned char *data, size_t size,
                        event_state_t *out);

#endif /* OPENRECET_TABLES_EVENT_H */
