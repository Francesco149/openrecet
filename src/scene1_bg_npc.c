/*
 * scene1_bg_npc.c — HOUSE background-window NPCs (the townsfolk drifting past
 * the shop's back window).  See scene1_bg_npc.h for the subsystem overview and
 * the naming history (this was the misnamed "ambient floor motes").
 *
 * Ports FUN_0046f621 (warmup pump) + FUN_0046f2a3 (spawn/integrate/anim-step) +
 * FUN_0046f648 (dark contact-shadow render) + FUN_0046f737 (bright character-
 * sprite render).
 *
 * RNG fidelity (findings/scene1-rng-stream-parity.md):
 *   The NPCs share the one global LCG (rng.c / DAT_006023a0) with the foot-dust
 *   + every gameplay roll.  Per objdump @ 0x46f2a3 they consume it in a FIXED
 *   order, and ONLY at two moments:
 *     • spawn  — 7 rolls (8 when the first mode roll misses): dir, z, speed,
 *                vthresh-sign, vthresh-mag, prob, mode-r1[, mode-r2].
 *     • respawn— 4 rolls (5 likewise): z, vthresh-sign, vthresh-mag,
 *                mode-r1[, mode-r2].  (prob + speed are NOT re-rolled.)
 *   The 180× entry warmup (FUN_0046f621) front-loads 6 spawns + 174 inert
 *   ticks onto the first HOUSE frame.  The sprite-anim stepping
 *   (bg_npc_anim_set / chr_anim_tick) consumes NO RNG, so it does not perturb
 *   the stream.
 *
 * Pause / "stop & look through the window" path (CORRECTED 2026-06-03 — an
 * earlier note here wrongly called this dead).  A `mode==2` NPC that is drifting
 * LEFT (dir == -1) and crosses its per-NPC threshold (+0x58 `vthresh`) sets the
 * pause counter (+0x54), enters anim 3 for 180 ticks, then flips direction and
 * resumes.  This is the townsperson who briefly stops and faces the window.
 * The crossing test is ASYMMETRIC in the binary (objdump 0x46f4bf-0x46f559):
 *   • dir == -1: the old-x compare is captured BEFORE the x update, so the guard
 *     is a genuine downward crossing  `old_x > vthresh && new_x <= vthresh`  →
 *     pause IS set.
 *   • dir == +1: x is updated BEFORE both compares, so the guard reduces to
 *     `new_x < vthresh && new_x >= vthresh` — self-contradictory → pause is
 *     never set.  We faithfully reproduce that asymmetry (rightward NPCs never
 *     pause; only leftward ones do).
 * The lone RNG roll in the pause path fires once, when the counter reaches 180
 * (a dir-flip coin); it therefore DOES perturb the shared LCG stream — relevant
 * to free-roam RNG-stream parity (findings/scene1-rng-stream-parity.md).
 */

#include "scene1_bg_npc.h"

#include <string.h>

#include "call_trace.h"
#include "math3d.h"
#include "rng.h"
#include "scene1_chr_sprite.h"   /* chr_anim_tick (FUN_00482a71) + CHR_ACTOR_* */

/* DAT_005c7dd8 — per-NPC sprite-type index, indexed by spawn idx % 6. */
static const int BG_NPC_TYPE_TABLE[SCENE1_BG_NPC_COUNT] = { 0, 1, 6, 7, 9, 8 };

/* DAT_005c7ce0 — static (char_id, key) registry; the bright render maps a
 * record's type → sprite-sheet char id via DAT_005c7ce0[type*2].  Dumped from
 * the unpacked exe @ 0x5c7ce0 (entries 0..17, then a 0xffffffff terminator).
 * Only the char-id (first element of each pair) is needed here. */
static const int BG_NPC_TYPE_CHAR[18] = {
    0x0a, 0x23, 0x25, 0x1d, 0x1c, 0x20, 0x27, 0x24, 0x26, 0x25,
    0x1e, 0x1f, 0x28, 0x29, 0x2a, 0x2b, 0x30, 0x42,
};

