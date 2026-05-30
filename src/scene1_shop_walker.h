/*
 * scene1_shop_walker.h — C8c port of FUN_004552d0 (5210 B).
 *
 * One of the four mesh walkers dispatched by scene1_render_meshes
 * (C8a, FUN_00459dfd's L218 call site).  Runs in the WIDE-frustum
 * pass (z_far swapped to 2000+ by the caller before entry).  Despite
 * its 5.2 KB size in the decomp this is structurally a sequence of
 * seven per-record loops + a tail-light setup + a tail-projection
 * swap; most of the body is the loop scaffolding and per-record
 * matrix math.
 *
 * The seven walker passes, in source order (engine line numbers from
 * docs/decompiled/by-address/4552d0.c):
 *
 *   Pass A (L68..L96)    — DAT_0076bd94 table, stride 0x2e9 (745
 *                          dwords / record).  Walks fixed range to
 *                          DAT_007c8f94.  Type filter
 *                          {0x3e, 0x3f, 0x41, 0x42}.  Per-record
 *                          calls FUN_00455191(0) with a
 *                          Translation × Scaling × RotationY world
 *                          matrix.  In HOUSE every record is
 *                          BSS-zero → dormant.
 *
 *   Pass B (L97..L193)   — DAT_069325b8 alias of g_scene1_records_b
 *                          (slot dw 0x42 base, stride 0x49).
 *                          Count-bounded by g_scene1_records_b_count.
 *                          Three type-branches dispatched by cardinal-
 *                          int (asm `cmp eax, K` is authoritative —
 *                          Ghidra's float-as-int reinterp produced
 *                          misleading raw-bits constants in the older
 *                          decomp):
 *                            0x8c — primary body (PART_IDX % 2 == 0)
 *                            0x9b / 0x9c — outer body + nested 4-iter
 *                                          sin/cos spoke sub-loop
 *                          Each sub-body emits via FUN_00455191(<mesh>)
 *                          with three different mesh-record slots
 *                          (0x73a96a8 / f8 / 0x73a9720).  All three
 *                          slots are DUNGEON-loaded only — HOUSE
 *                          leaves them NULL.  Ported in C8c.B.
 *
 *   Mid block (L194..197)— Four RS writes (AMBIENT, LightEnable(0,1),
 *                          LIGHTING=1, TSS COLOROP=7).
 *
 *   Pass C (L198..L237)  — g_scene1_records_b at slot[0] base
 *                          (DAT_069324b0 alias; different bias
 *                          than Pass B's slot[42]).  Stride 0x49,
 *                          count-bounded by g_scene1_records_b_count.
 *                          Cardinal-int type filter (asm `cmp eax,
 *                          K` authoritative): 0x23/0x2c/0x2b
 *                          require PART_IDX % 2 == 0; 0x56/0x96
 *                          always emit.  Per-record matrix chain
 *                          MATRIX0 × RotY(ROT_SCR) × S(-s,s,s) ×
 *                          T(POS), s = LIFE_MULT*0.2.  Emits via
 *                          FUN_00455191(&DAT_073a9680) — SAME
 *                          mesh-record slot as Pass D
 *                          (train_iwa.x, DUNGEON-loaded).  Ported
 *                          in C8c.C.
 *
 *   Pass D (L238..L258)  — DAT_069b2fb0 table, stride 0x25.
 *                          Count-bounded by DAT_0076b960.  Type
 *                          filter {0x74, 0x79, 0x96}.  Per-record
 *                          calls FUN_00455191(&DAT_073a9680) with a
 *                          Translation × Scaling × RotationX ×
 *                          Scaling(1,1,1) chain.
 *
 *   Pass E (L259..L317)  — Outer count from DAT_0438b89c (float
 *                          BSS-zero) × inner 10.  Outer stride
 *                          0xd89 dwords, inner stride 0xf8 dwords.
 *                          Per-record:
 *                            FUN_00454f03(4) + identity matrix
 *                            (FUN_00404bb8) + optional TEXTUREFACTOR
 *                            + Translation + matrix copy into two
 *                            scratch destinations + Scaling +
 *                            RotationY + FUN_00403d79 + FUN_00404866
 *                            + FUN_00404870 + FUN_00404a20 (the
 *                            scene-tree entry — recursive D3DXFRAME
 *                            walker chain we surveyed in C8b's
 *                            leaf-chain doc).
 *
 *   Pass F (L318..L324)  — Same table as Pass A, calls
 *                          FUN_00456d48(record+offset) for records
 *                          with [1]!=0, *[0]*0x1a's offset == 1,
 *                          and [-0x12]==0xff.
 *
 *   Tail state (L325..L356) — 8 RS writes + projection swap with
 *                          z_far computed from stage-palette gate.
 *                          For HOUSE (palette+0x108 == 0):
 *                            z_far = 2200.0 - (DAT_0438b778 +
 *                                              DAT_044e2c70 - 11.0)
 *                                            * 75.0
 *                          BSS-zero → z_far = 3025.0.  Also FUN_
 *                          00454f03(2) + SetVertexShader(0x142, the
 *                          RHW + DIFFUSE + TEX1 FVF) for Pass G.
 *
 *   Light pass (L357..L456) — Gated by DAT_0438b8bc == 0 AND
 *                          palette+0x1ae0 != 0.  Sets up to 3 lights
 *                          from per-stage tables at DAT_056da1d8 /
 *                          DAT_056daae8 + computes per-light alpha
 *                          fade from DAT_0438b4b4 (a fade-in
 *                          counter).  HOUSE palette+0x1ae0 == 0 →
 *                          dormant.
 *
 *   Tail call (L457)     — FUN_004705a3 (327 B, dormant in HOUSE:
 *                          gated by DAT_005c7dd0 which BSS-zeros).
 *
 *   Pass G (L460..L514)  — DAT_0076bdc0 table, stride 0x2e9.  Walks
 *                          fixed range to DAT_007c8fc0.  Per-record
 *                          gates: pVar7[-10] != 0.0 AND pVar7[0x1a9]
 *                          < 1 AND pVar7[-0x1d] == 3.57331e-43 (raw
 *                          0xff).  Calls FUN_0045a56f for the
 *                          per-record sprite draw.  Records BSS-zero
 *                          → dormant.
 *
 *   Tail RS (L515)       — D3DRS_ZFUNC = 5 (D3DCMP_LESSEQUAL).
 *
 *   Tail call (L516)     — FUN_00454f03(3) — TSS COLORARG2 reset.
 *
 * The per-record draw helpers (FUN_00455191 / FUN_00456d48 /
 * FUN_0045a56f) and the scene-tree chain (FUN_00404a20 +
 * FUN_004047df + FUN_00404757 + FUN_00403eb7) are NOT ported in this
 * chip — they live behind explicit TODO stubs.  Every walker pass in
 * HOUSE iterates a BSS-zero-bounded loop or BSS-zero-flagged records,
 * so the dormant-output of "no draws fire, state writes do" matches
 * the engine.
 *
 * What this chip lands as observable behaviour:
 *
 *   ~30 SetRenderState / SetTextureStageState writes at the top
 *   ~4 SetRenderState writes in the mid block
 *   ~10 SetRenderState / SetTextureStageState / SetTransform /
 *     SetVertexShader writes in the tail block, including the
 *     wide-to-narrow projection swap and FVF flip to RHW
 *   1 TSS COLORARG2 write at the very tail
 *
 * Wiring: replaces scene1_walk_wide_frustum_TODO's call site in
 * scene1_render_meshes (scene1_render.c L218 / engine L218).
 *
 * No-op when dev is NULL.
 */

