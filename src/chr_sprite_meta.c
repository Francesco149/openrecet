/*
 * chr_sprite_meta.c — Cchr.2a.  See chr_sprite_meta.h for the layout
 * map and the engine cross-references (FUN_00479f78 + FUN_004341fe tail).
 */
/* This TU holds the asset-independent data layer (alloc + parser +
 * accessors); it is linked into the host test suite.  The storage-backed
 * loaders live in chr_sprite_meta_load.c (real build only). */
#include "chr_sprite_meta.h"

#include <stdlib.h>
#include <string.h>

uint8_t *g_chr_desc          = NULL;
uint8_t *g_chr_formdata      = NULL;
size_t   g_chr_formdata_size = 0;

/* ─── descriptor allocation ─────────────────────────────────────────── */

int chr_meta_alloc(void)
{
    if (g_chr_desc != NULL)
        return 1;
    g_chr_desc = (uint8_t *)calloc(CHR_META_NUM_CHARS, CHR_META_BLOCK_BYTES);
    return g_chr_desc != NULL;
}

void chr_meta_shutdown(void)
{
    free(g_chr_desc);
    g_chr_desc = NULL;
    free(g_chr_formdata);
    g_chr_formdata = NULL;
    g_chr_formdata_size = 0;
}

uint8_t *chr_meta_block(int char_idx)
{
    if (g_chr_desc == NULL || char_idx < 0 || char_idx >= CHR_META_NUM_CHARS)
        return NULL;
    return g_chr_desc + (size_t)char_idx * CHR_META_BLOCK_BYTES;
}

/* ─── tiny parse helpers (mirror the engine's "%s" + comma-walk) ─────── */

/* sscanf(line, "%s", out): skip leading whitespace, copy until whitespace
 * or NUL.  `out` is a caller buffer of at least `cap` bytes; result is
 * NUL-terminated.  Returns the number of bytes copied. */
static size_t extract_token(const char *line, char *out, size_t cap)
{
    size_t n = 0;
    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n')
        line++;
    while (*line != '\0' && *line != ' ' && *line != '\t' &&
           *line != '\r' && *line != '\n' && n + 1 < cap) {
        out[n++] = *line++;
    }
    out[n] = '\0';
    return n;
}

/* Parse up to `max` comma-separated integers from `tok` (atoi semantics:
 * each field is read with atoi, which stops at the first non-numeric
 * char).  Returns the count parsed (>=1 for a non-empty token). */
static int parse_comma_ints(const char *tok, int32_t *vals, int max)
{
    int count = 0;
    const char *p = tok;
    while (count < max) {
        vals[count++] = (int32_t)atoi(p);
        /* advance to the next comma (bounded by NUL — the engine walks
         * unbounded but valid .idx data always has the comma). */
        while (*p != ',' && *p != '\0')
            p++;
        if (*p != ',')
            break;          /* no further field */
        p++;
    }
    return count;
}

/* ─── the .idx parser (FUN_00479f78 per-file body) ──────────────────── */

