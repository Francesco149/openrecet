/*
 * scene1_companion_ctrl.c — HOUSE companion (Tear / actor 2) controller.
 * Engine FUN_0048a4d1 (the spring-follow helper FUN_0048a833 invokes for the
 * free-roaming companion).  See header + runs/companion-truth/FINDINGS.md
 * (engine-quirks §71).
 *
 * Validated against the retail capture: feeding retail's player trajectory
 * through this exact law reproduces the companion's per-frame XZ to a one-step
 * mean error of 0.0036 (threshold 1.5), and the facing-copies-player rule
 * matches 621/621 moving frames.
 */

#include "scene1_companion_ctrl.h"

#include <math.h>
#include <stdint.h>

#include "call_trace.h"           /* CALL_TRACE_ENTER */
#include "scene1_player_ctrl.h"   /* actor record (mut/read) */
#include "scene1_chr_sprite.h"    /* CHR_ACTOR_* record-field indices */
#include "scene1_particles_tick.h"/* g_scene1_actor_pos + g_scene1_player_ground_y */

/* ── engine float constants (FUN_0048a4d1 .rdata, objdump 2026-05-31) ──
 *   0x5198cc = 0.15 (spring + Y lerp)   0x519bc4 = 0.35 (velocity clamp)
 *   threshold 1.5 (be94==0 path; tightens to 1.0 when DAT_0438be94>0 — a
 *   close-follow mode not reached in peaceful HOUSE)
 *   0x5198c4 = 0.04 (bob freq)   0x5198d8 = 0.2 (bob amp)   0x519438 = 3.0   */
#define CO_SPRING       0.15f
#define CO_VEL_CLAMP    0.35f
#define CO_THRESHOLD    1.5f
#define CO_BOB_FREQ     0.04f
#define CO_BOB_AMP      0.2f
#define CO_BOB_OFFSET   3.0f

/* |moved| ≤ this → idle, else walk (engine `if (|moved| <= 0.01)`). */
#define CO_MOVE_EPS     0.01f

/* Companion anim ids (FUN_0048a4d1): 1 = moving, 0 = idle (the alt idle id 5
 * fires only when the stage flag DAT_0450f405 is set — not modeled; HOUSE
 * new-game takes the id-0 path). */
#define CO_ANIM_MOVING  1
#define CO_ANIM_IDLE    0

/* Record field 5 (no CHR_ACTOR_* name) is the engine's anim-id selector
 * DAT_056dab54 — held to detect transitions. */
#define CO_REC_ANIMSEL  5

#define CO_ACTOR        2          /* the companion is actor slot 2 */
#define CO_TARGET       0          /* it follows actor 0 (the player) */

/* Per-scene hover-bob phase counter — engine DAT_056db054.  The engine's real
 * writer is the per-frame open FUN_00483e7b (++ every frame); modeled here as a
 * companion-local counter (the companion is currently db054's only port reader,
 * and in HOUSE free-roam this tick runs once per frame).  Promote to a shared
 * g_scene1_anim_counter when other db054 readers land. */
static int s_bob_counter = 0;

void scene1_companion_ctrl_reset(void)
{
    s_bob_counter = 0;
}

/* On an idle↔moving transition reseed the anim cycle (frame/timer/counter → 0)
 * and write the new id into both the ANIM field and the field-5 selector; an
 * unchanged state leaves the record alone.  Mirrors FUN_0048a4d1's anim block. */
static void co_set_anim(int32_t *rec, int anim_id)
{
    if (rec[CO_REC_ANIMSEL] == anim_id)
        return;
    union { float f; int32_t i; } z = { .f = 0.0f };
    rec[CO_REC_ANIMSEL]    = anim_id;
    rec[CHR_ACTOR_FRAME]   = 0;
    rec[CHR_ACTOR_TIMER]   = z.i;
    rec[CHR_ACTOR_COUNTER] = 0;
    rec[CHR_ACTOR_ANIM]    = anim_id;
}

void scene1_companion_ctrl_tick(void)
{
    /* Engine path: FUN_0048a833 (the companion dispatcher) → FUN_0048a4d1 (the
     * spring-follow helper) for the free-roam case.  The dispatcher's other
     * branches (intro standing-pose, random-wander) are retail-only (gated
     * behind the unported §60 event-gate) — see FINDINGS.md. */
    CALL_TRACE_ENTER(0x48a4d1u);

    if (player_ctrl_actor_char(CO_ACTOR) == -1)   /* companion not live */
        return;

    int32_t       *rec    = player_ctrl_actor_record_mut(CO_ACTOR);
    const int32_t *prec   = player_ctrl_actor_record(CO_TARGET);   /* player record */
    float         *comp   = g_scene1_actor_pos[CO_ACTOR];
    const float   *player = g_scene1_actor_pos[CO_TARGET];

    float pre_x = comp[0], pre_z = comp[2];   /* for the moved-delta anim test */

    /* Spring toward a point CO_THRESHOLD units from the player, on the
     * companion's current bearing (so it trails rather than overlapping). */
    float dx = comp[0] - player[0];
    float dz = comp[2] - player[2];
    float dist = sqrtf(dx * dx + dz * dz);
    float des_x = comp[0], des_z = comp[2];
    if (dist > CO_THRESHOLD) {
        float ang = atan2f(dx, dz);
        des_x = sinf(ang) * CO_THRESHOLD + player[0];
        des_z = cosf(ang) * CO_THRESHOLD + player[2];
    }
    float vx = (des_x - comp[0]) * CO_SPRING;
    float vz = (des_z - comp[2]) * CO_SPRING;
    float vmag = sqrtf(vx * vx + vz * vz);
    if (vmag > CO_VEL_CLAMP) {              /* velocity clamp */
        vx = (vx * CO_VEL_CLAMP) / vmag;
        vz = (vz * CO_VEL_CLAMP) / vmag;
    }
    comp[0] += vx;
    comp[2] += vz;

    /* Fairy hover bob — Y lerps 0.15 toward sin(db054·0.04)·0.2 + ground_y + 3.0. */
    float bob = sinf((float)s_bob_counter * CO_BOB_FREQ) * CO_BOB_AMP
                + g_scene1_player_ground_y + CO_BOB_OFFSET;
    comp[1] += (bob - comp[1]) * CO_SPRING;

    /* Anim/facing.  If the companion MOVED this frame → walk anim + copy the
     * PLAYER's facing octant (FUN_0048a4d1: dab58 = dab00[target·0xb]; verified
     * 621/621 against retail).  Else idle: the engine's standing-pose branch
     * (FUN_0048a833) faces the companion toward the player's SIDE — dab58 =
     * (comp.x ≤ player.x) ? 6 : 2 — which is what retail shows for the idle fairy
     * (95% of idle frames vs the capture; the port collapses the spring + the
     * standing-pose facing into one free-roam controller since it has no §60
     * intro window).  This is the fairy's resting orientation (octant 2 = facing
     * left toward Recette at the spawn offset). */
    float mvx = comp[0] - pre_x, mvz = comp[2] - pre_z;
    float moved = sqrtf(mvx * mvx + mvz * mvz);
    if (moved <= CO_MOVE_EPS) {
        co_set_anim(rec, CO_ANIM_IDLE);
        rec[CHR_ACTOR_FACING] = (comp[0] <= player[0]) ? 6 : 2;
    } else {
        co_set_anim(rec, CO_ANIM_MOVING);
        if (prec)
            rec[CHR_ACTOR_FACING] = prec[CHR_ACTOR_FACING];
    }

    s_bob_counter++;
}
