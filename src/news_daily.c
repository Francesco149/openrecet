/*
 * news_daily.c — daily-news generator FUN_00436623 + picker FUN_004363c6
 * + list reset FUN_00436180 + attr display name FUN_0049e6b3 + market
 * price-trend classifier FUN_004361b2.
 *
 * RE + objdump verification: docs/findings/news-daily-RE.md.  Every rng
 * draw is marked ★ and mirrors the engine's draw order/count exactly
 * (data-dependent — load-bearing for RNG parity).  Byte arithmetic on
 * the duration counter is kept at uint8 width with int8 signed
 * compares, matching the engine's al-register math.
 */

#include <stdio.h>      /* sprintf — FUN_005038ff */
#include <string.h>

#include "news_daily.h"
#include "tables_news.h"    /* g_news (news.txt, DAT_056e0e00 rows) */
#include "tables_item.h"    /* g_item + tables_item_find_slot_by_id (FUN_004681f6) */
#include "save_bank.h"
#include "rng.h"            /* rng_next15 (FUN_005041f6 via FUN_00471084) */
#include "audio_mci.h"      /* audio_mci_record_command (FUN_00451874, debug) */

int32_t g_news_ticker_timer = 0;   /* DAT_0438b92c */

/* ── row/name helpers ──────────────────────────────────────────────── */

/* Engine news-def row for a 1-based news id: 0x56e0d44 + id*0xbc =
 * records[id-1].  id==0 (picker returned -1) dereferences the 0xbc
 * bytes of BSS BELOW records[0] — zero at runtime — so the port hands
 * back a zeroed sentinel (RE doc "id==0 edge": zero draws, entry stays
 * id 0, not counted).  ids past the array (corrupt save) also map to
 * the sentinel; in-range unparsed rows are zero-filled like engine BSS. */
static const news_record_t k_news_row0;   /* zero sentinel */

static const news_record_t *news_row(int id)
{
    if (id < 1 || id > NEWS_MAX_RECORDS)
        return &k_news_row0;
    return &g_news.records[id - 1];
}

/* Item PLURAL name for the '<'-splice (engine &DAT_095d385a + slot*0x2cc).
 * The engine has no bounds check; a slot outside the records array reads
 * adjacent memory (only reachable via a '<' body with no matched target,
 * absent from vendor data) — the port guards to "" instead of UB. */
static const char *item_plural(int slot)
{
    if (slot < 0 || slot >= ITEM_MAX_RECORDS)
        return "";
    return g_item.records[slot].plural;
}

/* FUN_0049e6b3 — attr-mask → display name. */
const char *news_attr_display_name(int mask)
{
    switch (mask) {
    case 0x0001: return "weapons";
    case 0x0002: return "pieces of armor";
    case 0x0004: return "home decorations";
    case 0x0008: return "clothes";
    case 0x0010: return "accessories";
    case 0x0020: return "valuable things";
    case 0x0040: return "metal things";
    case 0x0080: return "dinner items";
    case 0x0100: return "sweets";
    case 0x0200: return "fancy things";
    case 0x0400: return "plain things";
    case 0x0800: return "rare things";
    case 0x1000: return "Cold Weather Gear";
    case 0x2000: return "Food";
    case 0x4000: return "holy things";
    case 0x8000: return "sinister things";
    /* engine default &DAT_005fd740 = SJIS ダミー ("dummy") */
    default:     return "\x83_\x83~\x81[";
    }
}

/* ── bank accessors (byte base, matching the engine's mixed width) ─── */

static uint8_t *entry_at(uint8_t *bank, int i)
{
    return bank + SAVE_BANK_NEWS_ENTRY_BYTE_OFF + i * 0xc;
}
static int32_t *entry_target(uint8_t *bank, int i)
{
    return (int32_t *)entry_at(bank, i);
}
static int32_t *entry_id(uint8_t *bank, int i)
{
    return (int32_t *)(entry_at(bank, i) + 4);
}
/* trend char @+8, duration int8 @+9 */

static char *headline_row(uint8_t *bank, int32_t n)
{
    return (char *)bank + SAVE_BANK_NEWS_HL_TEXT_BYTE_OFF
           + n * SAVE_BANK_NEWS_HL_ROW_BYTES;
}
static int32_t *headline_count(uint8_t *bank)
{
    return (int32_t *)bank + SAVE_BANK_FIELD_NEWS_HL_COUNT;
}
static int16_t *pairs_at(uint8_t *bank)
{
    return (int16_t *)(bank + SAVE_BANK_NEWS_PAIRS_BYTE_OFF);
}

