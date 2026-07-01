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

static int push_esc(struct seg_segment *s, uint32_t frame)
{
    if (s->n_escs >= s->cap_escs) {
        size_t ncap = s->cap_escs ? s->cap_escs * 2 : 4;
        struct seg_esc *ne = realloc(s->escs, ncap * sizeof *ne);
        if (!ne) return 0;
        s->escs = ne; s->cap_escs = ncap;
    }
    s->escs[s->n_escs].frame = frame;
    s->escs[s->n_escs].fired = 0;
    s->n_escs++;
    return 1;
}

static int push_gframe(struct seg_segment *s, uint32_t frame, uint32_t value)
{
    if (s->n_gframes >= s->cap_gframes) {
        size_t ncap = s->cap_gframes ? s->cap_gframes * 2 : 4;
        struct seg_gframe *ng = realloc(s->gframes, ncap * sizeof *ng);
        if (!ng) return 0;
        s->gframes = ng; s->cap_gframes = ncap;
    }
    s->gframes[s->n_gframes].frame = frame;
    s->gframes[s->n_gframes].value = value;
    s->gframes[s->n_gframes].fired = 0;
    s->n_gframes++;
    return 1;
}

static int push_gsimpin(struct seg_segment *s, uint32_t frame, uint32_t value)
{
    if (s->n_gsimpins >= s->cap_gsimpins) {
        size_t ncap = s->cap_gsimpins ? s->cap_gsimpins * 2 : 4;
        struct seg_gsimpin *ng = realloc(s->gsimpins, ncap * sizeof *ng);
        if (!ng) return 0;
        s->gsimpins = ng; s->cap_gsimpins = ncap;
    }
    s->gsimpins[s->n_gsimpins].frame = frame;
    s->gsimpins[s->n_gsimpins].value = value;
    s->gsimpins[s->n_gsimpins].fired = 0;
    s->n_gsimpins++;
    return 1;
}

static int push_bgnpcpin(struct seg_segment *s, uint32_t frame,
                         const uint32_t *values)
{
    if (s->n_bgnpcpins >= s->cap_bgnpcpins) {
        size_t ncap = s->cap_bgnpcpins ? s->cap_bgnpcpins * 2 : 2;
        struct seg_bgnpcpin *nb = realloc(s->bgnpcpins, ncap * sizeof *nb);
        if (!nb) return 0;
        s->bgnpcpins = nb; s->cap_bgnpcpins = ncap;
    }
    s->bgnpcpins[s->n_bgnpcpins].frame = frame;
    memcpy(s->bgnpcpins[s->n_bgnpcpins].values, values,
           SEG_BGNPCPIN_DWORDS * sizeof *values);
    s->bgnpcpins[s->n_bgnpcpins].fired = 0;
    s->n_bgnpcpins++;
    return 1;
}

static int push_phasepin(struct seg_segment *s, uint32_t frame)
{
    if (s->n_phasepins >= s->cap_phasepins) {
        size_t ncap = s->cap_phasepins ? s->cap_phasepins * 2 : 4;
        struct seg_phasepin *np = realloc(s->phasepins, ncap * sizeof *np);
        if (!np) return 0;
        s->phasepins = np; s->cap_phasepins = ncap;
    }
    s->phasepins[s->n_phasepins].frame = frame;
    s->phasepins[s->n_phasepins].fired = 0;
    s->n_phasepins++;
    return 1;
}