int scene1_bg_npc_type_to_char(int type)
{
    if (type < 0 || type >= (int)(sizeof BG_NPC_TYPE_CHAR / sizeof *BG_NPC_TYPE_CHAR))
        return -1;
    return BG_NPC_TYPE_CHAR[type];
}

scene1_bg_npc_t g_scene1_bg_npc[SCENE1_BG_NPC_COUNT];

/* DAT_073a8bb4 (spawn cursor) / DAT_073a8bb8 (warmup latch) /
 * DAT_073a8bb0 (frame counter). */
static int g_bg_npc_spawn_cursor;
static int g_bg_npc_warmup_done;
static int g_bg_npc_frame;

/* Trace-harness {phasepin}/{bgnpcseed} latch: when set, the NEXT
 * scene1_bg_npc_tick() forces the shared LCG to g_bg_npc_pin_seed right before
 * consuming RNG, so the 6-NPC layout is reproduced from a known RNG origin
 * regardless of how the live stream got here.  See scene1_bg_npc_phasepin()
 * (canonical 19937, re-arms the warmup) and scene1_bg_npc_seed_pin() (an
 * arbitrary captured seed, no re-arm — RE §21.21). */
static int      g_bg_npc_pin_pending;
static uint32_t g_bg_npc_pin_seed;
static int      g_bg_npc_pin_cursor;

/* Raw-dword records for the "dead" slots [0, g_bg_npc_pin_cursor) — see
 * bg_npc_write_record_from_dwords / scene1_bg_npc_seed_pin's `dead_soa`.
 * A slot the warmup cursor skips (dir stays 0 forever) is still drawn by the
 * shadow pass (which only checks `visible==-1`, not `dir==0` — RE §21.22), so
 * its leftover x/y/z must match retail's, not the port's BSS-zero default. */
static uint32_t g_bg_npc_pin_dead_soa[SCENE1_BG_NPC_COUNT * BG_NPC_ENGINE_DWORDS];
static int      g_bg_npc_pin_dead_count;

void scene1_bg_npc_reset(void)
{
    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++) {
        scene1_bg_npc_t z = (scene1_bg_npc_t){0};
        g_scene1_bg_npc[i] = z;
    }
    g_bg_npc_spawn_cursor = 0;
    g_bg_npc_warmup_done  = 0;
    g_bg_npc_frame        = 0;
}

/* Forward decl — defined near scene1_bg_npc_pin() below; used earlier by
 * scene1_bg_npc_tick()'s {bgnpcseed} dead-slot injection. */
static void bg_npc_write_record_from_dwords(int idx, const uint32_t *r);

/* FUN_00482a51 (0x482a51, 32 B): set the sprite-anim header's current anim.
 * On a change it resets frame/counter/timer; `anim` is stored in BOTH the
 * anim slot (dword 0) and the "last anim" slot (CHR_ACTOR_STATE / dword 5)
 * the change-detect compares. */
static void bg_npc_anim_set(int32_t *arec, int anim)
{
    if (arec[CHR_ACTOR_STATE] != anim) {
        arec[CHR_ACTOR_FRAME]   = 0;
        arec[CHR_ACTOR_COUNTER] = 0;
        arec[CHR_ACTOR_STATE]   = anim;
        arec[CHR_ACTOR_ANIM]    = anim;
        arec[CHR_ACTOR_TIMER]   = 0;
    }
}

