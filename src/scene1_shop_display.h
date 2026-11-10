/*
 * scene1_shop_display.h — the in-house display-stand interaction subsystem
 * (the cc04==1 "remove item from display" menu and its prerequisites).
 *
 * This module owns the engine's furniture-LAYOUT grid (DAT_074b28e8) and the
 * cell-highlight state (DAT_0438cbfc / DAT_0438cc00) that the shop-display
 * interaction is gated on.  It is the A0/A0b prerequisite for the open gate
 * (A1): the open only fires when the player is FACING a highlighted display
 * cell, and the highlight only resolves when the layout grid has a stand at
 * that cell.  Full RE: docs/findings/shop-display-menu-RE.md.
 *
 * Engine functions ported here:
 *   - FUN_0048960d (0x48960d) — rebuild the layout grid from the per-tier base
 *     template + the placed-furniture footprint stamps.  Runs every HOUSE frame
 *     in the FUN_0048670f prologue (all.c:86728).  shop_display_grid_rebuild().
 *   - FUN_004860c8 (0x4860c8) — return the furniture index whose footprint
 *     covers (col,row), or -1.  shop_display_furniture_index().
 *   - FUN_0048619f (0x48619f) — the cell-highlight detector: from the player's
 *     position + facing, resolve the faced display cell (col,row) and set the
 *     highlight iff the layout grid marks it a stand (cell value in [2,8]).
 *     Runs in the walk tail (all.c:87755).  shop_display_highlight_update().
 */

#ifndef OPENRECET_SCENE1_SHOP_DISPLAY_H
#define OPENRECET_SCENE1_SHOP_DISPLAY_H

#include <stdint.h>

/* ── bank-relative dword offsets (within the 0xb7f2-dword save record) ──────
 * All resolve as `save_work_dwords_at(save_work_active_slot()) + OFF`. */
#define SHOP_DISPLAY_TIER_SELECTOR     0xb378  /* DAT_04510578 (rec+0x2cde0) */
#define SHOP_DISPLAY_FURNITURE_ORIGINS 0xb384  /* DAT_045105a8 (rec+0x2ce10) — (x,z) pairs, stride 2 */
#define SHOP_DISPLAY_SUPPRESS_FLAGS    0xb1d4  /* DAT_0450fee8 (rec+0x2c750) — per table: 0, or the vender's item
                                                * handle when the table is a vending machine */
#define SHOP_DISPLAY_SHELF_ITEMS       0xb1e8  /* rec+0x2c7a0 — a vending machine's stock: 20 item handles per
                                                * table (-1 = empty) */
#define SHOP_DISPLAY_SHELF_SLOTS       20

/* Display-grid geometry (the layout grid is the same 15-row × 20-col shape as
 * the item grid SAVE_BANK_FIELD_DISPLAY_GRID; cell = col + row*0x14). */
#define SHOP_DISPLAY_GRID_STRIDE  0x14   /* 20 columns */
#define SHOP_DISPLAY_GRID_CELLS   300

/* FUN_0048960d: rebuild the layout grid (DAT_074b28e8) for this frame from the
 * active record's tier template + the live placed-furniture footprints
 * (g_scene1_walker_phase2_* count/mesh_type/rot_y + the bank furniture
 * origins).  Returns the overlap bitmask the engine returns (bit0 = a stamped
 * cell already held a 1/9 fixture, bit1 = held another furniture); the prologue
 * caller ignores it.  No-op-safe when no active save bank. */
uint32_t shop_display_grid_rebuild(void);

/* FUN_004860c8: the furniture index whose footprint covers (col,row), or -1.
 * Mirrors the grid_rebuild stamp footprints (2×2 / 1×4 / 4×1 by
 * mesh_type/rotation).  Last match wins (engine does not break early). */
int shop_display_furniture_index(int col, int row);

/* FUN_0048619f: resolve the faced display cell from the player's world
 * position (px,pz) + facing angle (pang, radians), and update the highlight.
 * Sets cbfc/cc00 to the faced (col,row) when the layout grid marks it a stand
 * (cell ∈ [2,8]); otherwise both to -1.  Also sets the render position
 * (cbf4/cbf8 = col*2-9 / row*2-7) and the occupancy indicator (bf68). */
void shop_display_highlight_update(float px, float pz, float pang);

/* FUN_0048619f: the above for the player (DAT_056da1d8 / e0, facing DAT_056db05c) */
void shop_display_faced_cell(void);

/* FUN_0048670f's arm without display fixtures: cbfc = -1 (cc00 kept) */
void shop_display_faced_none(void);

/* Force the highlight to "none" (engine all.c:87751: when the shop has no
 * display fixtures DAT_0450f3f2==0, the walk tail sets cbfc=-1 directly without
 * calling the detector). */
void shop_display_highlight_clear(void);

/* Highlighted cell accessors (DAT_0438cbfc col / DAT_0438cc00 row; -1 none). */
int shop_display_cbfc(void);
int shop_display_cc00(void);

