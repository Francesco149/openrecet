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
#include "call_trace.h"      /* E.2 CALL_TRACE_ENTER probe */
#include "scene1_camera.h"   /* g_scene1_camera_orient (= billboard base DAT_0438cdf8) */
#include "scene1_bg_npc.h"   /* scene1_bg_npc_sprite_render (FUN_0046f737) */
#include "scene1_chr_sprite.h" /* scene1_chr_sprite_render + CHR_ACTOR_* (player draw) */
#include "scene1_emit_record.h" /* scene1_emit_record — per-record draw helper */
#include "scene1_particles_tick.h" /* g_scene1_player_pos (DAT_056da1d8) */
#include "scene1_player_ctrl.h" /* player_ctrl_actor_* — the real actor model */
#include "scene1_preload.h"  /* scene1_preload_chr_sheet — the DAT_073a9b18 sheet */
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

/* DAT_0076bd94..DAT_007c8f94 — Pass A + Pass F per-stage record range
 * (128 records × stride 0x2e9 dw).  Not ported as a typed global yet;
 * the count stub returns 0 so the loop is dormant.  When the table
 * ports, replace with a real count (e.g. derived from a count global
 * or by walking until a sentinel record). */
static int           sw_pass_af_count(void)              { return 0;    }
static const int32_t *sw_pass_af_slot(int idx) { (void)idx; return NULL; }

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

/* DAT_044e2c70 — camera "eye.y add" config constant = 21.0 (.rdata
 * source DAT_005c4fd8), NOT a BSS-zero counter (the old stub mislabelled
 * it).  Loaded by the unported per-stage camera-param init; scene1_camera.c
 * already carries this value (g_eyey_add = 21.0, see its compose-formula
 * note).  It feeds the char-pass z_far: local_14 = DAT_0438b778(0 in
 * free-roam) + DAT_044e2c70(21) = 21 → z_far = 2200-(21-11)*75 = 1450.
 * Returning 0 here gave z_far = 3025, which drew the char body NEARER than
 * the additive wing-glow (z_far 2000) so the glow failed the ZFUNC=LE test
 * and was occluded — retail's 1450 puts the body FARTHER so the glow draws
 * over her head.  Proven by a synced port↔retail d3d-trace at the
 * house-walk-down-dense cap_03 (retail char z_far 1450.06, glow 2000.19).
 * See docs/findings/scene1-tear-visual-diffs.md / engine-quirks. */
static float sw_dat_044e2c70(void) { return 21.0f; }

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
    /* Engine FUN_004552d0 L68-L96 / asm @ 0x45548b..0x4555b6.  Walks
     * the fixed range DAT_0076bd94..DAT_007c8f94 (128 records × stride
     * 0x2e9 dw); the table itself is unported as typed storage so
     * sw_pass_af_count() returns 0 today.  When the table ports,
     * swap the count + slot accessors and the body fires through the
     * helpers verbatim.
     *
     * Per-record body asm-verified by C8c.A:
     *   gate: ACTIVE != 0 && VISIBILITY < 1 && TYPE ∈ {0x3e/0x3f/0x41/0x42}
     *         && SUBGATE != -1
     *   variant: 0 for 0x3e/0x41, 1 for 0x3f/0x42
     *   matrix: Rx(angle) × S(-0.04,0.04,0.04) × T(POS_v)
     *           where angle = (float)slot[-0x23] * 0.05f
     *   emit: FUN_00455191(0) — null mesh-record arg, engine reads
     *         default Pass A mesh from a still-unidentified static slot
     *         (HOUSE leaves it NULL → scene1_emit_record short-circuits).
     *
     * Doubly dormant in HOUSE: count_stub returns 0 AND the engine's
     * own active-flag check would reject every record. */
    int count = sw_pass_af_count();
    if (count == 0) return;

    for (int i = 0; i < count; i++) {
        const int32_t *slot = sw_pass_af_slot(i);
        if (!slot) continue;

        if (!sw_pass_a_should_emit(slot)) continue;

        float world[16];
        sw_pass_a_compose_world(world, slot);
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                      (const D3DMATRIX *)world);
        /* Engine: FUN_00455191(0) — null override; engine-default Pass
         * A mesh-record slot (unidentified, unported).  HOUSE leaves
         * it NULL → emit no-ops inside scene1_emit_record. */
        scene1_emit_record((struct IDirect3DDevice8 *)dev, NULL);
    }
}

