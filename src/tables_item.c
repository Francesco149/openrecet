/*
 * tables_item.c — `data/item.txt` parser.
 *
 * Source-level reference: FUN_00475270 block #3 in
 * docs/decompiled/by-address/475270.c (L428..L468 main dispatch) plus
 * the cross-block record fallback at L815..L829 (reached via
 * `goto LAB_00476d04`). Sub-parsers: FUN_00491044 (category header),
 * FUN_004912de (item record), plus helpers FUN_00491095 (stock-info
 * tags), FUN_00491216 (attribute bitmask), FUN_0049e849 (audience
 * mask), FUN_0049ed75 (equipment class), FUN_0049eb2a (category
 * bitmask).
 *
 * Full layout justification: docs/findings/item-table.md.
 *
 * Engine quirks faithfully reproduced:
 *
 *   - **Cross-block dispatcher.** Engine has a single shared epilogue
 *     for "line didn't match ':' prefix" inside the kyaku.txt block
 *     function body, reached via `goto LAB_00476d04`. The port
 *     linearises this into one parse_line() function.
 *
 *   - **Most-recent-header semantics.** Each item record copies the
 *     scratch buffers from the most recent FUN_00491044 call into the
 *     per-category name table at `categories[item_id/100]`. Header
 *     lines and item lines must alternate in a sensible order; the
 *     port matches this by tracking pending header state until the
 *     next record fires.
 *
 *   - **Singular/plural mid-line split.** Phase 0 of FUN_004912de
 *     writes each name char to BOTH singular[] and plural[]; on '+',
 *     it resets the column counter and continues writing only to
 *     plural[]. So a record like `Sword#…` (no `+`) ends up with
 *     plural == singular, while `Worn Sword+Worn Swords#…` keeps
 *     singular = "Worn Sword" and overwrites plural with "Worn Swords".
 *
 *   - **256-char outer iteration cap.** Outer loop bound is
 *     `param_3 != 0x100` (engine L197). Each phase-0 normal char and
 *     each phase-1/2 char consumes one iteration; the phase-0 '#'
 *     subroutine consumes one iteration but eats many bytes via inner
 *     scan-to-'#' loops. The port honours the 256-iteration cap, not
 *     a 256-byte cap.
 *
 *   - **Description phase 2 ends on '/'.** Engine treats '/' as a
 *     break char in phase 2 (L187 of FUN_004912de), so a literal '/'
 *     in a description line 2 truncates the field. Vendor data
 *     respects this. Phase 1 has no such '/' check — only '\r'/'\n'
 *     and '#' terminate.
 *
 *   - **Reserved slots at +0x1c / +0x24 / +0x30.** Engine never writes
 *     these; the port leaves them zero. They're populated by gameplay
 *     code (stock counters, etc.) that we don't have yet.
 *
 *   - **Unknown-line MessageBox.** Engine pops MessageBoxA "不明な行"
 *     for non-digit non-':' non-' ' lines (L818-820 of 475270.c).
 *     Port emits a stderr warning instead. No vendor data triggers
 *     this path.
 */

#include "tables_item.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "call_trace.h"

/* Global state — populated by tables.c via tables_parse_item. */
item_state_t g_item;

/* ============================================================ */
/* Sub-parser helpers — attribute/stock/audience/equip-class    */
/* ============================================================ */

/*
 * Attribute-tag table for FUN_00491216 + FUN_0049e9a7. Same 16 tags
 * that `oder.txt` uses (oder_attr_hash). We could include
 * tables_oder.h and call oder_attr_hash directly, but the engine
 * inlines a fresh 4-byte memcmp loop per consumer — easier to read
 * if the table is local. The bit positions match oder.txt.
 */
