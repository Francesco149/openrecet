/*
 * scene1_alpha_walker.c — see scene1_alpha_walker.h for the chip
 * writeup.
 *
 * C8d port of FUN_00458bdf: the alpha-pass mesh walker called by
 * scene1_render_meshes (FUN_00459dfd L247).  Structure ported
 * line-by-line from docs/decompiled/by-address/458bdf.c with the
 * two inner FUN_00459847 calls + the FUN_00457714 pass-init helpers
 * left as upstream TODOs (in scene1_render.c — they're the same
 * stubs C8a wired).
 */

#include "scene1_alpha_walker.h"

#ifdef _WIN32

#define COBJMACROS
#define CINTERFACE
#include <d3d8.h>

#include <stdint.h>

#include "scene1_render.h"   /* scene1_render_apply_palette_combiner_mode */
#include "scene1_walker_pass_init.h"  /* scene1_walker_pass_render_house */
#include "scene1_maplight.h"  /* scene1_current_stage_record */

/* ─── engine scratch globals — module-local mirrors ──────────────────── */

/* _DAT_006051ac — same scratch flag the shop walker writes; here we
 * write 1.  No consumer ported.  Kept as a named module-local for
 * grep symmetry — the shop walker's mirror is a separate static. */
static uint32_t g_scratch_006051ac = 0;

/* ─── TODO accessors for not-yet-ported state ──────────────────────────
 *
 * Same pattern as scene1_shop_walker.c — every engine global routes
 * through a named accessor returning the BSS-zero default that keeps
 * the body's branches dormant for HOUSE.
 */

/* DAT_0438bfb0 — initial-transform record count.  Written only by
 * FUN_00436f97 (unported sibling of FUN_00474a9a).  BSS-zero → loop
 * dormant. */
static int aw_init_transform_count(void) { return 0; }

/* The 0x1a40..0x1a5c palette ints are the parsed stage.idx fields
 * (DAT_068dd2f0 indexes the parsed table — see the erratum in
 * scene1_maplight.{c,h}).  HOUSE (stage:0-1): drawcode 2, hikaridrawcode
 * 2, hikarialpha 96, hikariadd 1; water* all 0 (no water).  Read them
 * from the live record instead of the old all-zero stubs. */

/* DAT_068dd2f0 + 0x1a40 — drawcode: combiner mode for the final
 * FUN_00454f03 call inside the alpha-pass-guard branch (after pass 3). */
static int aw_palette_combiner_mode_1a40(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->drawcode : 0; }

/* DAT_068dd2f0 + 0x1a44 — waterdrawcode: combiner mode for the top-level
 * FUN_00454f03 call right after the palette-gated blend swap. */
static int aw_palette_combiner_mode_1a44(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->waterdrawcode : 0; }

/* DAT_068dd2f0 + 0x1a48 — wateralpha: TEXTUREFACTOR alpha (low byte
 * OR'd into the high byte) for the water pass. */
static int aw_palette_texture_factor_1a48(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->wateralpha : 0; }

/* DAT_068dd2f0 + 0x1a50 — wateradd: blend-mode gate (0 → SRCALPHA/
 * INVSRCALPHA "over"; non-zero → DESTCOLOR/DESTCOLOR additive). */
static int aw_palette_blend_gate_1a50(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->wateradd : 0; }

/* DAT_068dd2f0 + 0x1a54 — hikaridrawcode: combiner mode for the inner
 * alpha-pass-guard FUN_00454f03 call (before pass 3). */
static int aw_palette_combiner_mode_1a54(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->hikaridrawcode : 0; }

/* DAT_068dd2f0 + 0x1a5c — hikariadd: inner blend-mode gate before the
 * hikari pass (HOUSE = 1 → SRCBLEND=DESTCOLOR, DESTBLEND=DESTCOLOR). */
static int aw_palette_inner_blend_gate_1a5c(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->hikariadd : 0; }

/* DAT_068dd2f0 + 0x1a58 — hikarialpha: the __ftol TEXTUREFACTOR alpha
 * set right before the hikari pass (HOUSE = 96). */
static int aw_palette_hikari_alpha_1a58(void)
{ const stage_record_t *r = scene1_current_stage_record(); return r ? r->hikarialpha : 0xff; }

/* DAT_073dfcec — alpha-pass guard.  Same accessor name as the one
 * in scene1_render.c but local-scoped: zero means "render normally"
 * which keeps the inner FUN_00457714 + per-state branch live. */
static int aw_alpha_pass_guard(void) { return 0; }

/* DAT_0438b4e4 — texture combiner override.  Same as
 * scene1_combiner_override in scene1_render.c; zero by default. */
static int aw_combiner_override(void) { return 0; }

