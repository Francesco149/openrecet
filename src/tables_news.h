/*
 * tables_news.h — parser for `data/news.txt` (block #11 of
 * FUN_00475270 / `tables_load_all`).
 *
 * `news.txt` defines the random in-game news ticker entries. Each line
 * is one news message; the engine picks one per day to show in the
 * town-square HUD. Messages can also bump prices or audience interest
 * for a named item/category/attribute via the `rate`/`price_*` fields.
 *
 * Per-line shape (vendor file):
 *
 *     //comment lines                                  ← line[0]=='/', skipped
 *     対象者,N                                          ← target_group = N (sticky)
 *     時期,A-B                                          ← period_start=A, period_end=B (sticky)
 *     <name>,<rate>,<dur_base>-<dur_range>,[<price_lo>-<price_hi>,]<body text>
 *     -,-,<body text>                                  ← "generic" news with no item effect
 *
 *   <name> is one of:
 *     - "特殊"          → attr_mask = -1, no further lookup
 *     - A 4-byte SJIS attribute tag (武器/防具/貴金/...) → attr_mask
 *     - An item-category singular name (Swords/Daggers/...)  → category
 *     - An item singular name (e.g. "Candy") → item_id
 *     - "-"             → row with no name-effect; body lives after 2 commas
 *
 * Source-level reference: `docs/decompiled/by-address/475270.c`
 * L1583..L2236 (everything between the `s_data_news_txt_005cb090` malloc
 * and the matching `FUN_005036af(local_1c)` free at LAB_00478d82).
 *
 * Pure C, no Win32 surface, so this module also compiles under host
 * gcc for unit testing.
 */

#ifndef OPENRECET_TABLES_NEWS_H
#define OPENRECET_TABLES_NEWS_H

#include <stddef.h>
#include <stdint.h>

/* Vendor news.txt has ~75 active data lines; engine has no explicit
 * cap on the count (just keeps incrementing DAT_06a46f88 and indexing
 * &DAT_056e0e00 + count*0xbc). The port reserves 100 slots — well
 * above the vendor size — as a defensive ceiling. */
#define NEWS_MAX_RECORDS 100

/* Name buffer at +0x80. Structurally 16 bytes (rate follows at +0x90),
 * but the parser writes up to NEWS_NAME_PARSE_CAP bytes before
 * terminating — those last 4 bytes overflow into rate. Dormant in
 * vendor data (all names are <= 12 bytes). See engine-quirks #27. */
#define NEWS_NAME_LEN       16
#define NEWS_NAME_PARSE_CAP 20

/* Body buffer at +0x00. Engine caps at 128 bytes; vendor messages are
 * all well under that. */
#define NEWS_BODY_LEN 128

/* "Special" name sentinel that bypasses attr/category/item lookup.
 * SJIS "特殊" = 4 bytes. */
#define NEWS_SPECIAL_NAME      "\x93\xc1\x8e\xea"
#define NEWS_SPECIAL_NAME_LEN  4

/* Category sentinel for "-" data rows — engine writes -100 at +0xa0.
 * Distinct from "name lookup miss" (-1 on non-"-" rows). */
#define NEWS_CATEGORY_DASH (-100)

/* Period defaults at parser init. Engine: local_18=0, local_20=0x64. */
#define NEWS_PERIOD_START_DEFAULT 0
#define NEWS_PERIOD_END_DEFAULT   100

/*
 * Resolver callbacks.
 *
 * `name` is the raw bytes of the news.txt name field, NUL-padded
 * within the record up to NEWS_NAME_PARSE_CAP+1 bytes. `name_len` is
 * the byte count up to (but not including) the terminating ','.
 *
 * Engine semantics: `FUN_00479f4d(name, candidate, name_len)` —
 * `memcmp(name, candidate, name_len) == 0`. This is a PREFIX match:
 * `name` only has to equal `candidate`'s first `name_len` bytes.
 * Vendor names happen to exactly match their candidates so the
 * distinction doesn't matter in practice, but resolvers should mirror
 * the engine. See `oder_attr_hash` and the resolver wiring in
 * `src/tables.c` for the canonical pattern.
 *
 * Return the matching index on hit, or -1 on miss / NULL inputs.
 */
typedef int32_t (*news_category_resolve_fn)(
    const char *name, size_t name_len, void *user);
typedef int32_t (*news_item_resolve_fn)(
    const char *name, size_t name_len, void *user);

