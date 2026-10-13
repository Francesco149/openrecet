/* P3 = P2 + anchor-segmented scenario replay + live haggle_decide shadow.
 *   TRACE=<win path to tests/scenarios/<s>/trace.jsonl>  (save.dat must already be
 *     the scenario's save in cwd)  — parser/state machine = src/input_segtrace.c,
 *     anchors = C port of the Frida agent's anchorTick (subset, see anchors_tick)
 *   HAGGLE_SHADOW=1  every retail FUN_00460672 call also runs the port's
 *     haggle_decide built -std=c11 and -std=gnu11; logs disagreements
 *   LOG_ANCHORS=1    log anchors + deferred-worker completions
 *   LOAD_PINS=1      approximate {primary,cs,tut}loadpin (worker held N frames
 *     from CREATION — not the Frida arm point; traces needing exact pins stall)
 *   TRACE_TAIL=n     frames to keep running after the last trace entry (300)
 *   {esc} ops post WM_KEYDOWN/UP VK_ESCAPE to the game window.
 * Build: tools/oracle/build.sh.
 *
 * P2 — run RETAIL in-process: map recettear.unpacked.exe at 0x400000 inside
 * this host, patch IAT + direct call sites, then jump to retail's own CRT
 * entry. The host owns the process, so hooks are plain call-site rewrites
 * (no DLL injection, no Frida) and memory snapshots are a memcpy.
 *
 *   p2_run.exe <retail.exe> <frames> <snap_frame> <snap_out> [inputs.txt]
 *
 * Hooks (call-site redirection of every direct E8 call to the target):
 *   0x47be2f clock  -> virtual ms clock (turbo: +17 ms per tick, like Frida)
 *   0x47be92 tick   -> frame counter, snapshot at <snap_frame>, exit at <frames>
 *   0x47b73c input  -> after the real poll, override mask DAT_073dddd0 from
 *                      a "frame mask" script; force tick gate DAT_073dfca0=1.
 *   0x4536cb sim_a / 0x49966a sim_b -> optional shadow (run twice on the same
 *                      .data state, compare) for determinism checks.
 *
 * inputs.txt: lines "<frame> <hexmask>" (sticky). 0x10=A, 0x1/0x2/0x4/0x8=dirs.
 *
 * Env knobs (PROTOTYPE, see docs/findings/in-process-retail-host.md):
 *   DEFER_THREADS=1  loader workers created suspended, run to completion at
 *                    the next tick boundary -> deterministic loads (WORKS)
 *   VTIME=1          timeGetTime/GetTickCount/QPC/GetLocalTime/... virtual
 *   RNG_SEED=n       pin DAT_006023a0 at frame 0 (like {rngseed})
 *   SHADOW_FROM=f SHADOW_EVERY=k   shadow sim_a/sim_b (skipped while a
 *                    retail worker thread is live)
 *   REPLACE_RAND=1|2 redirect rand (0x5041f6) call sites to port_rand /
 *                    deliberately-buggy port_rand_buggy
 *   STALL_END=s      sleep s seconds at the end (screenshot window)
 *   SYNC_THREADS=1   run worker inside CreateThread -> BROKEN (ordering)
 *   YIELD_LOADS=1    Sleep(1) per tick while a worker is live -> hangs on Win
 *   LOG_SLEEP=1      log Sleep/WaitForSingleObject callers
 *
 * Build (host must not overlap 0x400000.. and must not be ASLR-relocated):
 *   i686-w64-mingw32-gcc -std=gnu11 -O2 -Wl,--image-base,0x200000 \
 *     -Wl,--large-address-aware -Wl,--disable-dynamicbase \
 *     -o p2_run.exe p2_run.c -static-libgcc */
#include "retail_map.h"
#include <float.h>
#include "input_segtrace.h"

/* ---- anchor-segmented scenario replay (tests/scenarios/<s>/trace.jsonl) ----
 * Parser + segment state machine = the port's own src/input_segtrace.c (pure C).
 * Anchor predicates = C port of tools/frida/openrecet-agent.js anchorTick(),
 * sampled at end of each tick (agent samples at Present = end of frame). */
