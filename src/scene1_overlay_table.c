/*
 * scene1_overlay_table.c — port of FUN_00474f4f (0x474f4f, 801 B).
 *
 * See scene1_overlay_table.h for the file-format spec and the engine
 * quirks preserved.  This TU is host-portable: the buffer-driven
 * `scene1_overlay_table_parse_buf` has no Win32 deps so unit tests can
 * exercise it under ASan/UBSan.  The disk + storage fallbacks are
 * compiled unconditionally (both call into storage.h, which has a
 * portable interface).
 */

#include "scene1_overlay_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scene1_overlay.h"

#ifdef _WIN32
#include "storage.h"
#endif

/* ---- Shape table accessors ----------------------------------------- */

static void shape_set_i(int idx, int off, int32_t v)
{
    g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE + off] = v;
}

static void shape_set_f(int idx, int off, float v)
{
    int32_t bits;
    memcpy(&bits, &v, sizeof bits);
    g_scene1_overlay_shapes[idx * SCENE1_OVERLAY_SHAPE_STRIDE + off] = bits;
}

/* ---- Per-line helpers ---------------------------------------------- */

/* Mirror of FUN_00503d03 (atoi).  Engine's atoi parses a signed decimal
 * starting at the first '+' / '-' / digit; trailing garbage ignored. */
static int parse_atoi(const char *s)
{
    return atoi(s);
}

/* Engine "is end-of-line" predicate used at multiple decision points:
 * cVar6 ∈ {'\0', '\r', '\n'}. */
static int is_eol(char c)
{
    return c == '\0' || c == '\r' || c == '\n';
}

/* Skip pcVar7 forward until *pcVar7 == target.  Returns the new pcVar7
 * (or NULL if hit eol / end-of-line-buffer first).  `line_end` is one
 * past the last char of the line buffer (a '\0' sentinel). */
static const char *scan_to(const char *p, const char *line_end, char target)
{
    while (p < line_end && *p != target) {
        if (is_eol(*p)) return NULL;
        p++;
    }
    if (p >= line_end) return NULL;
    return p;
}

/* ---- GRP%02d: branch ----------------------------------------------- */

/* Match the line against `GRPNN:` for every NN in 0..99 (engine
 * iterates all 100 even after a match — observable identical since
 * NN is unique per line in well-formed input).
 *
 * On match: copy the filename (up to 255 chars or until \r/\n) into
 * g_scene1_overlay_layer_filenames[count], increment count.
 *
 * Returns 1 if matched (slot consumed), 0 otherwise. */
static int try_grp_branch(const char *line)
{
    char prefix[8];
    for (int n = 0; n < 100; n++) {
        snprintf(prefix, sizeof prefix, "GRP%02d:", n);
        /* Engine compares first 6 chars only: byte-for-byte. */
        if (memcmp(line, prefix, 6) != 0) continue;

        if (g_scene1_overlay_layer_count >= SCENE1_OVERLAY_LAYER_COUNT_MAX) {
            /* Drop silently — capacity matches engine's
             * DAT_0072a820 reservation (per our header cap). */
            return 1;
        }

        char *dst = g_scene1_overlay_layer_filenames
                        [g_scene1_overlay_layer_count];
        const char *src = line + 6;
        int j = 0;
        /* Engine: copy up to 255 chars; stop on \r or \n.  Then write
         * '\0' at the slot that held the terminator. */
        while (j < SCENE1_OVERLAY_LAYER_FILENAME_LEN - 1) {
            char c = *src++;
            dst[j] = c;
            if (c == '\r' || c == '\n' || c == '\0') break;
            j++;
        }
        dst[j] = '\0';
        g_scene1_overlay_layer_count++;
        return 1;
    }
    return 0;
}

/* ---- %03d: shape branch -------------------------------------------- */

/* Engine inner-anim parser: after the 4-arg `(ox,oy,sx,sy)` group, scan
 * forward for an optional second `(...)` group with 2..3 args
 * (frames, stride[, loop]).  Defaults are set by the caller (1/1/0).
 *
 * `p` points 1 byte past the closing `)` of the first group (per the
 * engine's `pcVar7 = pcVar7 + 1` advance).  `line_end` bounds the
 * forward search. */
