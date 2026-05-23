/*
 * xfile.c — implementation of the .x text-format parser.
 *
 * Token-driven recursive descent. Tokenizer strips line and block
 * comments, preserves line numbers, then a recursive walker dispatches per
 * template type. Unknown templates are brace-skipped without comment.
 *
 * Matches docs/formats/xfile.md and the Python oracle at
 * tools/extract/xfile.py.
 */

#include "xfile.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ───── Token kinds ────────────────────────────────────────────────────── */

enum {
    T_IDENT = 1, T_INT, T_FLT, T_STR, T_UID,
    T_LBR, T_RBR, T_SEM, T_COM, T_EOF
};

typedef struct {
    int          kind;
    int          line;
    int64_t      ival;        /* T_INT */
    double       fval;        /* T_FLT */
    const char  *str;         /* T_IDENT, T_STR, T_UID — points into source buffer */
    size_t       slen;
} xtok;

/* ───── Parser state ──────────────────────────────────────────────────── */

typedef struct {
    xfile_t      *out;
    const xtok   *toks;
    size_t        ntoks;
    size_t        pos;

    /* skipped template counts are useful for diagnostics but not in the
     * public shape. We just print a one-line summary if any non-trivial
     * template gets dropped. */
} P;

static void set_error(xfile_t *x, int line, const char *fmt, ...)
{
    if (x->error[0]) return;   /* keep the first error */
    char tail[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tail, sizeof tail, fmt, ap);
    va_end(ap);
    /* %.80s caps the path so the result fits in error[256] even with
     * a long line + tail. */
    snprintf(x->error, sizeof x->error, "%.80s:%d: %s",
             x->path[0] ? x->path : "<mem>", line, tail);
}

/* ───── Tokenizer ─────────────────────────────────────────────────────── */

static int is_ident_start(int c) { return (isalpha(c) || c == '_'); }
static int is_ident_cont(int c)  { return (isalnum(c) || c == '_'); }

/* Strip line and block comments from src in place, replacing comment
 * characters with ' ' (newlines preserved so line numbers stay valid). */
static void strip_comments(char *src, size_t len)
{
    size_t i = 0;
    while (i < len) {
        if (i + 1 < len && src[i] == '/' && src[i+1] == '/') {
            while (i < len && src[i] != '\n') src[i++] = ' ';
        } else if (i + 1 < len && src[i] == '/' && src[i+1] == '*') {
            src[i++] = ' '; src[i++] = ' ';
            while (i + 1 < len) {
                if (src[i] == '*' && src[i+1] == '/') {
                    src[i++] = ' '; src[i++] = ' ';
                    break;
                }
                if (src[i] != '\n') src[i] = ' ';
                i++;
            }
        } else {
            i++;
        }
    }
}

/* Match an optional [+-] followed by digits, optional '.', digits, optional [eE][+-]digits.
 * Returns the number of characters consumed (>= 0). If a '.' or 'e/E' is in
 * the match, *is_float is set to 1. */
static size_t match_number(const char *p, size_t n, int *is_float)
{
    size_t i = 0;
    int saw_digit = 0, has_dot = 0, has_exp = 0;
    if (i < n && (p[i] == '+' || p[i] == '-')) i++;
    while (i < n && isdigit((unsigned char)p[i])) { i++; saw_digit = 1; }
    if (i < n && p[i] == '.') {
        has_dot = 1; i++;
        while (i < n && isdigit((unsigned char)p[i])) { i++; saw_digit = 1; }
    }
    if (saw_digit && i < n && (p[i] == 'e' || p[i] == 'E')) {
        size_t j = i + 1;
        if (j < n && (p[j] == '+' || p[j] == '-')) j++;
        if (j < n && isdigit((unsigned char)p[j])) {
            has_exp = 1;
            i = j;
            while (i < n && isdigit((unsigned char)p[i])) i++;
        }
    }
    if (!saw_digit) return 0;
    *is_float = (has_dot || has_exp) ? 1 : 0;
    return i;
}

