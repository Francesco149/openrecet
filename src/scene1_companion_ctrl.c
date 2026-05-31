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
#include "scene1_particles_tick.h"/* g_scene1_actor_pos + g_scene1_player_ground_y + g_scene1_camera_yaw */
#include "scene1_spawn.h"         /* scene1_spawn (FUN_00447f4f) + the dab58 init global */

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

/* ── wing-glow sparkle emit (FUN_0048a833 tail, LAB_0048b2a0) ──────────────
 * Every 4th frame the companion driver drops one type-0x1f "scene-counter wave"
 * particle just off the fairy along her facing — the glowing-wing sparkle.
 * Ported from the asm; Ghidra dropped the spawn's type + scale args, recovered
 * from objdump @ 0x48b38e (the 0x1c = 7-dword cleanup), see engine-quirks §73:
 *
 *   if (b4b4==0 && dae20>0 && dae2c>0 && (b8f8!=0 || db054%4==0) && b1a0==0)
 *     angle = facing·2π/8 − camera_yaw
 *     spawn(0, da1f0−sin(angle)·0.6, da1f4+1.1, da1f8−cos(angle)·0.6, 0x1f, 0.1, …)
 *
 * The four non-frame gate terms are all true in HOUSE free-roam, so only the
 * every-4th-frame rate term varies:
 *   - DAT_0438b4b4 (scene fade-in countdown) is 0 once faded in (port stubs 0);
 *   - _DAT_056dae20 / _DAT_056dae2c (companion render scale cw/ch) are 1.0 (>0)
 *     for the live fairy;
 *   - DAT_0438b1a0 (config `easydisp`) defaults 0;
 *   - DAT_0438b8f8 (per-frame-emit override) is 0 → the every-4th-frame branch.
 * DAT_056db054 is the per-frame bob counter (s_bob_counter), reset to 0 with the
 * scene — the SAME counter the bob reads, so the %4 phase rides the §71-
 * validated bob alignment (the engine reads db054 once per frame for both).
 *
 * The spawned particle is faithful but INVISIBLE today — the table-A glow-
 * billboard renderer (FUN_004176ff, 30 KB) is unported (only pass_f / type 0x92
 * draws).  The integrator (scene1_particles_tick) DOES age + kill type 0x1f
 * (grav −0.001, damp 0.97, kill age 0x20), so the emit cannot leak slots; the
 * sparkle becomes visible for free once that renderer lands. */
#define CO_SPARKLE_PERIOD  4
#define CO_SPARKLE_OFFSET  0.6f    /* .rdata 0x51969c — facing-dir push */
#define CO_SPARKLE_Y       1.1f    /* .rdata 0x519724 — +Y above the fairy */
#define CO_SPARKLE_TYPE    0x1f    /* recovered: reused `push $0x1f` (cos arg) */
#define CO_SPARKLE_SCALE   0.1f    /* recovered: reused 0.1 const push (param_6) */
#define CO_TWO_PI          6.2831855f   /* .rdata 0x519398 */

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

/* Drop one wing-glow sparkle at the fairy's current facing (engine LAB_0048b2a0,
 * read after the spring-follow has set the post-move position + facing octant). */
static void co_emit_wing_sparkle(const int32_t *rec, const float *comp)
{
    int   facing = rec[CHR_ACTOR_FACING];      /* DAT_056dab58 octant */
    float angle  = (float)facing * CO_TWO_PI / 8.0f - g_scene1_camera_yaw;
    /* The type-0x1f init body re-reads dab58 for its per-particle jitter; the
     * companion controller IS the engine's dab58 writer, so keep the spawn-side
     * model (g_scene1_spawn_scene_counter_dab58) in step with the live facing. */
    g_scene1_spawn_scene_counter_dab58 = facing;
    scene1_spawn(0,
                 comp[0] - sinf(angle) * CO_SPARKLE_OFFSET,
                 comp[1] + CO_SPARKLE_Y,
                 comp[2] - cosf(angle) * CO_SPARKLE_OFFSET,
                 CO_SPARKLE_TYPE, CO_SPARKLE_SCALE, 0);
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

    /* Wing-glow sparkle (FUN_0048a833 tail): emit every 4th frame, off the
     * post-move fairy along her facing.  Uses the PRE-increment counter so it
     * shares the bob's db054 phase (the engine reads db054 once per frame). */
    if (s_bob_counter % CO_SPARKLE_PERIOD == 0)
        co_emit_wing_sparkle(rec, comp);

    s_bob_counter++;
}
