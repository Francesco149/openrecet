/*
 * test_scene1_bg_npc.c — HOUSE background-window NPC sim (FUN_0046f2a3 /
 * FUN_0046f621; the subsystem formerly misnamed "ambient motes").
 *
 * Locks the RNG-stream fidelity the foot-dust parity depends on: spawn
 * consumes exactly 7 (or 8) shared-LCG steps in a fixed order, a bound-cross
 * "respawn" exactly 4 (or 5) — see scene1_bg_npc.c.  Also checks the drift, the
 * 180× entry warmup spawning all 6 NPCs, and the "stop & look through the
 * window" pause path: a leftward (dir==-1) mode==2 NPC that crosses its vthresh
 * sets the pause counter, holds anim 3 for 180 ticks, then resumes.
 */
#include "t.h"

#include <math.h>
#include <string.h>

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

/* The 180× warmup seeds all 6 NPCs with the right type table and drifts them.
 * (pause may legitimately be set on a warmup NPC that crosses its vthresh — it
 * is bounded to [0,180), not asserted 0; the dedicated crossing test below
 * exercises the pause path directly.) */
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
        if (m->pause < 0 || m->pause >= 0xb4)
            T_FAIL("NPC %d pause=%d out of [0,180)", i, m->pause);
    }
    return 0;
}

/* "Stop & look through the window": a leftward (dir==-1) mode==2 NPC that
 * crosses its vthresh (old_x > vthresh && new_x <= vthresh) sets pause=1, then
 * holds position in anim 3 until the counter reaches 180, consuming exactly one
 * shared-LCG step at expiry (the dir-flip coin).  A rightward NPC at the
 * mirror-image setup must NOT pause (the binary's asymmetric guard). */
