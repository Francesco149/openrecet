/*
 * test_scene1_player_ctrl.c — the house controller's pure pieces: the facing
 * octant, the room clamp FUN_00486435, the actor-state model, the door / emote
 * / world-map exit arms.  The walk itself (FUN_0048b850, dungeon_party.c) is
 * checked against retail under rhost (portstate / portrng), not here.
 */
#include "t.h"

#include <math.h>
#include <stdint.h>

#include "scene1_chr_sprite.h"   /* CHR_ACTOR_* record-field indices */
#include "chr_sprite_meta.h"     /* chr_meta_alloc/block, CHR_META_OFF_LUT, ANIM_END */
#include "scene1_player_ctrl.h"
#include "input.h"               /* g_input_state[].buttons */
#include "scene1_particles_tick.h" /* g_scene1_player_pos[3] */
#include "title_save_dialog.h"   /* title_save_dialog_reset — clear the FUN_0048670f
                                  * prologue save-gate (FUN_00434d6a) between tests */
#include "fade.h"                /* g_fade_phase / fade_tick / fade_is_done — T1 door fade */
#include "scene.h"               /* g_scene_state — T1 mode-8 transition */
#include "worker_load.h"         /* worker_load_reset — T1 stage-2 cleanup */

static int near_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b));
}
#define T_ASSERT_NEAR(a, b) do { \
    float _a = (a), _b = (b); \
    if (!near_f(_a, _b)) \
        T_FAIL("expected %s ≈ %s (got %.7f, want %.7f)", #a, #b, _a, _b); \
} while (0)

/* ── the facing octant and the room clamp ─────────────────────────────────── */

int test_player_ctrl_facing_octant_cardinals(void)
{
    /* house camera yaw π: idle +π/2 → 6, LEFT −π/2 → 2, DOWN 0 → 4, UP π → 0. */
    const float yaw = 3.14159265f;
    if (player_ctrl_facing_octant( 1.57079633f, yaw) != 6) T_FAIL("RIGHT oct 6");
    if (player_ctrl_facing_octant(-1.57079633f, yaw) != 2) T_FAIL("LEFT oct 2");
    if (player_ctrl_facing_octant( 0.0f,        yaw) != 4) T_FAIL("DOWN oct 4");
    if (player_ctrl_facing_octant( 3.14159265f, yaw) != 0) T_FAIL("UP oct 0");
    return 0;
}

int test_player_ctrl_house_room_clamp(void)
{
    float px, pz;
    px = -1.625f; pz = 9.35f;                    /* past the left wall, pz>7 */
    player_ctrl_house_room_clamp(&px, &pz);
    T_ASSERT_NEAR(px, -1.5f);
    px = -3.0f; pz = 5.0f;                        /* pz<7 → no px stop */
    player_ctrl_house_room_clamp(&px, &pz);
    T_ASSERT_NEAR(px, -3.0f);
    px = 0.0f; pz = 12.0f;                        /* pz clamps to 9.5 */
    player_ctrl_house_room_clamp(&px, &pz);
    T_ASSERT_NEAR(pz, 9.5f);
    return 0;
}

/* ── Cchr.2h: house-standing actor-state model ───────────────────────────── */

int test_player_pose_seeds_actor0(void)
{
    /* Fresh set-anim reset at HOUSE entry (RE §21.30): char 0, scale 1/1,
     * record anim 0 / timer 0 / counter 0 / frame 0 / facing 6 — retail's
     * entry write (the old counter-25/frame-2/timer-5 seed was a later
     * steady-state leaf snapshot, viewer note #24). */
    player_ctrl_pose_house_standing(0);

    if (player_ctrl_actor_char(0) != 0) T_FAIL("actor0 char should be 0");
    T_ASSERT_NEAR(player_ctrl_actor_scale_xz(0), 1.0f);
    T_ASSERT_NEAR(player_ctrl_actor_scale_y(0), 1.0f);

    const int32_t *rec = player_ctrl_actor_record(0);
    if (rec == NULL) T_FAIL("actor0 record should be non-NULL");
    if (rec[CHR_ACTOR_ANIM]    != 0)  T_FAIL("anim should be 0");
    if (rec[CHR_ACTOR_COUNTER] != 0)  T_FAIL("counter should be 0");
    if (rec[CHR_ACTOR_FRAME]   != 0)  T_FAIL("frame should be 0");
    if (rec[CHR_ACTOR_FACING]  != 6)  T_FAIL("facing should be 6");
    if (rec[CHR_ACTOR_TIMER]   != 0)  T_FAIL("timer should be 0 (0.0f bits)");
    return 0;
}

