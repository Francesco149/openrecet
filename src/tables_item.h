/*
 * tables_item.h — parser for `data/item.txt` (block #3 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `item.txt` is the master item catalog: ~600 records of weapons,
 * armour, accessories, consumables, food, books, furniture, etc.,
 * grouped by 100-block into ~25 categories. Every other gameplay
 * table that references an "item" by name (oder.txt's attribute
 * resolver, enemy.txt's drop fields, gousei.txt's recipes) ultimately
 * resolves through the table this parser builds.
 *
 * Per-line shape (vendor file, with annotation):
 *
 *     :Swords#(Equippable)                   ← category header
 *     0000:Sword#-1#0#0#0#0# #...##...       ← item record id 0000
 *     0001:1#Worn Sword+Worn Swords#200#8#0#0#0# 金属地味 #在庫(1)#  ##desc
 *     ...
 *     :Daggers#(Equippable)                  ← next category header
 *     0100:0#Knife+Knives#...
 *
 * Source-level reference: `docs/decompiled/by-address/475270.c`
 * L428..L468 (item.txt main loop) + L815..L829 (cross-block fallback
 * reached via `goto LAB_00476d04`). Record parser is `FUN_004912de`
 * (`docs/decompiled/by-address/4912de.c`).
 *
 * See `docs/findings/item-table.md` for the full dispatcher chain,
 * record layout justification, and helper-function map.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_ITEM_H
#define OPENRECET_TABLES_ITEM_H

#include <stddef.h>
#include <stdint.h>

/* Per-record stride matches the engine's `slot * 0x2cc` indexing in
 * `FUN_004912de`. Engine never caps slots in the dispatcher, but
 * vendor data uses ~600. The port reserves 1000 slots (716 KB of
 * record memory) — comfortable headroom without going overboard. */
#define ITEM_MAX_RECORDS 1000

/* Category count = max(item_id) / 100 + 1. Item IDs are bounded
 * 0..9999 by the dispatcher's `iVar6 < 10000` check, so the engine's
 * implicit cap is 100. Vendor data populates ~25. */
#define ITEM_CATEGORY_COUNT 100

/* Per-field column widths within a record. These match the byte
 * spans between the engine's named write offsets (e.g. +0x4a..+0x89
 * for singular name = 64 bytes). NUL-terminated by the engine. */
#define ITEM_NAME_LEN     64
#define ITEM_DESC_LEN     256
#define ITEM_STOCK_FIELDS 9    /* +0x40 stock_info[0..8] */

/* Per-category name buffer width (engine: 0x20 byte slot). The
 * sprintf-style copy at the start of `FUN_004912de` uses an unbounded
 * "%s" format, so an overlong category name would overrun — but
 * vendor names are all <= 15 chars + NUL. The port truncates safely. */
#define ITEM_CATEGORY_NAME_LEN 0x20

/* Sentinel for "audience byte not explicitly set" (i.e. zero-init).
 * Engine reads this as "no audience" — items default to NPC-invisible
 * until a tag bit is OR'd in. */
#define ITEM_AUD_NONE 0u

/* Stock-info default for the "wholesale" slot. Engine init at
 * `FUN_00491095:19` writes 200 before scanning tags; matches vendor
 * data convention where unset → 200 (= "use category default"). */
#define ITEM_STOCK_WHOLESALE_DEFAULT 200

