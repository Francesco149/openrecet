/*
 * scene1_shop_walker.c — see scene1_shop_walker.h for the chip
 * writeup.
 *
 * C8c port of FUN_004552d0: the WIDE-frustum shop walker called by
 * scene1_render_meshes (FUN_00459dfd L218).  Structure ported line-
 * by-line from docs/decompiled/by-address/4552d0.c with per-record
 * draw helpers stubbed for chip-sized follow-ups.
 */

#include "scene1_shop_walker.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>

#include "math3d.h"          /* mat4_translation / scaling / rotation_x / mul */
#include "scene1_emit_record.h" /* scene1_emit_record — per-record draw helper */
#include "scene1_records.h"  /* per-pass active counts */
#include "scene1_render.h"   /* scene1_render_push_projection +
                                scene1_render_apply_palette_combiner_mode */
#include "stage_palette.h"   /* palette accessors when they land */

/* ─── engine scratch globals — module-local mirrors ────────────────────
 *
 * These are written by FUN_004552d0 and a handful of sibling render
 * functions; no consumer is ported yet, so they're write-only here.
 * Names match the engine's _DAT_ symbols so future grep finds them.
 */

/* _DAT_006051a8, _DAT_006051ac, _DAT_006051b8 — scratch flags set to
 * 0 at the top of the walker.  Almost certainly some pass-mode /
 * sub-pass tracking written here AND at FUN_00459dfd L186-L196 (we
 * also see writes at 0x4??? for combiner mode). */
static uint32_t g_scratch_006051a8 = 0;
static uint32_t g_scratch_006051ac = 0;
static uint32_t g_scratch_006051b8 = 0;

/* _DAT_00528ea0 — combiner-mode scratch set to 2 at L46 + L267 +
 * elsewhere.  Read by FUN_00454f03 to bias the COLORARG2 picker. */
static uint32_t g_scratch_00528ea0 = 0;

/* DAT_06a49b20 — render-pass complete flag.  Cleared at L325. */
static uint32_t g_scene1_pass_done_flag = 0;

/* ─── TODO accessors for not-yet-ported state ──────────────────────────
 *
 * Same pattern as scene1_render.c: every read of an engine global
 * routes through a named accessor returning the BSS-zero default.
 * Each accessor turns dormant by HOUSE → flip-to-real touches the
 * accessor and the global it owns, leaving the body of
 * scene1_shop_walker intact.
 */

/* DAT_0076b960 — Pass D record-count.  Bound on the DAT_069b2fb0
 * table loop.  Computed by scene1_records_counter_scan (C8g.1, was
 * FUN_00459dfd L52-L70); reads 0 until the populator lands. */
static int sw_pass_d_count(void) { return g_scene1_records_a_count; }

/* DAT_0076b964 — Pass B + Pass C record-count.  Bound on the
 * DAT_069325b8 / DAT_069324b0 table loops.  Computed by
 * scene1_records_counter_scan (C8g.1, was FUN_00459dfd L71-L77);
 * reads 0 until the populator lands. */
static int sw_pass_bc_count(void) { return g_scene1_records_b_count; }

/* DAT_0438b89c — Pass E outer count, stored as a FLOAT (per engine
 * decomp).  BSS-zero → 0.0f. */
static float sw_pass_e_outer_count(void) { return 0.0f; }

/* DAT_0438b8bc — Light-pass top gate.  BSS-zero → 0 → block enters. */
static int sw_light_pass_gate(void) { return 0; }

/* DAT_068dd2f0 + 0x1ae0 — stage-palette light-enable flag.  Already
 * exposed (scene1_palette_lighting_enabled); local thunk same as
 * above.  Dormant for HOUSE (palette zeroed). */
static int sw_palette_lighting_enabled(void) { return 0; }

/* DAT_068dd2f8 + DAT_0438b4dc * 0x1b3c — per-stage record at offset
 * 0 (an int).  HOUSE record is BSS-zero from stage_palette_init_
 * house, so this reads 0 → the "first projection arm" applies. */
static int sw_stage_record0(void) { return 0; }

