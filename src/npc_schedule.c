/*
 * npc_schedule.c — port of FUN_00490e56.  See npc_schedule.h.
 *
 * Source-level reference: docs/decompiled/by-address/490e56.c.
 */

#include "npc_schedule.h"

#include <stdbool.h>
#include <string.h>

#include "call_trace.h"
#include "chara_equip.h"  /* chara_equip_get_current_bank() = DAT_0438b1e0 */
#include "rng.h"          /* rng_next15() = thunk_FUN_005041f6 */

/* ─── Storage ─────────────────────────────────────────────────────── */

/* DAT_095d3810 first byte (schedule_mode) per NPC.  Engine record is
 * 0x2cc bytes; only this leading byte is read here. */
static uint8_t g_npc_schedule_mode[NPC_SCHEDULE_COUNT];

/* DAT_0450cd90 — per-(bank, NPC) status pair (status, counter), 2 shorts. */
static int16_t g_npc_schedule_status[NPC_SCHEDULE_BANK_COUNT]
                                    [NPC_SCHEDULE_COUNT][2];

/* DAT_04511578 — per-bank u32 "event active" flag. */
static uint32_t g_npc_event_active[NPC_SCHEDULE_BANK_COUNT];

static int clamp_bank(int bank)
{
    if (bank < 0 || bank >= NPC_SCHEDULE_BANK_COUNT) return -1;
    return bank;
}

/* ─── FUN_00490e56 — npc_schedule_apply ────────────────────────────── */

void npc_schedule_apply(int mode)
{
    CALL_TRACE_ENTER(0x490e56u);

    int bank = clamp_bank(chara_equip_get_current_bank());
    if (bank < 0) return;

    int16_t (*status_arr)[2] = g_npc_schedule_status[bank];

    bool found_active = false;

    if (mode == 1) {
        /* CONTINUE: scan for a state-4 NPC whose status > 1.  If found,
         * force re-init below.  Engine breaks on first hit. */
        for (int i = 0; i < NPC_SCHEDULE_COUNT; i++) {
            if (g_npc_schedule_mode[i] == 4 && status_arr[i][0] > 1) {
                found_active = true;
                break;
            }
        }
    } else {
        /* NEW GAME (or any non-1 mode): reset event-active flag. */
        g_npc_event_active[bank] = 0;
    }

    if (status_arr[0][0] == 0 || found_active) {
        /* INIT pass.  Engine writes status[0][0] = 1 before the loop —
         * immediately overwritten by the loop's first iteration writing
         * status[0][0] based on g_npc_schedule_mode[0].  Preserved for
         * bitwise faithfulness even though the value is never observed. */
        status_arr[0][0] = 1;
        for (int i = 0; i < NPC_SCHEDULE_COUNT; i++) {
            uint8_t m = g_npc_schedule_mode[i];
            int16_t *s = &status_arr[i][0];
            if (m == 0) {
                *s = 100;
            } else if (m == 1) {
                uint32_t r = rng_next15();
                *s = (int16_t)((r % 3) + 3);
            } else if (m == 2) {
                /* Engine calls rng but discards the result; preserve the
                 * call for rng-stream parity. */
                (void)rng_next15();
                *s = 0;
            } else if (m == 4) {
                /* Mode 4 is the lone "no rng call" branch in INIT. */
                *s = 0;
            } else {
                /* mode 3, 5, 6, 7+ */
                (void)rng_next15();
                *s = 1;
            }
        }
    } else if (mode == 0) {
        /* TICK pass: per-frame schedule advance.  Engine uses signed
         * arithmetic + comparisons on the int16 counters; preserved
         * via explicit (int16_t) casts so wrap behaviour matches. */
        for (int i = 0; i < NPC_SCHEDULE_COUNT; i++) {
            uint8_t m   = g_npc_schedule_mode[i];
            int16_t *s  = &status_arr[i][0];  /* engine psVar7[-1] */
            int16_t *c  = &status_arr[i][1];  /* engine *psVar7 */

            if (m == 0) {
                *s = 100;
            } else if (m == 1) {
                /* 5-tick presence-ticker: every 5 ticks, status += 1
                 * up to a cap of 10. */
                *c = (int16_t)(*c + 1);
                if (*c > 4) {
                    *c = 0;
                    if (*s < 10) {
                        *s = (int16_t)(*s + 1);
                    }
                }
            } else if (m == 2) {
                /* Random-window: status = (rand % 3), counter ticks,
                 * fires when counter reaches (i%8)+15. */
                uint32_t r = rng_next15();
                *s = (int16_t)(r % 3);
                *c = (int16_t)(*c + 1);
                int target = (i % 8) + 0xf;
                if (target <= (int)*c) {
                    *c = 0;
                    *s = 1;
                }
            } else if (m == 4) {
                /* Random-window: status = max((rand%3)-1, 0), counter
                 * ticks, fires at (i%8)+20. */
                uint32_t r = rng_next15();
                int16_t v = (int16_t)((int)(r % 3) - 1);
                *s = (v < 0) ? 0 : v;
                *c = (int16_t)(*c + 1);
                int target = (i % 8) + 0x14;
                if (target <= (int)*c) {
                    *c = 0;
                    *s = 1;
                }
            } else if (m == 5) {
                /* Gated 1-tick: only ticks when status == 0. */
                if (*s == 0) {
                    *c = (int16_t)(*c + 1);
                    if (*c >= 1) {
                        *c = 0;
                        *s = 1;
                    }
                }
            } else if (m == 6) {
                /* Gated 2-tick: only ticks when status == 0. */
                if (*s == 0) {
                    *c = (int16_t)(*c + 1);
                    if (*c >= 2) {
                        *c = 0;
                        *s = 1;
                    }
                }
            } else {
                /* mode 3 (and 7+): gated 3-tick. */
                if (*s == 0) {
                    *c = (int16_t)(*c + 1);
                    if (*c >= 3) {
                        *c = 0;
                        *s = 1;
                    }
                }
            }
        }
    }
}

