/*
 * item_use.h — using an item on the active chara (the dungeon pause Items
 * submenu, FUN_0047ff40): the recovery amounts and their application.
 *
 *   FUN_004848c4  item_hp_recovery   the HP an item restores (+ quality / 5)
 *   FUN_00484948  item_sp_recovery   the SP an item restores (+ quality / 5)
 *   FUN_004849a6  item_use_food      HP / SP up by the recovery + a rand bonus
 *   FUN_00484bcb  item_use_medicine  max HP up (4101..4107) or a 3600-tick buff
 *   FUN_0049ef78  item_category_is   the item's category name starts with `name`
 *
 * Items are encoded `id << 6 | rare << 4 | quality`.
 */
#ifndef OPENRECET_ITEM_USE_H
#define OPENRECET_ITEM_USE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t item_hp_recovery(int32_t item);
int32_t item_sp_recovery(int32_t item);
/* returns 1 (HP), 2 (SP), 3 (both) or 0; *hp / *sp get the gains */
int32_t item_use_food(int32_t item, int32_t *hp, int32_t *sp);
/* returns the max-HP gain (or 0 / 30 for the buffs) */
int32_t item_use_medicine(int32_t item);
int     item_category_is(int32_t item, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_ITEM_USE_H */
