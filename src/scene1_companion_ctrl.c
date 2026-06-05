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
#include "scene1_conversation_pose.h" /* yield anim/facing while the pose is held */
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

/* Trace-harness phase normalization (the segtrace `{phasepin}` op).  The
 * companion's free-roam phase — the db054 bob/sparkle counter AND the sprite
 * anim cycle (FRAME/TIMER/COUNTER) — is a per-scene frame count whose value at
 * free-roam onset depends on how long the pre-free-roam intro ran.  Retail
 * freezes db054 through the `recet_op.wmv` intro video and only ticks it from
 * the conversation (db054==43 at HOUSE_FREEROAM, engine-quirks §94); the port
 * SKIPS the video (§13), so its counter accumulates ~1518 extra ticks by
 * free-roam.  The per-frame increment + facing LAWS are bit-exact (constant
 * offset, zero drift — `scene1-tear-visual-diffs.md` §"#3/#4 determinism
 * verdict"), so this is a load-time-dependent phase ORIGIN, not a logic gap.
 * This resets that origin to a canonical zero so a trace comparison against a
 * retail run (pinned the same way) is phase-clean.  Trace/comparison ONLY — the
 * shipped game keeps the engine-faithful free-running counter (retail's own
 * free-roam phase is equally load-dependent). */
void scene1_companion_ctrl_phasepin(void)
{
    s_bob_counter = 0;
    int32_t *rec = player_ctrl_actor_record_mut(CO_ACTOR);
    if (rec) {
        union { float f; int32_t i; } z = { .f = 0.0f };
        rec[CHR_ACTOR_FRAME]   = 0;
        rec[CHR_ACTOR_TIMER]   = z.i;
        rec[CHR_ACTOR_COUNTER] = 0;
    }
}

/* The shared per-frame phase counter DAT_056db054.  The engine has ONE db054
 * that several subsystems read within a frame; here it backs the companion bob.
 * The player controller's foot-dust emit (FUN_0048b850, scene1_player_ctrl.c)
 * is another db054 reader — it runs in the player tick, BEFORE this tick
 * increments the counter, so both see this frame's value (matches the engine,
 * where FUN_0048b850 reads db054 and the companion update is nested inside it). */
int scene1_companion_db054(void)
{
    return s_bob_counter;
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

    /* Anim/facing — the free-roam spring-follow law FUN_0048a4d1 (all.c:89083-
     * 89121, the function FUN_0048a833 calls every free-roam frame; both fire
     * 49/49 over the loaded-shop window).  If the companion MOVED this frame →
     * walk anim + copy the target's (player's) facing octant (dab58 =
     * dab00[target·0xb]).  Else IDLE → set the idle anim ONLY and LEAVE THE
     * FACING UNTOUCHED: FUN_0048a4d1's idle path (moved ≤ 0.01) writes anim 0 but
     * never touches DAT_056dab58, so the facing holds its entry seed (octant 4 =
     * facing DOWN/toward-camera, set by player_ctrl_pose_house_standing) or the
     * last walking direction.  Retail bears this out — the loaded-shop fairy is
     * octant 4 for the whole window (flow_diff --verdict --align-field db054:
     * coct retail 4 / port 2, every other actor field bit-1:1).
     *   The OLD port rule `dab58 = (comp.x≤player.x)?6:2` was WRONG here: that
     * 6/2 side-facing is FUN_0048a833's INTRO-ONLY branch A (gated
     * DAT_0438b928==1 && DAT_0438b924<200 — the iv1_1 scene where Recette looks
     * UP at Tear), not the free-roam law.  It happened to pass new-game WALK
     * scenarios only because Tear is MOVING there (facing = copied player octant
     * on both sides); the bug surfaces only when the fairy is stationary from
     * scene entry (a CONTINUE-load into the shop). */
    /* While the iv1_2 conversation pose is held, FUN_0048407f's branch owns
     * Tear's anim (4 = talk) + facing and the per-actor anim step already ran in
     * scene1_conversation_pose_tick — yield the free-roam selection + the tick
     * below (the engine's FUN_0048a4d1 anim block is likewise gated off in the
     * event arm).  The spring-follow position / Y-bob / wing-sparkle still run:
     * the player is stationary during the dialogue so Tear holds her offset. */
    int in_conversation = scene1_conversation_pose_active();

    int prev_animsel = rec[CO_REC_ANIMSEL];
    float mvx = comp[0] - pre_x, mvz = comp[2] - pre_z;
    float moved = sqrtf(mvx * mvx + mvz * mvz);
    if (!in_conversation) {
        if (moved <= CO_MOVE_EPS) {
            co_set_anim(rec, CO_ANIM_IDLE);
            /* idle → anim only; facing holds its seed/last value (FUN_0048a4d1
             * writes no facing on the moved≤0.01 path). */
        } else {
            co_set_anim(rec, CO_ANIM_MOVING);
            if (prec)
                rec[CHR_ACTOR_FACING] = prec[CHR_ACTOR_FACING];
        }
    }

    /* Advance the companion's sprite animation every non-transition frame,
     * exactly as the player controller ticks actor 0 (scene1_player_ctrl.c
     * L919).  The engine runs the generic per-actor sprite anim-tick
     * (FUN_00482a71 / chr_anim_tick) on the companion too: Tear's idle wings
     * FLAP in a 4-frame loop (the wing-glow leaf reads the live FRAME → frames
     * 0,1 are the spread / largest pose, 2 is folded / smallest, 3 mid; ground
     * truth runs/comp-anim-probe, engine-quirks §81), so freezing the FRAME
     * stuck the glow on one pose and diverged from retail's animated wings.
     * On an idle↔moving transition co_set_anim already reset the cycle
     * to frame 0, so — mirroring the player — skip the tick on that frame
     * (CO_REC_ANIMSEL changed) and advance only when the anim is unchanged.
     * See docs/findings/scene1-wing-glow.md, engine-quirks §81. */
    if (!in_conversation && rec[CO_REC_ANIMSEL] == prev_animsel)
        chr_anim_tick(rec, player_ctrl_actor_char(CO_ACTOR), 1.0f);

    /* Wing-glow sparkle (FUN_0048a833 tail): emit every 4th frame, off the
     * post-move fairy along her facing.  Uses the PRE-increment counter so it
     * shares the bob's db054 phase (the engine reads db054 once per frame). */
    if (s_bob_counter % CO_SPARKLE_PERIOD == 0)
        co_emit_wing_sparkle(rec, comp);

    s_bob_counter++;
}
