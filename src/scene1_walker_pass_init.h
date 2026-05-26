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
 *     shop_table array DAT_073b1ac8 → local_5f8[].  Chip PII.3a.
 *   Outer cache-slot loop + per-slot SetTexture dispatch + draw loop A
 *     (DAT_068dcca0 mesh draw, PII.3c-deferred) + draw loop B
 *     (DAT_073b1ac8 = our g_scene_table; the HOUSE-furniture renderer).
 *     PII.3b ports the outer loop + draw loop B (decomp L52806-L53043).
 *
 * PII.3a ports the Phase 2 matrix builder.  It
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

/* ───────── PII.3b — outer cache-slot loop + draw loop B ─────────── */

/* Engine DAT_073dddb4 — status-screen-open flag.  When != 0, draw
 * loop B short-circuits (decomp L52952: `if (DAT_073dddb4 == 0 && ...)`).
 * Writers: FUN_00475270 (recet.ini "effectmode" parser) + status-screen
 * open/close at 0x476aaf / 0x476b8b.  Default 0 (HOUSE-entry default,
 * no Q-menu open) matches production. */
extern int32_t g_scene1_walker_status_screen_open;

/* Stage-texture lookup hooks for the per-cache-slot SetTexture
 * dispatch (decomp L52813-L52870).  Each hook returns the
 * IDirect3DBaseTexture8* to bind for its flag-class.  NULL return (or
 * NULL hook) → SetTexture is skipped and the prior binding stays.
 *
 *   - kabe_      ← DAT_073cc630[local_24[0xb379] * 4]
 *   - yuka_      ← DAT_073b18d8[local_24[0xb37a] * 4]
 *   - shop_jutan ← DAT_073ac728[local_24[0xb37b] * 4]
 *   - animated   ← DAT_073aa198[((DAT_06a49b24 /
 *                                   *(int*)(DAT_068dd2f0+0x1a24)) %
 *                                  *(int*)(DAT_068dd2f0+0x1a20)) * 0x10]
 *
 * All four default to NULL — HOUSE shop_table furniture meshes use
 * the default cache sprite path (no stage-texture override).  Engine
 * stage palette + cycle accessors aren't ported yet, so non-default
 * hooks land later as part of PII.3c / Cf.* per-stage init. */
typedef void *(*scene1_walker_stage_texture_fn)(void);
void scene1_walker_set_kabe_texture_hook    (scene1_walker_stage_texture_fn fn);
void scene1_walker_set_yuka_texture_hook    (scene1_walker_stage_texture_fn fn);
void scene1_walker_set_jutan_texture_hook   (scene1_walker_stage_texture_fn fn);
void scene1_walker_set_animated_texture_hook(scene1_walker_stage_texture_fn fn);

/* Shop-table selector hook (engine `local_24[0xb37c]` = the same
 * per-stage selector at DAT_04510588 + DAT_0438b1e0 * 0x2dfc8 used by
 * src/scene_table.c).  Default resolves to `g_scene_table_selector`. */
typedef int (*scene1_walker_int_fn)(void);
void scene1_walker_set_shop_table_selector_hook(scene1_walker_int_fn fn);

/* Pure-C classifier: given the 6 flag values for a cache slot and the
 * outer-pass param, return which texture source the engine selects
 * for this slot (= which `LAB_004581df` jump arms `fire_draw`).
 * Mirrors the decomp L52813-L52870 nested-if cascade verbatim. */
typedef enum {
    SCENE1_WALKER_SLOT_SKIP    = 0,
    SCENE1_WALKER_SLOT_DEFAULT = 1,  /* g_mesh_tex_cache.entries[slot].sprite */
    SCENE1_WALKER_SLOT_KABE    = 2,
    SCENE1_WALKER_SLOT_YUKA    = 3,
    SCENE1_WALKER_SLOT_JUTAN   = 4,
    SCENE1_WALKER_SLOT_EXT_TGA = 5,  /* g_mesh_tex_cache.entries[slot].sprite */
    SCENE1_WALKER_SLOT_HIKARI  = 6,  /* animated overlay, armed-once */
    SCENE1_WALKER_SLOT_WATER   = 7   /* animated overlay, armed-once */
} scene1_walker_slot_action;

scene1_walker_slot_action scene1_walker_classify_slot(
    int water_flag, int kabe_flag, int yuka_flag, int shop_jutan_flag,
    int ext_tga_flag, int hikari_flag, int param_1);

/* Pure-C mesh-index calculator for draw loop B inner-loop (engine
 * asm 0x4583b8..0x4583f8):
 *
 *   if (per_mesh_flag == 0) {
 *       *out_use_shop_table = 1;
 *       return mesh_type_value - 3 + selector * 2;          (shop_table)
 *   } else {
 *       *out_use_shop_table = 0;
 *       return mesh_type_value - 0x28a0 + (flag >> 6) * 2;  (wall/floor)
 *   }
 *
 * Caller checks the returned index against [0, 16) for shop_table or
 * against the wall/floor mesh array bound (unported as PII.3c). */
int scene1_walker_draw_b_mesh_index(int mesh_type_value, int32_t flag_value,
                                    int selector, int *out_use_shop_table);

#ifdef _WIN32
struct IDirect3DDevice8;

/* PII.3b — HOUSE branch of FUN_00457714: outer cache-slot loop +
 * draw loop B.  Walks `g_mesh_tex_cache.count` outer slots; for each
 * slot, scene1_walker_classify_slot picks a SetTexture source (gated
 * by `param_1` ∈ {0, 1, 2, 3}); then iterates the PII.3a phase 2
 * meshes, per-material filtered by `m->texture_slots[mat_i] == slot`.
 *
 * Scope:
 *   - Setup phase 1 (DAT_068dcca0 matrix builder) — NOT ported (PII.3c).
 *   - Setup phase 2 — PII.3a's `scene1_walker_phase2_compute`, called
 *     into an internal scratch buffer.
 *   - Outer loop (this function).
 *   - Per-slot flag dispatch — full 6-class cascade (4 of 6 dormant
 *     in HOUSE pass 0 by design).
 *   - Draw loop A (DAT_068dcca0 wall/floor/jutan) — NOT ported (PII.3c).
 *   - Draw loop B (DAT_073b1ac8 = g_scene_table) — PORTED.
 *   - Pulse path (DAT_0438cc08==2 && local_1c==DAT_0438bea4 inside
 *     per-face loop) — SKIPPED (gates BSS-zero in retail).
 *
 * HOUSE-branch gate (decomp L52658: stage palette mode < 1) is NOT
 * checked here — caller is responsible.  Today scene1_render_meshes
 * is the only caller and the engine's only HOUSE-mode dispatcher,
 * so unconditionally entering the HOUSE branch is correct.
 *
 * Production today (HOUSE entry, no smoke flags):
 *   - g_scene1_walker_phase2_count = 0 (writer FUN_00436f97 chunk
 *     unported — Cf.* sub-chip pending).
 *   - All 4 stage-texture hooks default NULL.
 * Net visible effect: outer slot loop runs (state side-effects via
 * scene1_emit_apply_material_state per slot), but no draw_loop_b
 * iterations fire.  Bit-exact at the canary level. */
void scene1_walker_pass_render_house(struct IDirect3DDevice8 *dev,
                                     int param_1);
#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_WALKER_PASS_INIT_H */