/* DAT_0438b778 — float, "elapsed-time" or similar scene-1 counter.
 * BSS-zero.  Reads as 0.0f. */
static float sw_dat_0438b778(void) { return 0.0f; }

/* DAT_044e2c70 — float, sub-frame phase counter.  BSS-zero. */
static float sw_dat_044e2c70(void) { return 0.0f; }

/* DAT_045105a4 + DAT_0438b1e0 * 0x2dfc8 — per-stage sub-record at
 * offset 0 (int).  Selector + record table both BSS-zero at boot
 * via stage_init_house.  Reads as 0 → the "close-up" override arm
 * stays off. */
static int sw_stage_subrecord0(void) { return 0; }

/* DAT_0438cc08 — scene mode flag.  BSS-zero → 0.  Equality with 4
 * gates the close-up override. */
static int sw_dat_0438cc08(void) { return 0; }

/* DAT_0438b1a0 — light count selector.  Read inside the light pass
 * to pick how many lights to set up (1 vs 3).  BSS-zero → dormant
 * branch (we never enter the light pass since the outer gate is
 * BSS-zero == 0 only via the && palette+0x1ae0 != 0 inner gate). */
static int sw_dat_0438b1a0(void) { return 0; }

/* ─── per-record draw helpers — DEFERRED ───────────────────────────────
 *
 * The per-record body of every walker pass eventually calls one of
 * three engine helpers:
 *
 *   FUN_00455191 (217 B)   — Pass A/B/C/D/E.  Override-table-driven
 *                            SetTexture + SetMaterial +
 *                            ID3DXMesh::DrawSubset.  Passes A/B/E
 *                            call FUN_00455191(NULL); passes C/D call
 *                            FUN_00455191(&DAT_073a9680).
 *
 *   FUN_00456d48 (526 B)   — Pass F.  Looks up per-record material
 *                            at &DAT_005c23f0, builds Translation ×
 *                            Scaling × RotationY, then dispatches
 *                            into the scene-tree chain
 *                            (FUN_00403d79 + FUN_00404866 +
 *                            FUN_00404870 + FUN_00404a20).
 *
 *   FUN_0045a56f (1223 B)  — Pass G.  Per-sprite RHW quad emit via
 *                            DrawPrimitiveUP.  Likely warrants its
 *                            own chip.
 *
 * None of these are ported in C8c — they live as TODO comments on
 * the relevant per-pass body below.  HOUSE doesn't reach any of
 * them today because every walker pass short-circuits on a BSS-zero
 * count global or record active flag.
 */

/* FUN_004705a3 (327 B) — between-pass sweep at L457.  Iterates
 * DAT_073a6ea8 (count from DAT_005c7dd0); per-record calls
 * FUN_0045a56f.  Then unconditional FUN_0046f737 + FUN_00470d44.
 * Gated by stage-record0 == 0.
 *
 * For HOUSE: DAT_005c7dd0 is BSS-zero (writer is in some unported
 * region of the gameplay code), so the loop short-circuits.
 * FUN_0046f737 + FUN_00470d44 still run, but they themselves walk
 * BSS-zero records → dormant.  Whole call is dormant in HOUSE. */
static void sw_pass_between_TODO(void)
{
    /* TODO C8-followup: port FUN_004705a3 + its two unconditional
     * tail calls. */
}

/* ─── walker pass bodies ───────────────────────────────────────────────
 *
 * Every pass below ports the engine's per-record loop structure.
 * The per-record body inside each loop is a TODO stub call to the
 * appropriate sw_*_TODO helper — the heavy matrix math lives there.
 *
 * In HOUSE every loop is dormant (BSS-zero count globals or active
 * flags), so no record body executes.  The loops themselves do
 * still iterate over the static table memory range; for Pass A / F
 * this is DAT_0076bd94..DAT_007c8f94 (= 0x53200 bytes = ~5810 records
 * × 0xba4 stride).  Walking that range with all-zero "active"
 * flags is cheap (one int read per record), but worth noting if
 * someone benchmarks scene-1 idle.
 */

