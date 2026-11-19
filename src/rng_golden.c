/* rng_golden.c — see rng_golden.h. */
#include "rng_golden.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t count, end; } tick_t;
typedef struct { uint32_t before, after, rva; } draw_t;

static tick_t  *g_gold, *g_port;     /* [0..g_frames] */

/* the per-tick state check: retail's block hashes (rhost --tick-hash) */
#define TH_MAX 16
static uint32_t  g_th_n, g_th_va[TH_MAX], g_th_len[TH_MAX];
static uint32_t *g_th;               /* [f * g_th_n + i], forward-filled; valid for ticks 1..g_th_to */
static uint32_t  g_th_to;
static uint32_t (*g_th_fn)(uint32_t va, uint32_t len);
#define VAS_MAX 64
static uint32_t  g_vas[VAS_MAX], g_n_vas;   /* the scalars block's dwords (a `vas` line) */
static int32_t (*g_val_fn)(uint32_t va);
static int32_t   g_split_vals[VAS_MAX];   /* the scalars' port values at a scalars-block split */
static int       g_split_block = -1; /* the split is a state split of this block */
static uint32_t  g_split_port_hash;
static uint32_t g_frames;            /* retail ticks 1..g_frames */
static int      g_loaded;
static uint32_t g_checked;           /* ticks 0..g_checked-1 compared */
static uint32_t g_split;             /* the split tick, valid once g_split_found */
static int      g_split_found;

/* the draws of the tick being accumulated (and, after a split, of the split tick) */
static draw_t  *g_cur;
static uint32_t g_cur_n, g_cur_cap, g_cur_frame;
static int      g_cur_frozen;        /* the split tick's draws are kept */

void rng_golden_reset(void)
{
    free(g_gold); free(g_port); free(g_cur); free(g_th);
    g_gold = g_port = NULL; g_cur = NULL; g_th = NULL;
    g_th_n = 0; g_th_to = 0; g_split_block = -1; g_split_port_hash = 0; g_n_vas = 0;
    g_frames = 0; g_loaded = 0; g_checked = 0; g_split = 0; g_split_found = 0;
    g_cur_n = g_cur_cap = g_cur_frame = 0; g_cur_frozen = 0;
}

