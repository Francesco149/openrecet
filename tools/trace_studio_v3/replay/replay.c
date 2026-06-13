/* OpenRecet Trace Studio v3 — replayer CLI (thin wrapper over replay_core).
 *
 * The render logic now lives in replay_core.{c,h} (resident: device + resources
 * created once, any frame on demand) — shared with the native viewer. This CLI keeps
 * the jobs the harness relies on:
 *   replay <cap.bin> <ref.raw> [frame-index] [out.raw]    single-frame bit-exact check
 *                                                          (port_capture greps "differing bytes")
 *   replay <cap.bin> --verify-hashes <v3refs.txt>         BATCH bit-exact check: render
 *                                                          every listed frame RESIDENT and
 *                                                          compare its fnv1a-64 to the
 *                                                          proxy's refhash line — the
 *                                                          thousands-of-frames path (no
 *                                                          per-frame process spawn, no GBs
 *                                                          of raw references)
 *   replay <cap.bin> --bench [frame-index] [iters]         resident per-render latency
 *
 * Build: i686-w64-mingw32-gcc (links the REAL d3d8). Run from a dir WITHOUT the
 * proxy d3d8.dll.
 */
#define CINTERFACE
#define COBJMACROS
#include <d3d8.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "replay_core.h"

static uint32_t ru(FILE *f) { uint32_t v = 0; if (fread(&v, 4, 1, f) != 1) return 0xffffffffu; return v; }

/* fnv1a-64 — MUST match the proxy's (d3d8_proxy.c) so a refhash line verifies. */
static uint64_t fnv1a(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t*)p; uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
    return h;
}

/* batch verify vs the proxy's v3refs.txt: render every listed kept frame on the
 * RESIDENT core and compare fnv1a-64 of the re-rendered pixels to the recorded
 * hash. One process for the whole window — the only way a thousands-of-frames
 * container verifies in seconds. */
static int verify_hashes(OrV3Replay *r, const char *refs_path)
{
    FILE *f = fopen(refs_path, "r");
    if (!f) { fprintf(stderr, "no refs file %s\n", refs_path); return 2; }
    uint32_t W = orv3_replay_width(r), H = orv3_replay_height(r);
    int npass = 0, nfail = 0, ntotal = 0;
    char line[256], first_fail[128] = {0};
    while (fgets(line, sizeof line, f)) {
        unsigned kept, present, w, h; unsigned long long want;
        if (sscanf(line, "REF %u present=%u w=%u h=%u fnv64=%llx",
                   &kept, &present, &w, &h, &want) != 5)
            continue;
        ntotal++;
        const char *why = NULL;
        if (w != W || h != H) why = "dims";
        const uint8_t *px = why ? NULL : orv3_replay_render(r, (int)kept);
        if (!why && !px) why = "render-failed";
        if (!why && fnv1a(px, (size_t)W * H * 4u) != want) why = "hash";
        if (why) {
            nfail++;
            printf("FAIL kept=%u present=%u (%s)\n", kept, present, why);
            if (!first_fail[0])
                snprintf(first_fail, sizeof first_fail, "kept=%u present=%u (%s)", kept, present, why);
        } else {
            npass++;
        }
    }
    fclose(f);
    printf("\n==== HASH VERIFY (resident, %d frames) ====\n", ntotal);
    printf("HASHVERIFY pass=%d fail=%d total=%d\n", npass, nfail, ntotal);
    if (first_fail[0]) printf("  first failure: %s\n", first_fail);
    printf("  VERDICT: %s\n", (nfail == 0 && ntotal > 0) ? "ALL FRAMES BIT-EXACT  *** GO ***" : "DIVERGENT");
    return (nfail == 0 && ntotal > 0) ? 0 : 1;
}

