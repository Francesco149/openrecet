/*
 * scene1_companion_ctrl.c — see scene1_companion_ctrl.h.
 */
#include "scene1_companion_ctrl.h"

#include "dungeon_party.h"        /* g_tear_wander (DAT_0438be6c..) */
#include "party.h"                /* DAT_056db054 */
#include "scene1_chr_sprite.h"    /* CHR_ACTOR_* record-field indices */
#include "scene1_player_ctrl.h"   /* actor record (mut) */

#define s_bob_counter (PW(0x56db054u).i)

void scene1_companion_ctrl_reset(void)
{
    s_bob_counter = 0;
}

void scene1_companion_ctrl_stage_reset(void)
{
    g_tear_wander = 0;
    g_tear_wander_t = 0;
    g_tear_wander_x = 0.0f;
    g_tear_wander_z = 0.0f;
}

int32_t scene1_companion_wander_state(void) { return g_tear_wander; }
int32_t scene1_companion_wander_t(void)     { return g_tear_wander_t; }
float   scene1_companion_wander_tx(void)    { return g_tear_wander_x; }
float   scene1_companion_wander_tz(void)    { return g_tear_wander_z; }

/* Trace-harness phase normalization (the segtrace `{phasepin}` op): db054 and
 * Tear's anim cycle restart at 0 (the port skips the intro video retail runs the
 * counter through, engine-quirks §94).  Comparison only. */
void scene1_companion_ctrl_phasepin(void)
{
    s_bob_counter = 0;
    int32_t *rec = player_ctrl_actor_record_mut(2);
    if (rec) {
        union { float f; int32_t i; } z = { .f = 0.0f };
        rec[CHR_ACTOR_FRAME]   = 0;
        rec[CHR_ACTOR_TIMER]   = z.i;
        rec[CHR_ACTOR_COUNTER] = 0;
    }
}

int scene1_companion_db054(void)
{
    return s_bob_counter;
}

void scene1_companion_ctrl_advance_phase_event(void)
{
    s_bob_counter++;
}
