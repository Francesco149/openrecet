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
#include "customer_service.h"     /* customer_service_f404 — the at-counter-arm gate */
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
 * is another db054 reader.  The engine reads db054 from BOTH the companion update
 * (FUN_0048a833, wing-sparkle gate) and the foot dust, then increments it ONCE at
 * the FUN_0048b850 tail (L90242) — AFTER both reads.  The port mirrors that: on a
 * free-roam walk frame player_ctrl_b850_move() runs the companion tick then the
 * foot dust (both read this value) then scene1_companion_ctrl_advance_phase()
 * bumps the counter; on every other frame scene1_sim.c ticks+advances it.  So all
 * intra-frame readers see this frame's value and the ++ lands last. */
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

/* ── cc08==4 (sell-active / f404) AT-COUNTER pose — FUN_0048a833's `local_c != 0`
 * branch (by-address 0x48ace7-0x48aeda, objdump 2026-06-19) ──────────────────
 * When the shop enters customer-service mode the companion (Tear — the tutorial's
 * first "customer") leaves the free-roam spring-follow and instead walks to a
 * fixed offset BESIDE the player at the counter (player.x ± 1.3, player.z),
 * holding the at-counter "ready" pose (canim 4 = the engine's anim id 4) facing
 * the player (octant 2 or 6).  The approach is a flat 0.1/frame lerp (NOT the
 * spring); the Y target is sin(db054·0.04)·0.2 + 3.0 — note NO ground_y term,
 * unlike the free-roam bob (asm 0x48ad65-0x48ad93; the at-counter Y sits at a
 * fixed height).
 *
 * The branch consumes NO rng (sqrt/sin/atan2/ftol only), so the carefully aligned
 * haggle stream (RE §8.8) is untouched; the wing-sparkle that DOES draw rng is
 * emitted by the shared tail in scene1_companion_ctrl_tick, unchanged.
 *
 * Target actor = the player (engine local_2c = (*DAT_068dd2f0>0); *DAT_068dd2f0==0
 * in the tutorial — a single companion, no extra followers — so local_2c=0 and the
 * post-move facing-copy at 0x48b284 is skipped, leaving the 2/6 at-counter facing).
 * Consts: 0x519b90=1.3, 0x519314=2.0, 0x5193a0=0.1, 0x5198c4=0.04, 0x5198d8=0.2,
 * 0x519438=3.0.  Walk-facing octant reuses the shared player_ctrl_facing_octant. */
#define CO_COUNTER_OFFSET   1.3f    /* .rdata 0x519b90 — ±X beside the player */
#define CO_COUNTER_ARRIVE   2.0f    /* .rdata 0x519314 — dist < this ⇒ arrived */
#define CO_COUNTER_MOVE     0.1f    /* .rdata 0x5193a0 — approach lerp /frame */
#define CO_ANIM_COUNTER     4       /* the at-counter ready pose (engine anim 4) */

static void co_at_counter_tick(int32_t *rec, float *comp, const float *player)
{
    /* canim 4 — reset the sprite cycle on the transition in from free-roam
     * (co_set_anim is a no-op once held). */
    co_set_anim(rec, CO_ANIM_COUNTER);

    /* target a fixed offset on the side the companion already stands (engine
     * `comp.x <= player.x ? player.x-1.3 (oct 6) : player.x+1.3 (oct 2)`). */
    float target_x;
    if (comp[0] > player[0]) {
        target_x = player[0] + CO_COUNTER_OFFSET;
        rec[CHR_ACTOR_FACING] = 2;
    } else {
        target_x = player[0] - CO_COUNTER_OFFSET;
        rec[CHR_ACTOR_FACING] = 6;
    }
    float target_z = player[2];
    float bob_y = sinf((float)s_bob_counter * CO_BOB_FREQ) * CO_BOB_AMP
                  + CO_BOB_OFFSET;          /* no ground_y term (asm 0x48ad93) */

    float dx = target_x - comp[0];
    float dy = bob_y    - comp[1];
    float dz = target_z - comp[2];
    float dist = sqrtf(dx * dx + dz * dz);  /* horizontal only (dy excluded) */

    /* dist ≥ 2.0 → still walking in: walk anim 1 + octant from the approach
     * angle; else arrived → hold anim 4.  (The tutorial is < 2.0 from frame 1, so
     * the walk branch is faithful-but-unexercised here; it serves the autonomous-
     * customer stages.)  Note: while walking the engine re-sets 4 then 1 each
     * frame — co_set_anim reproduces that (resets the cycle every walk frame). */
    if (dist >= CO_COUNTER_ARRIVE) {
        co_set_anim(rec, CO_ANIM_MOVING);
        rec[CHR_ACTOR_FACING] =
            player_ctrl_facing_octant(atan2f(dx, dz), g_scene1_camera_yaw);
    } else {
        co_set_anim(rec, CO_ANIM_COUNTER);
    }

    /* approach the target at a flat 0.1/frame (engine `comp += delta·0.1`). */
    comp[0] += dx * CO_COUNTER_MOVE;
    comp[1] += dy * CO_COUNTER_MOVE;
    comp[2] += dz * CO_COUNTER_MOVE;
}