int test_player_pose_seeds_companion_actor2(void)
{
    player_ctrl_pose_house_standing(0);
    /* HOUSE free-roam: actor 1 (guest, char 3) is DISABLED (char -1, the engine
     * FUN_00436f97 free-roam path); actor 2 is the live companion fairy
     * (char id 1, scale 1.0) — engine-quirks §71 / runs/companion-truth. */
    if (player_ctrl_actor_char(1) != -1) T_FAIL("actor1 (guest) char should be -1");
    T_ASSERT_NEAR(player_ctrl_actor_scale_xz(1), 0.0f);

    if (player_ctrl_actor_char(2) != 1)  T_FAIL("actor2 (companion) char should be 1");
    T_ASSERT_NEAR(player_ctrl_actor_scale_xz(2), 1.0f);
    T_ASSERT_NEAR(player_ctrl_actor_scale_y(2),  1.0f);
    return 0;
}

int test_player_pose_passes_char_id(void)
{
    /* the char id is whatever DAT_056da1cc holds (the wrapper passes it). */
    player_ctrl_pose_house_standing(7);
    if (player_ctrl_actor_char(0) != 7) T_FAIL("actor0 char should track arg");
    return 0;
}

int test_player_actor_accessors_out_of_range(void)
{
    player_ctrl_pose_house_standing(0);
    if (player_ctrl_actor_char(-1) != -1)  T_FAIL("oob -1 → -1");
    if (player_ctrl_actor_char(3)  != -1)  T_FAIL("oob 3 → -1");
    if (player_ctrl_actor_record(3) != NULL) T_FAIL("oob record → NULL");
    T_ASSERT_NEAR(player_ctrl_actor_scale_xz(99), 0.0f);
    return 0;
}

/* ── Chip 2: the FUN_0048b850-tail render banks the chr-walker reads ─────── */

int test_player_render_bank_accessor_contract(void)
{
    /* Two banks of PC_TRAIL_RECORDS / PC_BURST_RECORDS slots; out-of-range
     * sweep or index → NULL.  Reset leaves every slot dormant (life == 0) and
     * the burst counter zero, so the walker iterates and draws nothing. */
    player_ctrl_pose_house_standing(0);

    if (player_ctrl_render_bank_slot(2, 0)  != NULL) T_FAIL("bad sweep → NULL");
    if (player_ctrl_render_bank_slot(0, -1) != NULL) T_FAIL("idx -1 → NULL");
    if (player_ctrl_render_bank_slot(0, PC_TRAIL_RECORDS) != NULL) T_FAIL("trail oob → NULL");
    if (player_ctrl_render_bank_slot(1, PC_BURST_RECORDS) != NULL) T_FAIL("burst oob → NULL");
    if (player_ctrl_burst_count() != 0) T_FAIL("burst counter starts 0");

    for (int sweep = 0; sweep <= 1; sweep++) {
        int n = sweep ? PC_BURST_RECORDS : PC_TRAIL_RECORDS;
        for (int i = 0; i < n; i++) {
            const int32_t *slot = player_ctrl_render_bank_slot(sweep, i);
            if (slot == NULL) T_FAIL("in-range slot should be non-NULL");
            if (slot[14] != 0) T_FAIL("reset slot life must be 0 (dormant)");
        }
    }
    return 0;
}

/* ── T1: shop-door exit → world map (engine bVar17 + the all.c:86877 stage-2) ── */