static int tokenize(char *src, size_t len, xtok **out_toks, size_t *out_n,
                    xfile_t *err_sink)
{
    /* Two passes would let us size exactly, but we'd rather not parse
     * twice. Grow geometrically; .x files are 60-200 tokens per KB. */
    size_t cap = 64;
    xtok  *t   = (xtok *)malloc(cap * sizeof *t);
    if (!t) return -1;
    size_t n   = 0;

    int    line = 1;
    size_t i    = 0;

    #define EMIT(_k, _line) do { \
        if (n == cap) { \
            size_t nc = cap * 2; \
            xtok *nt = (xtok *)realloc(t, nc * sizeof *t); \
            if (!nt) { free(t); return -1; } \
            t = nt; cap = nc; \
        } \
        memset(&t[n], 0, sizeof t[n]); \
        t[n].kind = (_k); t[n].line = (_line); \
    } while (0)

    while (i < len) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }

        /* UUID <...> */
        if (c == '<') {
            size_t j = i + 1;
            while (j < len && src[j] != '>') j++;
            if (j >= len) {
                set_error(err_sink, line, "unterminated UUID");
                free(t); return -1;
            }
            EMIT(T_UID, line);
            t[n].str  = src + i + 1;
            t[n].slen = (j - i) - 1;
            n++;
            i = j + 1;
            continue;
        }

        /* String literal "..." */
        if (c == '"') {
            size_t j = i + 1;
            while (j < len && src[j] != '"') {
                if (src[j] == '\n') line++;
                j++;
            }
            if (j >= len) {
                set_error(err_sink, line, "unterminated string");
                free(t); return -1;
            }
            EMIT(T_STR, line);
            t[n].str  = src + i + 1;
            t[n].slen = (j - i) - 1;
            n++;
            i = j + 1;
            continue;
        }

        if (c == '{') { EMIT(T_LBR, line); n++; i++; continue; }
        if (c == '}') { EMIT(T_RBR, line); n++; i++; continue; }
        if (c == ';') { EMIT(T_SEM, line); n++; i++; continue; }
        if (c == ',') { EMIT(T_COM, line); n++; i++; continue; }

        /* Number? Must start with digit or +/-/. followed by digit. */
        {
            int    is_float = 0;
            size_t take     = match_number(src + i, len - i, &is_float);
            if (take > 0) {
                EMIT(is_float ? T_FLT : T_INT, line);
                if (is_float) {
                    /* Local copy to terminate for strtod */
                    char buf[64];
                    size_t cn = take < sizeof buf - 1 ? take : sizeof buf - 1;
                    memcpy(buf, src + i, cn);
                    buf[cn] = '\0';
                    t[n].fval = strtod(buf, NULL);
                } else {
                    char buf[32];
                    size_t cn = take < sizeof buf - 1 ? take : sizeof buf - 1;
                    memcpy(buf, src + i, cn);
                    buf[cn] = '\0';
                    t[n].ival = (int64_t)strtoll(buf, NULL, 10);
                }
                n++;
                i += take;
                continue;
            }
        }

        if (is_ident_start(c)) {
            size_t j = i + 1;
            while (j < len && is_ident_cont((unsigned char)src[j])) j++;
            EMIT(T_IDENT, line);
            t[n].str  = src + i;
            t[n].slen = j - i;
            n++;
            i = j;
            continue;
        }

        /* Unknown character: silently drop. Mirrors the Python tokenizer's
         * handling of stray '-' inside identifiers (PDX02_-_Default). */
        i++;
    }

    EMIT(T_EOF, line);
    n++;

    *out_toks = t;
    *out_n    = n;
    return 0;
    #undef EMIT
}

/* ───── Token-stream helpers ──────────────────────────────────────────── */

static const xtok *peek(P *p)
{
    return &p->toks[p->pos];
}

static const xtok *advance(P *p)
{
    const xtok *t = &p->toks[p->pos];
    if (t->kind != T_EOF) p->pos++;
    return t;
}

static int eat_if(P *p, int kind)
{
    if (peek(p)->kind == kind) { p->pos++; return 1; }
    return 0;
}

static int expect(P *p, int kind, const char *what)
{
    const xtok *t = advance(p);
    if (t->kind != kind) {
        set_error(p->out, t->line, "expected %s, got token kind %d", what, t->kind);
        return 0;
    }
    return 1;
}

/* Read INT or FLT as double. */
static int eat_number(P *p, double *out)
{
    const xtok *t = advance(p);
    if (t->kind == T_INT) { *out = (double)t->ival; return 1; }
    if (t->kind == T_FLT) { *out = t->fval; return 1; }
    set_error(p->out, t->line, "expected number");
    return 0;
}

static int eat_int32(P *p, int32_t *out)
{
    const xtok *t = advance(p);
    if (t->kind == T_INT) { *out = (int32_t)t->ival; return 1; }
    if (t->kind == T_FLT) { *out = (int32_t)t->fval; return 1; }
    set_error(p->out, t->line, "expected integer");
    return 0;
}

/* Skip body of a {...} block whose LBR has been consumed. Stops AT the
 * matching RBR (without consuming it). */
static void skip_block_body(P *p)
{
    int depth = 1;
    while (depth > 0) {
        const xtok *t = peek(p);
        if (t->kind == T_EOF) {
            set_error(p->out, t->line, "unexpected EOF in skip_block_body");
            return;
        }
        if (t->kind == T_LBR) { depth++; advance(p); }
        else if (t->kind == T_RBR) {
            depth--;
            if (depth > 0) advance(p);
            /* leave the final RBR for caller's expect() */
        } else {
            advance(p);
        }
    }
}

/* Read an optional UUID (consumes it if present). */
static void skip_optional_uuid(P *p)
{
    eat_if(p, T_UID);
}

/* Consume zero-or-more consecutive IDENT tokens (handling the
 * hyphen-stitch case e.g. PDX02_-_Default) iff followed by LBR or UID.
 * Concatenates into `dst`. Returns 1 if a name was consumed. */
