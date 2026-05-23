/*
 * scene1_wide_followup.c — see scene1_wide_followup.h for the chip
 * writeup.
 *
 * C8f.1 port of FUN_004161c7: the WIDE-frustum followup walker called
 * by scene1_render_meshes (FUN_00459dfd L219, right after the C8c shop
 * walker).  Structure ported line-by-line from
 * docs/decompiled/by-address/4161c7.c with per-record draw helpers
 * stubbed for chip-sized follow-ups (Pass F delegates to the existing
 * scene1_pass_f module).
 */

#include "scene1_wide_followup.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>

#include "scene1_pass_f.h"   /* Pass F is already ported (C8g.2 MVP) */
#include "scene1_records.h"  /* per-pass active counts */
#include "scene1_render.h"   /* scene1_render_push_projection */

/* ─── engine scratch globals — module-local mirrors ────────────────────
 *
 * Same convention as scene1_shop_walker.c: writes go through named
 * statics so a future reader can grep the _DAT_ symbol back to the
 * engine address.  No consumers ported yet.
 */

/* _DAT_0076b95c — "last bound texture" cache slot.  Written at the top
 * of each per-pass body when the desired texture differs.  Reset to 0
 * at function exit (engine L482). */
static uintptr_t g_tex_cache_last = 0;

/* ─── TODO accessors for not-yet-ported state ──────────────────────────
 *
 * Each engine global the walker reads routes through a named accessor
 * returning the BSS-zero default.  Loops gated by 0-returning accessors
 * are dormant in HOUSE.
 */

/* DAT_0076b964 — Pass A/B/E record count.  Computed by
 * scene1_records_counter_scan (C8g.1, FUN_00459dfd L71-L77); reads 0
 * until the per-stage record populator lands. */
static int wf_pass_abe_count(void) { return g_scene1_records_b_count; }

/* DAT_0076b968 — Pass C/D record count.  Computed by
 * scene1_records_counter_scan; reads 0 until the populator lands. */
static int wf_pass_cd_count(void) { return g_scene1_records_c_count; }

/* DAT_0438b1e0 — stage-record selector, multiplied by 0x2dfc8 to index
 * DAT_044f7030 for the mid block 2 cell walk.  BSS-zero → 0 (which
 * selects the first stage record). */
static int wf_stage_selector(void) { return 0; }

/* DAT_0438b1c0 — mid block 2 outer gate.  BSS-zero → 0 → cell walk
 * dormant (gate requires == 1). */
static int wf_mid2_outer_gate(void) { return 0; }

/* *DAT_068dd2f0 + 0 (int) — stage palette field 0.  BSS-zero → 0 →
 * mid block 2 inner gate satisfied (requires == 0), but the outer
 * gate is the dominant filter. */
static int wf_palette_field_0(void) { return 0; }

/* ─── per-pass stub helpers ────────────────────────────────────────────
 *
 * Each pass body has the same shape: walk a count-bounded table,
 * filter on type, compose a per-record world matrix, populate a
 * shared vbuf, call DrawPrimitiveUP.  All five (A, B, C, D, E)
 * remain TODO stubs in this chip because their tables are BSS-zero
 * in HOUSE (the engine's wide-followup data populator for tables B
 * and C is unported — see docs/findings/scene1-particles-tick.md).
 */

/* Pass A — DAT_06932548 table, stride 0x49 dwords.  Type filter on
 * fVar2 raw-bits {0x77 (1.66755e-43), 0xa2 (2.2701e-43)}.  Texture:
 * DAT_073cc8e0.  128-tex atlas UV (0.0078125 / 0.9921875). */
