/*
 * scene1_conversation_pose.{c,h} — the iv1_2 face-to-face conversation pose.
 *
 * Port of FUN_0048407f's conversation branch (the master event-actor tick,
 * 0x48407f, all.c:84547-84659).  During the iv1_2 opening conversation — after
 * the fade-from-black, while Tear slides in — retail poses the two HOUSE
 * freeroam chibis: Recette plays anim 6 (「ティアの話を聞くよ」, *listening*:
 * a look-up at Tear with a blink loop, cells 38↔39) and Tear plays anim 4
 * (her talking pose), each turned to face the other on the X axis.  The port
 * draws both actors but, with its freeroam controllers gated off during the
 * dialogue, left them frozen in idle — the `intro-iv2-gap` gap.  See
 * docs/findings/conversation-pose-driver.md for the full RE + port spec.
 *
 * The engine drives this via the talk-event flag DAT_0450f470[save] (one byte
 * per save slot, BSS-zero): 0 → hold the conversation pose, non-0 → free-roam.
 * Its faithful producer is the new-game intro event timeline FUN_00470a46
 * (it clears the flag at the end of the deferred shatter transition), with
 * FUN_004852fb setting it on the scene-out transition.  That producer is
 * entangled with the still-deferred shatter-transition render, so the port
 * derives the flag from the iv1_2 dialogue lifecycle instead (see the .c
 * PORT-DEBT note); the pose math below is faithful regardless.
 */
#ifndef OPENRECET_SCENE1_CONVERSATION_POSE_H
#define OPENRECET_SCENE1_CONVERSATION_POSE_H

#include <stdint.h>

/* The anim ids = state values FUN_0048407f's conversation branch sets (anim id
 * and the CHR_ACTOR_STATE field carry the same number in this branch). */
#define CONV_POSE_PLAYER_ANIM  6   /* Recette: look-up-at-Tear + blink (cells 38/39) */
#define CONV_POSE_COMP_ANIM    4   /* Tear: conversing-with-Recette talk pose         */

/* The engine's exact DAT_056db05c facing-angle writes: ±π/2 (0x3fc90fdb /
 * 0xbfc90fdb).  The float literal rounds to the positive bit pattern. */
#define CONV_POSE_FACE_ANGLE   1.5707964f

/*
 * Apply the conversation branch to the two actor sprite-state records (engine
 * FUN_0048407f L84549-84588).  Pure: no engine globals or callees.
 *
 *   talk_flag_clear != 0  → hold the pose (engine flag DAT_0450f470 == 0):
 *       face each other on X — if tear_x <= player_x, Recette faces octant 2 /
 *       Tear octant 6 and *player_facing_angle = -π/2; else 6 / 2 and +π/2 —
 *       then enter Recette anim/state 6 and Tear anim/state 4 (resetting the
 *       frame/timer/counter on the transition INTO the state, like the engine).
 *   talk_flag_clear == 0  → release both actors to idle anim/state 0 (engine
 *       flag != 0 branch); facing is left untouched (the freeroam controllers
 *       own it again).  The engine also advances its talk-manager FUN_00470970
 *       here — a render-layer effect, deferred.
 *
 * player_rec / comp_rec: >= PC_ACTOR_REC_DWORDS dwords each, mutated in place;
 *   either may be NULL (a dead/absent actor) and is then skipped.
 * player_facing_angle: the engine DAT_056db05c write; receives ±π/2 only in the
 *   pose branch.  May be NULL.
 * player_x / tear_x: actor 0 / actor 2 world X (DAT_056da1d8 / _DAT_056da1f0).
 *
 * Returns 1 while the pose is held (talk_flag_clear != 0), else 0.
 */
int scene1_conversation_pose_apply(int32_t *player_rec, int32_t *comp_rec,
                                   float *player_facing_angle,
                                   float player_x, float tear_x,
                                   int talk_flag_clear);

/*
 * Per-frame conversation-pose tick — call at the TOP of the INGAME default arm
 * (scene1_ingame_default_arm_tick), mirroring FUN_0048407f's position before the
 * per-actor anim step + the companion spring-follow.  While the iv1_2
 * conversation is the active script it holds the pose on the live actor records
 * and advances each posed actor's sprite animation one frame (the engine's
 * per-actor FUN_00482a71 loop) — so Recette's blink and Tear's talk pose
 * animate even though the freeroam controllers are gated off for the dialogue.
 * The frame the conversation ends it issues one release (actors → idle), then
 * goes inert and hands the actors back to the freeroam controllers.
 */
void scene1_conversation_pose_tick(void);

/* 1 while the pose tick is holding the conversation pose this frame.  The
 * companion controller reads this to suppress its own anim/facing selection
 * (the pose owns them); 0 in free-roam. */
int  scene1_conversation_pose_active(void);

/* Return to dormant (scene change / new game). */
void scene1_conversation_pose_reset(void);

#endif /* OPENRECET_SCENE1_CONVERSATION_POSE_H */