static struct input_segtrace g_seg; static int g_seg_on;
static struct { int init, scene, loading, reveal, revflag, linep, conv, blink, cc08, pause, tpick,
                 fx, savep, encp, optp, itemp, tset, tenc, trec, tsurv; } A;
static int fx_alpha(int dlg)   /* agent anchorFxAlpha: max standee alpha, slots 2..31 */
{
    if (!dlg) return 0;
    int maxa = 0;
    for (int i = 2; i <= 31; i++) {
        uint32_t sb = 0x073a3e70u + i * 0x70u;
        if (*(volatile int32_t *)(uintptr_t)(sb + 0x2c) == 0) continue;
        int a = (int)*(volatile float *)(uintptr_t)(sb + 0x48);
        if (a < 0) a = 0; else if (a > 255) a = 255;
        if (a > maxa) maxa = a;
    }
    return maxa;
}
static long g_anchor_count;
static HWND g_game_hwnd;
static BOOL CALLBACK find_wnd(HWND h, LPARAM lp)
{ DWORD pid; GetWindowThreadProcessId(h, &pid); if (pid == GetCurrentProcessId() && IsWindowVisible(h)) { *(HWND *)lp = h; return FALSE; } return TRUE; }
static long g_esc_count;
static void seg_esc(void *u)
{
    (void)u;
    if (!g_game_hwnd) EnumWindows(find_wnd, (LPARAM)&g_game_hwnd);
    if (g_game_hwnd) { PostMessageA(g_game_hwnd, WM_KEYDOWN, VK_ESCAPE, 0x00010001); PostMessageA(g_game_hwnd, WM_KEYUP, VK_ESCAPE, 0xC0010001); }
    g_esc_count++;
}
static uint32_t g_trace_done_at;
static uint32_t g_pin_primary, g_pin_cs, g_pin_tut;
static void seg_tutpin(uint32_t v, void *u) { (void)u; if (getenv("LOAD_PINS")) g_pin_tut = v; }
static void seg_rngseed(uint32_t v, void *u) { (void)u; *(volatile uint32_t *)0x6023a0 = v; }
static void anchor_fire(const char *name, uint32_t frame)
{
    g_anchor_count++;
    if (getenv("LOG_ANCHORS")) { printf("anchor %s @%u rng=%08x\n", name, frame, *(volatile uint32_t *)0x6023a0); fflush(stdout); }
    if (g_seg_on) input_segtrace_on_anchor(&g_seg, name, frame);
}
static void anchors_tick(uint32_t frame)
{
    #define R(va) (*(volatile int32_t *)(uintptr_t)(va))
    int scene = R(0x438b1c0);
    int loading = R(0x6a49958) != 0 || R(0x6a49960) != 0;
    int dlg = R(0x438b1c8) == 1;
    int reveal = R(0x73a3e00), revflag = R(0x73a3e04);
    int linep = dlg && R(0x73a6a38) >= 0;
    int conv = R(0x56daafc), blink = conv == 6 && R(0x56daaf8) == 1;
    int cc08 = R(0x438cc08), pause = R(0x438b150) != 0;
    int tsub = (scene == 0 && R(0x9643520) == 10) ? R(0x9643524) : 0;
    int tpick = tsub == 1, tset = tsub == 2, tenc = tsub == 3, trec = tsub == 4;
    int tsurv = scene == 0 && R(0x9643520) == 0 && R(0x9643524) == 0 && R(0x9643550) == 8;
    int fx = fx_alpha(dlg);
    int savep = 0, encp = 0, optp = 0, itemp = 0;
    if (scene == 9 && R(0x74b2880) == 10) {
        int sel = R(0x74b2878);
        if (sel >= 0 && sel < 8) { int t = R(0x74b2844 + sel * 4); savep = t == 3; encp = t == 6; optp = t == 2; itemp = t == 1; }
    }
    if (!A.init) {
        A.init = 1; anchor_fire("BOOT", frame);
    } else {
        if (A.scene == 0 && scene == 1) anchor_fire("NEW_GAME", frame);
        if (!A.loading && loading) anchor_fire("LOADING_START", frame);
        if (A.loading && !loading) anchor_fire("LOADING_END", frame);
        if (!(A.scene == 1 && !A.loading) && (scene == 1 && !loading)) anchor_fire("HOUSE_FREEROAM", frame);
        if (A.scene != 6 && scene == 6) anchor_fire("MARKET_ENTER", frame);
        if (A.cc08 != 4 && cc08 == 4) anchor_fire("CUSTOMER_SERVICE_ENTER", frame);
        if (dlg && reveal == 1 && A.reveal != 1) anchor_fire("TEXT_ANIM_START", frame);
        if (dlg && revflag != 0 && A.revflag == 0) anchor_fire("TEXT_ANIM_END", frame);
        if (A.linep && !linep) anchor_fire("DLG_LINE_CLEAR", frame);
        if (!A.linep && linep) anchor_fire("DLG_LINE_SHOW", frame);
        if (A.conv != 6 && conv == 6) anchor_fire("CONV_POSE_START", frame);
        if (A.conv == 6 && conv != 6) anchor_fire("CONV_POSE_END", frame);
        if (!A.blink && blink) anchor_fire("CONV_POSE_BLINK", frame);
        if (!A.pause && pause) anchor_fire("PAUSE_OPEN", frame);
        if (A.pause && !pause) anchor_fire("PAUSE_CLOSE", frame);
        if (A.loading && !loading && scene == 9) anchor_fire("PAUSE_READY", frame);
        if (A.scene != 0 && scene == 0) anchor_fire("TITLE_RETURN", frame);
        if (A.fx == 0 && fx > 0) anchor_fire("EXTRA_SPRITE_START", frame);
        if (fx >= 255 && A.fx < 255) anchor_fire("EXTRA_SPRITE_FADED_IN", frame);
        if (A.fx >= 255 && fx < 255 && fx > 0) anchor_fire("EXTRA_SPRITE_FADEOUT", frame);
        if (A.fx > 0 && fx == 0) anchor_fire("EXTRA_SPRITE_END", frame);
        if (!A.savep && savep) anchor_fire("SAVE_PICKER_READY", frame);
        if (!A.encp && encp) anchor_fire("ENCYCLOPEDIA_READY", frame);
        if (!A.optp && optp) anchor_fire("OPTIONS_READY", frame);
        if (!A.itemp && itemp) anchor_fire("ITEMS_READY", frame);
        if (!A.tpick && tpick) anchor_fire("TITLE_PICKER_READY", frame);
        if (!A.tset && tset) anchor_fire("TITLE_SETTINGS_READY", frame);
        if (!A.tenc && tenc) anchor_fire("TITLE_ENCYCLOPEDIA_READY", frame);
        if (!A.trec && trec) anchor_fire("TITLE_RECORDS_READY", frame);
        if (!A.tsurv && tsurv) anchor_fire("TITLE_SURVIVAL_READY", frame);
    }
    A.scene = scene; A.loading = loading; A.reveal = reveal; A.revflag = revflag; A.linep = linep;
    A.conv = conv; A.blink = blink; A.cc08 = cc08; A.pause = pause; A.tpick = tpick;
    A.fx = fx; A.savep = savep; A.encp = encp; A.optp = optp; A.itemp = itemp;
    A.tset = tset; A.tenc = tenc; A.trec = trec; A.tsurv = tsurv;
    #undef R
}

