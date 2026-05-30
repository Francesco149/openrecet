/*
 * test_scene1_player_ctrl.c — Cpop.1-8 coverage.
 *
 * Exercises the pure leaf math of the HOUSE per-frame player controller
 * (engine FUN_0048b850): the 8→4 facing-octant snap, camera zoom/shake
 * decay + clamp, emote-pulse counters, the motion-history rings, the
 * dash-trail + after-image burst fills, and the HP/SP gauge tween
 * (FUN_0048b6ad, the controller's first callee).  The stateful controller
 * body itself is dormant (its caller chain FUN_00442cef→FUN_0048670f is
 * unported) and not host-testable; these leaves are where the decoded
 * branch structure lives.
 */
#include "t.h"

#include <math.h>
#include <stdint.h>

#include "scene1_chr_sprite.h"   /* CHR_ACTOR_* record-field indices */
#include "scene1_player_ctrl.h"
#include "input.h"               /* g_input_state[].buttons */
#include "scene1_particles_tick.h" /* g_scene1_player_pos[3] */

static int near_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b));
}
#define T_ASSERT_NEAR(a, b) do { \
    float _a = (a), _b = (b); \
    if (!near_f(_a, _b)) \
        T_FAIL("expected %s ≈ %s (got %.7f, want %.7f)", #a, #b, _a, _b); \
} while (0)

/* ── player_ctrl_facing_snap ─────────────────────────────────────────────── */

int test_player_facing_snap_horizontal_sets_sticky(void)
{
    int s = 0;
    /* octant 2 passes through AND sets the sticky bias to 1. */
    if (player_ctrl_facing_snap(2, &s) != 2) T_FAIL("oct2 should stay 2");
    if (s != 1) T_FAIL("oct2 should set sticky=1");
    s = 0;
    if (player_ctrl_facing_snap(6, &s) != 6) T_FAIL("oct6 should stay 6");
    if (s != 1) T_FAIL("oct6 should set sticky=1");
    return 0;
}

int test_player_facing_snap_vertical_clears_sticky(void)
{
    int s = 1;
    /* octant 0 / 4 pass through AND clear the sticky bias to 0. */
    if (player_ctrl_facing_snap(0, &s) != 0) T_FAIL("oct0 should stay 0");
    if (s != 0) T_FAIL("oct0 should clear sticky");
    s = 1;
    if (player_ctrl_facing_snap(4, &s) != 4) T_FAIL("oct4 should stay 4");
    if (s != 0) T_FAIL("oct4 should clear sticky");
    return 0;
}

int test_player_facing_snap_diagonals_sticky0(void)
{
    /* sticky=0 (right/down... vertical-biased): {1,7}→0, {3,5}→4. */
    int s = 0; if (player_ctrl_facing_snap(1, &s) != 0) T_FAIL("1→0");
    s = 0;     if (player_ctrl_facing_snap(7, &s) != 0) T_FAIL("7→0");
    s = 0;     if (player_ctrl_facing_snap(3, &s) != 4) T_FAIL("3→4");
    s = 0;     if (player_ctrl_facing_snap(5, &s) != 4) T_FAIL("5→4");
    /* diagonals must NOT change the sticky bit. */
    s = 0;     player_ctrl_facing_snap(1, &s);
    if (s != 0) T_FAIL("diagonal must not touch sticky");
    return 0;
}

int test_player_facing_snap_diagonals_sticky1(void)
{
    /* sticky=1 (horizontal-biased): {1,3}→2, {5,7}→6. */
    int s = 1; if (player_ctrl_facing_snap(1, &s) != 2) T_FAIL("1→2");
    s = 1;     if (player_ctrl_facing_snap(3, &s) != 2) T_FAIL("3→2");
    s = 1;     if (player_ctrl_facing_snap(5, &s) != 6) T_FAIL("5→6");
    s = 1;     if (player_ctrl_facing_snap(7, &s) != 6) T_FAIL("7→6");
    s = 1;     player_ctrl_facing_snap(5, &s);
    if (s != 1) T_FAIL("diagonal must not touch sticky");
    return 0;
}

int test_player_facing_snap_masks_input(void)
{
    /* high bits are masked off (& 7); 0xA == 2. */
    int s = 0;
    if (player_ctrl_facing_snap(0xA, &s) != 2) T_FAIL("0xA & 7 == 2");
    if (s != 1) T_FAIL("0xA→oct2 sets sticky");
    return 0;
}

/* ── player_ctrl_camera_z_decay ──────────────────────────────────────────── */

int test_player_cam_z_decay_step(void)
{
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(0.0f), -0.03f);
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(1.0f), 0.97f);
    return 0;
}

int test_player_cam_z_decay_floor(void)
{
    /* already at/below the -2.0 floor → clamped, never runs away. */
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(-1.99f), -2.0f);
    T_ASSERT_NEAR(player_ctrl_camera_z_decay(-5.0f),  -2.0f);
    return 0;
}

/* ── player_ctrl_camera_shake_clamp ──────────────────────────────────────── */

int test_player_cam_shake_below_target_noop(void)
{
    /* magnitude (0.3) < target (0.5) → untouched (engine guard target<=mag). */
    float x = 0.3f, y = 0.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 0.5f);
    T_ASSERT_NEAR(x, 0.3f);
    T_ASSERT_NEAR(y, 0.0f);
    return 0;
}

