/* rhost — run retail Recettear inside this process, deterministically.
 * Productized from tools/oracle/p2_run.c + p3_run.c (2026-10-13 prototype).
 *
 *   rhost.exe --exe recettear.unpacked.exe --game <gamedir> --run <rundir> [options]
 *
 * Determinism (all ON by default; --no-* to disable one for experiments):
 *   vtime   timeGetTime/GetTickCount/QPC/QPF/GetLocalTime/GetSystemTime → frame clock
 *   defer   retail CreateThread workers start suspended and run to completion at the
 *           next tick boundary (every load = 1 frame; no CreateThread race)
 *   arena   retail heap = fixed-address deterministic allocator (rh_heap.c)
 *   overlay game dir read-only; writes go copy-on-write to the run dir (rh_files.c)
 *   seed    DAT_006023a0 = --seed (default 19937) at frame 0
 *   input   the real DInput poll is NOT called; slot masks come only from the script
 * Options:
 *   --frames N        stop after N ticks (default: trace end + --tail, else 3000)
 *   --trace F         tests/scenarios/<s>/trace.jsonl (anchor-segmented or flat)
 *   --masks F         "<frame> <hexmask>" lines (sticky)
 *   --flatten F       write the replayed input as a flat frame-absolute trace
 *   --record F        HUMAN PLAY: realtime, real input, writes a flat trace
 *   --realtime        pace ticks at 60 Hz wall clock
 *   --dump-at F[,F]   state dump(s) <rundir>\state_fNNNNNN.rhs (also "end")
 *   --hash-every N    log the state hash every N frames
 *   --hook SPEC       shadow:/replace:/off: + name, comma-separated (rh_hooks.c)
 *   --coverage F      first-hit coverage over the hex VAs in F → <rundir>\coverage.txt
 *   --d3d-proxy DLL   record the D3D8 stream with the v3 proxy; --cap FROM:COUNT
 *   --stall-ms N      hold the window N ms at the end (eyeballing)
 * Output: <rundir>\rhost.log; last line "RESULT ..." (key=value, parsed by rhost.py). */
#include "rhost.h"

rh_opts_t O;
volatile uint32_t g_frame;
FILE *g_log;
static FILE *g_logf;
static uint32_t g_vclock;
static char g_gamedir_abs[MAX_PATH];
volatile LONG g_live_threads, g_threads_created;
static int g_finished;

void rh_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap); fflush(stdout);
    if (g_logf) { va_start(ap, fmt); vfprintf(g_logf, fmt, ap); va_end(ap); fflush(g_logf); }
}
void rh_fatal(const char *fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rh_log("FATAL %s\nRESULT status=fatal frame=%u\n", b, g_frame);
    ExitProcess(3);
}

/* ---- IAT + call-site patching (retail image only) ---- */
static IMAGE_IMPORT_DESCRIPTOR *imports(void)
{
    IMAGE_NT_HEADERS32 *nt = (void *)(0x400000 + ((IMAGE_DOS_HEADER *)0x400000)->e_lfanew);
    return (void *)(0x400000 + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
}
static uint32_t *iat_slot(const char *name)
{
    for (IMAGE_IMPORT_DESCRIPTOR *d = imports(); d->Name; d++) {
        IMAGE_THUNK_DATA32 *ilt = (void *)(0x400000 + d->OriginalFirstThunk), *iat = (void *)(0x400000 + d->FirstThunk);
        for (; ilt->u1.AddressOfData; ilt++, iat++)
            if (!(ilt->u1.Ordinal & IMAGE_ORDINAL_FLAG32) &&
                !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(0x400000 + ilt->u1.AddressOfData))->Name, name))
                return (uint32_t *)&iat->u1.Function;
    }
    return NULL;
}
void iat_override(const char *name, void *fn)
{
    uint32_t *s = iat_slot(name);
    if (!s) rh_fatal("iat_override: retail does not import %s", name);
    *s = (uint32_t)(uintptr_t)fn;
}
static int iat_try(const char *name, void *fn) { uint32_t *s = iat_slot(name); if (s) *s = (uint32_t)(uintptr_t)fn; return s != NULL; }
void *iat_lookup(const char *name) { uint32_t *s = iat_slot(name); return s ? (void *)(uintptr_t)*s : NULL; }
int redirect_calls(uint32_t va, void *hook)
{
    int n = 0;
    for (uint8_t *p = (uint8_t *)RT_TEXT_LO, *e = p + RT_TEXT_SZ - 5; p < e; p++) {
        if (*p != 0xE8) continue;
        uint32_t tgt = (uint32_t)(uintptr_t)p + 5 + *(int32_t *)(p + 1);
        if (tgt != va) continue;
        *(int32_t *)(p + 1) = (int32_t)((uint32_t)(uintptr_t)hook - ((uint32_t)(uintptr_t)p + 5));
        n++;
    }
    return n;
}

