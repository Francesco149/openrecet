/*
 * input_segtrace.c — anchor-segmented input forcing (port side).
 * See input_segtrace.h for the format + role. Pure C, no Win32.
 *
 * The runtime state machine is a 1:1 port of segtraceTick /
 * segtraceOnSegmentEnter in tools/frida/openrecet-agent.js, validated on the
 * retail side first. The parser is the input_trace.c style widened to the
 * segment grammar (wait / capture / calltrace ops alongside frame+buttons).
 */

#include "input_segtrace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── parse helpers (mirror input_trace.c) ──────────────────────────────── */

static void skip_ws_and_comments(const char **pp, const char *end)
{
    const char *p = *pp;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p < end && *p == '#') { while (p < end && *p != '\n') p++; continue; }
        break;
    }
    *pp = p;
}

/* Decimal or 0x-hex value, optionally quoted. Advances past it. */
static int parse_number(const char **pp, const char *end, uint32_t *out)
{
    const char *p = *pp;
    int quoted = 0;
    if (p < end && *p == '"') { quoted = 1; p++; }
    char buf[32]; size_t n = 0;
    while (p < end && n + 1 < sizeof buf) {
        char c = *p;
        if (c == ',' || c == '}' || c == ']' || c == '"' ||
            c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        buf[n++] = c; p++;
    }
    if (n == 0) return 0;
    buf[n] = '\0';
    char *endp = NULL;
    unsigned long v = strtoul(buf, &endp, 0);
    if (!endp || *endp != '\0') return 0;
    if (quoted) { if (p >= end || *p != '"') return 0; p++; }
    *out = (uint32_t)v;
    *pp = p;
    return 1;
}

/* "....." string value into out (NUL-terminated, truncated to cap). */
static int parse_string(const char **pp, const char *end, char *out, size_t cap)
{
    const char *p = *pp;
    if (p >= end || *p != '"') return 0;
    p++;
    size_t n = 0;
    while (p < end && *p != '"') { if (n + 1 < cap) out[n++] = *p; p++; }
    if (p >= end) return 0;
    p++;  /* closing quote */
    out[n] = '\0';
    *pp = p;
    return 1;
}

/* ─── segment building ──────────────────────────────────────────────────── */

static struct seg_segment *push_segment(struct input_segtrace *st)
{
    if (st->n_segs >= st->cap_segs) {
        size_t ncap = st->cap_segs ? st->cap_segs * 2 : 4;
        struct seg_segment *ns = realloc(st->segs, ncap * sizeof *ns);
        if (!ns) return NULL;
        st->segs = ns; st->cap_segs = ncap;
    }
    struct seg_segment *s = &st->segs[st->n_segs++];
    memset(s, 0, sizeof *s);
    return s;
}

static int push_entry(struct seg_segment *s, uint32_t frame, uint16_t mask)
{
    if (s->n_entries >= s->cap_entries) {
        size_t ncap = s->cap_entries ? s->cap_entries * 2 : 16;
        struct seg_entry *ne = realloc(s->entries, ncap * sizeof *ne);
        if (!ne) return 0;
        s->entries = ne; s->cap_entries = ncap;
    }
    s->entries[s->n_entries].frame = frame;
    s->entries[s->n_entries].mask  = mask;
    s->n_entries++;
    return 1;
}

static int push_capture(struct seg_segment *s, uint32_t n)
{
    if (s->n_captures >= s->cap_captures) {
        size_t ncap = s->cap_captures ? s->cap_captures * 2 : 4;
        uint32_t *nc = realloc(s->captures, ncap * sizeof *nc);
        if (!nc) return 0;
        s->captures = nc; s->cap_captures = ncap;
    }
    s->captures[s->n_captures++] = n;
    return 1;
}

static int push_calltrace(struct seg_segment *s, uint32_t start, uint32_t len)
{
    if (s->n_calltraces >= s->cap_calltraces) {
        size_t ncap = s->cap_calltraces ? s->cap_calltraces * 2 : 4;
        struct seg_calltrace *nc = realloc(s->calltraces, ncap * sizeof *nc);
        if (!nc) return 0;
        s->calltraces = nc; s->cap_calltraces = ncap;
    }
    s->calltraces[s->n_calltraces].start = start;
    s->calltraces[s->n_calltraces].len   = len;
    s->n_calltraces++;
    return 1;
}

static int push_caprange(struct seg_segment *s, uint32_t start, uint32_t count)
{
    if (s->n_capranges >= s->cap_capranges) {
        size_t ncap = s->cap_capranges ? s->cap_capranges * 2 : 4;
        struct seg_caprange *nc = realloc(s->capranges, ncap * sizeof *nc);
        if (!nc) return 0;
        s->capranges = nc; s->cap_capranges = ncap;
    }
    s->capranges[s->n_capranges].start = start;
    s->capranges[s->n_capranges].count = count;
    s->n_capranges++;
    return 1;
}

static int push_setrng(struct seg_segment *s, uint32_t frame, uint32_t value)
{
    if (s->n_setrngs >= s->cap_setrngs) {
        size_t ncap = s->cap_setrngs ? s->cap_setrngs * 2 : 4;
        struct seg_setrng *ns = realloc(s->setrngs, ncap * sizeof *ns);
        if (!ns) return 0;
        s->setrngs = ns; s->cap_setrngs = ncap;
    }
    s->setrngs[s->n_setrngs].frame = frame;
    s->setrngs[s->n_setrngs].value = value;
    s->setrngs[s->n_setrngs].fired = 0;
    s->n_setrngs++;
    return 1;
}

void input_segtrace_free(struct input_segtrace *st)
{
    if (!st) return;
    for (size_t i = 0; i < st->n_segs; i++) {
        free(st->segs[i].entries);
        free(st->segs[i].captures);
        free(st->segs[i].calltraces);
        free(st->segs[i].capranges);
        free(st->segs[i].setrngs);
    }
    free(st->segs);
    memset(st, 0, sizeof *st);
}

/* ─── parser ────────────────────────────────────────────────────────────── */

int input_segtrace_parse_buf(const char *buf, size_t len, struct input_segtrace *out)
{
    if (!buf || !out) return 0;
    input_segtrace_free(out);
    struct seg_segment *cur = push_segment(out);   /* boot segment */
    if (!cur) return 0;

    const char *p = buf, *end = buf + len;
    for (;;) {
        skip_ws_and_comments(&p, end);
        if (p >= end) break;
        if (*p != '{') return 0;
        p++;

        /* Collect the object's fields. One line is exactly one of:
         * a wait op, a capture op, a calltrace op (ignored), or a
         * frame+buttons entry. */
        int      got_frame = 0, got_mask = 0, got_wait = 0, got_capture = 0;
        int      got_calltrace = 0, got_setrng = 0, got_caprange = 0;
        uint32_t frame = 0, mask = 0, capture = 0;
        uint32_t ct_start = 0, ct_len = 0;
        uint32_t cr_start = 0, cr_count = 0;
        uint32_t rng_frame = 0, rng_value = 0;
        char     waitname[24] = {0};

        for (;;) {
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p >= end || *p != '"') return 0;
            p++;
            const char *ks = p;
            while (p < end && *p != '"') p++;
            if (p >= end) return 0;
            size_t klen = (size_t)(p - ks);
            p++;  /* closing quote */
            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p >= end || *p != ':') return 0;
            p++;
            while (p < end && (*p == ' ' || *p == '\t')) p++;

            if (klen == 5 && memcmp(ks, "frame", 5) == 0) {
                if (!parse_number(&p, end, &frame)) return 0;
                got_frame = 1;
            } else if (klen == 7 && memcmp(ks, "buttons", 7) == 0) {
                if (!parse_number(&p, end, &mask)) return 0;
                got_mask = 1;
            } else if (klen == 4 && memcmp(ks, "wait", 4) == 0) {
                if (!parse_string(&p, end, waitname, sizeof waitname)) return 0;
                got_wait = 1;
            } else if (klen == 7 && memcmp(ks, "capture", 7) == 0) {
                if (!parse_number(&p, end, &capture)) return 0;
                got_capture = 1;
            } else if (klen == 9 && memcmp(ks, "calltrace", 9) == 0) {
                /* {calltrace:[start,len]} (anchor-relative window) or scalar
                 * {calltrace:N} == [0, N].  Drives the call tracer on BOTH
                 * targets — the same op the Frida agent consumes. */
                if (p < end && *p == '[') {
                    p++;  /* '[' */
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &ct_start)) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (p >= end || *p != ',') return 0;
                    p++;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &ct_len)) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (p >= end || *p != ']') return 0;
                    p++;
                } else {
                    if (!parse_number(&p, end, &ct_len)) return 0;
                    ct_start = 0;
                }
                got_calltrace = 1;
            } else if (klen == 8 && memcmp(ks, "caprange", 8) == 0) {
                /* {caprange:[start,count]} (anchor-relative contiguous window)
                 * or scalar {caprange:N} == [0, N].  Same parse shape as
                 * {calltrace}, but feeds the host's lo/hi capture-window test. */
                if (p < end && *p == '[') {
                    p++;  /* '[' */
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &cr_start)) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (p >= end || *p != ',') return 0;
                    p++;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &cr_count)) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (p >= end || *p != ']') return 0;
                    p++;
                } else {
                    if (!parse_number(&p, end, &cr_count)) return 0;
                    cr_start = 0;
                }
                got_caprange = 1;
            } else if (klen == 7 && memcmp(ks, "rngseed", 7) == 0) {
                /* {rngseed:[frame,value]} — force the LCG state to `value` at
                 * base+frame.  Array form only (a seed has no 1-arg shorthand);
                 * `value` is a bare uint32 (decimal or 0x-hex). */
                if (p >= end || *p != '[') return 0;
                p++;  /* '[' */
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &rng_frame)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ',') return 0;
                p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &rng_value)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ']') return 0;
                p++;
                got_setrng = 1;
            } else {
                return 0;  /* unknown key */
            }

            while (p < end && (*p == ' ' || *p == '\t')) p++;
            if (p < end && *p == ',') { p++; continue; }
            if (p < end && *p == '}') { p++; break; }
            return 0;
        }

        if (got_wait) {
            /* Close the current segment with this anchor; start a new one. */
            if (cur->has_wait) return 0;            /* two waits, no entries? */
            memcpy(cur->wait, waitname, sizeof cur->wait);
            cur->has_wait = 1;
            cur = push_segment(out);
            if (!cur) return 0;
        } else if (got_capture) {
            if (!push_capture(cur, capture)) return 0;
        } else if (got_calltrace) {
            if (!push_calltrace(cur, ct_start, ct_len)) return 0;
        } else if (got_caprange) {
            if (!push_caprange(cur, cr_start, cr_count)) return 0;
        } else if (got_setrng) {
            if (!push_setrng(cur, rng_frame, rng_value)) return 0;
        } else {
            if (!got_frame || !got_mask || mask > 0xffffu) return 0;
            if (!push_entry(cur, frame, (uint16_t)mask)) return 0;
        }
    }
    return 1;
}