/*
 * One item record. Mirrors the engine layout at
 * `&DAT_095d37d0 + slot * 0x2cc`. Field offsets are validated by
 * `_Static_assert` below — every field has a fixed offset that
 * matches the engine's hard-coded writes.
 *
 *   +0x00  valid          (i32)  set to 1 by parser on successful record
 *   +0x04  price          (i32)
 *   +0x08  attack         (i32)
 *   +0x0c  defense        (i32)
 *   +0x10  magic_attack   (i32)
 *   +0x14  magic_defense  (i32)
 *   +0x18  aud_mask       (u32)  customer/audience bitmask (FUN_0049e849)
 *   +0x1c  reserved0      (i32)  engine never writes; populated by gameplay
 *   +0x20  rank           (i32)  level rank (1..5 typical)
 *   +0x24  reserved1      (i32)  engine never writes; populated by gameplay
 *   +0x28  attr_mask      (u32)  attribute bitfield (FUN_00491216 + FUN_0049eb2a)
 *   +0x2c  equip_class    (i32)  equipment-class id (FUN_0049ed75; 0 for non-equippable)
 *   +0x30  reserved2      (i32)  engine never writes
 *   +0x34  item_id        (i32)  4-digit id from the line prefix
 *   +0x38  category       (i32)  item_id / 100
 *   +0x3c  subindex       (i32)  item_id % 100
 *   +0x40  stock_info[9]  (u8 ×9) stock/guild/market/buy/dungeon ×3/wholesale/hold
 *   +0x49  pad0           (u8)
 *   +0x4a  singular[64]   (char) SJIS singular name, NUL-terminated
 *   +0x8a  plural[64]     (char) SJIS plural name, NUL-terminated
 *   +0xca  desc_line1[256](char) SJIS description line 1
 *   +0x1ca desc_line2[256](char) SJIS description line 2
 *   +0x2ca pad1[2]        (u8)  padding to 0x2cc stride
 */
typedef struct {
    int32_t  valid;                          /* +0x000 */
    int32_t  price;                          /* +0x004 */
    int32_t  attack;                         /* +0x008 */
    int32_t  defense;                        /* +0x00c */
    int32_t  magic_attack;                   /* +0x010 */
    int32_t  magic_defense;                  /* +0x014 */
    uint32_t aud_mask;                       /* +0x018 */
    int32_t  reserved0;                      /* +0x01c */
    int32_t  rank;                           /* +0x020 */
    int32_t  reserved1;                      /* +0x024 */
    uint32_t attr_mask;                      /* +0x028 */
    int32_t  equip_class;                    /* +0x02c */
    int32_t  reserved2;                      /* +0x030 */
    int32_t  item_id;                        /* +0x034 */
    int32_t  category;                       /* +0x038 */
    int32_t  subindex;                       /* +0x03c */
    uint8_t  stock_info[ITEM_STOCK_FIELDS];  /* +0x040 */
    uint8_t  pad0;                           /* +0x049 */
    char     singular[ITEM_NAME_LEN];        /* +0x04a */
    char     plural[ITEM_NAME_LEN];          /* +0x08a */
    char     desc_line1[ITEM_DESC_LEN];      /* +0x0ca */
    char     desc_line2[ITEM_DESC_LEN];      /* +0x1ca */
    uint8_t  pad1[2];                        /* +0x2ca */
} item_record_t;                             /* = 0x2cc */

_Static_assert(sizeof(item_record_t) == 0x2cc,
               "item_record_t size must be 0x2cc");
_Static_assert(offsetof(item_record_t, valid)         == 0x000,
               "item_record_t.valid @ 0x000");
_Static_assert(offsetof(item_record_t, price)         == 0x004,
               "item_record_t.price @ 0x004");
_Static_assert(offsetof(item_record_t, aud_mask)      == 0x018,
               "item_record_t.aud_mask @ 0x018");
_Static_assert(offsetof(item_record_t, rank)          == 0x020,
               "item_record_t.rank @ 0x020");
_Static_assert(offsetof(item_record_t, attr_mask)     == 0x028,
               "item_record_t.attr_mask @ 0x028");
_Static_assert(offsetof(item_record_t, equip_class)   == 0x02c,
               "item_record_t.equip_class @ 0x02c");
_Static_assert(offsetof(item_record_t, item_id)       == 0x034,
               "item_record_t.item_id @ 0x034");
_Static_assert(offsetof(item_record_t, category)      == 0x038,
               "item_record_t.category @ 0x038");
_Static_assert(offsetof(item_record_t, subindex)      == 0x03c,
               "item_record_t.subindex @ 0x03c");
_Static_assert(offsetof(item_record_t, stock_info)    == 0x040,
               "item_record_t.stock_info @ 0x040");