static int push_memsnap(struct seg_segment *s, uint32_t frame)
{
    if (s->n_memsnaps >= s->cap_memsnaps) {
        size_t ncap = s->cap_memsnaps ? s->cap_memsnaps * 2 : 4;
        struct seg_memsnap *nm = realloc(s->memsnaps, ncap * sizeof *nm);
        if (!nm) return 0;
        s->memsnaps = nm; s->cap_memsnaps = ncap;
    }
    s->memsnaps[s->n_memsnaps].frame = frame;
    s->memsnaps[s->n_memsnaps].fired = 0;
    s->n_memsnaps++;
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
        free(st->segs[i].escs);
        free(st->segs[i].gframes);
        free(st->segs[i].gsimpins);
        free(st->segs[i].bgnpcpins);
        free(st->segs[i].phasepins);
        free(st->segs[i].memsnaps);
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
        int      got_esc = 0, got_gframe = 0, got_phasepin = 0;
        int      got_savefile = 0, got_capstride = 0, got_memsnap = 0;
        int      got_tutloadpin = 0, got_wait_timeout = 0, got_csloadpin = 0;
        int      got_gsimpin = 0, got_bgnpcpin = 0, got_primaryloadpin = 0;
        int      got_bgnpcseed = 0;
        uint32_t wait_timeout_val = 0;
        uint32_t frame = 0, mask = 0, capture = 0;
        uint32_t ct_start = 0, ct_len = 0;
        uint32_t cr_start = 0, cr_count = 0;
        uint32_t rng_frame = 0, rng_value = 0, esc_frame = 0;
        uint32_t gf_frame = 0, gf_value = 0, pp_frame = 0, capstride_val = 0;
        uint32_t gsp_frame = 0, gsp_value = 0;
        uint32_t ms_frame = 0, tlp_val = 0, csloadpin_val = 0, plp_val = 0;
        uint32_t bnp_frame = 0, bgnpcseed_val = 0, bgnpcseed_cursor_val = 0;
        uint32_t bnp_values[SEG_BGNPCPIN_DWORDS];
        uint32_t bgnpcseed_dead[SEG_BGNPCPIN_DWORDS];
        int      bgnpcseed_dead_n = 0;
        char     waitname[24] = {0};
        char     savepath[256] = {0};

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
            } else if (klen == 7 && memcmp(ks, "timeout", 7) == 0) {
                /* {wait:NAME, timeout:N} — skip the wait after N frames if the
                 * anchor never fires (cross-target load-structure bridge). */
                if (!parse_number(&p, end, &wait_timeout_val)) return 0;
                got_wait_timeout = 1;
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
            } else if (klen == 3 && memcmp(ks, "esc", 3) == 0) {
                /* {esc:N} — synthesise an ESC keypress at base+N (scalar). */
                if (!parse_number(&p, end, &esc_frame)) return 0;
                got_esc = 1;
            } else if (klen == 6 && memcmp(ks, "gframe", 6) == 0) {
                /* {gframe:[frame,value]} — force the global frame counter to
                 * `value` at base+frame.  Array form only (like rngseed).
                 * EXPERIMENTAL — pins frame-count-derived state. */
                if (p >= end || *p != '[') return 0;
                p++;  /* '[' */
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &gf_frame)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ',') return 0;
                p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &gf_value)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ']') return 0;
                p++;
                got_gframe = 1;
            } else if (klen == 7 && memcmp(ks, "gsimpin", 7) == 0) {
                /* {gsimpin:[frame,value]} — force g_sim_frame_count to `value`
                 * at base+frame.  Array form only (like rngseed/gframe).  Pins
                 * the 目玉 display-sparkle %8 phase WITHOUT {phasepin}'s bg-NPC
                 * re-seed (which stalls the wrap-up cutscene). */
                if (p >= end || *p != '[') return 0;
                p++;  /* '[' */
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &gsp_frame)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ',') return 0;
                p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &gsp_value)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ']') return 0;
                p++;
                got_gsimpin = 1;
            } else if (klen == 8 && memcmp(ks, "bgnpcpin", 8) == 0) {
                /* {bgnpcpin:[frame,[d0..d149]]} — pin the bg-NPC SoA to retail's
                 * captured natural records.  Outer [frame, <inner>]; the inner
                 * array is exactly SEG_BGNPCPIN_DWORDS engine dwords (6 records x
                 * 0x64).  PORT-ONLY (the retail agent skips it). */
                if (p >= end || *p != '[') return 0;
                p++;  /* outer '[' */
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (!parse_number(&p, end, &bnp_frame)) return 0;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ',') return 0;
                p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != '[') return 0;
                p++;  /* inner '[' */
                for (size_t k = 0; k < SEG_BGNPCPIN_DWORDS; k++) {
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &bnp_values[k])) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (k + 1 < SEG_BGNPCPIN_DWORDS) {
                        if (p >= end || *p != ',') return 0;
                        p++;
                    }
                }
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ']') return 0;  /* inner ']' */
                p++;
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ']') return 0;  /* outer ']' */
                p++;
                got_bgnpcpin = 1;
            } else if (klen == 8 && memcmp(ks, "phasepin", 8) == 0) {
                /* {phasepin:N} — reset the companion's load-dependent free-roam
                 * phase (db054 bob/sparkle + sprite anim cycle) at base+N.
                 * Scalar (like {esc}); trace-comparison normalization only. */
                if (!parse_number(&p, end, &pp_frame)) return 0;
                got_phasepin = 1;
            } else if (klen == 7 && memcmp(ks, "memsnap", 7) == 0) {
                /* {memsnap:N} — dump the writable PE sections at base+N
                 * (phase-census input; fires once, pre-sim, like {phasepin}).
                 * Scalar. */
                if (!parse_number(&p, end, &ms_frame)) return 0;
                got_memsnap = 1;
            } else if (klen == 8 && memcmp(ks, "savefile", 8) == 0) {
                /* {savefile:"<relpath>"} — trace-global embedded-save ref.
                 * String value (path to a content-addressed .sav.gz blob,
                 * relative to the trace file's dir). Stored, not auto-loaded;
                 * the Python harness decompresses + drives --save-override. */
                if (!parse_string(&p, end, savepath, sizeof savepath)) return 0;
                got_savefile = 1;
            } else if (klen == 9 && memcmp(ks, "capstride", 9) == 0) {
                /* {capstride:N} — trace-global two-tier capture cadence (D3).
                 * Scalar (like {phasepin}); modifies {caprange} membership to
                 * every Nth frame from the window start. Last declaration wins. */
                if (!parse_number(&p, end, &capstride_val)) return 0;
                got_capstride = 1;
            } else if (klen == 10 && memcmp(ks, "tutloadpin", 10) == 0) {
                /* {tutloadpin:N} — trace-global tutorial-load-bracket pin
                 * (see the header doc). Scalar; last declaration wins. */
                if (!parse_number(&p, end, &tlp_val)) return 0;
                got_tutloadpin = 1;
            } else if (klen == 9 && memcmp(ks, "csloadpin", 9) == 0) {
                /* {csloadpin:N} — trace-global cc08==4 d3e load-bracket pin
                 * (customer_service_set_load_pin). Scalar; last decl wins. */
                if (!parse_number(&p, end, &csloadpin_val)) return 0;
                got_csloadpin = 1;
            } else if (klen == 14 && memcmp(ks, "primaryloadpin", 14) == 0) {
                /* {primaryloadpin:N} — trace-global cad868 primary-worker
                 * load-duration pin (worker_load_set_primary_pin). Scalar;
                 * last decl wins. */
                if (!parse_number(&p, end, &plp_val)) return 0;
                got_primaryloadpin = 1;
            } else if (klen == 9 && memcmp(ks, "bgnpcseed", 9) == 0) {
                /* {bgnpcseed:V} == [V,0], {bgnpcseed:[V,C]}, or
                 * {bgnpcseed:[V,C,[d0..d(25*C-1)]]} — trace-global bg-NPC
                 * warmup LCG-origin + spawn-cursor + dead-slot pin
                 * (scene1_bg_npc_seed_pin). Last decl wins. RE §21.21/§21.22.
                 * The optional 3rd element is C raw {bgnpcpin}-format engine
                 * records (BG_NPC_ENGINE_DWORDS=25 dwords each) for the dead
                 * slots [0,C) — their leftover x/y/z still feed the shadow
                 * pass (visible==-1-only check), so the port's BSS-zero
                 * default draws a stray shadow without them. */
                bgnpcseed_dead_n = 0;
                if (p < end && *p == '[') {
                    p++;  /* '[' */
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &bgnpcseed_val)) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (p >= end || *p != ',') return 0;
                    p++;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (!parse_number(&p, end, &bgnpcseed_cursor_val)) return 0;
                    while (p < end && (*p == ' ' || *p == '\t')) p++;
                    if (p < end && *p == ',') {
                        p++;
                        while (p < end && (*p == ' ' || *p == '\t')) p++;
                        if (p >= end || *p != '[') return 0;
                        p++;  /* inner '[' */
                        size_t want = (size_t)bgnpcseed_cursor_val * SEG_BGNPC_RECORD_DWORDS;
                        if (want > SEG_BGNPCPIN_DWORDS) return 0;
                        for (size_t k = 0; k < want; k++) {
                            while (p < end && (*p == ' ' || *p == '\t')) p++;
                            if (!parse_number(&p, end, &bgnpcseed_dead[k])) return 0;
                            while (p < end && (*p == ' ' || *p == '\t')) p++;
                            if (k + 1 < want) {
                                if (p >= end || *p != ',') return 0;
                                p++;
                            }
                        }
                        while (p < end && (*p == ' ' || *p == '\t')) p++;
                        if (p >= end || *p != ']') return 0;  /* inner ']' */
                        p++;
                        bgnpcseed_dead_n = (int)want;
                        while (p < end && (*p == ' ' || *p == '\t')) p++;
                    }
                    if (p >= end || *p != ']') return 0;  /* outer ']' */
                    p++;
                } else {
                    if (!parse_number(&p, end, &bgnpcseed_val)) return 0;
                    bgnpcseed_cursor_val = 0;
                }
                got_bgnpcseed = 1;
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
            cur->wait_timeout = got_wait_timeout ? wait_timeout_val : 0;
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
        } else if (got_esc) {
            if (!push_esc(cur, esc_frame)) return 0;
        } else if (got_gframe) {
            if (!push_gframe(cur, gf_frame, gf_value)) return 0;
        } else if (got_gsimpin) {
            if (!push_gsimpin(cur, gsp_frame, gsp_value)) return 0;
        } else if (got_bgnpcpin) {
            if (!push_bgnpcpin(cur, bnp_frame, bnp_values)) return 0;
        } else if (got_phasepin) {
            if (!push_phasepin(cur, pp_frame)) return 0;
        } else if (got_memsnap) {
            if (!push_memsnap(cur, ms_frame)) return 0;
        } else if (got_savefile) {
            /* Trace-global: last declaration wins. Not segment-scoped. */
            memcpy(out->savefile, savepath, sizeof out->savefile);
            out->has_savefile = 1;
        } else if (got_capstride) {
            /* Trace-global: last declaration wins. Not segment-scoped. */
            out->capstride = capstride_val;
            out->has_capstride = 1;
        } else if (got_tutloadpin) {
            /* Trace-global: last declaration wins. Not segment-scoped. */
            out->tutloadpin = tlp_val;
            out->has_tutloadpin = 1;
        } else if (got_csloadpin) {
            /* Trace-global: last declaration wins. Not segment-scoped. */
            out->csloadpin = csloadpin_val;
            out->has_csloadpin = 1;
        } else if (got_primaryloadpin) {
            /* Trace-global: last declaration wins. Not segment-scoped. */
            out->primaryloadpin = plp_val;
            out->has_primaryloadpin = 1;
        } else if (got_bgnpcseed) {
            /* Trace-global: last declaration wins. Not segment-scoped. */
            out->bgnpcseed        = bgnpcseed_val;
            out->bgnpcseed_cursor = (int)bgnpcseed_cursor_val;
            out->bgnpcseed_dead_n = bgnpcseed_dead_n;
            if (bgnpcseed_dead_n > 0)
                memcpy(out->bgnpcseed_dead, bgnpcseed_dead,
                       (size_t)bgnpcseed_dead_n * sizeof(uint32_t));
            out->has_bgnpcseed    = 1;
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

void input_segtrace_set_esc_cb(struct input_segtrace *st,
                               segtrace_esc_fn cb, void *user)
{
    if (!st) return;
    st->esc_cb = cb; st->esc_user = user;
}

void input_segtrace_set_gframe_cb(struct input_segtrace *st,
                                  segtrace_gframe_fn cb, void *user)
{
    if (!st) return;
    st->gf_cb = cb; st->gf_user = user;
}

/* 1 if any segment carries a {bgnpcpin} — the f406 first-customer marker.  The
 * retail capture auto-arms its wrap-up skip DRIVER on this same marker (RE
 * §21.5/§21.6); the port mirrors it (sim's wrap-up skip driver, viewer note #3). */
int input_segtrace_has_bgnpcpin(const struct input_segtrace *st)
{
    if (!st) return 0;
    for (size_t i = 0; i < st->n_segs; i++)
        if (st->segs[i].n_bgnpcpins > 0) return 1;
    return 0;
}

void input_segtrace_set_gsimpin_cb(struct input_segtrace *st,
                                   segtrace_gsimpin_fn cb, void *user)
{
    if (!st) return;
    st->gp_cb = cb; st->gp_user = user;
}

void input_segtrace_set_bgnpcpin_cb(struct input_segtrace *st,
                                    segtrace_bgnpcpin_fn cb, void *user)
{
    if (!st) return;
    st->bnp_cb = cb; st->bnp_user = user;
}

void input_segtrace_set_phasepin_cb(struct input_segtrace *st,
                                    segtrace_phasepin_fn cb, void *user)
{
    if (!st) return;
    st->pp_cb = cb; st->pp_user = user;
}

void input_segtrace_set_memsnap_cb(struct input_segtrace *st,
                                   segtrace_memsnap_fn cb, void *user)
{
    if (!st) return;
    st->ms_cb = cb; st->ms_user = user;
}

/* Clear a segment's {rngseed} fire flags so they re-arm on segment activation. */
static void rearm_setrngs(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_setrngs; i++) s->setrngs[i].fired = 0;
}