int input_segtrace_load(const char *path, struct input_segtrace *out)
{
    if (!path || !out) return 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;
    char *buf = NULL; size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : (1u << 16);
            char *nb = realloc(buf, ncap);
            if (!nb) { free(buf); fclose(fp); return 0; }
            buf = nb; cap = ncap;
        }
        size_t got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) break;
    }
    int err = ferror(fp);
    fclose(fp);
    if (err) { free(buf); return 0; }
    int rc = input_segtrace_parse_buf(buf, len, out);
    free(buf);
    return rc;
}

/* ─── runtime ───────────────────────────────────────────────────────────── */

void input_segtrace_on_anchor(struct input_segtrace *st,
                              const char *name, uint32_t frame)
{
    if (!st || !name) return;
    for (int i = 0; i < SEGTRACE_MAX_FIRED; i++) {
        if (st->fired[i].set && strcmp(st->fired[i].name, name) == 0) {
            st->fired[i].frame = frame;   /* latest wins */
            return;
        }
    }
    for (int i = 0; i < SEGTRACE_MAX_FIRED; i++) {
        if (!st->fired[i].set) {
            st->fired[i].set = 1;
            st->fired[i].frame = frame;
            strncpy(st->fired[i].name, name, sizeof st->fired[i].name - 1);
            st->fired[i].name[sizeof st->fired[i].name - 1] = '\0';
            return;
        }
    }
    /* table full — silently drop (24 distinct anchor names is plenty) */
}