/* ---- fixed-address stacks ----
 * Retail stores stack addresses in globals (CRT __NLG_Notify writes EBP to
 * 0x602374) and the OS places thread stacks differently per run, so retail runs
 * on stacks at fixed VAs: main 0x58000000..0x59000000, the (serialized, DEFER)
 * loader workers share 0x59000000..0x59800000. The bottom 64 KB of each stays
 * uncommitted (overflow faults instead of corrupting). TEB StackBase/StackLimit
 * are set because SEH dispatch rejects registration records outside them. */
#define MAIN_STACK_LO 0x58000000u
#define MAIN_STACK_HI 0x59000000u
#define WORK_STACK_LO 0x59000000u
#define WORK_STACK_HI 0x59800000u
#define STACK_GUARD   0x10000u
static void stacks_init(void)
{
    if (VirtualAlloc((void *)(uintptr_t)MAIN_STACK_LO, WORK_STACK_HI - MAIN_STACK_LO, MEM_RESERVE, PAGE_READWRITE) != (void *)(uintptr_t)MAIN_STACK_LO)
        rh_fatal("cannot reserve the fixed stacks at %08x (err %lu)", MAIN_STACK_LO, GetLastError());
    if (!VirtualAlloc((void *)(uintptr_t)(MAIN_STACK_LO + STACK_GUARD), MAIN_STACK_HI - MAIN_STACK_LO - STACK_GUARD, MEM_COMMIT, PAGE_READWRITE) ||
        !VirtualAlloc((void *)(uintptr_t)(WORK_STACK_LO + STACK_GUARD), WORK_STACK_HI - WORK_STACK_LO - STACK_GUARD, MEM_COMMIT, PAGE_READWRITE))
        rh_fatal("cannot commit the fixed stacks (err %lu)", GetLastError());
}
/* call cdecl fn(arg) with ESP = top, TEB stack bounds = [lo,top), fresh SEH chain */
static uint32_t __attribute__((noinline)) call_on_stack(void *fn, void *arg, uint32_t lo, uint32_t top)
{
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();
    void *ob = tib->StackBase, *ol = tib->StackLimit; struct _EXCEPTION_REGISTRATION_RECORD *oe = tib->ExceptionList;
    tib->StackBase = (void *)(uintptr_t)top; tib->StackLimit = (void *)(uintptr_t)(lo + STACK_GUARD);
    tib->ExceptionList = (void *)(uintptr_t)0xffffffffu;
    uint32_t r;
    __asm__ volatile(
        "movl %%esp, %%esi\n\t"
        "movl %3, %%esp\n\t"
        "pushl %2\n\t"
        "call *%1\n\t"
        "movl %%esi, %%esp\n\t"
        : "=a"(r) : "r"(fn), "r"(arg), "r"(top - 64) : "esi", "ecx", "edx", "memory", "cc");
    tib->StackBase = ob; tib->StackLimit = ol; tib->ExceptionList = oe;
    return r;
}

/* ---- threads: deferred loader workers ---- */
static struct { HANDLE h; uint32_t fn, frame; } g_def[32]; static int g_ndef;
static volatile int g_in_defer;   /* run_deferred is resolving workers (boundary before tick g_frame+1) */
typedef struct { LPTHREAD_START_ROUTINE fn; LPVOID arg; } thr_wrap_t;
static DWORD WINAPI thread_wrapper(LPVOID p)
{
    thr_wrap_t w = *(thr_wrap_t *)p; free(p);
    /* retail thread procs end in a plain `ret` (cdecl). DEFER serializes workers,
     * so they can share one fixed stack; without DEFER they keep OS stacks. */
    DWORD r = O.no_defer ? ((DWORD (__cdecl *)(LPVOID))w.fn)(w.arg)
                         : call_on_stack((void *)w.fn, w.arg, WORK_STACK_LO, WORK_STACK_HI);
    InterlockedDecrement(&g_live_threads);
    return r;
}
static HANDLE WINAPI my_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD fl, LPDWORD tid)
{
    InterlockedIncrement(&g_threads_created);
    thr_wrap_t *w = malloc(sizeof *w); w->fn = fn; w->arg = arg;
    InterlockedIncrement(&g_live_threads);
    if (O.no_defer) return CreateThread(sa, st, thread_wrapper, w, fl, tid);
    if (g_ndef >= 32) rh_fatal("too many deferred threads");
    HANDLE h = CreateThread(sa, st, thread_wrapper, w, fl | CREATE_SUSPENDED, tid);
    if (tid) *tid = 0x1000 + (DWORD)g_threads_created;     /* deterministic id seen by the game */
    HANDLE dup; DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_def[g_ndef].h = dup; g_def[g_ndef].fn = (uint32_t)(uintptr_t)fn; g_def[g_ndef].frame = g_frame; g_ndef++;
    return h;
}
static void run_deferred(void)
{
    g_in_defer = 1;
    for (int i = 0; i < g_ndef; i++) {
        ResumeThread(g_def[i].h);
        DWORD r = WaitForSingleObject(g_def[i].h, 120000);
        if (r != WAIT_OBJECT_0) rh_fatal("deferred worker %08x (created f%u) did not finish (wait=%lu)", g_def[i].fn, g_def[i].frame, r);
        if (O.log_anchors) rh_log("worker %08x created f%u done f%u\n", g_def[i].fn, g_def[i].frame, g_frame);
        CloseHandle(g_def[i].h);
    }
    g_ndef = 0;
    g_in_defer = 0;
}

