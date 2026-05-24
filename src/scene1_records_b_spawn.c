/*
 * scene1_records_b_spawn.c — see header for chip writeup.
 *
 * Ports:
 *   FUN_0044376a @ 0x44376a (1124 B Ghidra decomp, L40901..41888) — entity
 *                  allocator; scene1_record_b_spawn_entity().
 *   FUN_00445a8c @ 0x445a8c (1153 B Ghidra decomp, L41984..43137) — NPC
 *                  allocator; scene1_record_b_spawn_npc().
 *
 * C8j.5 covers the SKELETON ONLY — outer slot scan + common preamble +
 * 6 minimal 1-particle anchor types (3 per allocator).  Other types are
 * trace-only and do not commit a slot (same divergence-vs-engine note
 * as scene1_spawn's C8i.1 — engine would leak a committed but
 * uninitialized slot; we keep the table clean until C8j.6+ fills in
 * the bodies).
 *
 * Layout notes:
 *
 *   Slot stride is 0x49 dwords (= 0x124 bytes = 292 B); 512 slots.
 *   Slot field offsets are in scene1_records.h (SCENE1_RECORDS_B_OFF_*).
 *   The engine's writer-view byte offsets translate as:
 *       byte_off  /  4  = dword index from slot base
 *   e.g. DAT_069324b0 (slot base) → dw 0; DAT_0693250c → dw 23 (0x5c/4);
 *   DAT_069325b8 → dw 66 (0x108/4); DAT_06932578 → dw 50 (0xc8/4).
 *
 * Type-IS-sentinel: both allocators write `*piVar = param_2 (type)`
 * mid-preamble.  Free-slot detection is `slot[TYPE] == 0` — which means
 * type 0 itself can NEVER be allocated (it's indistinguishable from
 * "empty").  Engine callers never request type 0.
 *
 * Sequence counter: g_scene1_record_b_seq_counter mirrors engine
 * DAT_06a46fb8.  Snapshot + post-increment in both preambles.
 *
 * Argless trig deferred: all per-type sin/cos calls land in C8j.6+;
 * the C8j.5 anchor types do no trig (no Ghidra-dropped arg risk).
 */

#include "scene1_records_b_spawn.h"

#include <math.h>
#include <string.h>

#include "math3d.h"
#include "rng.h"
#include "scene1_particles_tick.h"  /* g_scene1_camera_yaw, g_scene1_people */
#include "sim.h"                    /* g_sim_frame_count (engine DAT_0438b8cc) */

int g_scene1_record_b_seq_counter;

int                          g_scene1_record_b_spawn_trace_count;
scene1_record_b_spawn_call_t g_scene1_record_b_spawn_trace[
    SCENE1_RECORD_B_SPAWN_TRACE_CAPACITY];

/* ─── slot accessors ────────────────────────────────────────────────── */

static inline int32_t *slot_base(int i)
{
    return &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];
}

static inline void slot_set_i(int i, int off, int32_t v)
{
    slot_base(i)[off] = v;
}

static inline int32_t slot_get_i(int i, int off)
{
    return slot_base(i)[off];
}

static inline void slot_set_f(int i, int off, float f)
{
    int32_t v;
    memcpy(&v, &f, sizeof v);
    slot_base(i)[off] = v;
}

static inline float slot_get_f(int i, int off)
{
    int32_t v = slot_base(i)[off];
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static inline int slot_is_free(int i)
{
    /* Free-slot test mirrors engine `*piVar13 == 0`.  scene1_records_reset
     * zeroes slot[0]; allocators write `slot[TYPE] = type` once committed. */
    return slot_get_i(i, SCENE1_RECORDS_B_OFF_TYPE) == 0;
}

/* Byte-pun helper for owner-field reads — engine treats owner pointers
 * as `int` and uses `*(float *)(param_1 + offset)`.  Replicate verbatim. */
static inline float owner_read_f(const void *owner, int byte_off)
{
    int32_t v;
    memcpy(&v, (const char *)owner + byte_off, sizeof v);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static inline int32_t owner_read_i(const void *owner, int byte_off)
{
    int32_t v;
    memcpy(&v, (const char *)owner + byte_off, sizeof v);
    return v;
}

/* ─── trace ─────────────────────────────────────────────────────────── */

static void trace_record(scene1_record_b_spawn_kind_t kind, const void *owner,
                         int type, int flag)
{
    int idx = g_scene1_record_b_spawn_trace_count
              % SCENE1_RECORD_B_SPAWN_TRACE_CAPACITY;
    scene1_record_b_spawn_call_t *c = &g_scene1_record_b_spawn_trace[idx];
    c->kind  = kind;
    c->owner = owner;
    c->type  = type;
    c->flag  = flag;
    g_scene1_record_b_spawn_trace_count++;
}

void scene1_record_b_spawn_trace_reset(void)
{
    g_scene1_record_b_spawn_trace_count = 0;
    memset(g_scene1_record_b_spawn_trace, 0,
           sizeof g_scene1_record_b_spawn_trace);
}

/* ─── ground-query hook (FUN_00432e50, used by 0x29) ───────────────── */

static int default_ground_query_b(float x, float y, float *out_y)
{
    (void)x; (void)y; (void)out_y;
    return 0;
}

static scene1_b_ground_query_fn g_ground_query_b = default_ground_query_b;

scene1_b_ground_query_fn scene1_record_b_spawn_set_ground_query(
    scene1_b_ground_query_fn fn)
{
    scene1_b_ground_query_fn prev = g_ground_query_b;
    g_ground_query_b = fn ? fn : default_ground_query_b;
    return prev;
}

/* ─── entity-allocator preamble (FUN_0044376a L40932-40978) ────────── */

static void preamble_entity(int i, const void *owner, int type, int flag)
{
    int32_t *r = slot_base(i);

    /* L40933-40935: owner ref at dw 4; zero dw 5 + dw 1. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_OWNER_A, (int32_t)(intptr_t)owner);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_OWNER_B, 0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_FLAG_A, 0);

    /* L40936-40939: zero AGE + rotation scratch triple. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE,     0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_ROT_SCR, 0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_ROT_X,   0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_ROT_Z,   0);

    /* L40940: claim slot (type IS sentinel — subsequent sentinel scans
     * see this slot as busy from here on). */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_TYPE, type);

    /* L40941-40943: zero velocity triple. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    /* L40944: aux sentinel #2 (DAT_069325c4) — written by entity alloc
     * only; NPC alloc skips. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT2, -1);

    /* L40945-40956: position source — `flag == -1` selects the default
     * owner+0x20 triple with a -0.5 floor offset on pos.y; otherwise
     * pulls from a per-owner inline NPC slot array at owner+0x9e0
     * stride 0x44.  All observed call sites in the engine pass -1, but
     * port the alt path for completeness. */
    if (flag == -1) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   owner_read_f(owner, 0x20));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
                   owner_read_f(owner, 0x24) - 0.5f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   owner_read_f(owner, 0x28));
    } else {
        int npc_off = flag * 0x44;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   owner_read_f(owner, npc_off + 0x9e0));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
                   owner_read_f(owner, npc_off + 0x9e4));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   owner_read_f(owner, npc_off + 0x9e8));
    }

    /* L40957-40963: copy 16 dwords (64 B) of owner matrix to slot+MATRIX0. */
    memcpy(&r[SCENE1_RECORDS_B_OFF_MATRIX0],
           (const char *)owner + 0xde8,
           16 * sizeof(int32_t));

    /* L40964-40978: assorted aux init.  Order matches engine for
     * easier diff against the decomp. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_DRAG,       0);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT,  1.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT1, -1);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C0,     0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX,   0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_FLAG_B,     flag);

    /* L40970-40972: post-incremented sequence ID. */
    int seq = g_scene1_record_b_seq_counter;
    g_scene1_record_b_seq_counter = seq + 1;
    slot_set_i(i, SCENE1_RECORDS_B_OFF_SEQ_ID, seq);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,    1.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_OWNER_FLAG,
               owner_read_i(owner, 0xeac));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_Y,    1.0f);

    /* L40976-40977: byte writes — engine zeros ONLY bytes 0xc0 and
     * 0xc1 (the low 2 bytes of dw 48); bytes 0xc2/0xc3 retain
     * whatever was last written before slot reuse.  For a freshly
     * sentinel-reset slot they're 0 anyway.  Port faithfully — some
     * per-type body (e.g. type 100's `(&DAT_06932572)[iVar10] = 2`)
     * later writes byte 0xc2; if we cleared all 4 bytes we'd
     * silently clobber that on reuse. */
    uint8_t *bytes = (uint8_t *)r;
    bytes[0xc0] = 0;
    bytes[0xc1] = 0;

    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 0);
}

/* ─── NPC-allocator preamble (FUN_00445a8c L42016-42048) ──────────── */

static void preamble_npc(int i, const void *owner, int type, int flag)
{
    int32_t *r = slot_base(i);

    /* L42016: flag stored at FLAG_A (dw 1) — DIFFERENT location vs
     * entity alloc which puts flag at FLAG_B (dw 3). */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_FLAG_A, flag);

    /* L42017-42020: zero AGE + rotation scratch triple. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE,     0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_ROT_SCR, 0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_ROT_X,   0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_ROT_Z,   0);

    /* L42021: claim slot. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_TYPE, type);

    /* L42022-42023: owner ref at dw 5 (DIFFERENT from entity); dw 4
     * zeroed. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_OWNER_A, 0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_OWNER_B, (int32_t)(intptr_t)owner);

    /* L42024-42026: zero velocity triple. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    /* L42027-42029: position from owner+0x3f0 (NO -0.5 bias, NO
     * fallback path). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x3f4));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x3f8));

    /* L42030-42036: copy 16 dwords (64 B) of owner matrix from a
     * DIFFERENT field on the owner (0x39c vs entity's 0xde8). */
    memcpy(&r[SCENE1_RECORDS_B_OFF_MATRIX0],
           (const char *)owner + 0x39c,
           16 * sizeof(int32_t));

    /* L42037-42048: assorted aux init.  Several entity-alloc-only
     * writes are SKIPPED here (SCALE_Y, BYTE_PAIR, AUX_SENT2). */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_DRAG,      0);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT1, -1);

    int seq = g_scene1_record_b_seq_counter;
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C0,    0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX,  0);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_FLAG_B,   -1);   /* hardcoded -1 (NOT flag) */
    g_scene1_record_b_seq_counter = seq + 1;
    slot_set_i(i, SCENE1_RECORDS_B_OFF_SEQ_ID,    seq);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,   1.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8,    0);   /* dw 49 */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_OWNER_FLAG, 0);  /* zeroed, not inherited */

    (void)type;
}

/* ─── per-type bodies — entity allocator ──────────────────────────── */

/* All entity bodies have the same signature: take a slot + owner +
 * type + flag + per-particle index, write per-type fields, return the
 * loop cap (number of particles to commit for this call).  Mirrors the
 * engine's iVar10 (cap) + local_8 (part_idx) variables — see the loop
 * structure in scene1_record_b_spawn_entity below.
 *
 * Convention: cap = 1 for single-particle types (engine LAB_004457e7
 * tail); cap > 1 for multi-particle types (engine LAB_004455ed loop). */

/* Type 0x24 — pure preamble (engine L41052: direct goto LAB_004457e7). */
static int init_entity_24(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)i; (void)owner; (void)type; (void)flag; (void)part_idx;
    return 1;
}

/* Type 0x60 — preamble + ROT_X ← owner+0xea4 (engine L41066 →
 * LAB_004457db: `fVar2 = *(float *)(param_1 + 0xea4);` then
 * LAB_004457e1 writes ROT_X). */
static int init_entity_60(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, owner_read_f(owner, 0xea4));
    return 1;
}

/* Type 0x82 — preamble + SCALE_X = 2.0f + ROT_X ← owner+0xea4 (engine
 * L41061-41064: sets SCALE_X then falls through to 0x60's body via
 * LAB_004457cf → LAB_004457db → LAB_004457e1). */
static int init_entity_82(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, owner_read_f(owner, 0xea4));
    return 1;
}

#define B_TWO_PI_F 6.2831855f

/* Drift cluster — types 2, 3, 4, 0x22, 0x54, 0x67 (engine L41594-41621
 * + LAB_004449b0/c1 + LAB_00443dbe tail).  Shared body — per-type SCALE_X
 * override is the only variation.
 *
 * Body:
 *   ROT_X     = (int)(owner+0x948) * 2π / 8                    (NPC bend)
 *   ang       = (float)(owner+0xea4)
 *   VEL_X/Y/Z = sin(ang)*3, 0, cos(ang)*3
 *   POS_X    -= sin(ang)*0.5
 *   POS_Y    += 1.0  (additive on top of preamble's owner+0x24 - 0.5)
 *   POS_Z    -= cos(ang)*0.5
 *   SCALE_X per-type: 0x22→2.0, 0x67→1.2, 3 if flag!=-1 → 0.5;
 *                     all others keep preamble default 1.0.
 *   ROT_Z     = rng_next_unit() * 2π   (LAB_004449b0 random rot.z)
 *   DRAG      = 20.0                    (LAB_004449c1)
 *   AUX_C8    = 1                       (LAB_00443dbe)
 *
 * Argless cos sites at L41602 / L41609 verified via raw-asm read of
 * 0x443cbd / 0x443d0c — both reload `[esi+0xea4]` ⇒ cos(ang), same
 * PHC #7 pattern. */
static int init_entity_drift_cluster(int i, const void *owner, int type,
                                     int flag, int part_idx)
{
    (void)part_idx;

    /* L41597-41598: NPC bend angle into ROT_X. */
    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);

    /* L41599-41603: velocity from sin/cos(owner+0xea4)*3. */
    float ang = owner_read_f(owner, 0xea4);
    float sa  = sinf(ang);
    float ca  = cosf(ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 3.0f);

    /* L41604-41611: read-modify-write pos with -sin/-cos*0.5 jitter and
     * +1.0 y lift on top of preamble. */
    float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - ca * 0.5f);

    /* L41612-41620: per-type SCALE_X. */
    if (type == 0x67) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.2f);
    if (type == 0x22) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 2.0f);
    if (type == 3 && flag != -1)
        slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.5f);

    /* L41621 → LAB_004449b0: random ROT_Z. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    /* LAB_004449c1: DRAG = 20.0. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 20.0f);
    /* LAB_00443dbe: AUX_C8 = 1. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    return 1;
}

/* Cluster A — types 0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6, 99, 0x51, 0x52,
 * 0x53 (engine L41265-41397).  MULTI-particle for 0x4f/0x50/0xa5/0xa6;
 * 1-particle for the rest.  Per-particle angle shifts come from a
 * fixed 8-entry table.
 *
 * Body (per-particle):
 *   local_c = NPC bend (owner+0x948 * 2π/8) + per-particle shift:
 *     part_idx 0 → +0;       1 → -0.18;     2 → +0.18;
 *              3 → -0.36;    4 → +0.36;     5 → +π;
 *              6 → +π+0.18;  7 → +π-0.18.
 *   Default pos (all types): sin(local_c)*0.3 + owner.x, owner.y+0.7,
 *                            cos(local_c)*0.3 + owner.z.
 *   If type==0x53: POS_Y = owner.y (no +0.7 lift).
 *   If type in {0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6} (main 6): OVERRIDE
 *     pos with sin/cos(local_c)*0.8 + +1.4y.
 *   Per-type SCALE_X: 99→1.8, 0x52→1.0, 0x4d→0.7, 0x4e/0x4f/0x50/0xa5/
 *     0xa6→1.0, 0x51→1.5.  0x53 leaves preamble default 1.0.
 *   Per-type LIFE_MULT: 99→1.5, 0x4d→0.32, 0x4e/0x4f/0x50/0xa5/0xa6→0.4.
 *   local_10 (vel mag): 0x4d/99/0x52 → 0.3; else → 0.5.
 *   VEL_Y: 0.07 if 0x4d, else 0.
 *   If type==0x53: slot byte 0xc0 = 3 (lo byte of dw 48 BYTE_PAIR).
 *   VEL_X/Z = sin/cos(local_c) * local_10.
 *   Cap (iVar10): 0x4f→3, 0x50→5, 0xa5→6, 0xa6→8; else 1.
 *   ROT_X = local_c.  DRAG = 0.5.  AUX_C8 = 1.
 *
 * Argless cos at L41296, L41307, L41379 verified via raw-asm read of
 * 0x444034 / 0x444131 — both reload `[ebp-0x2c]` (dVar4 = (double)
 * local_c) ⇒ cos(local_c), same PHC #7 pattern. */
