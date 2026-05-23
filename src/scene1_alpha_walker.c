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

/* DAT_068dd2f0 + 0x1a40 (int) — combiner mode for the final
 * FUN_00454f03 call inside the alpha-pass-guard branch. */
static int aw_palette_combiner_mode_1a40(void) { return 0; }

/* DAT_068dd2f0 + 0x1a44 (int) — combiner mode for the top-level
 * FUN_00454f03 call right after the palette-gated blend swap. */
static int aw_palette_combiner_mode_1a44(void) { return 0; }

/* DAT_068dd2f0 + 0x1a48 (int) — TEXTUREFACTOR per-channel low byte,
 * OR'd into the high byte slot.  HOUSE → 0 → factor = 0xff000000
 * (opaque black multiplicand, effectively kills the modulation). */
static int aw_palette_texture_factor_1a48(void) { return 0; }

/* DAT_068dd2f0 + 0x1a50 (int) — blend-mode gate.  HOUSE → 0 → the
 * standard alpha-blend pair applies. */
static int aw_palette_blend_gate_1a50(void) { return 0; }

/* DAT_068dd2f0 + 0x1a54 (int) — combiner mode for the inner
 * alpha-pass-guard FUN_00454f03 call. */
static int aw_palette_combiner_mode_1a54(void) { return 0; }

/* DAT_068dd2f0 + 0x1a5c (int) — inner blend-mode gate inside the
 * alpha-pass-guard branch. */
static int aw_palette_inner_blend_gate_1a5c(void) { return 0; }

/* DAT_073dfcec — alpha-pass guard.  Same accessor name as the one
 * in scene1_render.c but local-scoped: zero means "render normally"
 * which keeps the inner FUN_00457714 + per-state branch live. */
static int aw_alpha_pass_guard(void) { return 0; }

/* DAT_0438b4e4 — texture combiner override.  Same as
 * scene1_combiner_override in scene1_render.c; zero by default. */
static int aw_combiner_override(void) { return 0; }

/* FUN_00457714 (5323 B) — per-pass texture/shader sub-init.  C8a
 * has a TODO stub `scene1_walk_pass_init_TODO` for this, but it's
 * module-local; redeclare the stub here so the body reads cleanly. */
static void aw_pass_init_TODO(int which_pass)
{
    /* TODO C8-followup: port FUN_00457714.  Same body as the
     * scene1_walk_pass_init_TODO in scene1_render.c; when one ports
     * the other can be deleted. */
    (void)which_pass;
}

/* FUN_00459847 (1444 B) — narrow-frustum mesh walker.  C8a has a
 * TODO stub `scene1_walk_narrow_frustum_TODO` for this; redeclare
 * here as above. */
static void aw_narrow_frustum_walker_TODO(int pass)
{
    /* TODO C8-followup: port FUN_00459847.  Walker B of the four
     * mesh walkers — likely the engine's "walls/floor static room
     * geometry" walker per the C8a survey. */
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
        /* L51-L52: ONE × ONE → DESTBLEND=DESTCOLOR (2),
         * SRCBLEND=DESTCOLOR (2).  Pre-multiplied-pixel mode for
         * some palette-driven post-effect.  Dormant in HOUSE. */
        IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_DESTCOLOR);
        IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_DESTCOLOR);
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
         * pass-id 2.  Stubbed. */
        aw_pass_init_TODO(2);

        /* L64-L74: inner palette blend-mode gate. */
        if (aw_palette_inner_blend_gate_1a5c() == 0) {
            /* L65-L66: SRCBLEND=SRCALPHA(5) + DESTBLEND=INVSRCCOLOR(4).
             * Wait — engine writes 0x13 (SRCBLEND) with 5 then DEST
             * was already set above so writes the dest at 0x14 with 0x14.
             *
             * Re-reading the decomp:
             *   SetRenderState(0x13, 5);  // SRCBLEND=SRCALPHA
             *   uVar6 = 0x14; iVar3 = *DAT_073dfcbc;
             *   (then below the if)
             *   SetRenderState(uVar6, 2);
             * So uVar6=0x14 picks DESTBLEND, then value=2 (DESTCOLOR).
             */
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_DESTCOLOR);
        } else {
            /* L70-L71: DESTBLEND=DESTCOLOR(2) + SRCBLEND=DESTCOLOR(2).
             * Same as the outer-gate non-zero arm — pre-multiplied
             * variant.  Engine literal:
             *   SetRenderState(0x14, 2);  // DESTBLEND=DESTCOLOR
             *   uVar6 = 0x13; iVar3 = *DAT_073dfcbc;
             *   SetRenderState(uVar6, 2); // SRCBLEND=DESTCOLOR
             */
            IDirect3DDevice8_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_DESTCOLOR);
            IDirect3DDevice8_SetRenderState(dev, D3DRS_SRCBLEND,  D3DBLEND_DESTCOLOR);
        }

        /* L75: FUN_00454f03(palette+0x1a54) — combiner mode. */
        scene1_render_apply_palette_combiner_mode(
            (struct IDirect3DDevice8 *)dev, aw_palette_combiner_mode_1a54());

        /* L76-L79: TEXTUREFACTOR set from __ftol().  Ghidra dropped
         * the float arg to ftol; likely a per-fade computed alpha.
         * Until the source ports, we use a placeholder of 0xff —
         * full opacity — which is the engine's "no fade" steady
         * state. */
        {
            DWORD factor = (0xffu << 24) | 0x00ffffffu;
            IDirect3DDevice8_SetRenderState(dev, D3DRS_TEXTUREFACTOR, factor);
        }
        /* TODO C8-followup: identify the __ftol source.  Likely
         * palette+0x1a58 * fade_scalar from the palette's fade
         * counters; surfaces once a starter ports. */

        /* L80-L82: restore TSS alpha state — may have been reset by
         * the FUN_00457714 call above. */
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP,    D3DTOP_MODULATE2X);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1,  D3DTA_TEXTURE);
        IDirect3DDevice8_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG2,  D3DTA_TFACTOR);

        /* L83: FUN_00457714(3). */
        aw_pass_init_TODO(3);

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
