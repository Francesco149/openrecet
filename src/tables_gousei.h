/*
 * tables_gousei.h — parser for `data/gousei.txt` (block #13 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `gousei.txt` (合成 = "synthesis") defines item-crafting recipes for
 * the shop's workshop UI: one output item (a craftable weapon, armour,
 * or accessory) plus up to 5 ingredient items, each with a quantity.
 * Recipes are grouped under `ランク:N` headers that tag the current
 * crafting rank.
 *
 * Per-line shape (vendor file):
 *
 *     0004:Gilded Sword:Longsword#1:Water Crystal#1:
 *     ^^^^                                          ^
 *     leading 4-digit "ID" — parsed-but-discarded   trailing ':'
 *
 * Engine skips the first 5 bytes of every recipe line, so the
 * "NNNN:" prefix is purely cosmetic (likely a documentation aid for
 * the data designers). The output item is identified by NAME, not by
 * the numeric prefix. Field separator is `:`; ingredient quantity is
 * `#count` appended to the ingredient name.
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_GOUSEI_H
#define OPENRECET_TABLES_GOUSEI_H

#include <stddef.h>
#include <stdint.h>

/* Maximum recipe slots. The engine's MessageBoxA "合成アイテム登録
 * オーバー" fires when `DAT_09642bf0 > 200` (i.e. when the 201st
 * record gets written). Slot 200 is borderline — by address math,
 * `base + 200 * 0x30 = 0x09642bd0`, only 0x20 bytes shy of the
 * adjacent counter at `&DAT_09642bf0` — so writing slot 200
 * already starts encroaching on neighbouring globals. The port
 * caps strictly at 200 records (engine writes have already
 * happened by the time the warning fires, but we draw the line
 * before stomping). Vendor data ships 177 recipes. */
#define GOUSEI_MAX_RECORDS 200

/* Per-line scan cap matching the engine's `local_8 != 0x100` safety
 * bound (256 chars after the 5-byte prefix). Vendor lines fit well
 * under this. */
#define GOUSEI_LINE_SCAN_CAP 0x100

/* Per-field name buffer cap. Engine local is 0x100 bytes (`local_57c`
 * spans up to `local_67c`). */
#define GOUSEI_FIELD_NAME_LEN 0x100

/* Ingredient slot count. Engine indexes columns 0..5 where column 0
 * is the output item and columns 1..5 are ingredients. */
#define GOUSEI_INGREDIENT_COUNT 5

/* Sentinel for an unused / unresolved ingredient slot. The engine
 * writes 0xffffffff to ing2..ing5 IDs when ing1 is written; unused
 * counts stay at 0 (BSS init). Port matches. */
#define GOUSEI_EMPTY (-1)

/*
 * One recipe record. Mirrors the engine layout at `&DAT_09640650` +
 * idx*0x30 (i.e. idx*0xc dwords):
 *
 *   +0x00  output_id      (i32)  — item id of the crafted output
 *                                   (DAT_09640650)
 *   +0x04  rank           (i32)  — current `ランク:N` value at parse
 *                                   time (DAT_09640654)
 *   +0x08  ingredient_id[0] (i32) — ing1 item id   (DAT_09640658)
 *   +0x0c  ingredient_id[1] (i32) — ing2 item id   (DAT_0964065c)
 *   +0x10  ingredient_id[2] (i32) — ing3 item id   (DAT_09640660)
 *   +0x14  ingredient_id[3] (i32) — ing4 item id   (DAT_09640664)
 *   +0x18  ingredient_id[4] (i32) — ing5 item id   (DAT_09640668)
 *   +0x1c  ingredient_count[0] (i32) — ing1 count  (DAT_0964066c)
 *   +0x20  ingredient_count[1] (i32) — ing2 count  (DAT_09640670)
 *   +0x24  ingredient_count[2] (i32) — ing3 count  (DAT_09640674)
 *   +0x28  ingredient_count[3] (i32) — ing4 count  (DAT_09640678)
 *   +0x2c  ingredient_count[4] (i32) — ing5 count  (DAT_0964067c)
 *
 * Total: 0x30 (48) bytes per record × 200 = 0x2580 bytes.
 */