/* ---- haggle_decide shadow: retail FUN_00460672 vs port (c11 + gnu11 builds) ---- */
extern int32_t haggle_decide(int32_t player_ask, int32_t accept_ref);        /* -std=c11 (shipping flags) */
extern int32_t haggle_decide_gnu11(int32_t player_ask, int32_t accept_ref);  /* same source, -std=gnu11 */
static long g_hag_calls, g_hag_mis_c11, g_hag_mis_gnu11;
static int32_t __cdecl hook_haggle_decide(void)
{
    int32_t ask = *(volatile int32_t *)0x5c6bb8, ref = *(volatile int32_t *)0x730b588;
    int32_t r = ((int32_t (__cdecl *)(void))0x460672)();
    int32_t a = haggle_decide(ask, ref), b = haggle_decide_gnu11(ask, ref);
    g_hag_calls++; if (a != r) g_hag_mis_c11++; if (b != r) g_hag_mis_gnu11++;
    if (g_hag_calls <= 40 || a != r || b != r)
        printf("  haggle_decide #%ld ref=%d ask=%d retail=%d port_c11=%d%s port_gnu11=%d%s\n", g_hag_calls, ref, ask, r,
               a, a != r ? "(DIVERGE)" : "", b, b != r ? "(DIVERGE)" : "");
    return r;                                   /* retail stays authoritative */
}