static void parse_inner_anim_group(const char *p, const char *line_end,
                                   int idx)
{
    /* Scan forward for next '(' (the inner anim group).  Engine's
     * outer do-while exits on \r / \n. */
    while (p < line_end - 1) {
        char peek = p[1];
        if (peek == '(') {
            /* Step past the '(' and read frames. */
            p += 2;
            if (p >= line_end) return;
            int frames = parse_atoi(p);
            shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT, frames);
            char c = *p;
            if (is_eol(c)) return;
            /* Find ',', step past, read stride. */
            const char *comma = scan_to(p, line_end, ',');
            if (!comma) return;
            p = comma + 1;
            if (p >= line_end) return;
            int stride = parse_atoi(p);
            shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD, stride);
            c = *p;
            if (is_eol(c)) return;
            /* Engine's `if (cVar6 != 0/\r/\n) goto LAB_00475236`: scan
             * forward to the next ',' and read the loop flag.  Bound
             * to line_end to match engine's implicit \r/\n bound. */
            const char *comma2 = scan_to(p, line_end, ',');
            if (!comma2) return;
            int loop = parse_atoi(comma2 + 1);
            shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE, loop);
            return;
        }
        if (is_eol(peek)) return;
        p++;
    }
}

/* Match the line against `NNN:` for every NNN in 0..998 (engine
 * iterates all 999 entries per line — see header note on observable
 * identity for unique-NNN well-formed input).
 *
 * On match: parse `T:(ox,oy,sx,sy)[(frames,stride,loop)]` and write
 * the 8-dw shape table entry.  Updates g_scene1_overlay_shapes_max_index.
 *
 * Returns 1 if any shape index matched (regardless of full body
 * parse), 0 otherwise.  Indices >= SCENE1_OVERLAY_SHAPE_COUNT are
 * silently dropped (capacity guard). */
static int try_nnn_branch(const char *line, size_t line_len)
{
    int matched = 0;
    /* Engine end-of-loop check: `puVar9 == &DAT_00771448` after 999
     * iterations.  We iterate the same range for fidelity. */
    char prefix[8];
    const char *line_end = line + line_len;

    for (int idx = 0; idx < 999; idx++) {
        snprintf(prefix, sizeof prefix, "%03d:", idx);
        if (memcmp(line, prefix, 4) != 0) continue;

        matched = 1;

        /* Engine: DAT_0076b94c = max(DAT_0076b94c, idx + 1). */
        if (g_scene1_overlay_shapes_max_index < idx + 1) {
            g_scene1_overlay_shapes_max_index = idx + 1;
        }

        /* Skip writes if outside our storage cap; still iterates the
         * remaining loop for fidelity (no early break). */
        if (idx >= SCENE1_OVERLAY_SHAPE_COUNT) continue;

        /* Defaults — engine writes these BEFORE atoi-ing the tex idx,
         * but observable identical to writing after. */
        shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_FRAME_COUNT, 1);
        shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_FRAME_PERIOD, 1);
        shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_LOOP_MODE, 0);

        /* Read tex_idx — atoi at line[4] (start of "T:..."). */
        int tex_idx = parse_atoi(line + 4);
        shape_set_i(idx, SCENE1_OVERLAY_SHAPE_OFF_TEX_GROUP, tex_idx);

        /* If the line ends after the NNN: prefix, leave UV / anim at
         * the defaults (uv coords stay 0, anim 1/1/0). */
        if (is_eol(line[4])) continue;

        /* Scan forward for the '(' that opens the 4-arg uv group. */
        const char *p = line + 4;
        const char *paren = scan_to(p, line_end, '(');
        if (!paren) continue;
        p = paren + 1;

        /* Read 4 ints (ox, oy, sx, sy) — comma-separated. */
        int uv_offs[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; k++) {
            if (p >= line_end) goto write_uv;
            uv_offs[k] = parse_atoi(p);
            if (k == 3) break;
            const char *comma = scan_to(p, line_end, ',');
            if (!comma) goto write_uv;
            p = comma + 1;
        }
    write_uv:
        shape_set_f(idx, SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_X,
                    (float)uv_offs[0]);
        shape_set_f(idx, SCENE1_OVERLAY_SHAPE_OFF_UV_ORIGIN_Y,
                    (float)uv_offs[1]);
        shape_set_f(idx, SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_X,
                    (float)uv_offs[2]);
        shape_set_f(idx, SCENE1_OVERLAY_SHAPE_OFF_UV_SIZE_Y,
                    (float)uv_offs[3]);

        /* Optional inner anim group.  Engine: after the 4-arg group,
         * `pcVar7 = pcVar7 + 1` advances by 1 — same as our scan-to
         * leaving p pointing at the digit, then +1 puts us at the
         * char after the digit; close enough for the +1 do-while
         * advance, since the look-ahead `pcVar7[1]` doesn't care
         * about the exact starting position. */
        parse_inner_anim_group(p + 1, line_end, idx);
    }

    return matched;
}

