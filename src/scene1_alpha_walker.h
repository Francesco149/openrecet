/*
 * scene1_alpha_walker.h — C8d port of FUN_00458bdf (904 B).
 *
 * One of the four mesh walkers dispatched by scene1_render_meshes
 * (C8a, FUN_00459dfd's L247 call site).  Fires in the alpha-pass
 * frame slot — depth-test on / depth-write off, alpha-blend on,
 * cull off — after C8c's wide-frustum pass has laid down opaque
 * geometry.
 *
 * Structure (engine line numbers from
 * docs/decompiled/by-address/458bdf.c):
 *
 *   L18-L30   Initial per-instance transform builder.  Count =
 *             DAT_0438bfb0 (BSS-zero in HOUSE — written only by
 *             FUN_00436f97, which is the unported 710-byte sibling
 *             of FUN_00474a9a in the case-1 dispatch).  For each
 *             record: Translation(puVar5[-0x14], puVar5[0],
 *             puVar5[0x14]) × Scaling(-1, 1, 1) → stored at
 *             local_500[i * 0x40].  Dormant for HOUSE.
 *
 *   L31       _DAT_006051ac = 1 (scratch flag, no consumer ported).
 *
 *   L32-L44   Render-state preset (14 writes):
 *               ZENABLE=TRUE  ZWRITEENABLE=FALSE
 *               SRCBLEND=SRCALPHA  DESTBLEND=INVSRCALPHA
 *               TSS COLOROP=4 (MODULATE2X)
 *               TSS COLORARG2=2 (D3DTA_TEXTURE)
 *               TSS COLORARG1=0 (D3DTA_DIFFUSE)
 *               TSS ALPHAOP=4 (MODULATE2X)
 *               TSS ALPHAARG1=2 (D3DTA_TEXTURE)
 *               TSS ALPHAARG2=3 (D3DTA_TFACTOR)
 *               TSS MINFILTER=LINEAR  MAGFILTER=LINEAR
 *               FOGENABLE=FALSE
 *
 *   L45-L55   Stage-palette-gated blend swap.  Read palette+0x1a50:
 *               == 0  (HOUSE)  → DESTBLEND=INVSRCALPHA SRCBLEND=SRCALPHA
 *               != 0           → DESTBLEND=SRCCOLOR    SRCBLEND=DESTCOLOR
 *
 *   L56       FUN_00454f03(palette+0x1a44) — TSS COLORARG2 from
 *             palette combiner-mode mode int.  Already exposed as
 *             scene1_render_apply_palette_combiner_mode.
 *
 *   L57-L58   SetRenderState(TEXTUREFACTOR, palette+0x1a48 << 24 |
 *             0xffffff).  Per-stage tint color.
 *
 *   L59-L85   Alpha-pass guard (DAT_073dfcec).  When 0 (normal):
 *               L60-L62  if (DAT_0438b4e4 != 0) FUN_00454f03(2);
 *               L63      FUN_00457714(2)  ← 5323 B, stubbed in C8a
 *               L64-L74  palette+0x1a5c gate: pick SRC vs DEST blend
 *               L75      FUN_00454f03(palette+0x1a54)
 *               L76-L79  TEXTUREFACTOR from __ftol() — Ghidra dropped
 *                        the arg, source likely palette+0x1a58 with a
 *                        scale.  Verbatim with placeholder for now.
 *               L80-L82  3 TSS writes (ALPHAOP=4, ALPHAARG1=2,
 *                        ALPHAARG2=3 — restore from any palette
 *                        combiner overrides)
 *               L83      FUN_00457714(3)
 *               L84      FUN_00454f03(palette+0x1a40)
 *
 *   L86       FUN_00459847(2) — narrow-frustum mesh walker, pass 2
 *             (currently stubbed in C8a as
 *             scene1_walk_narrow_frustum_TODO).
 *
 *   L87       FUN_00459847(3) — narrow-frustum mesh walker, pass 3.
 *
 *   L88-L94   Cleanup state writes (7):
 *               CULLMODE=1 (NONE — wait, 1 IS NONE → wait, 1 is NONE,
 *                          but reading hex 0x16=22 = CULLMODE, value
 *                          1 = NONE.  Earlier passes had CCW.)
 *               TEXTUREFACTOR=0xffffffff
 *               TSS ALPHAOP=4 (MODULATE2X)
 *               TSS ALPHAARG1=2 (D3DTA_TEXTURE)
 *               TSS ALPHAARG2=3 (D3DTA_TFACTOR)
 *               TSS ALPHAOP=2 (MODULATE)
 *               TSS ALPHAARG2=1 (D3DTA_CURRENT)
 *
 * Wiring: replaces scene1_walk_alpha_TODO's call site in
 * scene1_render_meshes (engine L247).
 *
 * What's dormant in HOUSE:
 *   - Initial transform loop (DAT_0438bfb0 == 0).
 *   - Palette branches all return BSS-zero default arms (palette
 *     +0x1a40/44/48/50/54/58/5c all zero).
 *   - FUN_00459847(2/3) is still TODO in C8a — no actual draws.
 *
 * Observable side-effect: ~30 device state writes that put the
 * device into alpha-pass-ready state for the next walker.
 *
 * No-op when dev is NULL.
 */

#ifndef OPENRECET_SCENE1_ALPHA_WALKER_H
#define OPENRECET_SCENE1_ALPHA_WALKER_H

#ifdef _WIN32

struct IDirect3DDevice8;

void scene1_alpha_walker(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_ALPHA_WALKER_H */
