/* rh_heap.c — deterministic fixed-address heap for retail.
 *
 * Why: the NT heap (LFH) randomizes block placement, so every pointer retail
 * stores to a heap object differed run-to-run (≈280 dwords of .data per run) and a
 * value-heuristic "semantic hash" had to guess what is a pointer. Routing every
 * retail Heap* / VirtualAlloc(NULL) call through a fixed-address arena makes heap
 * addresses a pure function of the (deterministic) allocation sequence, so the full
 * state hash is meaningful and heap objects (e.g. the item catalog DAT_095d3804)
 * become part of snapshots/shadow runs.
 *
 * Only RETAIL's IAT is redirected (MSVC6 CRT _crtheap + small-block heap regions,
 * and D3DX's HeapAlloc(GetProcessHeap())). System DLLs (d3d8, dinput, dmusic)
 * keep their own heaps; their pointers are classified "external" by rh_state.c.
 *
 * Regions (reserved at startup; abort if unavailable — never silently differ):
 *   SMALL 0x20000000 +256MB  size classes ≤64KB, LIFO free list per class
 *   LARGE 0x30000000 +512MB  >64KB, 4KB-granular first-fit spans, coalescing
 *   VREG  0x50000000 +128MB  VirtualAlloc(NULL,..) reservations (CRT SBH regions)
 * Allocator metadata for SMALL/LARGE lives in a 16-byte header before each block;
 * free spans are tracked host-side. Single lock; retail worker threads are
 * serialized by DEFER anyway. */
#include "rhost.h"

#define SMALL_LO 0x20000000u
#define SMALL_SZ 0x10000000u
#define LARGE_LO 0x30000000u
#define LARGE_SZ 0x20000000u
#define VREG_LO  0x50000000u
#define VREG_SZ  0x08000000u
#define ARENA_LO SMALL_LO
#define ARENA_HI (VREG_LO + VREG_SZ)
#define COMMIT_STEP 0x100000u
#define MAGIC_LIVE 0xA11C0C8Du
#define MAGIC_FREE 0xF4EEF4EEu
#define CLS_LARGE  0xFFFFu
#define N_CLS 48
#define FAKE_HEAP_BASE 0x1EA70000u
#define FAKE_PROCESS_HEAP 0x1EA7FFF0u

typedef struct { uint32_t req, cap, magic, cls; } hdr_t;
typedef struct { uint32_t lo, sz; } span_t;
typedef struct {
    uint32_t lo, hi, top, committed;
    span_t *free; int nfree, capfree;
    int commit_on_bump;
} region_t;

static CRITICAL_SECTION g_lk;
static region_t R_SMALL = { SMALL_LO, SMALL_LO + SMALL_SZ, SMALL_LO, SMALL_LO, 0, 0, 0, 1 };
static region_t R_LARGE = { LARGE_LO, LARGE_LO + LARGE_SZ, LARGE_LO, LARGE_LO, 0, 0, 0, 1 };
static region_t R_VREG  = { VREG_LO,  VREG_LO + VREG_SZ,   VREG_LO,  VREG_LO,  0, 0, 0, 0 };
static uint32_t g_freelist[N_CLS];
static span_t *g_vlive; static int g_nvlive, g_capvlive;
static uint32_t g_nheaps;
static int g_active;
static uint64_t g_nalloc, g_nfree, g_inuse, g_inuse_max;
static uint32_t g_host_lo, g_host_hi;

static void lock(void)   { EnterCriticalSection(&g_lk); }
static void unlock(void) { LeaveCriticalSection(&g_lk); }

static int in_region(const region_t *r, uint32_t a) { return a >= r->lo && a < r->hi; }
int rh_is_det_addr(uint32_t v)
{
    return (v >= RT_IMG_LO && v < RT_IMG_HI) || (v >= g_host_lo && v < g_host_hi) ||
           (g_active && v >= ARENA_LO && v < ARENA_HI);
}

static uint32_t small_class(uint32_t n, uint32_t *cap)
{
    if (n <= 256) { uint32_t c = n ? (n + 15u) & ~15u : 16u; *cap = c; return c / 16u - 1u; }
    int k = 31 - __builtin_clz(n - 1);              /* 2^k < n <= 2^(k+1), k in 8..15 */
    uint32_t step = 1u << (k - 2);
    uint32_t c = (n + step - 1) & ~(step - 1);
    *cap = c;
    return 16u + (uint32_t)(k - 8) * 4u + (c / step - 5u);
}

