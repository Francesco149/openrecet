/*
 * scene1_records.h — scene-1 per-record table storage + maintenance.
 *
 * Chip C8g.1 (2026-05-23).  Ports the sim-side data plumbing the
 * scene-1 mesh walkers (C8c shop, C8d alpha, the unported narrow /
 * wide-followup) read from.  Two engine functions:
 *
 *   - FUN_0040f64b @ 0x40f64b (128 B) — sentinel-init preamble.
 *   - FUN_00459dfd L51-L81           — "highest non-sentinel index"
 *                                      counter scan over the same
 *                                      tables, called once at the top
 *                                      of scene1_render_meshes.
 *
 * The full FUN_0040f64b also touches sibling tables DAT_044e28fc /
 * DAT_0695e07c / DAT_0064e818 and calls FUN_00414902 (an unrelated
 * 39 B init).  This module ports ONLY the three record-table touches;
 * the sibling tables land when their consumers port (no consumer reads
 * them today).
 *
 * Tables stay sentinel-empty until the sim-side populator FUN_0040fb3a
 * (8071 B, unported — "Mt. Everest" of the scene-1 render path) lands.
 * The counter-scan therefore lands 0/0/0 and every walker pass short-
 * circuits on its count gate, matching today's HOUSE dormancy.
 */
#ifndef SCENE1_RECORDS_H
#define SCENE1_RECORDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Table sizes derive from the address-range deltas in FUN_00459dfd's
 * L51-L81 counter scan:
 *
 *   DAT_069b2fb0 .. DAT_06a46fb0  →  0x94000 bytes  →  4096 × 0x25 dw
 *   DAT_069324b0 .. DAT_06956cb0  →  0x24800 bytes  →   512 × 0x49 dw
 *   DAT_06956cd8 .. DAT_0695e078  →  0x073a0 bytes  →   200 × 0x25 dw
 */
#define SCENE1_RECORDS_A_COUNT   4096
#define SCENE1_RECORDS_A_STRIDE  0x25   /* dwords per entry */
#define SCENE1_RECORDS_B_COUNT   512
#define SCENE1_RECORDS_B_STRIDE  0x49
#define SCENE1_RECORDS_C_COUNT   200
#define SCENE1_RECORDS_C_STRIDE  0x25

/*
 * Record storage.  Engine globals:
 *   - DAT_069b2f80 → g_scene1_records_a slot 0 (stride 0x25 dw, 4096 slots)
 *   - DAT_069324b0 → g_scene1_records_b (stride 0x49 dw,  512 slots)
 *   - DAT_06956cd8 → g_scene1_records_c (stride 0x25 dw,  200 slots)
 *
 * Table A is anchored at DAT_069b2f80 (slot base = first field).  The
 * engine's Ghidra dump frequently uses DAT_069b2fb0 (= slot_base +
 * 0x30 = TYPE-field address) and DAT_069b2f8c (= slot_base + 0x0c =
 * vel.x address) as alternative names for the same storage — we anchor
 * at the slot base so all field offsets are non-negative.
 *
 * Sentinel conventions (from FUN_0040f64b):
 *   - A and C: TYPE-field == -1 means slot empty.
 *   - B:       [0] ==  0 means slot empty; [2] holds slot index.
 *
 * Table A slot layout — 0x25 dwords (148 B) per slot.  See FUN_0040fb3a +
 * FUN_00447f4f for the per-handler field meanings; the offsets here are
 * the universal ones read by every consumer.
 */
#define SCENE1_RECORDS_A_OFF_POS_X     0   /* DAT_069b2f80 */
#define SCENE1_RECORDS_A_OFF_POS_Y     1   /* DAT_069b2f84 */
#define SCENE1_RECORDS_A_OFF_POS_Z     2   /* DAT_069b2f88 */
#define SCENE1_RECORDS_A_OFF_VEL_X     3   /* DAT_069b2f8c */
#define SCENE1_RECORDS_A_OFF_VEL_Y     4   /* DAT_069b2f90 */
#define SCENE1_RECORDS_A_OFF_VEL_Z     5   /* DAT_069b2f94 */
#define SCENE1_RECORDS_A_OFF_ROT_X     6   /* DAT_069b2f98 */
#define SCENE1_RECORDS_A_OFF_ROT_Y     7   /* DAT_069b2f9c */
#define SCENE1_RECORDS_A_OFF_ROT_Z     8   /* DAT_069b2fa0 */
#define SCENE1_RECORDS_A_OFF_BASE_X    9   /* DAT_069b2fa4 */
#define SCENE1_RECORDS_A_OFF_BASE_Y   10   /* DAT_069b2fa8 */
#define SCENE1_RECORDS_A_OFF_BASE_Z   11   /* DAT_069b2fac */
#define SCENE1_RECORDS_A_OFF_TYPE     12   /* DAT_069b2fb0  (-1 = empty) */
#define SCENE1_RECORDS_A_OFF_AGE      13   /* DAT_069b2fb4  (tick counter) */
#define SCENE1_RECORDS_A_OFF_SCALE    14   /* DAT_069b2fb8  (spawn param_6, float) */
#define SCENE1_RECORDS_A_OFF_AUX_15   15   /* DAT_069b2fbc */
#define SCENE1_RECORDS_A_OFF_PARAM1   16   /* DAT_069b2fc0  (spawn param_7 / scratch) */
#define SCENE1_RECORDS_A_OFF_PARAM2   17   /* DAT_069b2fc4  (per-type scratch) */
#define SCENE1_RECORDS_A_OFF_AUX_18   18   /* DAT_069b2fc8 */