static const uint8_t ITEM_ATTR_TAGS[16][4] = {
    /* 0x0001 */ { 0x95, 0x90, 0x8a, 0xed },  /* 武器  weapon                 */
    /* 0x0002 */ { 0x96, 0x68, 0x8b, 0xef },  /* 防具  armour                 */
    /* 0x0004 */ { 0x92, 0xb2, 0x93, 0x78 },  /* 調度  decor                  */
    /* 0x0008 */ { 0x95, 0x9e, 0x8f, 0xfc },  /* 服飾  clothing               */
    /* 0x0010 */ { 0x83, 0x41, 0x83, 0x4e },  /* アク  accessory (アクセサリ)  */
    /* 0x0020 */ { 0x8b, 0x4d, 0x8b, 0xe0 },  /* 貴金  precious metal         */
    /* 0x0040 */ { 0x8b, 0xe0, 0x91, 0xae },  /* 金属  metal                  */
    /* 0x0080 */ { 0x97, 0x5b, 0x94, 0xd1 },  /* 夕飯  dinner                 */
    /* 0x0100 */ { 0x8a, 0xc3, 0x82, 0xa2 },  /* 甘い  sweet                  */
    /* 0x0200 */ { 0x94, 0x68, 0x8e, 0xe8 },  /* 派手  fancy                  */
    /* 0x0400 */ { 0x92, 0x6e, 0x96, 0xa1 },  /* 地味  plain                  */
    /* 0x0800 */ { 0x92, 0xbf, 0x95, 0x69 },  /* 珍品  rare                   */
    /* 0x1000 */ { 0x96, 0x68, 0x8a, 0xa6 },  /* 防寒  cold-weather           */
    /* 0x2000 */ { 0x90, 0x48, 0x95, 0x69 },  /* 食品  food                   */
    /* 0x4000 */ { 0x90, 0xb9, 0x91, 0xae },  /* 聖属  holy                   */
    /* 0x8000 */ { 0x96, 0x82, 0x91, 0xae },  /* 魔属  sinister               */
};

/* FUN_0049e9a7 — 4-byte tag bitmask. Mirrors oder_attr_hash; kept
 * local so this module doesn't drag in oder. */
static uint32_t item_attr_4byte_mask(const char *p)
{
    uint32_t mask = 0;
    for (int i = 0; i < 16; i++) {
        if (memcmp(p, ITEM_ATTR_TAGS[i], 4) == 0) {
            mask = 1u << i;
        }
    }
    return mask;
}

/*
 * FUN_0049eb2a — category-name → broad-class bitmask. Returns:
 *   0x00001  for weapon categories (Swords, Daggers, …, Arm Parts, +1 unknown)
 *   0x00002  for armour categories (Clothes, Robes, …, Bracelets, Helms, +1 unknown)
 *   0x10000  for furniture categories (Flooring, Wallpapers, Counters, Carpets)
 *
 * The engine merges multiple match types via OR; in practice each
 * category name matches at most one class. Match is "first N bytes
 * equal" where N varies per tag (5..12). Vendor category names use
 * the longer English forms ("Bracelets", "Breastplates") and short
 * forms ("Bows", "Hats") — table preserves the exact lengths.
 */
static const struct {
    const char *name;
    uint32_t    bits;
} ITEM_CLASS_TAGS[] = {
    /* Engine memcmps against fixed-length .data tags (e.g. 12 bytes for
     * "Arm Parts"), where any bytes past the visible name are NUL
     * padding. We match the engine's semantics by comparing the visible
     * name as a prefix and requiring the source byte at position
     * strlen(name) to be NUL (== "exact match"). Vendor category
     * names always satisfy this. */
    { "Swords",       0x00001 },
    { "Daggers",      0x00001 },
    { "Staves",       0x00001 },
    { "Bows",         0x00001 },
    { "Spears",       0x00001 },
    { "Gloves",       0x00001 },
    { "Claws",        0x00001 },
    { "Arm Parts",    0x00001 },
    { "Clothes",      0x00002 },
    { "Robes",        0x00002 },
    { "Breastplates", 0x00002 },
    { "Armor",        0x00002 },
    { "Shields",      0x00002 },
    { "Bracelets",    0x00002 },
    { "Helms",        0x00002 },
    { "Hats",         0x00002 },
    { "Flooring",     0x10000 },
    { "Wallpapers",   0x10000 },
    { "Counters",     0x10000 },
    { "Carpets",      0x10000 },
};