RETAIL_HOLE_DEFINE

#define G32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define G16(va) (*(volatile uint16_t *)(uintptr_t)(va))

static char g_gamedir[MAX_PATH];
static char g_snap_out[MAX_PATH];
static uint32_t g_vclock, g_frame, g_frames_max, g_snap_frame;
static uint32_t g_frame_now(void) { return g_frame; }
static struct { uint32_t frame; uint16_t mask; } g_inputs[4096];
static int g_ninputs, g_icur;
static uint16_t g_mask;
static FILE *g_log;

/* ---- retail thread tracking: shadow only while no retail worker is alive ---- */
static volatile LONG g_live_threads, g_threads_created;
/* Deferred loader workers. Each runs to completion (main thread blocked) at a
 * deterministic frame: creation+1, or creation+N under a trace load pin
 * ({primaryloadpin} 0x45293d, {csloadpin} 0x452ae8/0x452b13, {tutloadpin} 0x452aab). */
static struct { HANDLE h; uint32_t fn, frame; } g_deferred[16]; static int g_ndeferred;
static uint32_t pin_for(uint32_t fn)
{
    uint32_t n = 0;
    if (fn == 0x45293d) n = g_pin_primary;
    else if (fn == 0x452ae8 || fn == 0x452b13) n = g_pin_cs;
    else if (fn == 0x452aab) n = g_pin_tut;
    return n > 1 ? n : 1;
}
static uint32_t g_frame_now(void);
static void run_deferred_threads(void)
{
    int k = 0;
    for (int i = 0; i < g_ndeferred; i++) {
        if (g_frame_now() < g_deferred[i].frame + pin_for(g_deferred[i].fn)) { g_deferred[k++] = g_deferred[i]; continue; }
        ResumeThread(g_deferred[i].h);
        DWORD r = WaitForSingleObject(g_deferred[i].h, 60000);
        if (getenv("LOG_ANCHORS")) { fprintf(stdout, "deferred worker %08x (created f%u, pin %u) completed at f%u (wait=%lu)\n",
                g_deferred[i].fn, g_deferred[i].frame, pin_for(g_deferred[i].fn), g_frame_now(), r); fflush(stdout); }
        CloseHandle(g_deferred[i].h);
    }
    g_ndeferred = k;
}
typedef struct { LPTHREAD_START_ROUTINE fn; LPVOID arg; } thr_wrap_t;
static DWORD WINAPI thread_wrapper(LPVOID p)
{
    thr_wrap_t w = *(thr_wrap_t *)p; free(p);
    /* retail thread procs end in a plain `ret` (caller-cleans): call as cdecl */
    DWORD r = ((DWORD (__cdecl *)(LPVOID))w.fn)(w.arg);
    InterlockedDecrement(&g_live_threads);
    return r;
}
static HANDLE WINAPI my_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn,
                                     LPVOID arg, DWORD fl, LPDWORD tid)
{
    InterlockedIncrement(&g_threads_created);
    if (getenv("SYNC_THREADS")) {               /* deterministic loads: run the worker to completion now */
        fn(arg);
        if (tid) *tid = 0;
        return NULL;      /* creator stores this into DAT_06a49950 (busy) -> must be 0 */
    }
    thr_wrap_t *w = malloc(sizeof *w); w->fn = fn; w->arg = arg;
    InterlockedIncrement(&g_live_threads);
    if (getenv("DEFER_THREADS")) {             /* deterministic: run to completion at next tick boundary */
        HANDLE h = CreateThread(sa, st, thread_wrapper, w, fl | CREATE_SUSPENDED, tid);
        if (tid && getenv("VTIME")) *tid = 0x1000 + g_threads_created;   /* deterministic id seen by the game */
        HANDLE dup; DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
        g_deferred[g_ndeferred].h = dup; g_deferred[g_ndeferred].fn = (uint32_t)(uintptr_t)fn;
        g_deferred[g_ndeferred].frame = g_frame_now(); g_ndeferred++;
        return h;
    }
    return CreateThread(sa, st, thread_wrapper, w, fl, tid);
}

