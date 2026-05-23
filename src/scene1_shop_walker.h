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
 *   Pass B (L97..L193)   — DAT_069325b8 table, stride 0x49 (73 dw/
 *                          record).  Count-bounded by DAT_0076b964.
 *                          Three type-branches by fVar2 raw-bits:
 *                            0x8c (1.96182e-43)  — sub-branch 1
 *                            0xf7/0xf8 (2.17/2.18e-43) — sub-branch 2
 *                              + nested 4-iter cos/sin sub-loop
 *                          Per-record calls FUN_00455191(0).
 *                          DAT_0076b964 BSS-zero → dormant.
 *
 *   Mid block (L194..197)— Four RS writes (AMBIENT, LightEnable(0,1),
 *                          LIGHTING=1, TSS COLOROP=7).
 *
 *   Pass C (L198..L237)  — DAT_069324b0 table, stride 0x49.
 *                          Count-bounded by DAT_0076b964.  Type
 *                          filter on fVar2 raw-bits {0x37, 0x44,
 *                          0x55, 0x95, 0x88} via two if-else
 *                          branches.  Per-record calls
 *                          FUN_00455191(&DAT_073a9680) with a
 *                          Translation × Scaling × RotationX ×
 *                          Translation chain.
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

#ifdef _WIN32

struct IDirect3DDevice8;

void scene1_shop_walker(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_SHOP_WALKER_H */