static uint32_t item_class_bits(const char *category_name)
{
    uint32_t bits = 0;
    for (size_t i = 0; i < sizeof(ITEM_CLASS_TAGS) / sizeof(*ITEM_CLASS_TAGS); i++) {
        size_t tag_len = strlen(ITEM_CLASS_TAGS[i].name);
        if (memcmp(category_name, ITEM_CLASS_TAGS[i].name, tag_len) == 0 &&
            category_name[tag_len] == '\0') {
            bits |= ITEM_CLASS_TAGS[i].bits;
        }
    }
    return bits;
}

/*
 * FUN_0049ed75 — category-name → equipment-class id (1..0x54).
 * 0 for non-equippable. Engine implements as a sequence of memcmp
 * calls where each match overwrites the result, so the LAST matching
 * tag wins. Table is in engine declaration order; final tag value
 * is what we return.
 */
static const struct {
    const char *name;
    int32_t     class_id;
} ITEM_EQUIP_TAGS[] = {
    { "Swords",        1    },
    { "Daggers",       2    },
    { "Staves",        3    },
    { "Bows",          4    },
    { "Spears",        6    },
    { "Gloves",        5    },
    { "Claws",         7    },
    { "Arm Parts",     0x54 },
    { "Shields",       0xe  },
    { "Bracelets",     0xd  },
    { "Robes",         10   },
    { "Clothes",       9    },
    { "Breastplates",  0xb  },
    { "Armor",         0xc  },
    { "Hats",          0xf  },
    { "Helms",         0x10 },
    { "Charms",        0x14 },
    { "Necklaces",     0x13 },
    { "Capes",         0x15 },
    { "Scarves",       0x11 },
    { "Shoes",         0x12 },
    { "Rings",         0x16 },
};

static int32_t item_equip_class(const char *category_name)
{
    int32_t class_id = 0;
    for (size_t i = 0; i < sizeof(ITEM_EQUIP_TAGS) / sizeof(*ITEM_EQUIP_TAGS); i++) {
        size_t tag_len = strlen(ITEM_EQUIP_TAGS[i].name);
        if (memcmp(category_name, ITEM_EQUIP_TAGS[i].name, tag_len) == 0 &&
            category_name[tag_len] == '\0') {
            class_id = ITEM_EQUIP_TAGS[i].class_id;
        }
    }
    return class_id;
}

/*
 * FUN_0049e849 — audience bitmask. 11 × 2-byte SJIS tags scanned per
 * char position; up to 10 tag-positions consumed (engine L78 cap).
 * Each tag scan consumes 2 bytes; ' ' is a separator (1 byte). Reads
 * until '#', '\r', '\n', or NUL.
 *
 * Tag table (SJIS bytes, low → high):
 */
static const struct {
    uint8_t  bytes[2];
    uint32_t bits;
} ITEM_AUD_TAGS[] = {
    { { 0x91, 0x53 }, 0xff },  /* 全 all                                */
    { { 0x83, 0x8a }, 0x01 },  /* リ Recette                            */
    { { 0x83, 0x56 }, 0x02 },  /* シ                                    */
    { { 0x83, 0x4a }, 0x04 },  /* カ Caillou                            */
    { { 0x83, 0x65 }, 0x08 },  /* テ Tielle                             */
    { { 0x83, 0x47 }, 0x10 },  /* エ Elan                               */
    { { 0x83, 0x69 }, 0x20 },  /* ナ Nagi                               */
    { { 0x83, 0x4f }, 0x40 },  /* グ Guildmaster                        */
    { { 0x83, 0x41 }, 0x80 },  /* ア Arma                               */
    { { 0x92, 0x6a }, 0x55 },  /* 男 male composite                     */
    { { 0x8f, 0x97 }, 0xaa },  /* 女 female composite                   */
};