static void wf_pass_a(IDirect3DDevice8 *dev)
{
    int count = wf_pass_abe_count();
    if (count == 0) return;
    /* TODO C8f-followup: walk DAT_06932548 stride 0x49 dwords;
     * for each record with raw-bit type in {0x77, 0xa2}:
     *
     *   1. Bind texture DAT_073cc8e0 (cache via g_tex_cache_last).
     *   2. Per-record scale = pIVar11[0x1c] * 0.005f, clamped:
     *        if ((int)pIVar11[0] < 5) scale = (int_type) * scale / 5.0
     *      (engine treats *r as int for the clamp).
     *   3. World matrix:
     *        T(r[-0xf], r[-0xe], r[-0xd])
     *        × S(scale, scale, scale)
     *        × RotZ(π/2)  (raw 0x3fc90fdb = 1.5707964)
     *        × RotY(π - r[-2])
     *   4. Fill DAT_0064bf68 vbuf — 4 vertices × 6 floats:
     *        diffuse = 0xffffffff per vertex
     *        UV box corners = 0.0078125 / 0.9921875 (1/128 texel inset)
     *   5. SetTransform(D3DTS_WORLD, M) + DrawPrimitiveUP. */
    (void)dev;
    (void)count;
}

/* Pass B — DAT_06932514 table, stride 0x49.  Type filter on
 * fVar2 raw-bits {0x53 (1.16308e-43)}.  Texture: DAT_073d8620.
 * 256-tex atlas UV (0.00390625 / 0.99609375). */
static void wf_pass_b(IDirect3DDevice8 *dev)
{
    int count = wf_pass_abe_count();
    if (count == 0) return;
    /* TODO C8f-followup: walk DAT_06932514 stride 0x49 dwords;
     * for each record with raw-bit type == 0x53:
     *
     *   1. Bind texture DAT_073d8620.
     *   2. Per-record scale = pIVar11[0x29] (read as float).
     *   3. World matrix:
     *        T(r[-2], r[-1], r[0])
     *        × S(scale, scale, scale)
     *        × RotZ(π/2)
     *      (NB: no RotY — passive billboard.  Engine omits L70
     *      RotY chain that Pass A has.)
     *   4. Fill DAT_0064bf68 vbuf — UV box corners
     *      0.00390625 / 0.99609375 (1/256 texel inset).
     *   5. DrawPrimitiveUP. */
    (void)dev;
    (void)count;
}

/* Pass C — DAT_06956cd8 table, stride 0x25.  Type filter on fVar2
 * cardinal-int reading {0, 1, 2, 3}.  Tile selector mixes type into
 * the index: `tile = (r[1]/3) % 7 + type_offset`, where type_offset
 * is {0, 8, 16, 24} for types {0, 1, 2, 3}.  256×512 atlas (64-tile
 * cells, 8 wide × 8 tall).  Texture: DAT_073cc930.  vbuf:
 * DAT_0064e5d8. */
static void wf_pass_c(IDirect3DDevice8 *dev)
{
    int count = wf_pass_cd_count();
    if (count == 0) return;
    /* TODO C8f-followup: walk DAT_06956cd8 stride 0x25 dwords;
     * for each record with cardinal type in {0, 1, 2, 3}:
     *
     *   1. Bind texture DAT_073cc930.
     *   2. Per-type scale:
     *        type==1: scale = 0.0096
     *        type==2: scale = 0.0288  (engine literal 0.028800001)
     *        else:    scale = 0.0192
     *      (read from r[7] (cardinal-float)).
     *   3. World matrix:
     *        T(r[-10], r[-9], r[-8])
     *        × S(scale, scale, scale)
     *        × DAT_0438cdf8  (precomputed shared 4×4, BSS-zero today)
     *   4. Tile = (r[1]/3) % 7 + type_offset[type].
     *      Per-axis: col = tile % 8, row = tile / 8.
     *      UV box: u in (col*64+0.5)/512 .. (col*64+63.5)/512
     *              v in (row*64+0.5)/256 .. (row*64+63.0)/256
     *   5. Fill DAT_0064e5d8 vbuf, diffuse = 0xff000000 (opaque
     *      black — engine quirk: vertex color zeros out emissive but
     *      keeps alpha for the alpha test).
     *   6. DrawPrimitiveUP. */
    (void)dev;
    (void)count;
}