int test_player_door_predicate_position_facing(void)
{
    /* scene type 0: within 1.8 of (3, -3.4), x past 2.895, facing ≈ +π/2, not
     * exited → v59 (FUN_0048670f 0x488616). */
    if (!player_ctrl_at_shop_door(3.0f, -3.4f, 1.5707964f, 0, 0))
        T_FAIL("(3, -3.4) facing π/2 not-exited should be AT the door");
    /* facing inside the ±0.1π band still counts. */
    if (!player_ctrl_at_shop_door(2.90f, -3.4f, 1.5707964f + 0.30f, 0, 0))
        T_FAIL("facing within +0.1π of the door dir should count");
    if (!player_ctrl_at_shop_door(2.90f, -3.4f, 1.5707964f - 0.30f, 0, 0))
        T_FAIL("facing within -0.1π of the door dir should count");
    /* scene types 1..4: the door at (6, -6.7) facing -π, edge z < -6.7. */
    if (!player_ctrl_at_shop_door(6.0f, -7.0f, -3.1415927f, 2, 0))
        T_FAIL("scene type 2: (6, -7) facing -π should be AT the door");
    if (player_ctrl_at_shop_door(6.0f, -6.6f, -3.1415927f, 2, 0))
        T_FAIL("scene type 2: z -6.6 is not past the -6.7 edge");
    return 0;
}

int test_player_door_predicate_rejects_already_exited(void)
{
    /* the DAT_0450f3f7 "already exited" guard blocks re-triggering. */
    if (player_ctrl_at_shop_door(3.0f, -3.4f, 1.5707964f, 0, 1))
        T_FAIL("already-exited guard must block the door");
    return 0;
}

int test_player_door_predicate_rejects_wrong_facing(void)
{
    /* facing 0 (not toward the door) is outside the ±0.1π band. */
    if (player_ctrl_at_shop_door(3.0f, -3.4f, 0.0f, 0, 0))
        T_FAIL("facing 0 (outside ±0.1π of +π/2) must not trigger");
    /* just past the band edge (0.1π ≈ 0.31416). */
    if (player_ctrl_at_shop_door(3.0f, -3.4f, 1.5707964f + 0.33f, 0, 0))
        T_FAIL("facing past the +0.1π band must not trigger");
    return 0;
}

int test_player_door_predicate_rejects_inside(void)
{
    /* X not past the door edge (2.895) → not at the door, even facing it. */
    if (player_ctrl_at_shop_door(0.0f, -3.4f, 1.5707964f, 0, 0))
        T_FAIL("player X=0 (not past 2.895) must not be at the door");
    if (player_ctrl_at_shop_door(2.895f, -3.4f, 1.5707964f, 0, 0))
        T_FAIL("player X exactly 2.895 is not strictly past the edge");
    /* past the edge but 1.8 or more from the door point. */
    if (player_ctrl_at_shop_door(3.0f, -1.5f, 1.5707964f, 0, 0))
        T_FAIL("1.9 from the door point must not be at the door");
    return 0;
}

/* ── the "GO!" door tooltip ramp (db000/db004, all.c:87591-87596) ── */

int test_player_emote_ramp_up_to_cap(void)
{
    /* at the door: type set to the affordance cell, level ramps 0→10 then caps. */
    int level = 0, type = 0;
    for (int i = 0; i < 15; i++)
        player_ctrl_emote_ramp_step(1, 7, &level, &type);
    if (type != 7)
        T_FAIL("at-door must set db004=7 (got %d)", type);
    if (level != 10)
        T_FAIL("level must ramp up to the cap 10 (got %d)", level);
    return 0;
}

int test_player_emote_ramp_one_per_frame(void)
{
    /* one step per frame: level 0→1→2 (the sin slide-in reads this gauge). */
    int level = 0, type = 0;
    player_ctrl_emote_ramp_step(1, 7, &level, &type);
    if (level != 1) T_FAIL("first step must give level 1 (got %d)", level);
    player_ctrl_emote_ramp_step(1, 7, &level, &type);
    if (level != 2) T_FAIL("second step must give level 2 (got %d)", level);
    return 0;
}

