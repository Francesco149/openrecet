/*
 * customer_roster.h — the customer eligibility / spawn "roster scan"
 * (FUN_0045edaa) and its pure helpers.
 *
 * The scan decides WHO walks into the shop and WHAT they want each
 * shop-open (cc08==4).  This header exposes the settled, self-contained
 * helper functions used by the 740-line scan body (all.c 57474-58212,
 * ported in customer_service.c's cs_roster_scan — VERIFIED 1:1 vs retail).
 *
 * RE: docs/findings/roster-scan-RE.md.  Every helper here was transcribed
 * against the objdump (not just the Ghidra decompile — several carry
 * FPU/index quirks Ghidra dropped: gotcha #1).
 *
 * Pure C, no Win32 surface — host-testable (tests/test_customer_roster.c).
 */

#ifndef OPENRECET_CUSTOMER_ROSTER_H
#define OPENRECET_CUSTOMER_ROSTER_H

#include <stdint.h>
#include "tables_kyaku.h"   /* kyaku_record_t */

/* ── FUN_0048439a — shop "attribute centroid" (DAT_0438b4b8/b4bc) ──────
 * The shop's aggregate attribute position, seeded from the 4 furniture
 * choices (wall/floor/carpet/table) via 4 const coordinate tables, then
 * nudged by every displayed item's attribute bits (0x200/0x400 → ±X,
 * 0x4000/0x8000 → ±Y; ±3 for the 3000..0xc1b "big" range, else ±1), and
 * clamped to ±0xd on each axis.  Retail recomputes it on any display
 * change; the roster scan reads it (via roster_dist_band) to weight each
 * customer by how close their attribute coord sits to the shop's.
 *
 * `bank` = the active save slot's working-arena base (byte pointer).
 * Reads the display grid + the 4 decoration selectors from it, and
 * g_item for per-item attr/id.  Result is stored module-globally and
 * read by roster_dist_band(); also exposed via the getters below. */
void    roster_compute_centroid(const uint8_t *bank);
int32_t roster_centroid_x(void);   /* DAT_0438b4b8 */
int32_t roster_centroid_y(void);   /* DAT_0438b4bc */

/* Test/harness hook: set the centroid directly (bypasses the recompute)
 * so roster_dist_band can be exercised in isolation. */
void    roster_set_centroid(int32_t x, int32_t y);

/* ── FUN_0040a68f — attribute-distance band classifier ────────────────
 * dist = (float)sqrt((cx-px)^2 + (cy-py)^2) between the shop centroid
 * (cx,cy) and a customer's attribute coord (px,py = kyaku attr_x/attr_y),
 * then banded through the retail f32 thresholds:
 *   dist <  1.153846  → 4   (closest → strongest spawn bias)
 *   dist <  3.461999  → 3
 *   dist <  9.230769  → 2
 *   dist < 12.692307  → 1
 *   dist <= 15.0      → 0
 *   dist > 15.0       → -1  (too far → penalty band)
 * (The Ghidra decompile hid this classification inside the caller; the
 * asm at 0x40a68f does it here.) */
int32_t roster_dist_band(int32_t px, int32_t py);

/* ── FUN_0045e55c — a customer's item-preference weight over the display ─
 * Scans the 15×20 display grid: each displayed item matching the
 * customer's wanted attribute mask scores +2, matching a wanted category
 * +4; items in row 0 at a "counter" column {1,2,3,4,11,12,13} score ×3.
 * The running total is then scaled by a per-shop-tier f32 multiplier
 * {—,6/7,2/3,3/7} (tiers 1/2/3; NB the Ghidra decompile dropped this
 * multiply as a bogus __ftol — gotcha #1) and a flat tier bonus
 * (+3/+6/+10).  Pure (no RNG).  `bank` = active-slot arena base;
 * reads the display grid + tier from it and g_item for per-item fields. */
int32_t roster_customer_weight(const uint8_t *bank, const kyaku_record_t *kr);

/* ── FUN_0045e505 — the roster shuffle ────────────────────────────────
 * NOT a textbook Fisher-Yates: n passes, each drawing rng()%n against the
 * FIXED n (not a shrinking range) and swapping arr[pass] ↔ arr[rng%n].
 * Consumes exactly n rng_next15() draws — load-bearing for RNG parity. */
void    roster_shuffle(int32_t *arr, uint32_t n);

/* ── FUN_0045e6e0 — daily "3 relics" special-event state (0..4) ────────
 * Reads the working slot (`bank`) only.  Returns:
 *   0  the event story-flag (EVENT_FLAG_BYTE) is set → event disabled
 *   1  not all three relic items (id 0xc1d/0xc26/0xc22) are held or on
 *      display
 *   2  all three present, ≤1 day since the last progression
 *   3  all three present, 2..3 days since
 *   4  all three present, >3 days since
 * The relic scan walks BOTH the inventory item table (dword 6, count at
 * ITEM_COUNT 0xaec6) and the 15×20 display grid.  Pure (no RNG). */
int32_t roster_event_state(const uint8_t *bank);

/* ── FUN_0045ed12 — row-0 quest-item range gate (0/1) ─────────────────
 * Returns 1 iff any display-grid ROW-0 counter column {1,2,3,4,11,12,13}
 * holds an item whose id falls in the quest ranges
 * (4000<id<0xfa7 | id==0xfab | id==0xfb0 | 0xfb8<id<0xfc3), else 0.
 * ★ Faithful index-mismatch quirk: retail tests the CELL at the counter
 *   column (k_range_cols[j]) for occupancy but resolves the ITEM id from
 *   the SEQUENTIAL column j — so the two reads hit different cells; this
 *   is replicated exactly.  Reads bank + g_item.  Pure (no RNG). */
int32_t roster_range_gate(const uint8_t *bank);

/* ── FUN_0045e80f — pick an oder (item request) for a customer ─────────
 * The daily-news featured-event state the scan latches (engine
 * DAT_0730b5e8/f0/f4); passed in so this helper stays self-contained. */
typedef struct {
    int32_t  active;      /* DAT_0730b5e8 — a news featured event is active */
    int32_t  target_id;   /* DAT_0730b5f0 — featured category / target id   */
    uint32_t attr_mask;   /* DAT_0730b5f4 — featured attribute mask         */
} roster_news_event_t;

/* Scans the oder pool (g_oder) for entries the customer `kr` would request,
 * then RNG-picks one uniformly among the matches.  Match rule:
 *   • no news event (ev->active==0): oder passes a quality gate
 *     (DAT_005c6c00[oder.level_minus_1] ≤ min(closeness/10, shop_rank)) AND
 *     (oder.attr_mask & kr->like_attr_mask) OR oder.attr_index ∈ kr->like_kinds
 *   • news event: match oder.attr_index==ev->target_id (when ev->attr_mask==0)
 *     else (oder.attr_mask & ev->attr_mask)!=0
 * `closeness_idx` indexes the per-customer closeness array (engine param_2).
 * Returns the chosen oder index, or -1 if none match.  Consumes exactly ONE
 * rng_next15() draw when ≥1 oder matches, ZERO otherwise (load-bearing). */
int32_t roster_pick_item(const uint8_t *bank, const kyaku_record_t *kr,
                         int32_t closeness_idx, const roster_news_event_t *ev);

#endif /* OPENRECET_CUSTOMER_ROSTER_H */
