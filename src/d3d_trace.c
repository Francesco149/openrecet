/* D3D state-trace emitter — port side.  See d3d_trace.h. */

#include "d3d_trace.h"
#include "d3d_trace_macros.h"

/* The wrappers below MUST forward to the engine's real D3D methods,
 * not to themselves — so undo the redirects from d3d_trace_macros.h.
 * After these #undefs, the d3d8.h-defined `(p)->lpVtbl->Foo(p, …)`
 * call form is back, which is exactly what the wrappers need.  Order
 * matters: the redirect macros included via d3d_trace_macros.h above
 * are now unconditional. */
#undef IDirect3DDevice8_SetRenderState
#undef IDirect3DDevice8_SetTextureStageState
#undef IDirect3DDevice8_SetTransform
#undef IDirect3DDevice8_SetMaterial
#undef IDirect3DDevice8_SetTexture
#undef IDirect3DDevice8_SetStreamSource
#undef IDirect3DDevice8_SetIndices
#undef IDirect3DDevice8_SetVertexShader
#undef IDirect3DDevice8_DrawPrimitive
#undef IDirect3DDevice8_DrawIndexedPrimitive
#undef IDirect3DDevice8_DrawPrimitiveUP
#undef IDirect3DDevice8_DrawIndexedPrimitiveUP

/* Restore the d3d8 COBJMACROS forms verbatim. */
#define IDirect3DDevice8_SetRenderState(p,a,b)                  (p)->lpVtbl->SetRenderState(p,a,b)
#define IDirect3DDevice8_SetTextureStageState(p,a,b,c)          (p)->lpVtbl->SetTextureStageState(p,a,b,c)
#define IDirect3DDevice8_SetTransform(p,a,b)                    (p)->lpVtbl->SetTransform(p,a,b)
#define IDirect3DDevice8_SetMaterial(p,a)                       (p)->lpVtbl->SetMaterial(p,a)
#define IDirect3DDevice8_SetTexture(p,a,b)                      (p)->lpVtbl->SetTexture(p,a,b)
#define IDirect3DDevice8_SetStreamSource(p,a,b,c)               (p)->lpVtbl->SetStreamSource(p,a,b,c)
#define IDirect3DDevice8_SetIndices(p,a,b)                      (p)->lpVtbl->SetIndices(p,a,b)
#define IDirect3DDevice8_SetVertexShader(p,a)                   (p)->lpVtbl->SetVertexShader(p,a)
#define IDirect3DDevice8_DrawPrimitive(p,a,b,c)                 (p)->lpVtbl->DrawPrimitive(p,a,b,c)
#define IDirect3DDevice8_DrawIndexedPrimitive(p,a,b,c,d,e)      (p)->lpVtbl->DrawIndexedPrimitive(p,a,b,c,d,e)
#define IDirect3DDevice8_DrawPrimitiveUP(p,a,b,c,d)             (p)->lpVtbl->DrawPrimitiveUP(p,a,b,c,d)
#define IDirect3DDevice8_DrawIndexedPrimitiveUP(p,a,b,c,d,e,f,g,h) \
    (p)->lpVtbl->DrawIndexedPrimitiveUP(p,a,b,c,d,e,f,g,h)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── configuration / state ───────────────────────────────────────── */

#define D3D_TRACE_FRAMES_MAX 64

static FILE         *g_f             = NULL;
static unsigned      g_frames[D3D_TRACE_FRAMES_MAX];
static size_t        g_n_frames      = 0;
static unsigned      g_cur_frame     = 0;
static int           g_emit_this_frame = 0;
static const char   *g_module_base   = NULL;

/* ── JSONL emission helpers ──────────────────────────────────────── */

static int d3d_trace_gate(void)
{
    return g_f && g_emit_this_frame;
}

/* `caller` reported relative to the EXE's load base — same convention
 * as the Frida agent's `this.returnAddress.sub(g_base)`.  Add the
 * preferred ImageBase (0x00400000 for openrecet) to map to a Ghidra
 * VA. */
static unsigned d3d_trace_ret_va(const void *retaddr)
{
    if (!retaddr || !g_module_base) return 0;
    return (unsigned)((const char *)retaddr - g_module_base);
}

static void d3d_trace_emit_tail(unsigned ret_va)
{
    fprintf(g_f, ",\"ret_va\":%u,\"frame\":%u}\n", ret_va, g_cur_frame);
}

static void d3d_trace_emit_matrix(const D3DMATRIX *m)
{
    if (!m) { fputs("null", g_f); return; }
    const float *p = (const float *)m;
    fputc('[', g_f);
    for (int i = 0; i < 16; i++) {
        if (i) fputc(',', g_f);
        fprintf(g_f, "%.9g", (double)p[i]);
    }
    fputc(']', g_f);
}