/* Mid block 2 — projection swap to z_far=350 + conditional 15×20
 * cell walk + projection back to z_far=2000.  Cell walk gated on
 * (DAT_0438b1c0 == 1) && (palette+0 == 0).
 *
 * Engine L204-L223:
 *
 *   z_far = 350  (raw 0x43af0000 = 350.0)
 *   if (DAT_0438b1c0 == 1 && *DAT_068dd2f0 == 0) {
 *     piVar11 = &DAT_044f7030 + DAT_0438b1e0 * 0x2dfc8;
 *     for (i = 0; i < 15; i++) {
 *       for (j = 0; j < 20; j++) {
 *         if (*piVar11 != -1) FUN_00415fab();
 *         piVar11++;
 *       }
 *     }
 *     FUN_00485f8c();
 *   }
 *   z_far = 2000 (raw 0x44fa0000 = 2000.0)
 *
 * For HOUSE: outer gate fails (DAT_0438b1c0 BSS-zero != 1) so the
 * cell walk is dormant.  Projection swaps still execute. */
static void wf_mid_block_2(IDirect3DDevice8 *dev)
{
    /* Projection to z_far = 350 (the engine's narrow z_far). */
    scene1_render_push_projection((struct IDirect3DDevice8 *)dev, 350.0f);

    /* Engine L37 (top of FUN_004161c7) computes the per-stage base
     * offset unconditionally — used here in the cell walk.  We compute
     * it lazily inside the dormant gate; the value is BSS-zero today
     * so it's a no-op either way. */
    int stage_base_offset = wf_stage_selector() * 0x2dfc8;
    (void)stage_base_offset;

    if (wf_mid2_outer_gate() == 1 && wf_palette_field_0() == 0) {
        /* TODO C8f-followup: port the 15×20 cell walk.
         *
         *   1. Compute base pointer = DAT_044f7030 + stage_sel * 0x2dfc8.
         *   2. For each cell (15 rows × 20 cols, stride 1 dword):
         *        if (*cell != -1) FUN_00415fab();
         *   3. After the loop: FUN_00485f8c().
         *
         * Both FUN_00415fab and FUN_00485f8c are unported — they
         * surface as their own chips once the stage record at
         * DAT_044f7030 ports.  Today the outer gate keeps the loop
         * dormant. */
    }

    /* Projection back to z_far = 2000 (wide). */
    scene1_render_push_projection((struct IDirect3DDevice8 *)dev, 2000.0f);
}

/* Pass D — DAT_06956cd8 (same table as Pass C!), stride 0x25.
 * Type filter `*r > 6` (cardinal int > 6).  Per-record alpha fade
 * + per-record texture lookup.  vbuf: DAT_0064e5d8 (shared with C). */
