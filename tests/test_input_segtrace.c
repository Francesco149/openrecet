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
