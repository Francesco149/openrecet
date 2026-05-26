/* D.5: re-route the 12 IDirect3DDevice8_* call-site macros through
 * the d3d_trace_* wrappers defined in d3d_trace.c.
 *
 * Injected via src/Makefile `-include d3d_trace_macros.h` so every
 * TU's `IDirect3DDevice8_SetRenderState(dev, …)` expands to
 * `d3d_trace_SetRenderState((dev), …)` automatically — no need to
 * touch the ~30 .c files that call D3D.
 *
 * The `-include` form arranges this header to be processed AT THE TOP
 * of every TU, BEFORE any other code in the TU.  We force-include
 * `<windows.h>` + `<d3d8.h>` from inside so that:
 *   (a) the macros we override actually exist by the time we
 *       redefine them (d3d8.h emits them when COBJMACROS is set);
 *   (b) subsequent `#include <d3d8.h>` in the user code is a no-op
 *       (the include guard fires), so our redefinitions survive.
 *
 * d3d_trace.c itself MUST NOT pick up the redirected macros — it
 * needs the raw lpVtbl calls inside the wrappers to forward the call
 * to d3d8.dll.  d3d_trace.c handles this by `#undef`ing each of the
 * 12 macros immediately after including d3d_trace_macros.h, then
 * doing the lpVtbl call manually.
 */

#ifndef OPENRECET_D3D_TRACE_MACROS_H
#define OPENRECET_D3D_TRACE_MACROS_H

/* Bring in the COBJMACROS variant of d3d8.h so the original macros
 * are present (we then immediately override the 12 we care about). */
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>

#include "d3d_trace.h"

#undef  IDirect3DDevice8_SetRenderState
#define IDirect3DDevice8_SetRenderState(p,a,b) \
    d3d_trace_SetRenderState((p),(a),(b))

#undef  IDirect3DDevice8_SetTextureStageState
#define IDirect3DDevice8_SetTextureStageState(p,a,b,c) \
    d3d_trace_SetTextureStageState((p),(a),(b),(c))

#undef  IDirect3DDevice8_SetTransform
#define IDirect3DDevice8_SetTransform(p,a,b) \
    d3d_trace_SetTransform((p),(a),(b))

#undef  IDirect3DDevice8_SetMaterial
#define IDirect3DDevice8_SetMaterial(p,a) \
    d3d_trace_SetMaterial((p),(a))

#undef  IDirect3DDevice8_SetTexture
#define IDirect3DDevice8_SetTexture(p,a,b) \
    d3d_trace_SetTexture((p),(a),(b))

#undef  IDirect3DDevice8_SetStreamSource
#define IDirect3DDevice8_SetStreamSource(p,a,b,c) \
    d3d_trace_SetStreamSource((p),(a),(b),(c))

#undef  IDirect3DDevice8_SetIndices
#define IDirect3DDevice8_SetIndices(p,a,b) \
    d3d_trace_SetIndices((p),(a),(b))

#undef  IDirect3DDevice8_SetVertexShader
#define IDirect3DDevice8_SetVertexShader(p,a) \
    d3d_trace_SetVertexShader((p),(a))

#undef  IDirect3DDevice8_DrawPrimitive
#define IDirect3DDevice8_DrawPrimitive(p,a,b,c) \
    d3d_trace_DrawPrimitive((p),(a),(b),(c))

#undef  IDirect3DDevice8_DrawIndexedPrimitive
#define IDirect3DDevice8_DrawIndexedPrimitive(p,a,b,c,d,e) \
    d3d_trace_DrawIndexedPrimitive((p),(a),(b),(c),(d),(e))

#undef  IDirect3DDevice8_DrawPrimitiveUP
#define IDirect3DDevice8_DrawPrimitiveUP(p,a,b,c,d) \
    d3d_trace_DrawPrimitiveUP((p),(a),(b),(c),(d))

#undef  IDirect3DDevice8_DrawIndexedPrimitiveUP
#define IDirect3DDevice8_DrawIndexedPrimitiveUP(p,a,b,c,d,e,f,g,h) \
    d3d_trace_DrawIndexedPrimitiveUP((p),(a),(b),(c),(d),(e),(f),(g),(h))

#endif
