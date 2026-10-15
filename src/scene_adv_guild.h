/*
 * scene_adv_guild.h — engine mode 11, the Adventurers' Guild (world-map dest 6):
 * hire an adventurer, pack the bag, pick the dungeon and the start floor.
 *
 * Engine sources ported here:
 *   FUN_0045bdc2 @ 0x45bdc2 (546 B)  — the scene init (load-worker case 11, with
 *                                      FUN_00473874): the bag cleared, the bag
 *                                      size by merchant level, the cursors from the
 *                                      last expedition (work +0x2cdf8/fc/0x2ce00),
 *                                      the start-floor counts, the dungeon count.
 *   FUN_00473874 @ 0x473874          — the texture-group-7 load.
 *   FUN_0045c051 @ 0x45c051 (3021 B) — the update (FUN_004536cb LABEL_106, only
 *                                      while no dialogue runs): the first-visit
 *                                      intro FUN_0044c7c3 (iv2_7, iv2_8, then out
 *                                      to the world map), the message boxes, the
 *                                      adventurer list, the bag editor + item
 *                                      picker, the dungeon map, the floor list,
 *                                      the departure / Leave commit.
 *   FUN_0044c7c3 @ 0x44c7c3 (204 B)  — the first-visit intro.
 *   FUN_0045cc85 @ 0x45cc85 (4579 B) — the render; FUN_0045cc1e the bag arrows.
 * See docs/findings/adventurers-guild-RE.md.
 */
#ifndef OPENRECET_SCENE_ADV_GUILD_H
#define OPENRECET_SCENE_ADV_GUILD_H

#include <stdint.h>

struct IDirect3DDevice8;

/* The 38-slot expedition bag (DAT_0438b5ec[], 1-based in the engine's
 * DAT_0438b5e8[k]); -1 = empty.  The dungeon (FUN_00468338 mode 6) reads it. */
#define ADV_GUILD_BAG_SLOTS 38
int adv_guild_bag_slot(int k);     /* k = 1..ADV_GUILD_BAG_SLOTS */
int *adv_guild_bag(void);          /* &DAT_0438b5ec[0] (slot 1) */
int  adv_guild_bag_size(void);     /* DAT_005c5a98 */

/* FUN_0045bcea: DAT_0438b4dc = the stage of (DAT_0438b4c8, DAT_0438b4cc);
 * `bb` = the active work bank. */
void adv_guild_set_stage(const uint8_t *bb);

/* FUN_0045c051 — one update tick (sim.c case 11; the caller gates on no
 * dialogue, as LABEL_106 does). */
void scene_adv_guild_sim(void);

/* Device registration + the load-worker case-11 callback. */
void scene_adv_guild_init(struct IDirect3DDevice8 *dev);

/* FUN_004547ab case 11: FUN_0045cc85 + FUN_0040a765 + FUN_0045cc1e. */
void scene_adv_guild_render(struct IDirect3DDevice8 *dev);

void scene_adv_guild_reset(void);

/* FUN_0049a59e's Continue arm: the bag size by merchant level (DAT_005c5a98)
 * and an empty bag (DAT_0438b5ec[] = -1). */
void adv_guild_continue_reset(int merchant_level);

/* FUN_0045bd36 — what the unpause (FUN_00453384) restores in mode 11. */
void scene_adv_guild_unpause(void);

/* State mirrors for --state-va (DAT_06a49b44 … DAT_005c5aa0). */
int adv_guild_dbg(uint32_t va, int32_t *out);

#endif /* OPENRECET_SCENE_ADV_GUILD_H */