/* ---- virtual time ---- */
static DWORD v_ms(void) { return 1000000u + g_frame * 17u; }
static DWORD WINAPI my_timeGetTime(void) { return v_ms(); }
static DWORD WINAPI my_GetTickCount(void) { return v_ms(); }
static BOOL WINAPI my_QPC(LARGE_INTEGER *p) { p->QuadPart = (LONGLONG)v_ms() * 1000; return TRUE; }
static BOOL WINAPI my_QPF(LARGE_INTEGER *p) { p->QuadPart = 1000000; return TRUE; }
static void WINAPI my_GetLocalTime(SYSTEMTIME *t)
{ memset(t, 0, sizeof *t); t->wYear = 2010; t->wMonth = 1; t->wDay = 1; t->wHour = 12; t->wSecond = (WORD)((v_ms() / 1000) % 60); t->wMinute = (WORD)((v_ms() / 60000) % 60); }
static void WINAPI my_GetSystemTime(SYSTEMTIME *t) { my_GetLocalTime(t); }
static DWORD WINAPI my_GetTimeZoneInformation(TIME_ZONE_INFORMATION *z) { memset(z, 0, sizeof *z); return TIME_ZONE_ID_UNKNOWN; }

/* ---- process identity the CRT copies into retail memory at startup ----
 * The real values differ per launch (our own --run path is in the command line;
 * the runner vs an interactive shell have different environments and std
 * handles) and the CRT stores them in its heap/.data, so they must be fixed. */
static char g_cmdline[MAX_PATH + 8];
static LPSTR WINAPI my_GetCommandLineA(void) { return g_cmdline; }
static char g_envA[] = "SystemRoot=C:\\Windows\0\0";
static WCHAR g_envW[] = L"SystemRoot=C:\\Windows\0\0";
static LPCH WINAPI my_GetEnvironmentStrings(void) { return g_envA; }
static LPWCH WINAPI my_GetEnvironmentStringsW(void) { return g_envW; }
static BOOL WINAPI my_FreeEnvironmentStringsA(LPCH p) { (void)p; return TRUE; }
static BOOL WINAPI my_FreeEnvironmentStringsW(LPWCH p) { (void)p; return TRUE; }
static VOID WINAPI my_GetStartupInfoA(LPSTARTUPINFOA si) { memset(si, 0, sizeof *si); si->cb = sizeof *si; si->wShowWindow = SW_SHOWNORMAL; }
static HANDLE WINAPI my_GetStdHandle(DWORD n) { (void)n; return INVALID_HANDLE_VALUE; }

/* ---- --quiet: batch runs stay off the user's desktop ----
 * The window must exist (D3D8 windowed device) but is placed off-screen, never
 * activated and has no taskbar button. Sim state does not depend on placement:
 * retail reads its window rect only on WM_DESTROY (rhost exits before that). */
typedef HWND (WINAPI *cwx_t)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
static HWND WINAPI my_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD st, int x, int y, int w, int h, HWND p, HMENU m, HINSTANCE i, LPVOID lp)
{
    (void)x; (void)y;
    return CreateWindowExA(ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, name, st, -8000, -8000, w, h, p, m, i, lp);
}
static BOOL WINAPI my_ShowWindow(HWND h, int cmd) { (void)cmd; return ShowWindow(h, SW_SHOWNOACTIVATE); }
static BOOL WINAPI my_SetWindowPlacement(HWND h, const WINDOWPLACEMENT *wp)
{
    WINDOWPLACEMENT q = *wp; OffsetRect(&q.rcNormalPosition, -8000 - q.rcNormalPosition.left, -8000 - q.rcNormalPosition.top);
    q.showCmd = SW_SHOWNOACTIVATE; return SetWindowPlacement(h, &q);
}

/* Retail's main loop ticks only while DAT_073dfca0 != 0, else WaitMessage().
 * WM_ACTIVATE sets the gate to !minimized, so a window that is never activated
 * (--quiet) would park forever. Deterministic runs never pause: raise the gate
 * and return. (--record keeps the real behaviour.) */
static BOOL WINAPI my_WaitMessage(void) { G32(DAT_TICKGATE) = 1; return TRUE; }

/* FUN_0040110f/0040112a (from the WndProc, 47b6cf/47b6de): EnumWindows saves the
 * WINDOWPLACEMENT of every visible titled HOST window except Progman into
 * 0x603dc0[100] (count DAT_00605080) and restores them later — host desktop state
 * (z-order!) leaking into retail memory (day2-shop record vs replay: two entries
 * swapped). Hermetic: enumerate nothing; no game logic reads the table. */
static BOOL WINAPI my_EnumWindows(WNDENUMPROC cb, LPARAM lp) { (void)cb; (void)lp; return TRUE; }