static void item_parse_audience(item_record_t *r, const char *field)
{
    /* Engine init: if field starts with '#' (i.e. empty), audience is
     * "all" (0xff). See FUN_0049e849 L11-13. */
    if (*field == '#') {
        r->aud_mask |= 0xff;
    }

    int consumed = 0;
    const char *p = field;
    while (*p != '\0' && *p != '#' && *p != '\r' && *p != '\n') {
        if (*p == ' ') {
            p++;
            consumed++;
            if (consumed == 10) return;
            continue;
        }
        for (size_t i = 0; i < sizeof(ITEM_AUD_TAGS) / sizeof(*ITEM_AUD_TAGS); i++) {
            if (memcmp(p, ITEM_AUD_TAGS[i].bytes, 2) == 0) {
                r->aud_mask |= ITEM_AUD_TAGS[i].bits;
            }
        }
        p += 2;
        consumed++;
        if (consumed == 10) return;
    }
}

/*
 * FUN_00491095 — stock-info tag parser. 7 tags scanned in 5 rounds
 * (engine outer loop runs `param_1 = 5; do { … } while(--param_1)`).
 * Multiple rounds let tags appear in any order; later occurrences
 * overwrite earlier ones, which matches engine behaviour.
 *
 * The "ダ" (dungeon) tag writes to 3 consecutive byte slots
 * (+0x44..+0x46), each with an optional ×10 multiplier for values < 10
 * (so a value of 5 becomes 50, but 11 stays 11).
 */
static const uint8_t STOCK_TAG_ZAIKO[5] = { 0x8d, 0xdd, 0x8c, 0xc9, 0x28 };  /* 在庫( */
static const uint8_t STOCK_TAG_GI    [3] = { 0x83, 0x4d, 0x28 };              /* ギ(   */
static const uint8_t STOCK_TAG_SHI   [3] = { 0x8e, 0x73, 0x28 };              /* 市(   */
static const uint8_t STOCK_TAG_KAI   [3] = { 0x94, 0x83, 0x28 };              /* 買(   */
static const uint8_t STOCK_TAG_DA    [3] = { 0x83, 0x5f, 0x28 };              /* ダ(   */
static const uint8_t STOCK_TAG_OROSHI[3] = { 0x8a, 0xd3, 0x28 };              /* 卸(   */
static const uint8_t STOCK_TAG_JI    [3] = { 0x8e, 0x9d, 0x28 };              /* 持(   */