/* Pass A and Pass F share the same table walk.  Both iterate the
 * 5810-record range from DAT_0076bd94 to DAT_007c8f94 with stride
 * 0x2e9 dwords.  Without symbol exports for those addresses we walk
 * a NULL-sentinel range here (skipping the body entirely in HOUSE).
 *
 * TODO C8-followup: once the engine's DAT_0076bd94 table ports as a
 * typed record array (likely as part of the scene-1 instance-state
 * port), wire the iteration here.  The table is large (~5810 ×
 * 0xba4 = 5.5 MiB) so it lives in BSS; first-record-active flag is
 * piVar8[1] != 0.
 */
static void sw_pass_a(IDirect3DDevice8 *dev)
{
    /* Engine L68-L96:
     *
     *   piVar8 = &DAT_0076bd94;
     *   do {
     *     iVar4 = *piVar8;
     *     if (piVar8[1] != 0
     *         && piVar8[0x1b4] < 1                  // visibility gate
     *         && (iVar4 == 0x3e || iVar4 == 0x3f
     *             || iVar4 == 0x41 || iVar4 == 0x42)
     *         && piVar8[0x178] != -1) {
     *       local_10 = (iVar4 == 0x3f || iVar4 == 0x42) ? 1 : 0;
     *       Translation(local_68, piVar8[0xc5 + local_10*3],
     *                             piVar8[0xc6 + local_10*3],
     *                             piVar8[0xc7 + local_10*3]);
     *       Scaling(local_1b4, -0.040f, 0.040f, 0.040f);
     *       Multiply(local_68, local_1b4);
     *       RotationY(local_a8, piVar8[-0x23] * 0.05f);
     *       Multiply(local_68, local_a8, local_68);
     *       SetTransform(D3DTS_WORLDMATRIX(0), local_68);
     *       FUN_00455191(0);
     *     }
     *     piVar8 += 0x2e9;
     *   } while (piVar8 != &DAT_007c8f94);
     *
     * Dormant in HOUSE — all 5810 records have piVar8[1] == 0.
     * Once the table ports, wire the iteration and call
     * sw_emit_record_TODO(NULL) for each match. */
    (void)dev;
}

/* Pass B walks the count-bounded DAT_069325b8 table; stride 0x49
 * dwords.  Three type-branches inside the loop:
 *
 *   fVar2 == 1.96182e-43 (raw 0x8c) — even-iVar5 sub-branch:
 *     compose Translation × Scaling × RotationY → emit
 *
 *   fVar2 == 2.17201e-43 (raw 0xf7) OR 2.18603e-43 (raw 0xf8) —
 *     compose Translation × Scaling × neg-RotationY × RotationX → emit
 *     then nested 4-iter inner loop computing per-spoke
 *     Translation(sin·k, cos·k, 70.0f) → emit each
 *
 * All other types fall through.  Dormant in HOUSE
 * (sw_pass_bc_count() == 0).
 */
static void sw_pass_b(IDirect3DDevice8 *dev)
{
    int count = sw_pass_bc_count();
    if (count == 0) return;
    /* TODO C8-followup: walk DAT_069325b8 with stride 0x49 dwords;
     * dispatch on fVar2 raw-bits per the structure above; emit via
     * sw_emit_record_TODO(NULL) per match.  Inner nested loop runs
     * up to 4 spokes per match in the 0xf7/0xf8 branch. */
    (void)dev;
    (void)count;
}

/* Pass C walks DAT_069324b0 table (different table than Pass B!
 * same stride 0x49 dwords).  Type filter via two cascading
 * if-else groups on fVar2 raw-bits with side condition
 * local_10[0x27] % 2 > 0.  Per-record: Translation × Scaling ×
 * RotationX × Translation chain → emit with &DAT_073a9680
 * override. */
static void sw_pass_c(IDirect3DDevice8 *dev)
{
    int count = sw_pass_bc_count();
    if (count == 0) return;
    /* TODO C8-followup: walk DAT_069324b0 stride 0x49 dwords;
     * type filter per engine L205-L213; emit via
     * sw_emit_record_TODO((void *)0x73a9680) per match. */
    (void)dev;
    (void)count;
}

