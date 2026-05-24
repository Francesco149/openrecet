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
 * Chip C8j.8 (2026-05-24) — entity allocator NPC-table + camera-yaw +
 * matrix-init types (0x23, 0x29, 0x30, 0x9b, 0x9d) + tail-share group
 * (0x3e, 0x5f).  Reads `g_scene1_camera_yaw` (engine _DAT_073de39c)
 * for the reverse-yaw poses, `g_scene1_people` (engine DAT_0076bd54
 * stride 0x2e9) for people-table fallbacks, and uses `mat4_rotation_x`
 * (engine `thunk_FUN_004a35d3`) for 0x23's matrix init.  Adds the
 * ground-query hook used by 0x29's people-table branch.  7 new types
 * total; all 1-particle (engine's per-particle dispatch in 0x23/0x29 is
 * preserved verbatim but unreachable since cap=1 in the loop).
 *
 * Chip C8j.9 (2026-05-24) — remaining single-spawn + small multi-spawn
 * FUN_0044376a types.  26 new types covered across 13 bodies:
 *
 *   1-particle:
 *     - 0x58 — drift-like body + DRAG=20.0 + AUX_C8=1 (no random ROT_Z).
 *     - 100 (0x64) — owner-anchored cone w/ dead per-particle dispatch
 *       (engine's part_idx 1..4 shifts unreachable in normal cap=1 flow);
 *       writes byte 0xc2 = 2, LIFE_MULT=0.5, AUX_C8=1.
 *     - 0x74, 0x79 — NPC-bend pos w/ 3-way owner+0x948 dispatch (same as
 *       mega-cluster A's preamble shape but without alt-pos / SCALE_X /
 *       LIFE_MULT overrides); VEL = 2*sin/cos(bend).  No DRAG / AUX_C8.
 *     - 0x61 — like 0x6a's body w/ SCALE_X=0.5, LIFE_MULT=0.3, DRAG=0.5,
 *       AUX_C8=0 (!).
 *     - 0x62 — RNG-shifted angle pos*1.3 + VEL.y=u*0.1 + SCALE_X=0.45 +
 *       AUX_C8=1.
 *     - 0x8a, 0x8b — LAB_004451f0 body: VEL=sin/cos(ang)*1.0 (not 3.0),
 *       SCALE_X per-type (0x8a→0.2, 0x8b→0.1).  No DRAG / AUX_C8.
 *     - 0x5b, 0x5c, 0x5e, 0x85, 0x86, 0x87 — LAB_00444be6 shared body:
 *       drift-like vel*3 + pos jitter + per-type SCALE_X
 *       (0x5b→0.7, 0x5c/0x5e/0x87→1.0, 0x85→0.0, 0x86→0.4) + AUX_C8=1.
 *     - 0x71, 0x72, 0x75, 0x7d — VEL=sin/cos(owner+0xea4)*1.0 + π/2-shifted
 *       ROT_Z; per-type SCALE_X (0x72→0.3, 0x7d→1.5, 0x71/0x75→1.0);
 *       0x75 skips VEL writes (preamble 0 sticks); AUX_C8=1.
 *     - 8 — VEL=0 + pos = owner.pos + +2y (preserves engine's *10 quirk);
 *       DRAG=20, AUX_C8=1, ROT_Z=(part_idx+1)*2π.
 *
 *   Multi-particle (cap as marked):
 *     - 0x65, 0x69 (cap=8) — NPC-bend+rng pos jitter + VEL.y=0.7 fixed +
 *       LIFE_MULT=0.3 + DRAG=0.5 + AUX_C8=1.
 *     - 0x6a (cap=8) — same pos shape as 0x61 + random ROT_SCR/ROT_Z +
 *       ROT_X=part_idx*2π/10 + SCALE_X=0.5 + DRAG=0.5 + AUX_C8=1.
 *     - 0x6d, 0x6e, 0x6f, 0x70 (cap=3) — drift-cluster-like body +
 *       DRAG=20 + AUX_C8=1 + PART_IDX=part_idx-1 (signed!).
 *
 * Chip C8j.9a (2026-05-24) — landed previously-deferred 0x68 single-
 * spawn with people-table sister-gate iteration.  Extends
 * scene1_people_entry_t with sister_720 / sister_724 fields (engine
 * DAT_0076c478 base, byte offsets +0x720 / +0x724) and resolves the
 * Ghidra-dropped FUN_005031e4 distance arg via raw-asm read of
 * 0x444070..0x44409c: arg = horizontal squared distance from owner.pos
 * to people[i].target.  Argless cos at L298/L328 follows PHC #7 (raw
 * asm 0x444131 reloads `[ebp-0x2c]`, the angle stash from the paired
 * sin).  Vestigial `local_10 != -NAN` sentinel in decomp (asm
 * `cmp eax, 0xffffffff; je fallback`) is dead — never reachable since
 * local_10 inits to 0 and only counts up; ported faithfully but logic
 * is equivalently a no-op.
 *
 * Chip C8j.10 (2026-05-24) — first batch of FUN_00445a8c NPC-allocator
 * single-spawn types: 0x56 (NPC-bend pos + matrix-init RotY×RotX +
 * +1.8y lift), 0x53 (NPC-bend low-lift drift, +0.08y), 0x51 (NPC-bend
 * +0.7y lift, cluster-B-shaped shift table ported faithfully despite
 * dead at cap=1), 0x68 (player-aim variant — RNG ring around
 * `g_scene1_player_pos` as alt-target; no people-table scan, unlike
 * entity-allocator 0x68).  0x56's `thunk_FUN_004a2a03` (D3DXMatrix-
 * Multiply) call appears argless in Ghidra but raw asm at 0x445c86
 * shows the 3 pushes (out=MATRIX0, a=scratch, b=MATRIX0); we use
 * math3d's `mat4_mul` which handles aliased out/b safely.
 *
 * Chip C8j.10b (2026-05-24) — small follow-up to C8j.10:
 *
 *   Trivial-tail group B (engine L42828 — 5-way "or" → goto
 *   LAB_00447584; preamble-only with no per-type body):
 *     0x24, 0xa, 0xb, 0x14, 0x13, 0x99.
 *
 *   Explicit-return group (engine L854-875 — write pos/alt/vel from
 *   `owner+0x420`, then explicit `return` skipping the loop tail):
 *     0x1e, 0x88, 0x89, 0x9a, 0x9e (0x9e additionally writes
 *     LIFE_MULT=1.8 + SCALE_X=10.0).
 *
 *   owner+0x420 is the NPC's "current orientation" angle — a NEW
 *   owner field for this allocator.  Used by ~10 more NPC types
 *   (0x1f / 0x33 / 0x6b / 0x27 / 0x28 / etc.) — those land in C8j.11+.
 *
 * Deferred from C8j.10 (target C8j.11+): mega-cluster B (0xa0-0xa4 +
 * 0x73/0x7a/0x7c/0x7e + 0x96 + 0xd/0x11/0x15/0xc/0x10/0x16/0x17/0x9c),
 * player-aim 0x84/0x96 (needs atan2 = FUN_00503dd0 wrapper), and the
 * owner+0x6fc alt-field-source family (0x33/0x36/0x38).  These have
 * the larger shared-body shape that's worth its own chip.
 *
 * Argless cos sites verified via raw-asm spot-check in the
 * 0x444500-0x444800 range — all reload either `[ebp-0x2c]` (the dVar4
 * angle stash, paired with prior sin call) or `[esi+0xea4]` (the owner
 * angle field).  Same PHC #7 pattern as C8j.6/7/8; no Frida needed.
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
 *   C8j.8 tail-share + NPC-table + matrix-init (engine L40979-41106):
 *   - 0x3e, 0x5f — group with 0x60: ROT_X = owner+0xea4.
 *   - 0x23 (1 particle) — POS triplet from sin/cos(-yaw)*15 + owner+0x38
 *     OR people[owner+0xea0].target +20y; VEL_Y = -0.3; LIFE_MULT = 1.2;
 *     ROT_Z = rng_unit()*2π (reused as ROT_X matrix angle); slot.matrix
 *     = RotationX(same angle).  DRAG = 0.
 *   - 0x29 (1 particle) — POS triplet from sin/cos(-yaw)*15 + owner+0x20
 *     OR people[owner+0xea0].pos -5y with optional ground-clamp via
 *     scene1_record_b_spawn_set_ground_query hook (default no-op).  No
 *     VEL/LIFE writes; DRAG = 0.
 *   - 0x30 (1 particle) — POS via sin/cos(0.31415927-yaw)*1.5 + owner+0x38;
 *     VEL via sin/cos(owner+0xea4)*0.7 OR (people[owner+0xea0].pos - POS)
 *     normalized × 0.7.  ROT_Z = rng_unit()*2π; DRAG = 20; AUX_C8 = 1.
 *     Engine's dropped-return atan2 call (FUN_00503dd0) is skipped (no
 *     observable side-effect).
 *   - 0x9b (1 particle) — ROT_X = bend(owner+0x948); LIFE_MULT = 1.3.
 *   - 0x9d (1 particle) — bend ROT_X + POS = owner.pos + +1.0y (override
 *     of preamble's -0.5y!) + ALT_POS = owner.pos + +0.9y + radial VEL
 *     = sin/cos(ROT_X) * 2 + SCALE_X = 10.  Engine returns explicitly;
 *     our cap=1 outer loop produces the same effect.
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
     (t) == 0x7a || (t) == 0x7b || (t) == 0x7c || (t) == 0x7e ||         \
     (t) == 0x3e || (t) == 0x5f ||                                       \
     (t) == 0x23 || (t) == 0x29 || (t) == 0x30 ||                        \
     (t) == 0x9b || (t) == 0x9d ||                                       \
     /* C8j.9 — single-spawn family + small multi-spawn (8/3/8/8 caps) */\
     (t) == 0x58 || (t) == 100  ||                                       \
     (t) == 0x74 || (t) == 0x79 ||                                       \
     (t) == 0x65 || (t) == 0x69 ||                                       \
     (t) == 0x6a || (t) == 0x61 ||                                       \
     (t) == 0x62 ||                                                      \
     (t) == 0x8a || (t) == 0x8b ||                                       \
     (t) == 0x5b || (t) == 0x5c || (t) == 0x5e ||                        \
     (t) == 0x85 || (t) == 0x86 || (t) == 0x87 ||                        \
     (t) == 0x6d || (t) == 0x6e || (t) == 0x6f || (t) == 0x70 ||         \
     (t) == 0x71 || (t) == 0x72 || (t) == 0x75 || (t) == 0x7d ||         \
     (t) == 8    ||                                                      \
     /* C8j.9a — single-spawn w/ people-table sister-gate iteration. */  \
     (t) == 0x68)
