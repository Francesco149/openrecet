/*
 * test_scene1_companion_ctrl.c — HOUSE companion (Tear / actor 2) controller.
 *
 * Exercises scene1_companion_ctrl_tick (engine FUN_0048a4d1 spring-follow):
 * the "stay 1.5 from the player" spring with 0.15 gain + 0.35 velocity clamp,
 * the fairy Y hover-bob, the moved→walk / still→idle anim, the facing-copies-
 * the-player rule, and the not-moving deadband (inside 1.5 → no XZ move).
 * Ground truth: runs/companion-truth/FINDINGS.md (engine-quirks §71).
 */
#include "t.h"

#include <math.h>
#include <stdint.h>

#include "scene1_chr_sprite.h"      /* CHR_ACTOR_* indices */
#include "scene1_player_ctrl.h"     /* pose seed + actor accessors */
#include "scene1_companion_ctrl.h"
#include "scene1_particles_tick.h"  /* g_scene1_actor_pos, ground_y, camera_yaw */
#include "scene1_spawn.h"           /* wing-glow emit → scene1_spawn trace */

static int near_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b));
}
#define T_NEAR(a, b) do { \
    float _a = (a), _b = (b); \
    if (!near_f(_a, _b)) T_FAIL("near: %g != %g", (double)_a, (double)_b); \
} while (0)

static void setup(float px, float pz)
{
    player_ctrl_pose_house_standing(0);   /* actor2 = char 1 @ (0.6,3.0,9.35) */
    scene1_companion_ctrl_reset();
    g_scene1_player_ground_y = 0.0f;
    g_scene1_actor_pos[0][0] = px;
    g_scene1_actor_pos[0][1] = 0.0f;
    g_scene1_actor_pos[0][2] = pz;
}

/* Inside the 1.5 follow radius the spring target == the companion's own
 * position, so it does not move (XZ), and reports idle. */
int test_companion_inside_radius_idle(void)
{
    setup(-0.3f, 9.35f);                   /* HOUSE spawn: player left of the seed */
    const int32_t *rec = player_ctrl_actor_record(2);
    float x0 = g_scene1_actor_pos[2][0], z0 = g_scene1_actor_pos[2][2];
    scene1_companion_ctrl_tick();
    T_NEAR(g_scene1_actor_pos[2][0], x0);  /* no XZ move within radius */
    T_NEAR(g_scene1_actor_pos[2][2], z0);
    if (rec[CHR_ACTOR_ANIM] != 0) T_FAIL("still → idle anim 0");
    /* idle side-rule facing: comp.x(0.6) > player.x(-0.3) → octant 2 (left). */
    if (rec[CHR_ACTOR_FACING] != 2) T_FAIL("idle facing should be side-rule 2");
    return 0;
}

/* Outside the radius the companion springs toward a point 1.5 from the player
 * (so it closes the gap), walks, and copies the player's facing octant. */
int test_companion_springs_and_copies_facing(void)
{
    setup(10.0f, 9.35f);                   /* player far in +x */
    int32_t *prec = player_ctrl_actor_record_mut(0);
    prec[CHR_ACTOR_FACING] = 3;            /* arbitrary player facing */
    const int32_t *rec = player_ctrl_actor_record(2);

    float x0 = g_scene1_actor_pos[2][0];
    scene1_companion_ctrl_tick();
    if (g_scene1_actor_pos[2][0] <= x0) T_FAIL("should spring toward player (+x)");
    if (rec[CHR_ACTOR_ANIM] != 1)  T_FAIL("moving → walk anim 1");
    if (rec[CHR_ACTOR_FACING] != 3) T_FAIL("moving → copy player facing (3)");

    /* one-step magnitude matches (desired−comp)·0.15 within the 0.35 clamp.
     * comp.x(0.6) far from player.x(10): dist 9.4 > 1.5 → desired.x = 10-1.5 =
     * 8.5; vel.x = (8.5-0.6)·0.15 = 1.185 → clamped to 0.35.  z unchanged. */
    T_NEAR(g_scene1_actor_pos[2][0], 0.6f + 0.35f);
    T_NEAR(g_scene1_actor_pos[2][2], 9.35f);
    return 0;
}