#ifndef OPENRECET_SCENE1_SHOP_WALKER_H
#define OPENRECET_SCENE1_SHOP_WALKER_H

#include <stdint.h>

#include "mesh.h"  /* mesh_t — for the Pass D mesh setter */

/* Pass D record-emit predicate + matrix composer.  D3D-free helpers,
 * available on host (Linux) for unit tests.  See
 * scene1_shop_walker_helpers.c for derivation. */
int  sw_pass_d_should_emit(const int32_t *slot);
void sw_pass_d_compose_world(float out[16], const int32_t *slot);

/* Pass B record-emit predicates + matrix composers (C8c.B).  D3D-free.
 *
 * Two sub-bodies dispatch by TYPE:
 *
 *   main  — TYPE == 0x8c, gated by PART_IDX % 2 == 0.  Composes
 *             MATRIX0 × Rx(ROT_X) × S(-s,s,s) × T(POS), s = LIFE_MULT*0.06.
 *   outer — TYPE ∈ {0x9b, 0x9c}.  Composes
 *             Ry(ROT_SCR) × Rx(-ROT_X) × S(-s,s,s) × T(POS), s = LIFE_MULT*0.05.
 *             Then the 4-iter spoke loop emits per-spoke matrices:
 *             T(sin(θ)·r/0.05, cos(θ)·r/0.05, 70.0f) × outer.
 *
 * spoke_pose computes (radius, angle) for spoke_idx ∈ [0, 4):
 *   base_angle = spoke_idx * π/2; default radius = 0.1f.
 *   For 0x9b with AGE > 60 / 0x9c with AGE > 20, both grow with AGE.
 *   Radius is clamped to 2.5f. */
