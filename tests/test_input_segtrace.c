/*
 * test_input_segtrace.c — anchor-segmented input forcing (port side).
 *
 * Covers the lowering state machine (1:1 with the validated Frida agent):
 *   - no-wait trace == absolute replay (backward compatible)
 *   - sticky mask holds between entries
 *   - `wait` rebases the segment onto the anchor fire frame
 *   - repeated anchor (double HOUSE_FREEROAM) resolves successive waits on
 *     successive firings (strictly-after-entry guard)
 *   - spam-until-anchor: long pre-wait entries abandoned when the anchor fires
 *   - `{capture:N}` schedules base+N via the callback
 *   - `{calltrace:[S,L]}` / scalar `{calltrace:N}` resolve to absolute windows
 *     [base+S, base+S+L) via the calltrace callback; has_calltrace reports them
 *   - parse rejects unknown keys
 */
#define _GNU_SOURCE
#include "t.h"
#include "input_segtrace.h"

#include <string.h>

struct cap_log { uint32_t f[32]; int n; };
static void cap_cb(uint32_t frame, void *user)
{
    struct cap_log *c = (struct cap_log *)user;
    if (c->n < 32) c->f[c->n++] = frame;
}

int test_segtrace_no_wait_is_absolute(void)
{
    const char buf[] =
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":10,\"buttons\":\"0x0004\"}\n"
        "{\"frame\":20,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 1);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 0,  NULL, NULL), 0);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 9,  NULL, NULL), 0);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 10, NULL, NULL), 0x0004);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 15, NULL, NULL), 0x0004); /* sticky */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 20, NULL, NULL), 0x0000);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_wait_rebases_on_anchor(void)
{
    const char buf[] =
        "{\"frame\":30,\"buttons\":\"0x0010\"}\n"
        "{\"frame\":32,\"buttons\":\"0x0000\"}\n"
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":5,\"buttons\":\"0x0002\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);
    /* boot segment: A at 30, release 32 */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 30, NULL, NULL), 0x0010);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 50, NULL, NULL), 0x0000);
    /* parked on the wait until the anchor fires */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 1000, NULL, NULL), 0x0000);
    /* anchor fires at 1200 → base = 1200; seg frame 5 → abs 1205 */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 1200);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 1200, NULL, NULL), 0x0000);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 1204, NULL, NULL), 0x0000);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 1205, NULL, NULL), 0x0002); /* LEFT */
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_double_anchor_resolves_successively(void)
{
    /* Two waits on the SAME anchor (the new-game double HOUSE_FREEROAM). The
     * second must resolve on the SECOND firing, not the first. */
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"frame\":0,\"buttons\":\"0x0010\"}\n"   /* seg1: A held */
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":3,\"buttons\":\"0x0004\"}\n";  /* seg2: UP at base+3 */
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 3);
    /* first firing @100 → enter seg1 (A held) */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 100);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 100, NULL, NULL), 0x0010);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 150, NULL, NULL), 0x0010); /* still seg1 */
    /* second firing @200 (> seg1 entry @100) → enter seg2 */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 200);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 200, NULL, NULL), 0x0000);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 203, NULL, NULL), 0x0004); /* UP at base+3 */
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_spam_until_anchor_short_circuits(void)
{
    /* Pre-wait entries run long; the wait short-circuits the instant the
     * anchor fires, abandoning the remaining entries. */
    const char buf[] =
        "{\"frame\":0,\"buttons\":\"0x0010\"}\n"
        "{\"frame\":9000,\"buttons\":\"0x0010\"}\n"   /* would run to 9000 */
        "{\"wait\":\"LOADING_END\"}\n"
        "{\"frame\":0,\"buttons\":\"0x0002\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 0, NULL, NULL), 0x0010);
    /* anchor fires at 500, long before the 9000 entry — abandon it */
    input_segtrace_on_anchor(&st, "LOADING_END", 500);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 500, NULL, NULL), 0x0002); /* seg2 frame 0 */
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_capture_scheduled_at_base_plus_n(void)
{
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"capture\":5}\n"
        "{\"capture\":40}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    struct cap_log log = {0};
    /* seg0 has no captures */
    input_segtrace_tick(&st, 0, cap_cb, &log);
    T_ASSERT_EQ_U(log.n, 0);
    /* anchor @1000 → seg1 captures at 1005, 1040 */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 1000);
    input_segtrace_tick(&st, 1000, cap_cb, &log);
    T_ASSERT_EQ_U(log.n, 2);
    T_ASSERT_EQ_U(log.f[0], 1005);
    T_ASSERT_EQ_U(log.f[1], 1040);
    input_segtrace_free(&st);
    return 0;
}