static void item_parse_stock(item_record_t *r, const char *field)
{
    /* Engine sets defaults at FUN_00491095 L12-20: 0,0,0,0,0,0,0,200,0.
     * The struct is memset to zero so we just need to set the +0x47
     * wholesale default. */
    r->stock_info[0] = 0;
    r->stock_info[1] = 0;
    r->stock_info[2] = 0;
    r->stock_info[3] = 0;
    r->stock_info[4] = 0;
    r->stock_info[5] = 0;
    r->stock_info[6] = 0;
    r->stock_info[7] = ITEM_STOCK_WHOLESALE_DEFAULT;
    r->stock_info[8] = 0;

    const char *p = field;
    int rounds = 5;
    while (rounds-- > 0) {
        if (memcmp(p, STOCK_TAG_ZAIKO, 5) == 0) {
            r->stock_info[0] = (uint8_t)atoi(p + 5);
            p += 7;
        }
        if (memcmp(p, STOCK_TAG_GI, 3) == 0) {
            r->stock_info[1] = (uint8_t)atoi(p + 3);
            p += 5;
        }
        if (memcmp(p, STOCK_TAG_SHI, 3) == 0) {
            r->stock_info[2] = (uint8_t)atoi(p + 3);
            p += 5;
        }
        if (memcmp(p, STOCK_TAG_KAI, 3) == 0) {
            r->stock_info[3] = (uint8_t)atoi(p + 3);
            p += 5;
        }
        if (memcmp(p, STOCK_TAG_DA, 3) == 0) {
            /* First ダ(N) slot — value, possibly ×10. */
            int v = atoi(p + 3);
            int digits;
            if (v < 10) {
                r->stock_info[4] = (uint8_t)(v * 10);
                digits = 2;
            } else {
                r->stock_info[4] = (uint8_t)v;
                digits = 3;
            }
            p += 3 + digits;
            if (*p == '(') {
                v = atoi(p + 1);
                if (v < 10) {
                    r->stock_info[5] = (uint8_t)(v * 10);
                    digits = 2;
                } else {
                    r->stock_info[5] = (uint8_t)v;
                    digits = 3;
                }
                p += 1 + digits;
                if (*p == '(') {
                    v = atoi(p + 1);
                    if (v < 10) {
                        r->stock_info[6] = (uint8_t)(v * 10);
                        p += 3;
                    } else {
                        r->stock_info[6] = (uint8_t)v;
                        p += 4;
                    }
                }
            }
        }
        if (memcmp(p, STOCK_TAG_OROSHI, 3) == 0) {
            r->stock_info[7] = (uint8_t)atoi(p + 3);
            p += 5;
        }
        if (memcmp(p, STOCK_TAG_JI, 3) == 0) {
            r->stock_info[8] = (uint8_t)atoi(p + 3);
            p += 5;
        }
    }
}

/*
 * FUN_00491216 — attribute bitmask. Up to 10 × 4-byte tag scan, then
 * OR-merge with the category-name's broad-class bits (FUN_0049eb2a).
 * Field is space-separated; ' ' advances by 1 byte without consuming
 * a tag slot.
 */
static void item_parse_attr(item_record_t *r, const char *field,
                            const char *category_singular)
{
    r->attr_mask = 0;
    int  consumed = 0;
    const char *p = field;
    while (*p != '\0' && *p != '#' && *p != '\r' && *p != '\n') {
        if (*p == ' ') {
            p++;
        } else {
            r->attr_mask |= item_attr_4byte_mask(p);
            p += 4;
        }
        consumed++;
        if (consumed == 10) break;
    }
    r->attr_mask |= item_class_bits(category_singular);
}

/* ============================================================ */
/* Sub-parsers — FUN_00491044 (category) and FUN_004912de       */
/* ============================================================ */

/*
 * Category-header scratch — mirrors the engine globals at
 * &DAT_09642bd0 (before-#) and &DAT_09640604 (after-#) that
 * FUN_00491044 writes to. Each subsequent call to the record parser
 * picks the current value up via FUN_005038ff sprintf-copy.
 *
 * 32 bytes engine cap (iVar4 != 0x20). Trailing NUL guaranteed by
 * the loop's per-write `[iVar+1] = 0` pattern.
 */
static char g_cat_singular_scratch[33];
static char g_cat_tag_scratch[33];
static int  g_cat_have_scratch = 0;
static int  g_cat_index_for_scratch = -1;

/* Reset scratch state before each parse run. */
static void parse_reset_scratch(void)
{
    memset(g_cat_singular_scratch, 0, sizeof g_cat_singular_scratch);
    memset(g_cat_tag_scratch, 0, sizeof g_cat_tag_scratch);
    g_cat_have_scratch = 0;
    g_cat_index_for_scratch = -1;
}

/*
 * FUN_00491044 — category-header line parser. Reads up to 32 chars
 * from `line` (which is the original line + 1, i.e. past the leading
 * `:`). Splits on `#`: bytes before `#` → singular scratch, bytes
 * after → tag scratch. Each char also writes a trailing NUL byte
 * (engine: `[iVar3+1] = 0` per-iteration).
 */
