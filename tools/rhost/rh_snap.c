/* rh_snap.c — in-process savestates for retail (needs --headless: rh_dev.c).
 *
 * At a tick boundary everything a tick can change is in this process's deterministic
 * regions: retail's .data/.bss, its heap arena, the DEV region (the fake devices' objects,
 * textures, buffers), a few bytes of the main stack, and a handful of host variables. A
 * savestate is a copy of that; restoring it and running on gives the state a straight
 * replay reaches (the gate: `rhost.py snap-check`, state hashes equal).
 *
 * Cost model. ~210 MB of tracked memory, so nothing is copied whole more than once:
 *   anchor   full copy of the tracked regions (the BASE) + every tracked page write-
 *            protected. The first write to a page faults once (rh_snap_fault: mark dirty,
 *            unprotect, continue); the page stays writable until the next anchor.
 *   D        the pages written since the anchor (append-only).
 *   take     for each page of D: shared with the parent savestate's copy if the bytes are
 *            equal, "= base" if equal to the base, else a fresh 4 KB slab. A savestate is
 *            |D| slab references + the stack bytes + the host blob.
 *   restore  for each page of D: copy from the savestate's slab, or from the base.
 * So both cost O(|D|), not O(state); re-anchor when D has grown (a new scene).
 * Pages committed after the anchor (heap / DEV growth) are dirty from birth, base = zeros.
 * Pages DEcommitted after the anchor (retail's VirtualFree of a VREG region: the CRT's
 * small-block heap gives regions back, e.g. after the dungeon results → town teardown) are
 * "gone": in D, never read (a savestate records GONE instead of bytes), and a restore
 * re-commits / decommits each page to the savestate's commit state before copying.
 *
 * Kernel-mode writes do not fault into the handler — a ReadFile into a protected heap page
 * just fails (wine: STATUS_ACCESS_VIOLATION from the syscall). Every retail import that
 * lets the system write into retail memory therefore passes through a wrapper that calls
 * rh_snap_touch() on the destination first (below + rh_handles.c). A missed one shows up
 * in snap-check: the tracked run's state hash differs from the plain replay's. */
#include "rhost.h"
#include <mmsystem.h>

#define PGSZ 4096u
#define NPG (1u << 20)
enum { PG_NONE = 0, PG_CLEAN = 1, PG_DIRTY = 2 };

static uint8_t *g_pst;                   /* [NPG] page state */
static uint8_t *g_gone;                  /* [NPG] 1 = in D and decommitted now (no bytes to read or write) */
#define H_GONE 0xffffffffu               /* savestate page handle: decommitted when taken */
static uint8_t **g_base;                 /* [NPG] page → its bytes in the base copy (NULL: zeros / untracked) */
static uint8_t *g_basebuf; static size_t g_basesz;
static uint32_t *g_D; static uint32_t g_nD, g_capD;
static int g_anchored;
static uint64_t g_faults, g_touches, g_takes, g_restores, g_anchors, g_decommits, g_recommits;
static uint8_t g_zero[PGSZ];

