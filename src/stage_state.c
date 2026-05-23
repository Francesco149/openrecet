/*
 * stage_state.c — seeds the per-stage runtime state. See
 * stage_state.h for the engine layout + chip-7c rationale.
 *
 * Today this is purely the four scene-1 prop selectors. As more of
 * the 0x2dfc8-byte stage record ports, fields fan out from this
 * module so the boot sequence has one consistent place to set
 * "current stage" state.
 */

#include "stage_state.h"

#include "scene_floor.h"
#include "scene_jutan.h"
#include "scene_table.h"
#include "scene_walls.h"

void stage_init_house(void)
{
    g_scene_walls_selector = STAGE_HOUSE_WALLS_SELECTOR_DEFAULT;
    g_scene_floor_selector = STAGE_HOUSE_FLOOR_SELECTOR_DEFAULT;
    g_scene_jutan_selector = STAGE_HOUSE_JUTAN_SELECTOR_DEFAULT;
    g_scene_table_selector = STAGE_HOUSE_TABLE_SELECTOR_DEFAULT;
}