/* The Y hover-bob stays within [ground_y+2.8, ground_y+3.2] and is centred at
 * +3.0 when the sin phase is 0. */
int test_companion_hover_bob(void)
{
    setup(0.6f, 9.35f);
    T_NEAR(g_scene1_actor_pos[2][1], 3.0f);     /* seed = ground_y(0)+3.0 */
    scene1_companion_ctrl_tick();                /* counter 0 → bob target 3.0 */
    T_NEAR(g_scene1_actor_pos[2][1], 3.0f);
    for (int i = 0; i < 300; i++)
        scene1_companion_ctrl_tick();
    float y = g_scene1_actor_pos[2][1];
    if (y < 2.8f - 1e-3f || y > 3.2f + 1e-3f) T_FAIL("bob out of [2.8,3.2]: %g", (double)y);
    return 0;
}

/* The velocity clamp caps a single step at 0.35 regardless of how far the
 * player is. */
int test_companion_velocity_clamp(void)
{
    setup(100.0f, 9.35f);
    float x0 = g_scene1_actor_pos[2][0];
    scene1_companion_ctrl_tick();
    float step = g_scene1_actor_pos[2][0] - x0;
    T_NEAR(step, 0.35f);
    return 0;
}

/* Wing-glow sparkle (engine-quirks §73): on the first frame (bob counter 0) the
 * controller emits one type-0x1f particle just off the fairy, along her facing.
 * With the player inside the follow radius the fairy is stationary, so the spawn
 * position is exact: facing octant 2 (idle side-rule, comp.x>player.x), camera
 * yaw 0 → angle = 2·2π/8 = π/2 → spawn at
 *   (0.6 − sin(π/2)·0.6, 3.0 + 1.1, 9.35 − cos(π/2)·0.6) = (0.0, 4.1, 9.35). */
int test_companion_wing_sparkle_emit(void)
{
    setup(-0.3f, 9.35f);                    /* player left, inside 1.5 radius → idle */
    g_scene1_camera_yaw = 0.0f;
    scene1_spawn_trace_reset();

    scene1_companion_ctrl_tick();           /* counter 0 → emit */

    if (g_scene1_spawn_trace_count != 1) T_FAIL("expected exactly 1 emit, got %d",
                                                 g_scene1_spawn_trace_count);
    scene1_spawn_call_t e = g_scene1_spawn_trace[0];
    if (e.type != 0x1f)        T_FAIL("sparkle type should be 0x1f, got 0x%x", e.type);
    if (e.slot_hint != 0)      T_FAIL("sparkle slot_hint should be 0");
    T_NEAR(e.scale, 0.1f);                  /* recovered reused-push scale */
    T_NEAR(e.x, 0.0f);
    T_NEAR(e.y, 4.1f);
    T_NEAR(e.z, 9.35f);
    return 0;
}

/* The emit fires every 4th frame (db054 % 4 == 0), riding the bob counter: ticks
 * 0 and 4 emit, 1/2/3 don't.  Five ticks → two sparkles. */
int test_companion_wing_sparkle_period(void)
{
    setup(-0.3f, 9.35f);                    /* stationary fairy */
    g_scene1_camera_yaw = 0.0f;
    scene1_spawn_trace_reset();

    scene1_companion_ctrl_tick();           /* counter 0 → emit (1) */
    if (g_scene1_spawn_trace_count != 1) T_FAIL("frame 0 should emit");
    scene1_companion_ctrl_tick();           /* counter 1 → no */
    scene1_companion_ctrl_tick();           /* counter 2 → no */
    scene1_companion_ctrl_tick();           /* counter 3 → no */
    if (g_scene1_spawn_trace_count != 1) T_FAIL("frames 1-3 should not emit (got %d)",
                                                 g_scene1_spawn_trace_count);
    scene1_companion_ctrl_tick();           /* counter 4 → emit (2) */
    if (g_scene1_spawn_trace_count != 2) T_FAIL("frame 4 should emit (got %d)",
                                                 g_scene1_spawn_trace_count);
    return 0;
}