static int bench(OrV3Replay *r, int idx, int iters)
{
    LARGE_INTEGER fq, a, b; QueryPerformanceFrequency(&fq);
    double best = 1e9, sum = 0;
    for (int i = 0; i < iters; i++) {
        QueryPerformanceCounter(&a);
        const uint8_t *buf = orv3_replay_render(r, idx);
        QueryPerformanceCounter(&b);
        if (!buf) { fprintf(stderr, "render %d failed\n", idx); return 2; }
        double ms = (double)(b.QuadPart - a.QuadPart) * 1000.0 / fq.QuadPart;
        if (ms < best) best = ms;
        sum += ms;
    }
    printf("==== RESIDENT RENDER BENCH ====\n");
    printf("  frame %d  %dx%d  %d draws / %d calls\n", idx,
           orv3_replay_width(r), orv3_replay_height(r),
           orv3_replay_draws(r, idx), orv3_replay_calls(r, idx));
    printf("  per-render (issue calls + readback): best %.2f ms, mean %.2f ms over %d iters\n",
           best, sum / iters, iters);
    printf("  (device + all resources created ONCE at open — this is the scrub cost)\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: replay <cap.bin> <ref.raw|--bench> [idx] [out.raw|iters]\n"); return 2; }
    const char *cappath = argv[1];
    char err[128] = {0};
    OrV3Replay *r = orv3_replay_open(cappath, err, sizeof err);
    if (!r) { fprintf(stderr, "open failed: %s\n", err); return 2; }

    if (strcmp(argv[2], "--verify-hashes") == 0) {
        if (argc < 4) { fprintf(stderr, "usage: replay <cap.bin> --verify-hashes <v3refs.txt>\n");
                        orv3_replay_close(r); return 2; }
        int rc = verify_hashes(r, argv[3]);
        orv3_replay_close(r);
        return rc;
    }

    if (strcmp(argv[2], "--bench") == 0) {
        int idx = argc > 3 ? atoi(argv[3]) : 0;
        int iters = argc > 4 ? atoi(argv[4]) : 30;
        int rc = bench(r, idx, iters);
        orv3_replay_close(r);
        return rc;
    }

    /* draw isolation: render frame idx issuing only its first max_draws draws.
     *   replay <cap.bin> --upto <idx> <max_draws> [out.raw] */
    if (strcmp(argv[2], "--upto") == 0) {
        int idx = argc > 3 ? atoi(argv[3]) : 0;
        int maxd = argc > 4 ? atoi(argv[4]) : -1;
        const char *out = argc > 5 ? argv[5] : "v3upto.raw";
        uint32_t W = orv3_replay_width(r), H = orv3_replay_height(r);
        const uint8_t *buf = orv3_replay_render_upto(r, idx, maxd);
        if (!buf) { fprintf(stderr, "render_upto frame %d failed\n", idx); orv3_replay_close(r); return 2; }
        FILE *of = fopen(out, "wb");
        if (of) { fwrite(&W, 4, 1, of); fwrite(&H, 4, 1, of); fwrite(buf, 1, (size_t)W * 4 * H, of); fclose(of); }
        fprintf(stderr, "frame %d: rendered first %d of %d draws -> %s\n",
                idx, maxd, orv3_replay_draws(r, idx), out);
        orv3_replay_close(r);
        return 0;
    }

    const char *refpath = argv[2];
    int target = argc > 3 ? atoi(argv[3]) : 0;
    const char *outpath = argc > 4 ? argv[4] : "v3replay.raw";
    uint32_t W = orv3_replay_width(r), H = orv3_replay_height(r), rb = W * 4;

    const uint8_t *mine = orv3_replay_render(r, target);
    if (!mine) { fprintf(stderr, "render frame %d failed (container has %d kept frame(s))\n",
                         target, orv3_replay_count(r)); orv3_replay_close(r); return 2; }
    fprintf(stderr, "replayed frame index %d: %d draws / %d calls issued\n",
            target, orv3_replay_draws(r, target), orv3_replay_calls(r, target));

    /* write out.raw for visual inspection (w,h header + BGRA) */
    FILE *of = fopen(outpath, "wb");
    if (of) { fwrite(&W, 4, 1, of); fwrite(&H, 4, 1, of); fwrite(mine, 1, (size_t)rb * H, of); fclose(of); }

    /* compare to reference */
    FILE *rf = fopen(refpath, "rb");
    if (!rf) { fprintf(stderr, "no ref %s\n", refpath); orv3_replay_close(r); return 2; }
    uint32_t rw = ru(rf), rh = ru(rf);
    if (rw != W || rh != H) { fprintf(stderr, "ref dims %ux%u != %ux%u\n", rw, rh, W, H); orv3_replay_close(r); return 2; }
    unsigned char *ref = malloc((size_t)rb * H);
    if (fread(ref, 1, (size_t)rb * H, rf) != (size_t)rb * H) { fprintf(stderr, "ref short\n"); orv3_replay_close(r); return 2; }
    fclose(rf);

    size_t total = (size_t)rb * H, ndiff = 0; unsigned maxd = 0; size_t difpx = 0;
    for (size_t i = 0; i < total; i++) { int d = abs((int)mine[i] - (int)ref[i]); if (d) { ndiff++; if ((unsigned)d > maxd) maxd = d; } }
    for (size_t px = 0; px < (size_t)W * H; px++) { const unsigned char *a = mine + px * 4, *b = ref + px * 4; if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2] || a[3] != b[3]) difpx++; }
    free(ref);

    printf("\n==== P0c REPLAY BIT-EXACT CHECK ====\n");
    printf("  frame: %ux%u  bytes: %zu\n", W, H, total);
    printf("  differing bytes : %zu (%.4f%%)\n", ndiff, 100.0 * ndiff / total);
    printf("  differing pixels: %zu / %u (%.4f%%)\n", difpx, W * H, 100.0 * difpx / ((double)W * H));
    printf("  max byte delta  : %u\n", maxd);
    printf("  VERDICT: %s\n", ndiff == 0 ? "BIT-EXACT  *** GO ***" :
           (difpx * 100.0 / ((double)W * H) < 0.5 ? "near-exact (investigate residual)" : "DIVERGENT"));
    orv3_replay_close(r);
    return ndiff == 0 ? 0 : 1;
}