/* ---- slab pool: 4 KB page copies, refcounted, addressed by index ---- */
#define CHUNK_SLABS 4096u                /* 16 MB chunks */
#define MAX_CHUNKS 96u                   /* <= 1.5 GB */
static uint8_t *g_chunk[MAX_CHUNKS]; static uint16_t *g_ref[MAX_CHUNKS]; static uint32_t g_nchunk;
static uint32_t *g_free; static uint32_t g_nfree, g_capfree, g_live_slabs, g_slab_cap = 60 * CHUNK_SLABS;   /* 960 MB */
static uint8_t *slab_ptr(uint32_t i) { return g_chunk[i / CHUNK_SLABS] + (size_t)(i % CHUNK_SLABS) * PGSZ; }
static uint32_t slab_new(void)           /* → index + 1, 0 = pool exhausted */
{
    if (!g_nfree) {
        if (g_nchunk >= MAX_CHUNKS || g_nchunk * CHUNK_SLABS >= g_slab_cap) return 0;
        uint8_t *c = VirtualAlloc(NULL, CHUNK_SLABS * PGSZ, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!c) return 0;
        g_chunk[g_nchunk] = c; g_ref[g_nchunk] = calloc(CHUNK_SLABS, sizeof(uint16_t));
        if (g_capfree < (g_nchunk + 1) * CHUNK_SLABS) { g_capfree = (g_nchunk + 1) * CHUNK_SLABS; g_free = realloc(g_free, g_capfree * 4); }
        for (uint32_t k = CHUNK_SLABS; k-- > 0; ) g_free[g_nfree++] = g_nchunk * CHUNK_SLABS + k;
        g_nchunk++;
    }
    uint32_t i = g_free[--g_nfree];
    g_ref[i / CHUNK_SLABS][i % CHUNK_SLABS] = 1; g_live_slabs++;
    return i + 1;
}
static void slab_ref(uint32_t h) { g_ref[(h - 1) / CHUNK_SLABS][(h - 1) % CHUNK_SLABS]++; }
static void slab_unref(uint32_t h)
{
    uint32_t i = h - 1;
    if (--g_ref[i / CHUNK_SLABS][i % CHUNK_SLABS] == 0) { g_free[g_nfree++] = i; g_live_slabs--; }
}

/* ---- host state: plain blocks registered by the modules + the special ones ---- */
#define MAX_BLK 48
static struct { const char *name; void *p; size_t n; } g_blk[MAX_BLK]; static int g_nblk; static size_t g_blk_total;
void rh_snap_register(const char *name, void *p, size_t n)
{
    if (g_nblk >= MAX_BLK) rh_fatal("rh_snap: too many host state blocks");
    g_blk[g_nblk].name = name; g_blk[g_nblk].p = p; g_blk[g_nblk].n = n; g_nblk++; g_blk_total += n;
}

typedef struct snap {
    int id; uint32_t count; uint32_t *pp;            /* per D index: slab handle, 0 = base */
    uint8_t *host; void *heap_meta; void *handles; void *files; void *trace; void *text;
    uint8_t *stack; uint32_t stack_lo, stack_n;
    uint16_t cw; uint32_t mxcsr; uint32_t frame;
} snap_t;
#define MAX_SNAP 65536
static snap_t **g_snap; static int g_next_id = 1, g_live_snaps; static snap_t *g_cur;

static void page_dirty(uint32_t pg, int unprotect)
{
    if (g_nD == g_capD) { g_capD = g_capD ? g_capD * 2 : 8192; g_D = realloc(g_D, g_capD * 4); }
    g_D[g_nD++] = pg; g_pst[pg] = PG_DIRTY;
    if (unprotect) { DWORD old; VirtualProtect((void *)(uintptr_t)(pg << 12), PGSZ, PAGE_READWRITE, &old); }
}

int rh_snap_fault(EXCEPTION_POINTERS *e)
{
    const EXCEPTION_RECORD *r = e->ExceptionRecord;
    if (!g_anchored || r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 || r->ExceptionInformation[0] != 1) return 0;
    uint32_t pg = (uint32_t)r->ExceptionInformation[1] >> 12;
    if (g_pst[pg] != PG_CLEAN) return 0;
    g_faults++;
    page_dirty(pg, 1);
    return 1;
}
void rh_snap_touch(const void *p, size_t n)
{
    if (!g_anchored || !p || !n) return;
    uint32_t a = (uint32_t)(uintptr_t)p, lo = a >> 12, hi = (a + (uint32_t)n - 1) >> 12;
    if (hi < lo) hi = NPG - 1;
    for (uint32_t pg = lo; pg <= hi; pg++)
        if (g_pst[pg] == PG_CLEAN) { g_touches++; page_dirty(pg, 1); }
}
void rh_snap_committed(uint32_t lo, uint32_t hi)
{
    if (!g_anchored) return;
    for (uint32_t pg = lo >> 12; pg < (hi + PGSZ - 1) >> 12; pg++) {
        g_gone[pg] = 0;
        if (g_pst[pg] == PG_NONE) { g_base[pg] = NULL; page_dirty(pg, 0); }
        else if (g_pst[pg] == PG_CLEAN) page_dirty(pg, 0);      /* the commit call reset its protection */
    }
}

