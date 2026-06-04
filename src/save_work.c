/*
 * save_work.c — see save_work.h.
 *
 * Engine sources (Ghidra all.c):
 *   FUN_00490259 @ 0x490259 (81 B)  — per-slot save→work load + item scan
 *   FUN_004902aa @ 0x4902aa (84 B)  — whole save→work copy (middle step)
 *
 * The working arena mirrors the save arena's geometry exactly, so we
 * reuse the SAVE_BANK_* defines from save_bank.h. The save arena lives
 * in save_bank.c (save_bank_dwords_at); we read from it and write into
 * our own buffer — clean separation between the disk-mirror and the
 * live game state.
 */

#include "save_work.h"

#include <string.h>

#include "save_bank.h"   /* geometry + save_bank_dwords_at (source) */

/* ── Working arena (engine DAT_044e2c88 header / DAT_044e3798 banks) ── */
static uint8_t g_work[SAVE_BANK_ARENA_BYTES];

/* Engine DAT_0438b1e0 — active working slot ("current stage index"). */
static int g_active_slot = 0;

int  save_work_active_slot(void)        { return g_active_slot; }
void save_work_set_active_slot(int s)   { g_active_slot = s; }

uint8_t *save_work_base(void) { return g_work; }

uint8_t *save_work_bank_at(int slot)
{
    if (slot < 0 || slot >= SAVE_BANK_COUNT) {
        return NULL;
    }
    return g_work + SAVE_BANK_HEADER_BYTES
                  + (size_t)slot * SAVE_BANK_STRIDE_BYTES;
}

uint32_t *save_work_dwords_at(int slot)
{
    uint8_t *p = save_work_bank_at(slot);
    return p ? (uint32_t *)p : NULL;
}

int save_work_item_count(int slot)
{
    uint32_t *bank = save_work_dwords_at(slot);
    return bank ? (int)bank[SAVE_BANK_FIELD_ITEM_COUNT] : 0;
}

/* ── FUN_00490259 — load one save bank into the active working slot ── */
void save_work_load_slot(int src_save_bank)
{
    const uint32_t *src = save_bank_dwords_at(src_save_bank);
    uint32_t       *dst = save_work_dwords_at(g_active_slot);
    if (!src || !dst) {
        return;
    }

    /* Copy the whole bank (0xb7f2 dwords) save→work. */
    memcpy(dst, src, (size_t)SAVE_BANK_STRIDE_DWORDS * 4);

    /* Recompute the live inventory count: index of the first empty
     * (-1) item slot. Engine scans up to 20000 entries from dword 6;
     * if none is empty the count stays whatever the copy brought in
     * (engine leaves the field untouched in that case — it only
     * writes inside the `== -1` arm). */
    const uint32_t *items = dst + SAVE_BANK_ITEM_TABLE_DWORD;
    for (int i = 0; i < SAVE_BANK_ITEM_TABLE_COUNT; i++) {
        if (items[i] == 0xFFFFFFFFu) {
            dst[SAVE_BANK_FIELD_ITEM_COUNT] = (uint32_t)i;
            break;
        }
    }
}

/* ── FUN_004902aa middle step — whole save arena → working arena ── */
void save_work_sync_from_save(void)
{
    memcpy(g_work, save_arena_base(), SAVE_BANK_ARENA_BYTES);
}

void save_work_clear(void)
{
    memset(g_work, 0, sizeof g_work);
    g_active_slot = 0;
}