static int init_entity_cluster_a(int i, const void *owner, int type,
                                 int flag, int part_idx)
{
    (void)flag;

    /* L41270-41291: per-particle angle. */
    static const float shifts[8] = {
        0.0f,
        -0.18f,
        +0.18f,
        -0.36f,
        +0.36f,
        3.1415927f,
        3.3215928f,    /* π + 0.18 */
        2.9615927f,    /* π - 0.18 */
    };
    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    float local_c = bend;
    if (part_idx >= 0 && part_idx < 8) local_c += shifts[part_idx];

    float ox = owner_read_f(owner, 0x20);
    float oy = owner_read_f(owner, 0x24);
    float oz = owner_read_f(owner, 0x28);

    float sc = sinf(local_c);
    float cc = cosf(local_c);

    /* L41293-41297: default pos with *0.3 jitter + +0.7y lift. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sc * 0.3f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 0.7f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cc * 0.3f + oz);

    /* L41298-41300: 0x53 sets POS_Y from owner+0x24 with NO +0.7 lift.
     * Order matters — the main-6 override below would clobber if 0x53
     * were a member; it isn't, so 0x53's owner.y survives. */
    if (type == 0x53) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy);
    }

    /* L41301-41309: main-6 override with *0.8 jitter + +1.4y lift. */
    int is_main6 = (type == 0x4d || type == 0x4e || type == 0x4f ||
                    type == 0x50 || type == 0xa5 || type == 0xa6);
    if (is_main6) {
        float sc2 = sinf(local_c);
        float cc2 = cosf(local_c);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sc2 * 0.8f + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.4f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cc2 * 0.8f + oz);
    }

    /* L41310-41336: per-type SCALE_X. */
    if (type == 99)   slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.8f);
    if (type == 0x52) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);
    if (type == 0x4d) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.7f);
    if (type == 0x4e) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);
    if (type == 0x4f) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);
    if (type == 0x50) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);
    if (type == 0xa5) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);
    if (type == 0xa6) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);
    if (type == 0x51) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.5f);

    /* L41337-41345: per-type LIFE_MULT. */
    if (type == 99) slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.5f);
    if (type == 0x4d)
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.32f);
    if (type == 0x4e || type == 0x4f || type == 0x50 ||
        type == 0xa5 || type == 0xa6)
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.4f);

    /* L41347-41373: local_10 (vel mag) + VEL_Y. */
    float local_10 = 0.5f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    if (type == 0x4d) {
        local_10 = 0.3f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.07f);
    }
    if (type == 99)   local_10 = 0.3f;
    if (type == 0x52) local_10 = 0.3f;

    /* L41374-41376: 0x53 writes byte 0xc0 = 3 (low byte of dw 48
     * BYTE_PAIR; preamble zeroed bytes 0xc0/0xc1). */
    if (type == 0x53) {
        uint8_t *bytes = (uint8_t *)slot_base(i);
        bytes[0xc0] = 3;
    }

    /* L41377-41381: VEL_X/Z = sin/cos(local_c) * local_10. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(local_c) * local_10);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(local_c) * local_10);

    /* L41394-41396: ROT_X = local_c; DRAG = 0.5; AUX_C8 = 1. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, local_c);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,  0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    /* L41382-41393: per-type cap (iVar10). */
    int cap = 1;
    if (type == 0x4f) cap = 3;
    if (type == 0x50) cap = 5;
    if (type == 0xa5) cap = 6;
    if (type == 0xa6) cap = 8;
    return cap;
}

/* Mega-cluster A — types 0x73, 0x76, 0x77, 0x78, 0x7a, 0x7b, 0x7c, 0x7e
 * (engine L41650-41813, reached only via the negated outer guard at
 * L41469-71).  MULTI-particle for 0x73 (4), 0x7c (5), 0x76/0x78/0x7a
 * (8 each); 1-particle for 0x77/0x7b/0x7e.
 *
 * Body (per-particle, with `part_idx = local_8`):
 *
 *   local_c = (owner+0x948) * 2π / 8                       (BEND angle)
 *   POS_X/Z   = sin/cos(local_c) * 1.2 + owner.x/z
 *   POS_Y     = owner.y + 1.3
 *   ALT_POS_X/Z = sin/cos(local_c) * 0.8 + owner.x/z
 *   ALT_POS_Y   = owner.y + 1.3
 *   if type==0x7a: local_c = owner+0xea4              (override AFTER pos)
 *
 *   3-way (owner+0x948) dispatch (re-read through slot OWNER_A in engine):
 *     mode 0:  POS_X -= 0.41;  ALT_POS_X -= 0.41;
 *     mode 4:  POS_X += 0.41;  ALT_POS_X += 0.41;
 *     else:    POS_Z -= 0.1;   ALT_POS_Z -= 0.1;
 *
 *   uVar9 = (owner+0xe3c + part_idx) % 32             (angle-table mod 32)
 *   LIFE_MULT = 0.3 (default; per-type overrides below)
 *   DRAG = 0.5
 *   local_10 = 0.18 (vel mag default)
 *   AUX_C8 = 1
 *   fVar2 = (uVar9 & 7) - 4   ;  fVar3 = uVar9 / 8     (int div)
 *   ROT_X = (fVar2*0.05 + fVar3*0.0125) * π + local_c  (default mod-32)
 *   VEL_Y = 0
 *
 *   Per-type overrides (some chains overwrite ROT_X / VEL_Y):
 *     0x7e:  LIFE_MULT=0.4, local_10=0.25,
 *            ROT_X = (u - 0.5)*2.1991148 + local_c
 *     0x78 | 0x7a:
 *            LIFE_MULT=0.15, local_10=0.5,
 *            ROT_X = (fVar2*0.01 + fVar3*0.0025)*π + local_c,
 *            ROT_X = (u - 0.5)*0.3 + local_c            (overwrites!)
 *            if part_idx != 3:
 *              0x78 → VEL_Y = u*0.01 - 0.1
 *              0x7a → VEL_Y = u*0.01 - 0.07
 *
 *   LAB_00444f72 (joined back here from 0x78/0x7a):
 *     0x7c:  local_10=0.3,
 *            base = local_c - π,
 *            shift_mag = ((part_idx + 1) / 2) * π / 10,
 *            ROT_X = base ∓ shift_mag (alternating sign per part_idx),
 *            VEL_Y = u*0.01 + 0.15
 *     0x76:  ROT_X = (fVar2*0.01 + fVar3*0.0025)*π + local_c
 *     0x7b:  ROT_X = local_c, local_10=0.24, VEL_Y = 0.1
 *     0x77:  local_10=0.3, LIFE_MULT=0.8, ROT_SCR=local_c,
 *            ROT_X = (u - 0.5)*0.3 + local_c
 *
 *   VEL_X = sin(ROT_X) * local_10
 *   VEL_Z = cos(ROT_X) * local_10
 *   if 0x7c:  POS_X -= 2*VEL_X;  POS_Z -= 2*VEL_Z      (rebound)
 *   ROT_Z = rng_next_unit() * 2π
 *   AGE = -part_idx     (0x7c override: AGE = part_idx * -4)
 *   if 0x76 && part_idx > 0:  PART_IDX = 1
 *   Per-type SCALE_X:
 *     0x7e→0.3, 0x73→0.25, 0x76→0.25, 0x78→0.125, 0x7a→0.125,
 *     0x77→1.0, 0x7b→1.0, 0x7c→0.5
 *   Cap:
 *     0x76/0x78/0x7a→8, 0x7c→5, 0x73→4, 0x77/0x7b/0x7e→1
 *
 * Argless cos at L41752 (after L41750 sin(slot+ROT_X)) verified via
 * raw-asm at 0x444769: `fld QWORD PTR [ebp-0x2c]; ...; call 0x503994`
 * — same dVar4 stash pattern as PHC #7.  Argless cos at L41636 (drift-
 * sibling tail) verified at 0x443d0c. */
static int init_entity_mega_cluster_a(int i, const void *owner, int type,
                                      int flag, int part_idx)
{
    (void)flag;

    int   npc_idx = owner_read_i(owner, 0x948);
    float bend    = (float)npc_idx * B_TWO_PI_F / 8.0f;
    float local_c = bend;

    float ox = owner_read_f(owner, 0x20);
    float oy = owner_read_f(owner, 0x24);
    float oz = owner_read_f(owner, 0x28);

    /* L41653-41661: default pos + alt-pos with sin/cos(bend). */
    float sc_b = sinf(bend);
    float cc_b = cosf(bend);
    float pos_x = sc_b * 1.2f + ox;
    float pos_y = oy + 1.3f;
    float pos_z = cc_b * 1.2f + oz;
    float alt_x = sc_b * 0.8f + ox;
    float alt_y = oy + 1.3f;
    float alt_z = cc_b * 0.8f + oz;

    /* L41662-41664: 0x7a swaps local_c to owner+0xea4 AFTER pos writes. */
    if (type == 0x7a) local_c = owner_read_f(owner, 0xea4);

    /* L41665-41679: 3-way (owner+0x948) dispatch — modifies pos + alt_pos
     * x/z based on the NPC's facing mode. */
    if (npc_idx == 0) {
        pos_x -= 0.41f;
        alt_x -= 0.41f;
    } else if (npc_idx == 4) {
        pos_x += 0.41f;
        alt_x += 0.41f;
    } else {
        pos_z -= 0.1f;
        alt_z -= 0.1f;
    }

    /* Commit pos + alt-pos. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, pos_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pos_z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, alt_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, alt_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, alt_z);

    /* L41680: angle-table mod 32.  owner+0xe3c is a per-entity sub-frame
     * counter (no global stand-in — read directly from owner). */
    uint32_t uvar9 = (uint32_t)(owner_read_i(owner, 0xe3c) + part_idx) & 0x1f;
    float    fVar2 = (float)((int)(uvar9 & 7) - 4);
    float    fVar3 = (float)((int)uvar9 / 8);

    /* L41681-41689: default tail state. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      0.5f);
    float local_10 = 0.18f;
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    float rot_x = (fVar2 * 0.05f + fVar3 * 0.0125f) * 3.1415927f + local_c;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);

    /* L41690-41696: 0x7e override. */
    if (type == 0x7e) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.4f);
        local_10 = 0.25f;
        rot_x    = (rng_next_unit() - 0.5f) * 2.1991148f + local_c;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
    }

    /* L41697-41713: 0x78 / 0x7a override (with part_idx==3 skip). */
    if (type == 0x78 || type == 0x7a) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.15f);
        local_10 = 0.5f;
        rot_x    = (fVar2 * 0.01f + fVar3 * 0.0025f) * 3.1415927f + local_c;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
        rot_x    = (rng_next_unit() - 0.5f) * 0.3f + local_c;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
        if (part_idx != 3) {
            float u = rng_next_unit();
            if (type == 0x78) {
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, u * 0.01f - 0.1f);
            } else {
                slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, u * 0.01f - 0.07f);
            }
        }
    }

    /* LAB_00444f72 — joined back here regardless of 0x78/0x7a branch. */
    if (type == 0x7c) {
        local_10 = 0.3f;
        float base = local_c - 3.1415927f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, base);
        float shift_mag = (float)((part_idx + 1) / 2) * 0.31415927f;
        float fan;
        if ((part_idx & 1) == 0) {
            fan = base - shift_mag;
        } else {
            fan = base + shift_mag;
        }
        rot_x = fan;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y,
                   rng_next_unit() * 0.01f + 0.15f);
    }
    if (type == 0x76) {
        rot_x = (fVar2 * 0.01f + fVar3 * 0.0025f) * 3.1415927f + local_c;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
    }
    if (type == 0x7b) {
        rot_x    = local_c;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
        local_10 = 0.24f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.1f);
    }
    if (type == 0x77) {
        local_10 = 0.3f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.8f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, local_c);
        rot_x = (rng_next_unit() - 0.5f) * 0.3f + local_c;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, rot_x);
    }

    /* L41750-41753: VEL_X/Z = sin/cos(ROT_X) * local_10.
     * Re-read ROT_X to capture all per-type overrides above. */
    float final_rot_x = slot_get_f(i, SCENE1_RECORDS_B_OFF_ROT_X);
    float vx = sinf(final_rot_x) * local_10;
    float vz = cosf(final_rot_x) * local_10;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);

    /* L41754-41763: 0x7c rebound — POS_X/Z -= 2*VEL.
     * Engine reads slot[POS_X/Z] and slot[VEL_X/Z] back, not the local
     * `vx`/`vz` (same values; faithful read-back). */
    if (type == 0x7c) {
        float pxr = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pzr = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, pxr - 2.0f * vx);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pzr - 2.0f * vz);
    }

    /* L41764-41769: ROT_Z, AGE. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, -part_idx);
    if (type == 0x7c) {
        slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, part_idx * -4);
    }

    /* L41770-41772: 0x76 sub-particle PART_IDX flag. */
    if (type == 0x76 && part_idx > 0) {
        slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, 1);
    }

    /* L41773-41796: per-type SCALE_X. */
    switch (type) {
    case 0x7e: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.3f);   break;
    case 0x73: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.25f);  break;
    case 0x76: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.25f);  break;
    case 0x78: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.125f); break;
    case 0x7a: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.125f); break;
    case 0x77: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);   break;
    case 0x7b: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.0f);   break;
    case 0x7c: slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.5f);   break;
    default: break;
    }

    /* L41797-41812: per-type cap. */
    int cap = 1;
    if (type == 0x76 || type == 0x78 || type == 0x7a) cap = 8;
    else if (type == 0x7c) cap = 5;
    else if (type == 0x73) cap = 4;
    return cap;
}

/* ─── C8j.8 — NPC-table + camera-yaw + matrix-init types ─────────── */

/* Types 0x3e, 0x5f — share the 0x60 body (engine L41053-41066 falls
 * through to LAB_004457db: `fVar2 = *(float *)(param_1 + 0xea4);`
 * LAB_004457e1: writes ROT_X).  The inner `if (param_2 == 0x82)` test
 * in the engine's 0x3e/0x5f branch is dead code (param_2 can't be both
 * 0x3e/0x5f AND 0x82). */
static int init_entity_3e_5f(int i, const void *owner, int type, int flag,
                             int part_idx)
{
    (void)type; (void)flag; (void)part_idx;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, owner_read_f(owner, 0xea4));
    return 1;
}

