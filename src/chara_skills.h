/*
 * chara_skills — per-chara skill-slot init driven by the level-threshold
 * RDATA table at 0x5c5060.
 *
 * Engine: FUN_004360b6 @ 0x4360b6 (202 B).  Called from
 * stage_post_load_init (FUN_00435c98 L33137) on every stage transition.
 *
 * For each of the 8 chara records:
 *
 *   1. Write 5 bytes at chara record +0x60..+0x64 = 1 — the "skill
 *      slot alive flag" array.  Engine encodes this as one dword
 *      `chara[+0x60] = 0x01010101` plus one byte `chara[+0x64] = 1`,
 *      which the port writes byte-by-byte for clarity.
 *
 *   2. Initialise `g_dat_0438b7fc[chara*5..chara*5+4] = {0, 1, 2, 3, 4}`
 *      — the per-chara skill-slot index list.
 *
 *   3. Read RDATA row `kCharaSkillThresholds[chara] = {count,
 *      threshold0, threshold1, threshold2, threshold3, threshold4}`.
 *      Set `g_dat_0438b7dc[chara] = 0`.  For each of `count`
 *      thresholds, if (threshold[i] <= chara_level), increment
 *      `g_dat_0438b7dc[chara]` — final value is the number of
 *      thresholds met by the chara's current level.
 *
 * Then a second pass walks the 8 chara count-slots and rewrites the
 * first `count` entries of each skill-slot list to {0, 1, ...,
 * count-1}.  For these specific RDATA values this is semantically a
 * no-op (the first-pass init already wrote those same values), but
 * the port follows the engine layout for fidelity.
 *
 * Finally writes 3 scratch DATs:
 *   DAT_0438b874 = 0
 *   DAT_0438b878 = 2
 *   DAT_0438b87c = 1
 *
 * The skill-count + skill-slot tables are consumed by unported per-
 * chara skill/spell code (FUN_004072f5, FUN_00480b65, FUN_00489c79,
 * FUN_00489d52, FUN_0048d5d6, FUN_0049791f).  No port reader yet, but
 * having this writer fire keeps the state in shape for when those
 * port — and surfaces the FUN_004360b6 probe in the trace diff.
 *
 * Pure C.  Tests cover threshold counting at various levels, RDATA
 * accuracy, and per-chara isolation.
 */

#ifndef OPENRECET_CHARA_SKILLS_H
#define OPENRECET_CHARA_SKILLS_H

#include <stdint.h>

#define CHARA_SKILLS_CHARA_COUNT   8
#define CHARA_SKILLS_SLOTS_PER     5
#define CHARA_SKILLS_THRESHOLD_MAX 5    /* per-row .rdata threshold limit */

/* FUN_004360b6.  Walks all 8 chara records for the active bank,
 * stamps skill-slot alive flags, fills the slot-index list, counts
 * met thresholds, then runs the second-pass slot rewrite and three
 * scratch writes.  Reads the active bank via
 * chara_equip_get_current_bank(); record-byte writes go through
 * chara_equip_set_record_byte. */
void chara_skills_init_at_stage_load(void);

/* Accessors (read-only) for the per-chara skill state.  Bounds-
 * checked; out-of-range → 0. */
int32_t chara_skills_get_count(int chara_idx);
int32_t chara_skills_get_slot(int chara_idx, int slot_idx);

/* Three scratch DATs the engine writes at the tail of FUN_004360b6.
 * No port reader yet — expose for test verification. */
int32_t chara_skills_get_dat_0438b874(void);
int32_t chara_skills_get_dat_0438b878(void);
int32_t chara_skills_get_dat_0438b87c(void);

/* Test helper — reset all module state to zero. */
void chara_skills_reset_for_test(void);

#endif /* OPENRECET_CHARA_SKILLS_H */