/*
 * Table B field offsets — populated as integrator + allocator handlers
 * reference them.  Field-0 is the slot-active flag AND the type field
 * (B sentinel: == 0 means empty; non-zero is the per-record type).
 * Field-2 (engine: DAT_069324b8) holds the slot's own index — set by the
 * sentinel-init in scene1_records_reset().
 *
 * Allocator-related offsets (touched by FUN_0044376a + FUN_00445a8c,
 * surveyed in docs/findings/scene1-table-b-allocators.md and ported in
 * C8j.5).  ACTIVE / TYPE alias: writing TYPE claims the slot and trips
 * the sentinel for subsequent allocator scans.
 */
#define SCENE1_RECORDS_B_OFF_ACTIVE     0    /* DAT_069324b0 (sentinel) */
#define SCENE1_RECORDS_B_OFF_TYPE       0    /* alias — type IS the sentinel */
#define SCENE1_RECORDS_B_OFF_FLAG_A     1    /* DAT_069324b4 (npc-alloc: flag; entity-alloc: 0) */
#define SCENE1_RECORDS_B_OFF_SELF_IDX   2    /* DAT_069324b8 */
#define SCENE1_RECORDS_B_OFF_FLAG_B     3    /* DAT_069324bc (entity-alloc: flag; npc-alloc: -1) */
#define SCENE1_RECORDS_B_OFF_OWNER_A    4    /* DAT_069324c0 (entity-alloc owner; npc-alloc: 0) */
#define SCENE1_RECORDS_B_OFF_OWNER_B    5    /* DAT_069324c4 (npc-alloc owner; entity-alloc: 0) */
#define SCENE1_RECORDS_B_OFF_POS_X      23   /* DAT_0693250c */
#define SCENE1_RECORDS_B_OFF_POS_Y      24   /* DAT_06932510 */
#define SCENE1_RECORDS_B_OFF_POS_Z      25   /* DAT_06932514 */
#define SCENE1_RECORDS_B_OFF_VEL_X      26   /* DAT_06932518 */
#define SCENE1_RECORDS_B_OFF_VEL_Y      27   /* DAT_0693251c */
#define SCENE1_RECORDS_B_OFF_VEL_Z      28   /* DAT_06932520 */
#define SCENE1_RECORDS_B_OFF_ALT_POS_X  29   /* DAT_06932524 (per-type alt-target / sub-pos) */
#define SCENE1_RECORDS_B_OFF_ALT_POS_Y  30   /* DAT_06932528 */
#define SCENE1_RECORDS_B_OFF_ALT_POS_Z  31   /* DAT_0693252c */
#define SCENE1_RECORDS_B_OFF_ROT_SCR    35   /* DAT_0693253c (rotation scratch) */
#define SCENE1_RECORDS_B_OFF_ROT_X      36   /* DAT_06932540 (a.k.a. NPC bend angle for many bodies) */
#define SCENE1_RECORDS_B_OFF_ROT_Z      37   /* DAT_06932544 (random rot.z, set by drift cluster tail) */
#define SCENE1_RECORDS_B_OFF_AGE        38   /* DAT_06932548 */
#define SCENE1_RECORDS_B_OFF_PART_IDX   39   /* DAT_0693254c (per-particle slot index in multi-spawn) */
#define SCENE1_RECORDS_B_OFF_AUX_SENT1  40   /* DAT_06932550 (= 0xffffffff in preamble) */
#define SCENE1_RECORDS_B_OFF_DRAG       42   /* DAT_06932558 (life timer / drag) */
#define SCENE1_RECORDS_B_OFF_AUX_B0     44   /* DAT_06932560 (per-type aux flag; e.g. 0x2f three-of-six split) */
#define SCENE1_RECORDS_B_OFF_SCALE_X    45   /* DAT_06932564 (= 1.0f in preamble) */
#define SCENE1_RECORDS_B_OFF_OWNER_FLAG 46   /* DAT_06932568 (entity-alloc: owner+0xeac; npc-alloc: 0) */
#define SCENE1_RECORDS_B_OFF_SCALE_Y    47   /* DAT_0693256c (entity-alloc only: 1.0f) */
#define SCENE1_RECORDS_B_OFF_BYTE_PAIR  48   /* DAT_06932570/71/72 — bytewise; preamble zeroes low 2 bytes only (entity-alloc only) */
#define SCENE1_RECORDS_B_OFF_AUX_C8     49   /* DAT_06932574 (= 0 in preamble) */
#define SCENE1_RECORDS_B_OFF_MATRIX0    50   /* DAT_06932578 — 16 floats follow */
#define SCENE1_RECORDS_B_OFF_LIFE_MULT  66   /* DAT_069325b8 (= 1.0f in preamble) */
#define SCENE1_RECORDS_B_OFF_AUX_C0     68   /* DAT_069325c0 (= 0 in preamble) */
#define SCENE1_RECORDS_B_OFF_AUX_SENT2  69   /* DAT_069325c4 (entity-alloc only: 0xffffffff) */
#define SCENE1_RECORDS_B_OFF_SEQ_ID     71   /* DAT_069325cc (post-incremented from g_scene1_record_b_seq_counter) */