/* Type 0x23 — matrix-init + camera-yaw + people-table fallback.
 * 1-particle: tail goes through LAB_00443a5d → LAB_00444230 (DRAG=0) →
 * LAB_004457e7 (bVar14=(local_8==0)).  Engine's per-particle dispatch
 * on `local_8 ∈ {1, 2}` is ported verbatim but unreachable in normal
 * flow (cap=1 means outer loop only ever passes part_idx=0).
 *
 * Body (engine L40979-41027):
 *   - POS_Y += 2 (read-modify-write on top of preamble's owner.y-0.5)
 *   - VEL = (0, -0.3f, 0)
 *   - If owner+0xea0 == -1 (people_idx):
 *       POS_X = sin(-yaw)*15 + owner+0x38
 *       POS_Y = owner+0x3c + 30        (overwrites the +=2 above)
 *       POS_Z = cos(-yaw)*15 + owner+0x40
 *     Else (people-table):
 *       POS_X = people[idx].target.x   (engine `&DAT_0076bd60` = base+0x0c)
 *       POS_Y = people[idx].target.y + 20
 *       POS_Z = people[idx].target.z
 *   - Per-particle dispatch (dead in normal flow, cap=1):
 *       part_idx==1: POS_X += sin(-yaw)*8; POS_Y += 10; POS_Z += cos(-yaw)*8
 *       part_idx==2: POS_X -= sin(-yaw)*8; POS_Y += 20; POS_Z -= cos(-yaw)*8
 *   - LIFE_MULT = 1.2f
 *   - u = rng_next_unit(); ROT_Z = u*2π; slot.matrix = RotationX(u*2π)
 *     (single RNG draw shared between ROT_Z and matrix angle)
 *   - DRAG = 0 (set explicitly, even though preamble already left it 0)
 *
 * Argless cos sites (L40989, L40999, L41003, L41009, L41013) all paired
 * with the immediately-prior sin call on the same angle (-_DAT_073de39c).
 * Same PHC #7 pattern. */
static int init_entity_23(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag;

    int people_idx = owner_read_i(owner, 0xea0);

    /* L40981: POS_Y += 2 (rmw). */
    float pos_y = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y) + 2.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y);

    /* L40982-40984: VEL = (0, -0.3f, 0).  Engine raw `0xbe99999a`. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    float neg_yaw = -g_scene1_camera_yaw;
    float sn_y = sinf(neg_yaw);
    float cs_y = cosf(neg_yaw);

    if (people_idx == -1) {
        /* L40986-40990: camera-yaw branch.  Note: pos source is
         * owner+0x38/0x3c/0x40, NOT the +0x20/0x24/0x28 used by the
         * preamble (which preamble overwrites here). */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   sn_y * 15.0f + owner_read_f(owner, 0x38));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
                   owner_read_f(owner, 0x3c) + 30.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   cs_y * 15.0f + owner_read_f(owner, 0x40));
    } else if (people_idx >= 0 && people_idx < SCENE1_PEOPLE_COUNT) {
        /* L40993-40996: people-table branch.  Engine `&DAT_0076bd60` is
         * base+0x0c = people[idx].target. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   g_scene1_people[people_idx].target[0]);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
                   g_scene1_people[people_idx].target[1] + 20.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   g_scene1_people[people_idx].target[2]);
    }
    /* OOB people_idx → preamble pos retained (engine would crash; we
     * stay safe). */

    /* L40998-41016: per-particle dispatch (DEAD in cap=1 flow). */
    if (part_idx == 1) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px + sn_y * 8.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + 10.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz + cs_y * 8.0f);
    } else if (part_idx == 2) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - sn_y * 8.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py + 20.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - cs_y * 8.0f);
    }

    /* L41017: LIFE_MULT = 1.2f (engine raw 0x3f99999a). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.2f);

    /* L41018-41020: ROT_Z and matrix share ONE rng draw — engine reuses
     * fVar15 across both writes. */
    float u_angle = rng_next_unit() * B_TWO_PI_F;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, u_angle);

    /* Engine `thunk_FUN_004a35d3(&DAT_06932578 + iVar10, u_angle)` is
     * D3DXMatrixRotationX writing 16 floats at slot.matrix.  Our port
     * uses math3d's mat4_rotation_x which produces the same row-major
     * 4x4 matrix layout. */
    float mat[16];
    mat4_rotation_x(mat, u_angle);
    int32_t *slot_mat_ptr = slot_base(i) + SCENE1_RECORDS_B_OFF_MATRIX0;
    memcpy(slot_mat_ptr, mat, sizeof mat);

    /* LAB_00443a5d → LAB_00444230: DRAG = 0 (explicit, redundant with
     * preamble but ported verbatim). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    return 1;
}

/* Type 0x29 — camera-yaw or people-table pos w/ optional ground-clamp.
 * 1-particle.  Note differences vs 0x23:
 *   - No VEL writes (preamble VEL=0 carries).
 *   - No LIFE_MULT (preamble 1.0 carries).
 *   - No matrix write.
 *   - People-table source is `people[idx].pos` (base+0), NOT target.
 *   - People-table branch has a ground-clamp via FUN_00432e50.
 *   - Camera-yaw branch pos source is owner+0x20/0x24/0x28, NOT 0x38..0x40.
 *
 * Body (engine L41067-41105):
 *   - If owner+0xea0 == -1:
 *       POS_X = sin(-yaw)*15 + owner+0x20
 *       POS_Y = owner+0x24                  (overwrites preamble's -0.5)
 *       POS_Z = cos(-yaw)*15 + owner+0x28
 *     Else (people-table):
 *       POS_X = people[idx].pos.x
 *       POS_Y = people[idx].pos.y - 5
 *       POS_Z = people[idx].pos.z
 *       g_ground_query_b(POS_X, POS_Y, &gy):
 *         on hit: POS_Y = max(gy, people[idx].pos.y - 5)
 *   - Per-particle dispatch (DEAD in cap=1 flow):
 *       part_idx==1: POS_X += sin(-yaw)*8; POS_Z += cos(-yaw)*8 (no POS_Y)
 *       part_idx==2: POS_X -= sin(-yaw)*8; POS_Z -= cos(-yaw)*8 (no POS_Y)
 *   - LAB_00443a5d → LAB_00444230: DRAG = 0
 *
 * Argless trig sites (L41073, L41092, L41101) all paired with prior
 * sin call on -_DAT_073de39c; PHC #7 pattern. */
static int init_entity_29(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag;

    int people_idx = owner_read_i(owner, 0xea0);

    float neg_yaw = -g_scene1_camera_yaw;
    float sn_y = sinf(neg_yaw);
    float cs_y = cosf(neg_yaw);

    if (people_idx == -1) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   sn_y * 15.0f + owner_read_f(owner, 0x20));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
                   owner_read_f(owner, 0x24));
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   cs_y * 15.0f + owner_read_f(owner, 0x28));
    } else if (people_idx >= 0 && people_idx < SCENE1_PEOPLE_COUNT) {
        float people_y = g_scene1_people[people_idx].pos[1];
        float anchor_y = people_y - 5.0f;
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
                   g_scene1_people[people_idx].pos[0]);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, anchor_y);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
                   g_scene1_people[people_idx].pos[2]);

        float gy = 0.0f;
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float py = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
        if (g_ground_query_b(px, py, &gy)) {
            /* L41082-41085: POS_Y = max(gy, anchor_y). */
            float new_y = (gy < anchor_y) ? anchor_y : gy;
            slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, new_y);
        }
    }

    /* L41088-41104: per-particle dispatch (DEAD in cap=1 flow). */
    if (part_idx == 1) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px + sn_y * 8.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz + cs_y * 8.0f);
    } else if (part_idx == 2) {
        float px = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
        float pz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px - sn_y * 8.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz - cs_y * 8.0f);
    }

    /* LAB_00443a5d → LAB_00444230: DRAG = 0. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);

    return 1;
}

/* Type 0x30 — reverse-yaw cone w/ optional people-target normalization.
 * 1-particle (engine sets iVar10=1 at LAB_00443dbe).
 *
 * Body (engine L41472-41510):
 *   - POS_X = sin(0.31415927 - yaw)*1.5 + owner+0x38
 *   - POS_Y = owner+0x3c + 1.5
 *   - POS_Z = owner+0x40 - cos(0.31415927 - yaw)*1.5
 *   - If owner+0xea0 == -1:
 *       VEL_X = sin(owner+0xea4)*0.7
 *       VEL_Y = 0
 *       VEL_Z = cos(owner+0xea4)*0.7
 *     Else (people-target):
 *       dx = people[idx].pos.x - POS_X
 *       dy = people[idx].pos.y - POS_Y
 *       dz = people[idx].pos.z - POS_Z
 *       len = sqrtf(dx² + dy² + dz²)        (engine FUN_005031e4)
 *       if len > 0: VEL = (dx, dy, dz) * 0.7 / len
 *       FUN_00503dd0(dx);  // atan2 w/ dropped return — SKIPPED (no
 *                          // observable side-effect on this slot)
 *   - LAB_004449b0: ROT_Z = rng_unit() * 2π
 *   - LAB_004449c1: DRAG = 20.0
 *   - LAB_00443dbe: AUX_C8 = 1  (iVar10=1 cap, fired automatically)
 *
 * Argless trig sites (L41112, L41118, L41131 — wait, those are 0x58/100)...
 * Actually for 0x30: paired sin/cos calls in the people branch use
 * normalized distance scalars (not trig); the only argless candidate
 * is L41131 in the else branch's `cos(owner+0xea4)` (paired with
 * L41128's sin call). */
static int init_entity_30(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    /* L41473-41481: pos via reverse-yaw cone. */
    float ang = 0.31415927f - g_scene1_camera_yaw;
    float sn = sinf(ang);
    float cs = cosf(ang);

    float pos_x = sn * 1.5f + owner_read_f(owner, 0x38);
    float pos_y = owner_read_f(owner, 0x3c) + 1.5f;
    float pos_z = owner_read_f(owner, 0x40) - cs * 1.5f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, pos_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pos_z);

    int people_idx = owner_read_i(owner, 0xea0);

    if (people_idx == -1) {
        /* L41108-41114: trig vel w/ owner+0xea4 angle. */
        float a = owner_read_f(owner, 0xea4);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(a) * 0.7f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(a) * 0.7f);
    } else if (people_idx >= 0 && people_idx < SCENE1_PEOPLE_COUNT) {
        /* L41087-41101: aim-at-people via normalized distance vector. */
        float dx = g_scene1_people[people_idx].pos[0] - pos_x;
        float dy = g_scene1_people[people_idx].pos[1] - pos_y;
        float dz = g_scene1_people[people_idx].pos[2] - pos_z;
        float len = sqrtf(dx*dx + dy*dy + dz*dz);
        if (len > 0.0f) {
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, (dx * 0.7f) / len);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, (dy * 0.7f) / len);
            slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, (dz * 0.7f) / len);
        }
        /* L41102: FUN_00503dd0(dx) — atan2 with dropped return value.
         * No observable side-effect on this slot.  Skipped. */
    }

    /* LAB_004449b0: ROT_Z = rng_unit() * 2π. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rng_next_unit() * B_TWO_PI_F);

    /* LAB_004449c1: DRAG = 20.0. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 20.0f);

    /* LAB_00443dbe: AUX_C8 = 1. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    return 1;
}

/* Type 0x9b — NPC-bend + LIFE_MULT.  1-particle.
 *
 * Body (engine L41029-41032):
 *   - ROT_X = ((float)(owner+0x948) * 2π) / 8     (NPC bend)
 *   - LIFE_MULT = 1.3f                            (engine raw 0x3fa66666)
 *   - Falls through to LAB_004457e7 (bVar14=(local_8==0), cap=1). */
static int init_entity_9b(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;
    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.3f);
    return 1;
}

/* Type 0x9d — NPC-bend + full pose + radial vel + SCALE_X.  1-particle
 * (engine returns explicitly after the body; same effect as cap=1 in
 * our outer loop).
 *
 * Body (engine L41034-41051):
 *   - LIFE_MULT = 1.3f
 *   - ROT_X = ((float)(owner+0x948) * 2π) / 8     (NPC bend)
 *   - POS = owner+0x20/0x24/0x28 with +1.0y       (NOT preamble's -0.5y)
 *   - ALT_POS = owner+0x20/0x24/0x28 with +0.9y
 *   - VEL_X = sin(ROT_X) * 2
 *   - VEL_Y = 0
 *   - VEL_Z = cos(ROT_X) * 2                      (argless paired)
 *   - SCALE_X = 10.0f                             (engine raw 0x41200000)
 *   - slot.TYPE = 0x9d  (redundant re-claim, preamble already set this)
 *   - EXPLICIT RETURN (skips post-body tail; our outer loop's cap=1
 *     achieves the same observable effect).
 *
 * Engine `(float)fVar15 + (float)fVar15` = 2*sin(angle); ported as
 * sinf(bend)*2.0. */
static int init_entity_9d(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)flag; (void)part_idx;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.3f);

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);

    float ox = owner_read_f(owner, 0x20);
    float oy = owner_read_f(owner, 0x24);
    float oz = owner_read_f(owner, 0x28);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, oz);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, oy + 0.9f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(bend) * 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(bend) * 2.0f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 10.0f);

    /* slot.TYPE = type re-claim (preamble already did this).  Engine
     * write at L41049: `*piVar13 = 0x9d`. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_TYPE, type);

    return 1;
}

/* ─── C8j.9 — remaining single-spawn + small multi-spawn types ────── */

/* Type 0x58 — drift-like body via LAB_004449c1 tail.  1-particle.
 *
 * Body (engine L41108-41124):
 *   VEL_X = sin(owner+0xea4) * 3
 *   VEL_Y = 0
 *   VEL_Z = cos(owner+0xea4) * 3        (argless paired with prior sin)
 *   POS_X -= sin(owner+0xea4) * 0.5
 *   POS_Y += 1.0
 *   POS_Z -= cos(owner+0xea4) * 0.5     (argless)
 *   ROT_X = (owner+0x948) * 2π / 8      (NPC bend)
 *   LAB_004449c1: DRAG = 20.0
 *   LAB_00443dbe: AUX_C8 = 1; cap = 1
 *
 * Differences vs the drift cluster (types 2/3/4/0x22/0x54/0x67):
 *   - No random ROT_Z (drift cluster does it via LAB_004449b0 entry).
 *   - No per-type SCALE_X override (preamble's 1.0 sticks).
 *
 * Argless cos sites verified via raw-asm in the 0x444500-0x444600
 * range — all reload `[esi+0xea4]` (the owner angle field).  PHC #7. */
static int init_entity_58(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float ang = owner_read_f(owner, 0xea4);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 3.0f);

    float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - ca * 0.5f);

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,  20.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    return 1;
}

/* Type 100 (0x64) — owner-anchored cone w/ dead per-particle dispatch.
 * 1-particle (engine bVar14 = (local_8 == 0) terminator).
 *
 * Body (engine L41126-41156):
 *   local_c = owner+0xea4
 *   POS_X = sin(local_c) * 0.5 + owner.x
 *   POS_Y = owner.y + 1.5
 *   POS_Z = cos(local_c) * 0.5 + owner.z   (argless)
 *   per-particle angle shift (DEAD in cap=1 flow):
 *     part_idx==1: local_c += π/10  (0.31415927)
 *     part_idx==2: local_c -= π/10
 *     part_idx==3: local_c += π/5   (0.62831855)
 *     part_idx==4: local_c -= π/5
 *   VEL_X = sin(local_c) * 0.4
 *   VEL_Y = 0
 *   VEL_Z = cos(local_c) * 0.4    (argless)
 *   slot.TYPE = 100  (redundant — preamble already wrote)
 *   slot byte 0xc2  = 2   (engine `(&DAT_06932572)[iVar10] = 2`)
 *   LIFE_MULT = 0.5    (= 0x3f000000)
 *   AUX_C8 = 1
 *   PART_IDX = local_8
 *   goto LAB_004457ee  (bVar14 set above to local_8==0 → cap=1) */