/* ---- Buffer-driven line loop --------------------------------------- */

void scene1_overlay_table_parse_buf(const char *buf, size_t buf_len)
{
    if (!buf || buf_len == 0) return;

    /* Engine's `local_210[512]` line buffer.  Lines longer than 511
     * chars get truncated (engine same behavior; would walk off the
     * 512-byte stack array — but no real `ef/grpN.idx` line approaches
     * this). */
    char line[512];

    size_t i = 0;
    while (i < buf_len) {
        /* Read one line into `line`, including its \r/\n terminator
         * (mirrors engine L73265-73291). */
        size_t j = 0;
        char c = '\0';
        int saw_eof = 0;
        while (i < buf_len && j < sizeof line - 1) {
            c = buf[i++];
            line[j++] = c;
            if (c == '\r' || c == '\n') break;
            if (c == '\0') { saw_eof = 1; break; }
        }
        line[j] = '\0';

        /* Engine post-line `if (saw_eof || cVar6 == '\0')` exits the
         * outer loop AFTER writing the line.  We process the partial
         * line if it has content; the next iteration's `i >= buf_len`
         * naturally exits. */
        (void)saw_eof;

        /* Skip empty / comment lines: first char ∈ {\r, \n, /}. */
        if (line[0] == '\r' || line[0] == '\n' ||
            line[0] == '/'  || line[0] == '\0') continue;

        /* Engine runs BOTH branches per line — GRP loop then NNN
         * loop.  A line matching GRP can't match NNN (digits-first
         * prefix mismatch), and vice versa.  We preserve that order
         * for fidelity even though the matches are disjoint. */
        try_grp_branch(line);
        try_nnn_branch(line, j);
    }
}

/* ---- Storage-driven file loader ------------------------------------ */
#ifdef _WIN32

int scene1_overlay_table_load(const char *name)
{
    if (!name) return 0;

    char *buf = NULL;
    size_t buf_len = 0;

    /* Try disk first (engine's FUN_005038b0 fopen(name, "rb") path). */
    FILE *f = fopen(name, "rb");
    if (f) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long sz = ftell(f);
            if (sz >= 0 && fseek(f, 0, SEEK_SET) == 0) {
                buf = (char *)malloc((size_t)sz + 10);
                if (buf) {
                    size_t n = fread(buf, 1, (size_t)sz, f);
                    buf_len = n;
                    buf[n] = '\0';
                }
            }
        }
        fclose(f);
    }

    /* Fall back to storage (engine's FUN_00434585 + FUN_004346bf). */
    if (!buf) {
        size_t sz = storage_get_size(name);
        if (sz == 0) return 0;
        buf = (char *)malloc(sz + 10);
        if (!buf) return 0;
        size_t n = storage_read(name, buf);
        if (n == 0) {
            free(buf);
            return 0;
        }
        buf_len = n;
        buf[n] = '\0';
    }

    scene1_overlay_table_parse_buf(buf, buf_len);
    free(buf);
    return 1;
}

int scene1_overlay_table_load_all(void)
{
    /* Engine all.c L76530-31: reset both counters before the four-file
     * sweep.  The full scene1_overlay_layers_reset() also wipes the
     * filenames table (an arrays-of-256-byte slots stays valid). */
    scene1_overlay_layers_reset();
    scene1_overlay_shapes_reset();

    static const char *const files[4] = {
        "ef/grp1.idx",
        "ef/grp2.idx",
        "ef/grp3.idx",
        "ef/grp4.idx",
    };
    for (int n = 0; n < 4; n++) {
        scene1_overlay_table_load(files[n]);
    }
    return g_scene1_overlay_layer_count;
}

#endif /* _WIN32 */
