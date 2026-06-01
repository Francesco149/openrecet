/*
 * scene1_dialogue.c — opening-prologue dialogue interpreter. See the header.
 *
 * This file ports FUN_0046ddea (the .ivt script compiler) + the FUN_0046c295
 * loader core. The runtime (FUN_0046c320 update / FUN_0046c9a2 draw / the
 * 0x46d8xx–0x46ddxx command handlers + the TEXT_ANIM anchors) lands next; its
 * handler bodies are computed-call-only stubs not present in the decompiled C,
 * so they need a raw-disassembly pass.
 */
#include "scene1_dialogue.h"

#include <stdlib.h>
#include <string.h>

/* ── small parse helpers (mirror the engine's FUN_00479f4d / FUN_00503d03) ── */

/* prefix match: nonzero iff `line` begins with the first `n` bytes of `kw`. */
static int kw(const char *line, const char *k, int n)
{
    return strncmp(line, k, (size_t)n) == 0;
}

/* atoi over a leading optional sign + decimal digits (engine FUN_00503d03). */
static int ive_atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

/* atof → truncate-to-int (engine FUN_00503c2b + __ftol). */
static int ive_atof_i(const char *s)
{
    return (int)strtod(s, NULL);
}

/* Advance past the next byte `c`; returns NULL if EOL/EOS hit first. The
 * engine's `for(;*p!=c;p++){}` relies on a '\r' terminator being present;
 * we additionally stop at '\0'/'\n' so a malformed line cannot run off. */
static const char *past(const char *p, char c)
{
    for (; *p && *p != '\n' && *p != '\r'; p++)
        if (*p == c) return p + 1;
    return NULL;
}

/* Pack r,g,b,a → ARGB the way the engine does:
 * ((a<<8 | r)<<8 | g)<<8 | b  ==  a<<24 | r<<16 | g<<8 | b. */
static int32_t pack_argb(int r, int g, int b, int a)
{
    return (int32_t)(((uint32_t)(a & 0xff) << 24) | ((uint32_t)(r & 0xff) << 16) |
                     ((uint32_t)(g & 0xff) << 8) | (uint32_t)(b & 0xff));
}

/* Append a command; flags overflow if the table is full. */
static void emit(struct ive_program *p, enum ive_op op, int32_t a1, int32_t a2)
{
    if (p->n_cmds >= IVE_MAX_CMDS) { p->overflow = 1; return; }
    p->cmds[p->n_cmds].op = (uint8_t)op;
    p->cmds[p->n_cmds].a1 = a1;
    p->cmds[p->n_cmds].a2 = a2;
    p->n_cmds++;
}