struct ct_log { uint32_t lo[8], hi[8]; int n; };
static void ct_cb(uint32_t lo, uint32_t hi, void *user)
{
    struct ct_log *c = (struct ct_log *)user;
    if (c->n < 8) { c->lo[c->n] = lo; c->hi[c->n] = hi; c->n++; }
}

int test_segtrace_calltrace_resolves_to_windows(void)
{
    /* Scalar {calltrace:N} == [0,N]; [start,len] is anchor-relative. Both
     * resolve to absolute half-open windows [base+start, base+start+len) via
     * the calltrace callback when the segment activates, without disturbing
     * the input entries. */
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"calltrace\":200}\n"
        "{\"calltrace\":[1500,680]}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":7,\"buttons\":\"0x0008\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);
    T_ASSERT_EQ_U(input_segtrace_has_calltrace(&st), 1);

    struct ct_log log = {0};
    input_segtrace_set_calltrace_cb(&st, ct_cb, &log);
    /* boot seg has no calltrace ops */
    input_segtrace_tick(&st, 0, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    /* anchor @10 → seg1 base=10; windows resolve to [10,210) and [1510,2190) */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 10);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 10, NULL, NULL), 0x0000);
    T_ASSERT_EQ_U(log.n, 2);
    T_ASSERT_EQ_U(log.lo[0], 10);   T_ASSERT_EQ_U(log.hi[0], 210);
    T_ASSERT_EQ_U(log.lo[1], 1510); T_ASSERT_EQ_U(log.hi[1], 2190);
    /* input entries unaffected */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 17, NULL, NULL), 0x0008); /* DOWN at base+7 */
    input_segtrace_free(&st);
    return 0;
}

struct cr_log { uint32_t lo[8], hi[8]; int n; };
static void cr_cb(uint32_t lo, uint32_t hi, void *user)
{
    struct cr_log *c = (struct cr_log *)user;
    if (c->n < 8) { c->lo[c->n] = lo; c->hi[c->n] = hi; c->n++; }
}

int test_segtrace_caprange_resolves_to_window(void)
{
    /* Scalar {caprange:N} == [0,N); [start,count] is anchor-relative. Both
     * resolve to absolute half-open windows [base+start, base+start+count) via
     * the caprange callback when the segment activates. */
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"caprange\":3}\n"
        "{\"caprange\":[1565,120]}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);
    T_ASSERT_EQ_U(st.segs[1].n_capranges, 2);

    struct cr_log log = {0};
    input_segtrace_set_caprange_cb(&st, cr_cb, &log);
    /* boot seg has no caprange ops */
    input_segtrace_tick(&st, 0, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    /* anchor @1000 → base=1000; windows resolve to [1000,1003) and [2565,2685) */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 1000);
    input_segtrace_tick(&st, 1000, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 2);
    T_ASSERT_EQ_U(log.lo[0], 1000); T_ASSERT_EQ_U(log.hi[0], 1003);
    T_ASSERT_EQ_U(log.lo[1], 2565); T_ASSERT_EQ_U(log.hi[1], 2685);
    input_segtrace_free(&st);
    return 0;
}

struct rng_log { uint32_t v[8]; int n; };
static void rng_cb(uint32_t value, void *user)
{
    struct rng_log *r = (struct rng_log *)user;
    if (r->n < 8) r->v[r->n++] = value;
}