/* FUN_0046f2a3 spawn block (objdump 0x46f2bd-0x46f3e2): seed one NPC. */
static void bg_npc_spawn(int idx)
{
    scene1_bg_npc_t *m = &g_scene1_bg_npc[idx];

    /* sprite-anim header init (engine writes +0x14 state = -1, +0x18 facing = 1,
     * +0x1c/+0x20/+0x24 flags = 0; anim/frame/counter/timer come up via the
     * first bg_npc_anim_set when the state changes off -1). */
    for (int d = 0; d < BG_NPC_REC_DWORDS; d++) m->arec[d] = 0;
    m->arec[CHR_ACTOR_STATE]  = -1;   /* +0x14 (was the named anim_id) */
    m->arec[CHR_ACTOR_FACING] = 1;    /* +0x18 (was the named flip) */

    m->visible = 0;                                   /* +0x48 */
    m->type    = BG_NPC_TYPE_TABLE[idx % SCENE1_BG_NPC_COUNT]; /* +0x4c */

    /* dir sign (+0x44): rng15 & 1 → odd:+1 even:-1.  The fld of -15/25 that
     * follows in the engine is a dead store into x (overwritten next). */
    m->dir = (rng_next15() & 1) ? 1 : -1;

    m->x = (float)idx * 4.6f - 14.0f;                 /* +0x2c */
    m->y = 0.0f;                                      /* +0x30 */
    m->z = -11.0f - rng_next_unit() * 4.0f;           /* +0x34 */

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
static void bg_npc_respawn(scene1_bg_npc_t *m)
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

/* Anim-mode selector (engine FUN_0046f2a3 active branch): the anim id handed to
 * bg_npc_anim_set for a drifting NPC — 1 normally, 2 inside a vthresh window. */
static int bg_npc_drift_anim_mode(const scene1_bg_npc_t *m)
{
    if (m->mode < 1)
        return 1;
    if ((m->x <= -7.0f || -1.0f <= m->x) && (m->x <= 13.0f || 18.0f <= m->x))
        return 1;
    return 2;
}

/* FUN_0046f2a3 tick loop body (objdump 0x46f400-0x46f5ed): integrate one NPC,
 * then step its sprite-anim header.  The engine sets the anim (FUN_00482a51)
 * inside the state branch, then advances the frame (FUN_00482a71 ==
 * chr_anim_tick) unconditionally at loop end. */
static void bg_npc_tick(scene1_bg_npc_t *m)
{
    if (m->dir == 0) {
        /* unspawned slot → engine requests anim 0 (no drift, no RNG). */
        bg_npc_anim_set(m->arec, 0);
    } else if (m->pause > 0) {
        /* +0x54>0 "stopped & looking" state (objdump 0x46f429-0x46f45f): hold
         * anim 3, count the pause up to 180, then clear it and maybe flip the
         * drift direction off one rng15 bit.  Reached when the drift branch
         * below trips the leftward vthresh crossing. */
        bg_npc_anim_set(m->arec, 3);
        if (++m->pause == 0xb4) {
            m->pause = 0;
            if (rng_next15() & 1)
                m->dir = -m->dir;
        }
    } else {
        bg_npc_anim_set(m->arec, bg_npc_drift_anim_mode(m));

        /* drift along X by dir·speed·0.05.  arec[FACING] records the sprite
         * facing (the engine's "flip": 6 for dir+, 2 for dir-).  A mode==2 NPC
         * that crosses its `vthresh` enters the pause/look state — but the
         * crossing test is asymmetric in the binary (see file header). */
        int respawn = 0;
        if (m->dir == 1) {
            /* rightward: x is updated before both vthresh compares → the guard
             * `new_x < vthresh && new_x >= vthresh` can never hold, so a
             * rightward NPC never pauses (objdump 0x46f4bf-0x46f50c). */
            m->x += (float)m->dir * m->speed * 0.05f;
            m->arec[CHR_ACTOR_FACING] = 6;
            if (m->x > 25.0f) respawn = 1;
        } else {                                  /* dir == -1 */
            /* leftward: capture old_x BEFORE the update so the guard is a real
             * downward crossing `old_x > vthresh && new_x <= vthresh`; on a
             * mode==2 NPC that sets pause → stop & look (objdump 0x46f50e-0x46f559). */
            float old_x = m->x;
            m->x += (float)m->dir * m->speed * 0.05f;
            m->arec[CHR_ACTOR_FACING] = 2;
            if (m->x < -15.0f) respawn = 1;
            if (m->mode >= 2 && old_x > m->vthresh && m->x <= m->vthresh)
                m->pause = 1;
        }
        if (respawn)
            bg_npc_respawn(m);
    }

    /* FUN_00482a71 frame advance (== chr_anim_tick), dt = 1.0, char = the
     * record's mapped sprite-sheet id.  No-op when no formdata is loaded
     * (host tests) — chr_meta_lut returns 0 safely. */
    chr_anim_tick(m->arec, scene1_bg_npc_type_to_char(m->type), 1.0f);
}

/* FUN_0046f2a3 (0x46f2a3, 894 B). */
void scene1_bg_npc_sim_once(void)
{
    CALL_TRACE_ENTER(0x46f2a3u);

    /* spawn block — engine gates it on the sim-freeze flag (DAT_0438b4e0==0),
     * which is 0 in steady free-roam.  Spawns one NPC per call until the cursor
     * reaches the count; the cursor then keeps incrementing but seeds nothing. */
    if (g_bg_npc_spawn_cursor < SCENE1_BG_NPC_COUNT)
        bg_npc_spawn(g_bg_npc_spawn_cursor);
    g_bg_npc_spawn_cursor++;

    /* tick loop — runs over all SCENE1_BG_NPC_COUNT records every call. */
    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++)
        bg_npc_tick(&g_scene1_bg_npc[i]);

    g_bg_npc_frame++;
}

