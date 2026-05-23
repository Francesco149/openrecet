/*
 * stage_state — minimal seed for the per-stage runtime state.
 *
 * Engine: a giant 0x2dfc8-byte (188360-byte) record per stage at
 * `&DAT_044e3798`, indexed by `DAT_0438b1e0` (current stage index).
 * The full record holds gameplay, geometry, AI, particles, save data
 * — well over a hundred fields. We port only what the scene-1 load
 * chain actually reads. Today that's just the four scene-1 prop
 * selectors:
 *
 *   walls   — int32, engine `(&DAT_0451057c)[stage * 0xb7f2]`
 *   floor   — int32, engine `(&DAT_04510580)[stage * 0xb7f2]`
 *   jutan   — int32, engine `(&DAT_04510584)[stage * 0xb7f2]`
 *   table   — int32, engine `(&DAT_04510588)[stage * 0xb7f2]`
 *
 * The port currently exposes these as four standalone int32 BSS
 * globals (`g_scene_*_selector`) — see scene_walls.h / scene_floor.h
 * / scene_jutan.h / scene_table.h. When the full 0x2dfc8 stage
 * record ports (well past C7), the standalone ints fold back into
 * the record at the engine offsets.
 *
 * Why a `stage_init_house()` hook even though the engine starts from
 * BSS-zero too:
 *
 *   1. **Documentation.** The "stage 0 defaults are zero" property
 *      is a happy accident of the engine putting the starter
 *      asset at slot 0 of each table (kabe_sikkui / shop_table01 /
 *      etc.). Writing the values explicitly makes that contract
 *      visible to readers + tests.
 *
 *   2. **Future stage transitions.** When the user buys a wall
 *      upgrade and exits to the world map then back, the engine
 *      writes the player's chosen selector index back into the
 *      stage record (engine sequence: save bank → DAT_0438b7b4..c0
 *      → stage record on re-entry). The same hook becomes the
 *      single place to fan that out into the four globals.
 *
 *   3. **Tests.** Worker-body unit tests can call this to get a
 *      known-good baseline before exercising the load chain.
 *
 * Idempotent — safe to call multiple times.
 */

#ifndef OPENRECET_STAGE_STATE_H
#define OPENRECET_STAGE_STATE_H

/*
 * Engine default values for stage 0 (HOUSE / shop interior, fresh
 * game state with no purchased upgrades). All four selectors default
 * to 0:
 *
 *   walls=0   →  `kabe_sikkui.bmp`   (plaster — the starter wall)
 *   floor=0   →  `yuka_ki.bmp`       (wood plank — starter floor)
 *   jutan=0   →  first slot of the 8-entry jutan table
 *   table=0   →  `shop_table` family pair, 01/02 variants
 *
 * Tags for reference in tests; the function below writes these into
 * the four selector globals via their owning modules.
 */
#define STAGE_HOUSE_WALLS_SELECTOR_DEFAULT  0
#define STAGE_HOUSE_FLOOR_SELECTOR_DEFAULT  0
#define STAGE_HOUSE_JUTAN_SELECTOR_DEFAULT  0
#define STAGE_HOUSE_TABLE_SELECTOR_DEFAULT  0

/*
 * Seed the per-stage runtime state for stage 0 (HOUSE). Writes the
 * four selector globals to their fresh-game defaults. Safe to call
 * before or after the worker bodies have been registered.
 */
void stage_init_house(void);

#endif /* OPENRECET_STAGE_STATE_H */