static int read_optional_instance_name(P *p, char *dst, size_t dstsz)
{
    if (peek(p)->kind != T_IDENT) { dst[0] = '\0'; return 0; }

    /* Look ahead for LBR or UID, with only IDENT tokens between. */
    size_t i = p->pos;
    while (i < p->ntoks) {
        int k = p->toks[i].kind;
        if (k == T_LBR || k == T_UID) break;
        if (k == T_IDENT) { i++; continue; }
        /* anything else means this sequence isn't an instance name */
        dst[0] = '\0';
        return 0;
    }
    /* Concatenate the IDENT tokens at [p->pos, i). */
    size_t used = 0;
    for (size_t j = p->pos; j < i; j++) {
        const xtok *t = &p->toks[j];
        size_t add = t->slen;
        if (used + add >= dstsz) add = dstsz - 1 - used;
        memcpy(dst + used, t->str, add);
        used += add;
        if (used >= dstsz - 1) break;
    }
    dst[used] = '\0';
    p->pos = i;
    return used > 0;
}

/* ───── Dynamic array grow ────────────────────────────────────────────── */

static int grow_array(void **arr, size_t *cap, size_t need, size_t elem)
{
    if (need <= *cap) return 1;
    size_t nc = *cap ? *cap : 4;
    while (nc < need) nc *= 2;
    void *na = realloc(*arr, nc * elem);
    if (!na) return 0;
    /* Zero the new tail. */
    memset((char *)na + (*cap) * elem, 0, (nc - *cap) * elem);
    *arr = na;
    *cap = nc;
    return 1;
}

/* ───── Read helpers for known struct shapes ──────────────────────────── */

static int read_vec3(P *p, xfile_vec3 *v)
{
    double x, y, z;
    if (!eat_number(p, &x) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &y) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &z) || !expect(p, T_SEM, ";")) return 0;
    v->x = (float)x; v->y = (float)y; v->z = (float)z;
    return 1;
}

static int read_vec2(P *p, xfile_vec2 *v)
{
    double u, w;
    if (!eat_number(p, &u) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &w) || !expect(p, T_SEM, ";")) return 0;
    v->u = (float)u; v->v = (float)w;
    return 1;
}

static int read_rgba(P *p, xfile_rgba *c)
{
    double r, g, b, a;
    if (!eat_number(p, &r) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &g) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &b) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &a) || !expect(p, T_SEM, ";")) return 0;
    c->r = (float)r; c->g = (float)g; c->b = (float)b; c->a = (float)a;
    return 1;
}

static int read_rgb(P *p, xfile_rgb *c)
{
    double r, g, b;
    if (!eat_number(p, &r) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &g) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_number(p, &b) || !expect(p, T_SEM, ";")) return 0;
    c->r = (float)r; c->g = (float)g; c->b = (float)b;
    return 1;
}

static int read_matrix16(P *p, float out[16])
{
    for (int i = 0; i < 16; i++) {
        double v;
        if (!eat_number(p, &v)) return 0;
        out[i] = (float)v;
        if (i < 15) {
            if (!expect(p, T_COM, ",")) return 0;
        } else {
            /* trailing ;; — two consecutive SEM */
            if (!expect(p, T_SEM, ";")) return 0;
            if (!expect(p, T_SEM, ";")) return 0;
        }
    }
    return 1;
}

static int read_mesh_face(P *p, xfile_face *f)
{
    int32_t cnt;
    if (!eat_int32(p, &cnt) || !expect(p, T_SEM, ";")) return 0;
    if (cnt < 0 || cnt > XFILE_MAX_FACE_VERTS) {
        set_error(p->out, p->toks[p->pos].line,
                  "face vertex count %d out of range [0,%d]", cnt, XFILE_MAX_FACE_VERTS);
        return 0;
    }
    f->count = cnt;
    for (int i = 0; i < cnt; i++) {
        if (!eat_int32(p, &f->verts[i])) return 0;
        if (i < cnt - 1) {
            if (!expect(p, T_COM, ",")) return 0;
        }
    }
    /* trailing ; (single, between this face and the next or closing the array) */
    if (!expect(p, T_SEM, ";")) return 0;
    return 1;
}

/* ───── Texture-name dedupe (per-file) ─────────────────────────────────── */

static void record_texture(xfile_t *x, const char *name, size_t *cap)
{
    if (!name || !name[0]) return;
    for (int32_t i = 0; i < x->texture_count; i++) {
        if (strcmp(x->textures[i], name) == 0) return;
    }
    if (!grow_array((void **)&x->textures, cap, (size_t)x->texture_count + 1,
                    sizeof *x->textures))
        return;
    size_t n = strlen(name);
    if (n >= XFILE_TEXTURE_MAX) n = XFILE_TEXTURE_MAX - 1;
    memcpy(x->textures[x->texture_count], name, n);
    x->textures[x->texture_count][n] = '\0';
    x->texture_count++;
}

/* ───── Per-template parsers ──────────────────────────────────────────── */
/* Convention: caller has consumed LBR; parser stops AT the matching RBR
 * (does NOT consume it). On error, set p->out->error and return 0. */