int test_player_cam_shake_above_target_scaled(void)
{
    /* magnitude (5.0 along x) > target (1.0) → scaled to length 1.0. */
    float x = 5.0f, y = 0.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 1.0f);
    T_ASSERT_NEAR(x, 1.0f);
    T_ASSERT_NEAR(y, 0.0f);

    /* 3-4-5 triangle: mag 5, target 2.5 → halve both components. */
    x = 3.0f; y = 4.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 2.5f);
    T_ASSERT_NEAR(x, 1.5f);
    T_ASSERT_NEAR(y, 2.0f);
    return 0;
}

int test_player_cam_shake_zero_mag_no_div(void)
{
    /* mag==0, target>0 → guard false, no division, stays zero. */
    float x = 0.0f, y = 0.0f;
    player_ctrl_camera_shake_clamp(&x, &y, 0.175f);
    T_ASSERT_NEAR(x, 0.0f);
    T_ASSERT_NEAR(y, 0.0f);
    return 0;
}

/* ── player_ctrl_pulse_counters ──────────────────────────────────────────── */

int test_player_pulse_down_counter(void)
{
    /* the down-counter ticks toward 0 and never goes negative. */
    int down = 2, phase = 0, level = 0;
    player_ctrl_pulse_counters(&down, &phase, &level);
    if (down != 1) T_FAIL("down 2→1");
    player_ctrl_pulse_counters(&down, &phase, &level);
    if (down != 0) T_FAIL("down 1→0");
    player_ctrl_pulse_counters(&down, &phase, &level);
    if (down != 0) T_FAIL("down floored at 0");
    /* phase==0 means the level machine never runs. */
    if (level != 0) T_FAIL("level untouched while phase==0");
    return 0;
}

int test_player_pulse_phase_idle_when_zero(void)
{
    /* phase==0 → whole phase/level block skipped (the `0 < db008` guard). */
    int down = 0, phase = 0, level = 5;
    player_ctrl_pulse_counters(&down, &phase, &level);
    if (phase != 0) T_FAIL("phase stays 0");
    if (level != 5) T_FAIL("level stays 5");
    return 0;
}

int test_player_pulse_level_ramps_up_then_holds(void)
{
    /* kicked to phase=1: level climbs by 1/frame while phase<0x1e, caps at 10. */
    int down = 0, phase = 1, level = 0;
    for (int i = 0; i < 10; i++)
        player_ctrl_pulse_counters(&down, &phase, &level);
    /* 10 frames: phase 1→11, level climbed to 10. */
    if (phase != 11) T_FAIL("phase 1→11 over 10 frames (got %d)", phase);
    if (level != 10) T_FAIL("level should reach the 10 cap (got %d)", level);
    /* keep going while still <0x1e: level holds at the cap. */
    player_ctrl_pulse_counters(&down, &phase, &level);
    if (level != 10) T_FAIL("level holds at 10 cap (got %d)", level);
    return 0;
}

int test_player_pulse_level_ramps_down_second_half(void)
{
    /* phase in [0x1e,0x3c]: level decrements toward 0 (the else branch). */
    int down = 0, phase = 0x1d, level = 3;
    player_ctrl_pulse_counters(&down, &phase, &level);  /* phase→0x1e, level 3→2 */
    if (phase != 0x1e) T_FAIL("phase 0x1d→0x1e (got %d)", phase);
    if (level != 2) T_FAIL("level decrements in second half (got %d)", level);
    /* drive it to the floor and confirm it never goes negative. */
    player_ctrl_pulse_counters(&down, &phase, &level);  /* level 2→1 */
    player_ctrl_pulse_counters(&down, &phase, &level);  /* level 1→0 */
    player_ctrl_pulse_counters(&down, &phase, &level);  /* level floored */
    if (level != 0) T_FAIL("level floored at 0 (got %d)", level);
    return 0;
}

int test_player_pulse_phase_wraps_at_60(void)
{
    /* phase pre-increments past 0x3c → wraps to 0 the same frame. */
    int down = 0, phase = 0x3c, level = 0;
    player_ctrl_pulse_counters(&down, &phase, &level);
    /* 0x3c+1 = 0x3d > 0x3c → reset to 0. */
    if (phase != 0) T_FAIL("phase wraps 0x3c→0 (got %d)", phase);
    return 0;
}

/* ── player_ctrl_trail_orbit_pos ─────────────────────────────────────────── */

int test_player_trail_orbit_zero_angle(void)
{
    /* angle = 2*0 + 0 = 0 → sin=0, cos=1; r = idx+3. */
    float player[3] = { 10.0f, 5.0f, 20.0f };
    float out[3];
    player_ctrl_trail_orbit_pos(/*idx*/ 0, /*stored*/ 0.0f, /*table*/ 0.0f,
                                player, out);
    T_ASSERT_NEAR(out[0], 10.0f);          /* sin(0)*3 + 10 */
    T_ASSERT_NEAR(out[1], 5.0f);           /* y copied straight */
    T_ASSERT_NEAR(out[2], 23.0f);          /* cos(0)*3 + 20 */
    return 0;
}

int test_player_trail_orbit_radius_and_angle(void)
{
    /* idx=5 → r=8; table=π/8, stored=π/4 → angle = 2·(π/8)+π/4 = π/2.
     * sin(π/2)=1, cos(π/2)≈0. */
    const float pi = 3.14159265358979f;
    float player[3] = { 0.0f, 0.0f, 0.0f };
    float out[3];
    player_ctrl_trail_orbit_pos(5, pi / 4.0f, pi / 8.0f, player, out);
    T_ASSERT_NEAR(out[0], 8.0f);           /* sin(π/2)*8 */
    T_ASSERT_NEAR(out[1], 0.0f);
    T_ASSERT_NEAR(out[2], 0.0f);           /* cos(π/2)*8 ≈ 0 */
    return 0;
}

