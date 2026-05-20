/*
 * tables_snews.h — parser for `data/snews.txt` (block #12 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `snews.txt` (short news / "戦闘ニュース") defines the random in-dungeon
 * status broadcasts that appear at the start of certain floors: "SP
 * consumption halved!", "Adventurer movement speed increased!", and so
 * on. The file has two parts:
 *
 *   - A flat name table: `NNN:<text>` lines populate up to 64 named
 *     news entries by ID (0..63 written; engine reserves slots 0..63
 *     in a 0x44-byte stride array at `&DAT_073d8ee0`).
 *
 *   - Per-dungeon, per-floor-range weight tables: a SJIS dungeon-key
 *     line (`ダンジョン1`..`ダンジョン6`) selects an outer index 0..5;
 *     each subsequent `f:N-M` line starts a new section covering floors
 *     N..M (inclusive); each subsequent `NNN,W` or `NON,W` line adds an
 *     entry (news id, spawn weight) to the active section.
 *
 *     Sections live in a fixed array at `&DAT_073b2108` (stride 0xa8,
 *     30 sections × 10 outer slots — only 6 outer slots reachable via
 *     the keys). The consumer at FUN_004364bc reads
 *     `sections[dungeon][section]` for the first section whose
 *     `floor_start <= current_floor+1 <= floor_end`, then rolls a
 *     weighted draw across the 20-entry table.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_SNEWS_H
#define OPENRECET_TABLES_SNEWS_H

#include <stddef.h>
#include <stdint.h>

/* Total slots in the name table at `&DAT_073d8ee0`. The engine reserves
 * exactly 64 (stride 0x44, span 0x1100 bytes). */
#define SNEWS_NAME_COUNT 64

/* Bytes per name buffer (engine: 0x40 chars + 1 NUL fits within the
 * 0x44-byte entry once the leading 4-byte `active` flag is subtracted).
 * Engine write cap is 0x40 chars and can omit the NUL on overflow (a
 * 1-byte overrun into the next entry's `active` byte); port caps at
 * SNEWS_NAME_LEN-1 data chars and always NUL-terminates. */
#define SNEWS_NAME_LEN 64

/* Engine outer slot count (10 reserved) and how many are reachable via
 * the SJIS dungeon keys (6 used). The 4 unused slots remain in their
 * post-init state (all `floor_start == -1`, no entries). */
#define SNEWS_DUNGEON_SLOT_COUNT 10
#define SNEWS_DUNGEON_KEY_COUNT  6

/* Sections per dungeon (engine: inner counter `local_14` ranges 0..29
 * before the outer-loop advance). Engine has no parser-side cap on the
 * 30-section count; port silently drops further `f:` lines once full. */
#define SNEWS_SECTION_COUNT 30

/* Entries per section (engine: 20 sub-records of stride 8). */
#define SNEWS_ENTRY_COUNT 20

/* Sentinel values used by the engine's init pass. `id == SNEWS_EMPTY`
 * marks an unused entry slot (engine writes 0xffffffff to every id
 * field at init); `id == SNEWS_NON_ID` marks a "NON" sentinel entry
 * (engine: iVar1 = -2 in the "NON" match branch). */
#define SNEWS_EMPTY  (-1)
#define SNEWS_NON_ID (-2)

/*
 * One section entry — engine sub-record at section_base + 8 + k*8:
 *
 *   +0x00  id     (i32)  — news id (0..63), -2 for NON, -1 for empty slot
 *   +0x04  weight (i32)  — spawn weight (engine: undefined for empty slots)
 *
 * Engine init writes 0xffffffff to id but leaves weight uninitialised;
 * the port memsets the whole struct to zero first for safety.
 */
typedef struct {
    int32_t id;      /* +0x00 */
    int32_t weight;  /* +0x04 */
} snews_entry_t;     /* = 0x08 */

/*
 * One floor-range section — engine record at `&DAT_073b2108` + idx*0xa8:
 *
 *   +0x00  floor_start (i32)  — inclusive lower bound (engine sentinel: -1)
 *   +0x04  floor_end   (i32)  — inclusive upper bound (engine sentinel: -1)
 *   +0x08  entries[20] (8b)   — id/weight pairs
 *
 * Total: 0xa8 (168) bytes per section.
 */
