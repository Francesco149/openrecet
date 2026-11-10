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
    int cut;                    /* free-play capture: end at a day-boundary save / a later game start (rh_play.c) */
    const char *preamble;       /* CODE[:BANK[:TOGGLE]]: drive the title to that game start (rh_play.c) */
    int turbo;                  /* --record without realtime pacing (headless tests of the record path) */
    uint32_t live_idle;         /* --live-idle S: a live session exits after S seconds without a command (1800) */
    uint32_t live_until;        /* --live with --trace: stop the replay at this frame (--frames) */
    int live, live_port;        /* --live PORT: serve commands on 127.0.0.1:PORT (0 = pick one → <run>\\live.port); implies --headless */
    const char *snap_test;      /* --snap-test A:B[:K]: savestate self-check (rh_live.c) */
    int profile;                /* --profile FROM: sample the main thread's EIP from tick FROM */
    int d3d_probe;              /* log the real Direct3D 8's adapter answers (rh_d3dprobe.c → rh_dev_ref.h) */
    int headless;               /* fake D3D8 / DirectMusic / DirectShow / DirectInput (rh_dev.c): no GPU, no desktop */
} rh_opts_t;

extern rh_opts_t O;
extern volatile uint32_t g_frame;
extern FILE *g_log;
void rh_log(const char *fmt, ...);
void rh_fatal(const char *fmt, ...);
void rh_log_flush(void);        /* rhost.log is buffered: flush (each 1000 frames, exceptions, exit) */
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
void *rh_files_snap(void);
void rh_files_unsnap(void *s);
void rh_files_snap_free(void *s);

/* rh_handles.c */
void rh_handles_install(void);
long rh_handles_live(void);
void *rh_handles_snap(void);
void rh_handles_unsnap(void *s);

/* rh_state.c */
typedef struct { uint64_t full, semantic; uint32_t masked, ranges, bytes; } rh_hash_t;
void rh_state_hash(rh_hash_t *h);
int  rh_state_dump(const char *path);
void rh_state_volatile_add(uint32_t va);
void rh_state_ext_report(void);
uint64_t rh_state_dev_hash(void);                   /* the DEV region in use (--headless), 0 otherwise */
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
int  rh_shot_on_present(void);        /* wine: shots come from a Present hook */
void rh_shot_arm(const char *tag);   /* shoot the next Present as `tag` */

/* rh_play.c — free-play capture: cuts + the title preamble */
void rh_play_install(void);
int  rh_play_preamble_active(void);
uint16_t rh_play_preamble_mask(void);
void rh_play_after_tick(void);
int  rh_play_cut_pending(void);          /* 0, or the exit code of the pending cut (10 save / 11 start) */
const char *rh_play_cut_reason(void);
void rh_play_finish(const char *why);    /* <run>\cut.json */
const char *rh_play_status(void);
void rh_play_handover(void);             /* rhost.c: the preamble is done — realtime + real input from here */

/* rh_dev.c — --headless devices; their memory is the DEV region (carved from the arena's
 * address range: retail's large-block heap peaks at 50 MB of the 256 MB left to it) */
#define RH_DEV_LO 0x40000000u
#define RH_DEV_HI 0x50000000u
void rh_dev_install(void);
void rh_dev_report(void);
int  rh_dev_range(uint32_t *lo, uint32_t *hi);   /* [DEV_LO, top) in use, page-rounded; 0 = not headless */
uint32_t rh_dev_committed(void);

/* rh_snap.c — in-process savestates (rh_dev.c's DEV region makes the state complete) */
void rh_snap_install(void);
void rh_snap_register(const char *name, void *p, size_t n);   /* a plain host-state block saved with every savestate */
void rh_snap_committed(uint32_t lo, uint32_t hi);   /* a deterministic region committed fresh pages */
int  rh_snap_fault(EXCEPTION_POINTERS *e);          /* 1 = a write-track fault, handled: continue */
void rh_snap_touch(const void *p, size_t n);        /* before the system writes [p, p+n) of retail memory */
int  rh_snap_anchor(void);                          /* base copy + write tracking; drops every savestate */
int  rh_snap_take(void);                            /* → id >= 1, 0 = not anchored / pool exhausted */
int  rh_snap_restore(int id);
void rh_snap_free(int id);
int  rh_snap_frame(int id);
long rh_snap_diff(int id, uint32_t lo, uint32_t hi, void (*cb)(uint32_t va, uint32_t was, uint32_t now, void *u), void *u);
int  rh_snap_anchored(void);
uint32_t rh_snap_dirty(void);
void rh_snap_stats(char *buf, size_t n);

/* rhost.c: the tick hook's coroutine */
void rh_step(void);                                 /* host stack: run one whole tick, boundary to boundary */
void rh_game_stack(uint32_t *lo, uint32_t *n);      /* retail's live main-stack bytes at a boundary */

/* rh_live.c — --live PORT: the command server (step / read / savestate / search) + --snap-test */
void rh_live_install(void);
void rh_live_main(void);
uint16_t rh_live_mask(uint32_t frame, int *have);
void rh_live_note_esc(uint32_t frame);
void rh_text_begin(void);
void *rh_text_snap(void);
void rh_text_unsnap(void *blob);
void rh_live_note_rngseed(uint32_t frame, uint32_t v);
void rh_snap_selftest(void);
const char *rh_snap_test_status(void);
void *rh_trace_snap(void);
void rh_trace_unsnap(void *blob);
void rh_trace_post_esc(void);                       /* rh_trace.c: the ESC key press, as a trace's {"esc"} op posts it */

/* rh_d3dprobe.c */
void rh_d3dprobe_install(void);
void rh_d3dprobe_device(void);

/* rh_hooks.c */
void rh_hooks_install(void);
void rh_hooks_report(void);