/* FUN_0046f621 (0x46f621, 39 B): first call pumps the sim 180× (DAT_073a8bb8
 * latch), every later call once. */
void scene1_bg_npc_tick(void)
{
    CALL_TRACE_ENTER(0x46f621u);

    /* {phasepin}: force the shared LCG to the canonical seed at the warmup
     * boundary (see scene1_bg_npc_phasepin).  Doing it HERE — at the exact
     * instant the re-armed warmup is about to consume RNG — makes the layout
     * independent of any RNG consumed between the pre-sim pin and this tick. */
    if (g_bg_npc_pin_pending) {
        g_bg_npc_pin_pending = 0;
        rng_seed(g_bg_npc_pin_seed);
        g_bg_npc_spawn_cursor = g_bg_npc_pin_cursor;
        /* Slots [0, cursor) are "dead" (the warmup will never spawn them —
         * dir stays 0 forever) but the shadow pass still draws them (RE
         * §21.22: it only checks visible==-1, not dir==0), so their leftover
         * x/y/z must match retail's, not the port's BSS-zero default. */
        for (int i = 0; i < g_bg_npc_pin_dead_count; i++)
            bg_npc_write_record_from_dwords(
                i, &g_bg_npc_pin_dead_soa[(size_t)i * BG_NPC_ENGINE_DWORDS]);
    }

    int n = 1;
    if (!g_bg_npc_warmup_done) {
        g_bg_npc_warmup_done = 1;
        n = 0xb4;                                     /* 180 */
    }
    for (; n != 0; n--)
        scene1_bg_npc_sim_once();
}

/* Trace-harness `{phasepin}` for the background-window NPCs.  The 6 NPCs ride
 * the shared global LCG (rng.c); their on-screen positions/identities at any
 * dialogue line therefore depend on the WHOLE prior RNG-consumption history,
 * which differs port↔retail by the load-dependent intro phase (the deferred
 * RNG-stream-completeness problem, findings/scene1-rng-stream-parity.md).  So a
 * raw port↔retail capture shows the window NPCs at different drift states even
 * though the spawn/drift LOGIC is bit-faithful.
 *
 * This normalizes them the same way db054/b154/rmb are normalized: re-arm the
 * warmup (clear records + cursor + latch) and latch a pending re-seed so the
 * next per-frame scene1_bg_npc_tick() re-runs the REAL 180x warmup from the
 * canonical seed SCENE1_BG_NPC_PHASEPIN_SEED.  Both sides run their own warmup
 * from the identical seed, so the captured layout is reproducible and 1:1 — and
 * because each side uses its own spawn/drift code, a post-pin MISMATCH would
 * expose a genuine logic divergence (this pin doubles as the parity test).
 *
 * The retail agent mirrors this (zero the SoA DAT_073a7f80 + cursor DAT_073a8bb4
 * + latch DAT_073a8bb8, force the spawn-gate DAT_0438b4e0=0, and seed
 * DAT_006023a0 at the FUN_0046f621 entry). */