static void commit_to(region_t *r, uint32_t top)
{
    if (top <= r->committed) return;
    uint32_t want = (top + COMMIT_STEP - 1) & ~(COMMIT_STEP - 1);
    if (want > r->hi) want = r->hi;
    if (!VirtualAlloc((void *)(uintptr_t)r->committed, want - r->committed, MEM_COMMIT, PAGE_READWRITE))
        rh_fatal("rh_heap: commit %08x..%08x failed (err %lu)", r->committed, want, GetLastError());
    r->committed = want;
}

static uint32_t bump(region_t *r, uint32_t sz)
{
    if (r->top + sz > r->hi || r->top + sz < r->top) {
        rh_log("rh_heap: region %08x exhausted (top %08x + %08x)\n", r->lo, r->top, sz);
        return 0;
    }
    uint32_t p = r->top; r->top += sz;
    if (r->commit_on_bump) commit_to(r, r->top);
    return p;
}

static uint32_t span_alloc(region_t *r, uint32_t sz)
{
    for (int i = 0; i < r->nfree; i++) {
        if (r->free[i].sz < sz) continue;
        uint32_t p = r->free[i].lo;
        r->free[i].lo += sz; r->free[i].sz -= sz;
        if (!r->free[i].sz) { memmove(&r->free[i], &r->free[i + 1], (r->nfree - i - 1) * sizeof(span_t)); r->nfree--; }
        return p;
    }
    return bump(r, sz);
}

static void span_free(region_t *r, uint32_t lo, uint32_t sz)
{
    int i = 0;
    while (i < r->nfree && r->free[i].lo < lo) i++;
    if (r->nfree == r->capfree) {
        r->capfree = r->capfree ? r->capfree * 2 : 256;
        r->free = realloc(r->free, r->capfree * sizeof(span_t));
    }
    memmove(&r->free[i + 1], &r->free[i], (r->nfree - i) * sizeof(span_t));
    r->free[i].lo = lo; r->free[i].sz = sz; r->nfree++;
    if (i + 1 < r->nfree && r->free[i].lo + r->free[i].sz == r->free[i + 1].lo) {   /* merge right */
        r->free[i].sz += r->free[i + 1].sz;
        memmove(&r->free[i + 1], &r->free[i + 2], (r->nfree - i - 2) * sizeof(span_t)); r->nfree--;
    }
    if (i > 0 && r->free[i - 1].lo + r->free[i - 1].sz == r->free[i].lo) {          /* merge left */
        r->free[i - 1].sz += r->free[i].sz;
        memmove(&r->free[i], &r->free[i + 1], (r->nfree - i - 1) * sizeof(span_t)); r->nfree--;
    }
}

static void *arena_alloc(uint32_t n, int zero)
{
    hdr_t *h;
    if (n <= 0x10000u) {
        uint32_t cap, cls = small_class(n, &cap);
        uint32_t p = g_freelist[cls];
        if (p) {
            h = (hdr_t *)(uintptr_t)p;
            g_freelist[cls] = *(uint32_t *)(h + 1);
        } else {
            p = bump(&R_SMALL, cap + sizeof(hdr_t));
            if (!p) return NULL;
            h = (hdr_t *)(uintptr_t)p;
            h->cap = cap; h->cls = cls;
        }
    } else {
        uint32_t chunk = (n + sizeof(hdr_t) + 0xfffu) & ~0xfffu;
        uint32_t p = span_alloc(&R_LARGE, chunk);
        if (!p) return NULL;
        h = (hdr_t *)(uintptr_t)p;
        h->cap = chunk - sizeof(hdr_t); h->cls = CLS_LARGE;
    }
    h->req = n; h->magic = MAGIC_LIVE;
    if (zero) memset(h + 1, 0, h->cap);
    g_nalloc++; g_inuse += h->cap; if (g_inuse > g_inuse_max) g_inuse_max = g_inuse;
    return h + 1;
}

