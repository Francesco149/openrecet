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
 *   - DAT_069b2fb0 → g_scene1_records_a (stride 0x25 dw, 4096 slots)
 *   - DAT_069324b0 → g_scene1_records_b (stride 0x49 dw,  512 slots)
 *   - DAT_06956cd8 → g_scene1_records_c (stride 0x25 dw,  200 slots)
 *
 * Sentinel conventions (from FUN_0040f64b):
 *   - A and C: [0] == -1 means slot empty.
 *   - B:       [0] ==  0 means slot empty; [2] holds slot index.
 */
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

#ifdef __cplusplus
}
#endif

#endif /* SCENE1_RECORDS_H */
