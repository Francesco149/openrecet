/*
 * model_render — the model enemies' draw (FUN_00456d48 → FUN_00404a20): the
 * enemy records with +0x20 == 1 draw their xfile2 model, posed like their
 * anchors (model_anchor_pose), through retail's unlit skinning shaders
 * (D3DINDEXEDVS: oD0 = the vertex colour, oT0 = uv; findings
 * skinned-mesh-render.md).  The port blends on the CPU — Σ w · (v · offset ·
 * combined) into world space — and draws with FFP lighting off.  COLOROP
 * MODULATE (ADDSIGNED while +0x1c is set); with +0x3dc < 255 the alpha comes
 * from TEXTUREFACTOR.
 */
#ifndef OPENRECET_MODEL_RENDER_H
#define OPENRECET_MODEL_RENDER_H

#include <stdint.h>

#ifdef _WIN32
struct IDirect3DDevice8;

/* FUN_00473c15's model loads: the models of the roster's kinds (after the
 * texture-cache clear) */
void model_render_load(struct IDirect3DDevice8 *dev, const int32_t present[100]);

/* FUN_00459dfd's actor passes: 0 = the opaque ones (+0x3dc 255, from
 * FUN_004552d0), 1 = the rest unless +0xaf4 > 0 (from FUN_00456f56) */
void model_render_pass(struct IDirect3DDevice8 *dev, int pass);
#endif

#endif /* OPENRECET_MODEL_RENDER_H */
