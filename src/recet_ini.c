/*
 * recet_ini.c — `recet.ini` reader.
 *
 * Source: FUN_0047a474 in docs/decompiled/by-address/47a474.c. The
 * engine fires 33 GetPrivateProfileIntA calls — 28 input bindings
 * (2 controllers × (9 pad + 5 skill)) + 5 misc — followed by a
 * `screen` switch into a (width,height) lookup. Our port mirrors
 * the same key set + defaults but uses an in-process parser instead
 * of the Win32 API, so it can run under ASan in unit tests.
 *
 * The only behavioural divergence from GetPrivateProfileIntA is that
 * we don't honour the `0x` / `0` prefix → hex/octal parsing trick (Win32
 * does; engine never uses it — every shipping recet.ini value is plain
 * base-10 small int). If the engine ever grew a hex value here we'd
 * need to teach `parse_signed_int` to use strtol with base 0.
 */

#include "recet_ini.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

/* ─── default tables (from the unpacked binary) ─────────────────────────────
 * pad bytes live at 0x005c81d8, skill bytes at 0x005c8204. The engine reads
 * `byte + 1` from each — capturing the +1 here makes the defaults match the
 * ini-shipped values byte-for-byte (e.g. pad04 default = 0x26+1 = 39).
 * Stride padding bytes (0x0d,0x1f for pad; none for skill) are dropped:
 * the engine indexes them by inner-loop offset only, so they never appear
 * as a default. */
const int16_t recet_ini_pad_defaults[RECET_INI_CONTROLLERS][RECET_INI_PAD_KEYS] = {
    {  1,  2,  3,  4, 39, 37, 16, 35, 36 },  /* bytes 00,01,02,03,26,24,0f,22,23 +1 */
    { 40, 41, 42, 43, 44, 45, 46, 47, 48 },  /* bytes 27..2f +1 */
};
const int16_t recet_ini_skill_defaults[RECET_INI_CONTROLLERS][RECET_INI_SKILL_KEYS] = {
    { 0, 0, 0, 0, 0 },                        /* byte 0xff + 1 truncated to int16_t = 0 */
    { 0, 0, 0, 0, 0 },
};

void recet_ini_resolution(int screen, int *width, int *height)
{
    switch (screen) {
    case 0: *width = 640;  *height = 480; break;
    case 1: *width = 800;  *height = 600; break;
    case 2: *width = 1024; *height = 768; break;
    default: *width = 1280; *height = 960; break;
    }
}

void recet_ini_set_defaults(struct recet_ini *out)
{
    memset(out, 0, sizeof *out);
    memcpy(out->pad,   recet_ini_pad_defaults,   sizeof out->pad);
    memcpy(out->skill, recet_ini_skill_defaults, sizeof out->skill);
    out->aspect  = 1;
    out->winmode = 1;
    out->se      = 9;
    out->mu      = 9;
    /* `screen` defaults to 0 (640×480) — but the engine's switch falls
     * through to the (1280×960) "else" branch when the key is missing,
     * because GetPrivateProfileIntA with default 0 returns 0 and the
     * switch hits `case 0`. So default 0 → 640×480. Confirmed by tracing
     * the decomp at lines 77724..77734. */
    recet_ini_resolution(0, &out->width, &out->height);
}

/* ─── tiny INI parser ───────────────────────────────────────────────────────
 * Section headers: `[name]`. Keys: `key=value`. Comments start with `;` or `#`
 * at column 0 (matches GetPrivateProfile* behaviour). Whitespace around `=`
 * and around values is trimmed. We don't expand environment variables or
 * support multi-line values — neither does Win32's INI API for our flags. */

static int ascii_tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ascii_streq_nocase(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b))
            return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int parse_signed_int(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return atoi(s);
}

/* Trim trailing whitespace + CR. Modifies in place. */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r')) {
        s[--n] = 0;
    }
}