/* ---- module identity ---- */
static HMODULE WINAPI my_GetModuleHandleA(LPCSTR n) { return n ? GetModuleHandleA(n) : (HMODULE)0x400000; }
static DWORD WINAPI my_GetModuleFileNameA(HMODULE h, LPSTR buf, DWORD n)
{
    if (!h || h == (HMODULE)0x400000) { snprintf(buf, n, "%s\\recettear.exe", g_gamedir_abs); return (DWORD)strlen(buf); }
    return GetModuleFileNameA(h, buf, n);
}

/* ---- coverage: one-shot INT3 at listed entries ---- */
static uint32_t *g_cov_va; static uint8_t *g_cov_orig; static uint32_t *g_cov_hit; static int g_cov_n;
static int cov_find(uint32_t va)
{ int lo = 0, hi = g_cov_n - 1; while (lo <= hi) { int m = (lo + hi) / 2; if (g_cov_va[m] == va) return m; if (g_cov_va[m] < va) lo = m + 1; else hi = m - 1; } return -1; }
static LONG CALLBACK cov_veh(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT) return EXCEPTION_CONTINUE_SEARCH;
    uint32_t a = (uint32_t)(uintptr_t)e->ExceptionRecord->ExceptionAddress;
    int i = cov_find(a);
    if (i < 0) return EXCEPTION_CONTINUE_SEARCH;
    *(volatile uint8_t *)(uintptr_t)a = g_cov_orig[i];
    if (!g_cov_hit[i]) g_cov_hit[i] = g_frame + 1;
    e->ContextRecord->Eip = a;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static int cmpu(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return x < y ? -1 : x > y; }
static void cov_install(void)
{
    FILE *f = fopen(O.coverage, "r"); if (!f) rh_fatal("cannot read %s", O.coverage);
    unsigned v; int cap = 4096; g_cov_va = malloc(cap * 4);
    while (fscanf(f, "%x", &v) == 1) { if (g_cov_n == cap) { cap *= 2; g_cov_va = realloc(g_cov_va, cap * 4); } g_cov_va[g_cov_n++] = v; }
    fclose(f);
    qsort(g_cov_va, g_cov_n, 4, cmpu);
    g_cov_orig = malloc(g_cov_n); g_cov_hit = calloc(g_cov_n, 4);
    AddVectoredExceptionHandler(1, cov_veh);
    for (int i = 0; i < g_cov_n; i++) { g_cov_orig[i] = *(uint8_t *)(uintptr_t)g_cov_va[i]; *(uint8_t *)(uintptr_t)g_cov_va[i] = 0xCC; }
    rh_log("coverage: %d entries armed\n", g_cov_n);
}
static void cov_dump(void)
{
    if (!g_cov_n) return;
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\coverage.txt", O.rundir);
    FILE *f = fopen(p, "w"); if (!f) return;
    int hits = 0;
    for (int i = 0; i < g_cov_n; i++) if (g_cov_hit[i]) { fprintf(f, "%08x %u\n", g_cov_va[i], g_cov_hit[i] - 1); hits++; }
    fclose(f);
    rh_log("coverage: %d/%d hit\n", hits, g_cov_n);
}

/* ---- D3D8 stream recording via the v3 proxy (loaded in-process) ---- */
typedef void (WINAPI *arm_fn)(unsigned, unsigned);
typedef unsigned (WINAPI *pc_fn)(void);
static arm_fn g_arm; static pc_fn g_present_count;
static void d3d_proxy_install(void)
{
    char dst[MAX_PATH], cfg[MAX_PATH], out[MAX_PATH];
    snprintf(dst, sizeof dst, "%s\\v3proxy_d3d8.dll", O.rundir);
    snprintf(cfg, sizeof cfg, "%s\\v3proxy.cfg", O.rundir);
    snprintf(out, sizeof out, "%s\\v3", O.rundir);
    CreateDirectoryA(out, NULL);
    if (!CopyFileA(O.d3d_proxy, dst, FALSE)) rh_fatal("copy %s failed", O.d3d_proxy);
    FILE *f = fopen(cfg, "w"); if (!f) rh_fatal("write %s", cfg);
    fprintf(f, "out=%s\narmwait=1\n", out); fclose(f);
    HMODULE m = LoadLibraryA(dst);
    if (!m) rh_fatal("LoadLibrary %s failed (%lu)", dst, GetLastError());
    void *create = (void *)GetProcAddress(m, "Direct3DCreate8");
    g_arm = (arm_fn)(void *)GetProcAddress(m, "OrV3ArmWindowAt");
    g_present_count = (pc_fn)(void *)GetProcAddress(m, "OrV3PresentCount");
    if (!create || !g_arm || !g_present_count) rh_fatal("proxy lacks Direct3DCreate8/OrV3ArmWindowAt/OrV3PresentCount");
    iat_override("Direct3DCreate8", create);
    rh_log("d3d: v3 proxy %s, capture %s\n", dst, O.has_cap ? "armed by frame" : "none");
}