/* FUN_00457714 is now ported as scene1_walker_pass_render_house
 * (src/scene1_walker_pass_init.c, PII.3b).  The call sites below
 * dispatch directly through it; this stub used to bridge the gap
 * while the function was unported.  E.2.3 call_trace_diff surfaced
 * the divergence (retail=4, port=2) — wiring fills the last two of
 * the four engine call sites (pass-id 2 + 3, both inside FUN_00458bdf
 * = this file).  Pass-id 0 + 1 already dispatch through the
 * scene1_render.c arm. */

/* FUN_00459847 (1444 B) — additive COMBAT projectile/effect billboard
 * renderer (see the long note in scene1_render.c).  The alpha walker
 * fires it with pass 2 (non-additive) and pass 3 (additive SRC=ONE/
 * DEST=ONE).  Dormant in HOUSE — the combat projectile/effect table it
 * walks (&DAT_0695f004) is empty in the shop.  NOT the walls/floor
 * walker (that earlier guess was wrong) and NOT the source of retail's
 * ~2x HOUSE brightness.  Port is combat-scene work; verify with a
 * dungeon capture. */
static void aw_narrow_frustum_walker_TODO(int pass)
{
    /* TODO (combat scene): port FUN_00459847's projectile/effect walk. */
    (void)pass;
}

/* ─── public entry ─────────────────────────────────────────────────── */