static int parse_texture_filename(P *p, char dst[XFILE_TEXTURE_MAX])
{
    skip_optional_uuid(p);
    const xtok *t = peek(p);
    if (t->kind == T_STR) {
        size_t n = t->slen < XFILE_TEXTURE_MAX - 1 ? t->slen : XFILE_TEXTURE_MAX - 1;
        memcpy(dst, t->str, n);
        dst[n] = '\0';
        advance(p);
        if (!expect(p, T_SEM, ";")) return 0;
        return 1;
    }
    /* drain to RBR */
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) advance(p);
    dst[0] = '\0';
    return 1;
}

static int parse_material_body(P *p, xfile_material *m)
{
    skip_optional_uuid(p);
    if (!read_rgba(p, &m->diffuse))   return 0;
    if (!expect(p, T_SEM, ";"))        return 0;  /* second ; of ;; */
    double pow;
    if (!eat_number(p, &pow))          return 0;
    m->power = (float)pow;
    if (!expect(p, T_SEM, ";"))        return 0;
    if (!read_rgb(p, &m->specular))    return 0;
    if (!expect(p, T_SEM, ";"))        return 0;
    if (!read_rgb(p, &m->emissive))    return 0;
    if (!expect(p, T_SEM, ";"))        return 0;

    m->texture[0] = '\0';
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind == T_IDENT && t->slen == strlen("TextureFilename")
            && memcmp(t->str, "TextureFilename", t->slen) == 0) {
            advance(p);
            char ignored[XFILE_NAME_MAX];
            read_optional_instance_name(p, ignored, sizeof ignored);
            skip_optional_uuid(p);
            if (!expect(p, T_LBR, "{")) return 0;
            if (!parse_texture_filename(p, m->texture)) return 0;
            if (!expect(p, T_RBR, "}")) return 0;
        } else if (t->kind == T_IDENT) {
            /* unknown nested template */
            advance(p);
            char ignored[XFILE_NAME_MAX];
            read_optional_instance_name(p, ignored, sizeof ignored);
            skip_optional_uuid(p);
            if (peek(p)->kind == T_LBR) {
                advance(p);
                skip_block_body(p);
                if (!expect(p, T_RBR, "}")) return 0;
            }
        } else {
            advance(p);
        }
    }
    return 1;
}

static int parse_mesh_normals(P *p, xfile_mesh *m)
{
    skip_optional_uuid(p);
    int32_t n;
    if (!eat_int32(p, &n) || !expect(p, T_SEM, ";")) return 0;
    m->normal_count = n;
    if (n > 0) {
        m->normals = (xfile_vec3 *)calloc((size_t)n, sizeof *m->normals);
        if (!m->normals) { set_error(p->out, 0, "oom normals"); return 0; }
        for (int32_t i = 0; i < n; i++) {
            if (!read_vec3(p, &m->normals[i])) return 0;
            if (i < n - 1) {
                if (!expect(p, T_COM, ",")) return 0;
            } else {
                if (!expect(p, T_SEM, ";")) return 0;   /* trailing ;; */
            }
        }
    }

    int32_t nf;
    if (!eat_int32(p, &nf) || !expect(p, T_SEM, ";")) return 0;
    if (nf > 0) {
        m->face_normals = (xfile_face *)calloc((size_t)nf, sizeof *m->face_normals);
        if (!m->face_normals) { set_error(p->out, 0, "oom face_normals"); return 0; }
        for (int32_t i = 0; i < nf; i++) {
            if (!read_mesh_face(p, &m->face_normals[i])) return 0;
            if (i < nf - 1) {
                if (!expect(p, T_COM, ",")) return 0;
            } else {
                /* Already consumed the ; inside read_mesh_face; trailing ;; means a second ; here. */
                if (!expect(p, T_SEM, ";")) return 0;
            }
        }
    }
    /* trailing unknowns */
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind == T_IDENT) {
            advance(p);
            char ignored[XFILE_NAME_MAX];
            read_optional_instance_name(p, ignored, sizeof ignored);
            skip_optional_uuid(p);
            if (peek(p)->kind == T_LBR) {
                advance(p); skip_block_body(p);
                if (!expect(p, T_RBR, "}")) return 0;
            }
        } else advance(p);
    }
    return 1;
}

static int parse_mesh_texture_coords(P *p, xfile_mesh *m)
{
    skip_optional_uuid(p);
    int32_t n;
    if (!eat_int32(p, &n) || !expect(p, T_SEM, ";")) return 0;
    m->uv_count = n;
    if (n > 0) {
        m->uvs = (xfile_vec2 *)calloc((size_t)n, sizeof *m->uvs);
        if (!m->uvs) { set_error(p->out, 0, "oom uvs"); return 0; }
        for (int32_t i = 0; i < n; i++) {
            if (!read_vec2(p, &m->uvs[i])) return 0;
            if (i < n - 1) {
                if (!expect(p, T_COM, ",")) return 0;
            } else {
                if (!expect(p, T_SEM, ";")) return 0;
            }
        }
    }
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind == T_IDENT) {
            advance(p);
            char ignored[XFILE_NAME_MAX];
            read_optional_instance_name(p, ignored, sizeof ignored);
            skip_optional_uuid(p);
            if (peek(p)->kind == T_LBR) {
                advance(p); skip_block_body(p);
                if (!expect(p, T_RBR, "}")) return 0;
            }
        } else advance(p);
    }
    return 1;
}

