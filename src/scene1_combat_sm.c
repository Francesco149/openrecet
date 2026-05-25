/*
 * scene1_combat_sm.c — per-record state machine (combat tick).
 *
 * Engine source: FUN_0043865e @ 0x43865e.  Chip C8jb.1 ports Phase A
 * only (entry gates + per-tick flag).  See scene1_combat_sm.h header
 * and docs/findings/scene1-records-b-state-machine.md.
 */
#include "scene1_combat_sm.h"

#include <stddef.h>
#include <stdint.h>

#include "scene1_records_b_tick.h"   /* g_scene1_records_b_tick_flag */
#include "scene1_sim.h"              /* g_scene1_ingame_paused_flag */

int32_t g_scene1_combat_subphase;    /* DAT_0438be98 */
int32_t g_scene1_combat_world_pause; /* DAT_0438be9c */
int32_t g_scene1_combat_aux_pause;   /* DAT_0438bea0 */

int scene1_combat_sm_tick(int32_t *slot)
{
    /* slot pointer is consumed by Phases B/C/D in future chips; Phase A
     * does not dereference it. */
    (void)slot;

    /* Engine L35173-L35181 — early-exit gates.  Order matches engine. */
    if (g_scene1_combat_subphase    > 0)  return 0;
    if (g_scene1_combat_world_pause > 0)  return 0;
    if (g_scene1_combat_aux_pause   > 0)  return 0;
    if (g_scene1_ingame_paused_flag != 0) return 0;

    /* Engine L35185 — `DAT_06a46f98 = 1;`  Resolves PHC #21 — this is
     * the writer the integrator's per-tick clear was paired with. */
    g_scene1_records_b_tick_flag = 1;

    /* Phases B/C/D stub — C8jb.1 returns 0 here.  Future chips fall
     * through to the attacker NPC scan (Phase B). */
    return 0;
}

/* ─── void-hook adapter (test-only convenience) ──────────────────────── */
/*
 * The existing scene1_records_b_set_state_machine_hook accepts
 * `void (*)(int32_t *)`.  We adapt scene1_combat_sm_tick by discarding
 * the int return.  C8jb.fin replaces this adapter with an int-ret
 * variant.
 */
static void combat_sm_void_adapter(int32_t *slot)
{
    (void)scene1_combat_sm_tick(slot);
}

void scene1_combat_sm_install_as_void_hook(void)
{
    scene1_records_b_set_state_machine_hook(combat_sm_void_adapter);
}

void scene1_combat_sm_uninstall_void_hook(void)
{
    scene1_records_b_set_state_machine_hook(NULL);
}
