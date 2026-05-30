/*
 * test_scene1_player_ctrl.c — Cpop.1 coverage.
 *
 * Exercises the pure leaf math of the HOUSE per-frame player controller
 * (engine FUN_0048b850): the 8→4 facing-octant snap with its sticky
 * horizontal bias, the camera zoom-bias decay + floor clamp, and the
 * camera-shake magnitude clamp.  The stateful controller body itself is
 * dormant (its caller chain FUN_00442cef→FUN_0048670f is unported) and not
 * host-testable; these leaves are where the decoded branch structure lives.
 */
#include "t.h"

#include <math.h>
#include <stdint.h>

#include "scene1_chr_sprite.h"   /* CHR_ACTOR_* record-field indices */
#include "scene1_player_ctrl.h"

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
