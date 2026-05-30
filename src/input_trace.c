/*
 * input_trace.c — sparse JSONL record + replay for input regression
 * harness. Pure C, no Win32, no DirectInput. See input_trace.h for
 * format + role.
 *
 * The parser is intentionally narrow:
 *   - Whitespace + `#` comments tolerated.
 *   - JSON keys must be one of "frame" / "buttons", anything else
 *     fails the line.
 *   - Values may be decimal or 0x-hex; `buttons` may also be a JSON
 *     string holding the same.
 *   - Frame indices must strictly increase.
 *
 * Anything fancier (real JSON, nested objects, alternate schemas) is
 * deferred until a scenario actually needs it.
 */

#include "input_trace.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── Record ─────────────────────────────────────────────────────────── */

static FILE    *g_record_fp        = NULL;
static int      g_record_started   = 0;     /* did we emit the seed line? */
static uint16_t g_record_last_mask = 0;

int input_trace_record_open(const char *path)
{
    if (!path) return 0;
    if (g_record_fp) {
        fclose(g_record_fp);
        g_record_fp = NULL;
    }
    g_record_fp        = fopen(path, "w");
    g_record_started   = 0;
    g_record_last_mask = 0;
    return g_record_fp != NULL;
}

void input_trace_record_frame(uint32_t frame, uint16_t mask)
{
    if (!g_record_fp) return;
    /* Always emit the first frame so replays know the starting mask
     * without having to guess "0 before the first entry". Subsequent
     * frames only emit on transitions. */
    if (!g_record_started) {
        fprintf(g_record_fp,
                "{\"frame\":%u,\"buttons\":\"0x%04x\"}\n",
                (unsigned)frame, (unsigned)mask);
        g_record_started   = 1;
        g_record_last_mask = mask;
        fflush(g_record_fp);
        return;
    }
    if (mask != g_record_last_mask) {
        fprintf(g_record_fp,
                "{\"frame\":%u,\"buttons\":\"0x%04x\"}\n",
                (unsigned)frame, (unsigned)mask);
        g_record_last_mask = mask;
        fflush(g_record_fp);
    }
}

void input_trace_record_close(void)
{
    if (g_record_fp) {
        fflush(g_record_fp);
        fclose(g_record_fp);
        g_record_fp = NULL;
    }
    g_record_started   = 0;
    g_record_last_mask = 0;
}

int input_trace_record_is_open(void)
{
    return g_record_fp != NULL;
}

/* ─── Parser ─────────────────────────────────────────────────────────── */

/* Skip ASCII whitespace + `#`-prefixed comment lines. Advances `*pp` in
 * place; tolerates EOF (returns with *pp at end). */
static void skip_ws_and_comments(const char **pp, const char *end)
{
    const char *p = *pp;
    while (p < end) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
        if (p < end && *p == '#') {
            while (p < end && *p != '\n') p++;
            continue;
        }
        break;
    }
    *pp = p;
}

/* Parse a JSON-ish numeric value (decimal or 0x-hex) ending at the
 * next `,`, `}`, or whitespace. Optionally quoted: if `*p == '"'`
 * we strip the surrounding quotes. On success returns 1, stores the
 * value in `*out`, and advances `*pp` past the value (and the closing
 * quote if any). */
static int parse_number(const char **pp, const char *end, uint32_t *out)
{
    const char *p = *pp;
    int quoted = 0;
    if (p < end && *p == '"') { quoted = 1; p++; }

    /* Hand to strtoul which already handles 0x prefixes when base=0. */
    char buf[32];
    size_t n = 0;
    while (p < end && n + 1 < sizeof buf) {
        char c = *p;
        if (c == ',' || c == '}' || c == '"' ||
            c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        buf[n++] = c;
        p++;
    }
    if (n == 0) return 0;
    buf[n] = '\0';

    char *endp = NULL;
    unsigned long v = strtoul(buf, &endp, 0);
    if (!endp || *endp != '\0') return 0;

    if (quoted) {
        if (p >= end || *p != '"') return 0;
        p++;
    }
    *out = (uint32_t)v;
    *pp  = p;
    return 1;
}

/* Parse a JSON key of the form `"name":` and return which key matched
 * via *kind: 0=frame, 1=buttons, -1=error. Advances `*pp` past the
 * colon. */
static int parse_key(const char **pp, const char *end, int *kind)
{
    const char *p = *pp;
    if (p >= end || *p != '"') return 0;
    p++;
    const char *kstart = p;
    while (p < end && *p != '"') p++;
    if (p >= end) return 0;
    size_t klen = (size_t)(p - kstart);
    p++;  /* skip closing quote */

    /* Skip ws then expect colon. */
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p >= end || *p != ':') return 0;
    p++;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    if (klen == 5 && memcmp(kstart, "frame", 5) == 0) {
        *kind = 0;
    } else if (klen == 7 && memcmp(kstart, "buttons", 7) == 0) {
        *kind = 1;
    } else {
        *kind = -1;
        return 0;
    }
    *pp = p;
    return 1;
}