int  sw_pass_b_should_emit_main(const int32_t *slot);
int  sw_pass_b_should_emit_outer(const int32_t *slot);
void sw_pass_b_compose_world_main(float out[16], const int32_t *slot);
void sw_pass_b_compose_world_outer(float out[16], const int32_t *slot);
void sw_pass_b_spoke_pose(float *out_radius, float *out_angle,
                          const int32_t *slot, int spoke_idx);
void sw_pass_b_compose_world_spoke(float out[16], const float outer[16],
                                   const int32_t *slot, int spoke_idx);

/* Pass C record-emit predicate + matrix composer (C8c.C).  D3D-free.
 *
 * Walks g_scene1_records_b at slot[0] base (different bias than Pass
 * B's slot[42]).  Type filter: {0x56, 0x96} always emit; {0x23, 0x2c,
 * 0x2b} require PART_IDX % 2 == 0; other types skip.
 * Matrix chain: MATRIX0 × Ry(ROT_SCR) × S(-s,s,s) × T(POS),
 * s = LIFE_MULT * 0.2 (.rdata 0x5198d8). */
int  sw_pass_c_should_emit(const int32_t *slot);
void sw_pass_c_compose_world(float out[16], const int32_t *slot);

/* Pass A record-emit predicate + matrix composer (C8c.A).  D3D-free.
 *
 * Walks engine's DAT_0076bd94..DAT_007c8f94 range (128 records × stride
 * 0x2e9 dw = 372 KB; per-stage furniture/NPC instance table — not
 * ported as a typed global yet).  Slot pointer anchors at TYPE
 * (engine `piVar8` = slot[0]); some offsets are negative — see
 * SCENE1_RECORDS_SHOP_OFF_ROT_SRC.
 *
 * Per-record gate (4 checks): ACTIVE != 0 AND VISIBILITY < 1 AND TYPE
 * ∈ {0x3e, 0x3f, 0x41, 0x42} AND SUBGATE != -1.
 *
 * Variant selector: TYPE ∈ {0x3f, 0x42} → variant 1; {0x3e, 0x41} →
 * variant 0.  Variant indexes into two adjacent 3-float position
 * triplets in the record.
 *
 * Matrix chain: Rx(angle) × S(-0.04, 0.04, 0.04) × T(POS_v) where
 * angle = (float)ROT_SRC * 0.05f.  Scale is hard-coded (not slot-driven)
 * — unlike Pass B/C/D which scale by LIFE_MULT.  ROT_SRC is read as
 * `fild` (int → float convert), not `fld` (float-load). */
int  sw_pass_a_should_emit(const int32_t *slot);
int  sw_pass_a_variant(int32_t type);
void sw_pass_a_compose_world(float out[16], const int32_t *slot);

/* Shop record dword offsets used by Pass A (and future Pass F/G).
 * Slot pointer anchors at TYPE.  Total record stride is 0x2e9 dw.
 *
 * ROT_SRC sits at slot[-0x23] (-0x8c bytes from the anchor) — engine's
 * record layout has fields both before and after the type/active
 * fields, with piVar8 positioned mid-record.  Callers (and tests)
 * that construct synthetic slots must allocate enough leading padding
 * (≥ 0x23 dw before the anchor) to read this field safely. */
#define SCENE1_RECORDS_SHOP_STRIDE      0x2e9   /* dwords per record */

#define SCENE1_RECORDS_SHOP_OFF_TYPE         0       /* cardinal int */
#define SCENE1_RECORDS_SHOP_OFF_ACTIVE       1       /* nonzero = active */
#define SCENE1_RECORDS_SHOP_OFF_ROT_SRC     (-0x23)  /* int, fild → ×0.05 → RotX angle */
#define SCENE1_RECORDS_SHOP_OFF_POS_X_V0     0xc5    /* float, variant=0 pos triplet */
#define SCENE1_RECORDS_SHOP_OFF_POS_Y_V0     0xc6
#define SCENE1_RECORDS_SHOP_OFF_POS_Z_V0     0xc7
#define SCENE1_RECORDS_SHOP_OFF_POS_X_V1     0xc8    /* float, variant=1 pos triplet */
#define SCENE1_RECORDS_SHOP_OFF_POS_Y_V1     0xc9
#define SCENE1_RECORDS_SHOP_OFF_POS_Z_V1     0xca
#define SCENE1_RECORDS_SHOP_OFF_SUBGATE      0x178   /* != -1 to enable */
#define SCENE1_RECORDS_SHOP_OFF_VISIBILITY   0x1b4   /* < 1 to enable */

