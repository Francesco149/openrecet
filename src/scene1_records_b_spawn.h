/*
 * scene1_records_b_spawn.h — table B slot allocators.
 *
 * Chip C8j.5 (2026-05-24).  Skeleton + common preamble + 3 minimal
 * 1-particle types per allocator.  Ports start on the 512-slot
 * DAT_069324b0 ("entity / NPC effect") table consumed by Pass C of
 * the shop walker + Pass A/B/C/D/E of the wide followup.
 *
 *   FUN_0044376a — "entity allocator", owner shape A: pos at owner+0x20,
 *                  matrix at owner+0xde8, NPC-bend at owner+0x948,
 *                  alt people-table at owner+0x9e0 (stride 0x44),
 *                  owner-flag inherit at owner+0xeac.  Public API:
 *                  scene1_record_b_spawn_entity().
 *   FUN_00445a8c — "NPC allocator",   owner shape B: pos at owner+0x3f0,
 *                  matrix at owner+0x39c.  Public API:
 *                  scene1_record_b_spawn_npc().
 *
 * Both allocators scan slots 0..511 for the first slot with
 * SCENE1_RECORDS_B_OFF_TYPE == 0 (engine: `*piVar == 0`).  The two
 * preambles share most field writes but differ in 8 ways
 * (per `docs/findings/scene1-table-b-allocators.md` §"Common preamble —
 * writer-view column map" / "Diff #1..#8" + the survey's bottom diff
 * table) — see preamble_entity / preamble_npc in the .c for the
 * verbatim port.
 *
 * Per-call effect (mirrors C8i.1 scene1_spawn):
 *   1. Record the call into the trace ring (for tests / instrumentation).
 *   2. If the type's body isn't implemented, return without committing
 *      a slot.  Engine actually commits + leaks an uninitialized slot
 *      in that case — we diverge for safety until the C8j.6+ ladder
 *      shrinks the unimplemented set, then will match the engine.
 *   3. Otherwise, scan for the first free slot, write the preamble,
 *      run the per-type body, and return.  Both allocators's C8j.5
 *      bodies are 1-particle (LAB_004457e7 / LAB_00447584 tails) — no
 *      multi-spawn outer loop yet.
 *
 * SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED / _NPC_TYPE_IMPLEMENTED
 * are the canonical whitelists; same idea as SCENE1_SPAWN_TYPE_IMPLEMENTED
 * in scene1_spawn.h.
 *
 * Owner pointers: passed as `const void *` and read via raw byte offsets
 * (`*(const float*)((const char*)owner + N)`).  The engine treats them
 * as raw int pointers — we don't have the engine's entity / NPC structs
 * mapped to a C type yet (owner shape A is ≥ 0xeb0 bytes, shape B is ≥
 * 0x3fc bytes), so the byte-offset form is the faithful match.
 *
 * The entity allocator's preamble dereferences:
 *   owner+0x20/0x24/0x28      pos.x/y/z (param_3 == -1 path)
 *   owner+0x9e0+param_3*0x44  alt people-table pos.x (param_3 != -1)
 *   owner+0x9e4, +0x9e8       alt pos.y / pos.z
 *   owner+0xde8..0xe27        16-dw matrix (64 B) copied to slot+0xc8
 *   owner+0xeac               owner flag (for slot OWNER_FLAG)
 *
 * The entity allocator's per-type 0x60 / 0x82 bodies additionally read:
 *   owner+0xea4               rot.x source
 *
 * The NPC allocator's preamble dereferences:
 *   owner+0x3f0/0x3f4/0x3f8   pos.x/y/z (no fallback path)
 *   owner+0x39c..0x3db        16-dw matrix (64 B)
 *
 * The C8j.5 NPC anchor types (0xe, 0x97, 0x46) do NOT deref owner.
 *
 * Callers in the engine pass `param_3 = 0xffffffff` for the entity
 * allocator at every observed site — the alt people-table fallback is
 * dead in shipping config but is ported for completeness.
 *
 * Sequence counter: g_scene1_record_b_seq_counter mirrors engine
 * DAT_06a46fb8.  Both preambles snapshot then post-increment.
 */
#ifndef SCENE1_RECORDS_B_SPAWN_H
#define SCENE1_RECORDS_B_SPAWN_H

#include <stdint.h>

#include "scene1_records.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Implemented as of C8j.5 (both allocators share the per-type
 * dispatcher pattern but route to allocator-specific switches).
 *
 * Entity allocator (FUN_0044376a):
 *   - 0x24 — pure preamble; LAB_004457e7 direct.
 *   - 0x60 — preamble + slot.ROT_X = *(float*)(owner + 0xea4).
 *   - 0x82 — preamble + slot.SCALE_X = 2.0f + slot.ROT_X = *(float*)
 *            (owner + 0xea4).
 *
 *   (Survey's plan named 0x66 as an anchor; body inspection of engine
 *    L41843-41866 shows 0x66 actually executes the sin/cos drift
 *    cluster default tail — sin/cos vel + pos shifts + life write.
 *    Substituted 0x82 instead; 0x66 belongs in C8j.6 with the drift
 *    cluster bodies.)
 *
 * NPC allocator (FUN_00445a8c):
 *   - 0xe, 0x97, 0x46 — all preamble-only via LAB_00447584 tail-share.
 */
#define SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(t) \
    ((t) == 0x24 || (t) == 0x60 || (t) == 0x82)
#define SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(t) \
    ((t) == 0xe || (t) == 0x97 || (t) == 0x46)

/* Engine DAT_06a46fb8 — monotonically incremented per slot claim by
 * either allocator.  Snapshot + post-increment pattern means the first
 * claim observes 0 and writes 0 to slot.SEQ_ID; the second observes 1;
 * etc.  Reset to 0 by scene1_records_reset() to keep tests deterministic. */
extern int g_scene1_record_b_seq_counter;

/* Trace ring — same shape + capacity as scene1_spawn's. */
#define SCENE1_RECORD_B_SPAWN_TRACE_CAPACITY 32

typedef enum {
    SCENE1_RECORD_B_SPAWN_KIND_ENTITY = 0,
    SCENE1_RECORD_B_SPAWN_KIND_NPC    = 1,
} scene1_record_b_spawn_kind_t;

typedef struct {
    scene1_record_b_spawn_kind_t kind;
    const void *owner;
    int         type;
    int         flag;
} scene1_record_b_spawn_call_t;

extern int                          g_scene1_record_b_spawn_trace_count;
extern scene1_record_b_spawn_call_t g_scene1_record_b_spawn_trace[
    SCENE1_RECORD_B_SPAWN_TRACE_CAPACITY];

/* Entity allocator (FUN_0044376a).  param_3 = -1 (engine convention
 * for "use owner+0x20 for pos"); pass any other index to pull pos
 * from owner+0x9e0+flag*0x44 instead. */
void scene1_record_b_spawn_entity(const void *owner_a, int type, int flag);

/* NPC allocator (FUN_00445a8c).  flag is stored verbatim at slot
 * FLAG_A (dw 1).  No pos-source fallback path. */
void scene1_record_b_spawn_npc(const void *owner_b, int type, int flag);

void scene1_record_b_spawn_trace_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_B_SPAWN_H */
