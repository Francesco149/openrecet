/*
 * npc_schedule — port of FUN_00490e56 (494 B).
 *
 * Per-NPC daily-schedule processor.  The engine carries 600 NPC schedule
 * definitions (~430 KB at DAT_095d3810, stride 0x2cc) and a per-bank
 * status array (DAT_0450cd90, 4 B per NPC: status short + counter short).
 *
 * The function has three logical paths controlled by a single int arg
 * `mode` plus the current state of the per-bank status array:
 *
 *   mode == 1 (CONTINUE save): scan the schedule array for any NPC with
 *                schedule_mode == 4 AND existing status > 1 (= "this NPC
 *                was active mid-event when the save was written").  If
 *                found, force a re-init pass.  Reaches FUN_0049a59e L79.
 *
 *   mode == 0 (NEW GAME): reset DAT_04511578 (= per-bank "is this stage's
 *                event chain active" flag) to 0, then either INIT or
 *                TICK depending on whether the bank's status array is
 *                BSS-zero.  Reaches FUN_0049a59e L232.
 *
 *   INIT pass: fires when the bank's first status short is 0 OR when
 *                the CONTINUE search above found a state-4 active NPC.
 *                Writes one status value per NPC based on the NPC's
 *                schedule_mode byte (0..6+).  Calls rng_next15() 1× per
 *                NPC for modes 1, 2, 3, 5, 6, 7+ (= 599 of 600 in
 *                BSS-zero case if mode 0 dominates).  Mode 4 has NO
 *                rng call.
 *
 *   TICK pass: fires when mode == 0 AND the bank's status was nonzero
 *                AND no state-4 active NPC.  Walks all 600 NPCs and
 *                advances their (status, counter) per schedule_mode.
 *                Pure per-frame state machine; rng for modes 2 and 4.
 *
 * No caller reads the return value of FUN_00490e56; we return void.
 *
 * Storage:
 *
 *   - g_npc_schedule_mode[600]                — engine DAT_095d3810 (first
 *                                                byte of each 0x2cc-byte
 *                                                NPC record).  BSS-zero
 *                                                today (no boot-init code
 *                                                populates this; pre-3D
 *                                                fresh-boot all-zero is
 *                                                what retail also has on
 *                                                NEW GAME from a clean
 *                                                Steam install).  Other
 *                                                bytes 1..0x2cb of each
 *                                                NPC record are not read
 *                                                here and not modelled.
 *
 *   - g_npc_schedule_status[bank][600][2]     — engine DAT_0450cd90 +
 *                                                bank * 0x2dfc8.  Each
 *                                                NPC has 4 bytes = two
 *                                                int16 (status, counter).
 *                                                Status[0] is read at
 *                                                gate; counter[1] is per-
 *                                                tick advance.
 *
 *   - g_npc_event_active[bank]                — engine DAT_04511578 +
 *                                                bank * 0x2dfc8.  u32
 *                                                flag (0/1).  Reset to 0
 *                                                in the NEW GAME path;
 *                                                no port readers ported
 *                                                yet (FUN_0045edaa is the
 *                                                consumer, unported).
 *
 * NEW GAME call-count expectation vs retail at frame 59:
 *
 *   npc_schedule_apply(0)   ×1   matches retail (single call from
 *                                FUN_0049a59e L232, fires NEW GAME init).
 *
 * Pure C, no Win32 surface.  Tests link this + rng + call_trace.
 */

#ifndef OPENRECET_NPC_SCHEDULE_H
#define OPENRECET_NPC_SCHEDULE_H

#include <stdint.h>

/* Engine: 600 NPC records × 0x2cc bytes = 0x68e20 (≈430 KB) starting at
 * DAT_095d3810, ending at &DAT_0963c630.  Only the first byte (the
 * schedule_mode) is read by FUN_00490e56; remaining 0x2cb bytes per
 * record are NOT modelled here.  Engine boot-init for DAT_095d3810 is
 * BSS-zero (no .data initializer; no startup writer ported).  On NEW
 * GAME from a clean Steam install, retail's array is also BSS-zero. */
#define NPC_SCHEDULE_COUNT       600

/* Engine BANK stride is 0x2dfc8 bytes.  NEW GAME / first-boot HOUSE
 * only touches bank 0; port reserves 1 bank.  Expand when save-slot
 * UI lands (mirrors chara_equip's BANK_COUNT). */
#define NPC_SCHEDULE_BANK_COUNT  1

/* ─── Engine body ─────────────────────────────────────────────────── */

/* FUN_00490e56 @ 0x490e56.  See header comment for the three paths.
 * `mode` = 1: CONTINUE save (search for state-4 active NPC).
 * `mode` = 0: NEW GAME (reset event-active flag).
 * Body decides INIT vs TICK from prior state. */
void npc_schedule_apply(int mode);

/* ─── Schedule-source accessors ───────────────────────────────────── */

/* Read/write the schedule_mode byte of NPC i.  Engine semantics:
 *   0 = static (always present at "100% schedule presence")
 *   1 = 5-tick presence-ticker
 *   2 = random-window event with target (i%8)+15
 *   3 = 3-tick gated event
 *   4 = random-window event with target (i%8)+20
 *   5 = 1-tick gated event
 *   6 = 2-tick gated event
 *   other: same as mode 3 (3-tick gated)
 *
 * Out-of-range (i < 0 or i >= NPC_SCHEDULE_COUNT) is a silent no-op on
 * set / 0 on get.  No engine code modifies these at runtime — they're
 * loaded from a data table at boot.  Until that load chip ports, all
 * modes are BSS-zero (= mode 0 / "always present"). */
uint8_t npc_schedule_get_mode(int npc_idx);
void    npc_schedule_set_mode(int npc_idx, uint8_t mode);

/* ─── Status array accessors (test + future consumers) ────────────── */

int16_t npc_schedule_get_status(int bank, int npc_idx);
int16_t npc_schedule_get_counter(int bank, int npc_idx);

/* ─── Event-active flag accessor (test + future stage-event chip) ──── */

uint32_t npc_schedule_get_event_active(int bank);
void     npc_schedule_set_event_active(int bank, uint32_t value);

/* ─── Test helper ─────────────────────────────────────────────────── */

void npc_schedule_reset_for_test(void);

#endif /* OPENRECET_NPC_SCHEDULE_H */