int test_player_trail_orbit_doubles_table(void)
{
    /* the table value is added TWICE (fadd st,st) — table=π/2 alone → angle=π. */
    const float pi = 3.14159265358979f;
    float player[3] = { 1.0f, 2.0f, 3.0f };
    float out[3];
    player_ctrl_trail_orbit_pos(0, 0.0f, pi / 2.0f, player, out);
    /* angle = 2·(π/2) = π → sin≈0, cos=-1, r=3. */
    T_ASSERT_NEAR(out[0], 1.0f);           /* sin(π)*3 + 1 ≈ 1 */
    T_ASSERT_NEAR(out[2], 0.0f);           /* cos(π)*3 + 3 = -3+3 = 0 */
    return 0;
}

/* ── Cpop.3: motion-history ring shift ───────────────────────────────────── */

int test_player_history_shift_writes_newest_to_slot0(void)
{
    float pos[PC_HIST_SLOTS][3] = {{0}};
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS] = {{0}};
    float cur_pos[3] = { 11.0f, 22.0f, 33.0f };
    int32_t cur_rec[PC_ACTOR_REC_DWORDS];
    for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++) cur_rec[j] = 100 + j;

    player_ctrl_history_shift(pos, rec, cur_pos, cur_rec);

    T_ASSERT_NEAR(pos[0][0], 11.0f);
    T_ASSERT_NEAR(pos[0][1], 22.0f);
    T_ASSERT_NEAR(pos[0][2], 33.0f);
    for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++)
        if (rec[0][j] != 100 + j) T_FAIL("rec slot0 should copy cur_rec");
    return 0;
}

int test_player_history_shift_moves_old_samples_down(void)
{
    float pos[PC_HIST_SLOTS][3] = {{0}};
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS] = {{0}};
    int32_t dummy_rec[PC_ACTOR_REC_DWORDS] = {0};

    /* Three distinct frames: positions x = 1, 2, 3. */
    float p1[3] = { 1.0f, 0.0f, 0.0f };
    float p2[3] = { 2.0f, 0.0f, 0.0f };
    float p3[3] = { 3.0f, 0.0f, 0.0f };
    player_ctrl_history_shift(pos, rec, p1, dummy_rec);
    player_ctrl_history_shift(pos, rec, p2, dummy_rec);
    player_ctrl_history_shift(pos, rec, p3, dummy_rec);

    /* Newest first: slot0 = 3 (last), slot1 = 2, slot2 = 1. */
    T_ASSERT_NEAR(pos[0][0], 3.0f);
    T_ASSERT_NEAR(pos[1][0], 2.0f);
    T_ASSERT_NEAR(pos[2][0], 1.0f);
    return 0;
}

int test_player_history_shift_record_lockstep(void)
{
    float pos[PC_HIST_SLOTS][3] = {{0}};
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS] = {{0}};
    float cur_pos[3] = {0};
    int32_t rec_a[PC_ACTOR_REC_DWORDS], rec_b[PC_ACTOR_REC_DWORDS];
    for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++) { rec_a[j] = j; rec_b[j] = 1000 + j; }

    player_ctrl_history_shift(pos, rec, cur_pos, rec_a);
    player_ctrl_history_shift(pos, rec, cur_pos, rec_b);

    /* slot0 = rec_b (newest), slot1 = rec_a (shifted down in lockstep). */
    for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++) {
        if (rec[0][j] != 1000 + j) T_FAIL("rec slot0 should be newest (rec_b)");
        if (rec[1][j] != j)        T_FAIL("rec slot1 should be prior (rec_a)");
    }
    return 0;
}

int test_player_history_shift_oldest_falls_off(void)
{
    float pos[PC_HIST_SLOTS][3] = {{0}};
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS] = {{0}};
    int32_t dummy_rec[PC_ACTOR_REC_DWORDS] = {0};

    /* Push PC_HIST_SLOTS+1 frames; the first must have been evicted. */
    for (int f = 1; f <= PC_HIST_SLOTS + 1; f++) {
        float cp[3] = { (float)f, 0.0f, 0.0f };
        player_ctrl_history_shift(pos, rec, cp, dummy_rec);
    }
    /* Ring holds frames N+1 .. 2 (newest..oldest); frame 1 is gone. */
    T_ASSERT_NEAR(pos[0][0], (float)(PC_HIST_SLOTS + 1));
    T_ASSERT_NEAR(pos[PC_HIST_SLOTS - 1][0], 2.0f);   /* oldest surviving */
    return 0;
}

/* ── Cpop.4: shake-damp factor selector ──────────────────────────────────── */

/* args: (mode_nonzero, grounded, flag_6ca, held_96b, edge_9, db100, db048) */

int test_player_shake_damp_mode_nonzero(void)
{
    /* DAT_056da1bc != 0 short-circuits to 0.97 regardless of the rest. */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(1, 1, 0, 0, 0, 0, 0), 0.97f);
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(1, 0, 1, 1, 1, 9, 3), 0.97f);
    return 0;
}

int test_player_shake_damp_not_grounded(void)
{
    /* grounded false (da1dc != daf88) → 0.99. */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 0, 0, 0, 0, 0, 0), 0.99f);
    return 0;
}

int test_player_shake_damp_flag_set_not_held(void)
{
    /* grounded, flag_6ca != 0, binding not held → 0.95. */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 1, 0, 0, 0, 0), 0.95f);
    return 0;
}