/* Clear a segment's {esc} fire flags so they re-arm on segment activation. */
static void rearm_escs(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_escs; i++) s->escs[i].fired = 0;
}

/* Clear a segment's {gframe} fire flags so they re-arm on segment activation. */
static void rearm_gframes(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_gframes; i++) s->gframes[i].fired = 0;
}

/* Clear a segment's {gsimpin} fire flags so they re-arm on segment activation. */
static void rearm_gsimpins(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_gsimpins; i++) s->gsimpins[i].fired = 0;
}

/* Clear a segment's {bgnpcpin} fire flags so they re-arm on segment activation. */
static void rearm_bgnpcpins(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_bgnpcpins; i++) s->bgnpcpins[i].fired = 0;
}

/* Clear a segment's {phasepin} fire flags so they re-arm on segment activation. */
static void rearm_phasepins(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_phasepins; i++) s->phasepins[i].fired = 0;
}

/* Clear a segment's {memsnap} fire flags so they re-arm on segment activation. */
static void rearm_memsnaps(struct input_segtrace *st, size_t seg_idx)
{
    if (seg_idx >= st->n_segs) return;
    struct seg_segment *s = &st->segs[seg_idx];
    for (size_t i = 0; i < s->n_memsnaps; i++) s->memsnaps[i].fired = 0;
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

/* Fire any of the active segment's {esc} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). */
static void fire_escs(struct input_segtrace *st, struct seg_segment *s,
                      uint32_t frame)
{
    for (size_t i = 0; i < s->n_escs; i++) {
        struct seg_esc *e = &s->escs[i];
        if (!e->fired && st->base + e->frame <= frame) {
            if (st->esc_cb) st->esc_cb(st->esc_user);
            e->fired = 1;
        }
    }
}

/* Fire any of the active segment's {gframe} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). */
static void fire_gframes(struct input_segtrace *st, struct seg_segment *s,
                         uint32_t frame)
{
    for (size_t i = 0; i < s->n_gframes; i++) {
        struct seg_gframe *g = &s->gframes[i];
        if (!g->fired && st->base + g->frame <= frame) {
            if (st->gf_cb) st->gf_cb(g->value, st->gf_user);
            g->fired = 1;
        }
    }
}

/* Fire any of the active segment's {gsimpin} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). */
static void fire_gsimpins(struct input_segtrace *st, struct seg_segment *s,
                          uint32_t frame)
{
    for (size_t i = 0; i < s->n_gsimpins; i++) {
        struct seg_gsimpin *g = &s->gsimpins[i];
        if (!g->fired && st->base + g->frame <= frame) {
            if (st->gp_cb) st->gp_cb(g->value, st->gp_user);
            g->fired = 1;
        }
    }
}

/* Fire any of the active segment's {bgnpcpin} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). */
static void fire_bgnpcpins(struct input_segtrace *st, struct seg_segment *s,
                           uint32_t frame)
{
    for (size_t i = 0; i < s->n_bgnpcpins; i++) {
        struct seg_bgnpcpin *b = &s->bgnpcpins[i];
        if (!b->fired && st->base + b->frame <= frame) {
            if (st->bnp_cb)
                st->bnp_cb(b->values, SEG_BGNPCPIN_DWORDS, st->bnp_user);
            b->fired = 1;
        }
    }
}

/* Fire any of the active segment's {phasepin} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). */
static void fire_phasepins(struct input_segtrace *st, struct seg_segment *s,
                           uint32_t frame)
{
    for (size_t i = 0; i < s->n_phasepins; i++) {
        struct seg_phasepin *pp = &s->phasepins[i];
        if (!pp->fired && st->base + pp->frame <= frame) {
            if (st->pp_cb) st->pp_cb(st->pp_user);
            pp->fired = 1;
        }
    }
}

/* Fire any of the active segment's {memsnap} ops whose frame base+frame has been
 * reached, once each, via the registered callback (NULL-safe). The callback gets
 * the RESOLVED frame base+N — stable dump filenames across runs. */
static void fire_memsnaps(struct input_segtrace *st, struct seg_segment *s,
                          uint32_t frame)
{
    for (size_t i = 0; i < s->n_memsnaps; i++) {
        struct seg_memsnap *ms = &s->memsnaps[i];
        if (!ms->fired && st->base + ms->frame <= frame) {
            if (st->ms_cb) st->ms_cb(st->base + ms->frame, st->ms_user);
            ms->fired = 1;
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
        st->base_anchor[0] = '\0';
        rearm_setrngs(st, 0);
        rearm_escs(st, 0);
        rearm_gframes(st, 0);
        rearm_gsimpins(st, 0);
        rearm_bgnpcpins(st, 0);
        rearm_phasepins(st, 0);
        rearm_memsnaps(st, 0);
        schedule_captures(st, 0, capture_cb, user);
        schedule_calltraces(st, 0);
        schedule_capranges(st, 0);
    }
    for (;;) {
        if (st->cur_seg >= st->n_segs) break;
        struct seg_segment *s = &st->segs[st->cur_seg];
        if (s->has_wait) {
            uint32_t af;
            /* A DIFFERENT next anchor may fire on the SAME frame the current
             * segment was entered (recording-adjacent anchors compress to one
             * frame on replay), so it resolves at af >= entry. The SAME anchor
             * recurring (HOUSE_FREEROAM twice) must take the NEXT firing, so it
             * requires af > entry. Without the per-name distinction a same-frame
             * anchor cluster stalls the whole {wait} chain. Mirrors the JS agent. */
            int same_name = (strcmp(s->wait, st->base_anchor) == 0);
            if (anchor_fired_frame(st, s->wait, &af) &&
                (same_name ? af > st->base_arm : af >= st->base_arm)) {
                st->cur_seg++;
                st->base = af; st->base_arm = af; st->cur_entry = 0;
                strncpy(st->base_anchor, s->wait, sizeof st->base_anchor - 1);
                st->base_anchor[sizeof st->base_anchor - 1] = '\0';
                rearm_setrngs(st, st->cur_seg);
                rearm_escs(st, st->cur_seg);
                rearm_gframes(st, st->cur_seg);
                rearm_gsimpins(st, st->cur_seg);
                rearm_bgnpcpins(st, st->cur_seg);
                rearm_phasepins(st, st->cur_seg);
                rearm_memsnaps(st, st->cur_seg);
                schedule_captures(st, st->cur_seg, capture_cb, user);
                schedule_calltraces(st, st->cur_seg);
                schedule_capranges(st, st->cur_seg);
                continue;  /* re-evaluate the next segment this same frame */
            }
            /* Cross-target optional wait: the anchor hasn't fired within
             * wait_timeout frames AFTER this segment's LAST recorded input.
             * Skip it WITHOUT adopting a new base, so the next segment's
             * frames/caprange stay relative to the last RESOLVED anchor.
             * Bridges the port collapsing a retail load-cycle burst into fewer
             * loads (the load-cycle waits the port never fires).
             *
             * Origin is the last entry's frame (base + last), NOT segment entry
             * (base_arm): a segment's recorded inputs must ALL apply before its
             * terminating wait can time out.  Measuring from segment entry ate
             * the customer-tutorial walk@rel66 + Z@rel156 under a timeout-60 —
             * the timeout fired at rel60, BEFORE the walk, so the player never
             * reached the counter / entered cc08==4.  The timeout's intent is
             * "hold the last input N frames waiting for the optional anchor", so
             * the countdown starts at that input.  Entries are ascending and
             * base==base_arm, so base+last+timeout is always >= the old
             * base_arm+timeout: this only ever DELAYS a timeout (a no-op when the
             * last entry is at rel0).  Add form (not frame-base sub) since the
             * last entry may still be in the future — no unsigned wrap. */
            uint32_t to_origin = st->base
                + (s->n_entries > 0
                       ? s->entries[s->n_entries - 1].frame : 0);
            if (s->wait_timeout > 0 && frame >= to_origin + s->wait_timeout) {
                st->cur_seg++;
                st->cur_entry = 0;
                rearm_setrngs(st, st->cur_seg);
                rearm_escs(st, st->cur_seg);
                rearm_gframes(st, st->cur_seg);
                rearm_gsimpins(st, st->cur_seg);
                rearm_bgnpcpins(st, st->cur_seg);
                rearm_phasepins(st, st->cur_seg);
                rearm_memsnaps(st, st->cur_seg);
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
        /* ESC fires in the same pre-sim window as {rngseed} so a recorded
         * dialogue-skip arms the skip prompt before that frame's sim runs. */
        fire_escs(st, s, frame);
        /* {gframe} fires in the same pre-sim window (EXPERIMENTAL frame-counter pin). */
        fire_gframes(st, s, frame);
        /* {gsimpin} fires in the same pre-sim window — pin g_sim_frame_count (the
         * 目玉-sparkle %8 phase) before that frame's sparkle/records-B consumers. */
        fire_gsimpins(st, s, frame);
        /* {bgnpcpin} fires in the same pre-sim window — overwrite the bg-NPC SoA
         * with retail's captured natural layout before that frame's bg_npc_tick
         * (respawn/pause) consumes the shared LCG. */
        fire_bgnpcpins(st, s, frame);
        /* {phasepin} fires in the same pre-sim window — normalize the companion's
         * load-dependent free-roam phase before that frame's bob/anim consumers. */
        fire_phasepins(st, s, frame);
        /* {memsnap} fires in the same pre-sim window, AFTER the pins above, so a
         * pinned-census snapshot sees the post-pin state of its own frame. */
        fire_memsnaps(st, s, frame);
        break;
    }
    return st->sticky;
}