/* Highlighted-cell render position (_DAT_0438cbf4 X / _DAT_0438cbf8 Z =
 * col*2-9 / row*2-7) and the occupancy indicator (DAT_0438bf68 = furniture+1,
 * 0 = none).  Consumed by the C3a faced-cell glow decal (chr_shadow Block G). */
float shop_display_render_x(void);
float shop_display_render_z(void);
int   shop_display_bf68(void);

/* Read one layout-grid cell (for tests / the open gate's stand check). */
int32_t shop_display_grid_cell(int col, int row);

/* Reset grid + highlight to BSS-zero / "none" (test setup, HOUSE re-entry). */
void shop_display_reset(void);

/* ── Move Tables: the counter menu's entry 5 (cc08 2, FUN_0048670f case 2) ──
 * One table is carried at a time (DAT_0438bea4): its origin in the bank
 * (+0x2ce10, the grid cell of its top-left) moves a cell per arrow, the items
 * on it wait in DAT_074b28d8.  FUN_0048960d is the placement check:
 * bit 0 = on a wall / counter cell (1 or 9), bit 1 = on another table,
 * 4 = an origin left of / above the grid. */

/* FUN_0048940e(table, pickup): the display-grid cells (work +0x13898) under
 * table `table` — pickup: into DAT_074b28d8 and the cells to -1; else back.
 * Footprint by DAT_0438bfcc[table]: 3 = 2×2, 4 = 1×4 (rotation 0) or 4×1,
 * 6 = 4×1; any other type does nothing. */
void shop_display_table_carry(int table, int pickup);

/* The cc08 10 dispatch of entry 5 (asm 0x4872f5): DAT_0438bea4 = 0,
 * FUN_0048940e(0, 1).  The caller sets cc08 = 2. */
void shop_display_move_begin(void);

/* One cc08 2 frame after the camera (asm 0x488235..0x488417): every table's
 * lift DAT_0438c0bc to 0, then on the edge mask `pressed` (DAT_073dddd4) /
 * the repeat mask `held` (DAT_073dddd6):
 *   B 0x20 — check 0: put the table down, SE 00re_sys07, returns 1 (the
 *            caller leaves: DAT_0438b4e8 = cc08 = DAT_0438b754 = 0); else SE 0x16a;
 *   A 0x10 — check 0: put it down, the next table (wraps), pick it up, SE
 *            0x143; else SE 0x16a;
 *   else   — the carried table's lift 0.5; held 2 / 1 / 4 / 8 = x-1 / x+1 /
 *            z-1 / z+1; the check; a changed origin plays SE 0x16a (refused:
 *            bit 0 or 4) or 0x146; a refused origin is put back.
 * Then LABEL_414: the world positions (shop_display_tables_place). */
int shop_display_move_tick(uint16_t pressed, uint16_t held);

/* LABEL_414 (asm 0x488396): DAT_0438c06c[i] / DAT_0438c10c[i] =
 * 2 · (origin − DAT_005ce3e0[scene type][i]) for the 10 table slots — the
 * walker's placement and the collision objects 5.. (one memory in retail). */
void shop_display_tables_place(void);

/* FUN_004864fd(bank): re-lay the shop for the bank's tier (work +0x2cde0) —
 * called by the guild's store Expansion right after the tier bump (and by the
 * title's New Game+ arm with the tier at 0):
 *   - the table count DAT_0438bfb4 = 3 / 4 / 6 / 10 for tier 0 / 1 / 2 / 3+;
 *   - every item on display goes back into the inventory (FUN_00468d22, in
 *     grid order: the 300 cells row-major), the grid is all -1;
 *   - per table: FUN_004865d5 (its footprint's cells — already empty by then),
 *     its 20-slot shelf list (work +0x2c7a0 + 80·i) back into the inventory,
 *     its flag dword (work +0x2c750 + 4·i) = 0;
 *   - FUN_0048ffd9: the ten table origins (work +0x2ce10) = the tier's template;
 *   - FUN_0048439a: the shop's attribute centroid (now of an empty shop). */
void shop_display_relayout(uint32_t *bank);

int shop_display_move_table(void);                 /* DAT_0438bea4 */

/* FUN_004865d5(table): what stands on table `i` goes back into the inventory
 * (FUN_00468d22) — the display-grid cells under its footprint, from its origin
 * (work +0x2ce10 + 8·i): type 3 (DAT_0438bfcc) 2×2 row-major, else rotation 0.0
 * (DAT_0438c01c) 4 along x, otherwise 4 along z.  i < 0 does nothing (retail
 * reads the dwords before its arrays). */
void shop_display_table_empty(uint32_t *bank, int i);

/* ── vending machines (FUN_0048670f: the cc04 1 confirm's 5201..5299 arm, the
 * cc04 0 gate's furniture arm, the cc04 2 arm) ─────────────────────────────
 * A table whose flag dword (work +0x2c750 + 4·table) is not 0 IS a vending
 * machine: the dword is the vender's item handle (id << 6: 0x51440 Old Vender
 * 5201, 0x51480 5202, 0x514c0 5203).  Its stock is the table's 20-slot shelf
 * list (work +0x2c7a0 + 80·table, -1 = empty); 6 / 8 / 10 slots are offered by
 * handle 0x51440 / 0x51480 / any other. */