static int init_entity_100(int i, const void *owner, int type, int flag,
                           int part_idx)
{
    (void)flag;

    float local_c = owner_read_f(owner, 0xea4);

    float ox = owner_read_f(owner, 0x20);
    float oy = owner_read_f(owner, 0x24);
    float oz = owner_read_f(owner, 0x28);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sinf(local_c) * 0.5f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cosf(local_c) * 0.5f + oz);

    /* Per-particle shift — DEAD in cap=1 flow.  Port verbatim. */
    if (part_idx == 1) local_c += 0.31415927f;
    if (part_idx == 2) local_c -= 0.31415927f;
    if (part_idx == 3) local_c += 0.62831855f;
    if (part_idx == 4) local_c -= 0.62831855f;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(local_c) * 0.4f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(local_c) * 0.4f);

    slot_set_i(i, SCENE1_RECORDS_B_OFF_TYPE, type);  /* engine `*piVar13 = 100` */

    /* Byte 0xc2 = 2 — engine `(&DAT_06932572)[iVar10] = 2`. */
    {
        uint8_t *bytes = (uint8_t *)slot_base(i);
        bytes[0xc2] = 2;
    }

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx);

    return 1;
}

/* Types 0x74 / 0x79 — NPC-bend + 3-way owner+0x948 dispatch.  1-particle
 * (engine `goto LAB_004457e7` after body; no DRAG=20 / AUX_C8=1 tail).
 *
 * Body (engine L41158-41185):
 *   bend  = (owner+0x948) * 2π / 8
 *   POS_X = sin(bend) * 1.2 + owner.x
 *   POS_Y = owner.y + 1.3
 *   POS_Z = cos(bend) * 1.2 + owner.z   (default; modified by dispatch below)
 *   Three-way (owner+0x948) dispatch:
 *     mode 0:  POS_X -= 0.41
 *     mode 4:  POS_X += 0.41
 *     else:    POS_Z -= 0.1
 *   ROT_X = bend
 *   VEL_X = 2 * sin(bend)                (engine `fVar15 + fVar15`)
 *   VEL_Y = 0
 *   VEL_Z = 2 * cos(bend)                (argless paired)
 *   cap = 1.  No DRAG / AUX_C8 writes (LAB_004457e7 skips them). */
static int init_entity_74_79(int i, const void *owner, int type, int flag,
                             int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    int   npc_mode = owner_read_i(owner, 0x948);
    float bend     = (float)npc_mode * B_TWO_PI_F / 8.0f;
    float sb       = sinf(bend);
    float cb       = cosf(bend);

    float ox = owner_read_f(owner, 0x20);
    float oy = owner_read_f(owner, 0x24);
    float oz = owner_read_f(owner, 0x28);

    float px = sb * 1.2f + ox;
    float py = oy + 1.3f;
    float pz = cb * 1.2f + oz;

    /* 3-way dispatch (mode 0 / 4 / else). */
    if (npc_mode == 0)        px -= 0.41f;
    else if (npc_mode == 4)   px += 0.41f;
    else                      pz -= 0.1f;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sb + sb);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cb + cb);

    return 1;
}

/* Types 0x65 / 0x69 — NPC-bend + RNG-shifted angle, centered pos, fixed
 * vel.y lift.  8-particle (engine `LAB_004456c7: bVar14 = local_8 == 7`).
 *
 * Body (engine L41241-41262):
 *   u1     = rng_unit()
 *   ang    = (owner+0x948) * 2π / 8  +  (u1 - 0.5) * π
 *   POS    = (owner.x, owner.y + 3.0, owner.z)
 *   u2     = rng_unit()
 *   mag    = u2 * 0.08 + 0.04
 *   VEL_X  = sin(ang) * mag
 *   VEL_Y  = 0.7                       (= 0x3f333333)
 *   VEL_Z  = cos(ang) * mag            (argless paired with sin)
 *   LIFE_MULT = 0.3
 *   ROT_X  = ang
 *   DRAG   = 0.5
 *   AUX_C8 = 1
 *   PART_IDX = part_idx
 *   cap = 8 */
static int init_entity_65_69(int i, const void *owner, int type, int flag,
                             int part_idx)
{
    (void)type; (void)flag;

    float u1 = rng_next_unit();
    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    float ang  = bend + (u1 - 0.5f) * 3.1415927f;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x24) + 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x28));

    float u2 = rng_next_unit();
    float mag = u2 * 0.08f + 0.04f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang) * mag);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.7f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * mag);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,  0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx);

    return 8;
}

/* Type 0x6a — NPC-bend + per-particle ROT_X stride.  8-particle.
 *
 * Body (engine L41399-41420):
 *   bend  = (owner+0x948) * 2π / 8
 *   POS_X = sin(bend) * 0.8 + owner.x
 *   POS_Y = owner.y + 1.1
 *   POS_Z = cos(bend) * 0.8 + owner.z    (argless paired)
 *   VEL_X = sin(bend) * 0.1
 *   VEL_Y = 0
 *   VEL_Z = cos(bend) * 0.1              (argless)
 *   LIFE_MULT = 0.3
 *   ROT_SCR = rng_unit() * 2π
 *   ROT_Z   = rng_unit() * 2π
 *   ROT_X   = part_idx * 2π / 10
 *   SCALE_X = 0.5  (engine writes 0x3f000000 to (&DAT_06932564))
 *   DRAG    = 0.5
 *   AUX_C8  = 1
 *   cap = 8 (engine `goto LAB_004456c7` → bVar14 = local_8 == 7) */
static int init_entity_6a(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag;

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    float sb = sinf(bend);
    float cb = cosf(bend);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sb * 0.8f + owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x24) + 1.1f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               cb * 0.8f + owner_read_f(owner, 0x28));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(bend) * 0.1f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(bend) * 0.1f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,   rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,
               (float)part_idx * B_TWO_PI_F / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8,  1);

    return 8;
}

/* Type 0x61 — like 0x6a's body but no random rotations and 1-particle.
 *
 * Body (engine L41422-41440):
 *   bend  = (owner+0x948) * 2π / 8
 *   POS_X = sin(bend) * 0.8 + owner.x
 *   POS_Y = owner.y + 1.1
 *   POS_Z = cos(bend) * 0.8 + owner.z
 *   VEL_X = sin(bend) * 0.1
 *   VEL_Y = 0
 *   VEL_Z = cos(bend) * 0.1
 *   SCALE_X = 0.5
 *   LIFE_MULT = 0.3
 *   ROT_X = bend
 *   DRAG = 0.5
 *   AUX_C8 = 0   (NOTE — different from 0x6a which sets 1)
 *   cap = 1 (engine `goto LAB_004457e7`) */
static int init_entity_61(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    float sb = sinf(bend);
    float cb = cosf(bend);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sb * 0.8f + owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x24) + 1.1f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               cb * 0.8f + owner_read_f(owner, 0x28));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(bend) * 0.1f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(bend) * 0.1f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,   0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,     bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8,    0);

    return 1;
}

/* Type 0x62 — RNG-shifted angle, large-amp pos, vel.y bias.  1-particle.
 *
 * Body (engine L41447-41467):
 *   u1   = rng_unit()
 *   ang  = (owner+0x948) * 2π / 8  +  (u1 - 0.5) * 0.75
 *   POS_X = sin(ang) * 1.3 + owner.x
 *   POS_Y = owner.y + 1.2
 *   POS_Z = cos(ang) * 1.3 + owner.z   (argless paired)
 *   VEL_X = sin(ang) * 0.5
 *   u2    = rng_unit()
 *   VEL_Y = u2 * 0.1                   (positive bias)
 *   VEL_Z = cos(ang) * 0.5             (argless)
 *   LIFE_MULT = 0.3
 *   ROT_X = ang
 *   DRAG = 0.5
 *   LAB_00443db8: SCALE_X = 0.45      (uVar1 = 0x3ee66666)
 *   LAB_00443dbe: AUX_C8 = 1; cap = 1 */
static int init_entity_62(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float u1 = rng_next_unit();
    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    float ang  = bend + (u1 - 0.5f) * 0.75f;

    float sa = sinf(ang);
    float ca = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 1.3f + owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x24) + 1.2f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 1.3f + owner_read_f(owner, 0x28));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang) * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, rng_next_unit() * 0.1f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * 0.5f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,     ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,   0.45f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8,    1);

    return 1;
}

/* Types 0x8a / 0x8b — LAB_004451f0 body (vel * 1.0 — not 3.0).
 * 1-particle (engine `goto LAB_004457e1` → ROT_X=bend → LAB_004457e7).
 *
 * Body (engine L41815-41830 + 0x8a entry @ L41815; 0x8b entry @ L41443-41445):
 *   SCALE_X (per-type):
 *     0x8a → 0.2  (0x3e4ccccd)
 *     0x8b → 0.1  (0x3dcccccd)
 *   ang = owner+0xea4
 *   VEL_X = sin(ang)               (NOT *3.0 — different from 0x58)
 *   VEL_Y = 0
 *   VEL_Z = cos(ang)               (argless paired)
 *   POS_X -= sin(ang) * 0.5
 *   POS_Y += 1.0
 *   POS_Z -= cos(ang) * 0.5
 *   ROT_X = (owner+0x948) * 2π / 8
 *   cap = 1.  No DRAG / AUX_C8 writes. */
static int init_entity_8a_8b(int i, const void *owner, int type, int flag,
                             int part_idx)
{
    (void)flag; (void)part_idx;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,
               (type == 0x8a) ? 0.2f : 0.1f);

    float ang = owner_read_f(owner, 0xea4);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca);

    float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - ca * 0.5f);

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);

    return 1;
}

/* Types 0x5b / 0x5c / 0x5e / 0x85 / 0x86 / 0x87 — LAB_00444be6 shared body.
 * 1-particle (engine `goto LAB_00443dbe`).
 *
 * Body (engine L41526-41559):
 *   SCALE_X (per-type):
 *     0x5b → 0.7  (0x3f333333)
 *     0x5c → 1.0  (via LAB_00444beb)
 *     0x5e → 1.0
 *     0x85 → 0.0
 *     0x86 → 0.4  (0x3ecccccd)
 *     0x87 → 1.0
 *   ang = owner+0xea4
 *   VEL_X = sin(ang) * 3
 *   VEL_Y = 0
 *   VEL_Z = cos(ang) * 3                (argless)
 *   POS_X -= sin(ang) * 0.5
 *   POS_Y += 1.0
 *   POS_Z -= cos(ang) * 0.5             (argless)
 *   ROT_X = (owner+0x948) * 2π / 8
 *   LAB_00443dbe: AUX_C8 = 1; cap = 1.  No DRAG write (skipped). */
static int init_entity_lab_00444be6(int i, const void *owner, int type,
                                    int flag, int part_idx)
{
    (void)flag; (void)part_idx;

    float sx = 1.0f;  /* default for 0x5c / 0x5e / 0x87 */
    switch (type) {
    case 0x5b: sx = 0.7f; break;
    case 0x5c: sx = 1.0f; break;
    case 0x5e: sx = 1.0f; break;
    case 0x85: sx = 0.0f; break;
    case 0x86: sx = 0.4f; break;
    case 0x87: sx = 1.0f; break;
    default: break;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, sx);

    float ang = owner_read_f(owner, 0xea4);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 3.0f);

    float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - ca * 0.5f);

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    return 1;
}

/* Types 0x6d / 0x6e / 0x6f / 0x70 — drift-like body with random ROT_Z,
 * DRAG=20, AUX_C8=1, and cap = 3 (engine `bVar14 = local_8 == 2`).
 *
 * Body (engine L41569-41592):
 *   ang = owner+0xea4
 *   VEL_X = sin(ang) * 3
 *   VEL_Y = 0
 *   VEL_Z = cos(ang) * 3                 (argless paired)
 *   POS_X -= sin(ang) * 0.5
 *   POS_Y += 1.0
 *   POS_Z -= cos(ang) * 0.5              (argless)
 *   ROT_X = (owner+0x948) * 2π / 8
 *   ROT_Z = rng_unit() * 2π
 *   DRAG  = 20.0
 *   AUX_C8 = 1
 *   PART_IDX = part_idx - 1              (negative for first particle!)
 *   cap = 3 */
static int init_entity_6d_to_70(int i, const void *owner, int type, int flag,
                                int part_idx)
{
    (void)type; (void)flag;

    float ang = owner_read_f(owner, 0xea4);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 3.0f);

    float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - ca * 0.5f);

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 20.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx - 1);

    return 3;
}

/* Types 0x71 / 0x72 / 0x75 / 0x7d — owner+0xea4 vel + π/2-shifted rot.z.
 * 1-particle.
 *
 * Body (engine L41626-41648):
 *   SCALE_X (per-type):
 *     0x71 → 1.0   (preamble default — engine writes nothing here)
 *     0x72 → 0.3   (= 0x3e99999a)
 *     0x75 → 1.0   (preamble default)
 *     0x7d → 1.5   (= 0x3fc00000 via LAB_00444adc)
 *   if type != 0x75:
 *     ang = owner+0xea4
 *     VEL_X = sin(ang)               (*1.0)
 *     VEL_Y = 0
 *     VEL_Z = cos(ang)               (argless)
 *   POS_X -= sin(ang_of_owner) * 0.5
 *   POS_Y += 1.0
 *   POS_Z -= cos(ang) * 0.5          (argless)
 *   ROT_X = (owner+0x948) * 2π / 8
 *   ROT_Z = (rng_unit() - 0.5) * π/2   (= * 1.5707964)
 *   LAB_00443dbe: AUX_C8 = 1; cap = 1.  No DRAG write. */
static int init_entity_71_72_75_7d(int i, const void *owner, int type,
                                   int flag, int part_idx)
{
    (void)flag; (void)part_idx;

    /* Per-type SCALE_X.  0x71 / 0x75 keep preamble's 1.0. */
    if (type == 0x72) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 0.3f);
    if (type == 0x7d) slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 1.5f);

    float ang = owner_read_f(owner, 0xea4);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    if (type != 0x75) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca);
    }

    float cx = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_X);
    float cy = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Y);
    float cz = slot_get_f(i, SCENE1_RECORDS_B_OFF_POS_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, cx - sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, cy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cz - ca * 0.5f);

    float bend = (float)owner_read_i(owner, 0x948) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               (rng_next_unit() - 0.5f) * 1.5707964f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    return 1;
}

/* Type 8 — manual zero VEL, anchor pos.  1-particle (engine `iVar10 = 1`
 * explicit + `goto LAB_004455ed`).
 *
 * Body (engine L41509-41524):
 *   VEL = (0, 0, 0)
 *   POS_X = VEL_X * 10 + owner.x = owner.x         (engine writes literally
 *                                                  the multiply-by-10 expr)
 *   POS_Y = VEL_Y * 10 + owner.y + 2.0 = owner.y + 2.0
 *   POS_Z = VEL_Z * 10 + owner.z = owner.z
 *   DRAG = 20.0
 *   AUX_C8 = 1
 *   ROT_Z = (part_idx + 1) * 2π
 *   cap = 1 (iVar10=1 explicit) */
static int init_entity_8(int i, const void *owner, int type, int flag,
                         int part_idx)
{
    (void)type; (void)flag;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    /* Engine writes via the VEL*10 expressions — vel is zero, so values
     * land at owner.x / owner.y + 2.0 / owner.z.  Ported literally. */
    float vx = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_X);
    float vy = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Y);
    float vz = slot_get_f(i, SCENE1_RECORDS_B_OFF_VEL_Z);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, vx * 10.0f + owner_read_f(owner, 0x20));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               vy * 10.0f + owner_read_f(owner, 0x24) + 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, vz * 10.0f + owner_read_f(owner, 0x28));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    20.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8,  1);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               (float)(part_idx + 1) * B_TWO_PI_F);

    return 1;
}