int test_player_shake_damp_idle_settle_0998(void)
{
    /* grounded, gate fails (edge_9 set, not held) → slow settle 0.998. */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 0, 0, 1, 0, 0), 0.998f);
    /* alt: not-edge but db100>=1, still not held → also 0.998. */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 0, 0, 0, 1, 0), 0.998f);
    return 0;
}

int test_player_shake_damp_state_block(void)
{
    /* Reach the db048 block: grounded, flag==0, held==0, edge==0, db100==0
     * → gate ((!edge && db100<1) || held) = true. */
    /* db048 == 3 → 0.95 */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 0, 0, 0, 0, 3), 0.95f);
    /* db048 == 2 → 0.82 */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 0, 0, 0, 0, 2), 0.82f);
    /* db048 other, grounded → 0.82 (the 0.98 arm is unreachable when grounded) */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 0, 0, 0, 0, 7), 0.82f);
    return 0;
}

int test_player_shake_damp_held_reaches_state_block(void)
{
    /* held_96b satisfies BOTH the 0.95-skip (flag||held) and the gate
     * (…||held), so a held binding lands in the db048 block even with
     * flag_6ca set and edge_9 set: db048==2 → 0.82. */
    T_ASSERT_NEAR(player_ctrl_shake_damp_factor(0, 1, 1, 1, 1, 0, 2), 0.82f);
    return 0;
}

/* ── Cpop.5: shake-target magnitude accumulation ─────────────────────────── */

/* args: (base, held_968, held_969, boost, b8b0_neg1, db074, dae9c_active,
 *        daeac, db048, da1cc, daed8_is_1, db07c_is_0, daedc, da1dc) */

int test_player_shake_target_base_passthrough(void)
{
    /* All gates off → just the base. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 0,0,0,0,0.0f,0,0, 0,0, 0,0, 0.0f,0.0f), 0.175f);
    return 0;
}

int test_player_shake_target_held_adds(void)
{
    /* held 0x968 → +0.02, held 0x969 → +0.08 (both → +0.10). */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 1,0,0,0,0.0f,0,0, 0,0, 0,0, 0.0f,0.0f), 0.195f);
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 0,1,0,0,0.0f,0,0, 0,0, 0,0, 0.0f,0.0f), 0.255f);
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 1,1,0,0,0.0f,0,0, 0,0, 0,0, 0.0f,0.0f), 0.275f);
    return 0;
}

int test_player_shake_target_boost_multiplies_after_adds(void)
{
    /* boost ×1.3 applies after the held adds: (0.175 + 0.02) * 1.3. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 1,0,1,0,0.0f,0,0, 0,0, 0,0, 0.0f,0.0f),
                  (0.175f + 0.02f) * 1.3f);
    return 0;
}

int test_player_shake_target_b8b0_and_rumble(void)
{
    /* b8b0==-1 → += db074;  then rumble bit2 → +0.06 (bit1 ignored when bit2 set). */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.1f, 0,0,0,1,0.5f,1,0x3, 0,0, 0,0, 0.0f,0.0f),
                  0.1f + 0.5f + 0.06f);
    /* rumble bit1 only → +0.03. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.1f, 0,0,0,0,0.0f,1,0x1, 0,0, 0,0, 0.0f,0.0f),
                  0.1f + 0.03f);
    /* dae9c inactive → no rumble add even with bits set. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.1f, 0,0,0,0,0.0f,0,0x3, 0,0, 0,0, 0.0f,0.0f), 0.1f);
    return 0;
}

int test_player_shake_target_state_overrides(void)
{
    /* db048==1 forces 0.5 regardless of prior accumulation. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 1,1,1,0,0.0f,0,0, 1,0, 0,0, 0.0f,0.0f), 0.5f);
    /* db048==4/5: char 0x29 → 1.0, else 0.5. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 0,0,0,0,0.0f,0,0, 4,0x29, 0,0, 0.0f,0.0f), 1.0f);
    T_ASSERT_NEAR(player_ctrl_shake_target(0.175f, 0,0,0,0,0.0f,0,0, 5,0x00, 0,0, 0.0f,0.0f), 0.5f);
    return 0;
}

int test_player_shake_target_proximity_ease(void)
{
    /* daed8==1 && db07c==0: t = 0.3 - clamp01(daedc - da1dc)*0.1, overriding all. */
    /* diff 0.5 (in range) → 0.3 - 0.05 = 0.25. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.9f, 1,1,1,0,0.0f,0,0, 0,0, 1,1, 1.0f,0.5f), 0.25f);
    /* diff > 1 clamps to 1 → 0.3 - 0.1 = 0.2. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.9f, 0,0,0,0,0.0f,0,0, 0,0, 1,1, 5.0f,0.5f), 0.2f);
    /* diff < 0 clamps to 0 → 0.3. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.9f, 0,0,0,0,0.0f,0,0, 0,0, 1,1, 0.0f,0.5f), 0.3f);
    /* db07c != 0 disables the block (db07c_is_0 == 0 here) → base passes through. */
    T_ASSERT_NEAR(player_ctrl_shake_target(0.42f, 0,0,0,0,0.0f,0,0, 0,0, 1,0, 1.0f,0.5f), 0.42f);
    return 0;
}

/* ── Cpop.6: dash-trail / after-image record advance ─────────────────────── */

/* Helpers: trail records carry mixed int/float fields in an int32 array. */
static void trail_set_f(int32_t rec[PC_TRAIL_REC_DWORDS], int field, float v)
{
    memcpy(&rec[field], &v, sizeof v);
}
static float trail_get_f(const int32_t rec[PC_TRAIL_REC_DWORDS], int field)
{
    float v; memcpy(&v, &rec[field], sizeof v); return v;
}