typedef struct {
    int32_t       floor_start;                      /* +0x00 */
    int32_t       floor_end;                        /* +0x04 */
    snews_entry_t entries[SNEWS_ENTRY_COUNT];       /* +0x08 .. +0xa7 */
} snews_section_t;                                  /* = 0xa8 */

/*
 * One name-table entry — engine record at `&DAT_073d8ee0` + idx*0x44:
 *
 *   +0x00  active (i32)  — 1 if populated (engine init: 0)
 *   +0x04  name[0x40]    — text (engine: not guaranteed NUL-terminated
 *                          on >= 64-char overflow; port always NUL-terms)
 *
 * Total: 0x44 (68) bytes per entry.
 */
typedef struct {
    int32_t active;                  /* +0x00 */
    char    name[SNEWS_NAME_LEN];    /* +0x04 .. +0x43 */
} snews_name_t;                      /* = 0x44 */

_Static_assert(sizeof(snews_entry_t)   == 0x08,
               "snews_entry_t size must be 0x08");
_Static_assert(sizeof(snews_section_t) == 0xa8,
               "snews_section_t size must be 0xa8");
_Static_assert(sizeof(snews_name_t)    == 0x44,
               "snews_name_t size must be 0x44");
_Static_assert(offsetof(snews_section_t, floor_start) == 0x00,
               "snews_section_t.floor_start offset must be 0x00");
_Static_assert(offsetof(snews_section_t, floor_end)   == 0x04,
               "snews_section_t.floor_end offset must be 0x04");
_Static_assert(offsetof(snews_section_t, entries)     == 0x08,
               "snews_section_t.entries offset must be 0x08");

/*
 * Combined state. The engine stores `names` (and `name_count`) far from
 * `sections` in memory (different `.bss` regions); we pack them into one
 * struct for the port's convenience — no consumer depends on the engine's
 * exact memory placement of these globals.
 */
typedef struct {
    snews_name_t    names[SNEWS_NAME_COUNT];                                 /* DAT_073d8ee0 */
    int32_t         name_count;                                              /* DAT_073dddc4 */
    snews_section_t sections[SNEWS_DUNGEON_SLOT_COUNT][SNEWS_SECTION_COUNT]; /* DAT_073b2108 */
} snews_state_t;

/* Engine-global state, populated from src/tables.c. */
extern snews_state_t g_snews;

/*
 * Parse a snews.txt buffer into `*out`. Mirrors FUN_00475270 block
 * L2238..L2401 plus its LAB_00478e12 line-reader loop.
 *
 * Pre-conditions: `*out` need not be initialised; this function memsets
 * it before parsing. After return:
 *   - `names[id].active == 1` for every populated name; `name` is
 *     always NUL-terminated (port divergence — engine may omit NUL on
 *     64-char overflow).
 *   - `sections[d][s].floor_start == -1` for unwritten sections.
 *   - `sections[d][s].entries[k].id == -1` for unwritten entry slots.
 *   - `name_count` is incremented once per parsed name line (engine:
 *     `_DAT_073dddc4++` regardless of whether `active` was already 1).
 *
 * Line dispatch (engine: L2272..L2401):
 *   '/', blank   — comment / skipped
 *   `ダンジョン{1..6}` (12 SJIS bytes)  — switch active dungeon (0..5);
 *                                         resets sections_in_current
 *   `f:N-M`     — open new section: write (N, M) to the OLD section
 *                  pointer (engine quirk — see below), then advance to
 *                  sections[current_dungeon][sections_in_current++]
 *   `NON,W`     — append entry (id=-2, weight=W) to current section
 *   `NNN,W`     — append entry (id=atoi(NNN), weight=W)
 *   `NNN:<text>` (or NNN<sep><text>) — set name[id] = text (chars after
 *                  the 1-byte separator at line[3])
 *   anything else — engine: MessageBoxA "不明なニュース"; port: skip
 */
void tables_parse_snews(const unsigned char *data, size_t size,
                        snews_state_t *out);

#endif /* OPENRECET_TABLES_SNEWS_H */
