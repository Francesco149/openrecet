/* Port-side call tracer — see call_trace.h. */

#include "call_trace.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define CALL_TRACE_FRAMES_MAX 256
#define CALL_TRACE_WINDOWS_MAX 64

static FILE         *g_f               = NULL;
static unsigned      g_frames[CALL_TRACE_FRAMES_MAX];
static size_t        g_n_frames        = 0;
static unsigned      g_windows[CALL_TRACE_WINDOWS_MAX][2];  /* [lo, hi) */
static size_t        g_n_windows       = 0;
static unsigned      g_cur_frame       = 0;
static int           g_emit_this_frame = 0;
static const char   *g_module_base     = NULL;

/* Per-frame execution-order counter — stamped on every emitted call (legacy
 * CALL_TRACE_ENTER + the BEGIN/FIELD/END field-bearing events) so flow_diff.py
 * can align the call CHAIN, not just the set.  Reset each begin_frame. */
static unsigned      g_seq             = 0;

/* Field-bearing event buffer (BEGIN/FIELD/END).  One event is assembled here
 * and fwritten atomically at END, so the shared stream is never interleaved.
 * The probe discipline (emit fields at entry, before any traced sub-call, then
 * END) keeps events non-nested; g_in_event guards against misuse. */
#define CT_EVT_CAP 1024
static char          g_evt[CT_EVT_CAP];
static int           g_evt_len         = 0;
static int           g_in_event        = 0;
static int           g_evt_nfields     = 0;
static int           g_evt_stub        = 0;   /* "stub":true on the open event */

static void ct_evt_append(const char *fmt, ...)
{
    if (g_evt_len >= CT_EVT_CAP - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_evt + g_evt_len, (size_t)(CT_EVT_CAP - g_evt_len),
                      fmt, ap);
    va_end(ap);
    if (n > 0) {
        g_evt_len += n;
        if (g_evt_len > CT_EVT_CAP - 1) g_evt_len = CT_EVT_CAP - 1;
    }
}

void call_trace_init_from_cli(const char *path,
                              const unsigned *frames, size_t n_frames)
{
    if (!path) return;

    g_f = fopen(path, "w");
    if (!g_f) return;

    /* Full-buffer the trace (1 MiB) rather than line-buffer it.  The {calltrace}
     * window emits ~150 lines/frame; line buffering issued a write() per newline
     * AND call_trace_end_frame fflush()ed every frame — a syscall storm that, over
     * the 9p \\wsl.localhost mount, throttled full-haggle drives to ~10 fps and
     * made a degraded 9p look hung (the "frame-231 hang" of RE §12 was this, not a
     * logic gap).  Full buffering collapses that to ~1 write per ~80 frames, so the
     * sim loop stops blocking on trace I/O.  A completed drive flushes identically
     * at fclose (call_trace_shutdown); a hard-killed one loses ≤1 buffer of tail
     * (fine — we re-drive).  Pairs with scenario-test's NTFS staging of this file. */
    setvbuf(g_f, NULL, _IOFBF, 1 << 20);

    if (frames && n_frames) {
        if (n_frames > CALL_TRACE_FRAMES_MAX) n_frames = CALL_TRACE_FRAMES_MAX;
        memcpy(g_frames, frames, n_frames * sizeof(unsigned));
        g_n_frames = n_frames;
    } else {
        g_n_frames = 0;
    }

#ifdef _WIN32
    g_module_base = (const char *)GetModuleHandleA(NULL);
#else
    /* Host (test) build: no PE base; ret_va stays at the raw pointer
     * value, which is fine — host tests don't exercise the file path. */
    g_module_base = NULL;
#endif
}

int call_trace_is_open(void)
{
    return g_f != NULL;
}

void call_trace_arm_window(unsigned lo, unsigned hi)
{
    if (!g_f || hi <= lo) return;
    if (g_n_windows >= CALL_TRACE_WINDOWS_MAX) return;
    g_windows[g_n_windows][0] = lo;
    g_windows[g_n_windows][1] = hi;
    g_n_windows++;
}

void call_trace_begin_frame(unsigned frame)
{
    g_cur_frame = frame;
    g_seq = 0;
    g_in_event = 0;           /* drop any half-open event from last frame */
    if (!g_f) { g_emit_this_frame = 0; return; }
    /* "Trace everything" only when neither a frame list nor a window was
     * supplied (legacy --call-trace with no --call-trace-frames).  Once a
     * {calltrace} window is armed, emission is window-gated. */
    if (g_n_frames == 0 && g_n_windows == 0) { g_emit_this_frame = 1; return; }
    g_emit_this_frame = 0;
    for (size_t i = 0; i < g_n_frames; i++) {
        if (g_frames[i] == frame) { g_emit_this_frame = 1; return; }
    }
    for (size_t i = 0; i < g_n_windows; i++) {
        if (frame >= g_windows[i][0] && frame < g_windows[i][1]) {
            g_emit_this_frame = 1; return;
        }
    }
}