/* ── FUN_004363c6 — eligible-row picker ────────────────────────────── */

/* A news-def row is date-eligible when its 時期 window covers `day`
 * (period_end==999 = evergreen).  category==-100 rows are the special
 * day-range pool (generator step 8) and are EXCLUDED here. */
static int row_in_period(const news_record_t *r, int day)
{
    return r->period_start <= day && (day <= r->period_end ||
                                      r->period_end == 999);
}

int news_pick_def(int day)
{
    if (day == 9)
        return 0;                       /* scripted first news (id 1) */
    int eligible = 0;
    for (int k = 0; k < g_news.count; k++) {
        const news_record_t *r = &g_news.records[k];
        if (r->category != NEWS_CATEGORY_DASH && row_in_period(r, day))
            eligible++;
    }
    if (eligible <= 0)
        return -1;                      /* no draw */
    uint32_t pick = rng_next15() % (uint32_t)eligible;      /* ★ */
    uint32_t seen = 0;
    for (int k = 0; k < g_news.count; k++) {
        const news_record_t *r = &g_news.records[k];
        if (r->category != NEWS_CATEGORY_DASH && row_in_period(r, day)) {
            if (seen == pick)
                return k;
            seen++;
        }
    }
    return -1;                          /* unreachable */
}

/* ── FUN_00436180 — list reset ─────────────────────────────────────── */

void news_list_reset(uint8_t *bank)
{
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        uint8_t *e = entry_at(bank, i);
        *(int32_t *)(e + 4) = -1;       /* news_id */
        e[8] = 0;                       /* trend  */
        e[9] = 0;                       /* duration */
    }
    *headline_count(bank) = 0;
}

/* ── the '<'-splice body copy (asm 436932-4369bc / 436c23-436cb0) ──── */

/* Copies `body` into the headline row char-by-char; a '<' consumes the
 * marker ('<' + 2 more source bytes = one SJIS char) and splices the
 * picked item's plural name in its place.  The just-written char is
 * re-checked each iteration (engine loop shape preserved). */
static void headline_splice(char *dst, const char *body, const char *name)
{
    int a = 0;
    const char *src = body;
    dst[0] = src[0];
    while (dst[a] != '\0') {
        if (dst[a] == '<') {
            src += 2;
            for (const char *n = name; *n != '\0'; n++)
                dst[a++] = *n;
        } else {
            a++;
        }
        src++;
        dst[a] = *src;
    }
}

/* ── FUN_00436623 — the daily news generator ───────────────────────── */