void scene1_bg_npc_phasepin(void)
{
    scene1_bg_npc_reset();
    g_bg_npc_pin_seed     = SCENE1_BG_NPC_PHASEPIN_SEED;
    g_bg_npc_pin_cursor   = 0;
    g_bg_npc_pin_dead_count = 0;
    g_bg_npc_pin_pending  = 1;
}

/* Trace-harness `{bgnpcseed}` (RE §21.21/§21.22) — see scene1_bg_npc.h.
 * Narrower than scene1_bg_npc_phasepin(): no scene1_bg_npc_reset(), so it is
 * meant to land BEFORE the warmup's NATURAL first-ever firing (nothing to
 * re-arm yet) rather than to re-run an already-completed one.
 *
 * `dead_soa`/`dead_n_dwords` (optional, NULL/0 to skip): raw engine records
 * (BG_NPC_ENGINE_DWORDS dwords each, {bgnpcpin}'s format) for the slots
 * [0, cursor) the warmup will never spawn.  Needed because the shadow pass
 * draws them anyway (visible==-1-only check, RE §21.22) — without this their
 * x/y/z stay the port's BSS-zero default instead of retail's leftover
 * position, drawing a stray shadow at the world origin. */
void scene1_bg_npc_seed_pin(uint32_t seed, int cursor,
                            const uint32_t *dead_soa, size_t dead_n_dwords)
{
    g_bg_npc_pin_seed    = seed;
    g_bg_npc_pin_cursor  = cursor;

    int dead_count = dead_soa ? (int)(dead_n_dwords / BG_NPC_ENGINE_DWORDS) : 0;
    if (dead_count > SCENE1_BG_NPC_COUNT)
        dead_count = SCENE1_BG_NPC_COUNT;
    g_bg_npc_pin_dead_count = dead_count;
    if (dead_count > 0)
        memcpy(g_bg_npc_pin_dead_soa, dead_soa,
               (size_t)dead_count * BG_NPC_ENGINE_DWORDS * sizeof(uint32_t));

    g_bg_npc_pin_pending = 1;
}

/* Trace-harness `{bgnpcpin}` — overwrite the live NPC array from retail's
 * CAPTURED NATURAL SoA (DAT_073a7f80) at the pin anchor.  See scene1_bg_npc.h.
 *
 * Why this and not {phasepin}'s warmup re-seed: the rng-consumer survey
 * (findings/customer-service-haggle-RE.md §21.1) needs the port to match the
 * RECORDING's bg-NPC positions, not a synthetic 19937-layout.  The 6 NPCs sit at
 * different positions port↔retail because the warmup (FUN_0046f621) seeded off a
 * different LCG origin (the intro skip), so boundary-respawns (which each consume
 * 4-5 LCG draws) cross on different frames → the whole shared stream drifts at
 * the off-7 first respawn.  Snapshotting retail's real drifted state at the
 * anchor makes the positions identical; from there both sides drift in lockstep
 * (the spawn/respawn logic is bit-faithful, the LCG is {rngseed}-pinned), so the
 * downstream cs_pick_line %2 variant draw lands on retail's parity.
 *
 * `soa` is `n_dwords` of raw little-endian engine dwords = (n_dwords /
 * BG_NPC_ENGINE_DWORDS) records of 0x64 bytes.  The port struct is NOT
 * byte-compatible with that record (different field order), so each field is
 * lifted from its engine dword offset; floats are bit-reinterpreted via memcpy. */