static hdr_t *hdr_of(const void *p)
{
    uint32_t a = (uint32_t)(uintptr_t)p;
    if (!(in_region(&R_SMALL, a) || in_region(&R_LARGE, a)) || (a & 15u)) return NULL;
    hdr_t *h = (hdr_t *)p - 1;
    return h->magic == MAGIC_LIVE ? h : NULL;
}

static void arena_free(hdr_t *h)
{
    h->magic = MAGIC_FREE;
    g_nfree++; g_inuse -= h->cap;
    if (h->cls == CLS_LARGE) span_free(&R_LARGE, (uint32_t)(uintptr_t)h, h->cap + sizeof(hdr_t));
    else { *(uint32_t *)(h + 1) = g_freelist[h->cls]; g_freelist[h->cls] = (uint32_t)(uintptr_t)h; }
}

/* ---- retail IAT replacements ---- */
static int g_bad_logged;
static void bad_ptr(const char *op, const void *p)
{
    if (g_bad_logged++ < 20) rh_log("rh_heap: %s on foreign/corrupt pointer %p (frame %u)\n", op, p, g_frame);
}

static HANDLE WINAPI my_HeapCreate(DWORD fl, SIZE_T init, SIZE_T max)
{ (void)fl; (void)init; (void)max; lock(); HANDLE h = (HANDLE)(uintptr_t)(FAKE_HEAP_BASE + ++g_nheaps); unlock(); return h; }
static BOOL WINAPI my_HeapDestroy(HANDLE h) { (void)h; return TRUE; }
static HANDLE WINAPI my_GetProcessHeap(void) { return (HANDLE)(uintptr_t)FAKE_PROCESS_HEAP; }
static BOOL WINAPI my_HeapValidate(HANDLE h, DWORD fl, LPCVOID p) { (void)h; (void)fl; return !p || hdr_of(p) != NULL; }

static LPVOID WINAPI my_HeapAlloc(HANDLE h, DWORD fl, SIZE_T n)
{
    (void)h;
    lock(); void *p = arena_alloc((uint32_t)n, (fl & HEAP_ZERO_MEMORY) != 0); unlock();
    if (O.log_heap && g_nalloc <= 50) rh_log("HeapAlloc(%lu) = %p\n", (unsigned long)n, p);
    return p;
}
static BOOL WINAPI my_HeapFree(HANDLE h, DWORD fl, LPVOID p)
{
    (void)h; (void)fl;
    if (!p) return TRUE;
    lock(); hdr_t *hd = hdr_of(p); if (hd) arena_free(hd); unlock();
    if (!hd) { bad_ptr("HeapFree", p); return FALSE; }
    return TRUE;
}
static SIZE_T WINAPI my_HeapSize(HANDLE h, DWORD fl, LPCVOID p)
{
    (void)h; (void)fl;
    hdr_t *hd = hdr_of(p);
    if (!hd) { bad_ptr("HeapSize", p); return (SIZE_T)-1; }
    return hd->req;
}
static LPVOID WINAPI my_HeapReAlloc(HANDLE h, DWORD fl, LPVOID p, SIZE_T n)
{
    (void)h;
    if (!p) return my_HeapAlloc(h, fl, n);
    lock();
    hdr_t *hd = hdr_of(p);
    void *r = NULL;
    if (!hd) { unlock(); bad_ptr("HeapReAlloc", p); return NULL; }
    if (n <= hd->cap && (hd->cls == CLS_LARGE ? n > 0x10000u : n <= 0x10000u)) {
        if ((fl & HEAP_ZERO_MEMORY) && n > hd->req) memset((uint8_t *)p + hd->req, 0, n - hd->req);
        hd->req = (uint32_t)n; r = p;
    } else if (!(fl & HEAP_REALLOC_IN_PLACE_ONLY)) {
        r = arena_alloc((uint32_t)n, 0);
        if (r) {
            uint32_t keep = hd->req < n ? hd->req : (uint32_t)n;
            memcpy(r, p, keep);
            if ((fl & HEAP_ZERO_MEMORY) && n > keep) memset((uint8_t *)r + keep, 0, n - keep);
            arena_free(hd);
        }
    }
    unlock();
    return r;
}

