/* rh_handles.c — deterministic kernel handle values for retail.
 *
 * Kernel handle numbers depend on everything else the process opened first
 * (driver/DLL threads, our own host), so the values retail stores (CRT __pioinfo
 * osfhnd for open files, the loader's thread handle DAT_06a49950, ...) differed
 * run-to-run (0x380 vs 0x388). Every handle-RETURNING retail import that yields a
 * kernel object gets a fake, sequence-numbered handle; every handle-CONSUMING
 * retail import translates it back. Handles retail never received from us (pseudo
 * handles, NULL, INVALID_HANDLE_VALUE) pass through unchanged.
 * Installed AFTER rh_files/threads so it wraps their overrides. */
#include "rhost.h"

#define FAKE_BASE 0x7E000000u
#define FAKE_MAX  8192
static HANDLE g_real[FAKE_MAX];
static uint32_t g_next;                  /* next fake index (monotonic: never reused) */
static CRITICAL_SECTION g_lk;
static long g_live;

static HANDLE fake_of(HANDLE real)
{
    if (real == NULL || real == INVALID_HANDLE_VALUE) return real;
    EnterCriticalSection(&g_lk);
    if (g_next >= FAKE_MAX) { LeaveCriticalSection(&g_lk); rh_fatal("rh_handles: more than %d handles", FAKE_MAX); }
    uint32_t k = g_next++;
    g_real[k] = real; g_live++;
    LeaveCriticalSection(&g_lk);
    return (HANDLE)(uintptr_t)(FAKE_BASE + 4u * k);
}
static HANDLE real_of(HANDLE h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v < FAKE_BASE || v >= FAKE_BASE + 4u * FAKE_MAX || (v & 3)) return h;
    HANDLE r = g_real[(v - FAKE_BASE) >> 2];
    return r ? r : INVALID_HANDLE_VALUE;
}
static void forget(HANDLE h)
{
    uint32_t v = (uint32_t)(uintptr_t)h;
    if (v < FAKE_BASE || v >= FAKE_BASE + 4u * FAKE_MAX || (v & 3)) return;
    EnterCriticalSection(&g_lk); g_real[(v - FAKE_BASE) >> 2] = NULL; g_live--; LeaveCriticalSection(&g_lk);
}

/* the previous IAT targets (possibly rhost's own overlay / deferral wrappers) */
typedef HANDLE (WINAPI *cfa_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *cfw_t)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *ct_t)(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
static cfa_t p_CreateFileA; static cfw_t p_CreateFileW; static ct_t p_CreateThread;

static HANDLE WINAPI h_CreateFileA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t)
{ return fake_of(p_CreateFileA(p, a, s, sa, d, f, real_of(t))); }
static HANDLE WINAPI h_CreateFileW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD d, DWORD f, HANDLE t)
{ return fake_of(p_CreateFileW(p, a, s, sa, d, f, real_of(t))); }
static HANDLE WINAPI h_CreateThread(LPSECURITY_ATTRIBUTES sa, SIZE_T st, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD fl, LPDWORD tid)
{ return fake_of(p_CreateThread(sa, st, fn, arg, fl, tid)); }
static HANDLE WINAPI h_CreateFileMappingA(HANDLE f, LPSECURITY_ATTRIBUTES sa, DWORD prot, DWORD hi, DWORD lo, LPCSTR name)
{ return fake_of(CreateFileMappingA(real_of(f), sa, prot, hi, lo, name)); }
static LPVOID WINAPI h_MapViewOfFile(HANDLE m, DWORD acc, DWORD hi, DWORD lo, SIZE_T n)
{ return MapViewOfFile(real_of(m), acc, hi, lo, n); }
static BOOL WINAPI h_CloseHandle(HANDLE h)
{ BOOL r = CloseHandle(real_of(h)); forget(h); return r; }
/* rh_snap_touch: the kernel writes these out parameters — a write-tracked (read-only) page
 * would make the call fail instead of faulting into the tracker */