/* Shared by scene1_bg_npc_pin() and scene1_bg_npc_seed_pin()'s `dead_soa`:
 * lift one raw engine record (BG_NPC_ENGINE_DWORDS dwords) into g_scene1_bg_npc[idx]. */
static void bg_npc_write_record_from_dwords(int idx, const uint32_t *r)
{
    scene1_bg_npc_t *m = &g_scene1_bg_npc[idx];

    /* dwords 0-10: chr-actor sprite-state header, copied verbatim. */
    for (int d = 0; d < BG_NPC_REC_DWORDS; d++)
        m->arec[d] = (int32_t)r[d];

    /* drift/respawn fields at their engine dword offsets (the fields that
     * decide WHEN each NPC next draws the shared LCG → must be bit-exact;
     * floats reinterpreted via memcpy, no aliasing pun). */
    memcpy(&m->x,       &r[11], sizeof m->x);        /* +0x2c */
    memcpy(&m->y,       &r[12], sizeof m->y);        /* +0x30 */
    memcpy(&m->z,       &r[13], sizeof m->z);        /* +0x34 */
    m->dir     = (int32_t)r[17];                     /* +0x44 */
    m->visible = (int32_t)r[18];                     /* +0x48 */
    m->type    = (int32_t)r[19];                     /* +0x4c */
    memcpy(&m->speed,   &r[20], sizeof m->speed);    /* +0x50 */
    m->pause   = (int32_t)r[21];                     /* +0x54 */
    memcpy(&m->vthresh, &r[22], sizeof m->vthresh);  /* +0x58 */
    m->mode    = (int32_t)r[23];                     /* +0x5c */
    m->prob    = (int32_t)r[24];                     /* +0x60 */
}

void scene1_bg_npc_pin(const uint32_t *soa, size_t n_dwords)
{
    if (!soa)
        return;
    int n_records = (int)(n_dwords / BG_NPC_ENGINE_DWORDS);
    if (n_records > SCENE1_BG_NPC_COUNT)
        n_records = SCENE1_BG_NPC_COUNT;

    for (int i = 0; i < n_records; i++)
        bg_npc_write_record_from_dwords(i, soa + (size_t)i * BG_NPC_ENGINE_DWORDS);

    /* the warmup is long done at any real pin anchor; force the bookkeeping so
     * the next scene1_bg_npc_tick() neither re-runs the 180x warmup nor seeds a
     * fresh NPC (cursor >= COUNT gates the spawn block off — behaviourally
     * identical to retail's larger post-warmup cursor, RE §21.1). */
    g_bg_npc_warmup_done  = 1;
    g_bg_npc_spawn_cursor = SCENE1_BG_NPC_COUNT;
}

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include "scene1_camera.h"   /* g_scene1_camera_orient (= engine DAT_0438cdf8) */
#include "scene1_preload.h"  /* scene1_preload_chr_sheet / _load_chr_sheet */
#include "sprite.h"          /* sprite_t */

/* ── dark contact-shadow render (FUN_0046f648) ───────────────────────────────
 * The shared ±256 object-space quad, engine DAT_0064bd88 — the SAME blob the
 * ground-shadow pass draws (scene1_chr_shadow.c SHADOW_QUAD).  Its UVs sample
 * the 64×64 shade.bmp the shadow envelope leaves bound; only the diffuse colour
 * is patched per draw (engine FUN_0040d11b).  See that file for the UV
 * derivation. */
typedef struct { float x, y, z; uint32_t color; float u, v; } bg_npc_shadow_vertex;

#define BG_NPC_UV_LO  0.001953125f   /*  0.5/256 */
#define BG_NPC_UV_HI  0.248046875f   /* 63.5/256 */