/* ─── Accessors ───────────────────────────────────────────────────── */

uint8_t npc_schedule_get_mode(int npc_idx)
{
    if (npc_idx < 0 || npc_idx >= NPC_SCHEDULE_COUNT) return 0;
    return g_npc_schedule_mode[npc_idx];
}

void npc_schedule_set_mode(int npc_idx, uint8_t mode)
{
    if (npc_idx < 0 || npc_idx >= NPC_SCHEDULE_COUNT) return;
    g_npc_schedule_mode[npc_idx] = mode;
}

int16_t npc_schedule_get_status(int bank, int npc_idx)
{
    bank = clamp_bank(bank);
    if (bank < 0) return 0;
    if (npc_idx < 0 || npc_idx >= NPC_SCHEDULE_COUNT) return 0;
    return g_npc_schedule_status[bank][npc_idx][0];
}

int16_t npc_schedule_get_counter(int bank, int npc_idx)
{
    bank = clamp_bank(bank);
    if (bank < 0) return 0;
    if (npc_idx < 0 || npc_idx >= NPC_SCHEDULE_COUNT) return 0;
    return g_npc_schedule_status[bank][npc_idx][1];
}

uint32_t npc_schedule_get_event_active(int bank)
{
    bank = clamp_bank(bank);
    if (bank < 0) return 0;
    return g_npc_event_active[bank];
}

void npc_schedule_set_event_active(int bank, uint32_t value)
{
    bank = clamp_bank(bank);
    if (bank < 0) return;
    g_npc_event_active[bank] = value;
}

void npc_schedule_reset_for_test(void)
{
    memset(g_npc_schedule_mode, 0, sizeof(g_npc_schedule_mode));
    memset(g_npc_schedule_status, 0, sizeof(g_npc_schedule_status));
    memset(g_npc_event_active, 0, sizeof(g_npc_event_active));
}