/* The cc04 1 confirm with a 5201..5299 item (asm 0x489274..0x4892fe).  Counts
 * the machines among the DAT_0438bfb4 tables, then by the merchant level (work
 * +0x2c400, 0-based):
 *   < 14                    → SHOP_VENDER_LEVEL (nothing changes);
 *   < 21 with 3 machines    → SHOP_VENDER_LIMIT (nothing changes);
 *   else                    → SHOP_VENDER_PLACED: the faced cell's table
 *     (FUN_004860c8(cbfc, cc00)) is emptied into the inventory (FUN_004865d5),
 *     its flag dword and the faced display-grid cell = `item`, `item` leaves the
 *     inventory (FUN_00469241). */
enum { SHOP_VENDER_PLACED = 0, SHOP_VENDER_LEVEL = 1, SHOP_VENDER_LIMIT = 2 };
int shop_vender_place(uint32_t *bank, int item);

/* The cc04 0 gate's furniture arm (asm 0x488c47..0x488cc9), after cc04 = 2 and
 * the cbe8 latch: FUN_00468338(0, first_open), the menu state — DAT_074b2ed4 0,
 * the row DAT_074b2ed8 2 (the first slot), the first row shown DAT_074b2edc 0,
 * the last row DAT_074b2ee0 7 / 9 / 11, DAT_0438bf64 = `table` — SE 0x143 and
 * the hand cursor's slide to the row (LABEL_673: x 16, y 40·(row − top) + 124). */
void shop_vender_menu_open(int table, int first_open);

/* One cc04 2 frame (asm 0x488dda..0x489153) on the edge mask `pressed`
 * (DAT_073dddd4) and the repeat mask `held` (DAT_073dddd6).  Rows: 0 "Revert To
 * Table", 1 "Done Stocking", 2.. slot row − 2.  Returns 1 when the menu closes
 * (the caller: cc04 = 0).
 *  DAT_074b2ed4 == 0 (the rows):
 *   - an open item detail (DAT_0734b96c) closes on A / B / Y (SE 710), else waits;
 *   - B 0x20: close — cursor off, the item window slides out, SE 0x13d;
 *   - Y 0x40 on a stocked slot: its detail (DAT_0734b96c 1, DAT_0734b998 = the
 *     item, no recount), cursor off, SE 710;
 *   - A 0x10: row 0 — the table is a table again: FUN_004865d5 (the machine
 *     itself is the item in its display cell), the 20 shelf slots back into the
 *     inventory, the flag dword of FUN_004860c8(cbfc, cc00) = 0, close, SE 0x143;
 *     row 1 — close, SE 0x143; a slot — DAT_074b2ed4 = 1, the hand cursor onto
 *     the item list (FUN_0046939a), SE 0x143;
 *   - else up 4 / down 8 a row, left 2 / right 1 six rows (SE 0x146, the window
 *     of 6 follows), then FUN_00468286 highlights the row's item (-1 on rows 0 / 1).
 *  DAT_074b2ed4 == 1 (the item list, FUN_00469414(1)): 2 → back to the rows (SE
 *  0x13d); 3 → SE 0x143; 1 → the slot's item back into the inventory, the picked
 *  one (FUN_00469a9f, -1 "Nothing") into the slot and out of the inventory, the
 *  list rebuilt (FUN_00468338(0, 0)) and shown at once (FUN_004682e3).
 *  Retail quirk: the pick is also written to DAT_0438b67c[row] = the dungeon
 *  bag's "brought from the shop" list DAT_0438b684[row − 2]. */
int shop_vender_menu_tick(uint16_t pressed, uint16_t held);

/* work +0x2c750 + 4·i: table i's vender handle, 0 = an ordinary table
 * (FUN_00457714's mesh choice and the 0x514c0 placement) */
int32_t shop_display_table_flag(int i);

/* DAT_074b2ed4 / ed8 / edc / ee0 and DAT_0438bf64 (the render, state_va) */
int shop_vender_menu_pick(void);
int shop_vender_menu_row(void);
int shop_vender_menu_top(void);
int shop_vender_menu_last(void);
int shop_vender_menu_table(void);

/* state_va: DAT_074b28d8 (the carried cells) .. DAT_074b28e8 (the layout
 * grid, 300 dwords); NULL if `va` is not one of them */
const int32_t *shop_display_state_va(uint32_t va);

/* DIAGNOSTIC (rng-survey §21.4 ROOT 2): one-shot dump of the layout grid +
 * inputs (tier/count/origins/mesh/rot) → <dir>\grid_dump.{bin,json}.  Used to
 * root-cause the port↔retail grid divergence behind the cs-walker retarget
 * rng-count gap.  No-op if dir==NULL. */
void shop_display_grid_dump(const char *dir, uint32_t frame);

#endif /* OPENRECET_SCENE1_SHOP_DISPLAY_H */