/* ---- --log-sbh: trace the MSVC6 CRT small-block heap (diagnostic) ---- */
static FILE *g_sbh; static long g_sbh_n;
static DWORD g_main_tid;
static void sbh_stack(char *b, size_t n)
{
    uint32_t *sp; __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    int k = 0; size_t o = 0; b[0] = 0;
    for (int i = 0; i < 256 && k < 5; i++)
        if (sp[i] >= RT_TEXT_LO && sp[i] < RT_TEXT_LO + RT_TEXT_SZ) { o += snprintf(b + o, n - o, "%s%x", k ? "," : "", sp[i]); k++; }
}
static void * __cdecl my_sbh_alloc(int n)
{
    void *r = ((void *(__cdecl *)(int))0x5085ca)(n);
    char st[96]; sbh_stack(st, sizeof st);
    fprintf(g_sbh, "A %ld f%u %s n=%d -> %p [%s]\n", g_sbh_n++, g_frame, GetCurrentThreadId() == g_main_tid ? "main" : "other", n, r, st);
    return r;
}
static void __cdecl my_sbh_free(void *hdr, void *p)
{
    char st[96]; sbh_stack(st, sizeof st);
    fprintf(g_sbh, "F %ld f%u %s %p [%s]\n", g_sbh_n++, g_frame, GetCurrentThreadId() == g_main_tid ? "main" : "other", p, st);
    ((void (__cdecl *)(void *, void *))0x50829f)(hdr, p);
}

/* ---- --log-rand FROM:TO: every retail rand() call in a frame window (diagnostic) ---- */
static uint32_t g_lr_from = 1, g_lr_to = 0;
static uint32_t __cdecl logrand(void)
{
    uint32_t s0 = G32(DAT_RNG);
    uint32_t r = ((uint32_t (__cdecl *)(void))VA_RAND)();
    if (g_frame >= g_lr_from && g_frame <= g_lr_to) {
        char st[96]; sbh_stack(st, sizeof st);
        /* " worker": drawn by a deferred loader at the boundary BEFORE tick g_frame+1
         * (the port labels such draws with that upcoming tick) */
        rh_log("rand f%u seed %08x -> %08x [%s]%s\n", g_frame, s0, G32(DAT_RNG), st, g_in_defer ? " worker" : "");
    }
    return r;
}

/* ---- the three engine hooks ---- */
static uint32_t __cdecl hook_clock(void) { return g_vclock; }

static uint16_t g_mask;
static void __cdecl hook_input(void)
{
    if (O.record) {                                  /* human play: real devices */
        ((void (__cdecl *)(void))VA_INPUT)();
        g_mask = G16(DAT_MASK);
        rh_trace_record(g_frame, g_mask);
        return;
    }
    int have;
    uint16_t m = rh_trace_mask(g_frame, &have);
    g_mask = have ? m : 0;
    G16(DAT_MASK) = g_mask;
    G16(DAT_MASK + 42) = 0;                          /* player-2 slot: never real devices */
    G32(DAT_TICKGATE) = 1;                           /* never park in WaitMessage */
}