/* Copy a path token (until CR/LF/EOS) into a fixed name slot. */
static void copy_name(char dst[IVE_NAME_BYTES], const char *src)
{
    int i = 0;
    while (i < IVE_NAME_BYTES - 1 && src[i] && src[i] != '\r' && src[i] != '\n') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ── chr:N:<sub> sub-dispatch (line points at "chr:N:..."; sub at line+6) ── */
static void parse_chr(struct ive_program *p, const char *line)
{
    int n = ive_atoi(line + 4);      /* chr index (single digit at +4) */
    const char *sub = line + 6;      /* after "chr:N:" */
    const char *arg;

    if (kw(sub, "dir:", 4)) {
        /* "left" → 0, anything else ("right") → 1. */
        emit(p, IVE_OP_CHR_DIR, n, kw(line + 10, "left", 4) ? 0 : 1);
    } else if (kw(sub, "move:", 5)) {
        /* move:x,y → two commands (x then y), engine 0x46da33 + 0x46dc0a. */
        emit(p, IVE_OP_CHR_MOVE_X, n, ive_atoi(line + 11));
        arg = past(line + 11, ',');
        emit(p, IVE_OP_CHR_MOVE_Y, n, arg ? ive_atoi(arg) : 0);
    } else if (kw(sub, "moveto:", 7)) {
        emit(p, IVE_OP_CHR_MOVETO_X, n, ive_atoi(line + 13));
        arg = past(line + 13, ',');
        emit(p, IVE_OP_CHR_MOVETO_Y, n, arg ? ive_atoi(arg) : 0);
    } else if (kw(sub, "center:", 7)) {
        emit(p, IVE_OP_CHR_CENTER, n, ive_atoi(line + 13));
    } else if (kw(sub, "speed:", 6)) {
        emit(p, IVE_OP_CHR_SPEED, n, ive_atof_i(line + 12));
    } else if (kw(sub, "anim:", 5)) {
        emit(p, IVE_OP_CHR_ANIM, n, ive_atoi(line + 11));
    } else if (kw(sub, "fadeframe:", 10)) {
        emit(p, IVE_OP_CHR_FADEFRAME, n, ive_atoi(line + 16));
    } else if (kw(sub, "normal_shade", 12)) {
        emit(p, IVE_OP_CHR_BLEND, n, 0);
    } else if (kw(sub, "normal_add", 10)) {
        emit(p, IVE_OP_CHR_BLEND, n, 1);
    } else if (kw(sub, "add_shade", 9)) {
        emit(p, IVE_OP_CHR_BLEND, n, 2);
    } else if (kw(sub, "add_add", 7) || kw(sub, "add", 3)) {
        emit(p, IVE_OP_CHR_BLEND, n, 3);
    } else if (kw(sub, "colto:", 6)) {
        const char *c = sub + 6;
        int r = ive_atoi(c), g = 0, b = 0, a = 0;
        if ((c = past(c, ','))) { g = ive_atoi(c); if ((c = past(c, ','))) { b = ive_atoi(c);
            if ((c = past(c, ','))) a = ive_atoi(c); } }
        emit(p, IVE_OP_CHR_COLTO, n, pack_argb(r, g, b, a));
    } else if (kw(sub, "col:", 4)) {
        const char *c = sub + 4;
        int r = ive_atoi(c), g = 0, b = 0, a = 0;
        if ((c = past(c, ','))) { g = ive_atoi(c); if ((c = past(c, ','))) { b = ive_atoi(c);
            if ((c = past(c, ','))) a = ive_atoi(c); } }
        emit(p, IVE_OP_CHR_COL, n, pack_argb(r, g, b, a));
    } else if (kw(sub, "grp:", 4)) {
        /* chr:N:grp:<tga> W,H — register the named graphic + dims, emit the
         * graphic-set command (engine shares 0x46dc97 with anim). a2 = the
         * chrname slot index. */
        int idx = p->n_chrname;
        if (idx < IVE_MAX_NAMES) {
            copy_name(p->chrname[idx], sub + 4);
            /* dims trail the path after a space: "...tga W,H" */
            const char *sp = sub + 4;
            while (*sp && *sp != ' ' && *sp != '\r' && *sp != '\n') sp++;
            /* The name ends at the space (the engine's name copy stops there,
             * all.c:68536 `if (cVar7 == ' ') break`). copy_name only stops at
             * CR/LF, so strip the trailing " W,H" it pulled in — else
             * sprite_load gets "tear.tga 128,256" and the standee never loads. */
            int namelen = (int)(sp - (sub + 4));
            if (namelen >= 0 && namelen < IVE_NAME_BYTES)
                p->chrname[idx][namelen] = '\0';
            p->chr_w[idx] = (*sp == ' ') ? ive_atoi(sp + 1) : 0;
            const char *comma = (*sp == ' ') ? past(sp + 1, ',') : NULL;
            p->chr_h[idx] = comma ? ive_atoi(comma) : 0;
            p->n_chrname++;
        } else {
            p->overflow = 1;
        }
        emit(p, IVE_OP_CHR_ANIM, n, idx);
    } else if (kw(sub, "disp", 4)) {
        /* Engine emits a2=1 (LAB_0046efd4): the disp handler 0x46da09 writes
         * standees[N].field[11] = a2, which the draw's active gate checks. (The
         * old a2=0 stub left every standee inactive — fine while the visual was
         * deferred, wrong now that standees render.) */
        emit(p, IVE_OP_CHR_DISP, n, 1);
    }
    /* else: unrecognised chr sub-op — engine would MessageBox; we drop it. */
}

/* ── msg:a:b:<text> — the dialogue beat (engine lines 671-751) ──
 * Writes the text rows into prog->glyph (one row per <BR> segment) and emits
 * MSG_SPEAKER + MSG_SHOW(row_start,row_count) [+ MSG_WAITKEY] [+ MSG_CLEAR]
 * [+ WAIT(10) for <W>]. Each msg = one reveal cycle = one TEXT_ANIM pair. */
static void parse_msg(struct ive_program *p, const char *line)
{
    int a = ive_atoi(line + 4);            /* speaker arg a (digit at +4) */
    int b = ive_atoi(line + 6);            /* speaker arg b (digit at +6) */
    const char *text = past(line + 6, ':');/* skip "msg:a:b:" → text start */
    int row_start = p->n_rows;
    int row = row_start;
    int col = 0;
    int br_count = 0;
    int waitkey = 0, clear = 0, wflag = 0;

    if (!text) text = "";

    if (*text != '\r' && *text != '\n' && *text != '\0') {
        const char *q = text;
        while (*q && *q != '\r' && *q != '\n') {
            if (*q == '<') {
                if (q[1] == 'B' && q[2] == 'R' && q[3] == '>') {
                    row++; col = 0; br_count++; q += 4;
                    continue;
                } else if (q[1] == 'K' && q[2] == 'E' && q[3] == 'Y' && q[4] == '>') {
                    waitkey = 1; q += 5;
                    continue;
                } else if (q[1] == 'W' && q[2] == '>') {
                    wflag = 10; q += 3;
                    continue;
                } else if (q[1] == 'C' && q[2] == '>') {
                    clear = 1; q += 3;
                    continue;
                }
                /* unknown '<…': fall through and store the '<' as a glyph */
            }
            if (row >= 0 && row < IVE_MAX_ROWS && col < IVE_ROW_BYTES - 1)
                p->glyph[row][col++] = *q;
            else if (row >= IVE_MAX_ROWS)
                p->overflow = 1;
            q++;
        }
    }
    p->n_rows = row + 1;   /* engine local_20++ past the final row */

    emit(p, IVE_OP_MSG_SPEAKER, a, b);
    emit(p, IVE_OP_MSG_SHOW, row_start, br_count + 1);
    if (waitkey) emit(p, IVE_OP_MSG_WAITKEY, 0, 0);
    if (clear)   emit(p, IVE_OP_MSG_CLEAR, 0, 0);
    if (wflag)   emit(p, IVE_OP_WAIT, wflag, 0);
}

/* ── one script line → command(s) ── */
static void parse_line(struct ive_program *p, const char *line)
{
    /* Comment / blank / indented-continuation lines are skipped by the
     * caller (first char \r \n \t /). */
    const char *arg;

    if (kw(line, "color", 5)) {
        /* color:i:r,g,b — clear-colour. a1 = leading index, a2 = packed rgb. */
        int idx = ive_atoi(line + 5);
        const char *c = past(line + 6, ':');
        int r = 0, g = 0, b = 0;
        if (c) { r = ive_atoi(c);
            if ((c = past(c, ','))) { g = ive_atoi(c);
                if ((c = past(c, ','))) b = ive_atoi(c); } }
        emit(p, IVE_OP_COLOR, idx,
             (int32_t)(((uint32_t)(r & 0xff) << 16) | ((uint32_t)(g & 0xff) << 8) | (uint32_t)(b & 0xff)));
    } else if (kw(line, "bgscroll:", 9)) {
        emit(p, IVE_OP_BGSCROLL, ive_atof_i(line + 9), 0);
    } else if (kw(line, "rmb:", 4)) {
        /* rmb:a,b — screen-shake jitter (engine 0x46d926). Both args are
         * stored as atoi(arg)+1 (the engine's `iVar1 + 1`). */
        int a = ive_atoi(line + 4) + 1;
        arg = past(line + 4, ',');
        emit(p, IVE_OP_RMB, a, (arg ? ive_atoi(arg) : 0) + 1);
    } else if (kw(line, "bgset:", 6)) {
        if (p->n_bg < IVE_MAX_NAMES) copy_name(p->bg[p->n_bg], line + 6); else p->overflow = 1;
        emit(p, IVE_OP_BG, p->n_bg, 0);
        if (p->n_bg < IVE_MAX_NAMES) p->n_bg++;
    } else if (kw(line, "polybg:", 7)) {
        if (p->n_polybg < IVE_MAX_NAMES) copy_name(p->polybg[p->n_polybg], line + 7); else p->overflow = 1;
        emit(p, IVE_OP_BG, 0, 0);
        if (p->n_polybg < IVE_MAX_NAMES) p->n_polybg++;
    } else if (kw(line, "windowset", 9)) {
        emit(p, IVE_OP_WINDOWSET, 0, 0);
    } else if (kw(line, "windowpos:", 10)) {
        int x = ive_atoi(line + 10);
        arg = past(line + 10, ',');
        emit(p, IVE_OP_WINDOWPOS, x, arg ? ive_atoi(arg) : 0);
    } else if (kw(line, "skipoff", 7) && !kw(line, "skipon", 6)) {
        emit(p, IVE_OP_SKIP, 1, 0);
    } else if (kw(line, "skipon", 6)) {
        emit(p, IVE_OP_SKIP, 0, 0);
    } else if (kw(line, "fadeinb", 7) || kw(line, "fadeoutb", 8)) {
        /* recognised marker, emits no command (engine sets only local_8) */
    } else if (kw(line, "fadein:", 7)) {
        /* fadein:f:r,g,b,a → a2 = frames, a1 = packed argb. */
        int frames = ive_atoi(line + 7);
        const char *c = past(line + 7, ':');
        int r = 0, g = 0, b = 0, a = 0;
        if (c) { r = ive_atoi(c);
            if ((c = past(c, ','))) { g = ive_atoi(c);
                if ((c = past(c, ','))) { b = ive_atoi(c);
                    if ((c = past(c, ','))) a = ive_atoi(c); } } }
        emit(p, IVE_OP_FADEIN, pack_argb(r, g, b, a), frames);
    } else if (kw(line, "fadeout:", 8)) {
        int frames = ive_atoi(line + 8);
        const char *c = past(line + 8, ':');
        int r = 0, g = 0, b = 0, a = 0;
        if (c) { r = ive_atoi(c);
            if ((c = past(c, ','))) { g = ive_atoi(c);
                if ((c = past(c, ','))) { b = ive_atoi(c);
                    if ((c = past(c, ','))) a = ive_atoi(c); } } }
        emit(p, IVE_OP_FADEOUT, pack_argb(r, g, b, a), frames);
    } else if (kw(line, "lightoff", 8)) {
        emit(p, IVE_OP_LIGHTOFF, 0, 0);
    } else if (kw(line, "lighton:", 8)) {
        int v = ive_atoi(line + 8);
        arg = past(line + 8, ':');
        emit(p, IVE_OP_LIGHTON, v, arg ? ive_atoi(arg) : 0);
    } else if (kw(line, "end:", 4)) {
        /* end: — script terminator (engine 0x46dd76, returns 3 → the walk
         * marks the scene's seen-flag and signals completion). Every
         * prologue script ends with this; the trailing IVE_OP_END (NULL fn)
         * the compiler always appends only *idles* the walk, it does not end
         * the script. See docs/findings/opening-prologue.md §handler bodies. */
        emit(p, IVE_OP_END_SCRIPT, 0, 0);
    } else if (kw(line, "wait:", 5)) {
        emit(p, IVE_OP_WAIT, ive_atoi(line + 5), 0);
    } else if (kw(line, "music:", 6)) {
        emit(p, IVE_OP_MUSIC, ive_atoi(line + 6), 0);
    } else if (kw(line, "holdmusic", 9)) {
        emit(p, IVE_OP_HOLDMUSIC, 0, 0);
    } else if (kw(line, "mfadein:", 8)) {
        emit(p, IVE_OP_MFADEIN, ive_atoi(line + 8), 0);
    } else if (kw(line, "mfadeout:", 9)) {
        emit(p, IVE_OP_MFADEOUT, ive_atoi(line + 9), 0);
    } else if (kw(line, "se:", 3)) {
        if (p->n_se < IVE_MAX_NAMES) copy_name(p->se[p->n_se], line + 3); else p->overflow = 1;
        emit(p, IVE_OP_SE, p->n_se, 0);
        if (p->n_se < IVE_MAX_NAMES) p->n_se++;
    } else if (kw(line, "chr:", 4)) {
        parse_chr(p, line);
    } else if (kw(line, "msg:", 4)) {
        parse_msg(p, line);
    }
    /* else: unrecognised line — engine MessageBoxes (local_8==0); we drop it. */
}

int scene1_dialogue_parse(const char *text, struct ive_program *prog)
{
    memset(prog, 0, sizeof *prog);
    if (!text) return 0;

    const char *p = text;
    while (*p) {
        /* Find the end of this logical line (CR / LF / CRLF / LFCR / EOS). */
        const char *eol = p;
        while (*eol && *eol != '\r' && *eol != '\n') eol++;

        char first = p[0];
        if (first != '\r' && first != '\n' && first != '\t' && first != '/') {
            /* The parse helpers stop at \r/\n, so we can hand them `p`
             * directly without copying the line out. */
            parse_line(prog, p);
        }

        /* advance past the line terminator(s) */
        if (*eol == '\r' && eol[1] == '\n') eol += 2;
        else if (*eol == '\n' && eol[1] == '\r') eol += 2;
        else if (*eol) eol += 1;
        p = eol;
    }

    /* IVE_OP_END terminator (fn == NULL) — the walk loop stops here. */
    emit(prog, IVE_OP_END, 0, 0);

    return prog->n_cmds > 1 ? 1 : 0;   /* >1 ⇒ at least one real command */
}