/* Pass D walks DAT_069b2fb0 table, stride 0x25 dwords.  Type
 * filter {0x74, 0x79, 0x96} with TYPE != -1 active gate.  Per-
 * record: Translation × Scaling(-s, s, s) × RotationX → emit with
 * &DAT_073a9680 override.  Engine FUN_004552d0 L239-L258, asm
 * @ 0x455bc8..0x455cea.
 *
 * The engine's &DAT_073a9680 is a static engine struct populated by
 * FUN_00472836(&DAT_073a9680, "xfile/etc/train_iwa.x", -1) — but
 * only in FUN_00474a9a's DUNGEON branch.  HOUSE leaves the slot
 * BSS-zero, so engine's `if (piVar2[0] != 0)` guard inside
 * FUN_00455191 short-circuits → Pass D is permanently dormant in
 * HOUSE by design.
 *
 * Port shape: a module-static (in scene1_shop_walker_helpers.c so
 * host tests can exercise it) substitutes for the engine global.
 * `--force-pass-d-mesh <path>` loads a mesh at boot and assigns it
 * via scene1_shop_walker_set_pass_d_mesh, mirroring the engine's
 * "DUNGEON-loaded mesh-record" state without porting the DUNGEON
 * preload.  Default NULL preserves byte-identical HOUSE behavior. */

static void sw_pass_d(IDirect3DDevice8 *dev)
{
    int count = sw_pass_d_count();
    if (count == 0) return;
    if (count > SCENE1_RECORDS_A_COUNT) count = SCENE1_RECORDS_A_COUNT;

    /* C8e.smoke `--debug-pass-d-unlit`: brute-force state override that
     * mirrors the C8e.bridge proof-of-life setup.  The engine's L548-562
     * preamble (LIGHTING=TRUE + LightEnable(0,TRUE) + COLOROP=ADD) needs
     * a per-stage SetLight + non-zero stage maplight to produce visible
     * lit pixels — HOUSE has neither (palette+0x1ae0 == 0 → engine
     * unlit by design), so even with --force-pass-d-mesh the production
     * Pass D path collapses to black.  This override forces the same
     * SELECTARG1+DIFFUSE state the bridge proof used, surfacing texture-
     * less vertex-diffuse silhouettes through the real walker + emit +
     * spawn + camera chain.  Diverges from engine; do not enable for
     * goldens.  Pass E is dormant in HOUSE so no restore is needed —
     * the tail block at L325-356 re-asserts state for Pass G. */
    if (scene1_shop_walker_get_debug_pass_d_unlit()) {
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
        IDirect3DDevice8_LightEnable(dev, 0, FALSE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                              D3DTOP_SELECTARG1);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1,
                                              D3DTA_DIFFUSE);
    }

    for (int slot_idx = 0; slot_idx < count; slot_idx++) {
        const int32_t *slot =
            &g_scene1_records_a[slot_idx * SCENE1_RECORDS_A_STRIDE];

        if (!sw_pass_d_should_emit(slot)) continue;

        float world[16];
        sw_pass_d_compose_world(world, slot);

        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                      (const D3DMATRIX *)world);

        /* Engine `FUN_00455191(&DAT_073a9680)` — the engine's Pass-D
         * mesh-record (train_iwa.x, loaded in DUNGEON only).  Our
         * stand-in defaults to NULL (HOUSE-dormant by design); when
         * --force-pass-d-mesh provides one, scene1_emit_record walks
         * cache slots × the mesh's submeshes and draws. */
        scene1_emit_record((struct IDirect3DDevice8 *)dev,
                           scene1_shop_walker_get_pass_d_mesh());
    }
}

