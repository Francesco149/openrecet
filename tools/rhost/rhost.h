/* rhost — in-process retail host (Phase 0 of docs/audits/2026-10-13-methodology-reset.md).
 * Runs the user's own recettear.unpacked.exe inside this process, deterministically:
 * virtual time, deferred loader threads, fixed-address heap arena, pinned seed,
 * scripted input, hermetic file overlay. See tools/rhost/README.md. */
#pragma once
#include "retail_map.h"
#include <stdarg.h>

#define RH_MAX_WATCH 64   /* --watch VAs (portstate --va) */

#define G32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define G16(va) (*(volatile uint16_t *)(uintptr_t)(va))
#define GF(va)  (*(volatile float *)(uintptr_t)(va))

/* retail image layout (recettear.unpacked.exe, ImageBase 0x400000, relocs stripped) */
#define RT_TEXT_LO 0x401000u
#define RT_TEXT_SZ 0x11358eu
#define RT_DATA_LO 0x528000u                 /* .data (+bss) .. .data1 end */
#define RT_DATA_HI (0x964f000u + 0x8e0u)
#define RT_IMG_LO  0x400000u
#define RT_IMG_HI  0x9955000u

/* retail VAs used by the host */
#define VA_CLOCK   0x47be2fu   /* ms clock (QPC) — 1 direct call site */
#define VA_TICK    0x47be92u   /* frame pacer + input poll — 1 call site */
#define VA_INPUT   0x47b73cu   /* DInput poll → mask DAT_073dddd0 — 1 call site */
#define VA_RAND    0x5041f6u
#define DAT_RNG    0x6023a0u
#define DAT_MASK   0x73dddd0u
#define DAT_TICKGATE 0x73dfca0u
#define DAT_SCENE  0x438b1c0u
#define DAT_PLAYER_POS 0x56da1d8u

typedef struct {
    const char *exe, *gamedir, *rundir;
    uint32_t frames;            /* stop after this many ticks (0 = trace end + tail) */
    uint32_t seed; int has_seed;
    const char *trace;          /* trace.jsonl (anchor-segmented or flat) */
    const char *masks;          /* legacy "<frame> <hexmask>" script */
    const char *record;         /* realtime human play: write a flat trace.jsonl */
    const char *flatten;        /* write the replayed trace as a flat (frame-absolute) trace */
    uint32_t dump_at[16]; int n_dump;
    uint32_t shot_at[64]; int n_shot; uint32_t shot_every;
    uint32_t hash_every;        /* log state hash every N frames (0 = only at end) */
    uint32_t log_every;         /* status line every N frames */
    uint32_t tail;              /* frames after the last trace entry (300) */
    uint32_t stall_frames;      /* end a trace run whose segment has not advanced for N frames */
    int realtime;               /* pace ticks at 60 Hz wall clock (human play) */
    int quiet;                  /* window off-screen, never activated, no taskbar button */
    int no_vtime, no_defer, no_arena, no_overlay;
    int log_anchors, log_files, log_heap, log_ext;
    const char *coverage, *coverage_out;
    const char *d3d_proxy;      /* path to the v3 proxy d3d8.dll (records the D3D stream) */
    uint32_t cap_from, cap_count; int has_cap;
    uint32_t stall_ms;
    const char *hooks;          /* hook spec, see rh_hooks.c */
    const char *log_sbh;        /* diagnostic: CRT small-block-heap call log */
    uint32_t watch[RH_MAX_WATCH]; int n_watch;   /* dwords appended to each status line */
} rh_opts_t;

extern rh_opts_t O;
extern volatile uint32_t g_frame;
extern FILE *g_log;
void rh_log(const char *fmt, ...);
void rh_fatal(const char *fmt, ...);
void rh_finish(const char *why);   /* end the run now: hash, dumps, RESULT, ExitProcess */
void iat_override(const char *name, void *fn);
void *iat_lookup(const char *name);
int  redirect_calls(uint32_t va, void *hook);

/* rh_heap.c */
int  rh_heap_init(void);
void rh_heap_install(void);
int  rh_heap_ranges(uint32_t (*out)[2], int max);
void rh_heap_stats(char *buf, size_t n);
int  rh_is_det_addr(uint32_t v);   /* image / host / arena: deterministic address space */
void *rh_heap_meta_save(void);
void rh_heap_meta_restore(void *m);
void rh_heap_meta_free(void *m);

/* rh_files.c */
void rh_files_install(void);

/* rh_handles.c */
void rh_handles_install(void);
long rh_handles_live(void);

/* rh_state.c */
typedef struct { uint64_t full, semantic; uint32_t masked, ranges, bytes; } rh_hash_t;
void rh_state_hash(rh_hash_t *h);
int  rh_state_dump(const char *path);
void rh_state_volatile_add(uint32_t va);
void rh_state_ext_report(void);
int  rh_shadow(uint32_t va, const char *name, void (*port)(void), char *why, size_t whyn);

/* rh_trace.c */
void rh_trace_init(void);
uint16_t rh_trace_mask(uint32_t frame, int *have);
void rh_trace_after_tick(uint32_t frame);
int  rh_trace_done(uint32_t frame);
int  rh_trace_stalled(uint32_t frame);
void rh_trace_record(uint32_t frame, uint16_t mask);
void rh_trace_record_esc(uint32_t frame);
void rh_trace_finish(void);
const char *rh_trace_status(void);

/* rh_dinput.c */
void rh_dinput_install(void);

/* rh_shot.c */
void rh_shot(const char *tag);
void rh_shot_warm(void);   /* first PrintWindow is blank: warm the DWM surface */

/* rh_hooks.c */
void rh_hooks_install(void);
void rh_hooks_report(void);