/* Pass B walks the count-bounded g_scene1_records_b table; stride 0x49
 * dwords.  Three sub-bodies inside the loop, dispatched by TYPE
 * (cardinal-int — comments in this header that called out "raw 0xf7/
 * 0xf8" were stale, asm `cmp eax, 0x9b/0x9c` is authoritative):
 *
 *   TYPE == 0x8c — gated by PART_IDX % 2 == 0:
 *     compose MATRIX0 × RotX(ROT_X) × S(-s,s,s) × T(POS) → emit
 *     with engine mesh-record &DAT_073a96a8.
 *
 *   TYPE == 0x9b or 0x9c — outer body:
 *     compose RotY(ROT_SCR) × RotX(-ROT_X) × S(-s,s,s) × T(POS) →
 *     emit with engine mesh-record &DAT_073a96f8.
 *     Then nested 4-iter spoke loop emits with engine mesh-record
 *     &DAT_073a9720 per spoke (per-spoke Translation × outer).
 *
 * All other types fall through.  Per-emit mesh-record overrides
 * (0x73a96a8 / f8 / 0x73a9720) are engine static slots populated by
 * code we haven't ported yet (same DUNGEON-loaded shape as Pass D's
 * &DAT_073a9680); HOUSE leaves them BSS-zero so all emits short-
 * circuit inside scene1_emit_record (mesh == NULL fast-path).  We
 * pass NULL today; a future chip can wire `--force-pass-b-{main,
 * outer,spoke}-mesh` setters analogous to Pass D's.
 *
 * Doubly dormant in HOUSE: g_scene1_records_b_count is 0 (no
 * populated records), AND the type filter would skip every C8j
 * allocator type anyway (allocators top out at 0xa6; Pass B needs
 * 0x8c/0x9b/0x9c which are populated by the unported FUN_0043ae20
 * table B tick).
 */
static void sw_pass_b(IDirect3DDevice8 *dev)
{
    int count = sw_pass_bc_count();
    if (count == 0) return;
    if (count > SCENE1_RECORDS_B_COUNT) count = SCENE1_RECORDS_B_COUNT;

    for (int i = 0; i < count; i++) {
        const int32_t *slot =
            &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];

        int32_t type = slot[SCENE1_RECORDS_B_OFF_TYPE];
        if (type == 0) continue;  /* engine: `cmp eax, ebx (=0); je skip` */

        if (sw_pass_b_should_emit_main(slot)) {
            float world[16];
            sw_pass_b_compose_world_main(world, slot);
            IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                          (const D3DMATRIX *)world);
            /* Engine: FUN_00455191(&DAT_073a96a8) — main body mesh. */
            scene1_emit_record((struct IDirect3DDevice8 *)dev, NULL);
        } else if (sw_pass_b_should_emit_outer(slot)) {
            float outer[16];
            sw_pass_b_compose_world_outer(outer, slot);
            IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                          (const D3DMATRIX *)outer);
            /* Engine: FUN_00455191(&DAT_073a96f8) — outer body mesh. */
            scene1_emit_record((struct IDirect3DDevice8 *)dev, NULL);

            /* 4-iter spoke loop @ engine 0x45583c..0x4559af. */
            for (int spoke_idx = 0; spoke_idx < 4; spoke_idx++) {
                float spoke_world[16];
                sw_pass_b_compose_world_spoke(spoke_world, outer, slot,
                                              spoke_idx);
                IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                              (const D3DMATRIX *)spoke_world);
                /* Engine: FUN_00455191(&DAT_073a9720) — spoke mesh. */
                scene1_emit_record((struct IDirect3DDevice8 *)dev, NULL);
            }
        }
    }
}

/* Pass C walks g_scene1_records_b at the slot[0] base (DAT_069324b0
 * in engine) — different bias than Pass B (slot[42] base) but the
 * same underlying table.  Type filter via a 5-case cardinal-int
 * cascade (asm `cmp eax, K`; Ghidra's float-as-int reinterp in the
 * decomp produced misleading raw-bits comments):
 *
 *   TYPE ∈ {0x23, 0x2c, 0x2b}: emit iff PART_IDX % 2 == 0
 *   TYPE ∈ {0x56, 0x96}: always emit
 *   other: skip
 *
 * Per-record matrix chain: MATRIX0 × RotY(ROT_SCR) × S(-s,s,s) ×
 * T(POS) where s = LIFE_MULT * 0.2f.  Emits via FUN_00455191(
 * &DAT_073a9680) — the SAME mesh-record slot as Pass D
 * (train_iwa.x, DUNGEON-loaded only); HOUSE leaves it NULL so
 * emit short-circuits.
 *
 * UNLIKE Pass B, Pass C IS smoke-fireable on types populated by
 * landed C8j allocators (0x23 entity matrix-init, 0x56 NPC
 * matrix-init, 0x96 NPC player-aim, 0x2b NPC owner+0x420 family).
 * With --force-b-* flags + --force-pass-d-mesh, Pass C should
 * draw geometry once smoke + mesh + camera all line up.
 */