/* Pass E outer × inner = sw_pass_e_outer_count() × 10.  Outer
 * stride 0xd89 dwords, inner stride 0xf8 dwords.  Inner body:
 *
 *   if (local_18[-3] >= 0) {
 *     _DAT_00528ea0 = 2;
 *     FUN_00454f03(4);
 *     local_8 = local_18[10] * 0.08f;
 *     identity(local_174);
 *     if (local_18[0xf] != 3.57331e-43)  // raw 0xff
 *       SetRenderState(TEXTUREFACTOR, local_18[0xf]<<24 | 0xffffff)
 *       SetTSS(0, ALPHAOP=4, 4)  // MODULATE2X
 *       SetTSS(0, ALPHAARG1=5, 2)
 *       SetTSS(0, ALPHAARG2=6, 3)
 *     Translation(local_174, local_18[-2], local_18[-1],
 *                            local_18[0]);
 *     copy local_174 → &DAT_073ad11c + local_18[-3]*0x2f0;
 *     Scaling(local_174, -local_8*local_18[0xc],
 *                         local_8*local_18[0xd],
 *                         local_8*local_18[0xc]);
 *     RotationY(local_f4, π - local_18[7]);
 *     Multiply(local_174, local_f4, local_174);
 *     copy local_174 → &DAT_073ad0dc + local_18[-3]*0x2f0;
 *     FUN_00403d79(local_18+0x19, local_18+0x7d, local_18[0x13]);
 *     FUN_00404866(0);
 *     FUN_00404870(0);
 *     FUN_00404a20();
 *     if (local_18[0xf] != 0xff) SetTSS(0, ALPHAOP=4, 2);
 *   }
 *
 * Dormant in HOUSE (sw_pass_e_outer_count() == 0.0f).
 */
static void sw_pass_e(IDirect3DDevice8 *dev)
{
    float outer_count = sw_pass_e_outer_count();
    if (outer_count == 0.0f) return;
    /* TODO C8-followup: nested outer × 10 inner loop per the
     * structure above.  The inner body's FUN_00404a20 call is the
     * scene-tree entry — see scene1-leaf-chain.md for the chain
     * that's still deferred to the chr-walker chip family. */
    (void)dev;
    (void)outer_count;
}

/* Pass F walks the same table as Pass A.  Per-record (after the
 * same active gate as Pass A) calls FUN_00456d48 — which itself
 * dispatches into the scene-tree chain. */
static void sw_pass_f(IDirect3DDevice8 *dev)
{
    /* Engine L318-L324:
     *
     *   piVar8 = &DAT_0076bd94;
     *   do {
     *     if (piVar8[1] != 0
     *         && (&DAT_005c2410)[*piVar8 * 0x1a] == 1
     *         && piVar8[-0x12] == 0xff) {
     *       FUN_00456d48(piVar8 - 0x109);
     *     }
     *     piVar8 += 0x2e9;
     *   } while (piVar8 != &DAT_007c8f94);
     *
     * Dormant in HOUSE — same active-flag dormancy as Pass A. */
    (void)dev;
}

/* Light pass — gated by sw_light_pass_gate() == 0 AND
 * sw_palette_lighting_enabled() != 0.  Sets up to 3 lights with
 * a per-light alpha fade controlled by sw_dat_0438b4b4.
 *
 * For HOUSE: palette+0x1ae0 == 0 → both inner-gate calls short-
 * circuit, no lights set or enabled.  We still write the top-level
 * AMBIENT reset at the tail of this block via the engine's L455
 * SetRenderState(AMBIENT, ?) — but the inner block dropped that
 * write's value via __ftol-mediated Ghidra arg loss.  Until the
 * AMBIENT value source is identified, we skip the write (no
 * consumer cares for HOUSE).
 */
