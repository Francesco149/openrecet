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

#include "rng.h"

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

    case 2: case 3: case 4: case 0x22: case 0x54: case 0x67:
        return init_entity_drift_cluster(slot, owner, type, flag, part_idx);

    case 0x4d: case 0x4e: case 0x4f: case 0x50:
    case 0xa5: case 0xa6: case 99:
    case 0x51: case 0x52: case 0x53:
        return init_entity_cluster_a(slot, owner, type, flag, part_idx);

    case 0x73: case 0x76: case 0x77: case 0x78:
    case 0x7a: case 0x7b: case 0x7c: case 0x7e:
        return init_entity_mega_cluster_a(slot, owner, type, flag, part_idx);

    default:
        /* Unreachable — outer dispatch gated by IMPLEMENTED. */
        return 1;
    }
}

/* ─── per-type bodies — NPC allocator ─────────────────────────────── */

/* Same per-particle body signature as the entity allocator (see
 * comment above init_entity_24).  Body returns the cap; outer loop
 * commits up to `cap` particles. */

/* Types 0xe / 0x97 / 0x46 — pure preamble (engine L42823: direct
 * goto LAB_00447584 → tail-share `local_8 = local_8 + 1; bVar11 =
 * local_8 == 1; goto LAB_00447cbe;`). */
static int init_npc_e_97_46(int i, const void *owner, int type, int flag,
                            int part_idx)
{
    (void)i; (void)owner; (void)type; (void)flag; (void)part_idx;
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

/* Dispatch helper for NPC allocator. */
static int run_npc_body(int slot, const void *owner, int type, int flag,
                        int part_idx)
{
    switch (type) {
    case 0xe: case 0x97: case 0x46:
        return init_npc_e_97_46(slot, owner, type, flag, part_idx);

    case 0x4d: case 0x4e: case 0x4f: case 0x50:
    case 0xa5: case 0xa6:
        return init_npc_cluster_b(slot, owner, type, flag, part_idx);

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