int test_segtrace_rngseed_fires_once_at_frame(void)
{
    /* {rngseed:[frame,value]} fires the callback exactly once when the absolute
     * frame base+frame is reached — not before, not again on later ticks. */
    const char buf[] =
        "{\"rngseed\":[10,2756183931]}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":12,\"buttons\":\"0x0004\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 1);
    T_ASSERT_EQ_U(st.segs[0].n_setrngs, 1);
    struct rng_log log = {0};
    input_segtrace_set_rngseed_cb(&st, rng_cb, &log);
    /* before the target frame → no fire */
    input_segtrace_tick(&st, 0, NULL, NULL);
    input_segtrace_tick(&st, 9, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    /* at the target frame → fires once with the value */
    input_segtrace_tick(&st, 10, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    T_ASSERT_EQ_U(log.v[0], 2756183931u);
    /* later ticks must NOT re-fire */
    input_segtrace_tick(&st, 11, NULL, NULL);
    input_segtrace_tick(&st, 20, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_rngseed_rebases_on_anchor(void)
{
    /* In a waited segment the {rngseed} frame is base-relative: it fires at the
     * anchor-resolved base+frame, BEFORE that frame's input entry. */
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"rngseed\":[1565,42]}\n"
        "{\"frame\":1565,\"buttons\":\"0x0010\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);
    struct rng_log log = {0};
    input_segtrace_set_rngseed_cb(&st, rng_cb, &log);
    /* boot segment has no setrng */
    input_segtrace_tick(&st, 0, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    /* anchor @1000 → base=1000; setrng fires at 1000+1565 = 2565 */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 1000);
    input_segtrace_tick(&st, 1000, NULL, NULL);
    input_segtrace_tick(&st, 2564, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    /* at 2565 the seed fires; the input entry at base+1565 also applies */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 2565, NULL, NULL), 0x0010);
    T_ASSERT_EQ_U(log.n, 1);
    T_ASSERT_EQ_U(log.v[0], 42);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_rngseed_absent_never_fires(void)
{
    const char buf[] =
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":5,\"buttons\":\"0x0004\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.segs[0].n_setrngs, 0);
    struct rng_log log = {0};
    input_segtrace_set_rngseed_cb(&st, rng_cb, &log);
    for (uint32_t f = 0; f <= 20; f++) input_segtrace_tick(&st, f, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_rngseed_rejects_scalar(void)
{
    /* {rngseed} requires the [frame,value] array form (no 1-arg shorthand). */
    const char buf[] = "{\"rngseed\":5}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 0);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_gsimpin_fires_once_at_frame(void)
{
    /* {gsimpin:[frame,value]} fires the g_sim_frame_count force exactly once at
     * base+frame (array form, like {rngseed}; pins the 目玉-sparkle %8 phase).
     * Base-relative in a waited segment, fires before that frame's sim. */
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"gsimpin\":[2,810]}\n"
        "{\"frame\":2,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);
    T_ASSERT_EQ_U(st.segs[0].n_gsimpins, 0);
    T_ASSERT_EQ_U(st.segs[1].n_gsimpins, 1);
    struct rng_log log = {0};
    input_segtrace_set_gsimpin_cb(&st, rng_cb, &log);
    /* boot segment has no gsimpin; before the anchor → no fire */
    input_segtrace_tick(&st, 0, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    /* anchor @1000 → base=1000; gsimpin fires at 1000+2 = 1002, not before */
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 1000);
    input_segtrace_tick(&st, 1000, NULL, NULL);
    input_segtrace_tick(&st, 1001, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    input_segtrace_tick(&st, 1002, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    T_ASSERT_EQ_U(log.v[0], 810);
    /* later ticks must NOT re-fire */
    input_segtrace_tick(&st, 1003, NULL, NULL);
    input_segtrace_tick(&st, 1010, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    input_segtrace_free(&st);
    return 0;
}

/* ── {bgnpcpin} — the bg-NPC SoA pin (rng-consumer-survey foundation) ──────── */
struct bnp_log { uint32_t v[SEG_BGNPCPIN_DWORDS]; size_t n; int fires; };
static void bnp_cb(const uint32_t *values, size_t n, void *user)
{
    struct bnp_log *b = (struct bnp_log *)user;
    b->fires++;
    b->n = n;
    if (n <= SEG_BGNPCPIN_DWORDS) memcpy(b->v, values, n * sizeof *values);
}

int test_segtrace_bgnpcpin_parses_and_fires(void)
{
    /* {bgnpcpin:[frame,[d0..d149]]} parses the SEG_BGNPCPIN_DWORDS-dword payload
     * and fires the SoA-pin callback exactly once at base+frame, delivering the
     * dwords intact (pre-sim, like the other pins). */
    char buf[4096];
    uint32_t expect[SEG_BGNPCPIN_DWORDS];
    int off = snprintf(buf, sizeof buf, "{\"bgnpcpin\":[1,[");
    for (size_t k = 0; k < SEG_BGNPCPIN_DWORDS; k++) {
        expect[k] = (uint32_t)(k * 7u + 1u);
        off += snprintf(buf + off, sizeof buf - (size_t)off, "%s%u",
                        k ? "," : "", expect[k]);
    }
    off += snprintf(buf + off, sizeof buf - (size_t)off,
                    "]]}\n{\"frame\":0,\"buttons\":\"0x0000\"}\n");

    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, (size_t)off, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 1);
    T_ASSERT_EQ_U(st.segs[0].n_bgnpcpins, 1);
    T_ASSERT_EQ_U(st.segs[0].bgnpcpins[0].frame, 1);

    struct bnp_log log = {0};
    input_segtrace_set_bgnpcpin_cb(&st, bnp_cb, &log);
    input_segtrace_tick(&st, 0, NULL, NULL);          /* before frame 1 → no fire */
    T_ASSERT_EQ_U(log.fires, 0);
    input_segtrace_tick(&st, 1, NULL, NULL);          /* fires once at frame 1 */
    T_ASSERT_EQ_U(log.fires, 1);
    T_ASSERT_EQ_U(log.n, SEG_BGNPCPIN_DWORDS);
    T_ASSERT_MEM_EQ(log.v, expect, sizeof expect);
    input_segtrace_tick(&st, 2, NULL, NULL);          /* must NOT re-fire */
    input_segtrace_tick(&st, 9, NULL, NULL);
    T_ASSERT_EQ_U(log.fires, 1);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_bgnpcpin_rejects_truncated(void)
{
    /* the inner array must carry EXACTLY SEG_BGNPCPIN_DWORDS values — a short
     * array (missing the next comma) is a parse error. */
    const char buf[] = "{\"bgnpcpin\":[0,[1,2,3]]}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 0);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_gsimpin_rejects_scalar(void)
{
    /* {gsimpin} requires the [frame,value] array form (no 1-arg shorthand). */
    const char buf[] = "{\"gsimpin\":7}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 0);
    input_segtrace_free(&st);
    return 0;
}

struct esc_log { int n; };
static void esc_cb(void *user) { ((struct esc_log *)user)->n++; }

int test_segtrace_esc_fires_once_at_frame(void)
{
    /* {esc:N} fires the callback exactly once when the absolute frame base+N is
     * reached — not before, not again on later ticks. */
    const char buf[] =
        "{\"esc\":10}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.segs[0].n_escs, 1);
    struct esc_log log = {0};
    input_segtrace_set_esc_cb(&st, esc_cb, &log);
    input_segtrace_tick(&st, 0, NULL, NULL);
    input_segtrace_tick(&st, 9, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    input_segtrace_tick(&st, 10, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    input_segtrace_tick(&st, 11, NULL, NULL);
    input_segtrace_tick(&st, 50, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_esc_rebases_on_anchor(void)
{
    /* In a waited segment the {esc} frame is base-relative: it fires at the
     * anchor-resolved base+frame. */
    const char buf[] =
        "{\"wait\":\"HOUSE_FREEROAM\"}\n"
        "{\"esc\":30}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    struct esc_log log = {0};
    input_segtrace_set_esc_cb(&st, esc_cb, &log);
    input_segtrace_tick(&st, 0, NULL, NULL);
    input_segtrace_on_anchor(&st, "HOUSE_FREEROAM", 1000);
    input_segtrace_tick(&st, 1000, NULL, NULL);
    input_segtrace_tick(&st, 1029, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 0);
    input_segtrace_tick(&st, 1030, NULL, NULL);   /* base+30 */
    T_ASSERT_EQ_U(log.n, 1);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_no_calltrace_reports_zero(void)
{
    const char buf[] =
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"capture\":5}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(input_segtrace_has_calltrace(&st), 0);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_rejects_unknown_key(void)
{
    const char buf[] = "{\"frame\":0,\"bogus\":1}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 0);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_savefile_op_stores_ref(void)
{
    /* The {savefile} op is trace-global: parsed, stored in st.savefile, and
     * does NOT count as a segment-breaking op. has_savefile rises 0→1. */
    const char buf[] =
        "{\"savefile\":\"../_saves/abc123.sav.gz\"}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"frame\":4,\"buttons\":\"0x0004\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.has_savefile, 1);
    T_ASSERT(strcmp(st.savefile, "../_saves/abc123.sav.gz") == 0);
    /* One boot segment with the two input entries — savefile is not a wait. */
    T_ASSERT_EQ_U(st.n_segs, 1);
    T_ASSERT_EQ_U(st.segs[0].n_entries, 2);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_no_savefile_clears_flag(void)
{
    const char buf[] = "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.has_savefile, 0);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_capstride_parses_trace_global(void)
{
    /* {capstride:N} (D3) is trace-global like {savefile}: parsed into st.capstride,
     * not a segment-breaking op, and composes with a {caprange} (the overview
     * shape). The actual every-Nth-frame thinning lives in main.c's
     * capture_in_range (not host-reachable); this guards the parse + storage. */
    const char buf[] =
        "{\"capstride\":8}\n"
        "{\"wait\":\"LOADING_END\"}\n"
        "{\"caprange\":[120,240]}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.has_capstride, 1);
    T_ASSERT_EQ_U(st.capstride, 8);
    /* capstride is not a wait → boot seg + post-anchor seg (the caprange's). */
    T_ASSERT_EQ_U(st.n_segs, 2);
    T_ASSERT_EQ_U(st.segs[1].n_capranges, 1);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_no_capstride_clears_flag(void)
{
    const char buf[] = "{\"caprange\":[0,48]}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.has_capstride, 0);   /* dense default: treat as stride 1 */
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_tutloadpin_parses_per_segment(void)
{
    /* {tutloadpin:N} is SEGMENT-SCOPED: parsed onto the CURRENT segment
     * (segs[i].tutloadpin/has_tutloadpin), not a trace-global field. The head
     * pin here lands on segment 0; the post-anchor segment carries none. The
     * actual bracket override lives in scene1_intro_dialogue (see
     * test_scene1_tutloadpin_pins_bracket_length); this guards parse+storage. */
    const char buf[] =
        "{\"tutloadpin\":8}\n"
        "{\"wait\":\"LOADING_END\"}\n"
        "{\"phasepin\":0}\n"
        "{\"caprange\":[0,240]}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);   /* not a wait → boot seg + post-anchor seg */
    T_ASSERT_EQ_U(st.segs[0].has_tutloadpin, 1);
    T_ASSERT_EQ_U(st.segs[0].tutloadpin, 8);
    T_ASSERT_EQ_U(st.segs[1].has_tutloadpin, 0);  /* sticky: no re-declaration */
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_no_tutloadpin_clears_flag(void)
{
    const char buf[] = "{\"caprange\":[0,48]}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    /* unset → segment carries no pin → synthetic default bracket length */
    T_ASSERT_EQ_U(st.segs[0].has_tutloadpin, 0);
    input_segtrace_free(&st);
    return 0;
}

struct tlp_log { int n; uint32_t v[8]; };
static void tlp_cb_rec(uint32_t value, void *user)
{
    struct tlp_log *r = (struct tlp_log *)user;
    if (r->n < 8) r->v[r->n++] = value;
}

int test_segtrace_tutloadpin_applies_per_segment(void)
{
    /* Segment-scoped application: the head pin fires the callback at segment 0's
     * entry; a segment WITHOUT one leaves the pin unchanged (no re-fire); a
     * mid-trace pin fires at its OWN segment's entry (when the preceding anchor
     * resolves) — the mirror of the port's rearm_tutloadpins at each advance. */
    const char buf[] =
        "{\"tutloadpin\":36}\n"                    /* seg0 pin */
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"wait\":\"LOADING_END\"}\n"              /* -> seg1 (no pin) */
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"wait\":\"LOADING_END\"}\n"              /* -> seg2 */
        "{\"tutloadpin\":110}\n"                   /* seg2 pin */
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 3);
    T_ASSERT_EQ_U(st.segs[0].has_tutloadpin, 1);
    T_ASSERT_EQ_U(st.segs[0].tutloadpin, 36);
    T_ASSERT_EQ_U(st.segs[1].has_tutloadpin, 0);
    T_ASSERT_EQ_U(st.segs[2].has_tutloadpin, 1);
    T_ASSERT_EQ_U(st.segs[2].tutloadpin, 110);
    struct tlp_log log = {0};
    input_segtrace_set_tutloadpin_cb(&st, tlp_cb_rec, &log);
    /* seg0 entry (first tick) applies the head pin 36 */
    input_segtrace_tick(&st, 0, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    T_ASSERT_EQ_U(log.v[0], 36);
    /* advance into seg1 (no pin) → callback must NOT re-fire */
    input_segtrace_on_anchor(&st, "LOADING_END", 100);
    input_segtrace_tick(&st, 100, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 1);
    /* advance into seg2 → its pin 110 applies at entry */
    input_segtrace_on_anchor(&st, "LOADING_END", 200);
    input_segtrace_tick(&st, 200, NULL, NULL);
    T_ASSERT_EQ_U(log.n, 2);
    T_ASSERT_EQ_U(log.v[1], 110);
    input_segtrace_free(&st);
    return 0;
}

static uint32_t s_memsnap_fired_frame;
static int      s_memsnap_fired_count;
static void memsnap_test_cb(uint32_t frame, void *user)
{
    (void)user;
    s_memsnap_fired_frame = frame;
    s_memsnap_fired_count++;
}

int test_segtrace_memsnap_parses_and_fires_resolved(void)
{
    /* {memsnap:N} is segment-scoped + pre-sim like {phasepin}; the callback
     * receives the RESOLVED frame base+N (stable dump filenames across runs).
     * Fires exactly once even when ticked past it repeatedly. */
    const char buf[] =
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"wait\":\"LOADING_END\"}\n"
        "{\"memsnap\":80}\n"
        "{\"caprange\":[120,48]}\n"
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n";
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 2);
    T_ASSERT_EQ_U(st.segs[1].n_memsnaps, 1);
    T_ASSERT_EQ_U(st.segs[1].memsnaps[0].frame, 80);

    s_memsnap_fired_frame = 0; s_memsnap_fired_count = 0;
    input_segtrace_set_memsnap_cb(&st, memsnap_test_cb, NULL);
    input_segtrace_tick(&st, 0, NULL, NULL);          /* boot segment */
    T_ASSERT_EQ_U(s_memsnap_fired_count, 0);
    input_segtrace_on_anchor(&st, "LOADING_END", 500);  /* base = 500 */
    input_segtrace_tick(&st, 500, NULL, NULL);
    T_ASSERT_EQ_U(s_memsnap_fired_count, 0);          /* 500+80 not reached */
    input_segtrace_tick(&st, 579, NULL, NULL);
    T_ASSERT_EQ_U(s_memsnap_fired_count, 0);
    input_segtrace_tick(&st, 580, NULL, NULL);        /* base+80 */
    T_ASSERT_EQ_U(s_memsnap_fired_count, 1);
    T_ASSERT_EQ_U(s_memsnap_fired_frame, 580);        /* RESOLVED frame */
    input_segtrace_tick(&st, 581, NULL, NULL);        /* no re-fire */
    T_ASSERT_EQ_U(s_memsnap_fired_count, 1);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_wait_timeout_skips_without_rebasing(void)
{
    /* A {wait,timeout} whose anchor never fires is SKIPPED after `timeout`
     * frames WITHOUT adopting a new base — the next segment stays relative to
     * the last RESOLVED anchor.  This is the cross-target load-burst bridge:
     * a retail recording carries load-cycle waits the PORT collapses into one
     * load; the port skips the ones it never fires and still lands its inputs
     * on the post-load free-roam.  seg0 resolves on LOADING_END@500; seg1 +
     * seg2 wait on anchors that never fire (timeout 60) → skipped, base kept at
     * 500; seg3's LEFT lands at base+10 = 510. */
    const char buf[] =
        "{\"wait\":\"LOADING_END\"}\n"                       /* seg0 → resolves @500 */
        "{\"frame\":0,\"buttons\":\"0x0000\"}\n"
        "{\"wait\":\"LOADING_START\",\"timeout\":60}\n"      /* seg1: never fires → skip */
        "{\"frame\":0,\"buttons\":\"0x0001\"}\n"             /* (skipped — load-cycle input) */
        "{\"wait\":\"LOADING_END\",\"timeout\":60}\n"        /* seg2: same anchor, not > base → skip */
        "{\"frame\":10,\"buttons\":\"0x0002\"}\n";           /* seg3: LEFT at base+10 */
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 4);
    T_ASSERT_EQ_U(st.segs[1].wait_timeout, 60);   /* parsed onto the right segment */
    T_ASSERT_EQ_U(st.segs[2].wait_timeout, 60);
    T_ASSERT_EQ_U(st.segs[0].wait_timeout, 0);    /* no-timeout default preserved */
    /* LOADING_END @500 resolves seg0 → base = 500; seg1 frame 0 = 0x0000 */
    input_segtrace_on_anchor(&st, "LOADING_END", 500);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 500, NULL, NULL), 0x0000);
    /* parked on seg1's LOADING_START wait; not yet timed out (559-500 = 59 < 60) */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 559, NULL, NULL), 0x0000);
    /* @560 (= 500+60) seg1 times out → skip (base kept 500); same frame seg2's
     * LOADING_END can't resolve (no firing > base_arm 500) → also times out →
     * seg3 active, base STILL 500, LEFT at base+10 = 510 (already passed). */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 560, NULL, NULL), 0x0002);
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_wait_timeout_anchor_still_resolves_first(void)
{
    /* A {wait,timeout} whose anchor DOES fire before the timeout resolves
     * normally (adopts the new base) — retail, which reproduces every load,
     * never hits the timeout. */
    const char buf[] =
        "{\"wait\":\"LOADING_START\",\"timeout\":60}\n"
        "{\"frame\":3,\"buttons\":\"0x0004\"}\n";           /* UP at base+3 */
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    /* anchor fires @40 (< 60-frame timeout from entry @0) → base = 40 */
    input_segtrace_on_anchor(&st, "LOADING_START", 40);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 40, NULL, NULL), 0x0000);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 43, NULL, NULL), 0x0004); /* UP at base+3 = 43 */
    input_segtrace_free(&st);
    return 0;
}

int test_segtrace_wait_timeout_after_last_entry(void)
{
    /* REGRESSION (customer-tutorial walk eaten): a segment whose own recorded
     * inputs extend PAST its terminating wait's timeout must still apply them —
     * the timeout countdown starts at the LAST entry, not at segment entry.
     * Here seg1 walks at base+66 then Zs at base+156 under a timeout-60; the
     * old code timed out at base+60 (before the walk) and skipped both inputs.
     * The fix delays the timeout to last-entry(156)+60 = base+216, so the walk
     * and Z land before it can fire. */
    const char buf[] =
        "{\"wait\":\"LOADING_END\"}\n"                     /* seg0 → resolves @100 */
        "{\"frame\":66,\"buttons\":\"0x0002\"}\n"          /* seg1: LEFT (walk) at base+66 */
        "{\"frame\":75,\"buttons\":\"0x0000\"}\n"          /* seg1: release at base+75 */
        "{\"frame\":156,\"buttons\":\"0x0010\"}\n"         /* seg1: Z (enter) at base+156 */
        "{\"wait\":\"LOADING_START\",\"timeout\":60}\n";   /* seg1 wait (optional) */
    struct input_segtrace st = {0};
    T_ASSERT(input_segtrace_parse_buf(buf, sizeof(buf) - 1, &st) == 1);
    T_ASSERT_EQ_U(st.n_segs, 3);   /* seg0(wait) seg1(entries+wait) seg2(empty tail) */
    T_ASSERT_EQ_U(st.segs[1].wait_timeout, 60);
    /* LOADING_END @100 → base = 100 */
    input_segtrace_on_anchor(&st, "LOADING_END", 100);
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 100, NULL, NULL), 0x0000);
    /* OLD bug: at base+60 = 160 the wait timed out and skipped seg1's inputs.
     * Fixed: the walk LANDS at base+66 = 166 (well past 160). */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 166, NULL, NULL), 0x0002); /* LEFT applied */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 175, NULL, NULL), 0x0000); /* release */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 256, NULL, NULL), 0x0010); /* Z applied (base+156) */
    /* Timeout now fires at last-entry(base+156) + 60 = base+216 = 316; the Z
     * mask stays held through it (spam-until-anchor), and with no LOADING_START
     * and no further segment the held Z persists. */
    T_ASSERT_EQ_U(input_segtrace_tick(&st, 320, NULL, NULL), 0x0010);
    input_segtrace_free(&st);
    return 0;
}