int test_player_trail_advance_skips_dead_records(void)
{
    int32_t recs[PC_TRAIL_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    int32_t ring[PC_ACTOR_REC_DWORDS] = {0};
    float player[3] = { 1.0f, 2.0f, 3.0f };
    float table[1] = { 0.0f };
    pc_trail_events ev;

    /* all countdowns 0 (and one negative) → untouched, no events. */
    recs[2][PC_TRAIL_COUNTDOWN] = -5;
    player_ctrl_trail_advance(recs, ring, player, table, /*decay*/ 1, &ev);

    if (ev.alloc_count != 0) T_FAIL("dead records must not alloc-spawn");
    if (ev.spawn_count != 0) T_FAIL("dead records must not spawn");
    if (recs[2][PC_TRAIL_COUNTDOWN] != -5) T_FAIL("dead countdown unchanged");
    return 0;
}

int test_player_trail_advance_copies_sprite_and_geometry(void)
{
    int32_t recs[PC_TRAIL_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    int32_t ring[PC_ACTOR_REC_DWORDS];
    for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++) ring[j] = 700 + j;
    float player[3] = { 10.0f, 5.0f, 20.0f };
    float table[1] = { 0.0f };

    /* one active record: idx 0, angle 0, life 100. */
    recs[0][PC_TRAIL_COUNTDOWN] = 100;
    recs[0][PC_TRAIL_IDX] = 0;
    trail_set_f(recs[0], PC_TRAIL_ANGLE, 0.0f);

    player_ctrl_trail_advance(recs, ring, player, table, /*decay*/ 0, NULL);

    /* sprite-state snapshot copied verbatim. */
    for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++)
        if (recs[0][PC_TRAIL_SPRITE + j] != 700 + j)
            T_FAIL("sprite ring should be copied into the record");
    /* angle 0 → sin=0,cos=1, r=idx+3=3: x=px, y=py, z=pz+3. */
    T_ASSERT_NEAR(trail_get_f(recs[0], PC_TRAIL_X), 10.0f);
    T_ASSERT_NEAR(trail_get_f(recs[0], PC_TRAIL_Y), 5.0f);
    T_ASSERT_NEAR(trail_get_f(recs[0], PC_TRAIL_Z), 23.0f);
    /* life counter decremented once. */
    if (recs[0][PC_TRAIL_COUNTDOWN] != 99) T_FAIL("countdown should decrement");
    return 0;
}

int test_player_trail_advance_spawns_at_600(void)
{
    int32_t recs[PC_TRAIL_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    int32_t ring[PC_ACTOR_REC_DWORDS] = {0};
    float player[3] = { 0.0f, 0.0f, 0.0f };
    float table[1] = { 0.0f };
    pc_trail_events ev;

    /* record 1 exactly at the spawn threshold; record 3 just past it. */
    recs[1][PC_TRAIL_COUNTDOWN] = 600;
    recs[3][PC_TRAIL_COUNTDOWN] = 601;
    player_ctrl_trail_advance(recs, ring, player, table, /*decay*/ 0, &ev);

    if (ev.spawn_count != 1) T_FAIL("only the life==600 record spawns");
    /* idx 0, angle 0: z = 0 + (0+3) = 3, x = y = 0. */
    T_ASSERT_NEAR(ev.spawn_pos[0][2], 3.0f);
    if (recs[1][PC_TRAIL_COUNTDOWN] != 599) T_FAIL("threshold rec still decrements");
    if (recs[3][PC_TRAIL_COUNTDOWN] != 600) T_FAIL("601 record decrements, no spawn");
    return 0;
}

int test_player_trail_advance_decay_alloc_order(void)
{
    int32_t recs[PC_TRAIL_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    int32_t ring[PC_ACTOR_REC_DWORDS] = {0};
    float player[3] = { 0.0f, 0.0f, 0.0f };
    float table[1] = { 0.0f };
    pc_trail_events ev;

    /* records 0, 2, 4 active → alloc requested for each, in record order. */
    recs[0][PC_TRAIL_COUNTDOWN] = 10;
    recs[2][PC_TRAIL_COUNTDOWN] = 10;
    recs[4][PC_TRAIL_COUNTDOWN] = 10;
    player_ctrl_trail_advance(recs, ring, player, table, /*decay*/ 1, &ev);

    if (ev.alloc_count != 3) T_FAIL("one alloc per active record when decay set");
    if (ev.alloc_index[0] != 0 || ev.alloc_index[1] != 2 || ev.alloc_index[2] != 4)
        T_FAIL("alloc requests should be in record order");

    /* with decay clear, no alloc requests. */
    recs[0][PC_TRAIL_COUNTDOWN] = 10;
    recs[2][PC_TRAIL_COUNTDOWN] = 10;
    recs[4][PC_TRAIL_COUNTDOWN] = 10;
    player_ctrl_trail_advance(recs, ring, player, table, /*decay*/ 0, &ev);
    if (ev.alloc_count != 0) T_FAIL("no alloc when decay flag clear");
    return 0;
}

/* ── Cpop.7: dacc0 after-image burst materialization ─────────────────────── */

/* Build the two 40-slot motion-history rings with recognisable per-slot
 * marker values: position slot s = (s*100, s*100+1, s*100+2); record slot s
 * dword j = s*1000 + j. */
static void burst_fill_history(float pos[PC_HIST_SLOTS][3],
                               int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS])
{
    for (int s = 0; s < PC_HIST_SLOTS; s++) {
        pos[s][0] = (float)(s * 100);
        pos[s][1] = (float)(s * 100 + 1);
        pos[s][2] = (float)(s * 100 + 2);
        for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++)
            rec[s][j] = s * 1000 + j;
    }
}