/* Type 0x68 — RNG-driven amp + angle pos around owner; iterates the
 * people table looking for an n-th "available" entry (alive==1 AND both
 * sister gates at +0x720/+0x724 are zero AND horizontal distance from
 * OWNER to people[i].target < 16.0).  The selector is `owner.field_ea0`
 * — when (filter-pass count) == owner.field_ea0, that people entry's
 * `.target` becomes the alt-target; otherwise the engine generates a
 * random alt-target in a wider ring (amp = (u+0.5)*8) around the owner.
 * vel = (alt - pos) / 10.0, LIFE_MULT = 0.6, PART_IDX = part_idx.
 *
 * Engine FUN_0044376a decomp L291-343, raw asm 0x444070..0x4441c5.
 *
 * Engine quirks (C8j.9a — verified via raw-asm read):
 *
 * 1. The argless cos call at decomp L298 + L328 (FUN_00503994 with no
 *    Ghidra-visible args) is the PHC #7 pattern.  Raw asm at 0x444131
 *    shows `fld QWORD [ebp-0x2c]; fstp QWORD [esp]; call 0x503994` —
 *    the cos reloads the same angle stashed by the paired sin (asm
 *    0x44410e / 0x444114).  So `cosf(angle)` is verbatim, not a guess.
 *
 * 2. FUN_005031e4 (sqrt) at decomp L305 dropped its arg in Ghidra.
 *    Raw asm 0x444070..0x44409c shows the arg is
 *    `(owner.pos.x - people[i].target.x)^2 +
 *     (owner.pos.z - people[i].target.z)^2`
 *    — horizontal-plane distance only.  Y is NOT included.
 *
 * 3. Decomp's `if (local_10 != -NAN)` (L307) → vestigial sentinel.
 *    Raw asm 0x444194 = `cmp eax, 0xffffffff; je fallback`.  Since
 *    local_10 (the people iteration index) inits to 0 and only
 *    increments, the -1 sentinel branch is unreachable.  Likely an
 *    unfinished optimization in the engine; we drop the dead check.
 *
 * 4. Fallback (no-match) path uses owner.y VERBATIM (no lift), while
 *    the primary path uses owner.y + 20.0 for POS_Y.  So a 0x68 with
 *    a successful people-match aims its vel DOWN by (~20/10 = 2.0).
 *    With no match, vel.y ≈ 0 (alt.y == pos.y - 20.0 → vel.y = -2.0).
 *    Same drop-by-2 either way — only the alt's X/Z target differs.
 *
 * 5. Match-counter semantics: `iVar8` (matched_count) starts at 0 and
 *    only increments when alive+sister+distance pass.  The engine's
 *    target is `owner.field_ea0` — the n-th qualifying people slot.
 *    When matched_count == wanted_match, people[iter_idx].target is
 *    used (NOT people[matched_count] — they decouple). */
static int init_entity_68(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag;

    /* Primary path — amp = (u+0.5)*4, angle = u*2π. */
    float u_amp = rng_next_unit();
    float amp   = (u_amp + 0.5f) * 4.0f;
    float u_ang = rng_next_unit();
    float angle = u_ang * B_TWO_PI_F;
    float sa = sinf(angle);
    float ca = cosf(angle);

    float owner_x = owner_read_f(owner, 0x20);
    float owner_y = owner_read_f(owner, 0x24);
    float owner_z = owner_read_f(owner, 0x28);

    float pos_x = sa * amp + owner_x;
    float pos_y = owner_y + 20.0f;
    float pos_z = ca * amp + owner_z;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, pos_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pos_z);

    /* People-table scan (engine raw-asm loop 0x444075..0x4440db, 128
     * iterations bounded by piVar13 == &DAT_007c9678).  Filter:
     *   alive == 1
     *   sister_724 == 0
     *   sister_720 == 0
     *   sqrt(dx² + dz²) < 16.0 (horizontal distance owner ↔ target)
     * Selector: `owner.field_ea0` (n-th passing entry). */
    int wanted_match  = owner_read_i(owner, 0xea0);
    int matched_count = 0;
    int found         = 0;
    float alt_x = 0.0f, alt_y = 0.0f, alt_z = 0.0f;

    for (int idx = 0; idx < SCENE1_PEOPLE_COUNT; idx++) {
        const scene1_people_entry_t *p = &g_scene1_people[idx];
        if (p->alive != 1)       continue;
        if (p->sister_724 != 0)  continue;
        if (p->sister_720 != 0)  continue;

        float dx = owner_x - p->target[0];
        float dz = owner_z - p->target[2];
        float dist_h = sqrtf(dx * dx + dz * dz);
        if (!(dist_h < 16.0f)) continue;

        if (matched_count == wanted_match) {
            alt_x = p->target[0];
            alt_y = p->target[1];
            alt_z = p->target[2];
            found = 1;
            break;
        }
        matched_count++;
    }

    if (!found) {
        /* Fallback — wider amp ((u+0.5)*8), owner.y VERBATIM (no +20
         * lift).  Engine L322-329 / raw asm 0x4440e7..0x44414a. */
        float u_amp2 = rng_next_unit();
        float amp2   = (u_amp2 + 0.5f) * 8.0f;
        float u_ang2 = rng_next_unit();
        float ang2   = u_ang2 * B_TWO_PI_F;
        alt_x = sinf(ang2) * amp2 + owner_x;
        alt_y = owner_y;
        alt_z = cosf(ang2) * amp2 + owner_z;
    }

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, alt_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, alt_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, alt_z);

    /* Common tail (LAB_0044414a → 0x4441d2): vel = (alt - pos) / 10,
     * LIFE_MULT = 0.6 (= 0x3f19999a), PART_IDX = part_idx. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, (alt_x - pos_x) / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, (alt_y - pos_y) / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, (alt_z - pos_z) / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.6f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx);

    return 1;
}

/* Dispatch helper — routes a (slot, type, part_idx) to the right body
 * and returns the cap.  Used by scene1_record_b_spawn_entity's outer
 * loop to know when to stop committing slots. */
static int run_entity_body(int slot, const void *owner, int type,
                           int flag, int part_idx)
{
    switch (type) {
    case 0x24: return init_entity_24(slot, owner, type, flag, part_idx);
    case 0x60: return init_entity_60(slot, owner, type, flag, part_idx);
    case 0x82: return init_entity_82(slot, owner, type, flag, part_idx);

    case 0x3e: case 0x5f:
        return init_entity_3e_5f(slot, owner, type, flag, part_idx);

    case 2: case 3: case 4: case 0x22: case 0x54: case 0x67:
        return init_entity_drift_cluster(slot, owner, type, flag, part_idx);

    case 0x4d: case 0x4e: case 0x4f: case 0x50:
    case 0xa5: case 0xa6: case 99:
    case 0x51: case 0x52: case 0x53:
        return init_entity_cluster_a(slot, owner, type, flag, part_idx);

    case 0x73: case 0x76: case 0x77: case 0x78:
    case 0x7a: case 0x7b: case 0x7c: case 0x7e:
        return init_entity_mega_cluster_a(slot, owner, type, flag, part_idx);

    case 0x23: return init_entity_23(slot, owner, type, flag, part_idx);
    case 0x29: return init_entity_29(slot, owner, type, flag, part_idx);
    case 0x30: return init_entity_30(slot, owner, type, flag, part_idx);
    case 0x9b: return init_entity_9b(slot, owner, type, flag, part_idx);
    case 0x9d: return init_entity_9d(slot, owner, type, flag, part_idx);

    /* C8j.9 — additions. */
    case 0x58: return init_entity_58(slot, owner, type, flag, part_idx);
    case 100:  return init_entity_100(slot, owner, type, flag, part_idx);
    case 0x74: case 0x79:
        return init_entity_74_79(slot, owner, type, flag, part_idx);
    case 0x65: case 0x69:
        return init_entity_65_69(slot, owner, type, flag, part_idx);
    case 0x6a: return init_entity_6a(slot, owner, type, flag, part_idx);
    case 0x61: return init_entity_61(slot, owner, type, flag, part_idx);
    case 0x62: return init_entity_62(slot, owner, type, flag, part_idx);
    case 0x8a: case 0x8b:
        return init_entity_8a_8b(slot, owner, type, flag, part_idx);
    case 0x5b: case 0x5c: case 0x5e:
    case 0x85: case 0x86: case 0x87:
        return init_entity_lab_00444be6(slot, owner, type, flag, part_idx);
    case 0x6d: case 0x6e: case 0x6f: case 0x70:
        return init_entity_6d_to_70(slot, owner, type, flag, part_idx);
    case 0x71: case 0x72: case 0x75: case 0x7d:
        return init_entity_71_72_75_7d(slot, owner, type, flag, part_idx);
    case 8: return init_entity_8(slot, owner, type, flag, part_idx);

    /* C8j.9a — single-spawn with people-table sister-gate iteration. */
    case 0x68: return init_entity_68(slot, owner, type, flag, part_idx);

    default:
        /* Unreachable — outer dispatch gated by IMPLEMENTED. */
        return 1;
    }
}

/* ─── per-type bodies — NPC allocator ─────────────────────────────── */

/* Same per-particle body signature as the entity allocator (see
 * comment above init_entity_24).  Body returns the cap; outer loop
 * commits up to `cap` particles. */

/* LAB_00447584 trivial-tail group — pure preamble (no per-type body).
 *
 * Engine routes the following types directly to LAB_00447584 (the loop
 * tail), so the body is a no-op beyond what the preamble already wrote:
 *
 *   Group A (engine L42823, hit via `param_2 == X → goto LAB_00447584`):
 *     0xe, 0x97, 0x46
 *
 *   Group B (engine L42828, hit via 5-way "or" `param_2 == X` chain):
 *     0x24, 0xa, 0xb, 0x14, 0x13, 0x99
 *
 * All 9 types behave identically — slot stays at preamble defaults.
 * Engine line 951 ends with `bVar11 = local_8 == 1` so cap=1. */
static int init_npc_lab_00447584(int i, const void *owner, int type, int flag,
                                 int part_idx)
{
    (void)i; (void)owner; (void)type; (void)flag; (void)part_idx;
    return 1;
}

/* Owner-pos w/ explicit-return group — engine L42835 + the if-cluster
 * at L854-876.  Types 0x1e, 0x88, 0x89, 0x9a, 0x9e.
 *
 * Body shape (engine raw decomp L856-875):
 *
 *   ang = owner+0x420                              (NEW owner-angle field)
 *   POS  = owner+0x3f0..0x3f8 + +1.0y               (override preamble's no-lift)
 *   ALT  = owner+0x3f0..0x3f8 + +0.9y               (alt-target)
 *   VEL  = (2*sin(ang), 0, 2*cos(ang))              (sin/cos doubled by engine,
 *                                                    not *2 — matches asm `fadd st`)
 *
 * The engine `goto LAB_00447584` (the loop tail) is replaced by an
 * EXPLICIT `return` for these types — i.e., they only ever commit ONE
 * slot regardless of the outer loop state.  In our port the cap=1
 * outer-loop semantics already produce that behavior.
 *
 * 0x9e is the only type that additionally writes LIFE_MULT and
 * SCALE_X (engine raw 0x3fe66666 / 0x41200000):
 *   0x9e: LIFE_MULT = 1.8 (= 0x3fe66666),
 *         SCALE_X   = 10.0 (= 0x41200000).
 *
 * owner+0x420 has not been seen in earlier C8j chips; it's the
 * "NPC current orientation" angle used by ~10 other types (0x1f, 0x33,
 * 0x6b, etc.) — those land in later chips. */
static int init_npc_explicit_return(int i, const void *owner, int type,
                                    int flag, int part_idx)
{
    (void)flag; (void)part_idx;

    float ang = owner_read_f(owner, 0x420);
    float sa = sinf(ang);
    float ca = cosf(ang);

    float ox = owner_read_f(owner, 0x3f0);
    float oy = owner_read_f(owner, 0x3f4);
    float oz = owner_read_f(owner, 0x3f8);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,     ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,     oy + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,     oz);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, oy + 0.9f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, oz);

    /* Engine emits `sin + sin` / `cos + cos` (not `sin * 2`) — same
     * value, but worth noting since some hand-decomp might miss it. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa + sa);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca + ca);

    if (type == 0x9e) {
        /* 0x3fe66666 — note this is NOT 1.8 exactly: 0x3fe66666 = 1.8
         * truncated.  Engine's hex literal is what it is. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.8f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,   10.0f);
    }

    return 1;
}

/* ─── C8j.12 — owner+0x420 family of NPC single-spawn types ──────── */

/* atan2(0.1, 0.5) constant — every FUN_00503dd0 call in FUN_00445a8c
 * uses the same two .rdata doubles (ds:0x519c80 = 0.1, ds:0x519318 =
 * 0.5).  Raw asm verified at 0x4465e3 / 0x44668b / 0x446750 / 0x446827
 * / 0x4469af — pattern is `fld ds:0x519318; fstp [esp]; fld
 * ds:0x519c80; push ecx; push ecx; fstp [esp]; call 0x503dd0`, putting
 * 0.1 at [esp+0] (first arg = y) and 0.5 at [esp+8] (second arg = x).
 * Result is atan2f(0.1f, 0.5f) ≈ 0.19739556.  Constant-folded to avoid
 * a runtime atan2f call per spawn. */
#define B_ATAN2_0P1_0P5 0.19739555984988f

/* Type 0x33 — alt-pos source from owner+0x6fc/700/704; vel.y derived
 * from just-written POS_Y; vel.{x,z} radial via sin/cos(owner+0x420);
 * rot.z random.  cap=1, no DRAG/AUX_C8 (engine L42297-42314 → fall-
 * through LAB_00447584). */
static int init_npc_33(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float ang = owner_read_f(owner, 0x420);

    /* L42300-42302: pos from owner+0x6fc/700/704 (the alt-pos source
     * shared with 0x38).  POS_Y is read back below for vel.y. */
    float pos_y = owner_read_f(owner, 0x700);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x6fc));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x704));

    /* L42304-42310: vel.{x,z} = sin/cos(ang)*0.8; vel.y = -0.01 *
     * slot.POS_Y (engine reads back the dword just written; equivalent
     * to owner+0x700 * -0.01).  Argless cos = same ang per PHC #7. */
    float sa = sinf(ang);
    float ca = cosf(ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.8f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, pos_y * -0.01f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 0.8f);

    /* L42311-42313: LIFE_MULT = 0x3f333333 (≈0.7); rot.z = u*2π. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.7f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    return 1;
}

/* Type 0x27 — owner+0x420 ang pos*2.5 + +8y; vel.y=-0.05; DRAG=0; cap=1
 * (engine L42422-42446 → LAB_004462ed → LAB_004462f0 → LAB_0044757e). */
static int init_npc_27(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    /* L42423 is a dead write: slot.POS_Y += 2.0, immediately overwritten
     * at L42429 with owner+0x3f4 + 8.0.  Skipped — observable end-state
     * matches. */
    float ang = owner_read_f(owner, 0x420);
    float ox  = owner_read_f(owner, 0x3f0);
    float oy  = owner_read_f(owner, 0x3f4);
    float oz  = owner_read_f(owner, 0x3f8);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 2.5f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 8.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 2.5f + oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -0.05f);     /* 0xbd4ccccd */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 0.5f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    return 1;
}

