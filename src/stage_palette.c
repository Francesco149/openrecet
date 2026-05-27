/*
 * stage_palette.c — see stage_palette.h for the engine layout + the
 * C7d rationale.
 *
 * Today we only seed the HOUSE record (stage 0). Dungeon variants
 * (DAT_0438b7d8 sub-modes) land in C7q.
 */

#include "stage_palette.h"

#include <string.h>

#include "call_trace.h"

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

void stage_palette_clear_resource_caches(void)
{
    /* E.3 probe — FUN_0043244c @ 0x43244c. */
    CALL_TRACE_ENTER(0x43244cu);

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

    /* Engine step 1-2: pointer-to-current entry.
     *   iVar3 = DAT_0438b4dc * 0x1b3c;
     *   DAT_068dd2f0 = &DAT_068dd2f8 + iVar3;
     * The caller (scene1_preload_house) invokes stage_palette_init_house()
     * after this function, which sets g_stage_palette to the HOUSE
     * record.  We intentionally do NOT re-do that here — the engine's
     * pointer set targets the same slot in our single-stage layout
     * (only HOUSE record is allocated today; dungeon variants land in
     * C7q).  See stage_palette.h for the deviation note. */

    /* Engine step 3: clear resource caches. */
    stage_palette_clear_resource_caches();

    /* Engine step 4-5: gated mesh + sprite preloaders.  Both gates
     * (palette[+0x1a2c] and palette[+0x108]) are BSS-zero on HOUSE
     * defaults, so the inner loops don't fire — body intentionally
     * empty here.  When dungeon stage records ship non-zero counts,
     * the loops will need FUN_00472836 (mesh-name preprocessor, 1609 B)
     * and FUN_00471d45 ported. */
}