void news_daily_update(uint8_t *bank)
{
    char dbg[256];
    const int32_t *bankw = (const int32_t *)bank;
    const int day  = bankw[SAVE_BANK_FIELD_SHOP_DAY];
    const int rank = bankw[SAVE_BANK_FIELD_SHOP_RANK];

    *headline_count(bank) = 0;

    /* ── step 2: pick ONE new news into the first free slot (id==0) ── */
    int free_slot = -1;
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        if (*entry_id(bank, i) == 0) { free_slot = i; break; }
    }
    if (free_slot >= 0) {
        uint8_t *e = entry_at(bank, free_slot);
        int32_t *id = entry_id(bank, free_slot);
        *id = news_pick_def(day) + 1;                        /* ★ ≤1 draw */

        int tries = 0, aborted = 0;
        for (;;) {
            tries++;
            if (tries > 100) {          /* give up: no news today */
                *id = 0;
                aborted = 1;
                break;
            }
            /* dedup vs every OTHER active entry (0 < id < 500) */
            int conflict = 0;
            const news_record_t *cand = news_row(*id);
            for (int j = 0; j < SAVE_BANK_NEWS_LIST_COUNT; j++) {
                if (j == free_slot) continue;
                int32_t oid = *entry_id(bank, j);
                if (oid <= 0 || oid >= NEWS_DEBUG_ID) continue;
                if (oid == *id) { conflict = 1; break; }
                const news_record_t *ex = news_row(oid);
                if (cand->rate != 0) {
                    if (ex->rate != 0) {
                        /* the EXISTING row's attr_mask picks the field */
                        int eq = ex->attr_mask == 0
                                 ? ex->category  == cand->category
                                 : ex->attr_mask == cand->attr_mask;
                        if (eq) conflict = 1;
                    }
                } else if (ex->rate == 0) {
                    conflict = 1;       /* one generic news at a time */
                }
                if (conflict) break;
            }
            /* eligibility gate (asm 43674a-436788) */
            if (!conflict) {
                if (cand->item_id > 0) {
                    /* item-targeted row: OK */
                } else if (cand->attr_mask == -1) {
                    conflict = 1;       /* 特殊 rows never random-picked */
                } else if (cand->category == -1 && cand->attr_mask == 0) {
                    conflict = 1;       /* failed-lookup row */
                }
            }
            if (!conflict)
                break;                  /* accepted */
            *id = news_pick_def(day) + 1;                    /* ★ reroll */
        }

        if (!aborted) {
            /* acceptance block (asm 4367ad-4369cf; the >100-tries abort
             * jumps past it).  NB the engine runs this for id==0 from a
             * -1 pick too (sentinel row: writes trend 'd', empty body,
             * dur 2; zero draws; not counted). */
            const news_record_t *row = news_row(*id);
            char *trend = (char *)(e + 8);
            *trend = (char)row->rate;
            if (*trend == 0)
                *trend = 'd';
            sprintf(headline_row(bank, *headline_count(bank)), "%s",
                    row->body);
            /* duration = base byte + ★rng%range (iff range>0) + 1, min 2 */
            e[9] = (uint8_t)row->dur_base;
            if (row->dur_range > 0)
                e[9] = (uint8_t)(e[9] + rng_next15() %       /* ★ */
                                        (uint32_t)row->dur_range);
            e[9] = (uint8_t)(e[9] + 1);
            if ((int8_t)e[9] < 2)
                e[9] = 2;
            *entry_target(bank, free_slot) = -1;
            if (row->price_lo >= 0) {
                /* two-pass target-item scan (asm 436857-43690c): pass 0
                 * counts matches, ★1 draw iff >0, pass 1 picks. */
                int pick = 0, matches = 0;
                for (int pass = 0; pass < 2; pass++) {
                    int found = -1;     /* running match ordinal (ebx) */
                    matches = 0;
                    for (int s = 0; s < g_item.count; s++) {
                        const item_record_t *it = &g_item.records[s];
                        if (it->valid != 1) continue;
                        if (it->price <= 0) continue;
                        if (it->price < row->price_lo ||
                            it->price > row->price_hi) continue;
                        if (row->attr_mask == -1) continue;
                        if (row->attr_mask == 0) {
                            if (it->category != row->category) continue;
                        } else if ((it->attr_mask &
                                    (uint32_t)row->attr_mask) == 0) {
                            continue;
                        }
                        matches++;
                        found++;
                        if (pass == 1 && found == pick) {
                            *entry_target(bank, free_slot) = it->item_id;
                            break;
                        }
                    }
                    if (pass == 0 && matches > 0)
                        pick = (int)(rng_next15() %          /* ★ */
                                     (uint32_t)matches);
                }
                int name_slot = *entry_target(bank, free_slot);
                if (*trend != '\0' && name_slot > 10)
                    name_slot = tables_item_find_slot_by_id(&g_item,
                                                            name_slot);
                headline_splice(headline_row(bank, *headline_count(bank)),
                                row->body, item_plural(name_slot));
            }
            if (*id != 0)
                *headline_count(bank) += 1;
        }
    }

    /* ── step 3: debug pair dump "IT %4d " (dormant, FUN_00451874) ── */
    int16_t *pairs = pairs_at(bank);
    int best_idx = -1, best_mult = 0;
    for (int i = 0; i < SAVE_BANK_NEWS_PAIRS_COUNT; i++) {
        sprintf(dbg, "IT %4d ", pairs[i * 2]);
        audio_mci_record_command(0, i + 10, dbg);
        if (pairs[i * 2] != 0) {
            int mult = 0;
            for (int j = 0; j < SAVE_BANK_NEWS_PAIRS_COUNT; j++)
                if (pairs[i * 2] == pairs[j * 2]) mult++;
            if (best_mult < mult) {     /* strict > keeps the FIRST max */
                best_mult = mult;
                best_idx  = i;
            }
        }
    }

    /* ── step 4: boom roll — ★1 draw iff ANY pair active (even p=0) ── */
    int boom = 0, hot_id = 0;
    if (best_idx >= 0) {
        hot_id = pairs[best_idx * 2];
        uint32_t p = 0;
        if (best_mult == 4) p = 10;
        if (best_mult == 5) p = 25;
        if (best_mult == 6) p = 50;
        if (best_mult == 7) p = 80;
        if (best_mult >= 8) p = 100;
        if (rng_next15() % 100u < p)                         /* ★ */
            boom = 1;
    }

    /* ── step 5: pair TTL decrement ── */
    for (int i = 0; i < SAVE_BANK_NEWS_PAIRS_COUNT; i++) {
        if (pairs[i * 2] != 0) {
            pairs[i * 2 + 1] -= 1;
            if ((uint16_t)pairs[i * 2 + 1] == 0) {   /* engine `ja` */
                pairs[i * 2]     = 0;
                pairs[i * 2 + 1] = 0;
            }
        }
    }

    /* ── step 6: player-driven boom news (rank gate ≥9) ── */
    if (best_idx >= 0 && boom && rank > 8) {
        int slot = -1;
        for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
            if (*entry_id(bank, i) == 0) { slot = i; break; }
        }
        if (slot >= 0) {
            uint8_t *e  = entry_at(bank, slot);
            int32_t *id = entry_id(bank, slot);
            *id = NEWS_BOOM_ID;
            uint32_t p2 = 0;
            if (best_mult == 4) p2 = 10;
            if (best_mult == 5) p2 = 30;
            if (best_mult == 6) p2 = 50;
            if (best_mult == 7) p2 = 70;
            if (best_mult >= 8) p2 = 90;
            if (rng_next15() % 100u < p2)                    /* ★ */
                *id = NEWS_BOOM_ALT_ID;
            const news_record_t *row = news_row(*id);
            /* NB: NO 0→'d' fixup on this path (asm 436b8b). */
            e[8] = (uint8_t)row->rate;
            if (*id == NEWS_BOOM_ALT_ID)
                e[9] = (uint8_t)((rng_next15() & 3) + 2);    /* ★ 2..5 */
            else
                e[9] = (uint8_t)(rng_next15() % 3u + 4);     /* ★ 4..6 */
            *entry_target(bank, slot) = hot_id;
            int name_slot = hot_id;
            if ((char)e[8] != '\0' && name_slot > 10)
                name_slot = tables_item_find_slot_by_id(&g_item,
                                                        name_slot);
            /* clear every pair carrying the (raw) hot item id */
            for (int i = 0; i < SAVE_BANK_NEWS_PAIRS_COUNT; i++) {
                if ((int32_t)(uint16_t)pairs[i * 2] ==
                    *entry_target(bank, slot)) {
                    pairs[i * 2]     = 0;
                    pairs[i * 2 + 1] = 0;
                }
            }
            char *hl = headline_row(bank, *headline_count(bank));
            sprintf(hl, "%s", row->body);
            headline_splice(hl, row->body, item_plural(name_slot));
            *headline_count(bank) += 1;
        }
    }

    /* ── step 7: expiry pass (no rng) ── */
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        uint8_t *e  = entry_at(bank, i);
        int32_t *id = entry_id(bank, i);
        int32_t tgt = *entry_target(bank, i);
        if (*id <= 0 || (int8_t)e[9] <= 0) continue;
        e[9] = (uint8_t)(e[9] - 1);
        if (e[9] != 0) continue;
        char *hl = headline_row(bank, *headline_count(bank));
        const news_record_t *row = news_row(*id);
        char tr = (char)e[8];
        if (*id == NEWS_DEBUG_ID) {
            sprintf(hl, "The price of %s has normalized.",
                    item_plural(tables_item_find_slot_by_id(&g_item, tgt)));
        } else if (tr == '\0') {
            if (tgt != -1) {
                /* ENGINE QUIRK #132: raw target used as SLOT (no id→slot
                 * conversion on this branch — asm 436d28). */
                sprintf(hl, "The %s boom has ended.", item_plural(tgt));
            } else if (row->attr_mask > 0) {
                sprintf(hl, "The %s boom has ended.",
                        news_attr_display_name(row->attr_mask));
            } else {
                sprintf(hl, "The %s boom has ended.", row->name);
            }
        } else if (tr == 'd') {
            if (tgt != -1) {
                sprintf(hl, "The %s boom has ended.",
                        item_plural(tables_item_find_slot_by_id(&g_item,
                                                                tgt)));
            } else if (row->attr_mask > 0) {
                sprintf(hl, "The %s boom has ended.",
                        news_attr_display_name(row->attr_mask));
            } else {
                sprintf(hl, "The %s boom has ended.", row->name);
            }
        } else {
            if (tgt > 0) {
                sprintf(hl, "The price of %s has normalized.",
                        item_plural(tables_item_find_slot_by_id(&g_item,
                                                                tgt)));
            } else if (row->attr_mask > 0) {
                sprintf(hl, "The price of %s has normalized.",
                        news_attr_display_name(row->attr_mask));
            } else {
                sprintf(hl, "The price of %s has normalized.", row->name);
            }
        }
        *headline_count(bank) += 1;
        *id = 0;
    }

    /* ── step 8: day-range story news (day ≥ 10; ★1 draw iff pool>0) ── */
    if (day > 9 && g_news.count != 0) {
        int pool = 0;
        for (int k = 0; k < g_news.count; k++) {
            const news_record_t *r = &g_news.records[k];
            if (r->category == NEWS_CATEGORY_DASH && row_in_period(r, day))
                pool++;
        }
        if (pool > 0) {
            uint32_t pick = rng_next15() % (uint32_t)pool;   /* ★ */
            uint32_t seen = 0;
            for (int k = 0; k < g_news.count; k++) {
                const news_record_t *r = &g_news.records[k];
                if (r->category == NEWS_CATEGORY_DASH &&
                    row_in_period(r, day)) {
                    if (seen == pick) {
                        sprintf(headline_row(bank, *headline_count(bank)),
                                "%s", r->body);
                        *headline_count(bank) += 1;
                        break;
                    }
                    seen++;
                }
            }
        }
    }

    /* ── step 9: per-headline scroll offsets (strlen+4 apart) ── */
    int32_t cum = 0;
    int32_t n = *headline_count(bank);
    for (int32_t i = 0; i < n; i++) {
        ((int32_t *)bank)[SAVE_BANK_FIELD_NEWS_HL_OFFS + i] = cum;
        cum += (int32_t)strlen(headline_row(bank, i)) + 4;
    }
    ((int32_t *)bank)[SAVE_BANK_FIELD_NEWS_HL_TOTAL] = cum;

    /* trailing debug id dump "T %d " (dormant) */
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        sprintf(dbg, "T %d ", *entry_id(bank, i));
        audio_mci_record_command(0x14, i + 10, dbg);
    }
}