void scene1_alpha_walker(struct IDirect3DDevice8 *dev_in)
{
    if (!dev_in) return;
    IDirect3DDevice8 *dev = (IDirect3DDevice8 *)dev_in;

    /* ─── L18-L30: initial per-instance transform builder ──────────── */

    /* For HOUSE: aw_init_transform_count() == 0 → loop dormant.
     *
     * Engine body builds Translation(record[-0x14], record[0],
     * record[0x14]) × Scaling(-1, 1, 1) per record at index i,
     * storing the resulting 4×4 matrix at local_500[i * 0x40].
     * No SetTransform happens here — the transforms are buffered
     * for FUN_00459847(2/3) to consume.
     *
     * TODO C8-followup: when DAT_0438bfb0 ports (via FUN_00436f97),
     * build the matrix array and expose it for the narrow walker
     * to read. */
    int init_count = aw_init_transform_count();
    if (init_count > 0) {
        /* TODO C8-followup: per-record Translation × neg-X-Scaling
         * matrix into a 64-entry scratch buffer.  Read by
         * FUN_00459847(2/3). */
        (void)init_count;
    }

    /* ─── L31: scratch flag ────────────────────────────────────────── */
    g_scratch_006051ac = 1;

    /* ─── L32-L44: render-state preset ─────────────────────────────── */

    /* L32: ZENABLE = TRUE (depth-test on for alpha-pass). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZENABLE, TRUE);

    /* L33: ZWRITEENABLE = FALSE (don't write to depth — alpha
     * geometry doesn't occlude itself). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);

    /* L34-L35: alpha-blend setup — SRCALPHA × INVSRCALPHA (standard
     * "over" blend). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    /* L36: TSS COLOROP = MODULATE2X (texture × vertex × 2 — brings
     * out highlights at the cost of overflow clipping). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);

    /* L37: TSS COLORARG2 = TEXTURE. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE);

    /* L38: TSS COLORARG1 = DIFFUSE (vertex color). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);

    /* L39: TSS ALPHAOP = MODULATE2X. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);

    /* L40: TSS ALPHAARG1 = TEXTURE. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);

    /* L41: TSS ALPHAARG2 = TFACTOR (read the per-frame TEXTUREFACTOR
     * set at L57-L58 below). */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);

    /* L42-L43: bilinear filtering. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);

    /* L44: fog off. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);

    /* ─── L45-L55: palette-gated blend swap ────────────────────────── */

    if (aw_palette_blend_gate_1a50() == 0) {
        /* L46-L47: standard alpha pair — DESTBLEND=INVSRCALPHA (6),
         * SRCBLEND=SRCALPHA (5). */
        IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    } else {
        /* L51-L52: additive — DESTBLEND=ONE(2), SRCBLEND=ONE(2).
         * Engine literal 2 is D3DBLEND_ONE (NOT D3DBLEND_DESTCOLOR=9 —
         * a prior decode erratum); src + dest.  Dormant in HOUSE
         * (wateradd == 0), live for additive water stages. */
        IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);
    }

    /* ─── L56: top-level combiner mode ─────────────────────────────── */
    scene1_render_apply_palette_combiner_mode((struct IDirect3DDevice8 *)dev,
                                              aw_palette_combiner_mode_1a44());

    /* ─── L57-L58: TEXTUREFACTOR from palette ──────────────────────── */

    /* Engine packs the per-channel low byte into the high byte of an
     * RGBA constant:
     *   TEXTUREFACTOR = (palette+0x1a48 << 24) | 0xffffff
     *
     * So if palette's int is 0x?? in the low byte, the high byte
     * becomes alpha (?? << 24), with the lower 24 bits forced to
     * 0xffffff (white RGB).  For HOUSE (palette+0x1a48 == 0) the
     * factor becomes 0x00ffffff — alpha = 0, RGB = white.  Effect:
     * the ALPHAARG2 = TFACTOR pulls alpha 0 from the constant. */
    {
        DWORD factor = ((DWORD)aw_palette_texture_factor_1a48() << 24)
                       | 0x00ffffffu;
        IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, factor);
    }

    /* ─── L59-L85: alpha-pass-guard branch ─────────────────────────── */

    if (aw_alpha_pass_guard() == 0) {
        /* L60-L62: combiner override gate.  If active, apply mode 2
         * (TSS COLORARG2 = D3DTA_TEMP) regardless of palette setting. */
        if (aw_combiner_override() != 0) {
            scene1_render_apply_palette_combiner_mode(
                (struct IDirect3DDevice8 *)dev, 2);
        }

        /* L63: FUN_00457714(2) — per-pass texture / shader sub-init,
         * pass-id 2.  Dispatches through PII.3b walker port. */
        scene1_walker_pass_render_house((struct IDirect3DDevice8 *)dev, 2);

        /* L64-L74: inner palette blend-mode gate (hikariadd).  Engine
         * literal 2 is D3DBLEND_ONE (NOT D3DBLEND_DESTCOLOR=9 — a prior
         * decode erratum that turned the additive god-ray blend into a
         * darkening multiply, so the rays never showed). */
        if (aw_palette_inner_blend_gate_1a5c() == 0) {
            /* Decomp L53649-53658, hikariadd == 0:
             *   SetRenderState(0x13, 5);   // SRCBLEND = SRCALPHA
             *   SetRenderState(0x14, 2);   // DESTBLEND = ONE
             * → src*srcAlpha + dest (alpha-weighted additive). */
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
        } else {
            /* Decomp L53654-53658, hikariadd != 0 (HOUSE = 1):
             *   SetRenderState(0x14, 2);   // DESTBLEND = ONE
             *   SetRenderState(0x13, 2);   // SRCBLEND  = ONE
             * → src + dest (pure additive — the god-ray glow). */
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_ONE);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_ONE);
        }

        /* L75: FUN_00454f03(palette+0x1a54) — combiner mode. */
        scene1_render_apply_palette_combiner_mode(
            (struct IDirect3DDevice8 *)dev, aw_palette_combiner_mode_1a54());

        /* L76-L79 (decomp L53662-53663): TEXTUREFACTOR alpha set from
         * __ftol(<dropped float>).  The dropped float is the hikari
         * alpha (palette+0x1a58 = hikarialpha; HOUSE = 96), truncated
         * to an int and packed into the high byte over white RGB.  A
         * per-fade scalar may still modulate it once the fade counters
         * port; for the steady state hikarialpha is the value. */
        {
            DWORD alpha = (DWORD)(aw_palette_hikari_alpha_1a58() & 0xff);
            DWORD factor = (alpha << 24) | 0x00ffffffu;
            IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, factor);
        }

        /* L80-L82: restore TSS alpha state — may have been reset by
         * the FUN_00457714 call above. */
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,    D3DTOP_MODULATE2X);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1,  D3DTA_TEXTURE);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2,  D3DTA_TFACTOR);

        /* L83: FUN_00457714(3). */
        scene1_walker_pass_render_house((struct IDirect3DDevice8 *)dev, 3);

        /* L84: FUN_00454f03(palette+0x1a40). */
        scene1_render_apply_palette_combiner_mode(
            (struct IDirect3DDevice8 *)dev, aw_palette_combiner_mode_1a40());
    }

    /* ─── L86-L87: narrow-frustum walker calls ─────────────────────── */
    aw_narrow_frustum_walker_TODO(2);
    aw_narrow_frustum_walker_TODO(3);

    /* ─── L88-L94: cleanup state writes ────────────────────────────── */

    /* L88: CULLMODE = 1 (D3DCULL_NONE).  Alpha geometry can be
     * double-sided. */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);

    /* L89: TEXTUREFACTOR = 0xffffffff (opaque white — reset for any
     * downstream consumer). */
    IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, 0xffffffffu);

    /* L90-L92: restore alpha state, then... */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,    D3DTOP_MODULATE2X);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1,  D3DTA_TEXTURE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2,  D3DTA_TFACTOR);

    /* L93-L94: ...then immediately knock ALPHAOP back to MODULATE and
     * ALPHAARG2 to CURRENT.  The previous three writes are
     * redundant in the steady-state, but the engine performs them —
     * likely for sub-init paths that left the TSS in inconsistent
     * states.  Verbatim. */
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,    D3DTOP_MODULATE);
    IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2,  D3DTA_CURRENT);
}

#endif /* _WIN32 */