/* Skip leading whitespace; return pointer into s. */
static char *ltrim(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/*
 * Visitor callback: invoked once per key=value pair encountered.
 * `section` and `key` are NUL-terminated, possibly empty. `val_int` is
 * pre-parsed via atoi; `val_str` is the raw NUL-terminated value (for
 * future string keys — none in FUN_0047a474, but plenty in the engine's
 * other ini loaders).
 */
typedef void (*ini_visitor)(const char *section, const char *key,
                            int val_int, const char *val_str,
                            void *user);

/* Iterate over every key=value pair in [text..text+len). Section tracking
 * is sticky between pairs. Lines without `=` (other than `[section]`
 * headers and comments) are skipped silently — same as Win32. */
static void ini_iter(const char *text, size_t len, ini_visitor visit, void *user)
{
    char section[64] = {0};
    char line[1024];

    size_t pos = 0;
    while (pos < len) {
        /* Read one line into `line` (truncate if longer than buffer; harmless
         * since recet.ini values are all small). */
        size_t llen = 0;
        while (pos < len && text[pos] != '\n') {
            if (llen + 1 < sizeof line) line[llen++] = text[pos];
            pos++;
        }
        if (pos < len) pos++; /* consume \n */
        line[llen] = 0;
        rtrim(line);

        char *p = ltrim(line);
        if (*p == 0 || *p == ';' || *p == '#') continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (!end) continue;
            *end = 0;
            size_t slen = (size_t)(end - p - 1);
            if (slen >= sizeof section) slen = sizeof section - 1;
            memcpy(section, p + 1, slen);
            section[slen] = 0;
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = p;
        char *val = eq + 1;
        rtrim(key);
        val = ltrim(val);

        visit(section, key, parse_signed_int(val), val, user);
    }
}

/* ─── dispatcher table ──────────────────────────────────────────────────────
 * One row per scalar GetPrivateProfileIntA call in FUN_0047a474. The pad/skill
 * grid is handled out-of-band because it uses formatted key names. */

struct field_row {
    const char *section;
    const char *key;
    size_t      offset;     /* byte offset into struct recet_ini */
    int         clamp_max;  /* -1 if no upper clamp */
};

#define FIELD(sec, k, member)        { sec, k, offsetof(struct recet_ini, member), -1 }
#define FIELD_CLAMPED(sec, k, member, hi) { sec, k, offsetof(struct recet_ini, member), hi }

static const struct field_row g_field_rows[] = {
    /* Order copies FUN_0047a474 verbatim. */
    FIELD("setup", "aspect",       aspect),
    FIELD("debug", "camfree",      camfree),  /* read twice in engine; we read once */
    FIELD("setup", "winmode",      winmode),
    FIELD("setup", "fps",          fps),
    FIELD("setup", "dispfps",      dispfps),
    FIELD("setup", "sfnouse",      sfnouse),
    FIELD("setup", "texmode",      texmode),
    FIELD("setup", "mapmode",      mapmode),
    FIELD("setup", "demomode",     demomode),
    FIELD("setup", "usemipmap",    usemipmap),
    FIELD("setup", "usetree",      usetree),
    FIELD("setup", "uselighttex",  uselighttex),
    FIELD("setup", "texlevel",     texlevel),
    FIELD("setup", "toorioff",     toorioff),
    FIELD("setup", "windowpos",    windowpos),
    FIELD("setup", "winx",         winx),
    FIELD("setup", "winy",         winy),
    FIELD("setup", "nolight",      nolight),
    FIELD("setup", "nolight_s",    nolight_s),
    FIELD("setup", "easydisp",     easydisp),
    FIELD("setup", "s_easydisp",   s_easydisp),
    FIELD("setup", "usefog",       usefog),
    FIELD("setup", "screen",       screen),
    FIELD_CLAMPED("config", "se",  se, 9),
    FIELD_CLAMPED("config", "mu",  mu, 9),
};

struct parse_ctx {
    struct recet_ini *out;
};

static void visit(const char *section, const char *key,
                  int val_int, const char *val_str, void *user)
{
    (void)val_str;
    struct parse_ctx *ctx = user;

    /* Input bindings first — formatted key match. */
    if (ascii_streq_nocase(section, "option")
        && (key[0] == 'p' || key[0] == 's')) {
        if (key[0] == 'p'
            && key[1] == 'a' && key[2] == 'd'
            && key[3] >= '0' && key[3] <= '0' + RECET_INI_CONTROLLERS - 1
            && key[4] >= '0' && key[4] <= '0' + RECET_INI_PAD_KEYS - 1
            && key[5] == 0)
        {
            ctx->out->pad[key[3] - '0'][key[4] - '0'] = (int16_t)val_int;
            return;
        }
        if (key[0] == 's' && key[1] == 'k' && key[2] == 'i' && key[3] == 'l' && key[4] == 'l'
            && key[5] >= '0' && key[5] <= '0' + RECET_INI_CONTROLLERS - 1
            && key[6] >= '0' && key[6] <= '0' + RECET_INI_SKILL_KEYS - 1
            && key[7] == 0)
        {
            ctx->out->skill[key[5] - '0'][key[6] - '0'] = (int16_t)val_int;
            return;
        }
    }

    /* Scalars. */
    for (size_t i = 0; i < sizeof g_field_rows / sizeof g_field_rows[0]; i++) {
        const struct field_row *r = &g_field_rows[i];
        if (!ascii_streq_nocase(section, r->section)) continue;
        if (!ascii_streq_nocase(key,     r->key))     continue;
        int v = val_int;
        if (r->clamp_max >= 0) {
            if (v > r->clamp_max) v = r->clamp_max;
            if (v < 0)            v = 0;
        }
        int *target = (int *)((char *)ctx->out + r->offset);
        *target = v;
        return;
    }
    /* Unknown key/section: silently ignored (matches Win32). */
}

int recet_ini_parse(const char *text, size_t len, struct recet_ini *out)
{
    recet_ini_set_defaults(out);

    struct parse_ctx ctx = { .out = out };
    ini_iter(text, len, visit, &ctx);

    /* Post-loop fixups, in engine order. */
    out->bgnodisp = out->easydisp;       /* DAT_0438b18c = DAT_0438b19c */
    recet_ini_resolution(out->screen, &out->width, &out->height);
    return 0;
}

#ifdef _WIN32

int recet_ini_default_path(char *buf, size_t buf_size)
{
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)buf_size);
    if (n == 0 || n >= buf_size) return -1;
    char *tail = strrchr(buf, '\\');
    if (!tail) tail = strrchr(buf, '/');
    if (!tail) return -1;
    static const char *const fname = "recet.ini";
    size_t need = (size_t)(tail - buf) + 1 + strlen(fname) + 1;
    if (need > buf_size) return -1;
    strcpy(tail + 1, fname);
    return 0;
}

int recet_ini_load(const char *path, struct recet_ini *out)
{
    /* Missing/unreadable file → silent defaults (matches Win32 behavior). */
    FILE *f = fopen(path, "rb");
    if (!f) {
        recet_ini_set_defaults(out);
        out->bgnodisp = out->easydisp;
        recet_ini_resolution(out->screen, &out->width, &out->height);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) sz = 0;
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }

    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = 0;

    int rc = recet_ini_parse(buf, got, out);
    free(buf);
    return rc;
}

#endif /* _WIN32 */
