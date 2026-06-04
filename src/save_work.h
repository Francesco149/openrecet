/*
 * save_work.h — the live "working" save arena (engine `DAT_044e2c88`
 * header / `DAT_044e3798` banks).
 *
 * The engine keeps TWO ~18 MB arenas of identical geometry (see
 * docs/findings/save-working-arena.md):
 *
 *   - the SAVE arena (`DAT_056e5770`, our `save_bank.c`) — the disk
 *     mirror of save.dat; the loader fills it at boot.
 *   - the WORKING arena (`DAT_044e2c88`, THIS module) — the LIVE game
 *     state gameplay reads and writes (money, day, inventory, the
 *     items on shop displays, …).
 *
 * "Continue / load a save" = copy a chosen SAVE bank into the active
 * WORKING slot. The active slot is `DAT_0438b1e0` ("current stage
 * index"); its only engine writer is `= 0`, so the live game always
 * lives in working slot 0 and the 100 save banks are pure storage.
 *
 * Geometry is shared with save_bank.h (SAVE_BANK_* defines): 100 banks
 * of 0xb7f2 dwords behind a 0xb10-byte header.
 *
 * Pure-C — no Win32, no D3D. Unit-testable under host gcc.
 */

#ifndef OPENRECET_SAVE_WORK_H
#define OPENRECET_SAVE_WORK_H

#include <stdint.h>

/* ── Active working slot (engine `DAT_0438b1e0`) ── */

/* The live game runs in this working slot. Engine BSS-init 0; only
 * writer is `= 0`. Exposed so the post-fade commit + slot loaders
 * agree on the destination. */
int  save_work_active_slot(void);
void save_work_set_active_slot(int slot);

/* ── Working-arena access ── */

/* Base of the working arena (shared header). Stable static buffer. */
uint8_t  *save_work_base(void);

/* Pointer to working bank `slot` (NULL if out of range). */
uint8_t  *save_work_bank_at(int slot);
uint32_t *save_work_dwords_at(int slot);

/* Live inventory count of working bank `slot` (the
 * SAVE_BANK_FIELD_ITEM_COUNT dword). Returns 0 on a bad slot. */
int save_work_item_count(int slot);

/* ── Load primitives ── */

/* Port of FUN_00490259 — load SAVE bank `src_save_bank` into the
 * ACTIVE working slot (save_work_active_slot), then recompute the
 * live inventory count by scanning the item-slot table for the first
 * empty (-1) entry.
 *
 * Reads from save_bank_dwords_at(src_save_bank); a bad source or a
 * NULL active working slot is a no-op. */
void save_work_load_slot(int src_save_bank);

/* Port of FUN_004902aa's middle step — copy the ENTIRE save arena
 * (all 100 banks + header) into the working arena verbatim. Used by
 * the "clear all / new-from-picker" special path. */
void save_work_sync_from_save(void);

/* ── Test helper ── */

/* Zero the whole working arena + reset the active slot to 0. */
void save_work_clear(void);

#endif /* OPENRECET_SAVE_WORK_H */