int test_player_burst_noop_when_counter_not_positive(void)
{
    int32_t bank[PC_BURST_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    float pos[PC_HIST_SLOTS][3];
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS];
    burst_fill_history(pos, rec);

    /* counter 0 and -3 are both no-ops: bank untouched, counter returned as-is. */
    if (player_ctrl_burst_materialize(bank, pos, rec, 0) != 0)
        T_FAIL("counter 0 should return 0 unchanged");
    if (player_ctrl_burst_materialize(bank, pos, rec, -3) != -3)
        T_FAIL("negative counter should return unchanged");
    for (int k = 0; k < PC_BURST_RECORDS; k++)
        for (int j = 0; j < PC_TRAIL_REC_DWORDS; j++)
            if (bank[k][j] != 0) T_FAIL("bank must stay zero when counter <= 0");
    return 0;
}

int test_player_burst_samples_every_other_slot_from_3(void)
{
    int32_t bank[PC_BURST_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    float pos[PC_HIST_SLOTS][3];
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS];
    burst_fill_history(pos, rec);

    int ret = player_ctrl_burst_materialize(bank, pos, rec, 5);
    if (ret != 4) T_FAIL("counter 5 should decrement to 4");

    /* record k sources history slot s = 3 + 2k → 3, 5, 7, 9, 11. */
    for (int k = 0; k < PC_BURST_RECORDS; k++) {
        int s = 3 + 2 * k;
        /* sprite-state copied verbatim from rec_hist[s]. */
        for (int j = 0; j < PC_ACTOR_REC_DWORDS; j++)
            if (bank[k][PC_TRAIL_SPRITE + j] != s * 1000 + j)
                T_FAIL("record %d sprite should come from history slot %d", k, s);
        /* full-slot position copy. */
        T_ASSERT_NEAR(trail_get_f(bank[k], PC_TRAIL_X), (float)(s * 100));
        T_ASSERT_NEAR(trail_get_f(bank[k], PC_TRAIL_Y), (float)(s * 100 + 1));
        T_ASSERT_NEAR(trail_get_f(bank[k], PC_TRAIL_Z), (float)(s * 100 + 2));
        /* life seeded to 0x14. */
        if (bank[k][PC_TRAIL_COUNTDOWN] != 0x14)
            T_FAIL("record %d life should seed to 0x14", k);
    }
    return 0;
}

int test_player_burst_final_frame_clears_life(void)
{
    int32_t bank[PC_BURST_RECORDS][PC_TRAIL_REC_DWORDS] = {{0}};
    float pos[PC_HIST_SLOTS][3];
    int32_t rec[PC_HIST_SLOTS][PC_ACTOR_REC_DWORDS];
    burst_fill_history(pos, rec);

    /* counter 1 → fills (life 0x14) then decrements to 0, which triggers the
     * clear pass: every life field zeroed, the rest of the record left as
     * just-filled (only the +0x38 dword is touched). */
    int ret = player_ctrl_burst_materialize(bank, pos, rec, 1);
    if (ret != 0) T_FAIL("counter 1 should decrement to 0");
    for (int k = 0; k < PC_BURST_RECORDS; k++) {
        int s = 3 + 2 * k;
        if (bank[k][PC_TRAIL_COUNTDOWN] != 0)
            T_FAIL("final burst frame must zero every life field");
        /* sprite/position still hold the just-materialized sample. */
        if (bank[k][PC_TRAIL_SPRITE] != s * 1000)
            T_FAIL("clear pass must leave the sprite copy intact");
        T_ASSERT_NEAR(trail_get_f(bank[k], PC_TRAIL_X), (float)(s * 100));
    }
    return 0;
}

/* ── Cpop.8: HP/SP displayed-gauge tween (FUN_0048b6ad) ───────────────────── */

int test_player_gauge_rate_sums_then_scales(void)
{
    /* (i16 + i16) * 0.01, sign-extended.  0x5193a4 = 0.01f. */
    T_ASSERT_NEAR(player_ctrl_gauge_rate(30, 20), 0.5f);
    T_ASSERT_NEAR(player_ctrl_gauge_rate(-10, 5), -0.05f);
    T_ASSERT_NEAR(player_ctrl_gauge_rate(0, 0), 0.0f);
    return 0;
}

