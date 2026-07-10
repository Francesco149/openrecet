/*
 * customer_roster.h — the customer eligibility / spawn "roster scan"
 * (FUN_0045edaa) and its pure helpers.
 *
 * The scan decides WHO walks into the shop and WHAT they want each
 * shop-open (cc08==4).  This header exposes the settled, self-contained
 * helper functions ported so far; the 740-line scan body itself
 * (all.c 57474-58212, PORT-DEBT(cs-roster-scan)) lands on top of these.
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

#endif /* OPENRECET_CUSTOMER_ROSTER_H */
