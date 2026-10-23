/*
 * mem_probe.c — see mem_probe.h.
 */
#include "mem_probe.h"

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <tlhelp32.h>

#include "d3d_tex_names.h"
#include "sim.h"   /* g_sim_frame_count */

/* the block-size histogram of the previous probe (sizes rounded up to 16) */
#define MP_BUCKETS 4096
static struct { size_t size; long n; } s_prev[MP_BUCKETS], s_cur[MP_BUCKETS];
static int s_nprev;

static int bucket(size_t size, int *n)
{
    for (int i = 0; i < *n; i++) if (s_cur[i].size == size) return i;
    if (*n == MP_BUCKETS) return -1;
    s_cur[*n].size = size;
    s_cur[*n].n = 0;
    return (*n)++;
}

void mem_probe(const char *what)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof ms;
    if (!GlobalMemoryStatusEx(&ms)) return;
    _HEAPINFO h;
    memset(&h, 0, sizeof h);
    size_t used = 0;
    long blocks = 0;
    int n = 0;
    while (_heapwalk(&h) == _HEAPOK) {
        if (h._useflag != _USEDENTRY) continue;
        used += h._size;
        ++blocks;
        const int b = bucket((h._size + 15) & ~(size_t)15, &n);
        if (b >= 0) ++s_cur[b].n;
    }
    /* the threads of this process, and the address space by kind: private
     * committed / private reserved-only (thread stacks are reservations) /
     * image + mapped, with the count of private regions */
    long threads = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof te;
        const DWORD pid = GetCurrentProcessId();
        for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
            if (te.th32OwnerProcessID == pid) ++threads;
        CloseHandle(snap);
    }
    unsigned long long priv_c = 0, priv_r = 0, other = 0;
    long regions = 0;
    static const void *s_rprev[8192];
    static const void *s_rcur[8192];
    static size_t s_rcur_sz[8192];
    static int s_nrprev;
    int nr = 0;
    MEMORY_BASIC_INFORMATION mi;
    for (const char *p = 0; VirtualQuery(p, &mi, sizeof mi) == sizeof mi; p = (const char *)mi.BaseAddress + mi.RegionSize) {
        if (mi.State == MEM_FREE) continue;
        if (mi.Type == MEM_PRIVATE) {
            if (mi.State == MEM_COMMIT) priv_c += mi.RegionSize; else priv_r += mi.RegionSize;
            ++regions;
            if (mi.State == MEM_COMMIT && mi.AllocationBase == mi.BaseAddress && nr < 8192) {
                s_rcur[nr] = mi.BaseAddress;
                s_rcur_sz[nr++] = mi.RegionSize;
            }
        } else {
            other += mi.RegionSize;
        }
        if ((const char *)mi.BaseAddress + mi.RegionSize < (const char *)mi.BaseAddress) break;
    }
    fprintf(stderr, "mem: f%u %s %lluMB used (private %lluMB committed + %lluMB reserved in %ld regions, "
            "image/mapped %lluMB) threads %ld heap %zuMB in %ld blocks", (unsigned)g_sim_frame_count, what,
            (unsigned long long)((ms.ullTotalVirtual - ms.ullAvailVirtual) >> 20), priv_c >> 20, priv_r >> 20,
            regions, other >> 20, threads, used >> 20, blocks);
    /* the 4 sizes that gained the most bytes since the last probe */
    for (int k = 0; k < 4; k++) {
        long long best = 0;
        int bi = -1;
        for (int i = 0; i < n; i++) {
            long prev = 0;
            for (int j = 0; j < s_nprev; j++) if (s_prev[j].size == s_cur[i].size) { prev = s_prev[j].n; break; }
            const long long gain = (long long)(s_cur[i].n - prev) * (long long)s_cur[i].size;
            if (gain > best) { best = gain; bi = i; }
        }
        if (bi < 0) break;
        long prev = 0;
        for (int j = 0; j < s_nprev; j++) if (s_prev[j].size == s_cur[bi].size) { prev = s_prev[j].n; break; }
        fprintf(stderr, " | %zuB +%ld", s_cur[bi].size, s_cur[bi].n - prev);
        s_cur[bi].n = -s_cur[bi].n;   /* mark taken (restored below) */
    }
    /* every heap of the process: its busy bytes / blocks (the biggest four) */
    {
        HANDLE heaps[64];
        DWORD nh = GetProcessHeaps(64, heaps);
        if (nh > 64) nh = 64;
        struct { HANDLE h; size_t busy; long n; } hs[64];
        for (DWORD i = 0; i < nh; i++) {
            hs[i].h = heaps[i];
            hs[i].busy = 0;
            hs[i].n = 0;
            if (!HeapLock(heaps[i])) continue;
            PROCESS_HEAP_ENTRY e;
            e.lpData = NULL;
            while (HeapWalk(heaps[i], &e))
                if (e.wFlags & PROCESS_HEAP_ENTRY_BUSY) { hs[i].busy += e.cbData; ++hs[i].n; }
            HeapUnlock(heaps[i]);
        }
        fprintf(stderr, " | %lu heaps:", (unsigned long)nh);
        for (int k = 0; k < 4; k++) {
            int bi = -1;
            for (DWORD i = 0; i < nh; i++)
                if (hs[i].n >= 0 && (bi < 0 || hs[i].busy > hs[bi].busy)) bi = (int)i;
            if (bi < 0) break;
            fprintf(stderr, " %p %zuMB/%ld%s", hs[bi].h, hs[bi].busy >> 20, hs[bi].n,
                    hs[bi].h == GetProcessHeap() ? "(process)" : "");
            hs[bi].n = -1;
        }
    }
    {
        const char *top[6];
        int top_n[6];
        const int live = d3d_tex_name_census(top, top_n, 6);
        fprintf(stderr, " | textures %d live", live);
        for (int i = 0; i < 6 && top[i]; i++) fprintf(stderr, " %s x%d", top[i], top_n[i]);
    }
    /* the committed allocations new since the last probe, by size */
    {
        size_t sz[32];
        int cnt[32], ns = 0, fresh = 0;
        for (int i = 0; i < nr; i++) {
            int seen = 0;
            for (int j = 0; j < s_nrprev && !seen; j++) seen = s_rprev[j] == s_rcur[i];
            if (seen) continue;
            ++fresh;
            int k = 0;
            while (k < ns && sz[k] != s_rcur_sz[i]) k++;
            if (k == ns && ns < 32) { sz[ns] = s_rcur_sz[i]; cnt[ns++] = 0; }
            if (k < ns) ++cnt[k];
        }
        if (s_nrprev) {
            fprintf(stderr, " | new allocs %d:", fresh);
            for (int k = 0; k < ns && k < 8; k++) fprintf(stderr, " %zuKB x%d", sz[k] >> 10, cnt[k]);
        }
        memcpy(s_rprev, s_rcur, sizeof s_rcur[0] * (size_t)nr);
        s_nrprev = nr;
    }
    fputc('\n', stderr);
    for (int i = 0; i < n; i++) if (s_cur[i].n < 0) s_cur[i].n = -s_cur[i].n;
    memcpy(s_prev, s_cur, sizeof s_cur[0] * (size_t)n);
    s_nprev = n;
}

#else

void mem_probe(const char *what) { (void)what; }

#endif