void scene1_companion_ctrl_tick(void)
{
    /* Engine path: FUN_0048a833 (the companion dispatcher) → FUN_0048a4d1 (the
     * spring-follow helper) for the free-roam case.  The dispatcher's other
     * branches (intro standing-pose, random-wander) are retail-only (gated
     * behind the unported §60 event-gate) — see FINDINGS.md.  The cc08==4
     * (sell-active) at-counter branch (co_at_counter_tick) is FUN_0048a833's own
     * `local_c != 0` arm — see below. */
    CALL_TRACE_ENTER(0x48a4d1u);

    if (player_ctrl_actor_char(CO_ACTOR) == -1)   /* companion not live */
        return;

    int32_t       *rec    = player_ctrl_actor_record_mut(CO_ACTOR);
    const int32_t *prec   = player_ctrl_actor_record(CO_TARGET);   /* player record */
    float         *comp   = g_scene1_actor_pos[CO_ACTOR];
    const float   *player = g_scene1_actor_pos[CO_TARGET];

    /* cc08==4 + f404 != 0 (a player-initiated counter SELL): the companion is the
     * haggle CUSTOMER — run the at-counter branch (FUN_0048a833 local_c!=0, forced
     * nonzero by f404 at by-address 0x48a98b) instead of the free-roam spring-follow.
     * The player arrival arm (player_ctrl_cs_arrival_tick, run earlier this frame
     * via scene1_player_ctrl_tick) wrote the companion octant = 0; this overwrites
     * it to the at-counter 2/6, exactly as retail (where FUN_0048a833 runs from the
     * master tick AFTER the arrival arm).  db054 is frozen (RE §8.8) so the
     * wing-sparkle gate (db054%4==0) still fires every frame; the at-counter
     * move/pose draws no rng.
     *
     * The AUTONOMOUS first customer (the f406 entry, gap 2) is cc08==4 but
     * f404 == 0: FUN_0048a833 then takes local_c==0 && local_28!=0 (f407 is set
     * only at the cs leave, all.c:60383, so it is 0 during the haggle) ⇒ the SAME
     * free-roam spring-follow (FUN_0048a4d1) the non-cc08 path below runs.  So gate
     * the at-counter arm on f404 and let f404==0 fall through to that path — retail's
     * Tear just follows the player to the counter (RE §18: canim 0, oct 0, cx≈-3.0).
     * The scold pose + 集中線 on the reaction beat are a SEPARATE live-machine
     * overlay (b534==6), ported later. */
    if (player_ctrl_cc08() == 4 && customer_service_f404()) {
        int prev_animsel = rec[CO_REC_ANIMSEL];
        co_at_counter_tick(rec, comp, player);
        /* advance the sprite anim on a non-transition frame (mirrors the
         * free-roam tail + the player); skip the frame canim transitioned in
         * (co_set_anim reset the cycle to frame 0). */
        if (rec[CO_REC_ANIMSEL] == prev_animsel)
            chr_anim_tick(rec, player_ctrl_actor_char(CO_ACTOR), 1.0f);
        /* wing-glow sparkle (FUN_0048a833 tail) — db054 frozen at 156 in cc08==4
         * ⇒ %4==0 emits every frame (the RE §8.8 rng-rate match). */
        if (s_bob_counter % CO_SPARKLE_PERIOD == 0)
            co_emit_wing_sparkle(rec, comp);
        return;
    }

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
     * shares the bob's db054 phase (the engine reads db054 once per frame).
     * The type-0x1f spawn consumes 6 rng_next_unit() draws; in the engine this
     * happens INSIDE FUN_0048b850 (FUN_0048a833 @ L90205) just BEFORE the foot
     * dust's 2 draws (L90215), so the port must tick the companion before the
     * dust too — see player_ctrl_b850_move() / engine-quirks §114. */
    if (s_bob_counter % CO_SPARKLE_PERIOD == 0)
        co_emit_wing_sparkle(rec, comp);
}

/* db054 phase-clock advance — the `DAT_056db054 = DAT_056db054 + 1` at the
 * FUN_0048b850 tail (all.c:90242), which the engine runs AFTER both the companion
 * update (FUN_0048a833, L90205) and the foot-dust emit (L90215) have read db054.
 * Split out of scene1_companion_ctrl_tick so the walk path can tick the companion
 * (its wing-sparkle RNG) BEFORE the dust yet still bump the counter AFTER it —
 * preserving the engine's read/emit/increment order (engine-quirks §114).
 *
 * db054 advances only on real free-roam frames: while the in-house display-stand
 * menu is open (cc04 != 0) the engine freezes it (FUN_0048670f keeps running + the
 * companion keeps ticking, but the walk arm that increments db054 is routed around
 * — engine-quirks §110), so gate the increment the same way. */
void scene1_companion_ctrl_advance_phase(void)
{
    if (player_ctrl_cc04() == 0)
        s_bob_counter++;
}

/* EVENT-ARM advance — the unconditional `DAT_056db054++` at the FUN_0048407f
 * tail (all.c:84658; see scene1_companion_ctrl.h).  Distinct from the cc04-
 * gated free-roam advance above: the event arm replaces the whole default arm
 * for the frame, so the menu freeze can never apply. */
void scene1_companion_ctrl_advance_phase_event(void)
{
    s_bob_counter++;
}
