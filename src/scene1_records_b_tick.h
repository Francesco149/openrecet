/*
 * scene1_records_b_tick.h — per-tick integrator for table B records.
 *
 * Chip C8j-tick.1 (2026-05-25) landed the SKELETON of engine FUN_0043ae20
 * @ 0x43ae20 (25750 B Mt. Everest); sub-chips C8j-tick.2..C8j-tick.13
 * fill the per-type bodies.  See docs/findings/scene1-records-b-tick.md
 * for the chip ladder.
 *
 * Outer-loop semantics (C8j-tick.1):
 *
 *   - Walk 512 slots (engine `local_2c == 0x200`).
 *   - Skip-dead-slot gate (`slot[TYPE] == 0` → LAB_0043fbbc).
 *   - Per-tick flag clear (engine DAT_06a46f98 = 0; modeled as
 *     `g_scene1_records_b_tick_flag = 0`, PHC #21).
 *   - Preamble: pos.{x,y,z} += vel.{x,y,z}; age++ — per engine L36456-62.
 *   - Per-type dispatch via `dispatch_default()` (sub-chip-extended
 *     switch table inside scene1_records_b_tick.c).
 *   - Kill helper `scene1_records_b_tick_kill_slot()` mirrors engine's
 *     LAB_004411e3 (`*piVar14 = 0`) — sets TYPE field to 0 to mark dead.
 *
 * C8j-tick.2 (2026-05-25) adds bodies for the anchor cascade:
 *
 *   - 0x1e / 0x2f / 0x88 / 0x9a — joint-table-anchored NPC effect, with
 *     a sub-dispatch (only for 0x1e) on owner+0x424 NPC motion-style ID:
 *     {0x48, 0x4b} → angle+0.3; 0x4c → angle-0.3; else → ALT_POS+1.5
 *     billboard.  Shared LAB_0043b205 tail sets drag, runs an iter loop
 *     of `state_machine + pos += vel` (anchored back), and kills at the
 *     per-type AGE threshold.
 *   - 0x89 / 0x9e — compass-direction billboard: pos anchored to owner
 *     pose with sin/cos(owner+0x18 / 8 * 2π) * radius; for 0x89 with
 *     age>10 spawns two scene1_overlay slots per tick (types 0xf and
 *     0x41); same iter-loop shape as the anchor cascade (reverse order:
 *     pos += vel BEFORE state_machine).  Kills on owner+0x428 != 1 OR
 *     age == 0xaf.
 *
 * Wiring:
 *   scene1_sim.c::scene1_ingame_default_arm_tick — calls
 *   `scene1_records_b_tick()` every INGAME frame on the default sim arm.
 *
 * Dormancy in production:
 *
 *   - Table B is BSS-zero at boot (every slot's TYPE field = 0 = dead).
 *   - The C8j allocator ladder is `unwired` in production (no in-port
 *     caller of scene1_record_b_spawn_npc / _entity).  Only the C8j.fin.b
 *     smoke flags (`--force-b-npc <type>` / `--force-b-entity <type>`)
 *     commit slots; defaults are -1 (no-op).
 *   - With smoke flags set to one of the C8j-tick.2 types, the slot's
 *     pose is reset every tick to the owner's anchor; with the SE/spawn
 *     hooks at their default no-op state, no audible / overlay output.
 *
 *   Consequence: in HOUSE with no smoke flags set, the tick walks
 *   512 dead slots per frame and returns — pure no-op.  Goldens
 *   (boot-idle, title-z-press, title-down-press) stay bit-exact.
 *
 * Hooks (host-installable):
 *
 *   - scene1_records_b_set_per_type_body(fn) — override the production
 *     dispatch.  Pass NULL to revert to the in-module dispatch_default
 *     (the version the engine ships).  Tests use this to inject
 *     instrumented bodies.
 *
 *   - scene1_records_b_set_state_machine_hook(fn) — stand-in for engine
 *     FUN_0043865e (Mt. Everest #2, 8059 B per-record state machine,
 *     PHC #20).  Called from the LAB_0043b205 iter loop AND the
 *     LAB_0043b325 0x89/0x9e iter loop; default `NULL` = no-op.  The
 *     C8jb.* ladder will install a real body once that ports.
 *
 *   - scene1_records_b_set_se_hook(fn) — stand-in for engine
 *     FUN_00499519 (sound-effect-by-ID).  Called from the 0x1e age==4
 *     and age==0x4b sites, and 0x89/0x9e age==0x50.  Default NULL =
 *     no-op (tests don't see SE side effects).
 */
#ifndef SCENE1_RECORDS_B_TICK_H
#define SCENE1_RECORDS_B_TICK_H

#include <stdint.h>

#include "scene1_records.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Engine DAT_06a46f98 — cleared at every slot iteration top; sub-chips
 * may set it from within per-type bodies to short-circuit retries.
 * PHC #21 — exact semantics unknown until first reader-body lands. */
extern int32_t g_scene1_records_b_tick_flag;

/* Hook signatures. */
typedef void (*scene1_b_per_type_body_fn)(int slot_idx, int32_t type);
typedef void (*scene1_b_state_machine_fn)(int32_t *slot);
typedef void (*scene1_b_se_fn)(uint16_t se_id);

/* Setters return prior value so tests can save/restore.  Pass NULL to
 * revert to the default — for `per_type_body` that's the in-module
 * `dispatch_default` (engine-faithful body); for `state_machine` and
 * `se` it's a no-op (engine helpers unported / side-effect-only). */
scene1_b_per_type_body_fn scene1_records_b_set_per_type_body(
    scene1_b_per_type_body_fn fn);
scene1_b_state_machine_fn scene1_records_b_set_state_machine_hook(
    scene1_b_state_machine_fn fn);
scene1_b_se_fn            scene1_records_b_set_se_hook(scene1_b_se_fn fn);

/* Mark a slot dead — engine LAB_004411e3 (`*piVar14 = 0`).  Setting
 * TYPE = 0 makes the next allocator scan reclaim it.  Death-effect
 * spawn (LAB_0043f39b 0x21 particle on age-parity) deferred to
 * C8j-tick.13. */
void scene1_records_b_tick_kill_slot(int slot_idx);

/* Walk all 512 slots; for each live slot run preamble + per-type
 * dispatch.  Engine FUN_0043ae20 outer loop. */
void scene1_records_b_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_B_TICK_H */