static void parse_category_header(const char *line)
{
    char *out = g_cat_singular_scratch;
    int col   = 0;
    int seen_hash = 0;
    int i;
    /* Engine cap: iVar4 != 0x20 = 32 iterations max. */
    for (i = 0; i < 32; i++) {
        char c = line[i];
        if (c == '\r' || c == '\n' || c == '\0') break;
        if (c == '#') {
            /* Switch to the second buffer, reset column. */
            out = g_cat_tag_scratch;
            col = 0;
            seen_hash = 1;
            continue;
        }
        out[col]     = c;
        out[col + 1] = 0;
        col++;
    }
    (void)seen_hash;
    g_cat_have_scratch = 1;
}

/* Commit the pending category-header scratch into
 * out->categories[cat_idx], if a header is pending. Mirrors engine
 * `FUN_005038ff` sprintf copies at FUN_004912de L24-25. */
static void commit_category(item_state_t *out, int cat_idx)
{
    if (!g_cat_have_scratch) return;
    if (cat_idx < 0 || cat_idx >= ITEM_CATEGORY_COUNT) return;
    /* Engine uses sprintf "%s" which is effectively strcpy from an
     * unbounded source — vendor names fit comfortably under our
     * ITEM_CATEGORY_NAME_LEN. Manual bounded copy avoids the
     * -Wstringop-truncation warning on `strncpy(dst, src, sizeof dst-1)`. */
    size_t i;
    for (i = 0; i < ITEM_CATEGORY_NAME_LEN - 1 && g_cat_singular_scratch[i] != 0; i++) {
        out->categories[cat_idx].singular[i] = g_cat_singular_scratch[i];
    }
    out->categories[cat_idx].singular[i] = 0;
    for (i = 0; i < ITEM_CATEGORY_NAME_LEN - 1 && g_cat_tag_scratch[i] != 0; i++) {
        out->categories[cat_idx].tag[i] = g_cat_tag_scratch[i];
    }
    out->categories[cat_idx].tag[i] = 0;
    g_cat_index_for_scratch = cat_idx;
}

/*
 * Skip from `*p` to the byte AFTER the next `#`. Returns 0 if EOL/EOF
 * was hit first (matches engine's `loop err sN` MessageBox path —
 * here we just stop parsing this record and emit nothing further).
 */
static int scan_past_hash(const char **p)
{
    while (**p != '#') {
        if (**p == '\0' || **p == '\r' || **p == '\n') return 0;
        (*p)++;
    }
    (*p)++;
    return 1;
}

/*
 * Parse an int via strtol up to the next non-digit character. atoi()
 * matches the engine's FUN_00503d03 (strtol with base 10).
 */
static int32_t atoi_pos(const char *p)
{
    return (int32_t)strtol(p, NULL, 10);
}

/*
 * FUN_004912de — item-record line parser. `line` points at the byte
 * after the dispatcher's 5-byte `NNNN:` prefix, so it starts with
 * either a digit (rank) or the first name char.
 */
