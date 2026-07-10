/*
 * customer_roster.c — customer eligibility / spawn "roster scan" helpers.
 * See customer_roster.h.  RE: docs/findings/roster-scan-RE.md.
 *
 * Every function here was transcribed against the objdump of the unpacked
 * retail exe, not just the Ghidra decompile — the two disagree in ways
 * that matter for parity (gotcha #1, Ghidra FPU/index drops):
 *   - FUN_0045e55c hid a per-tier f32 weight MULTIPLIER (DAT_005c6bd0)
 *     as a bogus argless `__ftol()`.
 *   - FUN_0040a68f's distance→band classification (5 f32 thresholds) was
 *     folded out of the function and mis-attributed to the callers.
 * The exact float bit-patterns are embedded below (via f32_bits) so the
 * host SSE build agrees with the x87 retail values.
 */

#include <math.h>       /* sqrt */
#include <string.h>     /* memcpy — exact f32 bit-pattern materialisation */

#include "customer_roster.h"
#include "tables_item.h"          /* g_item, tables_item_find_slot_by_id (FUN_004681f6) */
#include "tables_oder.h"          /* g_oder — the oder (item-request) pool (DAT_06a5dbd8) */
#include "save_bank.h"            /* SAVE_BANK_FIELD_* */
#include "scene1_shop_display.h"  /* SHOP_DISPLAY_TIER_SELECTOR (0xb378) */
#include "rng.h"                  /* rng_next15 (FUN_005041f6) */

/* Materialise an exact 32-bit float from its IEEE bit pattern (so the port
 * compares against the retail rodata constant bit-for-bit, not a decimal
 * literal the compiler might round differently). */
static float f32_bits(uint32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }

/* __ftol — x87 truncate-toward-zero to int32 (= C cast for finite values). */
static int32_t ftol_f(float v) { return (int32_t)v; }

/* ── shop attribute centroid (DAT_0438b4b8/b4bc) ─────────────────────── */
static int32_t s_centroid_x;   /* DAT_0438b4b8 */
static int32_t s_centroid_y;   /* DAT_0438b4bc */

int32_t roster_centroid_x(void) { return s_centroid_x; }
int32_t roster_centroid_y(void) { return s_centroid_y; }
void    roster_set_centroid(int32_t x, int32_t y) { s_centroid_x = x; s_centroid_y = y; }

/* The 4 per-decoration attribute-coord tables (verbatim from the retail
 * rodata at 0x5ccf4c / 0x5ccfc4 / 0x5cd03c / 0x5cd07c — each a run of
 * {y@+0, x@+4} int32 pairs; stored here as {x, y}).  Indexed by the
 * matching decoration selector; retail does no bounds check (the save
 * value is always in range). */
static const int8_t k_deco_floor[15][2] = {   /* DAT_04510580 (sel0) */
    {-4,0},{0,3},{-1,4},{-2,-1},{-2,-1},{-4,4},{5,0},{2,4},
    {-1,1},{-5,0},{-2,2},{1,4},{5,-3},{3,3},{0,-10},
};
static const int8_t k_deco_wall[15][2] = {    /* DAT_0451057c (sel1) */
    {0,0},{-5,1},{5,-1},{4,-1},{-2,0},{-2,2},{-2,2},{-10,0},
    {0,-10},{2,4},{5,-3},{2,2},{3,-2},{5,4},{3,3},
};
static const int8_t k_deco_carpet[8][2] = {   /* DAT_04510588 (sel2) */
    {-2,0},{-5,-1},{0,4},{-3,0},{2,3},{3,4},{0,-10},{10,0},
};
static const int8_t k_deco_table[8][2] = {    /* DAT_04510584 (sel3) */
    {2,0},{0,2},{5,0},{5,-3},{3,3},{10,0},{3,3},{0,-10},
};

void roster_compute_centroid(const uint8_t *bank)
{
    const int32_t *b = (const int32_t *)bank;
    int floor  = b[SAVE_BANK_FIELD_DECO_FLOOR];
    int wall   = b[SAVE_BANK_FIELD_DECO_WALL];
    int carpet = b[SAVE_BANK_FIELD_DECO_CARPET];
    int table  = b[SAVE_BANK_FIELD_DECO_TABLE];

    int32_t x = k_deco_floor[floor][0]  + k_deco_wall[wall][0]
              + k_deco_carpet[carpet][0] + k_deco_table[table][0];
    int32_t y = k_deco_floor[floor][1]  + k_deco_wall[wall][1]
              + k_deco_carpet[carpet][1] + k_deco_table[table][1];

    /* Nudge by every displayed item's attribute bits (all.c:84692-84723). */
    const int32_t *grid = b + SAVE_BANK_FIELD_DISPLAY_GRID;
    for (int i = 0; i < SAVE_BANK_DISPLAY_GRID_CELLS; i++) {
        int32_t cell = grid[i];
        if (cell == -1)
            continue;
        int slot = tables_item_find_slot_by_id(&g_item, cell >> 6);
        if (slot < 0)
            continue;   /* retail has no guard; a display cell always resolves */
        int32_t id = g_item.records[slot].item_id;
        int32_t step = (2999 < id && id < 0xc1c) ? 3 : 1;
        uint32_t attr = g_item.records[slot].attr_mask;
        if (attr & 0x200)  x += step;
        if (attr & 0x400)  x -= step;
        if (attr & 0x4000) y += step;
        if (attr & 0x8000) y -= step;
    }

    if (x >  0xd) x =  0xd;
    if (x < -0xd) x = -0xd;
    if (y >  0xd) y =  0xd;
    if (y < -0xd) y = -0xd;
    s_centroid_x = x;
    s_centroid_y = y;
}