/* ---- shadow execution: run a retail fn twice on the identical .data state ---- */
#define D_LO 0x528000u
#define D_HI (0x964f000u + 0x8e0u)
static uint8_t *g_pre, *g_post;
static uint32_t g_shadow_from = 0xffffffff, g_shadow_every = 1;
static long g_sh_runs, g_sh_div;
typedef void (__cdecl *vfn)(void);
static void shadow_call(uint32_t va, const char *name)
{
    if (!g_pre) { g_pre = malloc(D_HI - D_LO); g_post = malloc(D_HI - D_LO); }
    uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
    LARGE_INTEGER t0, t1, fq; QueryPerformanceCounter(&t0);
    memcpy(g_pre, (void *)D_LO, D_HI - D_LO);
    ((vfn)(uintptr_t)va)();
    memcpy(g_post, (void *)D_LO, D_HI - D_LO);
    memcpy((void *)D_LO, g_pre, D_HI - D_LO);
    __asm__ volatile("fldcw %0" : : "m"(cw));
    ((vfn)(uintptr_t)va)();
    long dpages = 0; uint32_t first = 0, changed = 0;
    for (uint32_t off = 0; off < D_HI - D_LO; off += 4096) {
        uint32_t n = (D_HI - D_LO - off) < 4096 ? (D_HI - D_LO - off) : 4096;
        if (memcmp(g_post + off, (void *)(D_LO + off), n)) { if (!dpages++) first = D_LO + off; }
        if (memcmp(g_pre + off, g_post + off, n)) changed++;
    }
    QueryPerformanceCounter(&t1); QueryPerformanceFrequency(&fq);
    g_sh_runs++; if (dpages) g_sh_div++;
    if (dpages || g_frame % 100 == 0)
        fprintf(g_log, "  shadow %s f=%u: pages written=%u, run1!=run2 pages=%ld first=%08x (%.1f ms)\n",
                name, g_frame, changed, dpages, first, (t1.QuadPart - t0.QuadPart) * 1000.0 / fq.QuadPart);
}
static void __cdecl hook_sim_a(void)
{ if (g_frame >= g_shadow_from && g_frame % g_shadow_every == 0 && !g_live_threads) shadow_call(0x4536cb, "sim_a"); else ((vfn)0x4536cb)(); }
static void __cdecl hook_sim_b(void)
{ if (g_frame >= g_shadow_from && g_frame % g_shadow_every == 0 && !g_live_threads) shadow_call(0x49966a, "sim_b"); else ((vfn)0x49966a)(); }

static long g_sleep_logs;
static void WINAPI my_Sleep(DWORD ms)
{
    if (getenv("LOG_SLEEP") && g_sleep_logs < 12) {
        g_sleep_logs++;
        fprintf(g_log, "Sleep(%lu) from %p tid=%lu frame=%u\n", ms, __builtin_return_address(0), GetCurrentThreadId(), g_frame);
        fflush(g_log);
    }
    Sleep(ms);
}
static DWORD WINAPI my_WaitForSingleObject(HANDLE h, DWORD ms)
{
    if (getenv("LOG_SLEEP")) { fprintf(g_log, "WaitForSingleObject(%p,%lu) from %p frame=%u\n", h, ms, __builtin_return_address(0), g_frame); fflush(g_log); }
    return WaitForSingleObject(h, ms);
}