static void sw_pass_light(IDirect3DDevice8 *dev)
{
    if (sw_light_pass_gate() != 0) return;

    if (sw_palette_lighting_enabled() != 0) {
        /* L359-L363: SetLight(?, ?) + LightEnable(0, TRUE) +
         *   SetRenderState(LIGHTING, TRUE) +
         *   SetRenderState(AMBIENT, 0xff000000).
         *
         * TODO C8-followup: SetLight args are dropped in Ghidra
         * decomp.  Likely (light_index=0, &DAT_06a49a40) per the
         * adjacent scene1_render_meshes light branch.  Until the
         * DAT_06a49a40 per-stage D3DLIGHT8 scratch ports, even
         * enabling the light has no defined effect (zero light). */
        IDirect3DDevice8_LightEnable(dev, 0, TRUE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT,  0xff000000u);
    }

    /* L364-L367: light count = 1 unless sw_dat_0438b1a0 == 1 in
     * which case the count is computed from sw_stage_record0 (1 or
     * 3 based on a sign trick).
     *
     *   local_14 = (DAT_0438b1a0 == 1)
     *              ? (((-(uint)(stage_record0 != 0)) & 0xfffffffe) + 3)
     *              : 1
     *
     * stage_record0 == 0 → ((0 & 0xfffffffe) + 3) = 3.
     * stage_record0 != 0 → ((0xfffffffe) + 3) = 1.
     * DAT_0438b1a0 != 1 → 1 light.
     */
    int light_count;
    if (sw_dat_0438b1a0() == 1) {
        light_count = (sw_stage_record0() != 0)
                          ? 1   /* engine: (0xfffffffe + 3) = 1 */
                          : 3;  /* engine: (0           + 3) = 3 */
    } else {
        light_count = 1;
    }

    /* L370-L453: per-light loop computing pose + alpha-fade from
     * DAT_056da1d8 (positions, 3 floats/light) + DAT_056dae18
     * (scales) + DAT_056daae8 (per-light sprite record) +
     * DAT_056da1cc (per-light tex slot).  Alpha fade-in is
     * (90 - DAT_0438b4b4) / 30.0 clamped to 1.0, scaled to 0.03.
     *
     * TODO C8-followup: port the per-light pose + draw.  Dormant
     * for HOUSE since none of the data globals are populated by
     * any port today (stage_palette init zeroes them). */
    (void)light_count;

    /* L455: SetRenderState(AMBIENT, ?) — value source is dropped
     * in decomp.  Skip until identified. */
}

/* Pass G walks DAT_0076bdc0..DAT_007c8fc0, stride 0x2e9 dwords.
 * Per-record gates:  [-10] != 0.0  &&  [0x1a9] < 1  &&
 *                    [-0x1d] == 3.57331e-43 (raw 0xff).  Then
 * computes a scaled alpha (saturated to 0xff), looks up
 * DAT_005c23f0 + (record[-0xb] * 0x68) for a material, builds
 * Translation × Scaling, and calls FUN_0045a56f with the world
 * matrix.  Sticky texture state across iterations via local_14
 * tracking the previous material slot.
 *
 * Dormant in HOUSE — all records BSS-zero on the active flag. */
static void sw_pass_g(IDirect3DDevice8 *dev)
{
    /* TODO C8-followup: walk DAT_0076bdc0 stride 0x2e9 dwords with
     * the gates above; emit via sw_pass_g_sprite_TODO per match. */
    (void)dev;
}

/* ─── public entry ─────────────────────────────────────────────────── */