static void parse_item_record(item_state_t *out, const char *line,
                              int32_t item_id, int slot)
{
    item_record_t *r = &out->records[slot];
    memset(r, 0, sizeof *r);
    r->item_id  = item_id;
    r->category = item_id / 100;
    r->subindex = item_id % 100;

    /* Commit pending category-header scratch into our category's
     * slot. Engine sprintf-copies on every record (re-writes the
     * same bytes when a category spans many records); we do it
     * once per record, which is observably identical. */
    commit_category(out, r->category);
    r->equip_class = item_equip_class(out->categories[r->category].singular);

    /* Engine inits these strings to " "+NUL via L28-31. Match. */
    r->desc_line1[0] = ' ';
    r->desc_line1[1] = 0;
    r->desc_line2[0] = ' ';
    r->desc_line2[1] = 0;

    int        phase = 0;
    int        name_col = 0;
    int        desc_col = 0;
    int        collecting_plural = 0;
    int        rank_set = 0;
    const char *p = line;

    /* Outer loop: engine bound is `param_3 != 0x100` (256 iter). */
    for (int iter = 0; iter < 0x100; iter++) {
        if (phase == 0) {
            char c = *p;
            if (c == '#') {
                /* Big sub-sequence: 5 ints + 3 helpers, then to phase 1. */
                p++;
                r->price = atoi_pos(p);
                if (!scan_past_hash(&p)) return;
                r->attack = atoi_pos(p);
                if (!scan_past_hash(&p)) return;
                r->defense = atoi_pos(p);
                if (!scan_past_hash(&p)) return;
                r->magic_attack = atoi_pos(p);
                if (!scan_past_hash(&p)) return;
                r->magic_defense = atoi_pos(p);
                if (!scan_past_hash(&p)) return;
                item_parse_attr(r, p, out->categories[r->category].singular);
                if (!scan_past_hash(&p)) return;
                item_parse_stock(r, p);
                if (!scan_past_hash(&p)) return;
                item_parse_audience(r, p);
                if (!scan_past_hash(&p)) return;
                phase = 1;
                desc_col = 0;
                /* Engine: LAB_004915be resets iVar5, then advances pcVar8++.
                 * Our scan_past_hash already advanced past the `#`. So
                 * we don't `p++` here — but we DO advance one iter,
                 * matching engine. */
                continue;
            }
            /* Not '#'. Check rank-digit branch. */
            if (!rank_set && c >= '0' && c <= '9') {
                r->rank = atoi_pos(p);
                rank_set = 1;
                /* Engine: scan to '#', advance past it. */
                if (!scan_past_hash(&p)) {
                    fprintf(stderr,
                            "tables_item: record %d missing '#' after rank\n",
                            item_id);
                    return;
                }
                c = *p;
                /* Fall through to name char handling on the post-# char. */
            }
            if (c == '+') {
                collecting_plural = 1;
                name_col = 0;
                p++;
                continue;
            }
            /* Plain name char: write to singular+plural, or plural-only. */
            if (name_col < ITEM_NAME_LEN - 1) {
                if (!collecting_plural) {
                    r->singular[name_col]     = c;
                    r->singular[name_col + 1] = 0;
                }
                r->plural[name_col]     = c;
                r->plural[name_col + 1] = 0;
                name_col++;
            }
            p++;
            continue;
        }
        if (phase == 1) {
            char c = *p;
            if (c == '\r' || c == '\n') {
                r->valid = 1;
                return;
            }
            if (c == '#') {
                phase = 2;
                desc_col = 0;
                p++;
                continue;
            }
            if (desc_col < ITEM_DESC_LEN - 1) {
                r->desc_line1[desc_col]     = c;
                r->desc_line1[desc_col + 1] = 0;
                desc_col++;
            }
            p++;
            continue;
        }
        /* phase == 2 */
        char c = *p;
        if (c == '/' || c == '\r' || c == '\n') {
            r->valid = 1;
            return;
        }
        if (desc_col < ITEM_DESC_LEN - 1) {
            r->desc_line2[desc_col]     = c;
            r->desc_line2[desc_col + 1] = 0;
            desc_col++;
        }
        p++;
    }
    /* Fell off the 256-iter cap. Engine sets valid anyway. */
    r->valid = 1;
}

/* ============================================================ */
/* Dispatcher — outer per-line loop                              */
/* ============================================================ */

/* Read one line from `*p`, copy up to `cap-1` bytes into `out`, and
 * advance `*p` past the line terminator. Returns the number of
 * line-content bytes (excluding the terminator). */
