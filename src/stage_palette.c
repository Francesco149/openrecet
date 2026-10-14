/*
 * stage_palette.c — see stage_palette.h for the engine layout + the
 * C7d rationale.
 *
 * Today we only seed the HOUSE record (stage 0). Dungeon variants
 * (DAT_0438b7d8 sub-modes) land in C7q.
 */

#include "stage_palette.h"

#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include "call_trace.h"
#include "map_collision.h"   /* FUN_0043244c / FUN_00471d45 */
#include "tables_stage.h"    /* g_stage (DAT_068dd2f8) */

stage_palette_t  g_stage_palette_house = {0};
stage_palette_t *g_stage_palette       = NULL;
int32_t          g_stage_b4dc;
static int32_t   s_stage_idx = -1;

_Static_assert(sizeof(stage_record_t) == sizeof(stage_palette_t), "both are the 0x1b3c stage record");

void stage_palette_init_house(void)
{
    /* Engine BSS-zero defaults for stage 0 — see stage_palette.h. The
     * memset is intentional even though static storage is already
     * zeroed: callers may invoke this on a stage transition back to
     * HOUSE after the engine has mutated fields, and the contract is
     * overwrite-not-OR (same as stage_init_house in stage_state.c). */
    memset(&g_stage_palette_house, 0, sizeof(g_stage_palette_house));
    g_stage_palette = &g_stage_palette_house;
    s_stage_idx = -1;
}

void stage_palette_select(int32_t idx)
{
    if (idx >= 0 && idx < g_stage.count) {
        g_stage_palette = (stage_palette_t *)(void *)&g_stage.records[idx];
        s_stage_idx = idx;
    } else {
        stage_palette_init_house();
    }
}

int32_t stage_palette_index(void) { return s_stage_idx; }

int32_t stage_index_for_id(int32_t id)
{
    int32_t r = 0;
    while (r < g_stage.count && g_stage.records[r].dungeon_id != id) r++;
    return r;
}

void stage_palette_clear_resource_caches(void)
{
    /* E.3 probe — FUN_0043244c @ 0x43244c. */
    CALL_TRACE_ENTER(0x43244cu);
    mapcol_clear_all();

    /* Engine clears two BSS slabs:
     *   - DAT_04348760..DAT_0437a760, stride 5 dwords (10,240 × 4 bytes).
     *   - DAT_00ac2434..DAT_046226b4, stride 0xbe008 (80 × 4 bytes).
     *
     * Neither slab is allocated in openrecet — these are engine-
     * internal per-stage chara/animation state pools whose ports
     * (FUN_00471d45 family, the chara_state_init/release pair, plus
     * the per-(bank,chara) animation cache at DAT_00ac2434) haven't
     * landed.  When they do, this function clears the first dword of
     * each entry (the "in-use" flag) to release all slots.  Probe-only
     * for now keeps the call_trace honest. */
}

void stage_palette_load_for_stage(void)
{
    /* E.3 probe — FUN_00474681 @ 0x474681. */
    CALL_TRACE_ENTER(0x474681u);

    /* DAT_068dd2f0 = the DAT_0438b4dc record; FUN_0043244c; then per `map:`
     * entry FUN_00472836 (the render mesh: scene_map_meshes, the caller) and
     * FUN_00471d45 (collision slot i from the file on disk, as retail fopens it;
     * a missing file leaves the slot empty).  The `mapbg` sprite (+0x108) is the
     * render side too. */
    stage_palette_select(g_stage_b4dc);
    stage_palette_clear_resource_caches();
    if (s_stage_idx < 0) return;
    const stage_record_t *r = &g_stage.records[s_stage_idx];
    const int large_pad = r->maptype >= 1;   /* FUN_00432ac6 reads the DAT_0438b4dc record */
    for (int32_t i = 0; i < r->map_count && i < STAGE_MAP_SLOTS; i++) {
        FILE *f = fopen(r->map[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *buf = n > 0 ? (char *)malloc((size_t)n) : NULL;
        if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) mapcol_load_x(i, buf, (size_t)n, large_pad);
        free(buf);
        fclose(f);
    }
}