/* ── FUN_0040a68f — attribute-distance band classifier ────────────────── */
int32_t roster_dist_band(int32_t px, int32_t py)
{
    float dx = (float)(s_centroid_x - px);
    float dy = (float)(s_centroid_y - py);
    float d2 = dx * dx + dy * dy;
    /* retail skips the sqrt when d2 <= 0 (leaving dist = d2 = 0). */
    float dist = (d2 > 0.0f) ? (float)sqrt((double)d2) : d2;

    if (dist <  f32_bits(0x3f93b13b)) return 4;   /* 1.153846  */
    if (dist <  f32_bits(0x405d89d8)) return 3;   /* 3.461999  */
    if (dist <  f32_bits(0x4113b13b)) return 2;   /* 9.230769  */
    if (dist <  f32_bits(0x414b13b1)) return 1;   /* 12.692307 */
    if (dist <= f32_bits(0x41700000)) return 0;   /* 15.0      */
    return -1;
}

/* ── FUN_0045e55c — customer item-preference weight over the display ──── */

/* Row-0 "counter" columns that earn the ×3 front bonus — retail rodata
 * DAT_005c6be0..DAT_005c6bfc. */
static const int32_t k_front_cols[7] = { 1, 2, 3, 4, 11, 12, 13 };

/* Per-tier weight multiplier — retail rodata DAT_005c6bd0 (f32).  Index 0
 * (1.0) is never used (guarded by tier>0); tiers 1/2/3 = 6/7, 2/3, 3/7. */
static const uint32_t k_tier_mult_bits[4] = {
    0x3f800000u, 0x3f5b6db7u, 0x3f2aaaabu, 0x3edb6db7u,
};

int32_t roster_customer_weight(const uint8_t *bank, const kyaku_record_t *kr)
{
    const int32_t *b = (const int32_t *)bank;
    const int32_t *grid = b + SAVE_BANK_FIELD_DISPLAY_GRID;
    int32_t total = 0;

    for (int row = 0; row < SAVE_BANK_DISPLAY_GRID_ROWS; row++) {
        for (int col = 0; col < SAVE_BANK_DISPLAY_GRID_COLS; col++) {
            int32_t cell = grid[row * SAVE_BANK_DISPLAY_GRID_COLS + col];
            if (cell == -1)
                continue;
            int slot = tables_item_find_slot_by_id(&g_item, cell >> 6);
            int32_t w = 0;
            if (slot >= 0) {
                if ((kr->like_attr_mask & g_item.records[slot].attr_mask) != 0)
                    w = 2;
                for (int k = 0; k < kr->like_count; k++)
                    if (g_item.records[slot].category == kr->like_kinds[k])
                        w += 4;
            }
            if (row == 0) {
                for (int f = 0; f < 7; f++)
                    if (col == k_front_cols[f]) { w *= 3; break; }
            }
            total += w;
        }
    }

    int tier = b[SHOP_DISPLAY_TIER_SELECTOR];
    if (tier > 0)
        total = ftol_f((float)total * f32_bits(k_tier_mult_bits[tier]));
    if (tier == 1) total += 3;
    if (tier == 2) total += 6;
    if (tier == 3) total += 10;
    return total;
}

/* ── FUN_0045e505 — the roster shuffle (n draws, fixed-n index) ────────── */
void roster_shuffle(int32_t *arr, uint32_t n)
{
    for (uint32_t pass = 0; pass < n; pass++) {
        uint32_t j = (uint32_t)rng_next15() % n;   /* rng%n against the FIXED n */
        int32_t tmp = arr[j];
        arr[j] = arr[pass];
        arr[pass] = tmp;
    }
}

/* ── FUN_0045e6e0 — daily "3 relics" special-event state (0..4) ────────── */

/* The three "relic" item ids the event watches for (retail literals). */
#define RELIC_ID_A 0xc1d
#define RELIC_ID_B 0xc26
#define RELIC_ID_C 0xc22

static void roster_relic_accumulate(uint32_t *have, int32_t cell)
{
    if (cell == -1)
        return;
    int32_t id = cell >> 6;
    if (id == RELIC_ID_A) *have |= 1;
    if (id == RELIC_ID_B) *have |= 2;
    if (id == RELIC_ID_C) *have |= 4;
}