static void finish(const char *why);
void rh_finish(const char *why) { finish(why); }
static LARGE_INTEGER g_t0, g_qf;
static int is_dump_frame(uint32_t f) { for (int i = 0; i < O.n_dump; i++) if (O.dump_at[i] == f) return 1; return 0; }
static void dump_now(const char *tag)
{
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\state_%s.rhs", O.rundir, tag);
    rh_state_dump(p);
}
static void __cdecl hook_tick(void)
{
    if (g_frame == 0 && O.has_seed) G32(DAT_RNG) = O.seed;
    g_vclock += 17;
    if (g_ndef) run_deferred();
    if (is_dump_frame(g_frame)) { char t[32]; snprintf(t, sizeof t, "f%06u", g_frame); dump_now(t); }
    if (O.log_every && g_frame % O.log_every == 0) {
        uint16_t cw; __asm__ volatile("fnstcw %0" : "=m"(cw));
        char wb[RH_MAX_WATCH * 24] = ""; size_t wo = 0;
        for (int i = 0; i < O.n_watch && wo < sizeof wb; i++) wo += snprintf(wb + wo, sizeof wb - wo, " %x=%d", O.watch[i], (int)G32(O.watch[i]));
        rh_log("frame %u scene=%d rng=%08x mask=%04x cw=%04x thr=%ld/%ld player=(%.2f,%.2f,%.2f)%s\n", g_frame,
               (int)G32(DAT_SCENE), G32(DAT_RNG), g_mask, cw, g_live_threads, g_threads_created,
               GF(DAT_PLAYER_POS), GF(DAT_PLAYER_POS + 4), GF(DAT_PLAYER_POS + 8), wb);
    }
    if (g_frame == 1 && (O.n_shot || O.shot_every)) rh_shot_warm();
    for (int i = 0; i < O.n_shot; i++)
        if (O.shot_at[i] == g_frame) { char t[32]; snprintf(t, sizeof t, "f%06u", g_frame); rh_shot(t); }
    if (O.shot_every && g_frame && g_frame % O.shot_every == 0) { char t[32]; snprintf(t, sizeof t, "f%06u", g_frame); rh_shot(t); }
    if (O.hash_every && g_frame % O.hash_every == 0) {
        rh_hash_t h; rh_state_hash(&h);
        rh_log("hash f%u full=%016llx sem=%016llx masked=%u\n", g_frame, (unsigned long long)h.full, (unsigned long long)h.semantic, h.masked);
    }
    if (O.has_cap && g_frame == O.cap_from && g_arm) {
        unsigned pc = g_present_count();
        g_arm(pc, O.cap_count);
        rh_log("d3d: armed present window [%u,%u) at frame %u\n", pc, pc + O.cap_count, g_frame);
    }
    if (g_frame >= O.frames) finish("frames");
    g_frame++;
    if (O.realtime) {
        LARGE_INTEGER t; QueryPerformanceCounter(&t);
        double due = g_frame * (1000.0 / 60.0), now = (t.QuadPart - g_t0.QuadPart) * 1000.0 / g_qf.QuadPart;
        if (due > now + 1) Sleep((DWORD)(due - now));
    }
    ((void (__cdecl *)(void))VA_TICK)();
    if (O.n_watch && O.log_every && g_frame % O.log_every == 0) {
        /* the state the anchors see: after tick N, BEFORE the next boundary's
         * deferred workers (which clear e.g. a dialogue load's DAT_06a49960). */
        char wb[RH_MAX_WATCH * 24] = ""; size_t wo = 0;
        for (int i = 0; i < O.n_watch && wo < sizeof wb; i++) wo += snprintf(wb + wo, sizeof wb - wo, " %x=%d", O.watch[i], (int)G32(O.watch[i]));
        rh_log("post %u%s\n", g_frame, wb);
    }
    rh_trace_after_tick(g_frame);
    if (rh_trace_done(g_frame) && O.frames > g_frame) O.frames = g_frame;
    if (rh_trace_stalled(g_frame)) finish("stalled");
}

static void finish(const char *why)
{
    if (g_finished) return;
    g_finished = 1;
    rh_trace_finish();
    cov_dump();
    rh_hooks_report();
    if (is_dump_frame(0xffffffffu)) dump_now("end");
    for (int i = 0; i < O.n_shot; i++) if (O.shot_at[i] == 0xffffffffu) rh_shot("end");
    char hs[256]; rh_heap_stats(hs, sizeof hs);
    rh_log("%s\n", hs);
    if (O.log_ext) rh_state_ext_report();
    rh_hash_t h; rh_state_hash(&h);
    LARGE_INTEGER t; QueryPerformanceCounter(&t);
    rh_log("RESULT status=%s reason=%s frame=%u rng=%08x scene=%d state_full=%016llx state_sem=%016llx masked=%u state_bytes=%u wall_ms=%.0f threads=%ld %s\n",
           !strcmp(why, "stalled") ? "stalled" : "ok", why, g_frame, G32(DAT_RNG), (int)G32(DAT_SCENE), (unsigned long long)h.full, (unsigned long long)h.semantic,
           h.masked, h.bytes, (t.QuadPart - g_t0.QuadPart) * 1000.0 / g_qf.QuadPart, g_threads_created, rh_trace_status());
    if (g_sbh) fclose(g_sbh);
    if (O.stall_ms) Sleep(O.stall_ms);
    if (g_logf) fclose(g_logf);
    ExitProcess(0);
}
static VOID WINAPI my_ExitProcess(UINT code) { if (!g_finished) finish("game-exit"); ExitProcess(code); }