static span_t *vlive_find(uint32_t a)
{
    for (int i = 0; i < g_nvlive; i++) if (g_vlive[i].lo == a) return &g_vlive[i];
    return NULL;
}
static LPVOID WINAPI my_VirtualAlloc(LPVOID a, SIZE_T n, DWORD type, DWORD prot)
{
    uint32_t addr = (uint32_t)(uintptr_t)a;
    if (!a) {
        uint32_t sz = ((uint32_t)n + 0xffffu) & ~0xffffu;
        lock();
        uint32_t p = span_alloc(&R_VREG, sz);
        if (p) {
            if (g_nvlive == g_capvlive) { g_capvlive = g_capvlive ? g_capvlive * 2 : 64; g_vlive = realloc(g_vlive, g_capvlive * sizeof(span_t)); }
            g_vlive[g_nvlive].lo = p; g_vlive[g_nvlive].sz = sz; g_nvlive++;
        }
        unlock();
        if (!p) return NULL;
        if (type & MEM_COMMIT) {
            if (!VirtualAlloc((void *)(uintptr_t)p, n, MEM_COMMIT, prot)) return NULL;
            memset((void *)(uintptr_t)p, 0, n);   /* recommitted pages may hold old data on some hosts */
        }
        return (void *)(uintptr_t)p;
    }
    if (in_region(&R_VREG, addr)) {
        if (type & MEM_COMMIT) return VirtualAlloc(a, n, MEM_COMMIT, prot);
        return a;
    }
    return VirtualAlloc(a, n, type, prot);
}
static BOOL WINAPI my_VirtualFree(LPVOID a, SIZE_T n, DWORD type)
{
    uint32_t addr = (uint32_t)(uintptr_t)a;
    if (!in_region(&R_VREG, addr)) return VirtualFree(a, n, type);
    if (type & MEM_RELEASE) {
        lock();
        span_t *s = vlive_find(addr);
        BOOL ok = FALSE;
        if (s) {
            VirtualFree(a, s->sz, MEM_DECOMMIT);
            span_free(&R_VREG, s->lo, s->sz);
            *s = g_vlive[--g_nvlive];
            ok = TRUE;
        }
        unlock();
        return ok;
    }
    return VirtualFree(a, n, MEM_DECOMMIT);
}

static int reserve(region_t *r)
{
    void *p = VirtualAlloc((void *)(uintptr_t)r->lo, r->hi - r->lo, MEM_RESERVE, PAGE_READWRITE);
    if (p != (void *)(uintptr_t)r->lo) {
        MEMORY_BASIC_INFORMATION mi; VirtualQuery((void *)(uintptr_t)r->lo, &mi, sizeof mi);
        fprintf(stderr, "rh_heap: cannot reserve %08x..%08x (err %lu; state %lx base %p)\n",
                r->lo, r->hi, GetLastError(), mi.State, mi.AllocationBase);
        return -1;
    }
    return 0;
}

int rh_heap_init(void)
{
    MEMORY_BASIC_INFORMATION mi;
    VirtualQuery((void *)rh_heap_init, &mi, sizeof mi);
    g_host_lo = (uint32_t)(uintptr_t)mi.AllocationBase;
    IMAGE_NT_HEADERS32 *nt = (void *)(g_host_lo + ((IMAGE_DOS_HEADER *)(uintptr_t)g_host_lo)->e_lfanew);
    g_host_hi = g_host_lo + nt->OptionalHeader.SizeOfImage;
    InitializeCriticalSection(&g_lk);
    if (reserve(&R_SMALL) || reserve(&R_LARGE) || reserve(&R_VREG)) return -1;
    g_active = 1;
    return 0;
}

void rh_heap_install(void)
{
    iat_override("HeapCreate", my_HeapCreate);
    iat_override("HeapDestroy", my_HeapDestroy);
    iat_override("GetProcessHeap", my_GetProcessHeap);
    iat_override("HeapAlloc", my_HeapAlloc);
    iat_override("HeapFree", my_HeapFree);
    iat_override("HeapSize", my_HeapSize);
    iat_override("HeapReAlloc", my_HeapReAlloc);
    iat_override("HeapValidate", my_HeapValidate);
    iat_override("VirtualAlloc", my_VirtualAlloc);
    iat_override("VirtualFree", my_VirtualFree);
}