int32_t roster_event_state(const uint8_t *bank)
{
    /* The gate byte (DAT_0450f463) set → the event is disabled entirely. */
    if (bank[SAVE_BANK_EVENT_FLAG_BYTE_OFF] != 0)
        return 0;

    const int32_t *b = (const int32_t *)bank;
    uint32_t have = 0;

    /* Relic scan #1: the inventory item table (dword 6, count 0xaec6). */
    int inv_count = b[SAVE_BANK_FIELD_ITEM_COUNT];
    const int32_t *items = b + SAVE_BANK_ITEM_TABLE_DWORD;
    for (int i = 0; i < inv_count; i++)
        roster_relic_accumulate(&have, items[i]);

    /* Relic scan #2: the 15×20 display grid. */
    const int32_t *grid = b + SAVE_BANK_FIELD_DISPLAY_GRID;
    for (int i = 0; i < SAVE_BANK_DISPLAY_GRID_CELLS; i++)
        roster_relic_accumulate(&have, grid[i]);

    if (have != 7)
        return 1;   /* not all three relics present */

    /* All three present: bucket by days-since-last-progression.  day is int,
     * the stored day (DAT_0450f462) is an unsigned byte. */
    int32_t day_delta = b[SAVE_BANK_FIELD_SHOP_DAY] - bank[SAVE_BANK_EVENT_DAY_BYTE_OFF];
    if (day_delta > 1)
        return (day_delta > 3) + 3;   /* 3 (2..3 days) or 4 (>3 days) */
    return 2;
}

/* ── FUN_0045ed12 — row-0 quest-item range gate (0/1) ─────────────────── */

/* Retail rodata DAT_005c6c14 — the row-0 "counter" columns tested here.
 * (Same values as k_front_cols but a distinct rodata table.) */
static const int32_t k_range_cols[7] = { 1, 2, 3, 4, 11, 12, 13 };

int32_t roster_range_gate(const uint8_t *bank)
{
    const int32_t *grid = (const int32_t *)bank + SAVE_BANK_FIELD_DISPLAY_GRID;

    /* retail's outer loop advances by 0x14 and exits at 0x14 → it runs for
     * ROW 0 only (columns iterated below). */
    for (int j = 0; j < 7; j++) {
        /* ★ index-mismatch quirk (RE roster-scan §helpers): occupancy is
         * tested at the counter column k_range_cols[j], but the item id is
         * resolved from the SEQUENTIAL column j — the two reads hit
         * different cells.  Replicated exactly. */
        if (grid[k_range_cols[j]] == -1)
            continue;
        int slot = tables_item_find_slot_by_id(&g_item, grid[j] >> 6);
        if (slot < 0)
            /* The sequential cell can be empty (>>6 of -1) even when the
             * guard cell is occupied; retail then reads OOB garbage, which
             * essentially never lands in the quest ranges.  The port treats
             * it as "no match".  (engine-quirk #131.) */
            continue;
        int32_t id = g_item.records[slot].item_id;
        if (((4000 < id && id < 0xfa7) || id == 0xfab) ||
            (id == 0xfb0 || (0xfb8 < id && id < 0xfc3)))
            return 1;
    }
    return 0;
}

/* ── FUN_0045e80f — pick an oder (item request) for a customer ─────────── */

/* Retail rodata DAT_005c6c00 — per-oder-tier closeness thresholds. */
static const int32_t k_quality_thresh[5] = { 0, 3, 10, 17, 22 };

int32_t roster_pick_item(const uint8_t *bank, const kyaku_record_t *kr,
                         int32_t closeness_idx, const roster_news_event_t *ev)
{
    /* local_c = min(closeness[idx]/10, shop_rank).  closeness is a signed
     * int16 stored one-per-dword (stride 4 bytes). */
    int32_t closeness = *(const int16_t *)(bank + SAVE_BANK_FIELD_CLOSENESS * 4
                                           + closeness_idx * 4);
    int32_t rank = ((const int32_t *)bank)[SAVE_BANK_FIELD_SHOP_RANK];
    int32_t cap = closeness / 10;
    if (rank <= closeness / 10)
        cap = rank;

    uint32_t chosen = 0xffffffffu;   /* local_10 — the rng-picked match ordinal */
    for (int pass = 0; pass < 2; pass++) {
        int matches = 0;             /* param_1 — running match counter */
        for (int i = 0; i < g_oder.count; i++) {
            const struct oder_entry *o = &g_oder.entries[i];
            if (!ev->active) {
                /* quality gate: the oder's tier threshold must fit `cap`. */
                if (k_quality_thresh[o->level_minus_1] > cap)
                    continue;
                int is_match = (o->attr_mask & kr->like_attr_mask) != 0;
                for (int k = 0; k < kr->like_count; k++)
                    if (o->attr_index == kr->like_kinds[k])
                        is_match = 1;
                if (!is_match)
                    continue;
            } else if (ev->attr_mask == 0) {
                if (o->attr_index != ev->target_id)
                    continue;   /* featured by exact target id */
            } else if ((o->attr_mask & ev->attr_mask) == 0) {
                continue;       /* featured by attribute mask */
            }
            /* MATCH */
            if (pass == 1 && chosen == (uint32_t)matches)
                return i;
            matches++;
        }
        if (pass == 0 && matches > 0)
            chosen = (uint32_t)rng_next15() % (uint32_t)matches;
    }
    return -1;
}
