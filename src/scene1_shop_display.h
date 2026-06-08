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
#define SHOP_DISPLAY_SUPPRESS_FLAGS    0xb1d4  /* DAT_0450fee8 (rec+0x2c750) — per-furniture dword */

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

#endif /* OPENRECET_SCENE1_SHOP_DISPLAY_H */