/* ---- CLI ---- */
static void usage(void)
{
    fprintf(stderr, "usage: rhost --exe RETAIL --game DIR --run DIR [--frames N] [--seed S|--no-seed] [--trace F|--masks F]\n"
                    "  [--flatten F] [--record F] [--realtime] [--dump-at F[,F|end]] [--hash-every N] [--log-every N]\n"
                    "  [--hook SPEC] [--coverage F] [--d3d-proxy DLL --cap FROM:COUNT] [--stall-ms N] [--tail N]\n"
                    "  [--no-vtime] [--no-defer] [--no-arena] [--no-overlay] [--log-anchors] [--log-files] [--log-heap]\n");
    ExitProcess(2);
}
static void parse(int argc, char **argv)
{
    O.seed = 19937; O.has_seed = 1; O.tail = 300; O.log_every = 100; O.frames = 0; O.stall_frames = 20000;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        #define ARG() (i + 1 < argc ? argv[++i] : (usage(), (char *)0))
        if (!strcmp(a, "--exe")) O.exe = ARG();
        else if (!strcmp(a, "--game")) O.gamedir = ARG();
        else if (!strcmp(a, "--run")) O.rundir = ARG();
        else if (!strcmp(a, "--frames")) O.frames = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--seed")) { O.seed = strtoul(ARG(), 0, 0); O.has_seed = 1; }
        else if (!strcmp(a, "--no-seed")) O.has_seed = 0;
        else if (!strcmp(a, "--trace")) O.trace = ARG();
        else if (!strcmp(a, "--masks")) O.masks = ARG();
        else if (!strcmp(a, "--flatten")) O.flatten = ARG();
        else if (!strcmp(a, "--record")) { O.record = ARG(); O.realtime = 1; }
        else if (!strcmp(a, "--realtime")) O.realtime = 1;
        else if (!strcmp(a, "--dump-at")) {
            char b[256]; snprintf(b, sizeof b, "%s", ARG());
            for (char *t = strtok(b, ","); t && O.n_dump < 16; t = strtok(NULL, ","))
                O.dump_at[O.n_dump++] = !strcmp(t, "end") ? 0xffffffffu : strtoul(t, 0, 0);
        }
        else if (!strcmp(a, "--hash-every")) O.hash_every = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--shot-at")) {
            char b[512]; snprintf(b, sizeof b, "%s", ARG());
            for (char *t = strtok(b, ","); t && O.n_shot < 64; t = strtok(NULL, ","))
                O.shot_at[O.n_shot++] = !strcmp(t, "end") ? 0xffffffffu : strtoul(t, 0, 0);
        }
        else if (!strcmp(a, "--shot-every")) O.shot_every = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--log-every")) O.log_every = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--tail")) O.tail = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--stall-frames")) O.stall_frames = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--hook")) O.hooks = ARG();
        else if (!strcmp(a, "--coverage")) O.coverage = ARG();
        else if (!strcmp(a, "--d3d-proxy")) O.d3d_proxy = ARG();
        else if (!strcmp(a, "--cap")) { unsigned f, c; if (sscanf(ARG(), "%u:%u", &f, &c) != 2) usage(); O.cap_from = f; O.cap_count = c; O.has_cap = 1; }
        else if (!strcmp(a, "--stall-ms")) O.stall_ms = strtoul(ARG(), 0, 0);
        else if (!strcmp(a, "--no-vtime")) O.no_vtime = 1;
        else if (!strcmp(a, "--no-defer")) O.no_defer = 1;
        else if (!strcmp(a, "--no-arena")) O.no_arena = 1;
        else if (!strcmp(a, "--no-overlay")) O.no_overlay = 1;
        else if (!strcmp(a, "--log-anchors")) O.log_anchors = 1;
        else if (!strcmp(a, "--log-files")) O.log_files = 1;
        else if (!strcmp(a, "--log-heap")) O.log_heap = 1;
        else if (!strcmp(a, "--log-ext")) O.log_ext = 1;
        else if (!strcmp(a, "--quiet")) O.quiet = 1;
        else if (!strcmp(a, "--log-sbh")) O.log_sbh = ARG();
        else if (!strcmp(a, "--log-rand")) { if (sscanf(ARG(), "%u:%u", &g_lr_from, &g_lr_to) != 2) usage(); }
        else if (!strcmp(a, "--watch")) {   /* hex VAs: in each `frame` log line + a `post N` line after tick N */
            char b[RH_MAX_WATCH * 12]; snprintf(b, sizeof b, "%s", ARG());
            for (char *t = strtok(b, ","); t; t = strtok(NULL, ",")) {
                if (O.n_watch == RH_MAX_WATCH) { fprintf(stderr, "--watch: more than %d VAs\n", RH_MAX_WATCH); usage(); }
                O.watch[O.n_watch++] = strtoul(t, 0, 16);
            }
        }
        else { fprintf(stderr, "unknown option %s\n", a); usage(); }
        #undef ARG
    }
    if (!O.exe || !O.gamedir || !O.rundir) usage();
    /* --record/--flatten relative paths live in the run dir */
    static char rec_abs[MAX_PATH], flat_abs[MAX_PATH];
    if (O.record && !strchr(O.record, ':') && O.record[0] != '\\') { snprintf(rec_abs, sizeof rec_abs, "%s\\%s", O.rundir, O.record); O.record = rec_abs; }
    if (O.flatten && !strchr(O.flatten, ':') && O.flatten[0] != '\\') { snprintf(flat_abs, sizeof flat_abs, "%s\\%s", O.rundir, O.flatten); O.flatten = flat_abs; }
    if (!O.frames) O.frames = O.record ? 0xfffffff0u : (O.trace || O.masks) ? 120000u : 3000u;
}

static void load_volatile(void)
{
    rh_state_volatile_add(0x73dfc7cu);   /* game HWND (CreateWindowExA result) */
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\volatile.txt", O.rundir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[256]; unsigned v;
    while (fgets(line, sizeof line, f)) if (sscanf(line, "%x", &v) == 1) rh_state_volatile_add(v);
    fclose(f);
}

RETAIL_HOLE_DEFINE

