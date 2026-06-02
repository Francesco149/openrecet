/*
 * test_scene1_conversation_pose.c — the iv1_2 face-to-face conversation pose
 * (engine FUN_0048407f conversation branch; scene1_conversation_pose.c).
 *
 * Exercises the pure scene1_conversation_pose_apply: the facing-each-other test
 * (Recette octant 2 / Tear 6 vs the mirror), the anim/state enter (Recette 6,
 * Tear 4) with its frame/timer/counter reset on transition + phase-hold once in
 * state, and the release-to-idle path.  Ground truth: docs/findings/
 * conversation-pose-driver.md (the FUN_0048407f branch table).
 */
#include "t.h"

#include <math.h>
#include <stdint.h>

#include "scene1_chr_sprite.h"        /* CHR_ACTOR_* indices */
#include "scene1_player_ctrl.h"       /* PC_ACTOR_REC_DWORDS */
#include "scene1_conversation_pose.h"

/* A sprite-state record with recognizable non-zero cycle fields, so a reset is
 * observable. */
static void seed_rec(int32_t rec[PC_ACTOR_REC_DWORDS], int anim, int state)
{
    for (int i = 0; i < PC_ACTOR_REC_DWORDS; i++)
        rec[i] = 0;
    rec[CHR_ACTOR_ANIM]    = anim;
    rec[CHR_ACTOR_STATE]   = state;
    rec[CHR_ACTOR_FRAME]   = 7;
    rec[CHR_ACTOR_COUNTER] = 9;
    union { float f; int32_t i; } t = { .f = 3.5f };
    rec[CHR_ACTOR_TIMER]   = t.i;
}

/* Tear to the LEFT of (or level with) Recette: Recette faces octant 2, Tear 6,
 * facing angle -π/2; both enter their pose anim/state, cycle reset. */
int test_conversation_pose_faces_tear_left(void)
{
    int32_t prec[PC_ACTOR_REC_DWORDS], crec[PC_ACTOR_REC_DWORDS];
    seed_rec(prec, 0, 0);
    seed_rec(crec, 0, 0);
    float angle = 0.0f;

    int r = scene1_conversation_pose_apply(prec, crec, &angle,
                                           /*player_x=*/2.0f, /*tear_x=*/-1.0f, 1);
    if (r != 1) T_FAIL("pose should be active");
    if (prec[CHR_ACTOR_FACING] != 2) T_FAIL("Recette faces octant 2 (Tear left)");
    if (crec[CHR_ACTOR_FACING] != 6) T_FAIL("Tear faces octant 6");
    if (angle > 0.0f) T_FAIL("angle should be -pi/2");
    if (fabsf(fabsf(angle) - CONV_POSE_FACE_ANGLE) > 1e-6f)
        T_FAIL("angle magnitude should be pi/2, got %g", (double)angle);

    if (prec[CHR_ACTOR_ANIM]  != CONV_POSE_PLAYER_ANIM) T_FAIL("Recette anim 6");
    if (prec[CHR_ACTOR_STATE] != CONV_POSE_PLAYER_ANIM) T_FAIL("Recette state 6");
    if (crec[CHR_ACTOR_ANIM]  != CONV_POSE_COMP_ANIM)   T_FAIL("Tear anim 4");
    if (crec[CHR_ACTOR_STATE] != CONV_POSE_COMP_ANIM)   T_FAIL("Tear state 4");
    /* transition into the state resets the anim cycle */
    if (prec[CHR_ACTOR_FRAME] != 0 || prec[CHR_ACTOR_COUNTER] != 0)
        T_FAIL("Recette cycle reset on enter");
    if (prec[CHR_ACTOR_TIMER] != 0) T_FAIL("Recette timer reset on enter");
    if (crec[CHR_ACTOR_FRAME] != 0 || crec[CHR_ACTOR_COUNTER] != 0)
        T_FAIL("Tear cycle reset on enter");
    return 0;
}

/* Tear to the RIGHT of Recette: the mirror — Recette octant 6, Tear 2,
 * angle +π/2. */
int test_conversation_pose_faces_tear_right(void)
{
    int32_t prec[PC_ACTOR_REC_DWORDS], crec[PC_ACTOR_REC_DWORDS];
    seed_rec(prec, 0, 0);
    seed_rec(crec, 0, 0);
    float angle = 0.0f;

    int r = scene1_conversation_pose_apply(prec, crec, &angle,
                                           /*player_x=*/-1.0f, /*tear_x=*/3.0f, 1);
    if (r != 1) T_FAIL("pose active");
    if (prec[CHR_ACTOR_FACING] != 6) T_FAIL("Recette faces octant 6 (Tear right)");
    if (crec[CHR_ACTOR_FACING] != 2) T_FAIL("Tear faces octant 2");
    if (angle < 0.0f) T_FAIL("angle should be +pi/2");
    if (fabsf(angle - CONV_POSE_FACE_ANGLE) > 1e-6f) T_FAIL("angle +pi/2");
    return 0;
}