/* ---- virtual time: every wall-clock import derives from the frame counter ---- */
static DWORD v_ms(void) { return 1000000u + g_frame * 17u; }
static DWORD WINAPI my_timeGetTime(void) { return v_ms(); }
static DWORD WINAPI my_GetTickCount(void) { return v_ms(); }
static BOOL WINAPI my_QPC(LARGE_INTEGER *p) { p->QuadPart = (LONGLONG)v_ms() * 1000; return TRUE; }
static BOOL WINAPI my_QPF(LARGE_INTEGER *p) { p->QuadPart = 1000000; return TRUE; }
static void WINAPI my_GetLocalTime(SYSTEMTIME *t)
{ memset(t, 0, sizeof *t); t->wYear = 2010; t->wMonth = 1; t->wDay = 1; t->wHour = 12; t->wSecond = (WORD)((v_ms() / 1000) % 60); t->wMinute = (WORD)((v_ms() / 60000) % 60); }
static void WINAPI my_GetSystemTime(SYSTEMTIME *t) { my_GetLocalTime(t); }
static void vtime_install(void);

/* ---- IAT overrides: make the guest believe it is the main module ---- */
static HMODULE WINAPI my_GetModuleHandleA(LPCSTR n)
{ return n ? GetModuleHandleA(n) : (HMODULE)0x400000; }
static DWORD WINAPI my_GetModuleFileNameA(HMODULE h, LPSTR buf, DWORD n)
{
    if (!h || h == (HMODULE)0x400000) {
        snprintf(buf, n, "%s\\recettear.exe", g_gamedir);
        return (DWORD)strlen(buf);
    }
    return GetModuleFileNameA(h, buf, n);
}

static void iat_override(const char *name, void *fn)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(0x400000 + ((IMAGE_DOS_HEADER *)0x400000)->e_lfanew);
    IMAGE_DATA_DIRECTORY *idd = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(0x400000 + idd->VirtualAddress); d->Name; d++) {
        IMAGE_THUNK_DATA32 *ilt = (void *)(0x400000 + d->OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *iat = (void *)(0x400000 + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++)
            if (!(ilt->u1.Ordinal & IMAGE_ORDINAL_FLAG32) &&
                !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(0x400000 + ilt->u1.AddressOfData))->Name, name))
                iat->u1.Function = (uint32_t)(uintptr_t)fn;
    }
}

/* ---- call-site redirection: rewrite every `E8 rel32` that targets `va` ---- */
static int redirect_calls(uint32_t va, void *hook)
{
    int n = 0;
    uint8_t *t0 = (uint8_t *)0x401000, *t1 = t0 + 0x11358e - 5;
    for (uint8_t *p = t0; p < t1; p++) {
        if (*p != 0xE8) continue;
        uint32_t tgt = (uint32_t)(uintptr_t)p + 5 + *(int32_t *)(p + 1);
        if (tgt != va) continue;
        *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hook - ((uint32_t)(uintptr_t)p + 5));
        n++;
    }
    return n;
}

static void snapshot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t lo = 0x528000, hi = 0x964f000 + 0x8e0;      /* .data(+bss) .. .data1 */
    unsigned int cw = _controlfp(0, 0);
    fwrite(&lo, 4, 1, f); fwrite(&hi, 4, 1, f); fwrite(&cw, 4, 1, f); fwrite(&g_frame, 4, 1, f);
    fwrite((void *)(uintptr_t)lo, 1, hi - lo, f);
    fclose(f);
    fprintf(g_log, "snapshot frame=%u cw=%08x -> %s\n", g_frame, cw, path);
}

static uint32_t __cdecl hook_clock(void) { return g_vclock; }

static void __cdecl hook_input(void)
{
    ((void (__cdecl *)(void))0x47b73c)();
    if (g_seg_on) g_mask = input_segtrace_tick(&g_seg, g_frame, NULL, NULL);
    else while (g_icur < g_ninputs && g_inputs[g_icur].frame <= g_frame) g_mask = g_inputs[g_icur++].mask;
    G16(0x73dddd0) = g_mask;
    G32(0x73dfca0) = 1;                       /* tick gate: never park in WaitMessage */
}