typedef struct {
    int32_t output_id;                                  /* +0x00 */
    int32_t rank;                                       /* +0x04 */
    int32_t ingredient_id[GOUSEI_INGREDIENT_COUNT];     /* +0x08 */
    int32_t ingredient_count[GOUSEI_INGREDIENT_COUNT];  /* +0x1c */
} gousei_record_t;                                      /* = 0x30 */

_Static_assert(sizeof(gousei_record_t) == 0x30,
               "gousei_record_t size must be 0x30");
_Static_assert(offsetof(gousei_record_t, output_id)        == 0x00,
               "gousei_record_t.output_id @ 0x00");
_Static_assert(offsetof(gousei_record_t, rank)             == 0x04,
               "gousei_record_t.rank @ 0x04");
_Static_assert(offsetof(gousei_record_t, ingredient_id)    == 0x08,
               "gousei_record_t.ingredient_id @ 0x08");
_Static_assert(offsetof(gousei_record_t, ingredient_count) == 0x1c,
               "gousei_record_t.ingredient_count @ 0x1c");

/*
 * Combined state. Engine counter `DAT_09642bf0` tracks the populated
 * record count, ratcheted up per parsed recipe line.
 */
typedef struct {
    gousei_record_t records[GOUSEI_MAX_RECORDS];
    int32_t         count;
} gousei_state_t;

/* Engine-global state, populated from src/tables.c. */
extern gousei_state_t g_gousei;

/*
 * Item-name → item-id resolver callback. The engine resolves names
 * against the `item.txt`-populated table at `&DAT_095d381a` (stride
 * 0x2cc, count `_DAT_005c80ac`) via an exact-string-match probe.
 *
 * `name` is a NUL-terminated SJIS string from the recipe line.
 * `user` is the opaque value passed through from `tables_parse_gousei`.
 *
 * Returns the resolved item id (>= 0) on success, or -1 on miss /
 * "table not loaded yet". Until item.txt's parser lands in Phase B,
 * tables.c passes a NULL resolver and every name resolves to -1.
 */
typedef int32_t (*gousei_resolve_fn)(const char *name, void *user);

/*
 * Parse a gousei.txt buffer into `*out`. Mirrors FUN_00475270 block
 * starting at LAB_004790cd (L2402..L2579 of
 * docs/decompiled/by-address/475270.c).
 *
 * Pre-conditions: `*out` need not be initialised; this function memsets
 * it before parsing. After return:
 *   - `count` is the number of successfully parsed recipe lines.
 *   - For each populated record: `output_id` is the resolver's return
 *     value for the output name (or -1); `rank` is the most recent
 *     `ランク:N` value (or 0 if no rank header preceded the line);
 *     `ingredient_id[k]` is the resolver's return value for ing(k+1),
 *     or -1 if that ingredient slot is unused;
 *     `ingredient_count[k]` is the `#N` count for ing(k+1), or 0 if
 *     unused.
 *
 * Line dispatch (engine):
 *   '/', '\r', '\n'  — first byte: comment / blank → skipped
 *   `ランク:N`        — set current_rank = atoi(N); no record written
 *   anything else     — treated as a recipe line: skip 5-byte prefix,
 *                        parse 6 colon-separated fields (1 output
 *                        name + 5 ingredient `name#count` entries).
 *
 * `resolve` may be NULL — in that case every name resolves to -1
 * (the convention used before item.txt's parser lands).
 *
 * `user` is passed through to every `resolve(name, user)` call.
 */
void tables_parse_gousei(const unsigned char *data, size_t size,
                         gousei_state_t *out,
                         gousei_resolve_fn resolve, void *user);

#endif /* OPENRECET_TABLES_GOUSEI_H */