int main(int argc, char **argv)
{
    parse(argc, argv);
    QueryPerformanceFrequency(&g_qf); QueryPerformanceCounter(&g_t0);
    CreateDirectoryA(O.rundir, NULL);
    char lp[MAX_PATH]; snprintf(lp, sizeof lp, "%s\\rhost.log", O.rundir);
    g_logf = fopen(lp, "w");
    g_log = stdout;
    if (!O.no_arena && rh_heap_init()) rh_fatal("heap arena reservation failed");
    stacks_init();
    GetFullPathNameA(O.gamedir, sizeof g_gamedir_abs, g_gamedir_abs, NULL);
    for (size_t l = strlen(g_gamedir_abs); l && g_gamedir_abs[l - 1] == '\\'; ) g_gamedir_abs[--l] = 0;
    char exe_abs[MAX_PATH]; GetFullPathNameA(O.exe, sizeof exe_abs, exe_abs, NULL);
    if (!SetCurrentDirectoryA(g_gamedir_abs)) rh_fatal("cannot cd to %s", g_gamedir_abs);
    retail_image_t img;
    if (retail_map(exe_abs, &img, 1)) rh_fatal("map %s failed", exe_abs);
    rh_log("rhost: mapped %s (imports %d resolved, %d stubbed) entry=%08x\n", exe_abs, img.n_imports_resolved, img.n_imports_stubbed, img.entry);
    iat_override("GetModuleHandleA", my_GetModuleHandleA);
    iat_override("EnumWindows", my_EnumWindows);
    iat_override("GetModuleFileNameA", my_GetModuleFileNameA);
    iat_override("CreateThread", my_CreateThread);
    iat_override("ExitProcess", my_ExitProcess);
    snprintf(g_cmdline, sizeof g_cmdline, "\"%s\\recettear.exe\"", g_gamedir_abs);
    iat_override("GetCommandLineA", my_GetCommandLineA);
    iat_override("GetEnvironmentStrings", my_GetEnvironmentStrings);
    iat_override("GetEnvironmentStringsW", my_GetEnvironmentStringsW);
    iat_override("FreeEnvironmentStringsA", my_FreeEnvironmentStringsA);
    iat_override("FreeEnvironmentStringsW", my_FreeEnvironmentStringsW);
    iat_override("GetStartupInfoA", my_GetStartupInfoA);
    iat_override("GetStdHandle", my_GetStdHandle);
    if (!O.no_vtime) {
        /* every wall-clock source retail imports (GetTickCount is not imported today) */
        iat_override("timeGetTime", my_timeGetTime);
        iat_try("GetTickCount", my_GetTickCount);
        iat_override("QueryPerformanceCounter", my_QPC);
        iat_override("QueryPerformanceFrequency", my_QPF);
        iat_override("GetLocalTime", my_GetLocalTime);
        iat_override("GetSystemTime", my_GetSystemTime);
        iat_override("GetTimeZoneInformation", my_GetTimeZoneInformation);
    }
    if (!O.no_arena) rh_heap_install();
    if (!O.no_overlay) rh_files_install();
    rh_handles_install();                            /* after files/threads: wraps their overrides */
    if (!O.record) iat_override("WaitMessage", my_WaitMessage);
    rh_dinput_install();
    if (O.quiet) {
        iat_override("CreateWindowExA", my_CreateWindowExA);
        iat_override("ShowWindow", my_ShowWindow);
        iat_override("SetWindowPlacement", my_SetWindowPlacement);
    }
    int nc = redirect_calls(VA_CLOCK, hook_clock), nt = redirect_calls(VA_TICK, hook_tick), ni = redirect_calls(VA_INPUT, hook_input);
    if (nc != 1 || nt != 1 || ni != 1) rh_fatal("engine call sites clock=%d tick=%d input=%d (want 1/1/1)", nc, nt, ni);
    load_volatile();
    g_main_tid = GetCurrentThreadId();
    if (O.log_sbh) {
        g_sbh = fopen(O.log_sbh, "w"); if (!g_sbh) rh_fatal("cannot write %s", O.log_sbh);
        rh_log("log-sbh: alloc sites=%d free sites=%d\n", redirect_calls(0x5085ca, my_sbh_alloc), redirect_calls(0x50829f, my_sbh_free));
    }
    rh_trace_init();
    if (g_lr_to >= g_lr_from) {   /* rand is mostly reached through the thunk j__rand @0x471084 (jmp 0x5041f6) */
        int a = redirect_calls(VA_RAND, logrand), b = redirect_calls(0x471084u, logrand);
        rh_log("log-rand: %d direct + %d thunk call sites\n", a, b);
    }
    rh_hooks_install();
    if (O.coverage) cov_install();
    if (O.d3d_proxy) d3d_proxy_install();
    rh_log("rhost: det vtime=%d defer=%d arena=%d overlay=%d seed=%s%u frames<=%u realtime=%d\n",
           !O.no_vtime, !O.no_defer, !O.no_arena, !O.no_overlay, O.has_seed ? "" : "none/", O.seed, O.frames, O.realtime);
    call_on_stack((void *)(uintptr_t)img.entry, NULL, MAIN_STACK_LO, MAIN_STACK_HI);   /* WinMainCRTStartup: never returns */
    finish("entry-returned");
    return 0;
}