/* Equal X takes the `tear_x <= player_x` branch (octant 2 / 6). */
int test_conversation_pose_equal_x_takes_left_branch(void)
{
    int32_t prec[PC_ACTOR_REC_DWORDS], crec[PC_ACTOR_REC_DWORDS];
    seed_rec(prec, 0, 0);
    seed_rec(crec, 0, 0);
    scene1_conversation_pose_apply(prec, crec, 0, 1.5f, 1.5f, 1);
    if (prec[CHR_ACTOR_FACING] != 2 || crec[CHR_ACTOR_FACING] != 6)
        T_FAIL("equal X → left branch (2/6)");
    return 0;
}

/* Already in the pose state: the facing is re-asserted but the anim cycle is
 * HELD (no frame/counter reset) so the blink keeps its phase frame-to-frame. */
int test_conversation_pose_holds_phase_in_state(void)
{
    int32_t prec[PC_ACTOR_REC_DWORDS], crec[PC_ACTOR_REC_DWORDS];
    seed_rec(prec, CONV_POSE_PLAYER_ANIM, CONV_POSE_PLAYER_ANIM);
    seed_rec(crec, CONV_POSE_COMP_ANIM, CONV_POSE_COMP_ANIM);
    prec[CHR_ACTOR_FRAME]   = 5;     /* mid-blink */
    prec[CHR_ACTOR_COUNTER] = 12;
    crec[CHR_ACTOR_FRAME]   = 3;

    scene1_conversation_pose_apply(prec, crec, 0, 2.0f, -1.0f, 1);

    if (prec[CHR_ACTOR_FRAME] != 5 || prec[CHR_ACTOR_COUNTER] != 12)
        T_FAIL("Recette cycle must be held when already in state 6");
    if (crec[CHR_ACTOR_FRAME] != 3)
        T_FAIL("Tear cycle must be held when already in state 4");
    /* facing is still set every frame */
    if (prec[CHR_ACTOR_FACING] != 2 || crec[CHR_ACTOR_FACING] != 6)
        T_FAIL("facing re-asserted each frame");
    return 0;
}

/* Release (flag set): both actors drop to idle anim/state 0, cycle reset;
 * facing left as-is (the freeroam controllers own it again). */
int test_conversation_pose_release_to_idle(void)
{
    int32_t prec[PC_ACTOR_REC_DWORDS], crec[PC_ACTOR_REC_DWORDS];
    seed_rec(prec, CONV_POSE_PLAYER_ANIM, CONV_POSE_PLAYER_ANIM);
    seed_rec(crec, CONV_POSE_COMP_ANIM, CONV_POSE_COMP_ANIM);
    prec[CHR_ACTOR_FACING] = 2;
    crec[CHR_ACTOR_FACING] = 6;

    int r = scene1_conversation_pose_apply(prec, crec, 0, 2.0f, -1.0f, 0);
    if (r != 0) T_FAIL("pose inactive on release");
    if (prec[CHR_ACTOR_ANIM] != 0 || prec[CHR_ACTOR_STATE] != 0)
        T_FAIL("Recette → idle 0");
    if (crec[CHR_ACTOR_ANIM] != 0 || crec[CHR_ACTOR_STATE] != 0)
        T_FAIL("Tear → idle 0");
    if (prec[CHR_ACTOR_FRAME] != 0 || crec[CHR_ACTOR_FRAME] != 0)
        T_FAIL("cycle reset on release");
    /* facing untouched by the release branch */
    if (prec[CHR_ACTOR_FACING] != 2 || crec[CHR_ACTOR_FACING] != 6)
        T_FAIL("release must not touch facing");
    return 0;
}

/* Release when already idle is a no-op (no spurious cycle reset). */
int test_conversation_pose_release_idempotent(void)
{
    int32_t prec[PC_ACTOR_REC_DWORDS], crec[PC_ACTOR_REC_DWORDS];
    seed_rec(prec, 0, 0);
    seed_rec(crec, 0, 0);
    prec[CHR_ACTOR_FRAME] = 4;       /* mid free-roam idle cycle */
    scene1_conversation_pose_apply(prec, crec, 0, 0.0f, 0.0f, 0);
    if (prec[CHR_ACTOR_FRAME] != 4)
        T_FAIL("idle actor must not be reset by a redundant release");
    return 0;
}

/* NULL records / angle pointer are tolerated (dead actor, no angle sink). */
int test_conversation_pose_null_safe(void)
{
    if (scene1_conversation_pose_apply(0, 0, 0, 1.0f, 2.0f, 1) != 1)
        T_FAIL("null-safe pose still reports active");
    if (scene1_conversation_pose_apply(0, 0, 0, 1.0f, 2.0f, 0) != 0)
        T_FAIL("null-safe release reports inactive");
    return 0;
}