static int parse_mesh_vertex_colors(P *p, xfile_mesh *m)
{
    /* docs/formats/xfile.md "quirk 1": exporter polymorphism between
     * items. Each record is:
     *   <vertex_index>; r;g;b;a;;
     * with the inter-record separator being either ';' or ','.
     *
     * Storage is sized at vertex_count (not the on-disk header N) so
     * the consumer can look up colours by vertex-index directly.
     * Vertices not touched by the block default to opaque white. */
    skip_optional_uuid(p);
    int32_t n;
    if (!eat_int32(p, &n) || !expect(p, T_SEM, ";")) return 0;
    m->vertex_color_count = n;

    if (m->vertex_count > 0 && m->vertex_colors == NULL) {
        m->vertex_colors = (xfile_rgba *)calloc((size_t)m->vertex_count,
                                                sizeof *m->vertex_colors);
        if (!m->vertex_colors) {
            set_error(p->out, 0, "oom vertex_colors");
            return 0;
        }
        for (int32_t i = 0; i < m->vertex_count; i++) {
            m->vertex_colors[i].r = 1.0f;
            m->vertex_colors[i].g = 1.0f;
            m->vertex_colors[i].b = 1.0f;
            m->vertex_colors[i].a = 1.0f;
        }
    }

    for (int32_t i = 0; i < n; i++) {
        int32_t idx;
        xfile_rgba color;
        if (!eat_int32(p, &idx) || !expect(p, T_SEM, ";")) return 0;
        if (!read_rgba(p, &color)) return 0;
        if (m->vertex_colors && idx >= 0 && idx < m->vertex_count) {
            m->vertex_colors[idx] = color;
        }
        /* drain any trailing SEMIs (the second ; of ;; styles) */
        while (peek(p)->kind == T_SEM) advance(p);
        /* optional inter-item COMMA */
        if (peek(p)->kind == T_COM) advance(p);
    }
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind == T_IDENT) {
            advance(p);
            char ignored[XFILE_NAME_MAX];
            read_optional_instance_name(p, ignored, sizeof ignored);
            skip_optional_uuid(p);
            if (peek(p)->kind == T_LBR) {
                advance(p); skip_block_body(p);
                if (!expect(p, T_RBR, "}")) return 0;
            }
        } else advance(p);
    }
    return 1;
}

static int parse_mesh_material_list(P *p, xfile_mesh *m,
                                    xfile_material **gmats, int32_t *ngmats, size_t *gmats_cap,
                                    size_t *texcap)
{
    skip_optional_uuid(p);
    int32_t nmat, nfi;
    if (!eat_int32(p, &nmat) || !expect(p, T_SEM, ";")) return 0;
    if (!eat_int32(p, &nfi)  || !expect(p, T_SEM, ";")) return 0;
    m->material_count       = nmat;
    m->face_material_count  = nfi;

    /* docs/formats/xfile.md "quirk 2": array terminator variance. Read N
     * values; each is followed by EITHER ; OR ,. After the last, an
     * optional second ;. */
    if (nfi > 0) {
        m->face_material_indexes = (int32_t *)calloc((size_t)nfi, sizeof *m->face_material_indexes);
        if (!m->face_material_indexes) { set_error(p->out, 0, "oom fmi"); return 0; }
        for (int32_t i = 0; i < nfi; i++) {
            if (!eat_int32(p, &m->face_material_indexes[i])) return 0;
            const xtok *sep = advance(p);
            if (sep->kind != T_SEM && sep->kind != T_COM) {
                set_error(p->out, sep->line, "expected ; or , in face_material_indexes");
                return 0;
            }
        }
        if (peek(p)->kind == T_SEM) advance(p);
    } else {
        advance(p);  /* at least one terminator */
        if (peek(p)->kind == T_SEM) advance(p);
    }

    /* Loop over reference blocks (`{Name}`) and inline Materials. */
    size_t refs_cap = 0, inl_cap = 0;
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind == T_LBR) {
            /* reference block — concat all tokens to a name (handles
             * hyphen-stitch quirk #4 since the - drops out and the IDENTs
             * concatenate naturally). */
            advance(p);
            char namebuf[XFILE_NAME_MAX];
            size_t used = 0;
            namebuf[0] = '\0';
            while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
                const xtok *tt = advance(p);
                if (tt->kind == T_IDENT) {
                    size_t add = tt->slen;
                    if (used + add >= sizeof namebuf) add = sizeof namebuf - 1 - used;
                    memcpy(namebuf + used, tt->str, add);
                    used += add;
                    namebuf[used] = '\0';
                }
            }
            if (!expect(p, T_RBR, "}")) return 0;

            if (!grow_array((void **)&m->material_refs, &refs_cap,
                            (size_t)m->material_ref_count + 1, sizeof *m->material_refs))
                { set_error(p->out, 0, "oom mat refs"); return 0; }
            strncpy(m->material_refs[m->material_ref_count], namebuf, XFILE_NAME_MAX - 1);
            m->material_refs[m->material_ref_count][XFILE_NAME_MAX - 1] = '\0';
            m->material_ref_count++;

        } else if (t->kind == T_IDENT
                   && t->slen == strlen("Material")
                   && memcmp(t->str, "Material", t->slen) == 0) {
            advance(p);
            char iname[XFILE_NAME_MAX] = {0};
            read_optional_instance_name(p, iname, sizeof iname);
            skip_optional_uuid(p);
            if (!expect(p, T_LBR, "{")) return 0;

            if (!grow_array((void **)&m->inline_materials, &inl_cap,
                            (size_t)m->inline_material_count + 1, sizeof *m->inline_materials))
                { set_error(p->out, 0, "oom inl mats"); return 0; }
            xfile_material *mat = &m->inline_materials[m->inline_material_count];
            memset(mat, 0, sizeof *mat);
            strncpy(mat->name, iname, XFILE_NAME_MAX - 1);
            if (!parse_material_body(p, mat)) return 0;
            if (!expect(p, T_RBR, "}")) return 0;
            m->inline_material_count++;

            record_texture(p->out, mat->texture, texcap);

            /* named inline materials are also added to the global pool */
            if (iname[0]) {
                if (!grow_array((void **)gmats, gmats_cap,
                                (size_t)(*ngmats) + 1, sizeof **gmats))
                    { set_error(p->out, 0, "oom gmats"); return 0; }
                memcpy(&(*gmats)[*ngmats], mat, sizeof *mat);
                (*ngmats)++;
            }
        } else if (t->kind == T_IDENT) {
            /* unknown nested */
            advance(p);
            char ignored[XFILE_NAME_MAX];
            read_optional_instance_name(p, ignored, sizeof ignored);
            skip_optional_uuid(p);
            if (peek(p)->kind == T_LBR) {
                advance(p); skip_block_body(p);
                if (!expect(p, T_RBR, "}")) return 0;
            }
        } else {
            advance(p);
        }
    }
    return 1;
}