static void wf_pass_d(IDirect3DDevice8 *dev)
{
    int count = wf_pass_cd_count();
    if (count == 0) return;
    /* TODO C8f-followup: walk DAT_06956cd8 stride 0x25 dwords;
     * for each record with cardinal type > 6:
     *
     *   1. Translation(r[-10], r[-9], r[-8]).
     *   2. If slot_index == DAT_056dae40 (pulsing-selected slot):
     *        angle = (int)r[1] * 0.3
     *        flash = sin(angle)              [argless cosf-like dropout]
     *        rgb_lo = ftol(flash * SCALE)    [scale dropped in decomp]
     *        scale = 0.026880002 (slightly larger than the normal 0.0192)
     *      else: rgb_lo = 0, scale = 0.0192.
     *   3. Alpha: if r[6] (cardinal-float) == 2 (= 2.8026e-45 raw):
     *        alpha = clamp(((int)r[1] - 0x1e) * 0x20, 0, 0xff) when
     *                (int)r[1] > 0x1e, else 0
     *      else alpha = 0xff.
     *   4. Per-vertex diffuse = (alpha << 24) | (rgb_lo << 16) |
     *                           (rgb_lo << 8)  | rgb_lo.
     *   5. Per-record texture lookup:
     *        iVar8 = FUN_004681f6((int)*r - 7)
     *        tex   = *(int *)(DAT_073d8778 + DAT_095d3808[iVar8 * 0xb3] * 0x10)
     *        (bind via g_tex_cache_last guard)
     *   6. Tile from same table entry:
     *        tile_raw  = DAT_095d380c[iVar8 * 0xb3]
     *        force0    = r[4] != 0 → tile_raw = 0
     *        tex_width = DAT_073d8780[iVar8 * 0xb3 * 4]
     *        col = tile_raw % 8
     *        row = tile_raw / 8
     *        u in (col*32+0.5)/256 .. (col*32+31.5)/256
     *        v in (row*32+0.5)/tex_width .. (row*32+31)/tex_width
     *   7. Fill vbuf + S(scale, scale, scale) × DAT_0438cdf8 chain.
     *   8. DrawPrimitiveUP.
     *
     * The argless cosf/sinf-like dropout at engine L236 + the __ftol
     * source at L238 are both Ghidra-dropped FPU args — port verbatim
     * with placeholder constants and surface in pending-human-check. */
    (void)dev;
    (void)count;
}

/* Pass E — DAT_069324b0 table, stride 0x49.  Two type groups:
 *   {0x71, 0x72, 0x75}            — "spear" (cardinal-int compare)
 *   {0x73, 0x7e, 0x78, 0xa0, 0x7a} — "fan"
 * Shared texture: DAT_073cc940 (bound at L289-292 before this loop).
 * vbuf: DAT_0064bf68 (shared with A/B).  Most-complex pass — full
 * port deferred. */
static void wf_pass_e(IDirect3DDevice8 *dev)
{
    int count = wf_pass_abe_count();
    if (count == 0) return;
    /* TODO C8f-followup: walk DAT_069324b0 stride 0x49 dwords;
     * dispatch on cardinal int type:
     *
     *   Spear group {0x71, 0x72, 0x75}:
     *     scale = r[0x42] * 0.005 (clamped by r[0x26] < 5 as in Pass A)
     *     type==0x72 → scale *= 0.8
     *     M = T(r[0x17..0x19]) × S × DAT_0438cdf8 × RotY(π - r[0x24])
     *     atlas tile: per-type column/row constants:
     *       0x71: col=128, row=192    (default)
     *       0x72: col=192, row depends on (r[0x26] % 4 < 2): 128 or 192
     *       0x75: col=192, row=0
     *       0x77: col=192, row=64     (NB: 0x77 not in this group!)
     *     atlas UVs (col + 0.5)/256 / (col + 63.5)/256 etc.
     *
     *   Fan group {0x73, 0x7e, 0x78, 0xa0, 0x7a}, gated on
     *     r[0x26] >= 0:
     *     scale = r[0x42] * 0.004
     *     M = FUN_00415f2e(r, ...)  (camera-billboard helper, unported)
     *     M × RotZ(π/2)
     *     per-type S:
     *       0x78, 0xa0: S(s, 2s, 2s)
     *       0x7a:       S(1.2s, 2*1.2s, 2*1.2s)
     *       else:       S(s, s, s)
     *     UVs:
     *       0x7e: 5-frame anim tile based on (local_18 % 5)
     *       0x78/0xa0/0x7a: fixed tile 96.5/127.5/128.5/159.5
     *       else: fixed tile 96.5/127.5/160.5/175.5
     *
     *   All other types fall through (goto LAB_00417271).
     *
     * The fan group's FUN_00415f2e is a sibling unport — likely the
     * camera-aligned-billboard matrix helper.  Defer to its own chip. */
    (void)dev;
    (void)count;
}

/* ─── public entry ─────────────────────────────────────────────────── */