static uint64_t data_hash(void)
{
    uint64_t h = 1469598103934665603ull;
    const uint64_t *p = (const uint64_t *)D_LO, *e = (const uint64_t *)(D_HI & ~7u);
    for (; p < e; p++) { h ^= *p; h *= 1099511628211ull; }
    return h;
}
/* semantic hash: skip dwords that look like heap/COM pointers (outside the retail image) */
static uint64_t data_hash_nonptr(void)
{
    uint64_t h = 1469598103934665603ull;
    for (const uint32_t *p = (const uint32_t *)D_LO; p < (const uint32_t *)(D_HI & ~3u); p++) {
        uint32_t v = *p;
        if (v >= 0x00a00000u && !(v >= 0x00400000u && v < 0x09955000u)) v = 0x9e3779b9u;
        h ^= v; h *= 1099511628211ull;
    }
    return h;
}
/* port rand (src/rng.c semantics) operating on retail's seed @0x6023a0 */
static uint32_t __cdecl port_rand(void)
{
    uint32_t s = G32(0x6023a0) * 0x343fdu + 0x269ec3u;
    G32(0x6023a0) = s;
    return (s >> 16) & 0x7fffu;
}
static uint32_t __cdecl port_rand_buggy(void)   /* deliberately wrong: drift on odd seeds */
{
    uint32_t s = G32(0x6023a0) * 0x343fdu + 0x269ec3u + (G32(0x6023a0) & 1);
    G32(0x6023a0) = s;
    return (s >> 16) & 0x7fffu;
}
static void __cdecl hook_tick(void)
{
    if (g_frame == 0 && getenv("RNG_SEED")) G32(0x6023a0) = strtoul(getenv("RNG_SEED"), 0, 0);
    g_vclock += 17;
    if (g_ndeferred) run_deferred_threads();
    if (g_live_threads && getenv("YIELD_LOADS")) Sleep(1);
    if (g_frame == g_snap_frame) snapshot(g_snap_out);
    if (g_frame % 100 == 0) {
        uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
        float *pp = (float *)0x56da1d8;
        fprintf(g_log, "frame %u scene=%d rng=%08x mask=%04x x87cw=%04x threads live=%ld created=%ld player=(%.2f,%.2f,%.2f)\n", g_frame,
                (int)G32(0x438b1c0), G32(0x6023a0), g_mask, cw, g_live_threads, g_threads_created, pp[0], pp[1], pp[2]);
        fflush(g_log);
    }
    if (g_frame >= g_frames_max && getenv("STALL_END")) { fprintf(g_log, "stalling for screenshot\n"); fflush(g_log); Sleep(1000 * atoi(getenv("STALL_END"))); }
    if (g_frame >= g_frames_max) { fprintf(g_log, "done at frame %u; seg %u/%u (last anchor %s) esc=%ld complete=%d; anchors=%ld haggle_calls=%ld mis_c11=%ld mis_gnu11=%ld; shadow runs=%ld divergent=%ld; data_hash=%016llx semantic_hash=%016llx rng=%08x\n", g_frame, (unsigned)g_seg.cur_seg, (unsigned)g_seg.n_segs, g_seg.base_anchor, g_esc_count, g_trace_done_at != 0, g_anchor_count, g_hag_calls, g_hag_mis_c11, g_hag_mis_gnu11, g_sh_runs, g_sh_div, (unsigned long long)data_hash(), (unsigned long long)data_hash_nonptr(), G32(0x6023a0)); fflush(g_log); ExitProcess(0); }
    g_frame++;
    ((void (__cdecl *)(void))0x47be92)();
    anchors_tick(g_frame);
    if (g_seg_on && !g_trace_done_at && g_seg.n_segs && g_seg.cur_seg + 1 >= g_seg.n_segs &&
        g_seg.cur_entry >= g_seg.segs[g_seg.cur_seg].n_entries)
        g_trace_done_at = g_frame + (getenv("TRACE_TAIL") ? strtoul(getenv("TRACE_TAIL"), 0, 0) : 300);
    if (g_trace_done_at && g_frame >= g_trace_done_at) g_frames_max = g_frame;   /* finish next tick */
}