static int anchor_fired_frame(const struct input_segtrace *st,
                              const char *name, uint32_t *out)
{
    for (int i = 0; i < SEGTRACE_MAX_FIRED; i++) {
        if (st->fired[i].set && strcmp(st->fired[i].name, name) == 0) {
            *out = st->fired[i].frame;
            return 1;
        }
    }
    return 0;
}

static void schedule_captures(struct input_segtrace *st, size_t seg_idx,
                              segtrace_capture_fn cb, void *user)
{
    if (!cb || seg_idx >= st->n_segs) return;
    const struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_captures; i++)
        cb(st->base + s->captures[i], user);
}

/* Resolve this segment's call-trace windows to absolute [base+start,
 * base+start+len) and fire them through the registered callback. */
static void schedule_calltraces(struct input_segtrace *st, size_t seg_idx)
{
    if (!st->ct_cb || seg_idx >= st->n_segs) return;
    const struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_calltraces; i++) {
        uint32_t lo = st->base + s->calltraces[i].start;
        st->ct_cb(lo, lo + s->calltraces[i].len, st->ct_user);
    }
}

/* Resolve this segment's capture ranges to absolute half-open [base+start,
 * base+start+count) windows and fire them through the registered callback. */
static void schedule_capranges(struct input_segtrace *st, size_t seg_idx)
{
    if (!st->cr_cb || seg_idx >= st->n_segs) return;
    const struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_capranges; i++) {
        uint32_t lo = st->base + s->capranges[i].start;
        st->cr_cb(lo, lo + s->capranges[i].count, st->cr_user);
    }
}

