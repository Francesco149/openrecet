/*
 * dungeon_results.h — the end of an expedition: the results screen (pause
 * action 2) and what it hands back.
 *
 *   FUN_0048f87c  dungeon_results_open   the screen's setup (pause worker case 9)
 *   FUN_0048f931  dungeon_results_tick   the screen: a returning party keeps the
 *                                        whole bag; a defeated one keeps 1..3
 *                                        items by merchant level; then the score,
 *                                        the period, the next scene
 *   FUN_00442c86  dungeon_results_strip  drop the lent (0x20) items, return the
 *                                        active chara's found gear to the bag,
 *                                        restore the departure gear (FUN_0048f848)
 *   FUN_0048f7f0  dungeon_results_unpack the bag into the inventory
 *   FUN_0048f8bb  dungeon_results_score  + ftol(sqrt(price / 10 + 10)) points
 */
#ifndef OPENRECET_DUNGEON_RESULTS_H
#define OPENRECET_DUNGEON_RESULTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dungeon_results_open(void);
void dungeon_results_tick(void);
void dungeon_results_strip(void);
void dungeon_results_unpack(void);

/* the screen's state, for the render and state_va */
extern int32_t g_results_leaving;   /* DAT_074b2f08 */
extern int32_t g_results_kept_all;  /* DAT_074b2f5c */
extern int32_t g_results_intro;     /* DAT_074b2f60 */
extern int32_t g_results_kind;      /* DAT_074b2f64 (DAT_0438be98 at the open) */
extern int32_t g_results_kept[4];   /* DAT_0438b718: [1..3] the kept items */
extern int32_t g_results_row;       /* DAT_0438b744: 0 = Done, 1..3 a kept slot */
extern int32_t g_results_picking;   /* DAT_0438b748: choosing from the bag */

#ifdef __cplusplus
}
#endif

#endif /* OPENRECET_DUNGEON_RESULTS_H */
