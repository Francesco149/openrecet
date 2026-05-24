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

    default:
        /* Unreachable — outer dispatch gated by IMPLEMENTED. */
        return 1;
    }
}

/* ─── per-type bodies — NPC allocator ─────────────────────────────── */

/* Types 0xe / 0x97 / 0x46 — pure preamble (engine L42823: direct
 * goto LAB_00447584 → tail-share `local_8 = local_8 + 1; bVar11 =
 * local_8 == 1; goto LAB_00447cbe;`). */
static void init_npc_e_97_46(int i, const void *owner)
{
    (void)i;
    (void)owner;
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

    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (!slot_is_free(i)) continue;

        preamble_npc(i, owner_b, type, flag);

        switch (type) {
        case 0xe:
        case 0x97:
        case 0x46:
            init_npc_e_97_46(i, owner_b);
            break;
        default: break;
        }
        return;
    }
}
