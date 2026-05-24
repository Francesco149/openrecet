/*
 * scene1_wide_followup.h — C8f.1 skeleton port of FUN_004161c7 (4925 B).
 *
 * The second of two functions called inside C8a's `scene1_render_meshes`
 * under the WIDE-frustum projection (z_far=2000):
 *
 *     scene1_render_push_projection(dev, 2000.0f);   // L216-L217
 *     scene1_shop_walker(dev);                        // L218 (C8c)
 *     scene1_wide_followup(dev);                      // L219 (THIS)
 *
 * Structurally a sibling of the C8c shop walker: same wide projection,
 * same per-record table reads, but emits 2D billboard quads via
 * DrawPrimitiveUP instead of 3D meshes via DrawSubset.  Six per-record
 * passes (A-F) plus two mid-pass injections.
 *
 * This skeleton chip (C8f.1) ports the top-level state preamble +
 * per-pass body stubs + the two mid-pass injection bodies + the tail
 * state writes + Pass F integration with the existing scene1_pass_f
 * module.  Per-pass inner draws (A/B/C/D/E) remain TODO stubs because
 * every pass is dormant in HOUSE today (BSS-zero count globals).
 *
 * The six passes (line numbers from docs/decompiled/by-address/4161c7.c):
 *
 *   Pass A (L50..L92)   — DAT_06932548 table, stride 0x49 dwords.
 *                         Count-bounded by DAT_0076b964.  Type filter
 *                         on fVar2 raw-bits {0x77, 0xa2}.  Per-record:
 *                         Translation × Scaling × RotZ(π/2) × RotY(π-yaw)
 *                         + 128-tex atlas UV (0.0078125 / 0.9921875).
 *                         Texture: DAT_073cc8e0.  Draw via
 *                         DrawPrimitiveUP(TRIANGLESTRIP, 2, vbuf, 0x18).
 *
 *   Pass B (L93..L127)  — DAT_06932514 table, stride 0x49 dwords.
 *                         Count-bounded by DAT_0076b964.  Type filter
 *                         on fVar2 raw-bits {0x53}.  Per-record:
 *                         Translation × Scaling × RotZ(π/2) + 256-tex
 *                         atlas UV (0.00390625 / 0.99609375).  Texture:
 *                         DAT_073d8620.
 *
 *   Mid block 1 (L128..L142) — Six RS writes + TSS COLOROP + light off
 *                         + SetVertexShader(0x142, RHW+DIFFUSE+TEX1) +
 *                         FUN_00414ee2(1, 0).  Same 2D overlay
 *                         dispatcher the C7h scene1_render_overlay
 *                         brackets call.
 *
 *   Pass C (L143..L203) — DAT_06956cd8 table, stride 0x25 dwords.
 *                         Count-bounded by DAT_0076b968.  Type filter
 *                         on fVar2 raw-bits {0, 1, 2, 3} → cardinal
 *                         enum.  Per-record: Translation × DAT_0438cdf8
 *                         (precomputed shared matrix) × Scaling.
 *                         Tile selector: `tile = (r[1]/3) % 7 +
 *                         type_offset[type]`, type_offset = {0, 8, 16,
 *                         24}.  Texture: DAT_073cc930.  vbuf:
 *                         DAT_0064e5d8.
 *
 *   Mid block 2 (L204..L223) — Projection swap to z_far=350 +
 *                         conditional inner 15×20 cell walk (gated on
 *                         DAT_0438b1c0 == 1 && palette+0 == 0):
 *                         FUN_00415fab() per non-(-1) cell, then
 *                         FUN_00485f8c() once.  Projection back to
 *                         z_far=2000 after.  This is the "shop floor
 *                         cell highlight" path.
 *
 *   Pass D (L224..L287) — DAT_06956cd8 table (same as Pass C!), stride
 *                         0x25.  Type filter `*r > 6` (cardinal int >
 *                         6).  Per-record: same matrix chain as Pass C,
 *                         plus per-record alpha-fade computation
 *                         (`local_18 == DAT_056dae40` gate selects a
 *                         pulsing variant) and per-record texture
 *                         lookup via FUN_004681f6 + DAT_095d3808 jump
 *                         table.  vbuf: DAT_0064e5d8 (shared with C).
 *
 *   State block (L288..L292) — TSS COLOROP=4 (MODULATE2X) + texture
 *                         cache flush to DAT_073cc940 (shared Pass E
 *                         texture).
 *
 *   Pass E (L293..L416) — DAT_069324b0 table, stride 0x49.
 *                         Count-bounded by DAT_0076b964.  Type filter
 *                         splits two groups:
 *                           {0x71, 0x72, 0x75}              — "spear"
 *                           {0x73, 0x7e, 0x78, 0xa0, 0x7a}  — "fan"
 *                         Spear: Translation × Scaling × shared-matrix
 *                         × RotY(π-yaw) + per-type 128/192 atlas UV.
 *                         Fan: FUN_00415f2e (camera-billboard matrix
 *                         helper) × RotZ(π/2) × type-driven aspect
 *                         Scaling + per-type 5-frame anim UV (0x7e) or
 *                         fixed tile UV (0x78/0xa0/0x7a/else).  vbuf:
 *                         DAT_0064bf68 (shared with A/B).
 *
 *   State block (L417..L422) — RS NORMALIZENORMALS=0 + RS LIGHTING=0
 *                         + texture cache flush to DAT_073cc8c0
 *                         (Pass F texture).
 *
 *   Pass F (L423..L481) — Already ported as src/scene1_pass_f.{c,h}.
 *                         Walks g_scene1_records_a (= DAT_069b2fb0)
 *                         for type-0x92 records, emits color-cycle
 *                         billboards via DrawPrimitiveUP.  This
 *                         skeleton calls scene1_pass_f_render() at
 *                         the Pass F slot.
 *
 *   Tail (L482)         — DAT_0076b95c = 0 (texture cache reset).
 *
 * Engine globals (sim-side BSS-zero for HOUSE — all loops dormant):
 *
 *   DAT_0076b964        — Pass A + B + E count.
 *   DAT_0076b968        — Pass C + D count.
 *   DAT_0076b960        — Pass F count (read inside scene1_pass_f from
 *                         g_scene1_records_a_count, equivalent).
 *   DAT_0076b95c        — texture-cache "last bound" tracker (we keep
 *                         this as a module-local static).
 *   DAT_0438b1e0        — stage-record selector (0x2dfc8 stride).
 *                         Used by mid block 2 to index DAT_044f7030.
 *   DAT_0438b1c0        — mid block 2 gate.
 *   DAT_068dd2f0 + 0    — stage palette field 0 (also a mid block 2
 *                         gate).
 *   DAT_056dae40        — Pass D pulsing-variant slot index.
 *
 * Wiring: replaces scene1_walk_wide_followup_TODO's call site in
 * scene1_render.c (scene1_render_meshes L219).
 *
 * No-op when dev is NULL.
 */