static void sw_pass_c(IDirect3DDevice8 *dev)
{
    int count = sw_pass_bc_count();
    if (count == 0) return;
    if (count > SCENE1_RECORDS_B_COUNT) count = SCENE1_RECORDS_B_COUNT;

    for (int i = 0; i < count; i++) {
        const int32_t *slot =
            &g_scene1_records_b[i * SCENE1_RECORDS_B_STRIDE];

        if (!sw_pass_c_should_emit(slot)) continue;

        float world[16];
        sw_pass_c_compose_world(world, slot);
        IDirect3DDevice8_SetTransform(dev, D3DTS_WORLD,
                                      (const D3DMATRIX *)world);

        /* Engine: FUN_00455191(&DAT_073a9680) — same mesh-record slot
         * as Pass D.  Sharing the slot means `--force-pass-d-mesh`
         * also feeds Pass C. */
        scene1_emit_record((struct IDirect3DDevice8 *)dev,
                           scene1_shop_walker_get_pass_d_mesh());
    }
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
 * gate cascade — ACTIVE != 0, type-enable table flag == 1, STATUS_F
 * == 0xff) calls FUN_00456d48(slot - 0x109).  FUN_00456d48 is a 526-
 * byte scene-tree dispatcher we haven't ported; the walker dispatches
 * through sw_pass_f_fire_emit which routes to a host-installable
 * hook (default no-op).
 *
 * Dormant in HOUSE: count_stub returns 0; even with records populated
 * the type-enable table at DAT_005c2410 is BSS-zero so every record
 * fails the second gate. */
static void sw_pass_f(IDirect3DDevice8 *dev)
{
    (void)dev;  /* Pass F doesn't write SetTransform itself — the
                 * scene-tree dispatcher inside FUN_00456d48 does. */
    int count = sw_pass_af_count();
    if (count == 0) return;

    for (int i = 0; i < count; i++) {
        const int32_t *slot = sw_pass_af_slot(i);
        if (!slot) continue;

        int32_t type = slot[SCENE1_RECORDS_SHOP_OFF_TYPE];
        int type_enabled = sw_pass_f_type_enabled(type);
        if (!sw_pass_f_should_emit(slot, type_enabled)) continue;

        /* Engine: FUN_00456d48(slot - 0x109).  The -0x109 dw offset
         * (= -0x424 bytes) points at the scene-tree record header
         * embedded in the parent shop record.  HOUSE: no-op default
         * since FUN_00456d48 isn't ported. */
        sw_pass_f_fire_emit(slot - 0x109);
    }
}

/* ─── player/companion billboard draw (engine FUN_004552d0 L357-454) ─────
 * MISLABEL CORRECTED 2026-05-30: this section was read as a "light pass"
 * (DAT_056da1cc taken for a per-light tex slot).  It is the **visible
 * standing player + companion billboard draw** — the leaf call at
 * 4552d0.c:449 `FUN_0045a56f(&DAT_056daae8 + i*0xb, char, char, world,
 * 0xff808080)`, preceded by `SetTexture(0, DAT_073a9b18[char])`
 * (objdump 0x45649f→0x4564d4).  Ground-truthed via runs/cchr2b leaf
 * capture: at HOUSE frame 17544 the player (char 0) + companion (char 1)
 * are drawn ONLY from here at color 0xff808080, NOT from the FUN_00456f56
 * chr-walker (whose blue 0x7f7fff player path is situational).
 *
 * Per actor i (0=player, 1=companion):
 *   world = DAT_0438cdf8(billboard base) × Scaling(scale_f·dae18,
 *           scale_f·dae24, scale_f·dae18) × Translation(DAT_056da1d8[i*3]);
 *   scale_f = fade·0.03, fade = (0x5a − DAT_0438b4b4)/30 clamp 1.0;
 *   the leaf reads anim/frame/facing/flags/shimmer from DAT_056daae8[i*0xb].
 * The per-channel colour base (4552d0.c:394 __ftol → 0x80) + the player's
 * idle/damage sin-pulse modulation (L397-435) are deferred — an idle
 * standing actor with no shake/damage stays 0xff808080 (capture-confirmed).
 *
 * Cchr.2h — de-MVP'd.  The per-call scene1_shop_walker_set_player_inject is
 * gone; the draw now reads the real engine-global actor model owned by
 * scene1_player_ctrl (char id DAT_056da1cc[i], scale DAT_056dae18[i] /
 * DAT_056dae24[i], the DAT_056daae8 sprite-state record) + the player
 * position g_scene1_player_pos (DAT_056da1d8).  scene1_postload_pose_house_
 * standing() seeds actor 0 on HOUSE entry; when FUN_0048b850 (Cpop) lands
 * it becomes the live per-frame writer of the same globals and this loop is
 * unchanged.  Companion (actor i>0) awaits the DAT_056da1d0 char id +
 * DAT_056da1e4.. position slots; the colour base/pulse (L394-435) is still
 * deferred (idle standing = 0xff808080, capture-confirmed). */