/* Type 0x2b — RNG amp ∈ [0.1, 0.1125), ×1.5 when owner+0x424 == 0x45;
 * positive-bias vel.y; LIFE_MULT=0.2; DRAG=0; cap=1
 * (engine L42448-42471 → LAB_0044703f → fall-through LAB_00447584). */
static int init_npc_2b(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    /* L42449-42453: amp w/ owner+0x424 dispatch. */
    float amp = rng_next_unit() * 0.0125f + 0.1f;
    if (owner_read_i(owner, 0x424) == 0x45) amp *= 1.5f;

    /* L42454-42458: ang = u*2π; vel.x = sin(ang)*amp. */
    float ang = rng_next_unit() * B_TWO_PI_F;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang) * amp);

    /* L42460-42461: vel.y = u*0.8+0.2 ∈ [0.2, 1.0). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y,
               rng_next_unit() * 0.8f + 0.2f);

    /* L42462-42464: vel.z = cos(ang)*amp (argless cos = same ang). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * amp);

    /* L42465-42470: rot.z random; DRAG=0; LIFE_MULT=0x3e4ccccd (=0.2). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.2f);
    return 1;
}

/* Types 0x26 / 0x2a — owner+0x420 ang pos*2.5; per-type +y lift;
 * RNG amp ∈ [0.02, 0.22); per-type vel.y formula; DRAG=0; cap=1
 * (engine L42473-42514 → LAB_004462f0 → LAB_0044757e).
 *
 *   0x26 lift = 4.8, vel.y = -u*amp (downward)
 *   0x2a lift = 3.5, vel.y = u*(u'*0.8 - 0.4)*0.5 (signed via amp2). */
static int init_npc_26_2a(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)flag; (void)part_idx;

    /* L42474 is a dead write (POS_Y += 2.0, overwritten below). */

    float amp = rng_next_unit() * 0.2f + 0.02f;
    float ang = owner_read_f(owner, 0x420);
    float ox  = owner_read_f(owner, 0x3f0);
    float oy  = owner_read_f(owner, 0x3f4);
    float oz  = owner_read_f(owner, 0x3f8);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    float lift = (type == 0x26) ? 4.8f : 3.5f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 2.5f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + lift);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 2.5f + oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * amp);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * amp);

    float vel_y;
    if (type == 0x26) {
        vel_y = -(rng_next_unit() * amp);
    } else {
        float amp2 = rng_next_unit() * 0.8f - 0.4f;
        vel_y = rng_next_unit() * amp2 * 0.5f;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    return 1;
}

/* Types 0x31 / 0x32 — RNG amp ∈ [0.5, 0.67); 0x32 → amp *= 0.5;
 * vel.{x,z} = sin/cos(owner+0x420)*amp; per-type vel.y; cap=1
 * (engine L42516-42545 → LAB_0044701d → LAB_0044703f).
 * Tail: rot.z random, DRAG=0, LIFE_MULT=2.0. */
static int init_npc_31_32(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)flag; (void)part_idx;

    float amp = rng_next_unit() * 0.17f + 0.5f;
    if (type == 0x32) amp *= 0.5f;

    float ang = owner_read_f(owner, 0x420);
    float sa  = sinf(ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * amp);

    float vel_y;
    if (type == 0x31) {
        vel_y = (rng_next_unit() - 0.5f) * 0.1f;
    } else {
        vel_y = (rng_next_unit() - 1.5f) * amp;
    }
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vel_y);

    /* Argless cos = cos(ang) per PHC #7. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * amp);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    return 1;
}

/* Type 0x25 — +8.8y lift; RNG amp ∈ [0.1, 0.17); positive vel.y;
 * cap=1 (engine L42547-42569 → LAB_0044701d → LAB_0044703f).
 * Tail: rot.z random, DRAG=0, LIFE_MULT=2.0. */
static int init_npc_25(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    /* L42548 dead write: POS_Y += 2.0 (overwritten). */

    float amp = rng_next_unit() * 0.07f + 0.1f;
    float ang = owner_read_f(owner, 0x420);
    float ox  = owner_read_f(owner, 0x3f0);
    float oy  = owner_read_f(owner, 0x3f4);
    float oz  = owner_read_f(owner, 0x3f8);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 2.5f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 8.8f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 2.5f + oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * amp);
    /* vel.y = amp - u*0.3 (positive bias). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y,
               amp - rng_next_unit() * 0.3f);
    /* Argless cos = same ang per PHC #7. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * amp);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 2.0f);
    return 1;
}

/* Type 0x3b — vel.{x,z} = sin/cos(owner+0x420)*0.6, vel.y=0; DRAG=0;
 * cap=1 (engine L42571-42580 → LAB_004462ed → LAB_004462f0 →
 * LAB_0044757e). */
static int init_npc_3b(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float ang = owner_read_f(owner, 0x420);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(ang) * 0.6f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    /* Argless cos = same ang per PHC #7. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * 0.6f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    return 1;
}

/* Type 0x28 — owner+0x420 ang vel*0.3; vel.y=0.13; pos = 2*VEL + owner
 * + +0.8y on Y; LIFE_MULT=0.5; DRAG=20; ROT_SCR=atan2 const; ROT_X=ang;
 * cap=1 (engine L42754-42782 → LAB_00446f4d → LAB_00446f7d).
 *
 * Engine writes DRAG=2.0 at L42764 then overwrites with 20.0 at L42774
 * — only the final write preserved. */
static int init_npc_28(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float ang = owner_read_f(owner, 0x420);
    float sa  = sinf(ang);
    float ca  = cosf(ang);
    float vx  = sa * 0.3f;
    float vy  = 0.13f;                                       /* 0x3e051eb8 */
    float vz  = ca * 0.3f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.5f);

    float ox = owner_read_f(owner, 0x3f0);
    float oy = owner_read_f(owner, 0x3f4);
    float oz = owner_read_f(owner, 0x3f8);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, vx + vx + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, vy + vy + oy + 0.8f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, vz + vz + oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    20.0f);     /* 0x41a00000 */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   ang);
    return 1;
}

/* Type 0x38 — owner+0x420 ang vel*0.5; pos from owner+0x6fc (alt
 * source, same as 0x33); LIFE_MULT=3.8; DRAG=3.0; ROT_SCR=atan2 const;
 * ROT_X=ang; cap=1 (engine L42784-42798 → LAB_00446f4d). */
static int init_npc_38(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float ang = owner_read_f(owner, 0x420);
    float sa  = sinf(ang);
    float ca  = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 0.5f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 3.8f);    /* 0x40733333 */

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x6fc));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, owner_read_f(owner, 0x700));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x704));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    3.0f);      /* 0x40400000 */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   ang);
    return 1;
}

/* Type 0x21 — random ±π/10 bias around owner+0x420; RNG amp ∈ [0.025,
 * 0.05); vel.y=0.02 (constant positive); pos at owner.pos + +1.8y;
 * rot.z random; DRAG=20; cap=1 (engine L42800-42821 → LAB_004466a8 →
 * LAB_0044757e). */
static int init_npc_21(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float ang_base = owner_read_f(owner, 0x420);
    /* (u - 0.5) * 0.62831855 = (u - 0.5) * (π/5).  Range: ±π/10. */
    float ang = (rng_next_unit() - 0.5f) * 0.62831855f + ang_base;
    float amp = (rng_next_unit() + 1.0f) * 0.025f;

    float sa = sinf(ang);
    float ca = cosf(ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * amp);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.02f);       /* 0x3ca3d70a */
    /* Argless cos = same ang per PHC #7. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * amp);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 1.8f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 20.0f);        /* 0x41a00000 */
    return 1;
}

/* Type 0x6b — NPC-bend (owner+0x18, NOT +0x420); RNG amp ∈ [4, 8);
 * pos = sin/cos(bend)*amp + owner + +0.2y; ROT_SCR=atan2 const;
 * ROT_X=bend; DRAG=0; cap=1 (engine L42637-42656 → LAB_004462f0
 * → LAB_0044757e). */
static int init_npc_6b(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    float amp  = (rng_next_unit() + 1.0f) * 4.0f;
    float sa   = sinf(bend);
    float ca   = cosf(bend);
    float ox   = owner_read_f(owner, 0x3f0);
    float oy   = owner_read_f(owner, 0x3f4);
    float oz   = owner_read_f(owner, 0x3f8);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * amp + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 0.2f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * amp + oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    0.0f);
    return 1;
}

/* Type 0x6c — NPC-bend; vel = sin/cos(bend)*0.2; pos = 3*VEL + owner +
 * +1.5y; ROT_SCR=atan2 const; ROT_X=bend; DRAG=1.0; cap=1
 * (engine L42658-42680 → LAB_0044757e). */
static int init_npc_6c(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    float vx   = sinf(bend) * 0.2f;
    float vy   = 0.0f;
    float vz   = cosf(bend) * 0.2f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, vx);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, vy);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, vz);

    float ox = owner_read_f(owner, 0x3f0);
    float oy = owner_read_f(owner, 0x3f4);
    float oz = owner_read_f(owner, 0x3f8);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, vx * 3.0f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, vy * 3.0f + oy + 1.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, vz * 3.0f + oz);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    1.0f);      /* 0x3f800000 */
    return 1;
}

/* Type 0x1f — NPC-bend (owner+0x18); 5-way amp switch on owner+0x424
 * (0.1 default, 0.12/0.14/0.16/0.15/0.2 for 7/8/9/0x24/0x23); 2-way
 * pos branch on owner+0x424 (0x24 or 0x23 → pos = sin/cos(bend)*1.5;
 * else pos = VEL*3); ROT_SCR=atan2 const; ROT_X=bend; DRAG=20;
 * AUX_C8=1; cap=1 (engine L42682-42733 → LAB_004469d2 → LAB_00447cb8). */
static int init_npc_1f(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    int   sub  = owner_read_i(owner, 0x424);
    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;

    /* L42686-42701: write-after-write switch — no else, so for
     * overlapping conditions the LAST matching write wins (they don't
     * overlap here since values are distinct). */
    float amp = 0.1f;
    if (sub == 7)    amp = 0.12f;
    if (sub == 8)    amp = 0.14f;
    if (sub == 9)    amp = 0.16f;
    if (sub == 0x24) amp = 0.15f;
    if (sub == 0x23) amp = 0.2f;

    float sa = sinf(bend);
    float ca = cosf(bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * amp);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * amp);

    float ox = owner_read_f(owner, 0x3f0);
    float oy = owner_read_f(owner, 0x3f4);
    float oz = owner_read_f(owner, 0x3f8);

    if (sub == 0x24 || sub == 0x23) {
        /* L42712-42718: pos = sin/cos(bend)*1.5 + owner.  Engine re-
         * calls sin/cos; mathematically equivalent to cached sa/ca. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 1.5f + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.5f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 1.5f + oz);
    } else {
        /* L42720-42725: pos = VEL*3 + owner (vel.y=0 → +1.5y absorbed). */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * amp * 3.0f + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.5f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * amp * 3.0f + oz);
    }

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   bend);

    /* LAB_004469d2: DRAG=20.0, AUX_C8=1.  uVar5=1 → cap=1. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 20.0f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    return 1;
}

/* NPC cluster B — types 0x4d, 0x4e, 0x4f, 0x50, 0xa5, 0xa6 (engine
 * L42112-42161).  MULTI-particle: 0x4f→3, 0x50→5, 0xa5→6, 0xa6→8;
 * 0x4d/0x4e are 1-particle.  Simpler than cluster A:
 *
 *   local_1c = (owner+0x18) * 2π / 8                   (NPC bend at +0x18)
 *   per-particle shifts (5 only — engine omits the 5/6/7 cases):
 *     part_idx 0 →  0
 *              1 → -0.18
 *              2 → +0.18
 *              3 → -0.36
 *              4 → +0.36
 *   POS_X     = sin(local_1c) * 0.8 + owner+0x3f0
 *   POS_Y     = owner+0x3f4 + 1.4
 *   POS_Z     = cos(local_1c) * 0.8 + owner+0x3f8
 *   LIFE_MULT = 0.4   (hardcoded — no per-type override)
 *   VEL_Y     = 0
 *   VEL_X     = sin(local_1c) * 0.5
 *   VEL_Z     = cos(local_1c) * 0.5
 *   ROT_X     = local_1c
 *   DRAG      = 0.5
 *   AUX_C8    = 1
 *   Cap:
 *     0x4f → 3, 0x50 → 5, 0xa5 → 6, 0xa6 → 8, else → 1.
 *
 * No SCALE_X override (preamble default 1.0 carries).  No 0x53 special.
 *
 * Argless cos at L42141 (after L42138 sin(local_10)) follows the same
 * `[ebp-Nx]` reload pattern as the entity allocator's cluster A — same
 * PHC #7 finding, no separate raw-asm check required. */
static int init_npc_cluster_b(int i, const void *owner, int type, int flag,
                              int part_idx)
{
    (void)flag;

    /* L42115: bend from owner+0x18 (NPC shape — DIFFERENT field from
     * entity shape's 0x948). */
    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    float local_1c = bend;

    /* L42116-42127: per-particle shifts (only 4 explicit cases — engine
     * uses cluster A's 8-shift table only for entity alloc). */
    static const float shifts[5] = {
        0.0f, -0.18f, +0.18f, -0.36f, +0.36f,
    };
    if (part_idx >= 0 && part_idx < 5) local_1c += shifts[part_idx];

    float ox = owner_read_f(owner, 0x3f0);
    float oy = owner_read_f(owner, 0x3f4);
    float oz = owner_read_f(owner, 0x3f8);

    float sc = sinf(local_1c);
    float cc = cosf(local_1c);

    /* L42131-42135: pos write. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sc * 0.8f + ox);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 1.4f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, cc * 0.8f + oz);

    /* L42136-42144: LIFE_MULT, vel. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.4f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sinf(local_1c) * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(local_1c) * 0.5f);

    /* L42157-42159: tail state. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, local_1c);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,  0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);

    /* L42145-42156: per-type cap. */
    int cap = 1;
    if (type == 0x4f) cap = 3;
    if (type == 0x50) cap = 5;
    if (type == 0xa5) cap = 6;
    if (type == 0xa6) cap = 8;
    return cap;
}

/* NPC type 0x56 — NPC-bend pos w/ matrix-init (engine L42049-42106).
 *
 * Body (raw asm 0x445c20..0x445c97 verified):
 *
 *   local_24 = bend = (owner+0x18) * 2π / 8
 *   POS_X    = sin(bend)*1.5 + owner+0x3f0
 *   POS_Y    = owner+0x3f4 + 1.8
 *   POS_Z    = cos(bend)*1.5 + owner+0x3f8
 *   VEL_X    = sin(bend)*0.3
 *   VEL_Y    = 0.15                            (= 0x3e19999a)
 *   VEL_Z    = cos(bend)*0.3
 *   ROT_SCR  = u1 * 2π                          (random RotX angle)
 *   ROT_Z    = u2 * 2π                          (random RotY angle)
 *   slot.MATRIX0 = RotY(ROT_Z) × RotX(ROT_SCR)
 *     — engine chains mat4_rotation_x(MATRIX0, ROT_SCR),
 *       mat4_rotation_y(scratch, ROT_Z),
 *       mat4_mul(MATRIX0, scratch, MATRIX0) — so MATRIX0 ends up as
 *       scratch * MATRIX0 = RotY * RotX.
 *   LIFE_MULT = 0.15
 *   ROT_X     = bend                            (LAB_00445c9a)
 *   DRAG      = 0.5                             (LAB_004469d2)
 *   AUX_C8    = 1
 *   cap = 1
 *
 * Engine quirk: thunk_FUN_004a2a03 (D3DXMatrixMultiply) call at
 * 0x445c86 looks argless in Ghidra but raw asm shows 3 stack pushes
 * (esi, eax, esi) — out=MATRIX0, a=scratch (local_8c), b=MATRIX0.
 * math3d.h::mat4_mul handles out==b safely via an internal temporary. */
