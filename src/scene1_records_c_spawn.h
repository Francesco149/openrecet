/*
 * scene1_records_c_spawn.h — table C slot allocators.
 *
 * Chip C8j.2 (2026-05-24).  Ports the four engine spawn entry points
 * for the 200-slot DAT_06956cd8 ("world drop / pickup") table:
 *
 *   FUN_0044aef0 (96  B)  — 5-arg lightweight pickup spawn (1 slot,
 *                           STATE=2, used by FUN_00484e97-area pickup
 *                           events at decomp L34981/35013).
 *   FUN_0044af50 (419 B)  — 11-arg multi-slot world-drop spawn (up
 *                           to param_6 slots, STATE=0, with a 4-color
 *                           RNG ramp for type ∈ [0xc87..0xcea] window).
 *   FUN_0044b0f3 (60  B)  — 9-arg wrapper → FUN_0044af50 with
 *                           param_10=0, param_11=-1 (auto 4-color ramp).
 *                           Called from C8h.4b's scene1_mesh_emit stub
 *                           for type 0x6e particle chains.
 *   FUN_0044b12f (61  B)  — 10-arg wrapper → FUN_0044af50 with
 *                           param_10=0, param_11=caller_param_10
 *                           (explicit type override).
 *
 * All four scan from slot 0 for the first slot with TYPE == -1
 * (offset 10 dw, SCENE1_RECORDS_C_OFF_TYPE).  FUN_0044aef0 scans the
 * full 200; FUN_0044af50 scans only the first 136 (= 0x88) when its
 * `type` arg is ≤ 6, otherwise 200.  Table-full → silent noop.
 *
 * The engine functions take ints for the position args because the
 * compiler punned the calls through `int` params even though the
 * fields are float bits; this header takes `float` for clarity but
 * the underlying writes are bit-identical.
 */
#ifndef SCENE1_RECORDS_C_SPAWN_H
#define SCENE1_RECORDS_C_SPAWN_H

#include <stdint.h>

#include "scene1_records.h"
#include "scene1_records_c_tick.h"   /* slot offsets */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single-slot pickup spawn (FUN_0044aef0).  Stages a STATE=2 record
 * at (px, py, pz) with the given type and zero velocity.  Allocator-
 * level fields:
 *
 *   pos.{x,y,z} = (px, py, pz)         slot[0..2]
 *   vel         = (0, 0, 0)            slot[3..5]
 *   age         = 0                    slot[11]
 *   type        = type                 slot[10]
 *   scale       = 1.0f                 slot[12]
 *   pickup_e2   = 0                    slot[15]
 *   state       = 2 (pickup-bob)       slot[16]
 *   slot[17]    = 0
 *   aux         = 0                    slot[36]
 *
 * Engine quirk: slot[14] (PICKUP_E1) is NOT initialized — it retains
 * whatever was last written before the slot went sentinel.  For a
 * freshly-reset table the post-reset slot is fully zeroed so this is
 * benign; if the slot was previously alive, prior pickup_e1 leaks
 * across the reuse.  Documented; not "fixed" in the port to preserve
 * engine semantics.
 *
 * `owner` is the engine's param_1 — entirely unused in the allocator;
 * accepted here to keep the call-site shape engine-faithful.
 */
void scene1_records_c_spawn_pickup(int owner, float px, float py, float pz,
                                   int type);

/*
 * Multi-slot world-drop spawn (FUN_0044af50).  Stages up to `count`
 * records in STATE=0 around (px, py, pz) with random radial velocities
 * scaled by `mag`.  Slot fields (per claimed slot):
 *
 *   pos.{x,y,z}    = (px, py, pz)                        slot[0..2]
 *   vel.x          = sin(angle) * ((u+0.2)*0.5) * mag    slot[3]
 *   vel.y          = (u+0.2) * mag * 0.5                 slot[4]
 *   vel.z          = cos(angle) * ((u+0.2)*0.5) * mag    slot[5]
 *   type           = type (or pickup_e2 ramp — see below) slot[10]
 *   age            = rng_next15() & 7                    slot[11]
 *   scale          = 1.0f                                slot[12]
 *   pickup_e1      = 0 or `e1` (depending on type > 6)   slot[14]
 *   pickup_e2      = `type_override` or RNG ramp result   slot[15]
 *   state          = 0 (world-drop physics)              slot[16]
 *   slot[17]       = `extra_aux`                          slot[17]
 *   owner_ref      = `owner`                              slot[18]
 *   aux            = `aux10`                              slot[36]
 *
 * The angle is `rng_next_unit() * 2π` per slot; vel.x and vel.z share
 * the SAME angle (paired sin/cos), so each spawned slot has its vel
 * pointing in a random horizontal direction with magnitude scaled by
 * mag and a small upward bias on vel.y.
 *
 * When `type > 6`:
 *   - pickup_e1 := `e1` (engine's param_8).
 *   - If `type_override` >= 0: pickup_e2 := `type_override`.
 *   - Else (`type_override` < 0): 4-color RNG ramp ONLY when
 *     (type - 7) ∉ [0xc80, 0xce3].  Picks pickup_e2 ∈ {0,1,2,3,4}
 *     via `rng_next15() % 100` thresholds:
 *       <50 → 0,  <70 → 1,  <85 → 2,  <95 → 3,  ≥95 → 4.
 *     When (type - 7) IS inside [0xc80, 0xce3], pickup_e2 stays 0
 *     (the prior write at the top of the slot-init block).
 *
 * When `type <= 6`: scan is limited to first 136 slots (engine quirk),
 * and pickup_e1 is left at the initial 0 — `e1` is ignored.
 *
 * Stops when `count` slots have been claimed OR the scan exhausts
 * the table (136 / 200 cap).  Returns silently in either case.
 */
void scene1_records_c_spawn_world_drop(int owner, float px, float py,
                                       float pz, int type, int count,
                                       float mag, int e1, int extra_aux,
                                       int aux10, int type_override);

/*
 * 9-arg wrapper — calls scene1_records_c_spawn_world_drop with
 * aux10=0 and type_override=-1 (engine FUN_0044b0f3).
 */
void scene1_records_c_spawn_world_drop_default(int owner, float px, float py,
                                               float pz, int type, int count,
                                               float mag, int e1, int extra_aux);

/*
 * 10-arg wrapper — calls scene1_records_c_spawn_world_drop with
 * aux10=0 and type_override=caller's `type_override` (engine
 * FUN_0044b12f).
 */
void scene1_records_c_spawn_world_drop_typed(int owner, float px, float py,
                                             float pz, int type, int count,
                                             float mag, int e1, int extra_aux,
                                             int type_override);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_C_SPAWN_H */