int test_bg_npc_leftward_crossing_pauses(void)
{
    scene1_bg_npc_reset();
    rng_seed(7u);
    scene1_bg_npc_tick();              /* warmup → spawn cursor past count */
    for (int i = 1; i < SCENE1_BG_NPC_COUNT; i++) g_scene1_bg_npc[i].dir = 0;

    scene1_bg_npc_t *m = &g_scene1_bg_npc[0];
    /* poised one drift step above vthresh, moving left → will cross it. */
    m->dir = -1; m->visible = 0; m->z = -12.0f;
    m->speed = 0.9f; m->prob = 50; m->mode = 2; m->vthresh = -3.0f;
    m->x = -2.96f; m->pause = 0;

    scene1_bg_npc_sim_once();          /* x: -2.96 → -3.005 ≤ -3.0 → cross */
    if (m->pause != 1)
        T_FAIL("leftward mode-2 crossing did not set pause (pause=%d, x=%g)",
               m->pause, (double)m->x);

    /* held position + counts to 180, then clears.  x must not move while paused. */
    float held_x = m->x;
    int ticks = 0;
    while (m->pause != 0 && ticks < 256) {
        uint32_t s_before = g_rng_seed;
        scene1_bg_npc_sim_once();
        ticks++;
        if (m->pause != 0) {
            if (s_before != g_rng_seed)
                T_FAIL("pause tick %d consumed RNG before expiry", ticks);
            if (m->x != held_x)
                T_FAIL("NPC drifted while paused (x=%g want %g)",
                       (double)m->x, (double)held_x);
        }
    }
    if (ticks != 179)
        T_FAIL("pause held %d ticks, want 179 (1→180)", ticks);

    /* rightward mirror image must NOT pause (asymmetric guard). */
    scene1_bg_npc_t *r = &g_scene1_bg_npc[1];
    r->dir = 1; r->visible = 0; r->z = -12.0f;
    r->speed = 0.9f; r->prob = 50; r->mode = 2; r->vthresh = 3.0f;
    r->x = 2.96f; r->pause = 0;
    scene1_bg_npc_sim_once();
    if (r->pause != 0)
        T_FAIL("rightward mode-2 crossing wrongly set pause (pause=%d)", r->pause);
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

/* {bgnpcseed} pin (RE §21.21): latches a seed + spawn-cursor that the NEXT
 * scene1_bg_npc_tick() applies right before consuming RNG — narrower than
 * scene1_bg_npc_phasepin() (no scene1_bg_npc_reset(), so it doesn't disturb
 * db054/anim/b154/rmb elsewhere in the trace harness).  Two independent
 * checks: (a) the seed actually overrides whatever rng_seed() was called
 * before the pin — proven by matching a DIRECT reset()+rng_seed()+tick() run
 * bit-for-bit; (b) the cursor actually skips that many slots — the observed
 * savefile had cursor==1 at the true first FUN_0046f621 entry (some earlier
 * activity, e.g. a title-screen bg render, had already spawned+frozen slot 0),
 * so a seed-only pin can't reproduce which slot the real spawn starts at. */
int test_bg_npc_seed_pin_forces_seed_and_cursor(void)
{
    /* (a) seed override: pinned-from-1234-but-forced-to-99 must exactly match
     * a plain reset()+rng_seed(99)+tick() run — proving the pending seed(99)
     * fired, not the pre-pin rng_seed(1234). */
    scene1_bg_npc_reset();
    rng_seed(1234u);
    scene1_bg_npc_seed_pin(99u, 0);
    scene1_bg_npc_tick();
    uint32_t pinned_rng  = g_rng_seed;
    scene1_bg_npc_t pinned_npc0 = g_scene1_bg_npc[0];

    scene1_bg_npc_reset();
    rng_seed(99u);
    scene1_bg_npc_tick();
    if (g_rng_seed != pinned_rng)
        T_FAIL("pinned seed=99 rng=%u != direct seed=99 rng=%u",
               pinned_rng, g_rng_seed);
    if (memcmp(&pinned_npc0, &g_scene1_bg_npc[0], sizeof pinned_npc0) != 0)
        T_FAIL("pinned-seed NPC 0 differs from a direct rng_seed(99) run "
               "(the pin's seed(99) did not take effect before the warmup)");

    /* (b) cursor skip: cursor=3 must leave slots 0-2 unspawned (dir==0, the
     * "unspawned"/dead sentinel both tick and render skip) and spawn 3-5. */
    scene1_bg_npc_reset();
    rng_seed(42u);
    scene1_bg_npc_seed_pin(42u, 3);
    scene1_bg_npc_tick();
    for (int i = 0; i < 3; i++)
        if (g_scene1_bg_npc[i].dir != 0)
            T_FAIL("slot %d spawned despite cursor=3 skip (dir=%d)",
                   i, g_scene1_bg_npc[i].dir);
    for (int i = 3; i < SCENE1_BG_NPC_COUNT; i++)
        if (g_scene1_bg_npc[i].dir != 1 && g_scene1_bg_npc[i].dir != -1)
            T_FAIL("slot %d not spawned past cursor=3 (dir=%d)",
                   i, g_scene1_bg_npc[i].dir);
    return 0;
}

/* {bgnpcpin} translation: scene1_bg_npc_pin lifts each engine-record field from
 * its dword offset into the (reordered, NOT byte-compatible) port struct,
 * reinterpreting the float bits exactly.  This is the parity-critical map — a
 * wrong offset injects garbage into the LCG-timing fields. */
int test_bg_npc_pin_translates_record(void)
{
    scene1_bg_npc_reset();

    /* one synthetic engine record (BG_NPC_ENGINE_DWORDS dwords) with a known,
     * distinct value at every modelled offset (objdump map @ 0x46f2a3). */
    uint32_t rec[BG_NPC_ENGINE_DWORDS];
    memset(rec, 0xAB, sizeof rec);               /* poison the unmodelled gaps */
    for (int d = 0; d < BG_NPC_REC_DWORDS; d++)
        rec[d] = (uint32_t)(0x100 + d);          /* arec header dwords 0-10 */
    float xf = 3.5f, yf = 0.0f, zf = -12.25f, spf = 0.75f, vtf = -2.5f;
    memcpy(&rec[11], &xf,  4);   /* x       @+0x2c */
    memcpy(&rec[12], &yf,  4);   /* y       @+0x30 */
    memcpy(&rec[13], &zf,  4);   /* z       @+0x34 */
    rec[17] = (uint32_t)-1;      /* dir     @+0x44 */
    rec[18] = 0;                 /* visible @+0x48 */
    rec[19] = 4;                 /* type    @+0x4c */
    memcpy(&rec[20], &spf, 4);   /* speed   @+0x50 */
    rec[21] = 0;                 /* pause   @+0x54 */
    memcpy(&rec[22], &vtf, 4);   /* vthresh @+0x58 */
    rec[23] = 2;                 /* mode    @+0x5c */
    rec[24] = 50;                /* prob    @+0x60 */

    scene1_bg_npc_pin(rec, BG_NPC_ENGINE_DWORDS);    /* 1 record */

    scene1_bg_npc_t *m = &g_scene1_bg_npc[0];
    for (int d = 0; d < BG_NPC_REC_DWORDS; d++)
        if (m->arec[d] != (int32_t)(0x100 + d))
            T_FAIL("arec[%d]=%d want %d", d, m->arec[d], 0x100 + d);
    if (m->x != 3.5f)        T_FAIL("x=%g want 3.5", (double)m->x);
    if (m->y != 0.0f)        T_FAIL("y=%g want 0", (double)m->y);
    if (m->z != -12.25f)     T_FAIL("z=%g want -12.25", (double)m->z);
    if (m->dir != -1)        T_FAIL("dir=%d want -1", m->dir);
    if (m->visible != 0)     T_FAIL("visible=%d want 0", m->visible);
    if (m->type != 4)        T_FAIL("type=%d want 4", m->type);
    if (m->speed != 0.75f)   T_FAIL("speed=%g want 0.75", (double)m->speed);
    if (m->pause != 0)       T_FAIL("pause=%d want 0", m->pause);
    if (m->vthresh != -2.5f) T_FAIL("vthresh=%g want -2.5", (double)m->vthresh);
    if (m->mode != 2)        T_FAIL("mode=%d want 2", m->mode);
    if (m->prob != 50)       T_FAIL("prob=%d want 50", m->prob);

    /* the pin marks the warmup done + cursor>=COUNT, so the NEXT scene1_bg_npc_tick
     * runs ONE sim pass (a single drift step, x += dir*speed*0.05 = -0.0375 →
     * 3.4625) — NOT the 180x warmup (which would respawn NPC 0 far out of band)
     * and NOT a fresh spawn (cursor>=COUNT). */
    scene1_bg_npc_tick();
    if (m->x < 3.5f - 0.05f || m->x > 3.5f + 0.001f)
        T_FAIL("post-pin x=%g — warmup re-ran or wrong drift (want ~3.4625)",
               (double)m->x);
    return 0;
}