void scene1_shop_walker(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* ─── L42-L67: top render-state block ──────────────────────────── */

    /* L42-L43: Z-test + Z-write on.  (Redundant — the caller's
     * scene1_render_meshes L168-L169 already enabled both — but
     * the engine writes them again here.  Likely defensive for
     * the case where some intervening function disabled them. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);

    /* L44-L47: scratch flags. */
    g_scratch_006051ac = 0;
    g_scratch_006051a8 = 0;
    g_scratch_00528ea0 = 2;
    g_scratch_006051b8 = 0;

    /* L48: AMBIENT = 0xff000000 (opaque black).  Disables any
     * residual per-vertex ambient additive. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff000000u);

    /* L49-L50: bring up light 0 + enable FFP lighting.  Most
     * passes below the top block disable lighting again at L64-
     * L65; the brief enable here is for the Pass A/B/C/D body
     * (per-record material with shaded geometry). */
    IDirect3DDevice8_LightEnable(dev, 0, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);

    /* L51: NORMALIZENORMALS = TRUE.  Walls/floor have non-unit
     * normals after baked transforms; engine renormalizes per
     * vertex. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_NORMALIZENORMALS, TRUE);

    /* L52: ZFUNC = LESSEQUAL.  Engine's default for the shop
     * pass (allows co-planar overlays to pass the depth test). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

    /* L53-L54: Z-test + Z-write on (duplicate of L42-L43; engine
     * writes again after the LIGHTING flip, probably from a
     * macro/state-block expansion). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,      TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);

    /* L55-L56: alpha-test on at ref 0 — anything with alpha > 0
     * passes the alpha test (so transparent-pixel discard works
     * once a material has source alpha). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF,         0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE,  TRUE);

    /* L57-L59: alpha-blend setup: SRCALPHA × INVSRCALPHA (standard
     * "over" blend).  ALPHABLENDENABLE on. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND,        D3DBLEND_INVSRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,         D3DBLEND_SRCALPHA);

    /* L60: TSS ALPHAOP = SELECTARG1 (alpha = ARG1 = TEXTURE).  Engine
     * writes the literal value 2 = D3DTOP_SELECTARG1; an earlier port
     * pass mistranscribed this as D3DTOP_MODULATE (4), which compounded
     * texture.α with whatever the prior stage left in current and
     * darkened the alpha of every subsequent draw. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);

    /* L61: COLORVERTEX on (vertex color contributes to material
     * source per the prior DIFFUSEMATERIALSOURCE=COLOR1 setup). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_COLORVERTEX, TRUE);

    /* L62-L63: bilinear texture filtering. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);

    /* L64-L66: but then immediately turn lighting + fog off again.
     * The L49-L50 enable was a brief pulse — likely a hold-over
     * from an earlier engine version that the current code path
     * doesn't actually need.  Kept verbatim. */
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING,  FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* L67: TSS COLOROP = MODULATE (texture × diffuse).  Engine writes
     * literal value 4 = D3DTOP_MODULATE; an earlier port pass wrote
     * D3DTOP_MODULATE2X (5), which double-bright every textured draw
     * from the top of the walker.  (The "2X" in the prior comment was
     * a misread of value 4 as enum-symbol MODULATE2X.) */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);

    /* ─── L68-L96: Pass A ──────────────────────────────────────────── */
    sw_pass_a(dev);

    /* ─── L97-L193: Pass B ─────────────────────────────────────────── */
    sw_pass_b(dev);

    /* ─── L194-L197: mid render-state block ────────────────────────── */

    /* L194-L196: AMBIENT + light 0 + LIGHTING for the Pass C/D
     * pair (which need shaded geometry from the per-stage palette). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT, 0xff000000u);
    IDirect3DDevice8_LightEnable(dev, 0, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);

    /* L197: TSS COLOROP = ADD (texture + ARG2).  Engine writes literal
     * value 7 = D3DTOP_ADD; an earlier port pass wrote
     * D3DTOP_MODULATEALPHA_ADDCOLOR (18), confusing the numeric value
     * with an enum symbol that contains "ADD" in its name.  The bug
     * killed visible HOUSE Pass D pixels: MODULATEALPHA_ADDCOLOR is
     * α(ARG1)×ARG1 + ARG2, and with LIGHTING=TRUE + zero stage-light
     * (HOUSE maplight==0 keeps light 0 unset) the per-vertex diffuse
     * collapses to zero and ARG1 (default DIFFUSE) goes to zero too. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_ADD);

    /* ─── L198-L237: Pass C ────────────────────────────────────────── */
    sw_pass_c(dev);

    /* ─── L238-L258: Pass D ────────────────────────────────────────── */
    sw_pass_d(dev);

    /* ─── L259-L317: Pass E ────────────────────────────────────────── */
    sw_pass_e(dev);

    /* ─── L318-L324: Pass F (sweep) ────────────────────────────────── */
    sw_pass_f(dev);

    /* ─── L325-L356: tail render-state + projection swap ───────────── */

    /* L325: render-pass-complete scratch flag.  No consumer ported
     * yet. */
    g_scene1_pass_done_flag = 0;

    /* L326: FOGENABLE off — Pass G is 2D RHW, no fog. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* L327-L330: alpha-blend reset to SRCALPHA × INVSRCALPHA +
     * TSS COLOROP/ARG1/ARG2 reset for the 2D pass.
     *
     *   TSS(0, COLOROP=1,    2=MODULATE)
     *   TSS(0, ALPHAOP=4,    4=BLENDDIFFUSEALPHA)
     *   TSS(0, ALPHAARG1=5,  2=D3DTA_TEXTURE)
     *   TSS(0, ALPHAARG2=6,  3=D3DTA_TFACTOR)
     */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,    D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,    D3DTOP_BLENDDIFFUSEALPHA);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1,  D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2,  D3DTA_TFACTOR);

    /* L331-L332: light 0 off + LIGHTING off.  Pass G is unlit
     * RHW. */
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);

    /* L333-L346: projection swap.  z_far depends on stage state.
     *
     *   if (stage_record0 < 1) {
     *     local_14 = DAT_0438b778 + DAT_044e2c70;     // BSS = 0
     *     z_far = 2200.0 - (local_14 - 11.0) * 75.0;  // = 3025
     *     if (stage_subrecord0 > 0 && DAT_0438cc08 != 4) {
     *       z_far = 1100.0;
     *     }
     *   } else {
     *     z_far = 2000.0;
     *   }
     *
     * For HOUSE: stage_record0 == 0, sub-gates dormant → z_far =
     * 3025.0.
     */
    float z_far;
    if (sw_stage_record0() < 1) {
        float local_14 = sw_dat_0438b778() + sw_dat_044e2c70();
        z_far = 2200.0f - (local_14 - 11.0f) * 75.0f;
        if (sw_stage_subrecord0() > 0 && sw_dat_0438cc08() != 4) {
            z_far = 1100.0f;
        }
    } else {
        z_far = 2000.0f;
    }
    scene1_render_push_projection((struct IDirect3DDevice8 *)dev, z_far);

    /* L347-L348: bilinear filtering re-asserted (Pass C/D may have
     * left MIPFILTER off; the engine resets for Pass G's RHW). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);

    /* L349: CULLMODE = 1 (D3DCULL_NONE).  Pass G quads are wound
     * either way. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* L350: SetVertexShader(FVF 0x142 = XYZRHW|DIFFUSE|TEX1).  Pass
     * G geometry is pre-transformed (the engine bakes screen-space
     * coords into the vertex buffer in FUN_0045a56f). */
    IDirect3DDevice8_SetVertexShader(dev, 0x142u);

    /* L351: FUN_00454f03(2) — TSS COLORARG2 = map[2 % 7] = 5
     * (D3DTA_TEMP).  Sets the per-stage palette combiner mode
     * for the 2D pass. */
    scene1_render_apply_palette_combiner_mode((struct IDirect3DDevice8 *)dev, 2);

    /* L352-L356: more TSS writes for the 2D quad path.
     *
     *   TSS(0, MIPFILTER=0x12, 0=NONE)
     *   TSS(0, COLOROP=1,      8=ADDSIGNED)
     *   TSS(0, ALPHAOP=4,      4=BLENDDIFFUSEALPHA)
     *   TSS(0, ALPHAARG1=5,    2=D3DTA_TEXTURE)
     *   TSS(0, ALPHAARG2=6,    0=D3DTA_DIFFUSE)
     */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MIPFILTER,   D3DTEXF_NONE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,     D3DTOP_ADDSIGNED);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,     D3DTOP_BLENDDIFFUSEALPHA);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1,   D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2,   D3DTA_DIFFUSE);

    /* ─── L357-L456: light pass (gated, dormant in HOUSE) ──────────── */
    sw_pass_light(dev);

    /* ─── L457: between-pass sweep (dormant in HOUSE) ──────────────── */
    sw_pass_between_TODO();

    /* ─── L460-L514: Pass G ────────────────────────────────────────── */
    sw_pass_g(dev);

    /* ─── L515: ZFUNC reset ────────────────────────────────────────── */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

    /* ─── L516: tail combiner mode ─────────────────────────────────── */
    scene1_render_apply_palette_combiner_mode((struct IDirect3DDevice8 *)dev, 3);
}

#endif /* _WIN32 */