static int parse_mesh_body(P *p, const char *name, const char *frame_path,
                           xfile_mesh **out_meshes, int32_t *out_n, size_t *out_cap,
                           xfile_material **gmats, int32_t *ngmats, size_t *gmats_cap,
                           size_t *texcap)
{
    skip_optional_uuid(p);

    if (!grow_array((void **)out_meshes, out_cap, (size_t)(*out_n) + 1, sizeof **out_meshes))
        { set_error(p->out, 0, "oom meshes"); return 0; }
    xfile_mesh *m = &(*out_meshes)[*out_n];
    memset(m, 0, sizeof *m);
    if (name) strncpy(m->name, name, XFILE_NAME_MAX - 1);
    if (frame_path) strncpy(m->frame_path, frame_path, sizeof m->frame_path - 1);
    (*out_n)++;

    int32_t nv;
    if (!eat_int32(p, &nv) || !expect(p, T_SEM, ";")) return 0;
    m->vertex_count = nv;
    if (nv > 0) {
        m->vertices = (xfile_vec3 *)calloc((size_t)nv, sizeof *m->vertices);
        if (!m->vertices) { set_error(p->out, 0, "oom verts"); return 0; }
        for (int32_t i = 0; i < nv; i++) {
            if (!read_vec3(p, &m->vertices[i])) return 0;
            if (i < nv - 1) {
                if (!expect(p, T_COM, ",")) return 0;
            } else {
                if (!expect(p, T_SEM, ";")) return 0;
            }
        }
    }

    int32_t nf;
    if (!eat_int32(p, &nf) || !expect(p, T_SEM, ";")) return 0;
    m->face_count = nf;
    if (nf > 0) {
        m->faces = (xfile_face *)calloc((size_t)nf, sizeof *m->faces);
        if (!m->faces) { set_error(p->out, 0, "oom faces"); return 0; }
        for (int32_t i = 0; i < nf; i++) {
            if (!read_mesh_face(p, &m->faces[i])) return 0;
            if (i < nf - 1) {
                if (!expect(p, T_COM, ",")) return 0;
            } else {
                if (!expect(p, T_SEM, ";")) return 0;
            }
        }
    }

    /* nested templates */
    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind != T_IDENT) { advance(p); continue; }

        const char *tname = t->str;
        size_t tlen = t->slen;
        advance(p);
        char iname[XFILE_NAME_MAX] = {0};
        read_optional_instance_name(p, iname, sizeof iname);
        skip_optional_uuid(p);
        if (peek(p)->kind != T_LBR) {
            /* declaration without body — odd, skip */
            continue;
        }
        advance(p);  /* consume LBR */

        #define IS(tag) (tlen == strlen(tag) && memcmp(tname, tag, tlen) == 0)
        if (IS("MeshNormals")) {
            if (!parse_mesh_normals(p, m)) return 0;
        } else if (IS("MeshTextureCoords")) {
            if (!parse_mesh_texture_coords(p, m)) return 0;
        } else if (IS("MeshMaterialList")) {
            if (!parse_mesh_material_list(p, m, gmats, ngmats, gmats_cap, texcap)) return 0;
        } else if (IS("MeshVertexColors")) {
            if (!parse_mesh_vertex_colors(p, m)) return 0;
        } else {
            skip_block_body(p);
        }
        #undef IS
        if (!expect(p, T_RBR, "}")) return 0;
    }
    return 1;
}