static void d3d_trace_emit_material(const D3DMATERIAL8 *mat)
{
    if (!mat) { fputs("null", g_f); return; }
    const float *p = (const float *)mat;
    fputc('[', g_f);
    for (int i = 0; i < 17; i++) {
        if (i) fputc(',', g_f);
        fprintf(g_f, "%.9g", (double)p[i]);
    }
    fputc(']', g_f);
}

/* ── per-method wrappers ─────────────────────────────────────────── */

HRESULT d3d_trace_SetRenderState(IDirect3DDevice8 *p,
                                 D3DRENDERSTATETYPE state, DWORD value)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetRenderState\","
                "\"args\":{\"state\":%u,\"value\":%u}",
                (unsigned)state, (unsigned)value);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetRenderState(p, state, value);
}

HRESULT d3d_trace_SetTextureStageState(IDirect3DDevice8 *p, DWORD stage,
                                       D3DTEXTURESTAGESTATETYPE type,
                                       DWORD value)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetTextureStageState\","
                "\"args\":{\"stage\":%u,\"type\":%u,\"value\":%u}",
                (unsigned)stage, (unsigned)type, (unsigned)value);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetTextureStageState(p, stage, type, value);
}

HRESULT d3d_trace_SetTransform(IDirect3DDevice8 *p,
                               D3DTRANSFORMSTATETYPE state,
                               const D3DMATRIX *matrix)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetTransform\","
                "\"args\":{\"state\":%u,\"matrix\":",
                (unsigned)state);
        d3d_trace_emit_matrix(matrix);
        fputc('}', g_f);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetTransform(p, state, matrix);
}

HRESULT d3d_trace_SetMaterial(IDirect3DDevice8 *p,
                              const D3DMATERIAL8 *material)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fputs("{\"op\":\"SetMaterial\",\"args\":{\"material\":", g_f);
        d3d_trace_emit_material(material);
        fputc('}', g_f);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetMaterial(p, material);
}

HRESULT d3d_trace_SetTexture(IDirect3DDevice8 *p, DWORD stage,
                             IDirect3DBaseTexture8 *texture)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetTexture\","
                "\"args\":{\"stage\":%u,\"texture\":\"0x%lx\"}",
                (unsigned)stage, (unsigned long)(uintptr_t)texture);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetTexture(p, stage, texture);
}

HRESULT d3d_trace_SetStreamSource(IDirect3DDevice8 *p, UINT stream,
                                  IDirect3DVertexBuffer8 *vb,
                                  UINT stride)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetStreamSource\","
                "\"args\":{\"stream\":%u,\"vb\":\"0x%lx\",\"stride\":%u}",
                (unsigned)stream, (unsigned long)(uintptr_t)vb,
                (unsigned)stride);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetStreamSource(p, stream, vb, stride);
}

HRESULT d3d_trace_SetIndices(IDirect3DDevice8 *p,
                             IDirect3DIndexBuffer8 *ib, UINT base_vertex)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetIndices\","
                "\"args\":{\"ib\":\"0x%lx\",\"base_vertex\":%u}",
                (unsigned long)(uintptr_t)ib, (unsigned)base_vertex);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetIndices(p, ib, base_vertex);
}

HRESULT d3d_trace_SetVertexShader(IDirect3DDevice8 *p, DWORD handle)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"SetVertexShader\","
                "\"args\":{\"handle\":%u}",
                (unsigned)handle);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_SetVertexShader(p, handle);
}

HRESULT d3d_trace_DrawPrimitive(IDirect3DDevice8 *p,
                                D3DPRIMITIVETYPE prim_type,
                                UINT start_vertex, UINT prim_count)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"DrawPrimitive\","
                "\"args\":{\"prim_type\":%u,\"start_vertex\":%u,"
                "\"prim_count\":%u}",
                (unsigned)prim_type, (unsigned)start_vertex,
                (unsigned)prim_count);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_DrawPrimitive(p, prim_type, start_vertex,
                                          prim_count);
}

HRESULT d3d_trace_DrawIndexedPrimitive(IDirect3DDevice8 *p,
                                       D3DPRIMITIVETYPE prim_type,
                                       UINT min_idx, UINT num_vertices,
                                       UINT start_idx, UINT prim_count)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"DrawIndexedPrimitive\","
                "\"args\":{\"prim_type\":%u,\"min_idx\":%u,"
                "\"num_vertices\":%u,\"start_idx\":%u,"
                "\"prim_count\":%u}",
                (unsigned)prim_type, (unsigned)min_idx,
                (unsigned)num_vertices, (unsigned)start_idx,
                (unsigned)prim_count);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_DrawIndexedPrimitive(p, prim_type, min_idx,
                                                 num_vertices, start_idx,
                                                 prim_count);
}