_Static_assert(offsetof(item_record_t, singular)      == 0x04a,
               "item_record_t.singular @ 0x04a");
_Static_assert(offsetof(item_record_t, plural)        == 0x08a,
               "item_record_t.plural @ 0x08a");
_Static_assert(offsetof(item_record_t, desc_line1)    == 0x0ca,
               "item_record_t.desc_line1 @ 0x0ca");
_Static_assert(offsetof(item_record_t, desc_line2)    == 0x1ca,
               "item_record_t.desc_line2 @ 0x1ca");

/*
 * Combined state. `count` mirrors `&DAT_005c80ac` (set by the
 * dispatcher after the line loop exits). `categories[*][singular]`
 * and `categories[*][tag]` mirror the per-category name globals at
 * `&DAT_0963e5f8` and `&DAT_0963c5f8` respectively, indexed by
 * `item_id / 100`.
 */
typedef struct {
    char singular[ITEM_CATEGORY_NAME_LEN];
    char tag[ITEM_CATEGORY_NAME_LEN];
} item_category_t;

typedef struct {
    item_record_t   records[ITEM_MAX_RECORDS];
    item_category_t categories[ITEM_CATEGORY_COUNT];
    int32_t         count;
} item_state_t;

/* Engine-global state, populated from src/tables.c. */
extern item_state_t g_item;

/*
 * Parse an item.txt buffer into `*out`. Mirrors FUN_00475270 block #3
 * (475270.c L428..L468) plus the cross-block record dispatch at
 * L815..L829, FUN_00491044 (category headers), and FUN_004912de
 * (item records + sub-helpers).
 *
 * Pre-conditions: `*out` need not be initialised; this function memsets
 * it before parsing. After return:
 *   - `count` is the number of successfully parsed item-record lines.
 *   - For each populated record (slot 0..count-1): all fields are
 *     filled in per the line content. `valid` is set to 1.
 *   - `categories[c]` is populated with the singular + tag from the
 *     most recent `:Category#(tag)` header that preceded an item
 *     in category `c` (item_id / 100 == c).
 *
 * Line dispatch (engine):
 *   '/', '\r', '\n'  — first byte: comment / blank → skipped
 *   ':'              — category header → updates `categories[<next-item's-category>]`
 *   ' '              — silently dropped (defensive indent-skip)
 *   '0'..'9'         — item record: line[0..4] is the 4-digit ID
 *   other            — unknown line → stderr warning (engine: MessageBoxA)
 *
 * Returns void; per-line errors are diagnosed via stderr to match the
 * engine's MessageBoxA-and-continue behaviour.
 */
void tables_parse_item(const unsigned char *data, size_t size,
                       item_state_t *out);

/*
 * Item-name → item-id resolver. Exact-match against the populated
 * records' singular[] field. Returns the matching record's `item_id`
 * on hit (NOT the slot index), or -1 on miss / NULL inputs.
 *
 * Wired into the deferred resolver hooks of `tables_parse_enemy` and
 * `tables_parse_gousei` once Phase B 11/15 lands.
 */
int32_t tables_item_resolve(const item_state_t *state, const char *name);

/*
 * Item-id → record-slot lookup.  Engine: FUN_004681f6 @ 0x4681f6 (42 B).
 *
 * Walks `state->records[0..count-1]` looking for the first record whose
 * `item_id` field exactly matches `item_id`.  Returns the slot index
 * (0..count-1) on hit; -1 on miss / NULL state.  Engine does no
 * `valid` check — matches whatever int32 sits at offset +0x34 of every
 * stride-0x2cc record up through count.  Port matches that behavior
 * (records past count won't be scanned since the loop bounds on count).
 *
 * Used by the chara-equipment subsystem (chara_equip.c) to resolve
 * encoded equip-slot dwords (item_id = slot_val >> 6) to records for
 * stat aggregation.
 *
 * NOTE: emits a CALL_TRACE_ENTER probe @ 0x4681f6.
 */
int32_t tables_item_find_slot_by_id(const item_state_t *state,
                                    int32_t item_id);

#endif /* OPENRECET_TABLES_ITEM_H */
