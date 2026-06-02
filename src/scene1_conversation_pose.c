/*
 * scene1_conversation_pose.c — see the header banner + the RE/port spec in
 * docs/findings/conversation-pose-driver.md.  Ports the conversation branch of
 * FUN_0048407f (0x48407f) plus the per-actor anim step it drives.
 */

#include "scene1_conversation_pose.h"

#include "scene1_chr_sprite.h"      /* CHR_ACTOR_* indices + chr_anim_tick */
#include "scene1_player_ctrl.h"     /* actor record (mut/read) + facing setter */
#include "scene1_particles_tick.h"  /* g_scene1_actor_pos + g_scene1_camera_yaw_alt */
#include "scene1_intro_dialogue.h"  /* iv1_2 lifecycle → the talk-event flag */
#include "call_trace.h"             /* CALL_TRACE_ENTER */

/* Enter / hold one actor's pose state (engine FUN_0048407f's
 * `if (state != id) { reset frame/timer/counter; anim = id; state = id; }`
 * block — the same shape for the player anim 6, Tear anim 4, and the idle
 * release anim 0).  No-op once already in the state, so the anim cycle keeps
 * its phase between frames (the per-frame chr_anim_tick advances it). */
static void conv_pose_enter(int32_t *rec, int id)
{
    if (rec[CHR_ACTOR_STATE] == id)
        return;
    union { float f; int32_t i; } z = { .f = 0.0f };
    rec[CHR_ACTOR_FRAME]   = 0;
    rec[CHR_ACTOR_TIMER]   = z.i;
    rec[CHR_ACTOR_COUNTER] = 0;
    rec[CHR_ACTOR_ANIM]    = id;
    rec[CHR_ACTOR_STATE]   = id;
}

int scene1_conversation_pose_apply(int32_t *player_rec, int32_t *comp_rec,
                                   float *player_facing_angle,
                                   float player_x, float tear_x,
                                   int talk_flag_clear)
{
    if (!talk_flag_clear) {
        /* flag set → free-roam: release both actors to idle (engine else-branch,
         * checked per-actor against state 0).  Facing untouched. */
        if (player_rec) conv_pose_enter(player_rec, 0);
        if (comp_rec)   conv_pose_enter(comp_rec, 0);
        return 0;
    }

    /* flag clear → conversation pose.  Face each other on the X axis. */
    if (tear_x <= player_x) {
        if (player_facing_angle) *player_facing_angle = -CONV_POSE_FACE_ANGLE;
        if (player_rec) player_rec[CHR_ACTOR_FACING] = 2;
        if (comp_rec)   comp_rec[CHR_ACTOR_FACING]   = 6;
    } else {
        if (player_facing_angle) *player_facing_angle = CONV_POSE_FACE_ANGLE;
        if (player_rec) player_rec[CHR_ACTOR_FACING] = 6;
        if (comp_rec)   comp_rec[CHR_ACTOR_FACING]   = 2;
    }
    if (player_rec) conv_pose_enter(player_rec, CONV_POSE_PLAYER_ANIM);
    if (comp_rec)   conv_pose_enter(comp_rec, CONV_POSE_COMP_ANIM);
    return 1;
}

/* Latch: 1 while the pose is held; the release frame clears it so the actors
 * are handed back to the freeroam controllers exactly once. */
static int s_pose_active = 0;

void scene1_conversation_pose_reset(void)
{
    s_pose_active = 0;
}

int scene1_conversation_pose_active(void)
{
    return s_pose_active;
}

int scene1_conversation_pose_player_state(void)
{
    if (player_ctrl_actor_char(0) == -1)
        return 0;
    const int32_t *rec = player_ctrl_actor_record(0);
    return rec ? rec[CHR_ACTOR_STATE] : 0;
}

int scene1_conversation_pose_player_blink(void)
{
    if (player_ctrl_actor_char(0) == -1)
        return 0;
    const int32_t *rec = player_ctrl_actor_record(0);
    if (!rec || rec[CHR_ACTOR_STATE] != CONV_POSE_PLAYER_ANIM)
        return 0;
    /* Recette anim 6 frame loop = cells [38,39,38,39] (durations 20/6/32/6), so
     * an ODD frame index is cell 39 — eyes closed, i.e. mid-blink. */
    return (rec[CHR_ACTOR_FRAME] & 1) ? 1 : 0;
}

void scene1_conversation_pose_tick(void)
{
    CALL_TRACE_ENTER(0x48407fu);

    /* The talk-event flag (DAT_0450f470 == 0 → pose).  PORT-DEBT: the faithful
     * producer is the intro event timeline FUN_00470a46 (clears the flag at the
     * end of the deferred shatter transition) + FUN_004852fb (sets it on
     * scene-out).  Until that render-entangled path is ported, derive the flag
     * from the iv1_2 dialogue lifecycle: the conversation pose is the iv1_2
     * (2nd-script, generation >= 2) window, which is exactly when retail holds
     * it.  scene1_intro_dialogue tracks both edges (active goes false when the
     * script finishes → the release). */
    int iv2 = scene1_intro_dialogue_active()
              && scene1_intro_dialogue_generation() >= 2u;

    /* Inert outside the pose window (and once released): the freeroam
     * controllers own the actors — don't fight them every frame. */
    if (!iv2 && !s_pose_active)
        return;

    if (player_ctrl_actor_char(0) == -1)   /* no live player (pre-HOUSE) */
        return;

    int32_t *player_rec = player_ctrl_actor_record_mut(0);
    int32_t *comp_rec   = (player_ctrl_actor_char(2) != -1)
                          ? player_ctrl_actor_record_mut(2) : 0;

    float angle = 0.0f;
    int active = scene1_conversation_pose_apply(player_rec, comp_rec, &angle,
                                                g_scene1_actor_pos[0][0],
                                                g_scene1_actor_pos[2][0], iv2);

    if (active) {
        /* DAT_056db05c is one engine global the port mirrors as both the player
         * facing angle (scene1_player_ctrl) and g_scene1_camera_yaw_alt (the
         * particle/spawn yaw); keep both in step with the engine's write. */
        player_ctrl_set_facing_angle(angle);
        g_scene1_camera_yaw_alt = angle;

        /* FUN_0048407f's per-actor anim step (the per-frame FUN_00482a71 loop):
         * advance each posed actor one frame so Recette's blink (38↔39) and
         * Tear's talk pose animate.  conv_pose_enter reset the cycle on the
         * frame it entered the state; the engine ticks that same frame too. */
        chr_anim_tick(player_rec, player_ctrl_actor_char(0), 1.0f);
        if (comp_rec)
            chr_anim_tick(comp_rec, player_ctrl_actor_char(2), 1.0f);
    }

    s_pose_active = active;
}