static int init_npc_56(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    float sa   = sinf(bend);
    float ca   = cosf(bend);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sa * 1.5f + owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 1.8f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               ca * 1.5f + owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.15f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 0.3f);

    float rot_x_angle = rng_next_unit() * B_TWO_PI_F;   /* slot.ROT_SCR */
    float rot_y_angle = rng_next_unit() * B_TWO_PI_F;   /* slot.ROT_Z   */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, rot_x_angle);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,   rot_y_angle);

    /* slot.MATRIX0 = RotY(rot_y_angle) × RotX(rot_x_angle).  Use
     * float[16] scratch then write into slot via memcpy — slot fields
     * are int32_t but the bit-pattern is the float matrix. */
    float matrix_rx[16];
    float matrix_ry[16];
    float matrix_out[16];
    mat4_rotation_x(matrix_rx, rot_x_angle);
    mat4_rotation_y(matrix_ry, rot_y_angle);
    mat4_mul(matrix_out, matrix_ry, matrix_rx);
    int32_t *r = slot_base(i);
    memcpy(&r[SCENE1_RECORDS_B_OFF_MATRIX0], matrix_out, sizeof matrix_out);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.15f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,     bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,      0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8,    1);

    return 1;
}

/* NPC type 0x53 — low-lift drift body (engine L42107-42129).
 *
 *   bend = (owner+0x18) * 2π / 8
 *   POS_X = sin(bend)*0.3 + owner+0x3f0
 *   POS_Y = owner+0x3f4 + 0.08             (LOW lift)
 *   POS_Z = cos(bend)*0.3 + owner+0x3f8
 *   VEL_X = sin(bend)*0.5
 *   VEL_Y = 0
 *   VEL_Z = cos(bend)*0.5                  (argless cos at L122 = PHC #7)
 *   ROT_X = bend                            (LAB_00447572)
 *   DRAG  = 0.5                             (LAB_0044757e via uVar2=0x3f000000)
 *
 * NO AUX_C8 = 1 here (LAB_0044757e doesn't set it — only LAB_004469d2
 * does, and 0x53 skips that label).  Preamble's AUX_C8=0 carries. */
static int init_npc_53(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    float sa   = sinf(bend);
    float ca   = cosf(bend);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sa * 0.3f + owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 0.08f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               ca * 0.3f + owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 0.5f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,  0.5f);
    return 1;
}

/* NPC type 0x51 — cluster-B-shaped body w/ +0.7 lift; CAP=1 (engine
 * L42199-42250).
 *
 * Engine computes a per-particle angle shift identical to cluster B's
 * (4 explicit shifts for part_idx ∈ {1..4}).  Since cap=1, part_idx
 * is always 0 in normal flow, so the shifts are dead code — but the
 * engine emits them so we port faithfully for layout-equivalence.
 *
 *   bend = (owner+0x18) * 2π / 8
 *   local_1c = bend + shift[part_idx]    (shift[0]=0, shift[1]=-0.18, ...)
 *   POS_X = sin(local_1c)*0.3 + owner+0x3f0
 *   POS_Y = owner+0x3f4 + 0.7
 *   POS_Z = cos(local_1c)*0.3 + owner+0x3f8
 *   VEL_X = sin(local_1c)*0.5
 *   VEL_Y = 0
 *   VEL_Z = cos(local_1c)*0.5            (argless cos at L245 = PHC #7)
 *   ROT_X = local_1c                      (LAB_00445c9a)
 *   DRAG  = 0.5                           (LAB_004469d2)
 *   AUX_C8 = 1
 *   cap = 1 */
static int init_npc_51(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag;

    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    static const float shifts[5] = {
        0.0f, -0.18f, +0.18f, -0.36f, +0.36f,
    };
    float local_1c = bend;
    if (part_idx >= 0 && part_idx < 5) local_1c += shifts[part_idx];

    float sa = sinf(local_1c);
    float ca = cosf(local_1c);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sa * 0.3f + owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 0.7f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               ca * 0.3f + owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 0.5f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,  local_1c);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,   0.5f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_C8, 1);
    return 1;
}

/* NPC type 0x68 — player-aim variant (engine L42164-42217).
 *
 * Distinct from entity 0x68: NO people-table scan; alt-target is
 * unconditionally derived from player_pos (DAT_056da1d8/dc/e0) with a
 * random RNG ring around it.
 *
 *   amp1  = (u + 0.5) * 4.0
 *   ang1  = u'*2π
 *   POS_X = sin(ang1)*amp1 + owner+0x3f0
 *   POS_Y = owner+0x3f4 + 20.0
 *   POS_Z = cos(ang1)*amp1 + owner+0x3f8
 *
 *   amp2  = (u + 0.5) * 8.0
 *   ang2  = u'*2π
 *   ALT_X = sin(ang2)*amp2 + player_pos.x
 *   ALT_Y = player_pos.y
 *   ALT_Z = cos(ang2)*amp2 + player_pos.z     (argless cos at L205 = PHC #7)
 *
 *   VEL = (ALT - POS) / 10
 *   LIFE_MULT = 0.6
 *   PART_IDX  = part_idx
 *   cap = 1
 *
 * DRAG stays at preamble 0 (no override — entity 0x68 also leaves it
 * at 0). */
static int init_npc_68(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag;

    /* Primary path — RNG amp + angle around owner. */
    float u_amp = rng_next_unit();
    float amp   = (u_amp + 0.5f) * 4.0f;
    float u_ang = rng_next_unit();
    float angle = u_ang * B_TWO_PI_F;
    float sa = sinf(angle);
    float ca = cosf(angle);

    float pos_x = sa * amp + owner_read_f(owner, 0x3f0);
    float pos_y = owner_read_f(owner, 0x3f4) + 20.0f;
    float pos_z = ca * amp + owner_read_f(owner, 0x3f8);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, pos_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, pos_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pos_z);

    /* Alt-target ring around player_pos (engine `DAT_056da1d8/dc/e0`). */
    float u_amp2 = rng_next_unit();
    float amp2   = (u_amp2 + 0.5f) * 8.0f;
    float u_ang2 = rng_next_unit();
    float ang2   = u_ang2 * B_TWO_PI_F;
    float alt_x  = sinf(ang2) * amp2 + g_scene1_player_pos[0];
    float alt_y  = g_scene1_player_pos[1];
    float alt_z  = cosf(ang2) * amp2 + g_scene1_player_pos[2];

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X, alt_x);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y, alt_y);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z, alt_z);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, (alt_x - pos_x) / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, (alt_y - pos_y) / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, (alt_z - pos_z) / 10.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 0.6f);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX,  part_idx);

    return 1;
}

/* ─── C8j.13 — NPC allocator remainder ────────────────────────────── */

/* Type 0x2f — 6-particle fan: per-particle ROT_Z evenly spaced + ALT_POS
 * fields seed PARAM-style life caps + AUX_B0 splits the 6 particles into
 * 0..2 vs 3..5 groups (engine L42316-42354).
 *
 *   ROT_Z       = part_idx * 2π/3 + π/2   (3-way fan, repeats x2)
 *   ROT_X       = owner+0x420              (NPC orientation)
 *   ROT_SCR     = u*300 + 10               (life cap stash at +0x88)
 *   ALT_POS_X   = (u+0.5)*π/10
 *   ALT_POS_Y   = u*2π
 *   POS         = sin/cos(owner+0x420)*0.5 + owner+0x3f0/4/8 + +1.5y
 *   VEL.x/z     = sin/cos(owner+0x420)*2 ; VEL_Y = 0
 *     (argless cos at L42340 reloads `[ebp-0x1c]` per PHC #7 = same ang)
 *   slot.TYPE   = 0x2f                     (explicit re-write)
 *   PART_IDX    = part_idx % 3             (group index 0..2 cycled)
 *   AUX_B0      = (part_idx < 3) ? 0 : 1   (first 3 vs last 3 split)
 *   cap = 6
 *
 * Engine increments local_8 at the END of the body (L42352); our outer
 * loop calls the body with part_idx = 0..5 then increments. */
static int init_npc_2f(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag;

    float rot_z = (float)part_idx * B_TWO_PI_F / 3.0f + 1.5707964f; /* +π/2 */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rot_z);

    float ang = owner_read_f(owner, 0x420);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, ang);

    /* L42321-42322: ROT_SCR = u*300 + 10 (engine stashes a life cap into
     * the rot-scratch field — consumer is the per-frame tick). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR,
               rng_next_unit() * 300.0f + 10.0f);
    /* L42324: ALT_POS_X = (u+0.5)*π/10 (≈ rng cone half-width). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X,
               (rng_next_unit() + 0.5f) * 0.31415927f);
    /* L42327: ALT_POS_Y = u*2π. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y,
               rng_next_unit() * B_TWO_PI_F);

    float sa = sinf(ang);
    float ca = cosf(ang);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               sa * 0.5f + owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 1.5f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               ca * 0.5f + owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    /* Argless cos at L42340 — same ang per PHC #7 (`[ebp-0x1c]` reload). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, ca * 2.0f);

    /* slot.TYPE is set to 0x2f at L42344 (already from preamble — engine
     * just re-asserts).  PART_IDX = part_idx % 3. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx % 3);

    /* L42346-42351: AUX_B0 = 0 for first 3 particles, 1 for last 3. */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_B0, (part_idx < 3) ? 0 : 1);

    return 6;
}

/* Types 0x2e / 0x36 — shared body (engine L42356-42420).
 *
 * Phase 1 (common):  pick local_14 = owner+0x420 ± (u+1)*π/4 (sign coin
 * via rng_next15()&1).  For 0x2e, override local_14 = u*π (fresh random).
 *
 * Phase 2 (per-type branch):
 *   0x36 (8 particles, indexed per-particle pos):
 *     POS_X = owner[part_idx*0xc + 0x708]
 *     POS_Y = owner[part_idx*0xc + 0x70c]
 *     POS_Z = owner[part_idx*0xc + 0x710]
 *     VEL_X = (POS_X - owner+0x3f0) * 0.02
 *     VEL_Y = (POS_Y - (owner+0x3f4 + 10)) * 0.02
 *     VEL_Z = (POS_Z - owner+0x3f8) * 0.02
 *   0x2e (1 particle, sin/cos pos around owner):
 *     POS_X = sin(local_14)*0.5 + owner+0x3f0
 *     POS_Y = owner+0x3f4 + 4.0
 *     POS_Z = cos(local_14)*0.5 + owner+0x3f8
 *     VEL_X = sin(local_14)*0.4
 *     VEL_Y = 0.14   (= 0x3e0f5c29, sign flipped to -0.14 in 0x2e tail)
 *     VEL_Z = cos(local_14)*0.4  (argless cos = same local_14 per PHC #7)
 *
 * Phase 3 (common tail):
 *   ALT_POS_X = (u-0.5)*3
 *   ALT_POS_Y = u + 1
 *   ALT_POS_Z = (u-0.5)*3
 *   ROT_Z     = u * 2π
 *
 * Phase 4 (0x2e only — sign-flip tail):
 *   VEL_Y = -VEL_Y       (so 0x2e's vy ends at -0.14)
 *   POS_Y -= 4.0         (cancels the +4 lift back to owner.y)
 *
 * Cap: 0x2e → 1, 0x36 → 8. */
static int init_npc_2e_36(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)flag;

    /* Phase 1 — common angle calc (consumes 1 u + 1 rng_next15 coin). */
    float ang = owner_read_f(owner, 0x420);
    float spread = (rng_next_unit() + 1.0f) * 0.7853982f; /* π/4 */
    if ((rng_next15() & 1) == 0) {
        ang = ang - spread;
    } else {
        ang = ang + spread;
    }
    /* L42368-42371: 0x2e overrides local_14 with a fresh random angle. */
    if (type == 0x2e) {
        ang = rng_next_unit() * 3.1415927f;
    }

    /* Phase 2 — per-type pos/vel. */
    float ox = owner_read_f(owner, 0x3f0);
    float oy = owner_read_f(owner, 0x3f4);
    float oz = owner_read_f(owner, 0x3f8);

    if (type == 0x36) {
        /* L42372-42383: per-particle pos from owner+(part*0xc + 0x708/c/10).
         *   POS_X is reloaded after compute for vel.x diff (engine reads
         *   the just-written slot field; ours uses the local). */
        int base = part_idx * 0xc;
        float px = owner_read_f(owner, base + 0x708);
        float py = owner_read_f(owner, base + 0x70c);
        float pz = owner_read_f(owner, base + 0x710);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, px);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, py);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, pz);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, (px - ox)             * 0.02f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, (py - (oy + 10.0f))   * 0.02f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, (pz - oz)             * 0.02f);
    } else {
        /* 0x2e else-branch (L42384-42400). */
        float sa = sinf(ang);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, sa * 0.5f + ox);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy + 4.0f);
        float ca = cosf(ang);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, ca * 0.5f + oz);
        float sa2 = sinf(ang);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa2 * 0.4f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.14f);     /* 0x3e0f5c29 */
        /* Argless cos at L42397 = same ang per PHC #7. */
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(ang) * 0.4f);
    }

    /* Phase 3 — common tail (L42402-42410). */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_X,
               (rng_next_unit() - 0.5f) * 3.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Y,
               rng_next_unit() + 1.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ALT_POS_Z,
               (rng_next_unit() - 0.5f) * 3.0f);
    /* L42408-42410: 1 RNG consumed BEFORE local_8++; ROT_Z gets the
     * subsequent u*2π value.  In our outer-loop refactor part_idx is
     * incremented after we return, so the RNG ordering is identical. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z,
               rng_next_unit() * B_TWO_PI_F);

    /* Phase 4 — 0x2e sign-flip tail (L42411-42413).  Engine reads the
     * just-written slot field; we mirror by inline math (vy was 0.14). */
    if (type == 0x2e) {
        slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -0.14f);
        slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y, oy);
    }

    return (type == 0x36) ? 8 : 1;
}

/* Type 0x3c — minimal-body anchor (engine L42582 + LAB_004462f0 +
 * LAB_0044757e + LAB_00447584).  Pure preamble + DRAG=0.  cap=1. */
static int init_npc_3c(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)owner; (void)type; (void)flag; (void)part_idx;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    return 1;
}

/* Type 0x98 — 5-particle NPC fan with per-particle wider shifts
 * (engine L42583-42619).  Wider than cluster-B's ±0.18/0.36 (±0.38/0.56).
 *
 *   bend = (owner+0x18) * 2π / 8
 *   per-particle shifts (only 4 explicit cases — part_idx 0 → 0):
 *      1 → -0.38
 *      2 → +0.38
 *      3 → -0.56
 *      4 → +0.56
 *   VEL_X     = sin(local_1c) * 0.3
 *   VEL_Y     = 0
 *   VEL_Z     = cos(local_1c) * 0.3
 *   POS       = owner+0x3f0..0x3f8 + 0.25y lift
 *   ROT_SCR   = atan2(0.1, 0.5) = 0.19739556
 *   ROT_X     = local_1c (= bend + shift)
 *   DRAG      = 20.0f
 *   PART_IDX  = part_idx
 *   cap = 5
 *
 * Argless cos at L42603 (after L42599 sin(local_24)) follows PHC #7. */
