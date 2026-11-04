/* rng_golden.c — see rng_golden.h. */
#include "rng_golden.h"

#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t count, end; } tick_t;
typedef struct { uint32_t before, after, rva; } draw_t;

static tick_t  *g_gold, *g_port;     /* [0..g_frames] */
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
    free(g_gold); free(g_port); free(g_cur);
    g_gold = g_port = NULL; g_cur = NULL;
    g_frames = 0; g_loaded = 0; g_checked = 0; g_split = 0; g_split_found = 0;
    g_cur_n = g_cur_cap = g_cur_frame = 0; g_cur_frozen = 0;
}

int rng_golden_load_fp(FILE *fp)
{
    rng_golden_reset();
    char line[256];
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
    fprintf(fp, "split %u retail %u %08x port %u %08x\n", f, g_gold[f].count, g_gold[f].end,
            g_port[f].count, g_port[f].end);
    for (uint32_t i = 0; i < g_cur_n; i++)
        fprintf(fp, "rand %u %08x -> %08x @%06x\n", f, g_cur[i].before, g_cur[i].after, g_cur[i].rva);
    return 0;
}
