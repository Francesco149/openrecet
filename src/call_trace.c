/* Port-side call tracer — see call_trace.h. */

#include "call_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define CALL_TRACE_FRAMES_MAX 256

static FILE         *g_f               = NULL;
static unsigned      g_frames[CALL_TRACE_FRAMES_MAX];
static size_t        g_n_frames        = 0;
static unsigned      g_cur_frame       = 0;
static int           g_emit_this_frame = 0;
static const char   *g_module_base     = NULL;

void call_trace_init_from_cli(const char *path,
                              const unsigned *frames, size_t n_frames)
{
    if (!path) return;

    g_f = fopen(path, "w");
    if (!g_f) return;

    setvbuf(g_f, NULL, _IOLBF, 0);

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

void call_trace_begin_frame(unsigned frame)
{
    g_cur_frame = frame;
    if (!g_f) { g_emit_this_frame = 0; return; }
    if (g_n_frames == 0) { g_emit_this_frame = 1; return; }
    g_emit_this_frame = 0;
    for (size_t i = 0; i < g_n_frames; i++) {
        if (g_frames[i] == frame) { g_emit_this_frame = 1; break; }
    }
}

void call_trace_end_frame(void)
{
    if (g_f) fflush(g_f);
}

void call_trace_shutdown(void)
{
    if (g_f) { fclose(g_f); g_f = NULL; }
}

void call_trace_enter(uint32_t ghidra_va, const void *ret_addr)
{
    if (!g_f || !g_emit_this_frame) return;

    unsigned ret_off = 0;
    if (ret_addr && g_module_base)
        ret_off = (unsigned)((const char *)ret_addr - g_module_base);

    fprintf(g_f,
            "{\"va\":%u,\"ret_va\":%u,\"frame\":%u}\n",
            (unsigned)ghidra_va, ret_off, g_cur_frame);
}