static const bg_npc_shadow_vertex BG_NPC_SHADOW_QUAD[4] = {
    { -256.0f, 0.0f,  256.0f, 0xffffffffu, BG_NPC_UV_LO, BG_NPC_UV_LO },
    { -256.0f, 0.0f, -256.0f, 0xffffffffu, BG_NPC_UV_LO, BG_NPC_UV_HI },
    {  256.0f, 0.0f,  256.0f, 0xffffffffu, BG_NPC_UV_HI, BG_NPC_UV_LO },
    {  256.0f, 0.0f, -256.0f, 0xffffffffu, BG_NPC_UV_HI, BG_NPC_UV_HI },
};

/* FUN_0046f648 (0x46f648, 239 B).  Called from FUN_00470385 @ the FUN_0045aa36
 * L122 slot, AFTER the player/companion ground shadows — so the shadow pass's
 * render-state envelope (shade.bmp texture, ZWRITE off, multiplicative-darken
 * blend SRCBLEND=ZERO/DESTBLEND=SRCCOLOR) is still active.  Each NPC is
 * therefore a soft dark floor blob multiplied into the street. */
void scene1_bg_npc_shadow_render(struct IDirect3DDevice8 *dev_in)
{
    CALL_TRACE_ENTER(0x46f648u);

    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++) {
        const scene1_bg_npc_t *m = &g_scene1_bg_npc[i];
        if (m->visible == -1)                         /* +0x48 hidden */
            continue;

        /* world = Scaling(-0.0046, 0.0046, 0.0046) · Translation(x, y+0.08, z). */
        float trans[16], scale[16], world[16];
        mat4_translation(trans, m->x, m->y + 0.08f, m->z);
        mat4_scaling(scale, -0.0046f, 0.0046f, 0.0046f);
        mat4_mul(world, scale, trans);

        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD, (const D3DMATRIX *)world);

        bg_npc_shadow_vertex quad[4];
        for (int v = 0; v < 4; v++) {
            quad[v] = BG_NPC_SHADOW_QUAD[v];
            quad[v].color = 0xff202020u;              /* engine FUN_0040d11b */
        }
        IDirect3DDevice8_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2,
                                         quad, sizeof(bg_npc_shadow_vertex));
    }
}

/* ── bright character-sprite render (FUN_0046f737, 0x46f737, 347 B) ───────────
 * Per active NPC: world = billboard(DAT_0438cdf8) × Scale(0.03) × Translate(pos),
 * bind sheet DAT_073a9b18[char] (char = DAT_005c7ce0[type*2]), draw via the
 * shared chr-sprite leaf at colour 0xff7f7f7f.  The engine sets NO render state
 * in FUN_0046f737 / FUN_0045a56f — both inherit the surrounding shop-walker
 * sprite envelope.  We set a self-contained envelope so the draw is correct
 * regardless of the port's (deliberately divergent) pass state.
 *
 * Z-WRITE (2026-06-03, user-flagged "NPCs overlap in weird ways more than
 * retail"): retail draws these sprites with ZWRITEENABLE=TRUE.  Ground truth =
 * runs/walkdust-d3d frame 5495, the FUN_0045a56f leaf draws (ret_va 0x45aa31):
 * the player (z≈+8.7), companion (z≈+9.3) and all five bg-NPCs (z≈−11..−15) draw
 * at ZEN1 ZWR1, ALPHAREF0 + ALPHAFUNC=GREATER (alpha>0, transparent texels
 * culled from the Z-write), SRCALPHA/INVSRCALPHA; only Tear's additive wing-glow
 * (ONE/ONE) is ZWR0.  With ZWRITE off, our NPCs were painter-ordered (spawn idx)
 * instead of depth-sorted → the weird overlaps.  We restore retail's ZWR1 (and
 * its GREATER alpha-test, so overlapping NPCs don't Z-punch square holes).
 *
 * Why this is SAFE here but NOT for the player sprite (which stays deferred —
 * the b1acf7c regression): the bg-NPCs are FAR (z≈−11..−15, behind the window).
 * Everything drawn AFTER the sprite pass (wing-glow + foot-dust, both z-tested)
 * sits at the player's NEAR depth (z≈+9), so it passes the depth test over the
 * NPCs' far Z-writes and is unaffected; the NPC + furniture shadows already drew
 * BEFORE the sprites.  The near player/companion ZWR1 is the dangerous one — its
 * Z lands at the same depth as the glow/dust drawn right after, and Tear's pose
 * isn't yet 1:1, so it occludes her own glow (confirmed-parity-ledger.md).  We
 * restore ZWRITE=FALSE at function exit so the trailing passes are untouched. */