static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD got, LPOVERLAPPED o)
{ rh_snap_touch(b, n); rh_snap_touch(got, sizeof *got); return ReadFile(real_of(h), b, n, got, o); }
static BOOL WINAPI h_WriteFile(HANDLE h, LPCVOID b, DWORD n, LPDWORD put, LPOVERLAPPED o)
{ rh_snap_touch(put, sizeof *put); return WriteFile(real_of(h), b, n, put, o); }
static DWORD WINAPI h_SetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD m)
{ rh_snap_touch(hi, sizeof *hi); return SetFilePointer(real_of(h), lo, hi, m); }
static DWORD WINAPI h_GetFileSize(HANDLE h, LPDWORD hi) { rh_snap_touch(hi, sizeof *hi); return GetFileSize(real_of(h), hi); }
static DWORD WINAPI h_GetFileType(HANDLE h) { return GetFileType(real_of(h)); }
static BOOL WINAPI h_FlushFileBuffers(HANDLE h) { return FlushFileBuffers(real_of(h)); }
static BOOL WINAPI h_SetEndOfFile(HANDLE h) { return SetEndOfFile(real_of(h)); }
static BOOL WINAPI h_SetStdHandle(DWORD n, HANDLE h) { (void)n; (void)h; return TRUE; }   /* never touch the host's std handles */

void rh_handles_install(void)
{
    InitializeCriticalSection(&g_lk);
    p_CreateFileA = (cfa_t)iat_lookup("CreateFileA");
    p_CreateFileW = (cfw_t)iat_lookup("CreateFileW");
    p_CreateThread = (ct_t)iat_lookup("CreateThread");
    iat_override("CreateFileA", h_CreateFileA);
    iat_override("CreateFileW", h_CreateFileW);
    iat_override("CreateThread", h_CreateThread);
    iat_override("CreateFileMappingA", h_CreateFileMappingA);
    iat_override("MapViewOfFile", h_MapViewOfFile);
    iat_override("CloseHandle", h_CloseHandle);
    iat_override("ReadFile", h_ReadFile);
    iat_override("WriteFile", h_WriteFile);
    iat_override("SetFilePointer", h_SetFilePointer);
    iat_override("GetFileSize", h_GetFileSize);
    iat_override("GetFileType", h_GetFileType);
    iat_override("FlushFileBuffers", h_FlushFileBuffers);
    iat_override("SetEndOfFile", h_SetEndOfFile);
    iat_override("SetStdHandle", h_SetStdHandle);
}

long rh_handles_live(void) { return g_live; }

/* savestates: the numbering (g_next) must replay, so it is saved; of the table only the live
 * entries. A boundary normally has none open (loads finish inside their deferred worker). On
 * restore, handles opened since are closed; one that was open then and closed since cannot
 * be reopened here — it is dropped and logged (never seen so far). */
typedef struct { uint32_t next, n; long live; struct { uint32_t k; HANDLE h; } e[]; } hsnap_t;
void *rh_handles_snap(void)
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < g_next; k++) if (g_real[k]) n++;
    hsnap_t *s = malloc(sizeof *s + n * sizeof s->e[0]);
    s->next = g_next; s->live = g_live; s->n = 0;
    for (uint32_t k = 0; k < g_next; k++) if (g_real[k]) { s->e[s->n].k = k; s->e[s->n].h = g_real[k]; s->n++; }
    return s;
}
void rh_handles_unsnap(void *p)
{
    const hsnap_t *s = p;
    uint32_t i = 0;
    static int warned;
    for (uint32_t k = 0; k < g_next; k++) {
        HANDLE want = (i < s->n && s->e[i].k == k) ? s->e[i++].h : NULL;
        if (g_real[k] && g_real[k] != want) CloseHandle(g_real[k]);          /* opened after the savestate */
        if (want && g_real[k] != want && !warned++) rh_log("rh_handles: a handle open in the savestate was closed since (#%u) — dropped\n", k);
        g_real[k] = (g_real[k] == want) ? want : NULL;
    }
    g_next = s->next; g_live = s->live;
}