/* Parse one `{"frame":N,"buttons":...}` line. On success, fills *out
 * and advances *pp past the closing brace. On failure, returns 0;
 * caller can decide whether to abort the file or skip to next line. */
static int parse_entry(const char **pp, const char *end,
                       struct input_trace_entry *out)
{
    const char *p = *pp;
    if (p >= end || *p != '{') return 0;
    p++;

    int got_frame = 0, got_mask = 0;
    uint32_t frame = 0, mask = 0;

    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        int kind = -1;
        if (!parse_key(&p, end, &kind)) return 0;

        uint32_t v;
        if (!parse_number(&p, end, &v)) return 0;
        if (kind == 0) { frame = v; got_frame = 1; }
        else           { mask  = v; got_mask  = 1; }

        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p < end && *p == ',') { p++; continue; }
        if (p < end && *p == '}') { p++; break; }
        return 0;
    }

    if (!got_frame || !got_mask) return 0;
    if (mask > 0xffffu)            return 0;

    out->frame = frame;
    out->mask  = (uint16_t)mask;
    *pp = p;
    return 1;
}

void input_trace_free(struct input_trace *trace)
{
    if (!trace) return;
    free(trace->entries);
    trace->entries = NULL;
    trace->count   = 0;
    trace->cap     = 0;
}

/* Append one entry, growing the heap table on demand (doubling, seeded
 * at 256). Returns 1 on success, 0 if the sanity ceiling is hit or the
 * allocation fails — both reported loudly so a runaway file isn't a
 * silent truncation like the old fixed-array cap. */
static int trace_push(struct input_trace *out, struct input_trace_entry e)
{
    if (out->count >= INPUT_TRACE_MAX_ENTRIES) {
        fprintf(stderr,
            "input_trace: exceeded %u-entry sanity ceiling — refusing "
            "(corrupt or runaway trace?)\n",
            (unsigned)INPUT_TRACE_MAX_ENTRIES);
        return 0;
    }
    if (out->count >= out->cap) {
        size_t ncap = out->cap ? out->cap * 2 : 256;
        struct input_trace_entry *ne =
            realloc(out->entries, ncap * sizeof *ne);
        if (!ne) {
            fprintf(stderr, "input_trace: out of memory growing table to "
                            "%zu entries\n", ncap);
            return 0;
        }
        out->entries = ne;
        out->cap     = ncap;
    }
    out->entries[out->count++] = e;
    return 1;
}

int input_trace_parse_buf(const char *buf, size_t len, struct input_trace *out)
{
    if (!buf || !out) return 0;
    /* Start clean: drop any prior table so the caller can reuse an
     * `out` across loads. Partial state on failure is preserved (the
     * entries parsed before the bad line) for diagnostics. */
    input_trace_free(out);

    const char *p   = buf;
    const char *end = buf + len;
    uint32_t last_frame = 0;
    int      have_prev  = 0;

    for (;;) {
        skip_ws_and_comments(&p, end);
        if (p >= end) break;

        struct input_trace_entry e = {0};
        if (!parse_entry(&p, end, &e)) return 0;

        if (have_prev && e.frame <= last_frame) return 0;
        if (!trace_push(out, e)) return 0;
        last_frame = e.frame;
        have_prev  = 1;
    }
    return 1;
}

int input_trace_load(const char *path, struct input_trace *out)
{
    if (!path || !out) return 0;
    FILE *fp = fopen(path, "rb");
    if (!fp) return 0;

    /* Slurp the whole file into a growing heap buffer — no fixed cap,
     * so a full-game trace (multi-MiB) loads instead of silently
     * failing. Chunked read works on non-seekable inputs too. */
    char  *buf = NULL;
    size_t len = 0, cap = 0;
    for (;;) {
        if (len == cap) {
            size_t ncap = cap ? cap * 2 : (1u << 16);
            char *nb = realloc(buf, ncap);
            if (!nb) {
                fprintf(stderr, "input_trace: out of memory reading %s\n", path);
                free(buf);
                fclose(fp);
                return 0;
            }
            buf = nb;
            cap = ncap;
        }
        size_t got = fread(buf + len, 1, cap - len, fp);
        len += got;
        if (got == 0) break;   /* EOF or error */
    }
    int read_err = ferror(fp);
    fclose(fp);
    if (read_err) {
        fprintf(stderr, "input_trace: read error on %s\n", path);
        free(buf);
        return 0;
    }

    int rc = input_trace_parse_buf(buf, len, out);
    free(buf);
    return rc;
}

uint16_t input_trace_lookup(const struct input_trace *trace, uint32_t frame)
{
    if (!trace || trace->count == 0) return 0;
    /* Binary search for the largest entry with entries[i].frame <= frame. */
    size_t lo = 0, hi = trace->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (trace->entries[mid].frame <= frame) lo = mid + 1;
        else                                    hi = mid;
    }
    if (lo == 0) return 0;
    return trace->entries[lo - 1].mask;
}