int rng_golden_load_fp(FILE *fp)
{
    rng_golden_reset();
    char line[512];
    uint32_t frames = 0;
    int have_frames = 0;
    while (fgets(line, sizeof line, fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        unsigned f, c, e;
        if (!have_frames) {
            if (sscanf(line, "frames %u", &f) != 1) return -1;
            frames = f;
            have_frames = 1;
            g_gold = calloc((size_t)frames + 1, sizeof *g_gold);
            g_port = calloc((size_t)frames + 1, sizeof *g_port);
            if (!g_gold || !g_port) { rng_golden_reset(); return -1; }
            continue;
        }
        if (!strncmp(line, "vas ", 4)) {
            g_n_vas = 0;
            for (char *p = line + 4; *p && *p != '\n' && *p != '\r'; ) {
                unsigned va; int n = 0;
                if (g_n_vas == VAS_MAX || sscanf(p, "%x%n", &va, &n) != 1) { rng_golden_reset(); return -1; }
                g_vas[g_n_vas++] = va;
                p += n;
                if (*p == ',') p++;
            }
            continue;
        }
        if (!strncmp(line, "blocks ", 7)) {
            if (g_th) { rng_golden_reset(); return -1; }
            g_th_n = 0;
            for (char *p = line + 7; *p && *p != '\n' && *p != '\r'; ) {
                unsigned va, len; int n = 0;
                if (g_th_n == TH_MAX || sscanf(p, "%x:%x%n", &va, &len, &n) != 2) { rng_golden_reset(); return -1; }
                g_th_va[g_th_n] = va; g_th_len[g_th_n] = len; g_th_n++;
                p += n;
                if (*p == ',') p++;
            }
            if (!g_th_n || !(g_th = calloc(((size_t)frames + 1) * g_th_n, sizeof *g_th))) { rng_golden_reset(); return -1; }
            continue;
        }
        if (!strncmp(line, "th ", 3)) {
            char *p = line + 3, *q;
            f = (unsigned)strtoul(p, &q, 10);
            if (!g_th || q == p || f == 0 || f > frames || f <= g_th_to) { rng_golden_reset(); return -1; }
            for (uint32_t t = g_th_to + 1; t < f; t++)        /* unchanged ticks */
                memcpy(&g_th[t * g_th_n], &g_th[(t - 1) * g_th_n], g_th_n * sizeof *g_th);
            memcpy(&g_th[f * g_th_n], &g_th[(f - 1) * g_th_n], g_th_n * sizeof *g_th);
            for (p = q; *p == ' '; ) {
                unsigned i, h; int n = 0;
                if (sscanf(p, " %u:%x%n", &i, &h, &n) != 2 || i >= g_th_n) { rng_golden_reset(); return -1; }
                g_th[f * g_th_n + i] = h;
                p += n;
            }
            g_th_to = f;
            continue;
        }
        if (sscanf(line, "%u %u %x", &f, &c, &e) != 3 || f > frames) { rng_golden_reset(); return -1; }
        g_gold[f].count = c;
        g_gold[f].end = e;
    }
    if (!have_frames) return -1;
    g_frames = frames;
    g_loaded = 1;
    /* frame 0 (boot) is not comparable: retail draws from seed 1 and is reseeded 19937 at the
     * frame-0 boundary, the port seeds 19937 first; both enter tick 1 at 19937 */
    g_checked = 1;
    return 0;
}

int rng_golden_load(const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    int r = rng_golden_load_fp(fp);
    fclose(fp);
    return r;
}

int rng_golden_active(void) { return g_loaded; }
void rng_golden_set_block_hash(uint32_t (*fn)(uint32_t va, uint32_t len)) { g_th_fn = fn; }
void rng_golden_set_scalar_value(int32_t (*fn)(uint32_t va)) { g_val_fn = fn; }

/* a block's port hash: VA 0 = the scalars block */
static uint32_t block_hash(uint32_t i)
{
    if (g_th_va[i] != 0) return g_th_fn ? g_th_fn(g_th_va[i], g_th_len[i]) : 0u;
    uint32_t h = 2166136261u;
    for (uint32_t k = 0; k < g_n_vas; k++) {
        h ^= g_val_fn ? (uint32_t)g_val_fn(g_vas[k]) : 0u;
        h *= 16777619u;
    }
    return h;
}

uint32_t rng_golden_frames(void) { return g_frames; }
int rng_golden_split(uint32_t *frame)
{
    if (g_split_found && frame) *frame = g_split;
    return g_split_found;
}

void rng_golden_note(int is_seed, uint32_t frame, uint32_t before, uint32_t after, uint32_t caller_rva)
{
    if (!g_loaded || is_seed) return;
    if (frame <= g_frames) {
        g_port[frame].count++;
        g_port[frame].end = after;
    }
    if (g_cur_frozen) return;
    if (frame != g_cur_frame) { g_cur_frame = frame; g_cur_n = 0; }
    if (g_cur_n == g_cur_cap) {
        uint32_t cap = g_cur_cap ? g_cur_cap * 2 : 4096;
        if (cap > (1u << 20)) return;                 /* a runaway tick: keep the first 1M */
        draw_t *p = realloc(g_cur, cap * sizeof *p);
        if (!p) return;
        g_cur = p; g_cur_cap = cap;
    }
    g_cur[g_cur_n++] = (draw_t){ before, after, caller_rva };
}

static int tick_differs(uint32_t f)
{
    if (g_gold[f].count != g_port[f].count) return 1;
    return g_gold[f].count && g_gold[f].end != g_port[f].end;
}

int rng_golden_end_tick(uint32_t frame)
{
    if (!g_loaded || g_split_found) return g_split_found;
    uint32_t last = frame < g_frames ? frame : g_frames;
    for (uint32_t f = g_checked; f <= last; f++) {
        if (tick_differs(f)) {
            g_split = f; g_split_found = 1;
            /* the buffer holds tick `frame`'s draws: the split's when f == frame (the
             * usual case, one tick checked per call) */
            g_cur_frozen = 1;
            if (g_cur_frame != f) g_cur_n = 0;
            return 1;
        }
    }
    /* the state after tick `frame` (only the current tick's can be read) */
    if (g_th && (g_th_fn || g_val_fn) && frame >= 1 && frame <= g_th_to) {
        for (uint32_t i = 0; i < g_th_n; i++) {
            const uint32_t h = block_hash(i);
            if (h != g_th[frame * g_th_n + i]) {
                g_split = frame; g_split_found = 1; g_split_block = (int)i; g_split_port_hash = h;
                if (g_th_va[i] == 0)
                    for (uint32_t k = 0; k < g_n_vas; k++) g_split_vals[k] = g_val_fn ? g_val_fn(g_vas[k]) : 0;
                g_cur_frozen = 1;
                if (g_cur_frame != frame) g_cur_n = 0;
                return 1;
            }
        }
    }
    if (last + 1 > g_checked) g_checked = last + 1;
    return 0;
}

int rng_golden_write_report(FILE *fp)
{
    if (!g_loaded) return -1;
    if (!g_split_found) {
        fprintf(fp, "identical %u\n", g_checked ? g_checked - 1 : 0);
        return 0;
    }
    uint32_t f = g_split;
    if (g_split_block >= 0) {
        const uint32_t i = (uint32_t)g_split_block;
        fprintf(fp, "state %u block %x:%x retail %08x port %08x\n", f, g_th_va[i], g_th_len[i],
                g_th[f * g_th_n + i], g_split_port_hash);
        if (g_th_va[i] == 0) {   /* the scalars block: the port's values (retail's: portstate) */
            fprintf(fp, "vals");
            for (uint32_t k = 0; k < g_n_vas; k++) fprintf(fp, " %x=%d", g_vas[k], g_split_vals[k]);
            fprintf(fp, "\n");
        }
        return 0;
    }
    fprintf(fp, "split %u retail %u %08x port %u %08x\n", f, g_gold[f].count, g_gold[f].end,
            g_port[f].count, g_port[f].end);
    for (uint32_t i = 0; i < g_cur_n; i++)
        fprintf(fp, "rand %u %08x -> %08x @%06x\n", f, g_cur[i].before, g_cur[i].after, g_cur[i].rva);
    return 0;
}
