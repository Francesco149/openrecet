/*
 * scene1_motes.c — HOUSE free-roam ambient floor motes.
 *
 * Ports FUN_0046f621 (warmup pump) + FUN_0046f2a3 (spawn/integrate) +
 * FUN_0046f648 (contact-shadow-blob render).  See scene1_motes.h for the
 * subsystem overview and the per-record field map.
 *
 * RNG fidelity (the point — findings/scene1-rng-stream-parity.md):
 *   The motes share the one global LCG (rng.c / DAT_006023a0) with the
 *   foot-dust + every gameplay roll.  Per objdump @ 0x46f2a3 they consume
 *   it in a FIXED order, and ONLY at two moments:
 *     • spawn  — 7 rolls (8 when the first mode roll misses): dir, z,
 *                speed, vthresh-sign, vthresh-mag, prob, mode-r1[, mode-r2].
 *     • respawn— 4 rolls (5 likewise): z, vthresh-sign, vthresh-mag,
 *                mode-r1[, mode-r2].  (prob + speed are NOT re-rolled.)
 *   The 180× entry warmup (FUN_0046f621) front-loads 6 spawns + 174 inert
 *   ticks onto the first HOUSE frame, exactly where retail runs it
 *   (FUN_0048670f all.c:86722).  Every roll here must match retail in count
 *   AND order or the downstream foot-dust jitter desyncs.
 *
 * Dead pause/counter path: objdump shows the per-tick "crossing" guard
 * (the ecx flag vs the X==vthresh re-check) is mutually exclusive in BOTH
 * drift directions — pause (+0x54) is never set, so the +0x54>0 branch (and
 * its lone RNG roll) never runs.  We reproduce the exact arithmetic so the
 * flag stays 0 the same way retail's does; motes are pure one-axis drifters
 * that respawn at the room bounds.  vthresh/mode are then vestigial (they
 * feed only the dead path) but are still rolled to keep the LCG in phase.
 *
 * Sprite-anim header (record +0x00..+0x14, fed to FUN_00482a51/71): a
 * deferred stub.  It drives an unported BRIGHT-sprite pass, consumes no RNG,
 * and is not read by the contact-shadow render below — so modelling only the
 * requested-anim id (+0x14 / flip +0x18) is behaviour-identical here.
 */

#include "scene1_motes.h"

#include "call_trace.h"
#include "math3d.h"
#include "rng.h"

/* DAT_005c7dd8 — per-mote sprite-type index, indexed by spawn idx % 6. */
static const int MOTE_TYPE_TABLE[SCENE1_MOTE_COUNT] = { 0, 1, 6, 7, 9, 8 };

scene1_mote_t g_scene1_motes[SCENE1_MOTE_COUNT];

/* DAT_073a8bb4 (spawn cursor) / DAT_073a8bb8 (warmup latch) /
 * DAT_073a8bb0 (frame counter). */
static int g_mote_spawn_cursor;
static int g_mote_warmup_done;
static int g_mote_frame;

void scene1_motes_reset(void)
{
    for (int i = 0; i < SCENE1_MOTE_COUNT; i++) {
        scene1_mote_t z = (scene1_mote_t){0};
        g_scene1_motes[i] = z;
    }
    g_mote_spawn_cursor = 0;
    g_mote_warmup_done  = 0;
    g_mote_frame        = 0;
}