void call_trace_end_frame(void)
{
    /* No per-frame fflush — that plus line-buffering was the 9p syscall storm
     * (see call_trace_init_from_cli).  Flush every 1024 frames as a safety net so
     * a hard-killed drive keeps most of its tail even on a sparse trace that never
     * fills the 1 MiB buffer; a completed drive flushes fully at fclose. */
    static unsigned n = 0;
    if (g_f && (++n & 1023u) == 0)
        fflush(g_f);
}

void call_trace_shutdown(void)
{
    if (g_f) { fclose(g_f); g_f = NULL; }
}

void call_trace_enter(uint32_t ghidra_va, const void *ret_addr, int stub)
{
    if (!g_f || !g_emit_this_frame) return;

    unsigned ret_off = 0;
    if (ret_addr && g_module_base)
        ret_off = (unsigned)((const char *)ret_addr - g_module_base);

    if (stub) {
        fprintf(g_f,
                "{\"va\":%u,\"ret_va\":%u,\"frame\":%u,\"seq\":%u,\"stub\":true}\n",
                (unsigned)ghidra_va, ret_off, g_cur_frame, g_seq++);
    } else {
        fprintf(g_f,
                "{\"va\":%u,\"ret_va\":%u,\"frame\":%u,\"seq\":%u}\n",
                (unsigned)ghidra_va, ret_off, g_cur_frame, g_seq++);
    }
}

/* ── field-bearing event (BEGIN/FIELD/END) ─────────────────────────────────
 * Assembles one event carrying a declared payload `f:{…}` (the inputs/state
 * the function used).  Joined to the retail side by (va, field-name); see
 * docs/plans/execution-flow-trace.md. */

static void ct_begin(uint32_t ghidra_va, const void *ret_addr, int stub)
{
    if (!g_f || !g_emit_this_frame) { g_in_event = 0; return; }
    if (g_in_event) call_trace_end();   /* misuse: finalize the prior event */

    unsigned ret_off = 0;
    if (ret_addr && g_module_base)
        ret_off = (unsigned)((const char *)ret_addr - g_module_base);

    g_evt_len = 0;
    g_evt_nfields = 0;
    g_evt_stub = stub;
    g_in_event = 1;
    ct_evt_append("{\"va\":%u,\"ret_va\":%u,\"frame\":%u,\"seq\":%u",
                  (unsigned)ghidra_va, ret_off, g_cur_frame, g_seq++);
}

void call_trace_begin(uint32_t ghidra_va, const void *ret_addr)
{
    ct_begin(ghidra_va, ret_addr, 0);
}

/* Stub variant: same field-bearing event, plus "stub":true — for a
 * partially-ported function whose declared INPUTS are still worth diffing
 * (the entry-state must track retail even when the body is a subset). */
void call_trace_begin_stub(uint32_t ghidra_va, const void *ret_addr)
{
    ct_begin(ghidra_va, ret_addr, 1);
}

static void ct_field_open(const char *name)
{
    /* First field opens the `f` object; subsequent fields just prepend a
     * comma.  Caller appends the value. */
    ct_evt_append(g_evt_nfields++ ? ",\"%s\":" : ",\"f\":{\"%s\":", name);
}

void call_trace_field_i32(const char *name, int32_t v)
{
    if (!g_in_event) return;
    ct_field_open(name);
    ct_evt_append("%ld", (long)v);
}

void call_trace_field_u32(const char *name, uint32_t v)
{
    if (!g_in_event) return;
    ct_field_open(name);
    ct_evt_append("%lu", (unsigned long)v);
}

void call_trace_field_f32(const char *name, float v)
{
    if (!g_in_event) return;
    ct_field_open(name);
    ct_evt_append("%.9g", (double)v);
}

void call_trace_field_hex(const char *name, uint32_t v)
{
    if (!g_in_event) return;
    ct_field_open(name);
    ct_evt_append("\"0x%lx\"", (unsigned long)v);
}

void call_trace_end(void)
{
    if (!g_in_event) return;
    if (g_evt_nfields) ct_evt_append("}");   /* close the `f` object */
    if (g_evt_stub) ct_evt_append(",\"stub\":true");
    ct_evt_append("}\n");
    fwrite(g_evt, 1, (size_t)g_evt_len, g_f);
    g_in_event = 0;
}