/*
 * One news record. Layout matches the engine's 0xbc-byte stride at
 * &DAT_056e0e00 byte-for-byte (32-bit ints, little-endian).
 *
 *   +0x00  body[128]      — news message (may include trailing '\r' if
 *                            the source line was CRLF — quirk #30)
 *   +0x80  name[16]       — matched name; parser CAN write up to 20
 *                            bytes here, overflowing into rate (quirk #27)
 *   +0x90  rate           — +N price up, -N down, 0 customers up
 *   +0x94  dur_base       — news LIFETIME base (atoi; semantics fixed
 *                            2026-07-10 from FUN_00436623: an accepted
 *                            entry's duration counter = dur_base +
 *                            rng%dur_range + 1, min 2; decrements once
 *                            per news_daily_update, expiry at 0)
 *   +0x98  dur_range      — lifetime rng modulus (atoi after '-');
 *                            <=0 ⇒ NO rng draw (load-bearing LCG count)
 *   +0x9c  attr_mask      — oder_attr_hash result; -1 for "特殊"; 0 for
 *                            "-" rows or no-attr-match
 *   +0xa0  category       — item-category index from category resolver;
 *                            -100 for "-" rows; -1 if no match
 *   +0xa4  item_id        — item id (NOT slot) from item resolver;
 *                            -1 if no match
 *   +0xa8  target_group   — last "対象者:" value; 0 for "-" rows (engine
 *                            never writes them — see quirk #29)
 *   +0xac  price_lo       — target-item PRICE window low (atoi); -1 if
 *                            absent ⇒ the generator picks NO target item;
 *                            0 for "-" rows (BSS init, engine quirk)
 *   +0xb0  price_hi       — target-item price window high (atoi); -1 if
 *                            absent.  FUN_00436623 scans g_item for
 *                            valid rows with price in [lo,hi] matching
 *                            attr_mask/category, rng-picks one target
 *                            (1 draw iff ≥1 match)
 *   +0xb4  period_start   — last "時期:" range start (default 0)
 *   +0xb8  period_end     — last "時期:" range end   (default 100)
 */
typedef struct {
    char     body[NEWS_BODY_LEN];   /* +0x00 */
    char     name[NEWS_NAME_LEN];   /* +0x80 */
    int32_t  rate;                  /* +0x90 */
    int32_t  dur_base;              /* +0x94 */
    int32_t  dur_range;              /* +0x98 */
    int32_t  attr_mask;             /* +0x9c */
    int32_t  category;              /* +0xa0 */
    int32_t  item_id;               /* +0xa4 */
    int32_t  target_group;          /* +0xa8 */
    int32_t  price_lo;               /* +0xac */
    int32_t  price_hi;               /* +0xb0 */
    int32_t  period_start;          /* +0xb4 */
    int32_t  period_end;            /* +0xb8 */
} news_record_t;

_Static_assert(sizeof(news_record_t) == 0xbc,
               "news_record_t size must be 0xbc (188)");
_Static_assert(offsetof(news_record_t, body)         == 0x00,
               "news_record_t.body @ +0x00");
_Static_assert(offsetof(news_record_t, name)         == 0x80,
               "news_record_t.name @ +0x80");
_Static_assert(offsetof(news_record_t, rate)         == 0x90,
               "news_record_t.rate @ +0x90");
_Static_assert(offsetof(news_record_t, dur_base)     == 0x94,
               "news_record_t.dur_base @ +0x94");
_Static_assert(offsetof(news_record_t, dur_range)     == 0x98,
               "news_record_t.dur_range @ +0x98");
_Static_assert(offsetof(news_record_t, attr_mask)    == 0x9c,
               "news_record_t.attr_mask @ +0x9c");
_Static_assert(offsetof(news_record_t, category)     == 0xa0,
               "news_record_t.category @ +0xa0");
_Static_assert(offsetof(news_record_t, item_id)      == 0xa4,
               "news_record_t.item_id @ +0xa4");
_Static_assert(offsetof(news_record_t, target_group) == 0xa8,
               "news_record_t.target_group @ +0xa8");
_Static_assert(offsetof(news_record_t, price_lo)      == 0xac,
               "news_record_t.price_lo @ +0xac");
_Static_assert(offsetof(news_record_t, price_hi)      == 0xb0,
               "news_record_t.price_hi @ +0xb0");
_Static_assert(offsetof(news_record_t, period_start) == 0xb4,
               "news_record_t.period_start @ +0xb4");
_Static_assert(offsetof(news_record_t, period_end)   == 0xb8,
               "news_record_t.period_end @ +0xb8");

typedef struct {
    news_record_t records[NEWS_MAX_RECORDS];
    int32_t       count;
} news_state_t;

/* Engine-global state, populated from src/tables.c. */
extern news_state_t g_news;

/*
 * Parse a news.txt buffer into `*out`. Mirrors FUN_00475270 block #11.
 *
 * `cat_resolve` / `item_resolve` may be NULL — those name-lookup paths
 * then always miss (record's `category` / `item_id` stays at -1).
 * `resolve_user` is passed through to both resolvers verbatim.
 *
 * Line dispatch:
 *   '/', '\r', '\n'  — line[0] = comment / blank → skipped
 *   "対象者,N"        — sets target_group to N (sticky for subsequent rows)
 *   "時期,A-B"        — sets period_start=A, period_end=B (sticky)
 *   "-"              — generic news row (no name-effect); body after 2 commas
 *   other            — data row: name + rate + price range [+ days range] + body
 *
 * Pre-conditions: `*out` need not be initialised; memset to 0 inside.
 *
 * Post-conditions: `out->count` is the number of populated records.
 * Records past `count` are zero (BSS-equivalent state).
 */
void tables_parse_news(const unsigned char *data, size_t size,
                       news_state_t *out,
                       news_category_resolve_fn cat_resolve,
                       news_item_resolve_fn     item_resolve,
                       void *resolve_user);

#endif /* OPENRECET_TABLES_NEWS_H */
