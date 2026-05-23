/*
 * stage_palette.c — see stage_palette.h for the engine layout + the
 * C7d rationale.
 *
 * Today we only seed the HOUSE record (stage 0). Dungeon variants
 * (DAT_0438b7d8 sub-modes) land in C7q.
 */

#include "stage_palette.h"

#include <string.h>

stage_palette_t  g_stage_palette_house = {0};
stage_palette_t *g_stage_palette       = NULL;

void stage_palette_init_house(void)
{
    /* Engine BSS-zero defaults for stage 0 — see stage_palette.h. The
     * memset is intentional even though static storage is already
     * zeroed: callers may invoke this on a stage transition back to
     * HOUSE after the engine has mutated fields, and the contract is
     * overwrite-not-OR (same as stage_init_house in stage_state.c). */
    memset(&g_stage_palette_house, 0, sizeof(g_stage_palette_house));
    g_stage_palette = &g_stage_palette_house;
}