/* after a VirtualFree in a deterministic region: every tracked page of [lo, hi) that is no
 * longer committed joins D (a CLEAN one's bytes = the base) and is marked gone */
void rh_snap_decommitted(uint32_t lo, uint32_t hi)
{
    if (!g_anchored) return;
    for (uint32_t a = lo & ~(PGSZ - 1); a < hi; ) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery((void *)(uintptr_t)a, &mi, sizeof mi)) break;
        uint32_t e = (uint32_t)(uintptr_t)mi.BaseAddress + (uint32_t)mi.RegionSize;
        if (e > hi || e <= a) e = (hi + PGSZ - 1) & ~(PGSZ - 1);
        if (mi.State != MEM_COMMIT)
            for (uint32_t pg = a >> 12; pg < e >> 12; pg++) {
                if (g_pst[pg] == PG_NONE || g_gone[pg]) continue;
                if (g_pst[pg] == PG_CLEAN) page_dirty(pg, 0);
                g_gone[pg] = 1; g_decommits++;
            }
        a = e;
    }
}
/* bring page PG to the commit state a savestate wants (before writing its bytes) */
static void page_commit_state(uint32_t pg, int gone)
{
    void *p = (void *)(uintptr_t)(pg << 12);
    if (gone == g_gone[pg]) return;
    if (gone) { if (!VirtualFree(p, PGSZ, MEM_DECOMMIT)) rh_fatal("rh_snap: decommit %08x failed (err %lu)", pg << 12, GetLastError()); }
    else if (!VirtualAlloc(p, PGSZ, MEM_COMMIT, PAGE_READWRITE)) rh_fatal("rh_snap: re-commit %08x failed (err %lu)", pg << 12, GetLastError());
    g_gone[pg] = (uint8_t)gone;
}

#define MAXR 4096
static int tracked_ranges(uint32_t (*r)[2])
{
    r[0][0] = RT_DATA_LO & ~(PGSZ - 1); r[0][1] = (RT_DATA_HI + PGSZ - 1) & ~(PGSZ - 1);
    int n = 1 + rh_heap_ranges(r + 1, MAXR - 2);
    if (rh_dev_committed() > RH_DEV_LO) { r[n][0] = RH_DEV_LO; r[n][1] = rh_dev_committed(); n++; }
    return n;
}

static void snap_free_one(snap_t *s)
{
    for (uint32_t i = 0; i < s->count; i++) if (s->pp[i] && s->pp[i] != H_GONE) slab_unref(s->pp[i]);
    free(s->pp); free(s->host); free(s->stack); free(s->handles); free(s->trace); free(s->text); rh_files_snap_free(s->files);
    rh_heap_meta_free(s->heap_meta);
    g_snap[s->id] = NULL; g_live_snaps--;
    if (g_cur == s) g_cur = NULL;
    free(s);
}
void rh_snap_free(int id) { if (id > 0 && id < MAX_SNAP && g_snap && g_snap[id]) snap_free_one(g_snap[id]); }

