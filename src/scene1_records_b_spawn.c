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

#include <string.h>

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

/* Type 0x24 — pure preamble (engine L41052: direct goto LAB_004457e7). */
static void init_entity_24(int i, const void *owner)
{
    (void)i;
    (void)owner;
}

/* Type 0x60 — preamble + ROT_X ← owner+0xea4 (engine L41066 →
 * LAB_004457db: `fVar2 = *(float *)(param_1 + 0xea4);` then
 * LAB_004457e1 writes ROT_X). */
static void init_entity_60(int i, const void *owner)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, owner_read_f(owner, 0xea4));
}

/* Type 0x82 — preamble + SCALE_X = 2.0f + ROT_X ← owner+0xea4 (engine
 * L41061-41064: sets SCALE_X then falls through to 0x60's body via
 * LAB_004457cf → LAB_004457db → LAB_004457e1). */
static void init_entity_82(int i, const void *owner)
{
    slot_set_f(i, SCENE1_RECORDS_B_OFF_SCALE_X, 2.0f);
    slot_set_f(i, SCENE1_RECORDS_B_OFF_ROT_X, owner_read_f(owner, 0xea4));
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

    /* Engine's outer loop scans slots 0..511 looking for slot[TYPE]==0.
     * All C8j.5 types are 1-particle; commit one slot then return. */
    for (int i = 0; i < SCENE1_RECORDS_B_COUNT; i++) {
        if (!slot_is_free(i)) continue;

        preamble_entity(i, owner_a, type, flag);

        switch (type) {
        case 0x24: init_entity_24(i, owner_a); break;
        case 0x60: init_entity_60(i, owner_a); break;
        case 0x82: init_entity_82(i, owner_a); break;
        default: /* unreachable — gated by IMPLEMENTED above */ break;
        }
        return;
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