/* FUN_0046f2a3 spawn block (objdump 0x46f2bd-0x46f3e2): seed one mote. */
static void mote_spawn(int idx)
{
    scene1_mote_t *m = &g_scene1_motes[idx];

    m->visible = 0;                                   /* +0x48 */
    m->type    = MOTE_TYPE_TABLE[idx % SCENE1_MOTE_COUNT]; /* +0x4c */

    /* dir sign (+0x44): rng15 & 1 → odd:+1 even:-1.  The fld of -15/25 that
     * follows in the engine is a dead store into x (overwritten next). */
    m->dir = (rng_next15() & 1) ? 1 : -1;

    m->x = (float)idx * 4.6f - 14.0f;                 /* +0x2c */
    m->y = 0.0f;                                      /* +0x30 */
    m->z = -11.0f - rng_next_unit() * 4.0f;           /* +0x34 */

    m->flip    = 1;                                   /* +0x18 */
    m->anim_id = -1;                                  /* +0x14 */

    m->speed = (rng_next_unit() + 1.0f) * 0.5f;       /* +0x50, [0.5,1.0) */

    /* vthresh sign (+0x58): rng15 & 1 chooses negative/positive magnitude. */
    if (rng_next15() & 1)
        m->vthresh = -(rng_next_unit() * 6.0f + 1.0f);   /* [-7,-1) */
    else
        m->vthresh = rng_next_unit() * 5.0f + 13.0f;     /* [13,18) */

    m->prob = (int)(rng_next15() % 100);              /* +0x60 */

    /* mode (+0x5c): two/three weighted rolls off prob. */
    int r1 = (int)(rng_next15() % 100);
    if (r1 < m->prob / 2) {
        m->mode = 2;
    } else {
        int r2 = (int)(rng_next15() % 100);
        m->mode = (r2 < m->prob) ? 1 : 0;
    }

    m->pause = 0;                                     /* +0x54 (BSS-zero) */
}

/* FUN_0046f2a3 respawn block (objdump 0x46f566-0x46f5ea): re-seed depth +
 * velocity threshold + mode, flip drift direction.  Speed + prob persist. */
static void mote_respawn(scene1_mote_t *m)
{
    m->z   = -11.0f - rng_next_unit() * 4.0f;
    m->dir = -m->dir;

    if (rng_next15() & 1)
        m->vthresh = -(rng_next_unit() * 6.0f + 1.0f);
    else
        m->vthresh = rng_next_unit() * 5.0f + 13.0f;

    int r1 = (int)(rng_next15() % 100);
    if (r1 < m->prob / 2) {
        m->mode = 2;
    } else {
        int r2 = (int)(rng_next15() % 100);
        m->mode = (r2 < m->prob) ? 1 : 0;
    }
}

/* FUN_0046f2a3 tick loop body (objdump 0x46f400-0x46f5ed): integrate one
 * mote.  Anim stepping (FUN_00482a51/71) is the deferred sprite-pass stub. */
static void mote_tick(scene1_mote_t *m)
{
    if (m->dir == 0) {
        /* unspawned slot → engine requests anim 0 then falls to render-step;
         * no drift, no RNG.  (All 6 are spawned by the warmup before the
         * first render, so this only matters mid-warmup.) */
        return;
    }

    if (m->pause > 0) {
        /* +0x54>0 pause/blink state.  DEAD in practice (pause is never set —
         * see file header), reproduced for completeness.  Would: hold, count
         * to 180, then maybe flip dir off one rng15. */
        if (++m->pause == 0xb4) {
            m->pause = 0;
            if (rng_next15() & 1)
                m->dir = -m->dir;
        }
        return;
    }

    /* drift along X by dir·speed·0.05 (engine fild dir → fmul speed → fmul
     * 0.05 → fadd x).  flip records the sprite facing for the bright pass. */
    int respawn = 0;
    if (m->dir == 1) {
        m->x += (float)m->dir * m->speed * 0.05f;
        m->flip = 6;
        if (m->x > 25.0f) respawn = 1;
        /* dead pause set: (mode>=2 && x<vthresh && x>=vthresh) — impossible. */
    } else {                                  /* dir == -1 */
        m->x += (float)m->dir * m->speed * 0.05f;
        m->flip = 2;
        if (m->x < -15.0f) respawn = 1;
        /* dead pause set: (mode>=2 && x>vthresh && x<=vthresh) — impossible. */
    }

    if (respawn)
        mote_respawn(m);
}

/* FUN_0046f2a3 (0x46f2a3, 894 B). */
void scene1_motes_sim_once(void)
{
    CALL_TRACE_ENTER(0x46f2a3u);

    /* spawn block — engine gates it on the sim-freeze flag (DAT_0438b4e0==0),
     * which is 0 in steady free-roam.  Spawns one mote per call until the
     * cursor reaches the count; the cursor then keeps incrementing (engine
     * inc DWORD ds:0x73a8bb4 every unfrozen call) but seeds nothing more. */
    if (g_mote_spawn_cursor < SCENE1_MOTE_COUNT)
        mote_spawn(g_mote_spawn_cursor);
    g_mote_spawn_cursor++;

    /* tick loop — runs over all SCENE1_MOTE_COUNT records every call,
     * unspawned slots included (they no-op). */
    for (int i = 0; i < SCENE1_MOTE_COUNT; i++)
        mote_tick(&g_scene1_motes[i]);

    g_mote_frame++;
}

