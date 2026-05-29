/*
 * test_scene1_chr_walker.c — Cchr.2d coverage.
 *
 * Exercises the pure per-actor math of the HOUSE character-sprite walker
 * (engine FUN_00456f56): the scene fade-in factor, the spawn pop-in ease,
 * the player/party draw-order alpha, and the NPC off-screen fade ramp.
 * The Win32 orchestrator itself is dormant (no populated actor tables) and
 * not host-testable; these leaf functions are where the decoded constants
 * and branch structure live.
 */
#include "t.h"

#include <math.h>

#include "scene1_chr_walker.h"

static int near_f(float a, float b)
{
    return fabsf(a - b) <= 1e-4f * (1.0f + fabsf(b));
}
#define T_ASSERT_NEAR(a, b) do { \
    float _a = (a), _b = (b); \
    if (!near_f(_a, _b)) \
        T_FAIL("expected %s ≈ %s (got %.7f, want %.7f)", #a, #b, _a, _b); \
} while (0)

/* ── chr_walker_fadein ──────────────────────────────────────────────────── */

int test_chr_walker_fadein_clamps_high(void)
{
    /* counter 0 → 90/30 = 3.0, clamped to 1.0. */
    T_ASSERT_NEAR(chr_walker_fadein(0), 1.0f);
    /* counter 0x3c (60) → 30/30 = exactly 1.0 (clamp boundary). */
    T_ASSERT_NEAR(chr_walker_fadein(0x3c), 1.0f);
    return 0;
}

int test_chr_walker_fadein_ramps(void)
{
    /* counter 0x4b (75) → (90-75)/30 = 0.5. */
    T_ASSERT_NEAR(chr_walker_fadein(0x4b), 0.5f);
    /* counter 0x5a (90) → 0/30 = 0.0 (block's last active frame). */
    T_ASSERT_NEAR(chr_walker_fadein(0x5a), 0.0f);
    return 0;
}

/* ── chr_walker_spawn_ease ──────────────────────────────────────────────── */

int test_chr_walker_spawn_ease_midspawn(void)
{
    float sx = 4.0f, sz = 3.0f;
    chr_walker_spawn_ease(10, &sx, &sz);   /* age 10 < 20 */
    T_ASSERT_NEAR(sx, 2.0f);               /* × 10/20 */
    T_ASSERT_NEAR(sz, 6.0f);               /* × ((20-10)/10 + 1) = 2.0 */
    return 0;
}

int test_chr_walker_spawn_ease_age0(void)
{
    float sx = 5.0f, sz = 5.0f;
    chr_walker_spawn_ease(0, &sx, &sz);
    T_ASSERT_NEAR(sx, 0.0f);               /* × 0/20 */
    T_ASSERT_NEAR(sz, 15.0f);              /* × ((20-0)/10 + 1) = 3.0 */
    return 0;
}

int test_chr_walker_spawn_ease_fullspawn_noop(void)
{
    float sx = 4.0f, sz = 3.0f;
    chr_walker_spawn_ease(20, &sx, &sz);   /* age == 20 → no ease */
    T_ASSERT_NEAR(sx, 4.0f);
    T_ASSERT_NEAR(sz, 3.0f);
    chr_walker_spawn_ease(99, &sx, &sz);
    T_ASSERT_NEAR(sx, 4.0f);
    T_ASSERT_NEAR(sz, 3.0f);
    return 0;
}

/* ── chr_walker_actor_alpha ─────────────────────────────────────────────── */

int test_chr_walker_player_alpha_clamps(void)
{
    /* age 0 → (0x254)*8 = 0x12a0, clamped to 0x9b. */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0, 0, 0x9b, 0), 0x9b);
    /* age 0x250 → (4)*8 = 0x20, in range. */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0x250, 0, 0x9b, 0), 0x20);
    /* age == 0x254 → 0 (boundary, not skipped). */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0x254, 0, 0x9b, 0), 0);
    return 0;
}

int test_chr_walker_player_alpha_skip_negative(void)
{
    /* age past 0x254 → (negative)*8 < 0 → skip (-1). */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0x255, 0, 0x9b, 0), -1);
    return 0;
}

int test_chr_walker_party_alpha_override(void)
{
    /* is_party with daae0 >= 0xa → alpha = prio_base (age must pass the
     * leading non-negative gate; age 0 → (0x254)*8 >= 0). */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0, 1, 0x87, 0xa), 0x87);
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0, 1, 0x4b, 99), 0x4b);
    return 0;
}

int test_chr_walker_party_alpha_lowfade(void)
{
    /* daae0 < 0xa → v = (daae0 - 10)*0xf + prio_base. */
    /* daae0 5, prio 0x9b(155) → (-5)*15 + 155 = 80 = 0x50. */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0, 1, 0x9b, 5), 0x50);
    /* daae0 0, prio 0x4b(75) → (-10)*15 + 75 = -75 → skip (-1). */
    T_ASSERT_EQ_I(chr_walker_actor_alpha(0, 1, 0x4b, 0), -1);
    return 0;
}

/* ── chr_walker_npc_alpha ───────────────────────────────────────────────── */

int test_chr_walker_npc_alpha_offscreen_skip(void)
{
    /* pos < -75 → fully off-screen → 0 (skip). */
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-80.0f, 1.0f), 0);
    return 0;
}

int test_chr_walker_npc_alpha_full(void)
{
    /* pos >= -70 → full 255 before the per-record multiply. */
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-60.0f, 1.0f), 255);
    /* pos exactly -70 → still full (engine jae: pos >= -70 skips ramp). */
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-70.0f, 1.0f), 255);
    /* per-record multiply truncates (255 × 0.5 = 127.5 → 127). */
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-60.0f, 0.5f), 127);
    /* mult 0 → 0 (caller skips). */
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-60.0f, 0.0f), 0);
    return 0;
}

int test_chr_walker_npc_alpha_ramp(void)
{
    /* pos in [-75,-70): a = (pos+70)*50 + 255.
     *   pos -72.5 → (-2.5)*50 + 255 = 130.
     *   pos -75   → (-5)*50  + 255 = 5  (ramp floor). */
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-72.5f, 1.0f), 130);
    T_ASSERT_EQ_I(chr_walker_npc_alpha(-75.0f, 1.0f), 5);
    return 0;
}