static int parse_frame_body(P *p, const char *frame_name, const char *parent_path,
                            xfile_mesh **meshes, int32_t *nmeshes, size_t *meshes_cap,
                            xfile_frame **frames, int32_t *nframes, size_t *frames_cap,
                            xfile_material **gmats, int32_t *ngmats, size_t *gmats_cap,
                            size_t *texcap)
{
    skip_optional_uuid(p);

    if (!grow_array((void **)frames, frames_cap, (size_t)(*nframes) + 1, sizeof **frames))
        { set_error(p->out, 0, "oom frames"); return 0; }
    xfile_frame *fr = &(*frames)[*nframes];
    memset(fr, 0, sizeof *fr);
    strncpy(fr->name, frame_name, XFILE_NAME_MAX - 1);
    /* identity transform */
    for (int i = 0; i < 16; i++) fr->transform[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    int32_t my_idx = *nframes;
    (*nframes)++;

    /* Build child path: parent_path + "/" + frame_name. */
    char child_path[256];
    if (parent_path && parent_path[0]) {
        snprintf(child_path, sizeof child_path, "%s/%s", parent_path, frame_name);
    } else {
        snprintf(child_path, sizeof child_path, "%s", frame_name);
    }

    int32_t local_child_count = 0;
    int32_t local_mesh_count  = 0;
    size_t  children_cap = 0;

    while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) {
        const xtok *t = peek(p);
        if (t->kind != T_IDENT) { advance(p); continue; }

        const char *tname = t->str;
        size_t tlen = t->slen;
        advance(p);
        char iname[XFILE_NAME_MAX] = {0};
        read_optional_instance_name(p, iname, sizeof iname);
        skip_optional_uuid(p);
        if (peek(p)->kind != T_LBR) continue;
        advance(p);

        #define IS(tag) (tlen == strlen(tag) && memcmp(tname, tag, tlen) == 0)
        if (IS("FrameTransformMatrix")) {
            skip_optional_uuid(p);
            if (!read_matrix16(p, (*frames)[my_idx].transform)) return 0;
            (*frames)[my_idx].has_transform = 1;
            /* drain stray */
            while (peek(p)->kind != T_RBR && peek(p)->kind != T_EOF) advance(p);
        } else if (IS("Frame")) {
            /* recurse */
            if (!grow_array((void **)&(*frames)[my_idx].children_names, &children_cap,
                            (size_t)local_child_count + 1,
                            sizeof *(*frames)[my_idx].children_names))
                { set_error(p->out, 0, "oom kids"); return 0; }
            strncpy((*frames)[my_idx].children_names[local_child_count], iname,
                    XFILE_NAME_MAX - 1);
            (*frames)[my_idx].children_names[local_child_count][XFILE_NAME_MAX - 1] = '\0';
            local_child_count++;
            if (!parse_frame_body(p, iname, child_path,
                                  meshes, nmeshes, meshes_cap,
                                  frames, nframes, frames_cap,
                                  gmats, ngmats, gmats_cap, texcap)) return 0;
        } else if (IS("Mesh")) {
            if (!parse_mesh_body(p, iname, child_path,
                                 meshes, nmeshes, meshes_cap,
                                 gmats, ngmats, gmats_cap, texcap)) return 0;
            local_mesh_count++;
        } else {
            skip_block_body(p);
        }
        #undef IS
        if (!expect(p, T_RBR, "}")) return 0;
    }
    (*frames)[my_idx].child_count = local_child_count;
    (*frames)[my_idx].mesh_count  = local_mesh_count;
    return 1;
}

/* ───── Top-level driver ──────────────────────────────────────────────── */

static int parse_header(xfile_t *x, const char *data, size_t len)
{
    if (len < 16) {
        snprintf(x->error, sizeof x->error, "%.80s: file too short for xof header",
                 x->path[0] ? x->path : "<mem>");
        return 0;
    }
    if (memcmp(data, "xof ", 4) != 0) {
        snprintf(x->error, sizeof x->error, "%.80s: not a .x file (bad magic)",
                 x->path[0] ? x->path : "<mem>");
        return 0;
    }
    memcpy(x->header_version,  data + 4, 4); x->header_version[4]  = '\0';
    memcpy(x->header_encoding, data + 8, 3); x->header_encoding[3] = '\0';
    char fs[5] = {data[12], data[13], data[14], data[15], '\0'};
    x->header_float_size = (int)strtol(fs, NULL, 10);
    if (memcmp(x->header_encoding, "txt", 3) != 0) {
        snprintf(x->error, sizeof x->error,
                 "%.80s: only txt-encoded .x supported (got %.3s)",
                 x->path[0] ? x->path : "<mem>", x->header_encoding);
        return 0;
    }
    return 1;
}

