/*
 * stage_gate.h — port of the engine's "is the (current dungeon, next
 * floor) target a boss-eligible transition?" gate (PHC #24).
 *
 * Engine sources:
 *
 *   FUN_00431990 @ 0x431990  (70 B) — pure enemy-id predicate.
 *     Returns 1 iff the enemy id is a "boss-class" id (one of the
 *     specific ranges that mark per-floor boss encounters).
 *
 *   FUN_0043195d @ 0x43195d  (51 B) — checkpoint-floor predicate.
 *     Reads DAT_0438b4c8 (current dungeon id) and DAT_0438b4cc
 *     (next-floor id) and returns 1 iff next_floor is a "boss
 *     floor" for the current dungeon:
 *       dungeon != 5:  next_floor % 5 == 4   (i.e. floors 5/10/15/...)
 *       dungeon == 5:  next_floor >= 0x1d     (29 onward)
 *
 *   FUN_004319d6 @ 0x4319d6 (170 B) — outer gate.  Returns 1 iff
 *     a "boss exists for this (dungeon, floor) target" — used by
 *     several other subsystems as a cooldown shrinker or transition
 *     allowance check.  Logic:
 *       1. Hard-coded special transitions: (0 → 4), (4 → 0x1d),
 *          (4 → 99) always allow.
 *       2. Otherwise checkpoint predicate must pass.
 *       3. Walk g_enemylist.sections[cur_dungeon][0..59] looking for
 *          a section whose [floor_lo, floor_hi] range contains
 *          next_floor.
 *       4. In that section, walk enemies[0..30] looking for any
 *          enemy_id passing the boss-id predicate.  Return 1 if
 *          found.
 *
 * Globals:
 *
 *   DAT_0438b4c8 — current dungeon id.  Already ported as
 *                  g_scene1_combat_stage_id in scene1_combat_sm.c
 *                  (BSS-zero stand-in).  We re-use it.
 *   DAT_0438b4cc — next-floor id.  Owned by this module as
 *                  g_stage_gate_next (BSS-zero stand-in; no engine
 *                  writer ported yet, so default 0).
 *
 * Pure C, no Win32 surface.  Reads only the existing engine globals
 * and the parsed g_enemylist.sections table — no new tables.
 *
 * Known engine quirks faithfully preserved:
 *
 *   - The decomp at L41-43 has the dead branch
 *       `if (iVar1 * 0x2f0 == -0x53f8e8) return 0;`
 *     iVar1 is bounded [0, 9*60+59] = [0, 599], so the LHS max is
 *     0x23b * 0x2f0 = 0x67e90 — never equal to 0xFFAC0718.  This is
 *     a Ghidra-side artefact of an overflow check or null-pointer
 *     guard the original asm encoded differently.  Skipped in the
 *     port (always-false in practice).
 *
 *   - The outer walker caps at 60 iterations (the 0x3c constant) —
 *     matches ENEMYLIST_SECTIONS_PER_DUNGEON exactly.
 *   - The inner walker caps at 31 iterations (the 0x1f constant) —
 *     matches ENEMYLIST_ENEMY_SLOTS_PER_SECTION exactly.
 *
 * No CALL_TRACE_ENTER probe in this header — each function emits
 * its own at the .c body.
 */

#ifndef OPENRECET_STAGE_GATE_H
#define OPENRECET_STAGE_GATE_H

#include <stdint.h>

/* DAT_0438b4cc storage — accessors only (private storage). */
int32_t stage_gate_get_next(void);
void    stage_gate_set_next(int32_t next_floor);

/* FUN_00431990 — pure boss-id predicate.  Returns 1 iff `enemy_id`
 * is in one of the specific ranges the engine treats as a boss-class
 * encounter:
 *
 *   [0x17..0x19], [0x1b..0x1c], 0x29, 0x2b,
 *   0x31, [0x36..0x37], [0x3b..0x49]
 *
 * All other values (including negative -1 sentinels for empty
 * enemy slots) return 0. */
int stage_gate_boss_id_allowed(int32_t enemy_id);

/* FUN_0043195d — checkpoint-floor predicate.  Reads
 * g_scene1_combat_stage_id + g_stage_gate_next; returns 1 iff the
 * next floor is a boss-checkpoint floor for the current dungeon. */
int stage_gate_floor_is_checkpoint(void);

/* FUN_004319d6 — full outer gate.  See header writeup for the
 * three-step logic; returns 1 iff a boss exists for the
 * (current_dungeon, next_floor) target. */
int stage_gate_query(void);

#endif /* OPENRECET_STAGE_GATE_H */
