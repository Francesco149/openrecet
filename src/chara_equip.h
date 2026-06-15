/*
 * chara_equip — per-(bank, chara) equipped-item table + stat aggregator.
 *
 * Engine subsystem: the 5 equipment slots a chara has on, plus their
 * "base stats" (the four atk/def/matk/mdef columns from the chara's
 * record that get added on top of equipment).  Both live in a
 * 0x6c-byte chara record at engine VA
 *
 *   &DAT_04510648 + bank_idx * 0x2dfc8 + chara_idx * 0x6c
 *
 * where `bank_idx = DAT_0438b1e0` (current stage/bank) and
 * `chara_idx = DAT_0438b7d8` (active chara within bank).  Engine
 * BSS-zero on first-boot NEW GAME → bank=0, chara=0, all slots = 0,
 * all base stats = 0.
 *
 * Engine layout within one 0x6c chara record:
 *
 *   +0x00..+0x03   reserved / item-instance hp (read elsewhere)
 *   +0x04..+0x07   equip slot 0  (encoded: item_id<<6 | meta_bits[0..3f])
 *   +0x08..+0x0b   equip slot 1
 *   +0x0c..+0x0f   equip slot 2
 *   +0x10..+0x13   equip slot 3
 *   +0x14..+0x17   equip slot 4
 *   +0x18..+0x2b   other equip state (uncategorised in this chip)
 *   +0x2c..+0x2f   base stat 0 (atk)
 *   +0x30..+0x33   base stat 1 (def)
 *   +0x34..+0x37   base stat 2 (matk)
 *   +0x38..+0x3b   base stat 3 (mdef)
 *   +0x3c..+0x6b   per-equip flags + condition counters (uncategorised)
 *
 * Engine functions ported here:
 *
 *   FUN_004844ef @ 0x4844ef (310 B) → chara_equip_recompute_aggregate
 *     Takes the current (bank, chara) record, sums equipment stats from
 *     all 5 slots into a 4-dword scratch (DAT_056db0ac..b8), then adds
 *     the record's base stats.  Side effect: zeros ~24 bytes of other
 *     scratch DATs (056db074..056db0a7, 074b2ec0) and writes the
 *     counter DAT_056db0a8 = 5.  Called from stage_post_load_init
 *     (FUN_00435c98) on every stage transition.
 *
 *   FUN_0048093f @ 0x48093f (136 B) → distribute_slot_stats (static)
 *     Per-slot helper.  Skips sentinel slots (0xffffffff = "empty").
 *     For populated slots, looks up the encoded item_id (slot >> 6)
 *     in the master item DB and accumulates atk/def/matk/mdef into
 *     the aggregator's sum buffer.  Also tracks the max-of-4 stat
 *     and adds the slot's low 4 meta bits (`slot & 0xf`, the equip's
 *     enchantment level) to that max slot's column.
 *
 * The item-id-to-record-slot helper FUN_004681f6 lives in
 * `tables_item.c` (tables_item_find_slot_by_id).
 *
 * NEW GAME call-count expectation vs retail at frame 59:
 *
 *   chara_equip_recompute_aggregate     ×1    matches retail
 *   distribute_slot_stats               ×5    matches retail
 *   tables_item_find_slot_by_id         ×4    matches retail
 *
 * The fourth call is skipped because slot A[0] of every chara holds
 * the 0xffffffff sentinel — written by save_bank.c::apply_starter_items
 * (port of FUN_0048ff93), which mirrors its save_bank arena writes into
 * chara_equip storage via chara_equip_set_record_dword.  The engine's
 * STARTER_ITEMS[chara][4] = -1 entry combined with the symmetric L/R
 * write pattern lands the sentinel at chara_equip[chara] + 4 (slot A[0]).
 *
 * Pure C, no Win32 surface.  Tests link this module + tables_item +
 * call_trace and exercise both the leaf helpers and the full
 * aggregator.
 */

#ifndef OPENRECET_CHARA_EQUIP_H
#define OPENRECET_CHARA_EQUIP_H

#include <stdint.h>

/* Per-bank stride in dwords: 5 slots + 4 base stats = 9 dwords readable
 * here, plus 11 other dwords (0x6c bytes total / 4 = 27).  Port stores
 * the full 0x6c-byte record per chara so future engine chips can index
 * the missing fields without restructuring storage. */
#define CHARA_EQUIP_RECORD_BYTES 0x6c
#define CHARA_EQUIP_SLOT_COUNT   5
#define CHARA_EQUIP_STAT_COUNT   4

/* Bank/chara counts.  Engine has up to ~24 banks visible in the
 * 0x2dfc8-stride layout, but NEW GAME / first-boot HOUSE only touches
 * (bank=0, chara=0..7).  Port reserves 1 bank × 8 charas (= 864
 * bytes); set/get with bounds check.  Expand when save-load lands. */
#define CHARA_EQUIP_BANK_COUNT   1
#define CHARA_EQUIP_CHARA_COUNT  8

/* Sentinel for "empty equip slot" — engine writes 0xffffffff after
 * unequip; FUN_0048093f bails early on this value.  BSS-zero slot
 * (= 0) is NOT a sentinel; it resolves to item_id=0 (the first item
 * in data/item.txt, which is the unequippable "Sword" placeholder
 * with zero stats — so a zero slot effectively contributes nothing
 * to the aggregate). */
#define CHARA_EQUIP_SLOT_EMPTY   0xffffffffu