int test_player_emote_ramp_down_to_floor(void)
{
    /* off the door: level ramps back down to 0 and floors (the bubble slides out). */
    int level = 10, type = 7;
    for (int i = 0; i < 15; i++)
        player_ctrl_emote_ramp_step(0, 7, &level, &type);
    if (level != 0)
        T_FAIL("level must ramp down to the floor 0 (got %d)", level);
    /* db004 is NOT cleared on the way down — the cell holds while it slides out. */
    if (type != 7)
        T_FAIL("type must hold its cell on the slide-out (got %d)", type);
    return 0;
}

int test_player_emote_ramp_idle_off_door_noop(void)
{
    /* off the door with level already 0: a no-op (no underflow). */
    int level = 0, type = 0;
    player_ctrl_emote_ramp_step(0, 7, &level, &type);
    if (level != 0) T_FAIL("off-door at level 0 must stay 0 (got %d)", level);
    if (type != 0)  T_FAIL("off-door must not write db004 (got %d)", type);
    return 0;
}

int test_player_worldmap_exit_arm_sets_flags_and_fade(void)
{
    /* arm (all.c:87637-87648): sets DAT_074b2ec4 and kicks the phase-1 dissolve
     * fade (FUN_004526f5 → counter=1, phase=1, duration=0x11). */
    player_ctrl_worldmap_exit_reset();
    fade_reset();
    player_ctrl_worldmap_exit_arm();
    if (!player_ctrl_worldmap_exit_armed())
        T_FAIL("arm must set the exit-armed flag (DAT_074b2ec4)");
    if (g_fade_phase != 1)
        T_FAIL("arm must kick the phase-1 dissolve fade (g_fade_phase=%d)",
               g_fade_phase);
    if (g_fade_duration != 0x11)
        T_FAIL("door fade duration must be 0x11 (got %d)", g_fade_duration);
    player_ctrl_worldmap_exit_reset();
    fade_reset();
    return 0;
}

int test_player_worldmap_exit_stage2_transitions_on_fade_done(void)
{
    /* stage 2 (all.c:86877-86888): frozen while the dissolve runs; on completion
     * flips to mode 8 (the world map) + disarms.  The 16-frame fade (counter
     * 1→0x11) matches the recording's door-Z@409 → LOADING_START@425 gap. */
    player_ctrl_worldmap_exit_reset();
    fade_reset();
    worker_load_reset();
    g_scene_state = SCENE_STATE_INGAME;   /* in the shop */

    player_ctrl_worldmap_exit_arm();      /* arm + start the dissolve */

    /* mid-dissolve: stage 2 freezes but does NOT transition. */
    if (!player_ctrl_worldmap_exit_stage2())
        T_FAIL("stage2 must report frozen while armed");
    if (g_scene_state != SCENE_STATE_INGAME)
        T_FAIL("must not leave the shop before the dissolve completes");
    if (!player_ctrl_worldmap_exit_armed())
        T_FAIL("must stay armed until the dissolve completes");

    /* drive the dissolve to completion. */
    int ticks = 0;
    for (; ticks < 40 && !fade_is_done(); ticks++)
        fade_tick();
    if (!fade_is_done())
        T_FAIL("dissolve did not complete");
    if (ticks != 0x10)   /* counter 1→0x11 = 16 ticks → the 16-frame gap */
        T_FAIL("dissolve should take 16 ticks (got %d)", ticks);

    if (!player_ctrl_worldmap_exit_stage2())
        T_FAIL("stage2 still consumes the transition frame");
    if (g_scene_state != 8)
        T_FAIL("dissolve-done must flip to mode 8 / world map (got %d)",
               g_scene_state);
    if (player_ctrl_worldmap_exit_armed())
        T_FAIL("must disarm after the transition");

    /* cleanup the shared scene/fade/worker state for later tests. */
    g_scene_state = SCENE_STATE_INGAME;
    fade_reset();
    worker_load_reset();
    return 0;
}