static int init_npc_98(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag;

    float local_1c = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    if (part_idx == 1) local_1c -= 0.38f;
    if (part_idx == 2) local_1c += 0.38f;
    if (part_idx == 3) local_1c -= 0.56f;
    if (part_idx == 4) local_1c += 0.56f;

    float sa = sinf(local_1c);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, sa * 0.3f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    /* Argless cos = same local_1c per PHC #7. */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, cosf(local_1c) * 0.3f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 0.25f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   local_1c);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    20.0f);    /* 0x41a00000 */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx);
    return 5;
}

/* Type 0x5a — zero-vel point release + atan2 stash (engine L42620-42636).
 *
 *   local_18 = (owner+0x18) * 2π / 8     (bend)
 *   VEL.x/y/z = 0
 *   POS       = owner+0x3f0..0x3f8 + 0.25y lift
 *   ROT_SCR   = atan2(0.1, 0.5)
 *   ROT_X     = local_18
 *   DRAG      = 20.0  (via LAB_004466a8 → LAB_0044757e)
 *   cap = 1 (LAB_00447584). */
static int init_npc_5a(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;

    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 0.25f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x3f8));

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, B_ATAN2_0P1_0P5);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,   bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG,    20.0f);
    return 1;
}

/* Types 0xf / 0x12 — SCALE_X-only anchor (engine L42822-42826 + L42924).
 * 0xf → SCALE_X = 0.2  (= 0x3e4ccccd)
 * 0x12 → SCALE_X = 3.0 (= 0x40400000)
 * Then LAB_00446b7a → LAB_00447584; cap = 1. */
static int init_npc_f_12(int i, const void *owner, int type, int flag,
                         int part_idx)
{
    (void)owner; (void)flag; (void)part_idx;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X,
               (type == 0x12) ? 3.0f : 0.2f);
    return 1;
}

/* Type 0x9c — NPC-bend + LIFE_MULT=1.8 (engine L42904-42909 + LAB_0044703f).
 *
 *   ROT_X      = (owner+0x18) * 2π / 8         (NPC bend)
 *   LIFE_MULT  = 1.8                            (= 0x3fe66666)
 *   cap = 1 (falls through LAB_0044703f → LAB_00447584). */
static int init_npc_9c(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag; (void)part_idx;
    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,     bend);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_LIFE_MULT, 1.7999998f);  /* 0x3fe66666 */
    return 1;
}

/* Type 0x3a — centered random pos ±5 around player + RotY matrix
 * (engine L42735-42753 + LAB_004462f0 → LAB_0044757e → LAB_00447584).
 *
 *   VEL_X     = 0
 *   VEL_Y     = -0.3        (= 0xbe99999a)
 *   VEL_Z     = 0
 *   POS_X     = (u-0.5)*5 + player_pos[0]
 *   POS_Y     = player_pos[1] + 20
 *   POS_Z     = (u-0.5)*5 + player_pos[2]
 *   ROT_SCR   = π/2          (= 0x3fc90fdb)
 *   ROT_Z     = u * 2π
 *   slot.MATRIX0 = RotY(ROT_Z)   (via engine thunk_FUN_004a35d3)
 *   DRAG      = 0            (LAB_004462f0 → uVar2=0 → LAB_0044757e)
 *   cap = 1 (LAB_00447584). */
static int init_npc_3a(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)owner; (void)type; (void)flag; (void)part_idx;

    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, 0.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, -0.3f);      /* 0xbe99999a */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, 0.0f);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X,
               (rng_next_unit() - 0.5f) * 5.0f + g_scene1_player_pos[0]);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               g_scene1_player_pos[1] + 20.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z,
               (rng_next_unit() - 0.5f) * 5.0f + g_scene1_player_pos[2]);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_SCR, 1.5707964f); /* π/2 */
    float rot_z = rng_next_unit() * B_TWO_PI_F;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_Z, rot_z);

    /* slot.MATRIX0 = RotY(rot_z) — engine thunk_FUN_004a35d3 at L42751.
     * Same shape as type 0x56's mat compose. */
    float matrix_ry[16];
    mat4_rotation_y(matrix_ry, rot_z);
    int32_t *r = slot_base(i);
    memcpy(&r[SCENE1_RECORDS_B_OFF_MATRIX0], matrix_ry, sizeof matrix_ry);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_DRAG, 0.0f);
    return 1;
}

/* Type 0x34 — 8-particle ALT_POS targeting player with frame-indexed
 * azimuth table (engine L42858-42902).
 *
 * Per-particle (each of 8 iterations):
 *   idx = frame_table[(g_sim_frame_count + part_idx) % 8]
 *     where frame_table = { -3, -1, -4, 2, 1, 3, -2, 4 }
 *   radius = idx * 5 ± 3   (− 3 when result > 0; + 3 when result ≤ 0)
 *   negated_ang = -(owner+0x420)
 *   ALT_POS_X (DAT_06932530) = sin(negated_ang) * radius + player.x
 *   ALT_POS_Y (DAT_06932534) = player.y + 2
 *   ALT_POS_Z (DAT_06932538) = cos(negated_ang) * radius + player.z
 *     (argless cos at L42881 = same negated_ang per PHC #7)
 *
 *   POS       = owner+0x3f0/4/8 + +11y
 *   ang       = owner+0x420  (NOT negated)
 *   VEL_X     = sin(ang) * 2
 *   VEL_Y     = 0
 *   VEL_Z     = cos(ang) * 2
 *     (argless cos at L42893 = same ang per PHC #7)
 *   slot.TYPE = 0x34 (explicit re-assert)
 *   AGE       = part_idx * -4    (negative stagger)
 *   AUX_SENT1 = part_idx         (engine's saved local_8 = pre-increment)
 *
 *   cap = 8 (LAB_00447325).
 *
 * Engine writes ALT_POS_X/Y/Z to DAT_06932530/34/38 — those are 1 dw
 * BEFORE ALT_POS_X (which is DAT_06932524).  Looking at the layout, the
 * engine has the ALT_POS triplet for 0x34 at dw +0x80/+0x84/+0x88 (=
 * dw 32/33/34) instead of dw 29/30/31 — a SECOND alt-target field that
 * shares the rotation scratch area.  Use raw slot_base[] for these so
 * we match the engine offsets verbatim.  Specifically:
 *   DAT_06932530 = byte 0x80 = dw 32
 *   DAT_06932534 = byte 0x84 = dw 33
 *   DAT_06932538 = byte 0x88 = dw 34 (this overlaps ROT_SCR dw 35 by 1
 *     dw — verified: byte 0x88 vs ROT_SCR's 0x8c are 4 B apart). */
static int init_npc_34(int i, const void *owner, int type, int flag,
                       int part_idx)
{
    (void)type; (void)flag;

    /* Engine's 8-element azimuth table (local_4c[0..7] at L42860-42867). */
    static const int frame_table[8] = { -3, -1, -4, 2, 1, 3, -2, 4 };

    int   tbl_idx = (int)((g_sim_frame_count + (uint32_t)part_idx) % 8u);
    float radius  = (float)frame_table[tbl_idx] * 5.0f;
    if (radius <= 0.0f) radius += 3.0f;
    else                radius -= 3.0f;

    float ang     = owner_read_f(owner, 0x420);
    float neg_ang = -ang;
    float s_n     = sinf(neg_ang);
    /* ALT_POS at dw 32/33/34 — distinct from the standard ALT_POS at
     * dw 29/30/31.  Engine writes DAT_06932530/34/38. */
    int32_t *r = slot_base(i);
    float alt_x_v = s_n * radius + g_scene1_player_pos[0];
    float alt_y_v = g_scene1_player_pos[1] + 2.0f;
    /* Argless cos at L42881 = same neg_ang per PHC #7. */
    float c_n     = cosf(neg_ang);
    float alt_z_v = c_n * radius + g_scene1_player_pos[2];
    memcpy(&r[32], &alt_x_v, sizeof alt_x_v);
    memcpy(&r[33], &alt_y_v, sizeof alt_y_v);
    memcpy(&r[34], &alt_z_v, sizeof alt_z_v);

    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_X, owner_read_f(owner, 0x3f0));
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Y,
               owner_read_f(owner, 0x3f4) + 11.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_POS_Z, owner_read_f(owner, 0x3f8));

    float s_a = sinf(ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_X, s_a + s_a);  /* * 2 */
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Y, 0.0f);
    /* Argless cos at L42893 = same ang per PHC #7. */
    float c_a = cosf(ang);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_VEL_Z, c_a + c_a);

    /* TYPE explicit at L42898 (already set in preamble — matches). */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AGE, part_idx * -4);
    /* AUX_SENT1 = pre-increment local_8 = part_idx (engine's uVar5
     * snapshot at L42894). */
    slot_set_i(i, SCENE1_RECORDS_B_OFF_AUX_SENT1, part_idx);
    return 8;
}

/* Types 0x16 / 0x17 — 3-particle NPC-bend with PART_IDX = part_idx - 1
 * quirk (engine L42911-42918).
 *
 *   ROT_X    = (owner+0x18) * 2π / 8           (NPC bend)
 *   PART_IDX = part_idx - 1                     (engine's iVar4 = pre-inc
 *                                                local_8 - 1; PRE-decrement
 *                                                quirk preserved verbatim)
 *   cap = 3.
 *
 * Engine quirk #50: PART_IDX value for 0x16/0x17 is offset by -1 from
 * the natural per-particle index (so the 3 spawned slots get -1, 0, 1
 * not 0, 1, 2).  Faithful port — consumer tick must accept negative
 * PART_IDX values for these types. */
static int init_npc_16_17(int i, const void *owner, int type, int flag,
                          int part_idx)
{
    (void)type; (void)flag;
    float bend = (float)owner_read_i(owner, 0x18) * B_TWO_PI_F / 8.0f;
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X,    bend);
    slot_set_i(i, SCENE1_RECORDS_B_OFF_PART_IDX, part_idx - 1);
    return 3;
}

/* Dispatch helper for NPC allocator. */
static int run_npc_body(int slot, const void *owner, int type, int flag,
                        int part_idx)
{
    switch (type) {
    case 0xe: case 0x97: case 0x46:
    /* C8j.10b — group B of the LAB_00447584 trivial-tail bucket. */
    case 0x24: case 0xa: case 0xb: case 0x14: case 0x13: case 0x99:
        return init_npc_lab_00447584(slot, owner, type, flag, part_idx);

    case 0x4d: case 0x4e: case 0x4f: case 0x50:
    case 0xa5: case 0xa6:
        return init_npc_cluster_b(slot, owner, type, flag, part_idx);

    /* C8j.10 — single-spawn NPC types. */
    case 0x56: return init_npc_56(slot, owner, type, flag, part_idx);
    case 0x53: return init_npc_53(slot, owner, type, flag, part_idx);
    case 0x51: return init_npc_51(slot, owner, type, flag, part_idx);
    case 0x68: return init_npc_68(slot, owner, type, flag, part_idx);

    /* C8j.10b — owner-pos + explicit-return group. */
    case 0x1e: case 0x88: case 0x89: case 0x9a: case 0x9e:
        return init_npc_explicit_return(slot, owner, type, flag, part_idx);

    /* C8j.12 — owner+0x420 family of NPC single-spawn types. */
    case 0x33: return init_npc_33   (slot, owner, type, flag, part_idx);
    case 0x27: return init_npc_27   (slot, owner, type, flag, part_idx);
    case 0x2b: return init_npc_2b   (slot, owner, type, flag, part_idx);
    case 0x26: case 0x2a:
        return init_npc_26_2a       (slot, owner, type, flag, part_idx);
    case 0x31: case 0x32:
        return init_npc_31_32       (slot, owner, type, flag, part_idx);
    case 0x25: return init_npc_25   (slot, owner, type, flag, part_idx);
    case 0x3b: return init_npc_3b   (slot, owner, type, flag, part_idx);
    case 0x28: return init_npc_28   (slot, owner, type, flag, part_idx);
    case 0x38: return init_npc_38   (slot, owner, type, flag, part_idx);
    case 0x21: return init_npc_21   (slot, owner, type, flag, part_idx);
    case 0x6b: return init_npc_6b   (slot, owner, type, flag, part_idx);
    case 0x6c: return init_npc_6c   (slot, owner, type, flag, part_idx);
    case 0x1f: return init_npc_1f   (slot, owner, type, flag, part_idx);

    /* C8j.13 — NPC allocator remainder. */
    case 0x2f: return init_npc_2f   (slot, owner, type, flag, part_idx);
    case 0x2e: case 0x36:
        return init_npc_2e_36       (slot, owner, type, flag, part_idx);
    case 0x3c: return init_npc_3c   (slot, owner, type, flag, part_idx);
    case 0x98: return init_npc_98   (slot, owner, type, flag, part_idx);
    case 0x5a: return init_npc_5a   (slot, owner, type, flag, part_idx);
    case 0xf:  case 0x12:
        return init_npc_f_12        (slot, owner, type, flag, part_idx);
    case 0x9c: return init_npc_9c   (slot, owner, type, flag, part_idx);
    case 0x3a: return init_npc_3a   (slot, owner, type, flag, part_idx);
    case 0x34: return init_npc_34   (slot, owner, type, flag, part_idx);
    case 0x16: case 0x17:
        return init_npc_16_17       (slot, owner, type, flag, part_idx);

    default:
        /* Unreachable — outer dispatch gated by IMPLEMENTED. */
        return 1;
    }
}

/* ─── public API ─────────────────────────────────────────────────── */

void scene1_record_b_spawn_entity(const void *owner_a, int type, int flag)
{
    trace_record(SCENE1_RECORD_B_SPAWN_KIND_ENTITY, owner_a, type, flag);

    if (!SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(type)) {
        /* Unimplemented — divergence vs engine (see header).  Skip
         * the slot scan entirely so no preamble fires. */
        return;
    }

    /* Engine's outer loop (FUN_0044376a L40926-41887):
     *   local_8 = 0;            (part_idx — per-particle counter)
     *   local_1c = 0;           (slot scan index)
     *   for (;;) {
     *     if (slot[local_1c].type == 0) {
     *       preamble(...);
     *       per_type_body(...);    // may set iVar10 = cap
     *       LAB_004455ed:  local_8++;  bVar14 = (local_8 == iVar10);
     *       LAB_004457ee:  local_8 = saved + 1;
     *                      if (bVar14) return;
     *     }
     *     local_1c++; if (local_1c == 0x200) return;
     *   }
     *
     * The body returns its cap (iVar10).  We commit one slot per
     * particle and break once part_idx reaches the cap. */
    int part_idx = 0;
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (!slot_is_free(i)) continue;

        preamble_entity(i, owner_a, type, flag);
        int cap = run_entity_body(i, owner_a, type, flag, part_idx);

        part_idx++;
        if (part_idx >= cap) return;
    }
    /* Table full → silent return (engine falls through outer-loop
     * termination at local_1c == 0x200). */
}

void scene1_record_b_spawn_npc(const void *owner_b, int type, int flag)
{
    trace_record(SCENE1_RECORD_B_SPAWN_KIND_NPC, owner_b, type, flag);

    if (!SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(type)) {
        return;
    }

    /* NPC allocator outer loop — same shape as the entity allocator's
     * after C8j.6 refactor.  Engine LAB_00447cb8 / 00447cbe tail uses
     * uVar5 as the cap; body returns it. */
    int part_idx = 0;
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (!slot_is_free(i)) continue;

        preamble_npc(i, owner_b, type, flag);
        int cap = run_npc_body(i, owner_b, type, flag, part_idx);

        part_idx++;
        if (part_idx >= cap) return;
    }
}
