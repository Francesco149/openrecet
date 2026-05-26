/* D3D state-trace emitter — port-side counterpart of the Frida
 * D3D agent (tools/frida/openrecet-agent.js installD3dTraceHooks).
 *
 * Emits one JSONL row per IDirect3DDevice8 vtable call, matching the
 * agent's schema field-for-field so tools/render_diff.py (Phase D.6)
 * can walk the two traces in lockstep.  See docs/findings/d3d-trace.md
 * for the schema and docs/harness-roadmap.md §D.5 for the project goal.
 *
 * Interception mechanism: call-site macro redirection.  The 12 D3D
 * macros (IDirect3DDevice8_SetRenderState etc.) are redefined in
 * `d3d_trace_macros.h`, which the Makefile -includes ahead of every
 * TU.  This way every existing `IDirect3DDevice8_Foo(dev, …)` call
 * site automatically routes through `d3d_trace_Foo(dev, …)` defined
 * here, with no source-tree changes.
 *
 * Vtable hot-patch (the obvious alternative) was tried first; the
 * d3d8 implementation on this host crashes when `dev->lpVtbl` is
 * reassigned to a verbatim copy of the original vtable, so the
 * approach is unusable.  Macro redirection sidesteps the issue.
 *
 * Wiring (in src/main.c):
 *
 *   1. parse_cmdline absorbs --d3d-trace <path>
 *      and optional --d3d-trace-frames i,j,k.
 *      d3d_trace_init_from_cli stashes those.
 *   2. After CreateDevice succeeds, call d3d_trace_install(dev) so
 *      the wrappers know which device pointer is being traced.
 *   3. At the top of render_dispatch, call d3d_trace_begin_frame(N)
 *      with the upcoming sim-frame number — this re-evaluates the
 *      filter set and gates all subsequent emits for that frame.
 *   4. After Present, call d3d_trace_end_frame() to fflush() so an
 *      ungraceful exit still leaves a complete-up-to-the-last-flush
 *      file.
 *
 * Cost when not enabled: every entry point is a single null-check on a
 * static FILE pointer, then a forward through the lpVtbl call.
 */

#ifndef OPENRECET_D3D_TRACE_H
#define OPENRECET_D3D_TRACE_H

#include <stddef.h>
#include <d3d8.h>

/* Capture configuration from CLI.  Pass NULL/0 to disable.
 * `frames` may be NULL → trace every frame; otherwise the listed sim-
 * frame indices buffer & flush, others pay zero cost (gate check before
 * any work).  Copies the frame array internally — caller's storage is
 * not retained. */
void d3d_trace_init_from_cli(const char *path,
                             const unsigned *frames, size_t n_frames);

/* Remember the traced device pointer.  No vtable mutation. */
void d3d_trace_install(IDirect3DDevice8 *dev);

/* Called once per frame from render_dispatch with the upcoming sim-
 * frame index.  Caches whether emit should happen this frame
 * (per-frame filter check). */
void d3d_trace_begin_frame(unsigned frame);

/* Called once per frame from render_dispatch after Present.  fflush()'s
 * the trace file. */
void d3d_trace_end_frame(void);

/* Closes the trace file. */
void d3d_trace_shutdown(void);

/* ── per-call wrappers ─────────────────────────────────────────────
 * Called via the call-site macros in d3d_trace_macros.h.  Each one
 * does an emit-gate check, optionally writes one JSONL row to the
 * trace file, then forwards to the engine's original D3D method.
 */
HRESULT d3d_trace_SetRenderState(IDirect3DDevice8 *p,
                                 D3DRENDERSTATETYPE state, DWORD value);
HRESULT d3d_trace_SetTextureStageState(IDirect3DDevice8 *p, DWORD stage,
                                       D3DTEXTURESTAGESTATETYPE type,
                                       DWORD value);
HRESULT d3d_trace_SetTransform(IDirect3DDevice8 *p,
                               D3DTRANSFORMSTATETYPE state,
                               const D3DMATRIX *matrix);
HRESULT d3d_trace_SetMaterial(IDirect3DDevice8 *p,
                              const D3DMATERIAL8 *material);
HRESULT d3d_trace_SetTexture(IDirect3DDevice8 *p, DWORD stage,
                             IDirect3DBaseTexture8 *texture);
HRESULT d3d_trace_SetStreamSource(IDirect3DDevice8 *p, UINT stream,
                                  IDirect3DVertexBuffer8 *vb,
                                  UINT stride);
HRESULT d3d_trace_SetIndices(IDirect3DDevice8 *p,
                             IDirect3DIndexBuffer8 *ib,
                             UINT base_vertex);
HRESULT d3d_trace_SetVertexShader(IDirect3DDevice8 *p, DWORD handle);
HRESULT d3d_trace_DrawPrimitive(IDirect3DDevice8 *p,
                                D3DPRIMITIVETYPE prim_type,
                                UINT start_vertex, UINT prim_count);
HRESULT d3d_trace_DrawIndexedPrimitive(IDirect3DDevice8 *p,
                                       D3DPRIMITIVETYPE prim_type,
                                       UINT min_idx, UINT num_vertices,
                                       UINT start_idx, UINT prim_count);
HRESULT d3d_trace_DrawPrimitiveUP(IDirect3DDevice8 *p,
                                  D3DPRIMITIVETYPE prim_type,
                                  UINT prim_count, const void *data,
                                  UINT stride);
HRESULT d3d_trace_DrawIndexedPrimitiveUP(IDirect3DDevice8 *p,
                                         D3DPRIMITIVETYPE prim_type,
                                         UINT min_vertex_idx,
                                         UINT vertex_count,
                                         UINT prim_count,
                                         const void *index_data,
                                         D3DFORMAT index_format,
                                         const void *data, UINT stride);

#endif