void input_segtrace_set_calltrace_cb(struct input_segtrace *st,
                                     segtrace_calltrace_fn cb, void *user)
{
    if (!st) return;
    st->ct_cb = cb; st->ct_user = user;
}

void input_segtrace_set_caprange_cb(struct input_segtrace *st,
                                    segtrace_caprange_fn cb, void *user)
{
    if (!st) return;
    st->cr_cb = cb; st->cr_user = user;
}

void input_segtrace_set_rngseed_cb(struct input_segtrace *st,
                                   segtrace_rngseed_fn cb, void *user)
{
    if (!st) return;
    st->rng_cb = cb; st->rng_user = user;
}

/* Clear a segment's {rngseed} fire flags so they re-arm on segment activation. */
static void rearm_setrngs(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_setrngs; i++) s->setrngs[i].fired = 0;
}

/* Fire any of the active segment's {rngseed} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). */
static void fire_setrngs(struct input_segtrace *st, struct seg_segment *s,
                         uint32_t frame)
{
    for (size_t i = 0; i < s->n_setrngs; i++) {
        struct seg_setrng *sr = &s->setrngs[i];
        if (!sr->fired && st->base + sr->frame <= frame) {
            if (st->rng_cb) st->rng_cb(sr->value, st->rng_user);
            sr->fired = 1;
        }
    }
}

int input_segtrace_has_calltrace(const struct input_segtrace *st)
{
    if (!st) return 0;
    for (size_t i = 0; i < st->n_segs; i++)
        if (st->segs[i].n_calltraces) return 1;
    return 0;
}

uint16_t input_segtrace_tick(struct input_segtrace *st, uint32_t frame,
                             segtrace_capture_fn capture_cb, void *user)
{
    if (!st || st->n_segs == 0) return 0;
    if (!st->started) {
        st->started = 1;
        st->cur_seg = 0; st->cur_entry = 0;
        st->base = 0; st->base_arm = 0;
        rearm_setrngs(st, 0);
        schedule_captures(st, 0, capture_cb, user);
        schedule_calltraces(st, 0);
        schedule_capranges(st, 0);
    }
    for (;;) {
        if (st->cur_seg >= st->n_segs) break;
        struct seg_segment *s = &st->segs[st->cur_seg];
        if (s->has_wait) {
            uint32_t af;
            if (anchor_fired_frame(st, s->wait, &af) && af > st->base_arm) {
                st->cur_seg++;
                st->base = af; st->base_arm = af; st->cur_entry = 0;
                rearm_setrngs(st, st->cur_seg);
                schedule_captures(st, st->cur_seg, capture_cb, user);
                schedule_calltraces(st, st->cur_seg);
                schedule_capranges(st, st->cur_seg);
                continue;  /* re-evaluate the next segment this same frame */
            }
            /* not resolved: fall through and keep applying this segment's
             * entries (spam-until-anchor) — the wait only short-circuits the
             * remaining entries once its anchor fires. */
        }
        while (st->cur_entry < s->n_entries &&
               st->base + s->entries[st->cur_entry].frame <= frame) {
            st->sticky = s->entries[st->cur_entry].mask;
            st->cur_entry++;
        }
        /* Force the LCG at base+frame BEFORE the caller hands this frame to sim
         * (the port ticks segtrace in input_poll, ahead of the RNG consumers). */
        fire_setrngs(st, s, frame);
        break;
    }
    return st->sticky;
}
