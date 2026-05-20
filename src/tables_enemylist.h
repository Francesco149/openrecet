/*
 * tables_enemylist.h — parser for `data/enemylist.txt` (block #14 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `enemylist.txt` ships the per-dungeon, per-floor-range enemy roster.
 * Each entry is one of:
 *
 *   - **wisp drop**       `wispN:item-name`               (N ∈ 1..9)
 *   - **dungeon header**  `ダンジョン１` .. `ダンジョン６`   (sticky)
 *   - **floor header**    `f:N`   or   `f:N-M`           (sticky)
 *   - **enemy line**      `<name>[(<v>)][x<n>]:<d1>[#<d2>[#<d3>]]`
 *   - **comment**         line[0] in {`/`, `\r`, `\n`}
 *
 * Engine globals populated:
 *
 *   - `&DAT_0053f8e8` — 10 × 60 grid of 752-byte floor-range
 *     sections. Engine reserves 10 dungeon slots but only addresses
 *     6 of them (via the SJIS dungeon-key compare); the remaining 4
 *     stay at their post-init -1 sentinel.
 *
 *   - `&DAT_073d8630` — 10 dwords of wisp item ids (`wispN:` →
 *     index N-1, default -1).
 *
 * Pure C, no Win32 surface, so this module compiles under host gcc
 * for unit testing alongside the rest of `tables_*`.
 */

#ifndef OPENRECET_TABLES_ENEMYLIST_H
#define OPENRECET_TABLES_ENEMYLIST_H

#include <stddef.h>
#include <stdint.h>

#include "tables_enemy.h"   /* enemy_record_t for the name-lookup table */
#include "tables_item.h"    /* item_state_t for the drop/wisp resolver */

/* The engine reserves slots for 10 dungeons; only 6 (`ダンジョン１..６`)
 * are addressable via the parser's prefix-compare chain at L2690..L2702.
 * Slots 6..9 are pre-initialised but never written by vendor data. */
#define ENEMYLIST_DUNGEON_SLOTS   10
#define ENEMYLIST_DUNGEON_KEYS    6

/* Each dungeon holds up to 60 floor-range sections, indexed by the
 * order in which `f:` headers appear after the dungeon's title line.
 * Engine: `iVar6 = local_20 * 0x3c + local_14` at L2848. */
#define ENEMYLIST_SECTIONS_PER_DUNGEON 60

/* Per-section enemy slot count. The init loop at L2598..L2606 writes
 * 31 sentinel `-1`s into the `enemy_id` field of slots 0..30. The
 * overflow check at L2842 caps the WRITABLE count at 30 — slot 30
 * exists only to receive the post-line terminator write. So
 * realistically 30 enemies per floor block, with slot 30 reserved.
 *
 * **Engine hazard:** when a vendor file actually pushes 30 enemies
 * onto a single `f:` block, the post-line terminator write would
 * land at slot 31's `enemy_id` field — which is the FIRST drop
 * dword of slot 0 (see layout below). Dormant in vendor; no `f:`
 * block has more than ~12 enemies. The port preserves the engine
 * behaviour for fidelity (and the overflow MessageBoxA at L2843
 * for diagnostics; we route it to stderr). */
#define ENEMYLIST_ENEMY_SLOTS_PER_SECTION 31
#define ENEMYLIST_ENEMY_WRITABLE_LIMIT    30

/* Per-enemy drop count. Engine breaks the drop scan when `local_28 == 3`
 * (L2835), so each enemy has at most 3 drop ids. */
#define ENEMYLIST_DROPS_PER_ENEMY 3

/* `wispN:` ranges over N=1..9 — engine's name-copy loop at L2641
 * reads from `line[0x26]` (the 7th char after the line start), which
 * is the digit-after position for single-digit `wisp1..wisp9` but
 * lands on `:` for two-digit `wisp10` → empty name → silent drop.
 * Init at L2608..L2612 still reserves 10 slots. */
#define ENEMYLIST_WISP_SLOTS 10

/* Engine per-line cap from the line-read loop (`pcVar18 + 0x100`).
 * Vendor lines stay well under this. */
#define ENEMYLIST_LINE_CAP 0x100

/* Per-drop scratch buffer size — engine `local_5c` cap is 0x20. */
#define ENEMYLIST_DROP_NAME_LEN 33  /* 0x20 chars + NUL */

/* Per-wisp item-name scratch buffer — engine reuses `local_27c` from
 * offset 0, cap 0x100. */
#define ENEMYLIST_WISP_NAME_LEN ENEMYLIST_LINE_CAP

/*
 * One enemy spawn slot (3 dwords). Memory layout, packed into the
 * 752-byte section starting at `&DAT_0053f8e8 + section_idx * 0x2f0`:
 *
 *   +0x00  dword[0]              section.floor_lo (atoi(line+2)-1)
 *   +0x04  dword[1]              section.floor_hi (atoi after '-' - 1)
 *   +0x08  dword[2..4]           enemies[0]   = { enemy_id, variant, count }
 *   +0x14  dword[5..7]           enemies[1]
 *   ...
 *   +0xb8  dword[44..46]         enemies[14]
 *   ...
 *   +0x174 dword[92..94]         enemies[30]   (last slot — terminator-only)
 *   +0x17c dword[95..97]         drops[0]      = { item_id[0..2] }
 *   +0x188 dword[98..100]        drops[1]
 *   ...
 *   +0x2e8 dword[185..187]       drops[30]
 *
 * Total: 2 + 31×3 + 31×3 = 188 dwords = 752 bytes (= 0x2f0).
 *
 * Engine writes:
 *
 *   enemy_id at piVar4[slot*3 + 2]   ← matches enemies[slot].enemy_id
 *   variant  at piVar4[(slot+1)*3]   ← matches enemies[slot].variant
 *   count    at piVar4[slot*3 + 4]   ← matches enemies[slot].count
 *   drop[k]  at piVar4[slot*3 + 0x5f + k] ← matches drops[slot].item_id[k]
 *
 * Init pre-fills the enemy_id of every slot 0..30 with -1 and the
 * floor_lo with -1.  variant defaults to 0 (set per-line). count
 * defaults to 1.  Drop ids default to -1 (per-line reset).
 */