void chr_meta_parse_idx(int char_idx, const char *text)
{
    uint8_t *block = chr_meta_block(char_idx);
    if (block == NULL || text == NULL)
        return;

    int32_t *lut = (int32_t *)(block + CHR_META_OFF_LUT);
    char tok[512];
    int   state = 0;            /* engine local_8 */
    int   anim_base = -CHR_META_ANIM_STRIDE_DW;  /* engine local_c (-0x100) */
    int   frame_dw = 0;         /* engine iVar8 (field index within anim) */

    const char *p = text;
    while (*p != '\0') {
        /* slice one line (up to and including the terminator) */
        const char *line = p;
        while (*p != '\0' && *p != '\r' && *p != '\n')
            p++;
        size_t line_len = (size_t)(p - line);
        if (*p != '\0')
            p++;                /* consume the \r or \n */

        /* engine guard: skip lines that begin with a bare terminator */
        if (line_len == 0)
            continue;
        char first = line[0];
        if (first == '\r' || first == '\n')
            continue;

        /* work on a NUL-bounded copy of the line for the token helpers */
        char linebuf[512];
        size_t cpy = line_len < sizeof(linebuf) - 1 ? line_len : sizeof(linebuf) - 1;
        memcpy(linebuf, line, cpy);
        linebuf[cpy] = '\0';

        if (state == 0) {
            if (first != '/' && first != '[') {
                size_t tn = extract_token(linebuf, tok, sizeof(tok));
                /* sheet name → block+0x00 (engine puVar4-0x20); the
                 * memset guarantees the 0x20-byte field stays
                 * NUL-terminated even at the truncation boundary. */
                if (tn > 0x1f)
                    tn = 0x1f;
                memset(block + CHR_META_OFF_NAME, 0, 0x20);
                memcpy(block + CHR_META_OFF_NAME, tok, tn);
                state = 1;
            }
            continue;
        }
        if (state == 1) {
            extract_token(linebuf, tok, sizeof(tok));
            int32_t v[2];
            int n = parse_comma_ints(tok, v, 2);
            *(int32_t *)(block + CHR_META_OFF_HDR0) = v[0];
            if (n >= 2) {       /* engine only advances when the comma exists */
                *(int32_t *)(block + CHR_META_OFF_HDR1) = v[1];
                state = 2;
            }
            continue;
        }
        if (state == 2) {
            extract_token(linebuf, tok, sizeof(tok));
            int32_t v[2];
            int n = parse_comma_ints(tok, v, 2);
            *(int32_t *)(block + CHR_META_OFF_SHEET_W) = v[0];
            if (n >= 2) {
                *(int32_t *)(block + CHR_META_OFF_HDR3) = v[1];
                state = 3;
            }
            continue;
        }
        if (state == 3) {
            extract_token(linebuf, tok, sizeof(tok));
            *(int32_t *)(block + CHR_META_OFF_YORIGIN) = (int32_t)atoi(tok);
            state = 4;
            continue;
        }
        if (state == 4) {
            extract_token(linebuf, tok, sizeof(tok));
            *(int32_t *)(block + CHR_META_OFF_SCALE) = (int32_t)atoi(tok);
            state = 5;
            continue;
        }
        if (state == 5) {
            extract_token(linebuf, tok, sizeof(tok));   /* discarded */
            state = 6;
            continue;
        }

        /* state == 6: animation / frame blocks */
        if (first == '/') {
            if (anim_base >= 0)
                lut[anim_base + frame_dw] = (int32_t)CHR_META_ANIM_END;
            anim_base += CHR_META_ANIM_STRIDE_DW;
            frame_dw = 0;
            continue;
        }

        extract_token(linebuf, tok, sizeof(tok));
        if (anim_base < 0)
            continue;           /* frame before first '/' — guarded (engine UB) */

        if (strncmp(tok, "HALT", 4) == 0) {
            for (int i = 0; i < CHR_META_FRAME_DW; i++)
                lut[anim_base + frame_dw + i] = CHR_META_HALT;
            frame_dw += CHR_META_FRAME_DW;
            continue;
        }

        int32_t v[CHR_META_FRAME_DW];
        int n = parse_comma_ints(tok, v, CHR_META_FRAME_DW);
        for (int i = 0; i < n; i++)
            lut[anim_base + frame_dw + i] = v[i];
        frame_dw += n;
    }
}

/* ─── field accessors ───────────────────────────────────────────────── */

const char *chr_meta_name(int char_idx)
{
    uint8_t *b = chr_meta_block(char_idx);
    return b ? (const char *)(b + CHR_META_OFF_NAME) : NULL;
}

int32_t chr_meta_sheet_w(int char_idx)
{
    uint8_t *b = chr_meta_block(char_idx);
    return b ? *(int32_t *)(b + CHR_META_OFF_SHEET_W) : 0;
}

int32_t chr_meta_scale_x100(int char_idx)
{
    uint8_t *b = chr_meta_block(char_idx);
    return b ? *(int32_t *)(b + CHR_META_OFF_SCALE) : 0;
}

int32_t chr_meta_y_origin(int char_idx)
{
    uint8_t *b = chr_meta_block(char_idx);
    return b ? *(int32_t *)(b + CHR_META_OFF_YORIGIN) : 0;
}

int32_t chr_meta_lut(int char_idx, int anim, int frame, int field)
{
    uint8_t *b = chr_meta_block(char_idx);
    if (b == NULL)
        return 0;
    int idx = anim * CHR_META_ANIM_STRIDE_DW + frame * CHR_META_FRAME_DW + field;
    if (idx < 0)
        return 0;
    /* bound to the LUT region of the block */
    size_t lut_dwords = (CHR_META_BLOCK_BYTES - CHR_META_OFF_LUT) / 4;
    if ((size_t)idx >= lut_dwords)
        return 0;
    return ((int32_t *)(b + CHR_META_OFF_LUT))[idx];
}