#define BG_NPC_SPRITE_SCALE 0.03f

void scene1_bg_npc_sprite_render(struct IDirect3DDevice8 *dev_in)
{
    CALL_TRACE_ENTER(0x46f737u);

    if (dev_in == NULL)
        return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* sprite render-state envelope (mirrors sw_pass_light's chr billboard set). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);
    /* retail AFUNC=5 = D3DCMP_GREATER (NOT GreaterEqual): with ref 0 the test is
     * alpha > 0, so fully-transparent texels FAIL and do not write Z.  Critical
     * now that ZWRITE is on: GreaterEqual (alpha >= 0, pass-all) made the whole
     * transparent quad lay down a Z footprint → square cut-outs where NPC sprites
     * overlap (the same "transparent quad = invisible occluding rectangle" bug as
     * the player b1acf7c).  GREATER restricts Z-writes to the opaque silhouette. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);  /* retail ZWR1 — far-depth, safe (see header) */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);

    int last_char = -1;
    for (int i = 0; i < SCENE1_BG_NPC_COUNT; i++) {
        scene1_bg_npc_t *m = &g_scene1_bg_npc[i];
        if (m->visible == -1 || m->dir == 0)         /* hidden / unspawned */
            continue;

        int char_id = scene1_bg_npc_type_to_char(m->type);
        if (char_id < 0)
            continue;

        /* lazy-load the NPC sheet (engine pre-loads DAT_073a9b18; the port loads
         * party sheets at boot but not these — bring them in on first draw). */
        const sprite_t *sheet = scene1_preload_chr_sheet(char_id);
        if (sheet == NULL || sheet->tex == NULL) {
            scene1_preload_load_chr_sheet(char_id);
            sheet = scene1_preload_chr_sheet(char_id);
        }
        if (sheet == NULL || sheet->tex == NULL)
            continue;                                /* sheet missing → skip */

        /* world = billboard × Scale(0.03) × Translate(x,y,z). */
        float world[16], scale[16], tmp[16];
        mat4_translation(tmp, m->x, m->y, m->z);
        mat4_scaling(scale, BG_NPC_SPRITE_SCALE, BG_NPC_SPRITE_SCALE,
                     BG_NPC_SPRITE_SCALE);
        mat4_mul(tmp, scale, tmp);
        mat4_mul(world, g_scene1_camera_orient, tmp);

        if (char_id != last_char) {                  /* sticky-texture (engine) */
            last_char = char_id;
            IDirect3DDevice8_SetTexture(dev, 0,
                (IDirect3DBaseTexture8 *)sheet->tex);
        }

        /* engine FUN_0045a56f(record, char, char, world, 0xff7f7f7f). */
        scene1_chr_sprite_render((struct IDirect3DDevice8 *)dev, m->arec,
                                 char_id, world, 0xff7f7f7fu,
                                 (int)sheet->width, (int)sheet->height);
    }

    /* restore ZWRITE off + LINEAR filter for trailing passes (the wing-glow +
     * foot-dust + sw_pass_g draws are ZWR0; mirrors sw_pass_light's restore). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
}

#endif /* _WIN32 */
