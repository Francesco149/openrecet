/*
 * stage_state.c — seeds the per-stage runtime state. See
 * stage_state.h for the engine layout + chip-7c rationale.
 *
 * Today this is the four scene-1 prop selectors, now sourced from the
 * active working-save slot (the loaded game's shop upgrades) instead of
 * hardcoded zero. As more of the 0x2dfc8-byte stage record ports,
 * fields fan out from this module so the boot sequence has one
 * consistent place to set "current stage" state.
 */

#include "stage_state.h"

#include <stdint.h>

#include "save_work.h"
#include "scene_floor.h"
#include "scene_jutan.h"
#include "scene_table.h"
#include "scene_walls.h"

void stage_init_house(void)
{
    /* The selectors live in the active working-save slot. On a zeroed
     * working arena (boot, or a fresh new game whose bank fields here
     * are 0) every selector reads 0 = the engine fresh-game defaults,
     * so boot/new-game are byte-identical to the old hardcode. A loaded
     * save drives the player's purchased wall/floor/carpet/table. */
    const uint32_t *bank = save_work_dwords_at(save_work_active_slot());
    if (bank) {
        g_scene_walls_selector = (int32_t)bank[STAGE_FIELD_WALLS_SELECTOR_DWORD];
        g_scene_floor_selector = (int32_t)bank[STAGE_FIELD_FLOOR_SELECTOR_DWORD];
        g_scene_jutan_selector = (int32_t)bank[STAGE_FIELD_JUTAN_SELECTOR_DWORD];
        g_scene_table_selector = (int32_t)bank[STAGE_FIELD_TABLE_SELECTOR_DWORD];
    } else {
        g_scene_walls_selector = STAGE_HOUSE_WALLS_SELECTOR_DEFAULT;
        g_scene_floor_selector = STAGE_HOUSE_FLOOR_SELECTOR_DEFAULT;
        g_scene_jutan_selector = STAGE_HOUSE_JUTAN_SELECTOR_DEFAULT;
        g_scene_table_selector = STAGE_HOUSE_TABLE_SELECTOR_DEFAULT;
    }
}