xfile_t *xfile_parse(const char *data, size_t len, const char *path_for_errors)
{
    xfile_t *x = (xfile_t *)calloc(1, sizeof *x);
    if (!x) return NULL;
    if (path_for_errors) {
        strncpy(x->path, path_for_errors, sizeof x->path - 1);
    }
    x->size_bytes = len;

    if (!parse_header(x, data, len)) return x;

    /* Stripped working copy of the source. Stays alive for the whole
     * parse since tokens hold pointers into it. We free it before
     * returning, after copying out everything the parser needs (names
     * land in xfile_t buffers via copy_str). */
    char *work = (char *)malloc(len);
    if (!work) { snprintf(x->error, sizeof x->error, "oom"); return x; }
    memcpy(work, data, len);
    strip_comments(work, len);

    xtok  *toks = NULL;
    size_t ntoks = 0;
    if (tokenize(work, len, &toks, &ntoks, x) < 0) {
        if (!x->error[0]) snprintf(x->error, sizeof x->error, "oom in tokenize");
        free(work);
        return x;
    }

    P p = { .out = x, .toks = toks, .ntoks = ntoks, .pos = 0 };

    /* Skip header tokens. They tokenize as IDENT 'xof', INT 0303,
     * IDENT 'txt', INT 0032 with no following LBR — handled by the
     * stray-IDENT path in the top loop. */

    /* Dynamic accumulators. */
    size_t meshes_cap = 0, frames_cap = 0, gmats_cap = 0, texcap = 0;

    while (peek(&p)->kind != T_EOF) {
        const xtok *t = peek(&p);

        if (t->kind == T_SEM || t->kind == T_COM || t->kind == T_UID
            || t->kind == T_INT || t->kind == T_FLT || t->kind == T_STR) {
            advance(&p);
            continue;
        }
        if (t->kind != T_IDENT) { advance(&p); continue; }

        const char *tname = t->str;
        size_t tlen = t->slen;
        advance(&p);

        /* template { ... } — type declaration; skip */
        if (tlen == strlen("template") && memcmp(tname, "template", tlen) == 0) {
            /* template Name <UUID> { ... } */
            if (peek(&p)->kind == T_IDENT) advance(&p);
            skip_optional_uuid(&p);
            if (peek(&p)->kind == T_LBR) {
                advance(&p);
                skip_block_body(&p);
                if (!expect(&p, T_RBR, "}")) goto done;
            }
            continue;
        }

        /* instance */
        char iname[XFILE_NAME_MAX] = {0};
        read_optional_instance_name(&p, iname, sizeof iname);
        skip_optional_uuid(&p);
        if (peek(&p)->kind != T_LBR) {
            /* stray identifier (e.g. xof header line tokens) — fine */
            continue;
        }
        advance(&p);  /* consume LBR */

        #define IS(tag) (tlen == strlen(tag) && memcmp(tname, tag, tlen) == 0)
        if (IS("Material")) {
            if (!grow_array((void **)&x->global_materials, &gmats_cap,
                            (size_t)x->global_material_count + 1, sizeof *x->global_materials))
                { set_error(x, 0, "oom gmats"); break; }
            xfile_material *mat = &x->global_materials[x->global_material_count];
            memset(mat, 0, sizeof *mat);
            strncpy(mat->name, iname, XFILE_NAME_MAX - 1);
            if (!parse_material_body(&p, mat)) goto done;
            x->global_material_count++;
            record_texture(x, mat->texture, &texcap);
        } else if (IS("Frame")) {
            if (!parse_frame_body(&p, iname, "",
                                  &x->meshes, &x->mesh_count, &meshes_cap,
                                  &x->frames, &x->frame_count, &frames_cap,
                                  &x->global_materials, &x->global_material_count, &gmats_cap,
                                  &texcap)) goto done;
        } else if (IS("Mesh")) {
            if (!parse_mesh_body(&p, iname, "",
                                 &x->meshes, &x->mesh_count, &meshes_cap,
                                 &x->global_materials, &x->global_material_count, &gmats_cap,
                                 &texcap)) goto done;
        } else if (IS("Header")) {
            skip_block_body(&p);
        } else {
            skip_block_body(&p);
        }
        #undef IS
        if (!expect(&p, T_RBR, "}")) break;
    }

done:
    /* Now we still need to record textures pulled in by global Materials
     * that came BEFORE we knew about the texture buffer growth helper —
     * but record_texture is called at material parse time, so we're
     * already correct. Same for inline materials inside MeshMaterialList. */

    free(toks);
    free(work);
    return x;
}

void xfile_free(xfile_t *x)
{
    if (!x) return;
    for (int32_t i = 0; i < x->mesh_count; i++) {
        xfile_mesh *m = &x->meshes[i];
        free(m->vertices);
        free(m->faces);
        free(m->normals);
        free(m->face_normals);
        free(m->uvs);
        free(m->vertex_colors);
        free(m->face_material_indexes);
        free(m->material_refs);
        free(m->inline_materials);
    }
    free(x->meshes);
    for (int32_t i = 0; i < x->frame_count; i++) {
        free(x->frames[i].children_names);
    }
    free(x->frames);
    free(x->global_materials);
    free(x->textures);
    free(x);
}