static void vtime_install(void)
{
    iat_override("timeGetTime", my_timeGetTime);
    iat_override("GetTickCount", my_GetTickCount);
    iat_override("QueryPerformanceCounter", my_QPC);
    iat_override("QueryPerformanceFrequency", my_QPF);
    iat_override("GetLocalTime", my_GetLocalTime);
    iat_override("GetSystemTime", my_GetSystemTime);
}

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: p2_run retail.exe frames snap_frame snap_out [inputs]\n"); return 2; }
    g_log = stdout;
    g_frames_max = strtoul(argv[2], 0, 0);
    g_snap_frame = strtoul(argv[3], 0, 0);
    snprintf(g_snap_out, sizeof g_snap_out, "%s", argv[4]);
    if (argc > 5) {
        FILE *f = fopen(argv[5], "r");
        unsigned fr, m;
        while (f && g_ninputs < 4096 && fscanf(f, "%u %x", &fr, &m) == 2) {
            g_inputs[g_ninputs].frame = fr; g_inputs[g_ninputs].mask = (uint16_t)m; g_ninputs++;
        }
        if (f) fclose(f);
    }
    GetCurrentDirectoryA(sizeof g_gamedir, g_gamedir);

    retail_image_t img;
    if (retail_map(argv[1], &img, 1)) return 1;
    iat_override("GetModuleHandleA", my_GetModuleHandleA);
    iat_override("GetModuleFileNameA", my_GetModuleFileNameA);
    iat_override("CreateThread", my_CreateThread);
    if (getenv("VTIME")) vtime_install();
    iat_override("Sleep", my_Sleep);
    iat_override("WaitForSingleObject", my_WaitForSingleObject);
    int nc = redirect_calls(0x47be2f, hook_clock);
    int nt = redirect_calls(0x47be92, hook_tick);
    int ni = redirect_calls(0x47b73c, hook_input);
    int na = redirect_calls(0x4536cb, hook_sim_a), nb = redirect_calls(0x49966a, hook_sim_b);
    if (getenv("REPLACE_RAND")) fprintf(g_log, "REPLACE rand: %d call sites -> port_rand%s\n",
        redirect_calls(0x5041f6, atoi(getenv("REPLACE_RAND")) == 2 ? (void *)port_rand_buggy : (void *)port_rand),
        atoi(getenv("REPLACE_RAND")) == 2 ? "_buggy" : "");
    if (getenv("TRACE")) {
        if (!input_segtrace_load(getenv("TRACE"), &g_seg)) { fprintf(g_log, "TRACE parse failed\n"); return 3; }
        input_segtrace_set_rngseed_cb(&g_seg, seg_rngseed, NULL);
        input_segtrace_set_esc_cb(&g_seg, seg_esc, NULL);
        input_segtrace_set_tutloadpin_cb(&g_seg, seg_tutpin, NULL);
        if (getenv("LOAD_PINS")) {   /* opt-in: approximates Frida pins from worker creation (not exact) */
            if (g_seg.has_csloadpin) g_pin_cs = g_seg.csloadpin;
            if (g_seg.has_primaryloadpin) g_pin_primary = g_seg.primaryloadpin;
        }
        fprintf(g_log, "load pins: primary=%u cs=%u (tut via segments)\n", g_pin_primary, g_pin_cs);
        g_seg_on = 1; fprintf(g_log, "TRACE %s loaded\n", getenv("TRACE"));
    }
    if (getenv("HAGGLE_SHADOW")) fprintf(g_log, "haggle_decide shadow: %d call sites\n", redirect_calls(0x460672, hook_haggle_decide));
    if (getenv("SHADOW_FROM")) g_shadow_from = strtoul(getenv("SHADOW_FROM"), 0, 0);
    if (getenv("SHADOW_EVERY")) g_shadow_every = strtoul(getenv("SHADOW_EVERY"), 0, 0);
    fprintf(g_log, "sim_a sites=%d sim_b sites=%d shadow_from=%u every=%u\n", na, nb, g_shadow_from, g_shadow_every);
    fprintf(g_log, "mapped; imports %d/%d; call sites clock=%d tick=%d input=%d; entry=%08x\n",
            img.n_imports_resolved, img.n_imports_stubbed, nc, nt, ni, img.entry);
    fflush(g_log);
    ((void (__cdecl *)(void))(uintptr_t)img.entry)();   /* WinMainCRTStartup: never returns */
    return 0;
}