/*
 * Table C layout differs from A: TYPE is at offset 10 dw (= 0x28
 * bytes), not 12 like table A.  The engine's "table C base" alias
 * DAT_06956cd8 IS that type-field address; the slot's true starting
 * field (pos.x) is 10 dw earlier at DAT_06956cb0.  See
 * docs/findings/scene1-record-populators.md for the full layout +
 * allocator FUN_0044aef0 reference.  Other C offsets are in
 * scene1_records_c_tick.h (consumer module).
 */
#define SCENE1_RECORDS_C_OFF_TYPE     10   /* DAT_06956cd8 (sentinel) */

extern int32_t g_scene1_records_a[SCENE1_RECORDS_A_COUNT * SCENE1_RECORDS_A_STRIDE];
extern int32_t g_scene1_records_b[SCENE1_RECORDS_B_COUNT * SCENE1_RECORDS_B_STRIDE];
extern int32_t g_scene1_records_c[SCENE1_RECORDS_C_COUNT * SCENE1_RECORDS_C_STRIDE];

/*
 * Per-pass active counts.  Engine globals:
 *   - DAT_0076b960 → g_scene1_records_a_count (Pass D bound)
 *   - DAT_0076b964 → g_scene1_records_b_count (Pass B+C bound, wide
 *                                              walker; narrow walker
 *                                              shares)
 *   - DAT_0076b968 → g_scene1_records_c_count (alpha-walker bound)
 *
 * Each is set to "one past the last non-sentinel slot" by
 * scene1_records_counter_scan() — NOT a count of active records.
 */
extern int g_scene1_records_a_count;
extern int g_scene1_records_b_count;
extern int g_scene1_records_c_count;

/*
 * Sentinel-reset the three record tables.  Engine FUN_0040f64b @ 0x40f64b
 * (3-table preamble only — sibling tables deferred).  Call sites in the
 * engine pass param_1=1 except for two dispatch sites Ghidra failed to
 * argpush; for scene-1 entry, pass reset_c=1 to match the typical case.
 */
void scene1_records_reset(int reset_c);

/*
 * Recompute the three per-pass active counts from the table contents.
 * Engine FUN_00459dfd L51-L81.  Called once at the top of
 * scene1_render_meshes (FUN_00459dfd in the engine).
 */
void scene1_records_counter_scan(void);

/*
 * MVP test helper.  Manually populate table-A slot 0 with a single
 * type-0x92 (color-cycle billboard) record at the given world position,
 * then bump g_scene1_records_a_count to 1 so the renderers' count gate
 * passes.  Used by --show-pass-f-test in main.c to validate the
 * scene1_pass_f render path without needing the integrator or spawn API
 * ported yet.
 *
 * Writes through SCENE1_RECORDS_A_OFF_* with the field semantics
 * FUN_004161c7's Pass F (L423-481) expects.
 */
void scene1_records_inject_test_type92(float pos_x, float pos_y,
                                       float pos_z);

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_H */