#define SCENE1_RECORD_B_SPAWN_NPC_TYPE_IMPLEMENTED(t)                    \
    ((t) == 0xe  || (t) == 0x97 || (t) == 0x46 ||                        \
     (t) == 0x4d || (t) == 0x4e || (t) == 0x4f || (t) == 0x50 ||         \
     (t) == 0xa5 || (t) == 0xa6 ||                                       \
     /* C8j.10 — NPC single-spawn (matrix-init / drift / player-aim) */  \
     (t) == 0x56 || (t) == 0x53 || (t) == 0x51 || (t) == 0x68 ||         \
     /* C8j.10b — LAB_00447584 trivial-tail group B + explicit-return */ \
     (t) == 0x24 || (t) == 0xa  || (t) == 0xb  ||                        \
     (t) == 0x14 || (t) == 0x13 || (t) == 0x99 ||                        \
     (t) == 0x1e || (t) == 0x88 || (t) == 0x89 ||                        \
     (t) == 0x9a || (t) == 0x9e)

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

/* Ground-query hook used by type 0x29's people-table branch (engine
 * FUN_00432e50).  Engine calls with (POS_X, POS_Y) — Ghidra dropped the
 * trailing engine args (real signature is `(float,float,float,float*)`),
 * so this 3-arg surface mirrors the Ghidra-visible interface verbatim.
 * The hook should write the ground height for the (x, y) input to
 * *out_y and return nonzero on hit, zero on no-hit.  Default stub
 * returns 0 → 0x29's people-table branch leaves POS_Y at
 * `people[idx].pos.y - 5` (a benign offset matching the engine when
 * no ground intersects).  See pending-human-check #15. */
typedef int (*scene1_b_ground_query_fn)(float x, float y, float *out_y);
scene1_b_ground_query_fn scene1_record_b_spawn_set_ground_query(
    scene1_b_ground_query_fn fn);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_B_SPAWN_H */