static size_t read_line(const unsigned char **p, const unsigned char *end,
                        char *out, size_t cap)
{
    size_t n = 0;
    while (*p < end && **p != '\r' && **p != '\n' && **p != '\0') {
        if (n + 1 < cap) {
            out[n] = (char)**p;
        }
        n++;
        (*p)++;
    }
    if (n < cap) out[n] = 0;
    else out[cap - 1] = 0;
    /* Skip a CR LF pair or single terminator. */
    if (*p < end && **p == '\r') (*p)++;
    if (*p < end && **p == '\n') (*p)++;
    return n;
}

void tables_parse_item(const unsigned char *data, size_t size,
                       item_state_t *out)
{
    memset(out, 0, sizeof *out);
    parse_reset_scratch();

    const unsigned char *p   = data;
    const unsigned char *end = data + size;

    /* Engine writes \0 sentinel at buf[size] after malloc; with our
     * size-bounded reads we don't need that. */

    /* Line scratch: engine local_27c[0x20..] spans ~600 bytes. */
    char line[1024];

    while (p < end) {
        size_t n = read_line(&p, end, line, sizeof line);
        if (n == 0) continue;                 /* blank → skip      */
        char first = line[0];
        if (first == '\r' || first == '\n' || first == '/') continue;

        if (first == ':') {
            /* Category header — fill scratch but don't commit yet.
             * Commit happens at the next record's category index. */
            parse_category_header(line + 1);
            continue;
        }

        if (first == ' ') {
            /* Engine's defensive skip path (DAT_005cacf4 = ' '). */
            continue;
        }

        if (first < '0' || first > '9') {
            /* Engine: MessageBoxA "不明な行" (unknown line). Port logs. */
            fprintf(stderr, "tables_item: unknown line: %s\n", line);
            continue;
        }

        /* Item record. Parse leading 4-digit id; engine bounds 0..9999. */
        int32_t item_id = atoi_pos(line);
        if (item_id < 0 || item_id >= 10000) continue;
        /* Engine guards `line[5] not in {\r, \n}` — i.e. line must have
         * content after the `NNNN:` prefix. */
        if (n < 6 || line[5] == '\r' || line[5] == '\n') continue;

        if (out->count >= ITEM_MAX_RECORDS) {
            fprintf(stderr,
                    "tables_item: dropping record %d — slot cap %d reached\n",
                    item_id, ITEM_MAX_RECORDS);
            break;
        }

        parse_item_record(out, line + 5, item_id, out->count);
        out->count++;
    }
}

int32_t tables_item_resolve(const item_state_t *state, const char *name)
{
    if (state == NULL || name == NULL) return -1;
    for (int i = 0; i < state->count; i++) {
        if (state->records[i].valid != 1) continue;
        if (strncmp(state->records[i].singular, name, ITEM_NAME_LEN) == 0) {
            return state->records[i].item_id;
        }
    }
    return -1;
}

/* FUN_004681f6 @ 0x4681f6 — item-id → record-slot lookup.
 *
 * Engine body (docs/decompiled/by-address/4681f6.c):
 *
 *     iVar1 = 0;
 *     if (DAT_005c80ac != 0) {
 *         piVar2 = &DAT_095d3804;        // &records[0].item_id
 *         do {
 *             if (*piVar2 == param_1) return iVar1;
 *             iVar1++;
 *             piVar2 += 0xb3;             // stride 0x2cc bytes
 *         } while (iVar1 != DAT_005c80ac);
 *     }
 *     return -1;
 *
 * Verbatim translation: the engine reads the `item_id` field (offset
 * +0x34) of each populated record up through `count` and returns the
 * first match's index, or -1.  No `valid` check (matches engine — the
 * struct's `item_id` is set by parse_item_record before count is
 * incremented, so records[0..count-1].item_id is always populated).
 */
int32_t tables_item_find_slot_by_id(const item_state_t *state,
                                    int32_t item_id)
{
    CALL_TRACE_ENTER(0x4681f6u);
    if (state == NULL) return -1;
    for (int i = 0; i < state->count; i++) {
        if (state->records[i].item_id == item_id) {
            return i;
        }
    }
    return -1;
}