/* full base copy; every tracked page write-protected; all savestates dropped */
int rh_snap_anchor(void)
{
    static uint32_t r[MAXR][2];
    if (!O.headless) { rh_log("snap: savestates need --headless (device state must live in the DEV region)\n"); return -1; }
    if (!g_pst) {
        g_pst = VirtualAlloc(NULL, NPG, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        g_gone = VirtualAlloc(NULL, NPG, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        g_base = VirtualAlloc(NULL, NPG * sizeof *g_base, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        g_snap = calloc(MAX_SNAP, sizeof *g_snap);
        if (!g_pst || !g_gone || !g_base || !g_snap) rh_fatal("rh_snap: tables");
    }
    for (int i = 1; i < MAX_SNAP; i++) if (g_snap[i]) snap_free_one(g_snap[i]);
    /* writable again while we re-read everything (and for the pages that left the set) */
    for (uint32_t i = 0; i < g_nD; i++) { g_pst[g_D[i]] = PG_NONE; g_gone[g_D[i]] = 0; }   /* gone pages: uncommitted, out of the new set */
    for (uint32_t pg = 0; pg < NPG; pg++)
        if (g_pst[pg] == PG_CLEAN) { DWORD old; VirtualProtect((void *)(uintptr_t)(pg << 12), PGSZ, PAGE_READWRITE, &old); g_pst[pg] = PG_NONE; }
    g_nD = 0; g_anchored = 0;
    int n = tracked_ranges(r);
    size_t tot = 0;
    for (int i = 0; i < n; i++) tot += r[i][1] - r[i][0];
    if (tot > g_basesz) {
        if (g_basebuf) VirtualFree(g_basebuf, 0, MEM_RELEASE);
        g_basesz = tot + (32u << 20);
        g_basebuf = VirtualAlloc(NULL, g_basesz, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!g_basebuf) { g_basesz = 0; rh_log("snap: cannot allocate the %u MB base copy\n", (unsigned)(tot >> 20)); return -1; }
    }
    memset(g_base, 0, NPG * sizeof *g_base);
    size_t o = 0;
    for (int i = 0; i < n; i++) {
        size_t l = r[i][1] - r[i][0];
        memcpy(g_basebuf + o, (void *)(uintptr_t)r[i][0], l);
        for (uint32_t pg = r[i][0] >> 12; pg < r[i][1] >> 12; pg++) { g_base[pg] = g_basebuf + o + ((size_t)(pg - (r[i][0] >> 12)) << 12); g_pst[pg] = PG_CLEAN; }
        DWORD old;
        if (!VirtualProtect((void *)(uintptr_t)r[i][0], l, PAGE_READONLY, &old))
            rh_fatal("rh_snap: VirtualProtect %08x+%x failed (err %lu)", r[i][0], (unsigned)l, GetLastError());
        o += l;
    }
    g_anchored = 1; g_anchors++; g_cur = NULL;
    return 0;
}

int rh_snap_take(void)
{
    if (!g_anchored) return 0;
    int id = 0;
    for (int k = 0; k < MAX_SNAP - 1; k++) { int c = g_next_id; g_next_id = g_next_id + 1 < MAX_SNAP ? g_next_id + 1 : 1; if (!g_snap[c]) { id = c; break; } }
    if (!id) return 0;
    snap_t *s = calloc(1, sizeof *s), *par = g_cur;
    s->id = id; s->count = g_nD; s->pp = malloc((g_nD ? g_nD : 1) * 4); s->frame = g_frame;
    for (uint32_t i = 0; i < g_nD; i++) {
        uint32_t pg = g_D[i];
        const uint8_t *cur = (const uint8_t *)(uintptr_t)(pg << 12);
        uint32_t ph = par && i < par->count ? par->pp[i] : 0;
        if (g_gone[pg]) { s->pp[i] = H_GONE; continue; }
        if (ph == H_GONE) ph = 0;                    /* parent had it decommitted: compare with the base */
        if (ph && !memcmp(cur, slab_ptr(ph - 1), PGSZ)) { slab_ref(ph); s->pp[i] = ph; continue; }
        if (!ph && !memcmp(cur, g_base[pg] ? g_base[pg] : g_zero, PGSZ)) { s->pp[i] = 0; continue; }
        uint32_t h = slab_new();
        if (!h) {                                    /* pool exhausted: undo */
            for (uint32_t k = 0; k < i; k++) if (s->pp[k] && s->pp[k] != H_GONE) slab_unref(s->pp[k]);
            free(s->pp); free(s);
            return 0;
        }
        memcpy(slab_ptr(h - 1), cur, PGSZ);
        s->pp[i] = h;
    }
    s->host = malloc(g_blk_total ? g_blk_total : 1);
    size_t o = 0;
    for (int i = 0; i < g_nblk; i++) { memcpy(s->host + o, g_blk[i].p, g_blk[i].n); o += g_blk[i].n; }
    s->heap_meta = rh_heap_meta_save();
    s->handles = rh_handles_snap();
    s->files = rh_files_snap();
    s->trace = rh_trace_snap();
    s->text = O.live ? rh_text_snap() : NULL;
    rh_game_stack(&s->stack_lo, &s->stack_n);
    s->stack = malloc(s->stack_n ? s->stack_n : 1);
    memcpy(s->stack, (void *)(uintptr_t)s->stack_lo, s->stack_n);
    __asm__ volatile("fnstcw %0" : "=m"(s->cw));
    __asm__ volatile("stmxcsr %0" : "=m"(s->mxcsr));
    g_snap[id] = s; g_live_snaps++; g_cur = s; g_takes++;
    return id;
}

int rh_snap_restore(int id)
{
    if (!g_anchored || id <= 0 || id >= MAX_SNAP || !g_snap[id]) return -1;
    snap_t *s = g_snap[id];
    for (uint32_t i = 0; i < g_nD; i++) {
        uint32_t pg = g_D[i], h = i < s->count ? s->pp[i] : 0;
        if (h == H_GONE || g_gone[pg]) { if (g_gone[pg] && h != H_GONE) g_recommits++; page_commit_state(pg, h == H_GONE); }
        if (h == H_GONE) continue;
        memcpy((void *)(uintptr_t)(pg << 12), h ? slab_ptr(h - 1) : g_base[pg] ? g_base[pg] : g_zero, PGSZ);
    }
    size_t o = 0;
    for (int i = 0; i < g_nblk; i++) { memcpy(g_blk[i].p, s->host + o, g_blk[i].n); o += g_blk[i].n; }
    rh_heap_meta_restore(s->heap_meta);
    rh_handles_unsnap(s->handles);
    rh_files_unsnap(s->files);
    rh_trace_unsnap(s->trace);
    if (O.live) rh_text_unsnap(s->text);
    uint32_t lo, n; rh_game_stack(&lo, &n);
    if (lo != s->stack_lo || n != s->stack_n) rh_fatal("rh_snap: the game stack moved (%08x+%u, savestate %08x+%u)", lo, n, s->stack_lo, s->stack_n);
    memcpy((void *)(uintptr_t)lo, s->stack, n);
    __asm__ volatile("fldcw %0" : : "m"(s->cw));
    __asm__ volatile("ldmxcsr %0" : : "m"(s->mxcsr));
    g_cur = s; g_restores++;
    return 0;
}
/* dwords that differ between savestate ID and memory now (pages written since the anchor
 * only — everything else is equal by construction), addresses in [lo, hi) */
long rh_snap_diff(int id, uint32_t lo, uint32_t hi, void (*cb)(uint32_t va, uint32_t was, uint32_t now, void *u), void *u)
{
    if (!g_anchored || id <= 0 || id >= MAX_SNAP || !g_snap[id]) return -1;
    const snap_t *s = g_snap[id];
    long n = 0;
    for (uint32_t i = 0; i < g_nD; i++) {
        uint32_t pg = g_D[i], base = pg << 12, h = i < s->count ? s->pp[i] : 0;
        if (base + PGSZ <= lo || base >= hi) continue;
        if (h == H_GONE || g_gone[pg]) {                /* decommitted then or now: count it as one change if not both */
            if ((h == H_GONE) != g_gone[pg]) { n++; if (cb) cb(base, h == H_GONE ? 0xdeadc0deu : 0, g_gone[pg] ? 0xdeadc0deu : 0, u); }
            continue;
        }
        const uint32_t *was = (const uint32_t *)(h ? slab_ptr(h - 1) : g_base[pg] ? g_base[pg] : g_zero), *now = (const uint32_t *)(uintptr_t)base;
        if (!memcmp(was, now, PGSZ)) continue;
        for (uint32_t k = 0; k < PGSZ / 4; k++)
            if (was[k] != now[k] && base + k * 4 >= lo && base + k * 4 < hi) { n++; if (cb) cb(base + k * 4, was[k], now[k], u); }
    }
    return n;
}
int rh_snap_frame(int id) { return id > 0 && id < MAX_SNAP && g_snap && g_snap[id] ? (int)g_snap[id]->frame : -1; }
int rh_snap_anchored(void) { return g_anchored; }
uint32_t rh_snap_dirty(void) { return g_nD; }

void rh_snap_stats(char *b, size_t n)
{
    snprintf(b, n, "snap: anchors=%llu dirty=%u pages takes=%llu restores=%llu live=%d slabs=%u (%u MB) faults=%llu touches=%llu decommits=%llu recommits=%llu base=%u MB",
             (unsigned long long)g_anchors, g_nD, (unsigned long long)g_takes, (unsigned long long)g_restores, g_live_snaps, g_live_slabs,
             g_live_slabs >> 8, (unsigned long long)g_faults, (unsigned long long)g_touches,
             (unsigned long long)g_decommits, (unsigned long long)g_recommits, (unsigned)(g_basesz >> 20));
}

/* ---- imports through which the SYSTEM writes into retail memory: touch first ----
 * (user-mode writers — NLS, wsprintf, the CRT — fault into rh_snap_fault like retail's own
 * code; these are the ones whose write can happen in kernel mode / wine's unix side) */
#define PREV(name) static __typeof__(name) *p_##name
PREV(GetGlyphOutlineA);
PREV(GetTextMetricsA);
PREV(GetObjectA);
PREV(CreateDIBSection);
PREV(GetClientRect);
PREV(GetWindowRect);
PREV(GetWindowPlacement);
PREV(AdjustWindowRect);
PREV(GetClassNameA);
PREV(GetWindowTextA);
PREV(RegQueryValueExA);
PREV(EnumDisplaySettingsA);
PREV(GetVersionExA);
PREV(GetCurrentDirectoryA);
PREV(timeGetDevCaps);
PREV(DrawTextA);
PREV(DrawTextW);
PREV(PeekMessageA);
PREV(GetMessageA);
PREV(GetCPInfo);
#define T(p, n) rh_snap_touch((p), (n))
static DWORD WINAPI w_GetGlyphOutlineA(HDC h, UINT c, UINT f, LPGLYPHMETRICS gm, DWORD cb, LPVOID buf, const MAT2 *m)
{ T(gm, sizeof *gm); T(buf, cb); return p_GetGlyphOutlineA(h, c, f, gm, cb, buf, m); }
static BOOL WINAPI w_GetTextMetricsA(HDC h, LPTEXTMETRICA tm) { T(tm, sizeof *tm); return p_GetTextMetricsA(h, tm); }
static int WINAPI w_GetObjectA(HANDLE h, int c, LPVOID pv) { if (c > 0) T(pv, (size_t)c); return p_GetObjectA(h, c, pv); }
static HBITMAP WINAPI w_CreateDIBSection(HDC h, const BITMAPINFO *bi, UINT u, VOID **bits, HANDLE s, DWORD o)
{ T(bits, sizeof *bits); return p_CreateDIBSection(h, bi, u, bits, s, o); }
static BOOL WINAPI w_GetClientRect(HWND h, LPRECT r) { T(r, sizeof *r); return p_GetClientRect(h, r); }
static BOOL WINAPI w_GetWindowRect(HWND h, LPRECT r) { T(r, sizeof *r); return p_GetWindowRect(h, r); }
static BOOL WINAPI w_GetWindowPlacement(HWND h, WINDOWPLACEMENT *p) { T(p, sizeof *p); return p_GetWindowPlacement(h, p); }
static BOOL WINAPI w_AdjustWindowRect(LPRECT r, DWORD st, BOOL menu) { T(r, sizeof *r); return p_AdjustWindowRect(r, st, menu); }
static int WINAPI w_GetClassNameA(HWND h, LPSTR b, int n) { if (n > 0) T(b, (size_t)n); return p_GetClassNameA(h, b, n); }
static int WINAPI w_GetWindowTextA(HWND h, LPSTR b, int n) { if (n > 0) T(b, (size_t)n); return p_GetWindowTextA(h, b, n); }
static LONG WINAPI w_RegQueryValueExA(HKEY k, LPCSTR v, LPDWORD res, LPDWORD type, LPBYTE data, LPDWORD cb)
{ T(type, 4); if (cb) { T(cb, 4); T(data, *cb); } return p_RegQueryValueExA(k, v, res, type, data, cb); }
static BOOL WINAPI w_EnumDisplaySettingsA(LPCSTR d, DWORD i, DEVMODEA *dm) { T(dm, sizeof *dm); return p_EnumDisplaySettingsA(d, i, dm); }
static BOOL WINAPI w_GetVersionExA(LPOSVERSIONINFOA v) { if (v) T(v, v->dwOSVersionInfoSize); return p_GetVersionExA(v); }
static DWORD WINAPI w_GetCurrentDirectoryA(DWORD n, LPSTR b) { T(b, n); return p_GetCurrentDirectoryA(n, b); }
static MMRESULT WINAPI w_timeGetDevCaps(LPTIMECAPS c, UINT n) { T(c, n); return p_timeGetDevCaps(c, n); }
static int WINAPI w_DrawTextA(HDC h, LPCSTR s, int n, LPRECT r, UINT f) { T(r, sizeof *r); return p_DrawTextA(h, s, n, r, f); }
static int WINAPI w_DrawTextW(HDC h, LPCWSTR s, int n, LPRECT r, UINT f) { T(r, sizeof *r); return p_DrawTextW(h, s, n, r, f); }
static BOOL WINAPI w_PeekMessageA(LPMSG m, HWND h, UINT a, UINT b, UINT f) { T(m, sizeof *m); return p_PeekMessageA(m, h, a, b, f); }
static BOOL WINAPI w_GetMessageA(LPMSG m, HWND h, UINT a, UINT b) { T(m, sizeof *m); return p_GetMessageA(m, h, a, b); }
static BOOL WINAPI w_GetCPInfo(UINT cp, LPCPINFO i) { T(i, sizeof *i); return p_GetCPInfo(cp, i); }
#undef T

/* after every other module's overrides: each wrapper chains to what the IAT held */
void rh_snap_install(void)
{
    #define WRAP(name) do { p_##name = iat_lookup(#name); if (p_##name) iat_override(#name, w_##name); } while (0)
    WRAP(GetGlyphOutlineA); WRAP(GetTextMetricsA); WRAP(GetObjectA); WRAP(CreateDIBSection); WRAP(GetClientRect);
    WRAP(GetWindowRect); WRAP(GetWindowPlacement); WRAP(AdjustWindowRect); WRAP(GetClassNameA); WRAP(GetWindowTextA);
    WRAP(RegQueryValueExA); WRAP(EnumDisplaySettingsA); WRAP(GetVersionExA); WRAP(GetCurrentDirectoryA); WRAP(timeGetDevCaps);
    WRAP(DrawTextA); WRAP(DrawTextW); WRAP(PeekMessageA); WRAP(GetMessageA); WRAP(GetCPInfo);
    #undef WRAP
}