/* ── FUN_004361b2 — market price-trend classifier ──────────────────── */

int news_price_trend(const uint8_t *bank, int item_handle, int tutorial_sell)
{
    char dbg[256];
    if (tutorial_sell)
        return 0;
    int acc = 0, hard_down = 0;
    int item_id   = item_handle >> 6;
    int item_slot = tables_item_find_slot_by_id(&g_item, item_id);
    for (int i = 0; i < SAVE_BANK_NEWS_LIST_COUNT; i++) {
        const uint8_t *e = bank + SAVE_BANK_NEWS_ENTRY_BYTE_OFF + i * 0xc;
        int32_t id  = *(const int32_t *)(e + 4);
        int32_t tgt = *(const int32_t *)e;
        char    tr  = (char)e[8];
        if (id < 1) continue;
        int match = 0;
        if (id == NEWS_DEBUG_ID) {
            /* engine pops a debug MessageBoxA("init s3") here — dead in
             * shipped data (nothing writes id 500); port no-ops it. */
            match = tgt == item_id;
        } else {
            const news_record_t *row = news_row(id);
            sprintf(dbg, "SUB%d ", row->item_id);
            audio_mci_record_command(0xf, i + 2, dbg);
            sprintf(dbg, "SUB%d ", id);
            audio_mci_record_command(0x19, i + 2, dbg);
            if (tgt > 0) {
                match = tgt == item_id;
            } else if (row->item_id > 0) {
                match = row->item_id == item_id;
            } else if (row->attr_mask < 1) {
                /* category-name prefix match (FUN_0049ef78): the item's
                 * category singular vs the news row's category singular,
                 * first 4 bytes.  Guard the -1/-100 category indices the
                 * engine would read OOB. */
                int c = row->category;
                if (item_slot >= 0 && c >= 0 && c < ITEM_CATEGORY_COUNT) {
                    int ic = g_item.records[item_slot].category;
                    if (ic >= 0 && ic < ITEM_CATEGORY_COUNT)
                        match = memcmp(g_item.categories[ic].singular,
                                       g_item.categories[c].singular,
                                       4) == 0;
                }
                if (!match) continue;
            } else if (item_slot >= 0 &&
                       (g_item.records[item_slot].attr_mask &
                        (uint32_t)row->attr_mask) != 0) {
                match = 1;
                sprintf(dbg, "SUB %d ", row->attr_mask);
                audio_mci_record_command(0xf, 8, dbg);
                sprintf(dbg, "SUB %d ", id);
                audio_mci_record_command(0xf, 9, dbg);
            }
        }
        if (match && tr != 'd') {
            acc += (int8_t)tr;
            if ((int8_t)tr < -1)
                hard_down = 1;
        }
    }
    if (hard_down)
        return -2;
    if (acc < -1) acc = -1;
    if (acc >  1) acc =  1;
    return acc;
}
