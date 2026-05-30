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

/* Skip a `[ ... ]` array value (we don't need its contents for `calltrace`). */
static int skip_array(const char **pp, const char *end)
{
    const char *p = *pp;
    if (p >= end || *p != '[') return 0;
    p++;
    while (p < end && *p != ']') p++;
    if (p >= end) return 0;
    p++;
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

void input_segtrace_free(struct input_segtrace *st)
{
    if (!st) return;
    for (size_t i = 0; i < st->n_segs; i++) {
        free(st->segs[i].entries);
        free(st->segs[i].captures);
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
        int      got_calltrace = 0;
        uint32_t frame = 0, mask = 0, capture = 0;
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
                /* Retail-side op — parse + ignore (scalar or [start,len]). */
                if (p < end && *p == '[') { if (!skip_array(&p, end)) return 0; }
                else { uint32_t dummy; if (!parse_number(&p, end, &dummy)) return 0; }
                got_calltrace = 1;
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
            /* ignored on the port */
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

uint16_t input_segtrace_tick(struct input_segtrace *st, uint32_t frame,
                             segtrace_capture_fn capture_cb, void *user)
{
    if (!st || st->n_segs == 0) return 0;
    if (!st->started) {
        st->started = 1;
        st->cur_seg = 0; st->cur_entry = 0;
        st->base = 0; st->base_arm = 0;
        schedule_captures(st, 0, capture_cb, user);
    }
    for (;;) {
        if (st->cur_seg >= st->n_segs) break;
        struct seg_segment *s = &st->segs[st->cur_seg];
        if (s->has_wait) {
            uint32_t af;
            if (anchor_fired_frame(st, s->wait, &af) && af > st->base_arm) {
                st->cur_seg++;
                st->base = af; st->base_arm = af; st->cur_entry = 0;
                schedule_captures(st, st->cur_seg, capture_cb, user);
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
        break;
    }
    return st->sticky;
}