int test_player_gauge_rises_and_clamps_with_dir1(void)
{
    /* disp below target → rises by rate, counter ticks, dir = 1 (healing). */
    float hp = 5.0f, sp = 0.0f;
    int counter = 7, dir = -1;
    player_ctrl_gauge_track(&hp, 10.0f, 2.0f, &sp, 0.0f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(hp, 7.0f);
    T_ASSERT_EQ_I(counter, 8);
    T_ASSERT_EQ_I(dir, 1);
    /* overshoot is clamped to the target, not stepped past it. */
    player_ctrl_gauge_track(&hp, 7.5f, 2.0f, &sp, 0.0f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(hp, 7.5f);
    T_ASSERT_EQ_I(counter, 9);
    return 0;
}

int test_player_gauge_falls_with_dir0(void)
{
    /* disp above target → falls by rate, counter ticks, dir = 0 (damage). */
    float hp = 10.0f, sp = 0.0f;
    int counter = 0, dir = 1;
    player_ctrl_gauge_track(&hp, 4.0f, 2.0f, &sp, 0.0f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(hp, 8.0f);
    T_ASSERT_EQ_I(counter, 1);
    T_ASSERT_EQ_I(dir, 0);
    /* undershoot clamps up to the target. */
    player_ctrl_gauge_track(&hp, 7.5f, 2.0f, &sp, 0.0f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(hp, 7.5f);
    T_ASSERT_EQ_I(dir, 0);
    return 0;
}

int test_player_gauge_settled_resets_counter_keeps_dir(void)
{
    /* HP already at target → counter resets to 0, dir untouched. */
    float hp = 6.0f, sp = 0.0f;
    int counter = 42, dir = 1;
    player_ctrl_gauge_track(&hp, 6.0f, 2.0f, &sp, 0.0f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(hp, 6.0f);
    T_ASSERT_EQ_I(counter, 0);
    T_ASSERT_EQ_I(dir, 1);   /* the engine's equal branch leaves db0d0 alone */
    return 0;
}

int test_player_gauge_sp_channel_has_no_counter(void)
{
    /* Channel B eases + clamps independently of the HP counter/dir. */
    float hp = 3.0f, sp = 2.0f;
    int counter = 0, dir = 0;
    /* SP rises and clamps; HP holds (already settled → counter 0). */
    player_ctrl_gauge_track(&hp, 3.0f, 1.0f, &sp, 2.4f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(sp, 2.4f);     /* 2.0 + 1.0 overshoots 2.4 → clamp */
    T_ASSERT_EQ_I(counter, 0);   /* HP settled, untouched by the SP move */
    /* SP falls and clamps. */
    sp = 5.0f;
    player_ctrl_gauge_track(&hp, 3.0f, 1.0f, &sp, 4.7f, 1.0f, &counter, &dir);
    T_ASSERT_NEAR(sp, 4.7f);
    return 0;
}

/* ── W3: the per-frame free-roam walk controller (FUN_0048670f) ───────────── */

int test_player_ctrl_tick_no_input_preserves_idle_pose(void)
{
    /* With no d-pad held the tick must leave the seeded idle pose untouched:
     * zero velocity (no integrate), idle facing octant 6 re-derived identically,
     * anim/frame/counter/timer all as seeded. */
    g_input_state[0].buttons = 0;
    player_ctrl_pose_house_standing(0);
    g_scene1_player_pos[0] = -0.3f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 9.35f;

    int32_t rec_before[PC_ACTOR_REC_DWORDS];
    memcpy(rec_before, player_ctrl_actor_record(0), sizeof rec_before);

    scene1_player_ctrl_tick();

    if (player_ctrl_actor_char(0) != 0) T_FAIL("tick changed actor0 char");
    T_ASSERT_NEAR(g_scene1_player_pos[0], -0.3f);   /* didn't move */
    T_ASSERT_NEAR(g_scene1_player_pos[2], 9.35f);
    T_ASSERT_MEM_EQ(player_ctrl_actor_record(0), rec_before, sizeof rec_before);
    return 0;
}

int test_player_ctrl_dpad_angle_cardinals(void)
{
    /* vx = sin(angle), vz = cos(angle): RIGHT +x → +π/2, LEFT −x → −π/2,
     * DOWN +z → 0, UP −z → π. */
    float a;
    if (!player_ctrl_dpad_angle(0x0001u, &a)) T_FAIL("RIGHT should move");
    T_ASSERT_NEAR(a,  1.57079633f);
    if (!player_ctrl_dpad_angle(0x0002u, &a)) T_FAIL("LEFT should move");
    T_ASSERT_NEAR(a, -1.57079633f);
    if (!player_ctrl_dpad_angle(0x0008u, &a)) T_FAIL("DOWN should move");
    T_ASSERT_NEAR(a,  0.0f);
    if (!player_ctrl_dpad_angle(0x0004u, &a)) T_FAIL("UP should move");
    T_ASSERT_NEAR(a,  3.14159265f);
    return 0;
}

int test_player_ctrl_dpad_angle_none_and_diagonal(void)
{
    float a = 123.0f;
    if (player_ctrl_dpad_angle(0x0000u, &a)) T_FAIL("no dir should not move");
    T_ASSERT_NEAR(a, 123.0f);                     /* left untouched */
    if (player_ctrl_dpad_angle(0x0010u, &a)) T_FAIL("button A is not a dir");
    /* UP+LEFT: dx=-1, dz=-1 → atan2(-1,-1) = -3π/4. */
    if (!player_ctrl_dpad_angle(0x0004u | 0x0002u, &a)) T_FAIL("UP+LEFT moves");
    T_ASSERT_NEAR(a, -2.35619449f);
    /* opposing LEFT+RIGHT cancel → no move. */
    if (player_ctrl_dpad_angle(0x0001u | 0x0002u, &a)) T_FAIL("L+R cancel");
    return 0;
}

int test_player_ctrl_facing_octant_cardinals(void)
{
    /* HOUSE cam yaw −π: idle +π/2 → 6, LEFT −π/2 → 2, DOWN 0 → 4, UP π → 0. */
    if (player_ctrl_facing_octant( 1.57079633f, PC_HOUSE_CAM_YAW) != 6) T_FAIL("RIGHT oct 6");
    if (player_ctrl_facing_octant(-1.57079633f, PC_HOUSE_CAM_YAW) != 2) T_FAIL("LEFT oct 2");
    if (player_ctrl_facing_octant( 0.0f,        PC_HOUSE_CAM_YAW) != 4) T_FAIL("DOWN oct 4");
    if (player_ctrl_facing_octant( 3.14159265f, PC_HOUSE_CAM_YAW) != 0) T_FAIL("UP oct 0");
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

int test_player_ctrl_walk_left_matches_retail(void)
{
    /* Ground truth: runs/w3-walk-watch, retail HOUSE walk-left from idle
     * (px −0.3, pz 9.35).  Holding LEFT, the per-frame px sequence is
     * −0.4, −0.575, −0.75, −0.925, −1.1, −1.275, −1.45, then the left-wall
     * clamp pins −1.5.  (impulse 0.1 → cap 0.175 → integrate → damp 0.82.) */
    static const float want_px[] = {
        -0.4f, -0.575f, -0.75f, -0.925f, -1.1f, -1.275f, -1.45f, -1.5f, -1.5f,
    };
    g_input_state[0].buttons = 0;
    player_ctrl_pose_house_standing(0);
    g_scene1_player_pos[0] = -0.3f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 9.35f;

    g_input_state[0].buttons = 0x0002u;          /* hold LEFT */
    for (size_t i = 0; i < sizeof want_px / sizeof want_px[0]; i++) {
        scene1_player_ctrl_tick();
        T_ASSERT_NEAR(g_scene1_player_pos[0], want_px[i]);
        T_ASSERT_NEAR(g_scene1_player_pos[2], 9.35f);         /* pure-x walk */
    }
    /* facing octant 2 (LEFT) + walk anim id 1. */
    const int32_t *rec = player_ctrl_actor_record(0);
    if (rec[CHR_ACTOR_FACING] != 2) T_FAIL("LEFT walk facing should be oct 2");
    if (rec[CHR_ACTOR_ANIM]   != 1) T_FAIL("walking anim id should be 1");
    return 0;
}

int test_player_ctrl_walk_release_decays_and_idles(void)
{
    /* After releasing the d-pad: anim → idle (0) immediately, and the residual
     * velocity decays by 0.82/frame (retail release tail).  Walk RIGHT from a
     * wall-free spot so the decay shows as continued +x creep, then stops. */
    g_input_state[0].buttons = 0;
    player_ctrl_pose_house_standing(0);
    g_scene1_player_pos[0] = 0.0f;
    g_scene1_player_pos[1] = 0.0f;
    g_scene1_player_pos[2] = 5.0f;               /* pz<7 → no wall clamp */

    g_input_state[0].buttons = 0x0001u;          /* RIGHT */
    for (int i = 0; i < 6; i++) scene1_player_ctrl_tick();
    if (player_ctrl_actor_record(0)[CHR_ACTOR_ANIM] != 1) T_FAIL("should be walking");
    float px_walk = g_scene1_player_pos[0];
    if (px_walk <= 0.0f) T_FAIL("RIGHT walk should increase px");

    g_input_state[0].buttons = 0;                /* release */
    scene1_player_ctrl_tick();
    if (player_ctrl_actor_record(0)[CHR_ACTOR_ANIM] != 0) T_FAIL("release → idle anim");
    float px_after = g_scene1_player_pos[0];
    if (px_after <= px_walk) T_FAIL("residual velocity should still creep +x");

    /* many idle frames later the velocity has decayed to rest: the residual
     * per-frame step is negligible (0.82^60 ≈ 5e-6 of the initial). */
    for (int i = 0; i < 59; i++) scene1_player_ctrl_tick();
    float p_a = g_scene1_player_pos[0];
    scene1_player_ctrl_tick();
    float step = g_scene1_player_pos[0] - p_a;
    if (step < 0.0f) step = -step;
    if (step > 1e-3f) T_FAIL("velocity should have decayed to rest");
    return 0;
}

/* ── Cchr.2h: house-standing actor-state model ───────────────────────────── */

int test_player_pose_seeds_actor0(void)
{
    /* runs/cchr2b leaf ground truth: char 0, scale 1/1, record
     * anim 0 / timer 5.0f / counter 25 / frame 2 / facing 6. */
    player_ctrl_pose_house_standing(0);

    if (player_ctrl_actor_char(0) != 0) T_FAIL("actor0 char should be 0");
    T_ASSERT_NEAR(player_ctrl_actor_scale_xz(0), 1.0f);
    T_ASSERT_NEAR(player_ctrl_actor_scale_y(0), 1.0f);

    const int32_t *rec = player_ctrl_actor_record(0);
    if (rec == NULL) T_FAIL("actor0 record should be non-NULL");
    if (rec[CHR_ACTOR_ANIM]    != 0)  T_FAIL("anim should be 0");
    if (rec[CHR_ACTOR_COUNTER] != 25) T_FAIL("counter should be 25");
    if (rec[CHR_ACTOR_FRAME]   != 2)  T_FAIL("frame should be 2");
    if (rec[CHR_ACTOR_FACING]  != 6)  T_FAIL("facing should be 6");
    /* TIMER is float bits (5.0f). */
    union { float f; int32_t i; } t; t.i = rec[CHR_ACTOR_TIMER];
    T_ASSERT_NEAR(t.f, 5.0f);
    return 0;
}

int test_player_pose_empties_party_slots(void)
{
    player_ctrl_pose_house_standing(0);
    /* slots 1/2 stay empty (char -1, scale 0) until companion lands. */
    if (player_ctrl_actor_char(1) != -1) T_FAIL("actor1 char should be -1");
    if (player_ctrl_actor_char(2) != -1) T_FAIL("actor2 char should be -1");
    T_ASSERT_NEAR(player_ctrl_actor_scale_xz(1), 0.0f);
    T_ASSERT_NEAR(player_ctrl_actor_scale_y(2), 0.0f);
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