static void sw_pass_light(IDirect3DDevice8 *dev)
{
    if (sw_light_pass_gate() != 0) return;         /* DAT_0438b8bc == 0 */

    if (sw_palette_lighting_enabled() != 0) {
        /* L359-363: maplight enable (HOUSE: palette+0x1ae0 == 0 → skipped).
         * SetLight args dropped in decomp; the DAT_06a49a40 D3DLIGHT8 scratch
         * isn't ported, so this stays a bare enable (no effect for HOUSE). */
        IDirect3DDevice8_LightEnable(dev, 0, TRUE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, TRUE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_AMBIENT,  0xff000000u);
    }

    /* L364-367: actor count.  The engine DEFAULT is 3 (the player + 2 companion
     * slots) — `local_14 = 4.2039e-45`, i.e. the float bit-pattern of int 3,
     * used as the loop bound (engine all.c L51980 `do … while(i != local_14)`).
     * The DAT_0438b1a0 (config `easydisp`) party-render override only RECOMPUTES
     * it (3 − 2·(stage_record0!=0) → 1 or 3).  An earlier MVP wrongly defaulted
     * to 1 (player-only); restoring the engine default lets the live companion
     * (actor 2, the bobbing fairy — char id 1) draw.  Per-slot the loop still
     * gates on char != -1 && scale > 0, so the free-roam-disabled actor 1
     * (char -1) is skipped (engine-quirks §71). */
    int actor_count = 3;
    if (sw_dat_0438b1a0() == 1) {
        actor_count = (sw_stage_record0() != 0) ? 1 : 3;
    }

    /* fade = (0x5a − DAT_0438b4b4)/30 clamp 1.0; the fade counter is stubbed
     * 0 (no spawn-in animation ported) → fade 1.0. */
    float fade = (float)(0x5a - 0) / 30.0f;
    if (fade > 1.0f) fade = 1.0f;
    float scale_f = fade * 0.03f;

    /* Character sprites use POINT (nearest) filtering — sharp pixel-art, no
     * bilinear smear (the sw_pass top set LINEAR for the 3D meshes; the
     * engine flips to POINT here for the billboards).  Engine FUN_004552d0
     * @ 0x456055/0x456067: SetTextureStageState(0, MAG/MINFILTER, POINT)
     * just before the actor-draw loop — objdump-confirmed, retail d3d-trace
     * shows the prim-12 chr leaf draws at (POINT,POINT,NONE) vs the 3D
     * meshes' (LINEAR,LINEAR,LINEAR).  Restored to LINEAR after the loop so
     * the trailing passes (sw_pass_g etc.) keep the bilinear mesh filter. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_POINT);

    /* Player/companion sprites WRITE depth so later z-tested billboards (the
     * foot-dust records-A 0xe and the wing-glow) read as BEHIND the walker.
     * Retail GT (d3d full-state extract at the FUN_0045a56f draw 0x45aa31,
     * runs/walkdust-d3d frame 5495): ZENABLE=1, ZWRITEENABLE=1, ALPHATEST
     * ref 0 **GREATER** (AFUNC=5 = D3DCMP_GREATER, NOT GreaterEqual), blend
     * SRCALPHA/INVSRCALPHA.
     *
     * The GREATER (alpha > 0) test is load-bearing: only OPAQUE texels write Z,
     * so just the character's silhouette lays down a Z footprint.  A previous
     * attempt (b1acf7c) used GREATEREQUAL (ref 0 → passes EVERY texel) so the
     * whole TRANSPARENT quad wrote Z — an invisible occluding rectangle that
     * (a) killed Tear's wing-glow (the glow extends into the quad's transparent
     * border, which was writing Z at the actor depth) and (b) punched a
     * rectangular hole in the dust/shadow around Recette.  That regression was
     * reverted (957af8c).  The same square-cutout class was independently
     * re-confirmed on the bg-NPC sprites this session (2c96b97): GREATEREQUAL
     * cuts squares, GREATER does not — see docs/findings/merchant-hud-character-zorder.md
     * + project_bg_npc_and_sprite_zorder.  Restore ZWRITE off after the loop so
     * trailing transparent passes keep their inherited state. */
#ifndef OPENRECET_NO_CHAR_ZWRITE
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE,         TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE,    TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHATESTENABLE, TRUE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF,        0);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAFUNC,       D3DCMP_GREATER);
#endif

    for (int i = 0; i < actor_count; i++) {
        int   char_id  = player_ctrl_actor_char(i);
        float scale_xz = player_ctrl_actor_scale_xz(i);
        float scale_y  = player_ctrl_actor_scale_y(i);

        /* L443 gate: dae18[i] > 0 && dae24[i] > 0 && DAT_056da1cc[i] != -1. */
        if (char_id == -1 || scale_xz <= 0.0f || scale_y <= 0.0f)
            continue;

        const int32_t *actor = player_ctrl_actor_record(i);
        if (actor == NULL)
            continue;

        /* Actor position = the contiguous engine block (&DAT_056da1d8)[i*3]:
         * slot 0 = player, slot 2 = the companion fairy (driven by
         * scene1_companion_ctrl_tick).  Slot 1 (guest) is gated out above by
         * its char id == -1 at free-roam (see player_ctrl_pose_house_standing). */
        float px = g_scene1_actor_pos[i][0];
        float py = g_scene1_actor_pos[i][1];
        float pz = g_scene1_actor_pos[i][2];

        const sprite_t *sheet = scene1_preload_chr_sheet(char_id);
        if (sheet == NULL || sheet->tex == NULL)
            continue;                              /* no sheet → skip this actor */

        float world[16], scale[16], tmp[16];
        mat4_translation(tmp, px, py, pz);
        mat4_scaling(scale, scale_f * scale_xz, scale_f * scale_y,
                     scale_f * scale_xz);
        mat4_mul(tmp, scale, tmp);                 /* scale × translation */
        mat4_mul(world, g_scene1_camera_orient, tmp);  /* base × (scale×T) */

        /* L448: bind sheet (engine SetTexture(0, DAT_073a9b18[char])). */
        IDirect3DDevice8_SetTexture(dev, 0, (IDirect3DBaseTexture8 *)sheet->tex);

        /* L449: colour 0xff808080 (opaque neutral; pulse modulation deferred). */
        scene1_chr_sprite_render((struct IDirect3DDevice8 *)dev, actor,
                                 char_id, world, 0xff808080u,
                                 (int)sheet->width, (int)sheet->height);
    }

    /* Restore LINEAR for the trailing mesh/sprite passes (engine re-asserts
     * bilinear after the character draws). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);

    /* Stop writing depth again — the player's Z footprint stays in the buffer
     * to occlude the later dust/glow billboards, but trailing transparent
     * passes must not lay down new Z (matches the inherited ZWRITE=0 state). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
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
    /* E.2 probe — the WIDE-frustum shop walker FUN_004552d0 @ 0x4552d0
     * (owns the standing player/companion billboard draw in sw_pass_light). */
    CALL_TRACE_ENTER(0x4552d0u);

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

    /* ─── L457: between-pass sweep ─────────────────────────────────── */
    sw_pass_between_TODO();                 /* FUN_004705a3 (DAT_073a6ea8, dormant) */
    /* FUN_0046f737: the background-window NPC bright character billboards (the
     * townsfolk drifting past the back window; their dark shadows draw earlier
     * in the shadow pass).  Was a hidden stub here until ported. */
    scene1_bg_npc_sprite_render(dev_in);
    /* FUN_00470d44 (talk-flag NPC overlay, DAT_0450f470) still TODO. */

    /* ─── L460-L514: Pass G ────────────────────────────────────────── */
    sw_pass_g(dev);

    /* ─── L515: ZFUNC reset ────────────────────────────────────────── */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);

    /* ─── L516: tail combiner mode ─────────────────────────────────── */
    scene1_render_apply_palette_combiner_mode((struct IDirect3DDevice8 *)dev, 3);
}

#endif /* _WIN32 */