typedef struct {
    int32_t enemy_id;       /* +0x00: -1 = empty */
    int32_t variant;        /* +0x04: from "(N)" suffix; 0 default */
    int32_t count;          /* +0x08: from "xN" suffix; 1 default */
} enemylist_enemy_t;        /* = 0x0c */

typedef struct {
    int32_t item_id[ENEMYLIST_DROPS_PER_ENEMY]; /* drop1/2/3; -1 = empty */
} enemylist_drops_t;         /* = 0x0c */

typedef struct {
    int32_t floor_lo;       /* +0x000 */
    int32_t floor_hi;       /* +0x004 */
    enemylist_enemy_t enemies[ENEMYLIST_ENEMY_SLOTS_PER_SECTION]; /* +0x008..+0x174 */
    enemylist_drops_t drops[ENEMYLIST_ENEMY_SLOTS_PER_SECTION];   /* +0x17c..+0x2e8 */
} enemylist_section_t;       /* = 0x2f0 (752 bytes) */

_Static_assert(sizeof(enemylist_enemy_t)   == 0x0c,  "enemy slot must be 12B");
_Static_assert(sizeof(enemylist_drops_t)   == 0x0c,  "drops slot must be 12B");
_Static_assert(sizeof(enemylist_section_t) == 0x2f0, "section must be 752B");
_Static_assert(offsetof(enemylist_section_t, floor_lo) == 0x000, "floor_lo @ 0x000");
_Static_assert(offsetof(enemylist_section_t, floor_hi) == 0x004, "floor_hi @ 0x004");
_Static_assert(offsetof(enemylist_section_t, enemies)  == 0x008, "enemies @ 0x008");
_Static_assert(offsetof(enemylist_section_t, drops)    == 0x17c, "drops @ 0x17c");

/*
 * Aggregate state populated by `tables_parse_enemylist`.
 *
 * Mirrors the two engine globals:
 *   sections    ↔ &DAT_0053f8e8
 *   wisp_drops  ↔ &DAT_073d8630
 *
 * `section_counts` is a per-dungeon count of populated `f:` blocks —
 * not present in the engine; the engine just walks all 60 slots and
 * checks `floor_lo != -1`. We track the count to keep tests / boot
 * trace concise.
 */
typedef struct {
    enemylist_section_t sections[ENEMYLIST_DUNGEON_SLOTS]
                                [ENEMYLIST_SECTIONS_PER_DUNGEON];
    int32_t  wisp_drops[ENEMYLIST_WISP_SLOTS];
    int16_t  section_counts[ENEMYLIST_DUNGEON_SLOTS];  /* convenience */
} enemylist_state_t;

extern enemylist_state_t g_enemylist;

/*
 * Name → item-id resolver callback (drops and wisp items both use it).
 *
 * The engine resolves item names against the `item.txt`-populated
 * table at `&DAT_095d381a` (stride 0x2cc, count `_DAT_005c80ac`) via
 * an effectively exact-string match — the loop does `memcmp` twice,
 * once with each side's strlen, so a hit requires both lengths AND
 * all bytes equal. We expose this through the standard
 * `(name, user) -> id` callback used by enemy.txt and gousei.txt.
 *
 * `name` is a NUL-terminated SJIS string from the line buffer.
 * `user` is the opaque value passed through `tables_parse_enemylist`.
 *
 * Returns the resolved item id (>= 0) on success, or -1 on miss /
 * "table not loaded yet".  Passing a NULL resolver to the parser
 * collapses every drop and wisp to -1.
 */
typedef int32_t (*enemylist_resolve_fn)(const char *name, void *user);

/*
 * Parse an enemylist.txt buffer into `out`.
 *
 * `enemy_names` must point to the pre-baked enemy-name table the
 * parser uses for the per-line longest-prefix lookup. Pass `g_enemy`
 * after `tables_enemy_init`; the parser only reads `name` and
 * `flags` (it stops walking at the first `flags == 2` sentinel,
 * matching the engine's loop condition at L2709). Lines whose
 * leading bytes do not prefix-match any record are silently
 * dropped — the engine would pop MessageBoxA "無効な敵ネーム"; the
 * port logs to stderr if `verbose_unknowns` is non-zero (default
 * 0 keeps the test output clean).
 *
 * Cross-table dependency: drop-name and wisp-name resolution is
 * delegated to `resolve` against `user`. tables.c binds this to
 * `tables_item_resolve` against `g_item` (populated earlier in the
 * load order by `load_item_txt`). A NULL resolver yields -1 for
 * every drop and wisp.
 *
 * `out` is fully zero-initialised first, then floor_lo of every
 * section + enemy_id of every enemy slot + every wisp_drop is set
 * to -1 (matches the engine's init loop at L2592..L2612).
 */
void tables_parse_enemylist(const unsigned char *data, size_t size,
                            enemylist_state_t *out,
                            const enemy_record_t *enemy_names,
                            int                  enemy_names_count,
                            enemylist_resolve_fn resolve,
                            void                *user);

#endif /* OPENRECET_TABLES_ENEMYLIST_H */