/* FUN_0046f621 (0x46f621, 39 B): first call pumps the sim 180× (DAT_073a8bb8
 * latch), every later call once. */
void scene1_motes_tick(void)
{
    CALL_TRACE_ENTER(0x46f621u);

    int n = 1;
    if (!g_mote_warmup_done) {
        g_mote_warmup_done = 1;
        n = 0xb4;                                     /* 180 */
    }
    for (; n != 0; n--)
        scene1_motes_sim_once();
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

/* The shared ±256 object-space quad, engine DAT_0064bd88 — the SAME blob the
 * ground-shadow pass draws (scene1_chr_shadow.c SHADOW_QUAD).  Its UVs sample
 * the 64×64 shade.bmp the shadow envelope leaves bound; only the diffuse
 * colour is patched per draw (engine FUN_0040d11b).  See that file for the
 * UV derivation. */
typedef struct { float x, y, z; uint32_t color; float u, v; } mote_vertex;

#define MOTE_UV_LO  0.001953125f   /*  0.5/256 */
#define MOTE_UV_HI  0.248046875f   /* 63.5/256 */

static const mote_vertex MOTE_QUAD[4] = {
    { -256.0f, 0.0f,  256.0f, 0xffffffffu, MOTE_UV_LO, MOTE_UV_LO },
    { -256.0f, 0.0f, -256.0f, 0xffffffffu, MOTE_UV_LO, MOTE_UV_HI },
    {  256.0f, 0.0f,  256.0f, 0xffffffffu, MOTE_UV_HI, MOTE_UV_LO },
    {  256.0f, 0.0f, -256.0f, 0xffffffffu, MOTE_UV_HI, MOTE_UV_HI },
};

/* FUN_0046f648 (0x46f648, 239 B).  Called from FUN_00470385 @ the
 * FUN_0045aa36 L122 slot, AFTER the player/companion ground shadows — so the
 * shadow pass's render-state envelope (shade.bmp texture, ZWRITE off,
 * multiplicative-darken blend SRCBLEND=ZERO/DESTBLEND=SRCCOLOR) is still
 * active.  Each mote is therefore a soft dark floor blob: shade.bmp tinted
 * 0xff202020 and multiplied into the floor.  Sets its own WORLD transform +
 * diffuse colour only; everything else is inherited. */
void scene1_motes_render(struct IDirect3DDevice8 *dev_in)
{
    CALL_TRACE_ENTER(0x46f648u);

    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* engine gate: DAT_0438b4e0==0 (sim not frozen) && DAT_0438b188==0
     * (render not suppressed) && count!=0 — all hold in steady free-roam. */
    for (int i = 0; i < SCENE1_MOTE_COUNT; i++) {
        const scene1_mote_t *m = &g_scene1_motes[i];
        if (m->visible == -1)                         /* +0x48 hidden */
            continue;

        /* world = Scaling(-0.0046, 0.0046, 0.0046) · Translation(x, y+0.08, z)
         * (engine: thunk_FUN_004a3462 translate, _004a33d2 scale, _004a2a03
         * multiply scale·translate). */
        float trans[16], scale[16], world[16];
        mat4_translation(trans, m->x, m->y + 0.08f, m->z);
        mat4_scaling(scale, -0.0046f, 0.0046f, 0.0046f);
        mat4_mul(world, scale, trans);

        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)world);

        mote_vertex quad[4];
        for (int v = 0; v < 4; v++) {
            quad[v] = MOTE_QUAD[v];
            quad[v].color = 0xff202020u;              /* engine FUN_0040d11b */
        }
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2,
                                         quad, sizeof(mote_vertex));
    }
}

#endif /* _WIN32 */