/* Pass F-specific field — int compared to 0xff (== 255) for enable. */
#define SCENE1_RECORDS_SHOP_OFF_STATUS_F    (-0x12)  /* slot[-18], int compare to 0xff */

/* Pass F record-emit predicate (C8c.F).  D3D-free.
 *
 * Walks the same range as Pass A (engine DAT_0076bd94..DAT_007c8f94,
 * stride 0x2e9 dw).  Gates: ACTIVE != 0 AND type_enabled AND STATUS_F
 * == 0xff (= 255).  The type_enabled flag is the result of a lookup
 * into a per-type 0x1a-dword record at DAT_005c2410 (engine asm
 * `(type * 0x68 bytes) + 0x5c2410`).  Caller passes the lookup result
 * — the helper itself stays pure.
 *
 * Per-record action (in the walker body): call FUN_00456d48(slot -
 * 0x109) — a 526-byte scene-tree dispatcher we haven't ported.  The
 * walker uses a hook (sw_pass_f_set_emit_hook) so tests can observe
 * what would have fired. */
int sw_pass_f_should_emit(const int32_t *slot, int type_enabled);

/* Per-type enable lookup hook.  Default returns 0 (every type
 * disabled) since DAT_005c2410 is BSS-zero at boot and never
 * populated for HOUSE.  Tests + future ports can install a real
 * lookup via the setter. */
void sw_pass_f_set_type_enabled_hook(int (*hook)(int32_t type));
int  sw_pass_f_type_enabled(int32_t type);

/* Per-emit hook — receives the record pointer offset by -0x109 dw
 * (matching FUN_00456d48's expected scene-tree-record arg).  Default
 * no-op.  Setter is host-linkable for tests. */
void sw_pass_f_set_emit_hook(void (*hook)(const int32_t *record_offset));
void sw_pass_f_clear_emit_hook(void);

/* Routes to the installed emit hook (no-op if none set).  The walker
 * (Win32 only) calls this once per matching record; tests can call
 * it directly to verify hook plumbing. */
void sw_pass_f_fire_emit(const int32_t *record_offset);

/* Pass D mesh slot.  Stand-in for the engine's static &DAT_073a9680
 * (train_iwa.x, populated by FUN_00474a9a's DUNGEON branch only).
 * Default NULL → Pass D dormant inside scene1_emit_record (matches
 * HOUSE behavior).  Set non-NULL from main.c when --force-pass-d-mesh
 * provides a hand-loaded mesh.  Caller owns the mesh — the walker
 * holds a borrowed pointer. */
void          scene1_shop_walker_set_pass_d_mesh(const mesh_t *m);
const mesh_t *scene1_shop_walker_get_pass_d_mesh(void);

/* Pass D unlit-debug override (C8e.smoke, `--debug-pass-d-unlit`).
 * Default 0 → Pass D uses the engine's LIGHTING=TRUE + COLOROP=ADD
 * preamble from L548-562 verbatim.  When set to non-zero, sw_pass_d
 * overrides state to LIGHTING=FALSE + LightEnable(0,FALSE) +
 * CULLMODE=NONE + COLOROP=SELECTARG1 + COLORARG1=DIFFUSE before its
 * per-record loop — mirrors the brute-force state used by the
 * C8e.bridge proof-of-life (runs/c8e-bridge-smoke/
 * frame_00100_bridge_proof.png), but routes through the production
 * walker + emit path so the bridge + spawn + camera chain can be
 * verified end-to-end.  Pass E is permanently dormant in HOUSE so
 * no restore is needed before the tail block re-asserts state for
 * Pass G.  Diverges from engine state while set — do not enable
 * for goldens. */
void scene1_shop_walker_set_debug_pass_d_unlit(int on);
int  scene1_shop_walker_get_debug_pass_d_unlit(void);

/*
 * Cchr.2g (MVP) — seed the standing player billboard the shop-walker's
 * sw_pass_light section draws (engine FUN_004552d0 L357-454, the visible
 * player/companion draw at color 0xff808080).  Until FUN_0048b850 fills the
 * DAT_056daae8 ring + the DAT_056da1cc/1d8/dae18 player globals, this
 * supplies one player record (char/pos/anim/frame/facing); the sheet is
 * bound from scene1_preload_chr_sheet(char) (load it via
 * scene1_preload_load_chr_sheet first).  enable=0 disables.  No-op on the
 * host build (the draw is Win32-only).
 */
void scene1_shop_walker_set_player_inject(int enable, int char_id,
                                          float px, float py, float pz,
                                          int anim, int frame, int facing);

#ifdef _WIN32

struct IDirect3DDevice8;

void scene1_shop_walker(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_SHOP_WALKER_H */
