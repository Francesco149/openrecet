/*
 * scene1_records_b_tick.h — per-tick integrator for table B records.
 *
 * Chip C8j-tick.1 (2026-05-25).  Ports the SKELETON of engine
 * FUN_0043ae20 @ 0x43ae20 (25750 B Mt. Everest).  See
 * docs/findings/scene1-records-b-tick.md for the C8j-tick.0 survey.
 *
 * Scope of this chip:
 *
 *   - Outer 512-slot loop matching engine's `local_2c == 0x200` bound.
 *   - Skip-dead-slot gate (`slot[TYPE] == 0` → LAB_0043fbbc).
 *   - Per-tick flag clear (engine DAT_06a46f98 = 0; modeled as
 *     `g_scene1_records_b_tick_flag = 0`, PHC #21).
 *   - Preamble: pos.{x,y,z} += vel.{x,y,z}; age++ — per engine L36456-62.
 *   - Per-type dispatch: stubbed.  Each TYPE goes through
 *     `scene1_records_b_tick_dispatch_type(slot_idx)` which is a no-op
 *     for the skeleton.  Sub-chips C8j-tick.2..C8j-tick.13 will fill
 *     in per-type bodies cluster-by-cluster.
 *   - Kill helper `scene1_records_b_tick_kill_slot()` mirrors engine's
 *     LAB_004411e3 (`*piVar14 = 0`) — sets TYPE field to 0 to mark dead.
 *     Death-effect particle spawn (LAB_0043f39b age-parity gated 0x21
 *     spawn) deferred to C8j-tick.13.
 *
 * Wiring:
 *   scene1_sim.c::scene1_ingame_default_arm_tick — replaces the stub
 *   comment at L60 with `scene1_records_b_tick();`.  Runs every INGAME
 *   frame on the default-running sim arm (HOUSE default).
 *
 * Dormancy in production:
 *
 *   - Table B is BSS-zero at boot (every slot's TYPE field = 0 = dead).
 *   - The C8j allocator ladder is `unwired` in production (no in-port
 *     caller of scene1_record_b_spawn_npc / _entity).  Only the C8j.fin.b
 *     smoke flags (`--force-b-npc <type>` / `--force-b-entity <type>`)
 *     commit slots; defaults are -1 (no-op).
 *   - Per-type dispatch is a no-op stub in this chip.
 *
 *   Consequence: in HOUSE with no smoke flags set, the tick walks
 *   512 dead slots per frame and returns — pure no-op.  Goldens
 *   (boot-idle, title-z-press, title-down-press) stay bit-exact.
 *
 *   With `--force-b-{npc,entity} <type>` set, the slot is committed by
 *   the allocator + then sees per-tick pos += vel + age++ from this
 *   integrator.  No per-type behaviors fire yet (sub-chip work).
 *
 * Hooks (host-installable):
 *
 *   - scene1_records_b_set_per_type_body(fn) — sub-chips swap in their
 *     dispatch table here.  Default `NULL` = no-op.  Sub-chip
 *     C8j-tick.2+ provides the real body.
 *
 *   - scene1_records_b_set_state_machine_hook(fn) — stand-in for engine
 *     FUN_0043865e (Mt. Everest #2, 8059 B per-record state machine,
 *     PHC #20).  Called from ~73 per-type bodies; default `NULL` = no-op.
 *     The C8jb.* ladder will install a real body once that ports.
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

/* Setters return prior value so tests can save/restore.  Pass NULL to
 * revert to the default no-op. */
scene1_b_per_type_body_fn scene1_records_b_set_per_type_body(
    scene1_b_per_type_body_fn fn);
scene1_b_state_machine_fn scene1_records_b_set_state_machine_hook(
    scene1_b_state_machine_fn fn);

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
