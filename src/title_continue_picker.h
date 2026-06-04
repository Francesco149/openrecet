/*
 * title_continue_picker.h — the title-screen "Continue / load a save"
 * slot picker (engine state DAT_09643524 == 1, inside FUN_0049a59e).
 *
 * Selecting "Continue" from the title menu opens a 3-row grid of save
 * slots. The cursor walks the grid; A on an occupied slot loads it
 * (FUN_00490259 → save_work_load_slot) and kicks the scene fade into
 * the in-game scene. B backs out.
 *
 * Engine globals modelled here:
 *   DAT_09643530  cursor          DAT_09643534  scroll (top of window)
 *   DAT_09643538  hscroll anim    DAT_0964353c  vscroll anim
 *   DAT_005d1bbc  slot count (=100, from FUN_0049b537)
 *   DAT_09643380  slot-index array (identity 0..99, from FUN_0049b537)
 *   DAT_056e578c  last-used slot (save-header field; cursor default)
 *
 * Input bits (shared with scene_title.h SCENE_TITLE_INPUT_*): the grid
 * moves on HELD d-pad — DOWN(+1)/UP(-1) step within a column,
 * RIGHT(+3)/LEFT(-3) step columns; A(0x10)/B(0x20) on PRESSED confirm/
 * cancel.
 *
 * PORT-DEBT(subset): only the pure LOAD path (menu code 1,
 * CONTINUE_ANY) is modelled. The "new game into a chosen slot"
 * (code 4) + Survival (code 6) overwrite modes set `overwrite_mode`
 * but their confirm-time fresh-init + FUN_0049b4f4 survival seed are
 * NOT ported. Retire when those late-game entries are needed.
 *
 * Pure-C (audio SE is the only side effect). Unit-testable.
 */

#ifndef OPENRECET_TITLE_CONTINUE_PICKER_H
#define OPENRECET_TITLE_CONTINUE_PICKER_H

#include <stdint.h>

#define TITLE_PICKER_SLOTS  100

typedef enum {
    TITLE_PICKER_NONE   = 0,   /* still browsing */
    TITLE_PICKER_LOAD   = 1,   /* A on an occupied slot — bank loaded */
    TITLE_PICKER_CANCEL = 2,   /* B — return to main menu */
} title_picker_result_t;

typedef struct {
    int cursor;          /* DAT_09643530 — selected slot (grid index)   */
    int scroll;          /* DAT_09643534 — first visible slot           */
    int hscroll_anim;    /* DAT_09643538 — within-column slide counter   */
    int vscroll_anim;    /* DAT_0964353c — between-column slide counter  */
    int slot_count;      /* DAT_005d1bbc                                 */
    int slot_index[TITLE_PICKER_SLOTS]; /* DAT_09643380 (identity)       */
    int overwrite_mode;  /* DAT_09643564 — new-game/survival (see debt)  */
    int prompt_pending;  /* DAT_0964354c — "choose a file" toast queued  */
} title_continue_picker_t;

extern title_continue_picker_t g_title_continue_picker;

/* Port of FUN_0049b537 + the picker-entry setup in FUN_0049a59e.
 * Fills the slot-index array (identity), sets slot_count = 100, and
 * seeds the cursor to `last_used_slot` (scroll = last-2, clamped).
 * `entry_menu_code` selects the mode: 1 = load (default); 4/6 set
 * overwrite_mode (see PORT-DEBT). */
void title_continue_picker_open(int entry_menu_code, int last_used_slot);

/* One sim tick of the picker (engine FUN_0049a59e L100795 block, gated
 * on the menu being fully slid in). Returns the player's action; on
 * TITLE_PICKER_LOAD, `*out_load_bank` carries the chosen save-bank
 * index and the bank has ALREADY been loaded into the active working
 * slot (save_work_load_slot) — the caller only needs to start the
 * fade. `out_load_bank` may be NULL. */
title_picker_result_t title_continue_picker_step(uint16_t pressed,
                                                 uint16_t held,
                                                 int *out_load_bank);

#endif /* OPENRECET_TITLE_CONTINUE_PICKER_H */