HRESULT d3d_trace_DrawPrimitiveUP(IDirect3DDevice8 *p,
                                  D3DPRIMITIVETYPE prim_type,
                                  UINT prim_count, const void *data,
                                  UINT stride)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"DrawPrimitiveUP\","
                "\"args\":{\"prim_type\":%u,\"prim_count\":%u,"
                "\"vb\":\"0x%lx\",\"vb_stride\":%u}",
                (unsigned)prim_type, (unsigned)prim_count,
                (unsigned long)(uintptr_t)data, (unsigned)stride);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_DrawPrimitiveUP(p, prim_type, prim_count,
                                            data, stride);
}

HRESULT d3d_trace_DrawIndexedPrimitiveUP(IDirect3DDevice8 *p,
                                         D3DPRIMITIVETYPE prim_type,
                                         UINT min_vertex_idx,
                                         UINT vertex_count,
                                         UINT prim_count,
                                         const void *index_data,
                                         D3DFORMAT index_format,
                                         const void *data, UINT stride)
{
    void *ret = __builtin_return_address(0);
    if (d3d_trace_gate()) {
        fprintf(g_f,
                "{\"op\":\"DrawIndexedPrimitiveUP\","
                "\"args\":{\"prim_type\":%u,\"min_vtx_idx\":%u,"
                "\"num_vtx_indices\":%u,\"prim_count\":%u,"
                "\"ib\":\"0x%lx\",\"ib_fmt\":%u,"
                "\"vb\":\"0x%lx\",\"vb_stride\":%u}",
                (unsigned)prim_type, (unsigned)min_vertex_idx,
                (unsigned)vertex_count, (unsigned)prim_count,
                (unsigned long)(uintptr_t)index_data,
                (unsigned)index_format,
                (unsigned long)(uintptr_t)data, (unsigned)stride);
        d3d_trace_emit_tail(d3d_trace_ret_va(ret));
    }
    return IDirect3DDevice8_DrawIndexedPrimitiveUP(p, prim_type,
                                                   min_vertex_idx,
                                                   vertex_count,
                                                   prim_count,
                                                   index_data,
                                                   index_format,
                                                   data, stride);
}

/* ── public API ──────────────────────────────────────────────────── */

void d3d_trace_init_from_cli(const char *path,
                             const unsigned *frames, size_t n_frames)
{
    if (!path) return;

    g_f = fopen(path, "w");
    if (!g_f) return;

    setvbuf(g_f, NULL, _IOLBF, 0);

    if (frames && n_frames) {
        if (n_frames > D3D_TRACE_FRAMES_MAX) n_frames = D3D_TRACE_FRAMES_MAX;
        memcpy(g_frames, frames, n_frames * sizeof(unsigned));
        g_n_frames = n_frames;
    } else {
        g_n_frames = 0;
    }

    g_module_base = (const char *)GetModuleHandleA(NULL);
}

void d3d_trace_install(IDirect3DDevice8 *dev)
{
    /* Macro-redirect interception happens at every TU's call site.
     * No vtable mutation needed; just keep this stub for the caller. */
    (void)dev;
}

/* Optional half-open emit window [lo, hi).  hi<=lo = inactive.  Set from the
 * {caprange} segtrace callback so the port d3d-trace emits exactly the
 * anchor-relative capture window (jitter-immune) without a fixed frame list —
 * the window can exceed D3D_TRACE_FRAMES_MAX.  ORed with the explicit list. */
static unsigned g_win_lo = 0, g_win_hi = 0;
static int      g_windowed = 0;   /* once set, suppresses the emit-all default */

void d3d_trace_set_window(unsigned lo, unsigned hi)
{
    g_windowed = 1;
    g_win_lo = lo; g_win_hi = hi;
}

void d3d_trace_begin_frame(unsigned frame)
{
    g_cur_frame = frame;
    if (!g_f) { g_emit_this_frame = 0; return; }
    /* emit-all default only when neither a frame list nor a window is in play */
    if (g_n_frames == 0 && !g_windowed) { g_emit_this_frame = 1; return; }
    g_emit_this_frame = (g_win_hi > g_win_lo && frame >= g_win_lo && frame < g_win_hi)
                        ? 1 : 0;
    for (size_t i = 0; i < g_n_frames; i++) {
        if (g_frames[i] == frame) { g_emit_this_frame = 1; break; }
    }
}

void d3d_trace_end_frame(void)
{
    if (g_f) fflush(g_f);
}

void d3d_trace_shutdown(void)
{
    if (g_f) { fclose(g_f); g_f = NULL; }
}
