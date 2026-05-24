/*
 * scene1_records_b_spawn.h — table B slot allocators.
 *
 * Chip C8j.5 (2026-05-24) — skeleton + common preamble + 3 minimal
 * 1-particle types per allocator.
 *
 * Chip C8j.6 (2026-05-24) — sin/cos drift cluster (6 types) + cluster A
 * (10 types) + multi-particle outer-loop infrastructure for the entity
 * allocator.  17 new entity types total; NPC allocator unchanged.
 *
 * Chip C8j.7 (2026-05-24) — mega-cluster A in entity allocator (8 types)
 * + cluster B in NPC allocator (6 types).  Multi-particle outer-loop
 * refactor for NPC allocator (mirrors C8j.6's entity refactor).
 * 14 new types total.
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

/* Implemented as of C8j.6 (both allocators share the per-type
 * dispatcher pattern but route to allocator-specific switches).
 *
 * Entity allocator (FUN_0044376a):
 *   C8j.5 anchors (1-particle, no-trig):
 *   - 0x24 — pure preamble; LAB_004457e7 direct.
 *   - 0x60 — preamble + slot.ROT_X = *(float*)(owner + 0xea4).
 *   - 0x82 — preamble + slot.SCALE_X = 2.0f + slot.ROT_X = *(float*)
 *            (owner + 0xea4).
 *   C8j.6 sin/cos drift cluster (engine L41594-41621, 1-particle each):
 *   - 2, 3, 4, 0x22, 0x54, 0x67 — NPC-bend ROT_X, sin/cos(owner+0xea4)
 *     vel*3 + pos shift, per-type SCALE_X (0x22→2.0, 0x67→1.2, 3 if
 *     flag!=-1 → 0.5), DRAG=20.0, ROT_Z=rng*2π, AUX_C8=1.
 *   C8j.6 cluster A (engine L41265-41397, MULTI-particle):
 *   - 0x4d, 0x4e, 99, 0x51, 0x52, 0x53 — 1 particle each.
 *   - 0x4f — 3 particles.
 *   - 0x50 — 5 particles.
 *   - 0xa5 — 6 particles.
 *   - 0xa6 — 8 particles.
 *     Per-particle angle = bend + per-particle shift (0/±0.18/±0.36/π/
 *     π+0.18/π-0.18); pos = sin/cos(angle)*{0.3 default | 0.8 main6} +
 *     {+0.7y default | +1.4y main6 | owner.y for 0x53}.  Per-type
 *     SCALE_X + LIFE_MULT + local_10 vel-mag + VEL_Y (0x4d=0.07).
 *     0x53 also writes byte 0xc0 = 3.
 *   C8j.7 mega-cluster A (engine L41650-41813):
 *   - 0x77, 0x7b, 0x7e — 1 particle each.
 *   - 0x73 — 4 particles.
 *   - 0x7c — 5 particles.
 *   - 0x76, 0x78, 0x7a — 8 particles each.
 *     Pos from sin/cos(bend)*1.2 + +1.3y; ALT_POS from sin/cos(bend)
 *     *0.8 + +1.3y.  0x7a overrides local_c with owner+0xea4 AFTER
 *     pos writes.  Three-way (owner+0x948) dispatch: 0 → POS_X -= 0.41,
 *     4 → POS_X += 0.41, else → POS_Z -= 0.1 (ALT_POS gets same shift).
 *     Angle table mod 32 = (owner+0xe3c + part_idx) % 32 → ROT_X wobble.
 *     Per-type ROT_X/VEL_Y/SCALE_X/LIFE_MULT/local_10 overrides.
 *     0x7c: POS_X/Z -= 2*VEL (rebound), per-particle alternating
 *     bidirectional ROT_X fan.  0x76 with part>0: PART_IDX = 1.
 *
 * NPC allocator (FUN_00445a8c):
 *   C8j.5 anchors (1-particle, preamble-only):
 *   - 0xe, 0x97, 0x46 — all preamble-only via LAB_00447584 tail-share.
 *   C8j.7 cluster B (engine L42112-42161, MULTI-particle):
 *   - 0x4d, 0x4e — 1 particle each.
 *   - 0x4f — 3 particles.
 *   - 0x50 — 5 particles.
 *   - 0xa5 — 6 particles.
 *   - 0xa6 — 8 particles.
 *     Per-particle angle = bend (owner+0x18 * 2π/8) + 5-shift table
 *     {0/±0.18/±0.36}; pos = sin/cos(angle)*0.8 + +1.4y; LIFE_MULT
 *     hardcoded 0.4; VEL_X/Z = sin/cos(angle)*0.5; ROT_X = angle;
 *     DRAG = 0.5; AUX_C8 = 1.  No per-type SCALE_X / LIFE_MULT.
 *     (Simpler than cluster A — owner shape B has fewer fields.)
 */
#define SCENE1_RECORD_B_SPAWN_ENTITY_TYPE_IMPLEMENTED(t)                 \
    ((t) == 0x24 || (t) == 0x60 || (t) == 0x82 ||                        \
     (t) == 2    || (t) == 3    || (t) == 4    ||                        \
     (t) == 0x22 || (t) == 0x54 || (t) == 0x67 ||                        \
     (t) == 0x4d || (t) == 0x4e || (t) == 0x4f || (t) == 0x50 ||         \
     (t) == 0xa5 || (t) == 0xa6 || (t) == 99   ||                        \
     (t) == 0x51 || (t) == 0x52 || (t) == 0x53 ||                        \
     (t) == 0x73 || (t) == 0x76 || (t) == 0x77 || (t) == 0x78 ||         \
     (t) == 0x7a || (t) == 0x7b || (t) == 0x7c || (t) == 0x7e)
#define SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(t)                    \
    ((t) == 0xe  || (t) == 0x97 || (t) == 0x46 ||                        \
     (t) == 0x4d || (t) == 0x4e || (t) == 0x4f || (t) == 0x50 ||         \
     (t) == 0xa5 || (t) == 0xa6)

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