/* committed sub-ranges of the arena (deterministic: commit pattern follows the
 * allocation sequence) — for hashing / dumps / shadow snapshots */
int rh_heap_ranges(uint32_t (*out)[2], int max)
{
    int n = 0;
    if (!g_active) return 0;
    uint32_t a = ARENA_LO;
    while (a < ARENA_HI && n < max) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t lo = (uint32_t)(uintptr_t)mi.BaseAddress, hi = lo + (uint32_t)mi.RegionSize;
        if (hi > ARENA_HI) hi = ARENA_HI;
        if (mi.State == MEM_COMMIT) {
            if (n && out[n - 1][1] == lo) out[n - 1][1] = hi;
            else { out[n][0] = lo; out[n][1] = hi; n++; }
        }
        a = hi;
    }
    return n;
}

/* allocator metadata snapshot (host-side state that is NOT inside the arena), so a
 * shadow run can roll back allocations made by the shadowed function */
typedef struct {
    region_t s, l, v; span_t *sf, *lf, *vf, *vl; int nvl;
    uint32_t fl[N_CLS]; uint64_t na, nf, iu, ium;
} heap_meta_t;
static span_t *dup_spans(const span_t *s, int n) { span_t *d = malloc((n ? n : 1) * sizeof *d); memcpy(d, s, n * sizeof *d); return d; }
void *rh_heap_meta_save(void)
{
    lock();
    heap_meta_t *m = malloc(sizeof *m);
    m->s = R_SMALL; m->l = R_LARGE; m->v = R_VREG;
    m->sf = dup_spans(R_SMALL.free, R_SMALL.nfree); m->lf = dup_spans(R_LARGE.free, R_LARGE.nfree);
    m->vf = dup_spans(R_VREG.free, R_VREG.nfree); m->vl = dup_spans(g_vlive, g_nvlive); m->nvl = g_nvlive;
    memcpy(m->fl, g_freelist, sizeof g_freelist);
    m->na = g_nalloc; m->nf = g_nfree; m->iu = g_inuse; m->ium = g_inuse_max;
    unlock();
    return m;
}
static void restore_region(region_t *r, const region_t *s, const span_t *f)
{
    if (r->capfree < s->nfree) { r->capfree = s->nfree; r->free = realloc(r->free, r->capfree * sizeof(span_t)); }
    memcpy(r->free, f, s->nfree * sizeof(span_t));
    r->nfree = s->nfree; r->top = s->top;      /* keep r->committed: pages stay committed */
}
void rh_heap_meta_restore(void *p)
{
    heap_meta_t *m = p;
    lock();
    restore_region(&R_SMALL, &m->s, m->sf); restore_region(&R_LARGE, &m->l, m->lf); restore_region(&R_VREG, &m->v, m->vf);
    if (g_capvlive < m->nvl) { g_capvlive = m->nvl; g_vlive = realloc(g_vlive, g_capvlive * sizeof(span_t)); }
    memcpy(g_vlive, m->vl, m->nvl * sizeof(span_t)); g_nvlive = m->nvl;
    memcpy(g_freelist, m->fl, sizeof g_freelist);
    g_nalloc = m->na; g_nfree = m->nf; g_inuse = m->iu; g_inuse_max = m->ium;
    unlock();
}
void rh_heap_meta_free(void *p)
{
    heap_meta_t *m = p; if (!m) return;
    free(m->sf); free(m->lf); free(m->vf); free(m->vl); free(m);
}

void rh_heap_stats(char *buf, size_t n)
{
    snprintf(buf, n, "heap: allocs=%llu frees=%llu inuse=%lluK max=%lluK small_top=+%uK large_top=+%uK vreg_top=+%uK heaps=%u",
             (unsigned long long)g_nalloc, (unsigned long long)g_nfree, (unsigned long long)(g_inuse >> 10),
             (unsigned long long)(g_inuse_max >> 10), (R_SMALL.top - R_SMALL.lo) >> 10,
             (R_LARGE.top - R_LARGE.lo) >> 10, (R_VREG.top - R_VREG.lo) >> 10, g_nheaps);
}