#ifndef OPENRECET_SCENE1_WIDE_FOLLOWUP_H
#define OPENRECET_SCENE1_WIDE_FOLLOWUP_H

#include <stdint.h>

/* ─── Pass C helpers (D3D-free, host-linkable) ─────────────────────────
 *
 * Live in scene1_wide_followup_helpers.c.  See that TU for the engine
 * line-number references and the cardinal-int / denormal-float
 * encoding the engine uses for the type filter.  Slot field offsets
 * are SCENE1_RECORDS_C_OFF_* from scene1_records.h /
 * scene1_records_c_tick.h.  All inputs are int32_t arrays into
 * g_scene1_records_c (the 0x25-dw-stride record table).
 *
 * The pre-matrix stand-in models engine DAT_0438cdf8 (writer
 * unidentified today; defaults to identity → benign).  Lives in the
 * helpers TU so host tests can exercise its setter without <d3d8.h>.  */
int          wf_pass_c_should_emit(const int32_t *slot);
float        wf_pass_c_per_record_scale(const int32_t *slot);
int          wf_pass_c_tile_index(const int32_t *slot);
void         wf_pass_c_uv_box(int tile,
                              float *out_u0, float *out_u1,
                              float *out_v0, float *out_v1);
void         wf_pass_c_compose_world(float out[16], const int32_t *slot);
void         wf_pass_c_set_pre_matrix(const float m[16]);
const float *wf_pass_c_get_pre_matrix(void);

/* ─── Pass A helpers (D3D-free, host-linkable) ─────────────────────────
 *
 * Walks g_scene1_records_b (stride 0x49) for type ∈ {0x77, 0xa2}, draws
 * via a 1-tile katter.tga (64×64) atlas with 1/128 UV inset.  Engine
 * FUN_004161c7 L51-91.  See helpers TU for line-number map.  */
int   wf_pass_a_should_emit(const int32_t *slot);
float wf_pass_a_per_record_scale(const int32_t *slot);
void  wf_pass_a_compose_world(float out[16], const int32_t *slot);

#ifdef _WIN32

struct IDirect3DDevice8;

void scene1_wide_followup(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_WIDE_FOLLOWUP_H */
