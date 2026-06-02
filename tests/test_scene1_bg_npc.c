/*
 * test_scene1_bg_npc.c — HOUSE background-window NPC sim (FUN_0046f2a3 /
 * FUN_0046f621; the subsystem formerly misnamed "ambient motes").
 *
 * Locks the RNG-stream fidelity the foot-dust parity depends on: spawn
 * consumes exactly 7 (or 8) shared-LCG steps in a fixed order, a bound-cross
 * "respawn" exactly 4 (or 5), and the per-tick pause/counter path is dead
 * (never advances) — see scene1_bg_npc.c.  Also checks the drift + the 180×
 * entry warmup spawning all 6 NPCs.
 */
#include "t.h"

#include <math.h>

#include "scene1_bg_npc.h"
#include "rng.h"

/* How many LCG steps (s = s*0x343fd + 0x269ec3) carry `from` to `to`?  -1 if
 * none within `max`.  Both rng_next15 and rng_next_unit advance one step. */
static int lcg_steps(uint32_t from, uint32_t to, int max)
{
    uint32_t s = from;
    for (int n = 0; n <= max; n++) {
        if (s == to) return n;
        s = s * 0x343fdu + 0x269ec3u;
    }
    return -1;
}

/* Spawn (idx 0) consumes 7 or 8 LCG steps; fields land in range. */
int test_bg_npc_spawn_rng_count(void)
{
    scene1_bg_npc_reset();
    rng_seed(99u);
    uint32_t s0 = g_rng_seed;

    scene1_bg_npc_sim_once();          /* spawn NPC 0 (+ one inert drift tick) */

    int n = lcg_steps(s0, g_rng_seed, 64);
    if (n != 7 && n != 8)
        T_FAIL("spawn consumed %d LCG steps, want 7 or 8", n);

    scene1_bg_npc_t *m = &g_scene1_bg_npc[0];
    if (m->dir != 1 && m->dir != -1) T_FAIL("dir=%d not ±1", m->dir);
    if (m->visible != 0)             T_FAIL("visible=%d want 0", m->visible);
    if (m->type != 0)                T_FAIL("type=%d want 0 (table[0])", m->type);
    if (!(m->z <= -11.0f && m->z >= -15.0f)) T_FAIL("z=%g not in [-15,-11]", (double)m->z);
    if (!(m->speed >= 0.5f && m->speed < 1.0f)) T_FAIL("speed=%g not in [0.5,1)", (double)m->speed);
    if (m->prob < 0 || m->prob > 99) T_FAIL("prob=%d not in [0,99]", m->prob);
    if (m->mode < 0 || m->mode > 2)  T_FAIL("mode=%d not in {0,1,2}", m->mode);
    /* spawn x = idx*4.6 - 14 = -14, minus at most one 0.05 drift tick. */
    if (fabsf(m->x - (-14.0f)) > 0.06f) T_FAIL("spawn x=%g, want ≈-14", (double)m->x);
    return 0;
}

/* The 180× warmup seeds all 6 NPCs with the right type table, drifts them,
 * and never trips the dead pause path. */
int test_bg_npc_warmup_spawns_all(void)
{
    static const int want_type[SCENE1_BG_NPC_COUNT] = { 0, 1, 6, 7, 9, 8 };

    scene1_bg_npc_reset();
    rng_seed(1u);
    scene1_bg_npc_tick();              /* first call → 180 sim passes */

    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++) {
        scene1_bg_npc_t *m = &g_scene1_bg_npc[i];
        if (m->dir != 1 && m->dir != -1)
            T_FAIL("NPC %d not spawned (dir=%d)", i, m->dir);
        if (m->type != want_type[i])
            T_FAIL("NPC %d type=%d want %d", i, m->type, want_type[i]);
        if (m->x == (float)i * 4.6f - 14.0f)
            T_FAIL("NPC %d never drifted (x still %g)", i, (double)m->x);
        if (m->pause != 0)
            T_FAIL("NPC %d pause=%d — dead path triggered", i, m->pause);
    }
    return 0;
}

/* A second tick after the warmup runs the sim exactly once (no re-warmup):
 * each live NPC moves by dir·speed·0.05, a small step. */
int test_bg_npc_tick_drifts_once(void)
{
    scene1_bg_npc_reset();
    rng_seed(3u);
    scene1_bg_npc_tick();              /* warmup */

    float x0[SCENE1_BG_NPC_COUNT];
    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++) x0[i] = g_scene1_bg_npc[i].x;

    scene1_bg_npc_tick();              /* one more sim pass */

    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++) {
        scene1_bg_npc_t *m = &g_scene1_bg_npc[i];
        float step = fabsf(m->x - x0[i]);
        /* one drift step is |speed|·0.05 ∈ [0.025, 0.05); a bounce keeps x. */
        if (step > 0.051f)
            T_FAIL("NPC %d moved %g in one tick (>1 step)", i, (double)step);
    }
    return 0;
}

/* A NPC crossing the +25 bound bounces: direction flips and exactly 4 or 5
 * LCG steps are consumed (z, vthresh-sign, vthresh-mag, mode-r1[, mode-r2]) —
 * speed + prob are NOT re-rolled. */
int test_bg_npc_respawn_rng_and_bounce(void)
{
    scene1_bg_npc_reset();
    rng_seed(7u);
    scene1_bg_npc_tick();              /* warmup → spawn cursor past count */

    /* Isolate NPC 0 on the verge of crossing +25; silence the rest. */
    for (int i = 1; i < SCENE1_BG_NPC_COUNT; i++) g_scene1_bg_npc[i].dir = 0;
    scene1_bg_npc_t *m = &g_scene1_bg_npc[0];
    m->dir = 1; m->visible = 0; m->x = 24.99f; m->z = -12.0f;
    m->speed = 0.9f; m->prob = 50; m->mode = 0; m->vthresh = 15.0f; m->pause = 0;

    uint32_t s0 = g_rng_seed;
    scene1_bg_npc_sim_once();          /* x → 25.035 > 25 → bounce */

    if (m->dir != -1)
        T_FAIL("direction not flipped on bounce: dir=%d", m->dir);
    int n = lcg_steps(s0, g_rng_seed, 64);
    if (n != 4 && n != 5)
        T_FAIL("bounce consumed %d LCG steps, want 4 or 5", n);
    if (!(m->z <= -11.0f && m->z >= -15.0f))
        T_FAIL("bounce z=%g not re-rolled into [-15,-11]", (double)m->z);
    return 0;
}

/* Reset zeroes the array + warmup latch (so a fresh scene re-warms). */
int test_bg_npc_reset_clears(void)
{
    scene1_bg_npc_reset();
    rng_seed(5u);
    scene1_bg_npc_tick();              /* spawn everything */
    scene1_bg_npc_reset();
    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++)
        if (g_scene1_bg_npc[i].dir != 0)
            T_FAIL("NPC %d not cleared by reset (dir=%d)", i, g_scene1_bg_npc[i].dir);
    return 0;
}
