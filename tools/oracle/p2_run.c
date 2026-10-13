/* P2 — run RETAIL in-process: map recettear.unpacked.exe at 0x400000 inside
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

RETAIL_HOLE_DEFINE

#define G32(va) (*(volatile uint32_t *)(uintptr_t)(va))
#define G16(va) (*(volatile uint16_t *)(uintptr_t)(va))

static char g_gamedir[MAX_PATH];
static char g_snap_out[MAX_PATH];
static uint32_t g_vclock, g_frame, g_frames_max, g_snap_frame;
static struct { uint32_t frame; uint16_t mask; } g_inputs[4096];
static int g_ninputs, g_icur;
static uint16_t g_mask;
static FILE *g_log;

/* ---- retail thread tracking: shadow only while no retail worker is alive ---- */
static volatile LONG g_live_threads, g_threads_created;
static HANDLE g_deferred[16]; static int g_ndeferred;
static void run_deferred_threads(void)
{
    for (int i = 0; i < g_ndeferred; i++) {
        ResumeThread(g_deferred[i]);
        DWORD r = WaitForSingleObject(g_deferred[i], 60000);
        fprintf(stdout, "deferred worker %d ran to completion (wait=%lu)\n", i, r); fflush(stdout);
        CloseHandle(g_deferred[i]);
    }
    g_ndeferred = 0;
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
        g_deferred[g_ndeferred++] = dup;
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
    while (g_icur < g_ninputs && g_inputs[g_icur].frame <= g_frame) g_mask = g_inputs[g_icur++].mask;
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
        if ((uintptr_t)p == 0x73dfc7cu) v = 0;   /* HWND/GDI handle: differs per run on Windows */
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
    if (g_frame >= g_frames_max) { fprintf(g_log, "done at frame %u; shadow runs=%ld divergent=%ld; data_hash=%016llx semantic_hash=%016llx rng=%08x\n", g_frame, g_sh_runs, g_sh_div, (unsigned long long)data_hash(), (unsigned long long)data_hash_nonptr(), G32(0x6023a0)); fflush(g_log); ExitProcess(0); }
    g_frame++;
    ((void (__cdecl *)(void))0x47be92)();
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
    if (getenv("SHADOW_FROM")) g_shadow_from = strtoul(getenv("SHADOW_FROM"), 0, 0);
    if (getenv("SHADOW_EVERY")) g_shadow_every = strtoul(getenv("SHADOW_EVERY"), 0, 0);
    fprintf(g_log, "sim_a sites=%d sim_b sites=%d shadow_from=%u every=%u\n", na, nb, g_shadow_from, g_shadow_every);
    fprintf(g_log, "mapped; imports %d/%d; call sites clock=%d tick=%d input=%d; entry=%08x\n",
            img.n_imports_resolved, img.n_imports_stubbed, nc, nt, ni, img.entry);
    fflush(g_log);
    ((void (__cdecl *)(void))(uintptr_t)img.entry)();   /* WinMainCRTStartup: never returns */
    return 0;
}