void scene1_wide_followup(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* ─── L37: texture-cache reset ─────────────────────────────────── */
    g_tex_cache_last = 0;

    /* ─── L38-L49: top render-state preamble (Pass A/B prelude) ────── */

    /* L38-L39: TSS COLORARG2 + COLORARG1 = D3DTA_TEXTURE.  Pass A and
     * B pull both color args from the bound texture (vertex diffuse
     * is white per-vertex; the texture provides the color). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);

    /* L40: ZENABLE = TRUE.  Depth test on. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);

    /* L41-L42: ZWRITEENABLE toggled OFF then ON.  The double-write is
     * engine-verbatim — likely a macro/state-block expansion that
     * computes both polarities.  The final ON value is what sticks. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);

    /* L43: ALPHABLENDENABLE = TRUE. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);

    /* L44: ALPHAREF = 0.  Any alpha > 0 passes the alpha test (set up
     * by C8a's scene1_render_meshes preamble — ALPHATESTENABLE is
     * already on from there). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);

    /* L45-L46: SRCBLEND = SRCALPHA (5), DESTBLEND = INVSRCCOLOR (4).
     * Engine quirk — INVSRCCOLOR (not INVSRCALPHA) as the dest is
     * the engine's signature for these "color-keyed" 2D billboards.
     * Reproduced verbatim. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);

    /* L47: TSS COLOROP = 4 (MODULATE2X — texture × diffuse × 2).
     * Same as the shop walker tail's pass G setup. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

    /* ─── L50-L92: Pass A ──────────────────────────────────────────── */
    wf_pass_a(dev);

    /* ─── L93-L127: Pass B ─────────────────────────────────────────── */
    wf_pass_b(dev);

    /* ─── L128-L142: mid block 1 ───────────────────────────────────── */

    /* L128: FOGENABLE = FALSE (raw RS 0x1c = D3DRS_FOGENABLE). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* L129: ALPHABLENDENABLE = TRUE (re-asserted). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);

    /* L130: ALPHAREF = 0 (re-asserted). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ALPHAREF, 0);

    /* L131: CULLMODE = 1 (D3DCULL_NONE) — 2D quads don't cull.
     * (raw RS 0x16 = D3DRS_CULLMODE.) */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* L132-L133: TSS COLORARG2 + COLORARG1 = D3DTA_TEXTURE (re-asserted). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);

    /* L134: TSS COLOROP = 4 (MODULATE2X — re-asserted). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

    /* L135: ZWRITEENABLE = TRUE (re-asserted). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, TRUE);

    /* L136: TSS MAGFILTER = 5 (D3DTEXF_LINEAR).  Engine raw 5; the D3D8
     * enum has LINEAR = 2, but value 5 is GAUSSIANQUAD which the
     * runtime tolerates for stage 0 as LINEAR on most drivers.  Engine
     * writes 5 — kept verbatim.  (raw TSS 0x13 = D3DTSS_MAGFILTER.) */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, 5);

    /* L137: TSS MINFILTER = 6 (D3DTEXF_ANISOTROPIC).  Engine raw 6;
     * D3D8 enum has ANISOTROPIC = 3.  Engine writes 6 — kept
     * verbatim. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, 6);

    /* L138: LightEnable(0, FALSE). */
    IDirect3DDevice8_LightEnable(dev, 0, FALSE);

    /* L139: RS 0x89 = D3DRS_LIGHTING = FALSE.  (Now confirmed to map
     * to the LIGHTING render state — same as the shop walker's L491.) */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_LIGHTING, FALSE);

    /* L140: SetVertexShader(0x142 = D3DFVF_XYZRHW|DIFFUSE|TEX1).
     * Mid block 1 transitions to the 2D RHW path that FUN_00414ee2
     * consumes — same FVF the C7h overlay brackets use. */
    IDirect3DDevice8_SetVertexShader(dev, 0x142u);

    /* L141: FUN_00414ee2(1, 0) — 2D overlay layer 1 dispatcher.
     *
     * Same 4006-byte function the C7h scene1_render_overlay brackets
     * call, but with the second arg = 0 (vs 1 from C7h).  Likely the
     * "additive blend mode" path vs the "alpha blend mode" path.
     * Stubbed — full port is its own chip. */
    /* TODO C8f-followup: port FUN_00414ee2 (4006 B).  Shared with C7h
     * scene1_render_overlay's four call sites.  When that chip lands,
     * call it here with (layer=1, mode=0). */

    /* L142: TSS COLOROP = 7 (MODULATEALPHA_ADDCOLOR — α × col1 + col2).
     * Engine sets this AFTER FUN_00414ee2 — so Pass C inherits it. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP,
                                          D3DTOP_MODULATEALPHA_ADDCOLOR);

    /* ─── L143-L203: Pass C ────────────────────────────────────────── */
    wf_pass_c(dev);

    /* ─── L204-L223: mid block 2 ───────────────────────────────────── */
    wf_mid_block_2(dev);

    /* ─── L224-L287: Pass D ────────────────────────────────────────── */
    wf_pass_d(dev);

    /* ─── L288-L292: pre-Pass-E state block ────────────────────────── */

    /* L288: TSS COLOROP = 4 (MODULATE2X) — back from Pass C/D's
     * MODULATEALPHA_ADDCOLOR. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

    /* L289-L292: bind Pass E shared texture DAT_073cc940 via cache. */
    /* TODO C8f-followup: once DAT_073cc940 is exposed as an engine
     * texture symbol (it lives in the system asset loader's texture
     * cache), bind it here:
     *
     *   if (g_tex_cache_last != (uintptr_t)DAT_073cc940) {
     *     g_tex_cache_last = (uintptr_t)DAT_073cc940;
     *     SetTexture(0, DAT_073cc940);
     *   }
     *
     * For now Pass E's loop is dormant anyway. */

    /* ─── L293-L416: Pass E ────────────────────────────────────────── */
    wf_pass_e(dev);

    /* ─── L417-L422: pre-Pass-F state block ────────────────────────── */

    /* L417: RS NORMALIZENORMALS = FALSE.
     * (raw RS 7 — wait, raw 7 = ZENABLE.  Engine line is
     * `SetRenderState(7, 0)` which IS ZENABLE = FALSE.  Pass F is
     * un-depth-tested.) */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, FALSE);

    /* L418: RS 0xe = ZWRITEENABLE = FALSE. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);

    /* L419-L422: bind Pass F texture DAT_073cc8c0 via cache.
     *
     * Pass F's existing scene1_pass_f_render binds SetTexture(0, NULL)
     * + SELECTARG1/DIFFUSE — the texture isn't sampled for the color-
     * cycle billboards.  The engine binds DAT_073cc8c0 here for
     * completeness even though MODULATE2X against vertex-only color
     * means the texture sample is unused.  We skip the engine-side
     * cache update; pass_f's own state writes take precedence. */
    /* TODO C8f-followup: align scene1_pass_f's state setup with the
     * engine's verbatim L419-L422 once the engine texture binding
     * comes from a real symbol (DAT_073cc8c0). */

    /* ─── L423-L481: Pass F ────────────────────────────────────────── */

    /* Delegate to the existing scene1_pass_f module (C8g.2 MVP).
     * scene1_pass_f_render reapplies its own state preamble (CULLMODE
     * + LIGHTING + TextureStage configuration + SetVertexShader);
     * those writes override the L417-L418 ZENABLE=FALSE pair we just
     * set, which is fine since pass_f doesn't depend on Z either
     * way.  A follow-up chip can refactor pass_f to assume the
     * wide-followup preamble is already in place. */
    scene1_pass_f_render(dev_in);

    /* ─── L482: tail texture-cache reset ───────────────────────────── */
    g_tex_cache_last = 0;
}

#endif /* _WIN32 */