/* ─── Active bank/chara selectors ──────────────────────────────────── */

/* Engine: DAT_0438b1e0 (stage/bank index) and DAT_0438b7d8 (chara index
 * within the bank).  Both BSS-zero by default; the stat aggregator
 * reads these every call to pick which 0x6c record to crunch.
 *
 * Other modules also read DAT_0438b1e0 (scene_floor, scene_walls, etc.)
 * — they currently model it as "always 0".  Centralising ownership here
 * gives a single source of truth; those modules can migrate to the
 * accessor when their state grows beyond stage 0.  DAT_0438b7d8 is
 * also referenced by stage_palette (sub-mode select); same story. */
void    chara_equip_set_current_bank(int32_t bank_idx);
int32_t chara_equip_get_current_bank(void);
void    chara_equip_set_current_chara(int32_t chara_idx);
int32_t chara_equip_get_current_chara(void);

/* ─── Per-(bank, chara) equipment record accessors ─────────────────── */

/* Equip slot get/set.  Out-of-range → silent no-op (set) / 0 (get). */
uint32_t chara_equip_get_slot(int32_t bank, int32_t chara, int slot_idx);
void     chara_equip_set_slot(int32_t bank, int32_t chara, int slot_idx,
                              uint32_t slot_val);

/* Base-stat get/set.  Engine reads these as signed 32-bit ints at
 * record offset 0x2c + stat_idx*4. */
int32_t  chara_equip_get_base_stat(int32_t bank, int32_t chara,
                                   int stat_idx);
void     chara_equip_set_base_stat(int32_t bank, int32_t chara,
                                   int stat_idx, int32_t value);

/* Chara level — record dword at offset +0x00.  Read by stage_post_load
 * and by the skill-slot initializer in chara_skills.  No port writer
 * lands here yet (engine seeds via save_bank → stage record copy at
 * stage transition; that copy chip isn't ported). */
int32_t  chara_equip_get_chara_level(int32_t bank, int32_t chara);
void     chara_equip_set_chara_level(int32_t bank, int32_t chara,
                                     int32_t level);

/* Raw byte get/set into the per-chara 0x6c record.  Used by
 * chara_skills (writes the +0x60..+0x64 "skill slot alive flag" run
 * of 5 bytes).  Out-of-range (bank/chara/byte_offset) is a silent
 * no-op (set) or 0 (get). */
uint8_t  chara_equip_get_record_byte(int32_t bank, int32_t chara,
                                     int byte_offset);
void     chara_equip_set_record_byte(int32_t bank, int32_t chara,
                                     int byte_offset, uint8_t value);

/* Raw dword get/set into the per-chara 0x6c record.  Engine code that
 * touches the chara_equip arena via raw pointer arithmetic (e.g.
 * FUN_0048ff93's symmetric L/R write pair) lands at byte offsets the
 * named accessors don't cover (slot B at 0x18..0x28, level stomps from
 * the duplicate-write loop).  byte_offset must be 4-byte-aligned and in
 * [0, 0x6c - 4]; out-of-range is a silent no-op (set) or 0 (get). */
uint32_t chara_equip_get_record_dword(int32_t bank, int32_t chara,
                                      int byte_offset);
void     chara_equip_set_record_dword(int32_t bank, int32_t chara,
                                      int byte_offset, uint32_t value);

/* ─── Aggregator scratch (read-only accessors for tests + future
 *      combat-damage callers) ────────────────────────────────────── */

/* DAT_056db0ac..b8 — the 4-dword aggregated stat sum.  Indices map to
 * { 0: atk-equiv, 1: stat_1, 2: def-equiv, 3: stat_3 } matching the
 * order items are summed (item_record_t fields atk/def/matk/mdef).
 *
 * Index 0 → g_scene1_combat_damage_base_idle2 (scene1_combat_sm.c).
 * Index 2 → g_scene1_combat_damage_base_idle  (scene1_combat_sm.c).
 * Indices 1 + 3 → chara_equip module-locals (no other reader ported
 * yet; getter returns the stored value for test/diagnostic use). */
int32_t chara_equip_get_aggregate_stat(int idx);

/* DAT_056db0a8 — written to 5 by the aggregator (the loop counter,
 * stored after the loop completes).  Other combat code may read it
 * later; getter exposes the post-aggregator value. */
int32_t chara_equip_get_dat_056db0a8(void);

/* ─── The two engine bodies ───────────────────────────────────────── */

/* FUN_004844ef.  Reads current (bank, chara) record, runs 5 calls to
 * the internal slot distributor, sums into DAT_056db0ac..b8.  Writes
 * the listed scratch DATs to their engine reset values.
 *
 * No args, no return (matches engine).  Side effects on the four
 * scratch DATs and on g_scene1_combat_damage_base_idle{,_2}. */
void chara_equip_recompute_aggregate(void);

/* FUN_0048093f — distribute an encoded item-slot value's 4 stats (ATK/DEF/MAG/
 * MDEF for equipment; HP/SP for consumables) into `sum[4]` (ADDS; zero first).
 * Used by the encyclopedia item-detail overlay (FUN_0046a336). */
void chara_equip_item_stats(uint32_t slot_val, int32_t sum[4]);

/* ─── Test helper ─────────────────────────────────────────────────── */

void chara_equip_reset_for_test(void);

#endif /* OPENRECET_CHARA_EQUIP_H */
