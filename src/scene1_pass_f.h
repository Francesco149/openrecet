/*
 * scene1_pass_f.h — MVP port of FUN_004161c7 Pass F (lines L423-481).
 *
 * The wide-followup walker's last per-record pass: a color-cycle
 * billboard quad per table-A slot whose TYPE field == 0x92.  Renders
 * with vertex DIFFUSE only (no texture sampling); the color cycles
 * through a 9-entry palette indexed by `slot_index % 9`.
 *
 * MVP scope: this file ports ONLY Pass F, not the surrounding A-E
 * passes or the wide-followup's render-state preamble.  The caller is
 * responsible for setting view + projection beforehand.  Designed to be
 * called from a temporary `--show-pass-f-test` dev path that validates
 * the C8c/C8e per-record draw contract without first porting
 * FUN_0040fb3a (the 8071 B integrator) or FUN_00447f4f (the 11826 B
 * spawn API).  See docs/findings/scene1-particles-tick.md "Option A".
 *
 * Diverges from the engine in two intentional ways:
 *
 *   1. **Self-contained vertex buffer.**  The engine's static vbuf at
 *      DAT_00648698 is initialized to specific object-space coords by
 *      a separate scene-boot helper (all.c lines 8849 area, unported).
 *      We allocate a local static vbuf with a unit quad in object space
 *      so the scale formula `piVar11[5]/200 * piVar11[2] * 0.005`
 *      produces a visually sensible billboard size when fed the
 *      engine's typical spawn values (param2=100..199, scale=1.0f).
 *
 *   2. **State setup.**  The engine wide-followup's L40-L49 preamble
 *      sets ~10 device states shared across Pass A-F; only those Pass F
 *      actually needs to draw correctly are set here.  In particular:
 *      CULLMODE=NONE (billboards face all ways), texture stage configured
 *      to SELECTARG2/D3DTA_DIFFUSE (no texture sample), and the FVF set
 *      to XYZ|DIFFUSE|TEX1 (0x142, what the engine uses for these
 *      world-space quads).  No texture is bound — the original Pass F
 *      relies on the previous pass having left DAT_073cc8c0 bound, but
 *      with SELECTARG2 the texture is unused.
 *
 * Both divergences let this MVP run standalone for visual smoke.  When
 * the broader C8h ladder lands, this file's draw helper folds into the
 * full FUN_004161c7 port and the divergences go away.
 */

#ifndef OPENRECET_SCENE1_PASS_F_H
#define OPENRECET_SCENE1_PASS_F_H

#ifdef _WIN32

struct IDirect3DDevice8;

/* Walk g_scene1_records_a[0 .. g_scene1_records_a_count) and emit one
 * billboard quad per type-0x92 record.  No-op when dev is NULL or the
 * count is zero.  Leaves device state changed (CULLMODE, texture stage
 * ops, world matrix, FVF) — caller must restore if necessary.
 */
void scene1_pass_f_render(struct IDirect3DDevice8 *dev);

#endif /* _WIN32 */

#endif /* OPENRECET_SCENE1_PASS_F_H */
