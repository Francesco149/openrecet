/*
 * scene1_walker_pass_init.h — port of FUN_00457714 (5323 B) chunks.
 *
 * FUN_00457714 is the "pass-init" walker, called from
 * scene1_render_meshes at L188 as the first walker entry (arg=0) and
 * from siblings with args 1/3.  Body splits into:
 *
 *   Phase 1 (decomp L52671-L52701) — per-mesh transforms for the
 *     wall/floor/jutan array DAT_068dcca0 → local_738[].
 *   Phase 2 (decomp L52704-L52803) — per-mesh transforms for the
 *     shop_table array DAT_073b1ac8 → local_5f8[].  THIS CHIP (PII.3a).
 *   Outer NPC loop + draw loop A (DAT_068dcca0 mesh draw) + draw loop
 *     B (DAT_073b1ac8 — the HOUSE-furniture renderer; PII.3b).
 *
 * PII.3a (this header) ports the Phase 2 matrix builder only.  It
 * builds one D3DMATRIX per shop_table mesh from a struct-of-arrays
 * of per-mesh fields (rot_y, pos_x/y/z, mesh_type + a separate flag
 * byte stream), applying:
 *
 *     world = S(-0.2, 0.2, 0.2) × <optional flip> × RotY(rot_y) × T(pos)
 *
 * Where <optional flip> appends T(2,0,0) × RotY(π) at most twice,
 * gated by mesh_type==4 + flag-byte match + rot_y / pos_y triggers.
 * See scene1_walker_pass_init.c for the full asm-decoded chain.
 *
 * Storage model:
 *   - Up to 20 meshes (the engine's parallel-array fields at
 *     DAT_0438bfcc..0438c15c are 20×4 bytes each).
 *   - Per-mesh fields live in 5 parallel int/float arrays so the
 *     port doesn't have to invent a struct.  Engine FUN_00436f97
 *     populates them on stage init.
 *   - Output matrices stride 64 bytes (D3DMATRIX).
 *
 * Production today: PII.3a builds matrices into a static scratch
 * buffer that PII.3b will hand off to SetTransform per outer-loop
 * iteration.  Phase 2 dormant in HOUSE on default BSS-zero count
 * (g_scene1_walker_phase2_count == 0).  FUN_00436f97's writer chunk
 * for these fields is in a separate Cf.* sub-chip still pending.
 */

#ifndef OPENRECET_SCENE1_WALKER_PASS_INIT_H
#define OPENRECET_SCENE1_WALKER_PASS_INIT_H

#include <stdint.h>

/* Engine cap: parallel-array field strides are 20 dwords each (the
 * 80-byte spacing between DAT_0438bfcc and DAT_0438c01c). */
#define SCENE1_WALKER_PHASE2_MAX 20

/* Per-mesh field arrays (= the engine's parallel arrays at
 * DAT_0438bfcc + i*4 / DAT_0438c01c + i*4 / +0x50 / +0xa0 / +0xf0).
 *
 * BSS-zero defaults match the engine's BSS state before any stage
 * init populates them.  FUN_00436f97 (per-stage init, partially
 * ported as Cf.*) writes these for HOUSE/DUNGEON stages; until that
 * sub-chip lands, the arrays stay zero in production. */
extern int32_t g_scene1_walker_phase2_mesh_type[SCENE1_WALKER_PHASE2_MAX]; /* DAT_0438bfcc + i*4 */
extern float   g_scene1_walker_phase2_rot_y    [SCENE1_WALKER_PHASE2_MAX]; /* DAT_0438c01c + i*4 */
extern float   g_scene1_walker_phase2_pos_y    [SCENE1_WALKER_PHASE2_MAX]; /* DAT_0438c06c + i*4 */
extern float   g_scene1_walker_phase2_pos_x    [SCENE1_WALKER_PHASE2_MAX]; /* DAT_0438c0bc + i*4 */
extern float   g_scene1_walker_phase2_pos_z    [SCENE1_WALKER_PHASE2_MAX]; /* DAT_0438c10c + i*4 */

/* Per-mesh count — engine DAT_0438bfb4.  Set by FUN_00436f97 on
 * stage init; 0 = phase 2 short-circuits (compute writes nothing). */
extern int32_t g_scene1_walker_phase2_count;

/* Flag-byte source for the mesh_type==4 special-transform gate.
 *
 * Engine reads `local_24 + 0xb1d4 + i*4` (= a per-NPC scratch buffer
 * populated by code we haven't traced).  Modelled as a
 * host-installable hook so PII.3a's matrix builder stays pure-C and
 * fully host-testable; default returns 0 → gate always closed and
 * the optional T(2,0,0)×RotY(π) chain never fires.
 *
 * Engine gate condition (asm 0x457e8f..0x457eaa):
 *     mesh_type[i] == 4
 *  && hook(i) != 0
 *  && (hook(i) & 0xffffffc0) == 0x000514c0
 *
 * The last condition is a specific 26-bit bit pattern; anything not
 * in [0x000514c0, 0x000514ff] fails. */
typedef int32_t (*scene1_walker_phase2_flag_fn)(int mesh_index);
void scene1_walker_phase2_set_flag_hook(scene1_walker_phase2_flag_fn fn);
scene1_walker_phase2_flag_fn scene1_walker_phase2_get_flag_hook(void);

/* Reset all phase-2 state to BSS-zero (count, all field arrays, and
 * the flag hook).  Used by test setup. */
void scene1_walker_phase2_reset(void);

/* Build one D3DMATRIX per mesh into out_matrices[i*16..i*16+16] for
 * i in [0, g_scene1_walker_phase2_count).  Caller-provided buffer
 * must hold at least count*16 floats; passing NULL or count==0 is a
 * no-op.  Returns the count actually written (clamped to
 * SCENE1_WALKER_PHASE2_MAX).
 *
 * Matrix composition per mesh (asm 0x457e48..0x457fff):
 *
 *   world = RotY(rot_y) × T(pos_x, pos_y, pos_z)
 *
 *   if (mesh_type == 4 && flag_hook(i) != 0
 *                       && (flag_hook(i) & 0xffffffc0) == 0x514c0) {
 *       if (rot_y == 0) {
 *           world = RotY(π) × T(2, 0, 0) × world
 *       } else if (rot_y == π/2) {
 *           world = RotY(π) × T(2, 0, 0) × world
 *           if (pos_y > 5.0) {
 *               world = RotY(π) × T(2, 0, 0) × world  // applied AGAIN
 *           }
 *       } else if (pos_y > 5.0) {
 *           world = RotY(π) × T(2, 0, 0) × world
 *       }
 *   }
 *
 *   world = S(-0.2, 0.2, 0.2) × world
 *
 * `mat4_mul(out, A, B)` = `A × B` per the project convention; the
 * engine's `Multiply(out, A, B)` matches.  All constants verified
 * via tools/analyze/pe.py:
 *   0x519314=2.0, 0x519320=0.0, 0x519434=π/2, 0x51943c=π,
 *   0x51953c=5.0, 0x5198d8=0.2, 0x519a8c=-0.2.
 */
int scene1_walker_phase2_compute(float *out_matrices);

#endif /* OPENRECET_SCENE1_WALKER_PASS_INIT_H */
