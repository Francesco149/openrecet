/*
 * font_atlas.c — GDI atlas builder + pure-C glyph helpers.
 *
 * Source: FUN_0047c474 @ 0x47c474 (1425 bytes). See font_atlas.h for
 * the high-level architecture and on-disk record layout.
 *
 * Structure of this file:
 *   1. font_atlas_special_table[]   — embedded .data blob from
 *                                     vendor at 0x005cbc7c
 *   2. font_atlas_padded_dim()      — pure-C, dimension math
 *   3. font_atlas_blit_glyph()      — pure-C, GGO_GRAY4 → encoded byte
 *   4. font_atlas_dilate()          — pure-C, 5×5 radial halo
 *   5. font_atlas_pack_record()     — pure-C, 40-byte record assembly
 *   6. font_atlas_build_win32()     — Win32 GDI driver, behind _WIN32
 */

#include "font_atlas.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  include "storage.h"   /* storage_get_size / storage_read */
#endif

struct font_atlas g_font_atlas;

/* ─── (1) embedded special-codepoint table ──────────────────────────────
 *
 * Two bytes per entry (SJIS high + low). Extracted via
 *   tools/analyze/pe.py blob 0x005cbc7c 576 --out ...
 * from vendor/unpacked/recettear.unpacked.exe.
 *
 * 288 codepoints in writer-iteration order. The trailing 18 entries
 * are `81 40` (full-width space) — engine padding, not deliberate
 * data. We preserve the bytes verbatim so the produced fontidx.bin is
 * byte-identical to retail.
 */
const uint8_t font_atlas_special_table[FONT_ATLAS_SPECIAL_TABLE_BYTES] = {
    0x81, 0x40, 0x81, 0x49, 0x81, 0x68, 0x81, 0x94, 0x81, 0x90, 0x81, 0x93, 0x81, 0x95, 0x81, 0x66,
    0x81, 0x69, 0x81, 0x6a, 0x81, 0x96, 0x81, 0x7b, 0x81, 0x43, 0x81, 0x7c, 0x81, 0x44, 0x81, 0x5e,
    0x82, 0x4f, 0x82, 0x50, 0x82, 0x51, 0x82, 0x52, 0x82, 0x53, 0x82, 0x54, 0x82, 0x55, 0x82, 0x56,
    0x82, 0x57, 0x82, 0x58, 0x81, 0x46, 0x81, 0x47, 0x81, 0x83, 0x81, 0x81, 0x81, 0x84, 0x81, 0x48,
    0x81, 0x97, 0x82, 0x60, 0x82, 0x61, 0x82, 0x62, 0x82, 0x63, 0x82, 0x64, 0x82, 0x65, 0x82, 0x66,
    0x82, 0x67, 0x82, 0x68, 0x82, 0x69, 0x82, 0x6a, 0x82, 0x6b, 0x82, 0x6c, 0x82, 0x6d, 0x82, 0x6e,
    0x82, 0x6f, 0x82, 0x70, 0x82, 0x71, 0x82, 0x72, 0x82, 0x73, 0x82, 0x74, 0x82, 0x75, 0x82, 0x76,
    0x82, 0x77, 0x82, 0x78, 0x82, 0x79, 0x81, 0x6d, 0x81, 0x8f, 0x81, 0x6e, 0x81, 0x4f, 0x81, 0x51,
    0x81, 0x79, 0x81, 0x7a, 0x81, 0x60, 0x81, 0x7d, 0x81, 0x40, 0x81, 0x40, 0x82, 0xf0, 0x82, 0x9f,
    0x82, 0xa1, 0x82, 0xa3, 0x82, 0xa5, 0x82, 0xa7, 0x82, 0xe1, 0x82, 0xe3, 0x82, 0xe5, 0x82, 0xc1,
    0x81, 0x5b, 0x82, 0xa0, 0x82, 0xa2, 0x82, 0xa4, 0x82, 0xa6, 0x82, 0xa8, 0x82, 0xa9, 0x82, 0xab,
    0x82, 0xad, 0x82, 0xaf, 0x82, 0xb1, 0x82, 0xb3, 0x82, 0xb5, 0x82, 0xb7, 0x82, 0xb9, 0x82, 0xbb,
    0x82, 0xbd, 0x82, 0xbf, 0x82, 0xc2, 0x82, 0xc4, 0x82, 0xc6, 0x82, 0xc8, 0x82, 0xc9, 0x82, 0xca,
    0x82, 0xcb, 0x82, 0xcc, 0x82, 0xcd, 0x82, 0xd0, 0x82, 0xd3, 0x82, 0xd6, 0x82, 0xd9, 0x82, 0xdc,
    0x82, 0xdd, 0x82, 0xde, 0x82, 0xdf, 0x82, 0xe0, 0x82, 0xe2, 0x82, 0xe4, 0x82, 0xe6, 0x82, 0xe7,
    0x82, 0xe8, 0x82, 0xe9, 0x82, 0xea, 0x82, 0xeb, 0x82, 0xed, 0x82, 0xf1, 0x81, 0xaa, 0x81, 0xab,
    0x81, 0xa9, 0x81, 0x42, 0x81, 0x75, 0x81, 0x76, 0x81, 0x41, 0x81, 0x45, 0x83, 0x92, 0x83, 0x40,
    0x83, 0x42, 0x83, 0x44, 0x83, 0x46, 0x83, 0x48, 0x83, 0x83, 0x83, 0x85, 0x83, 0x87, 0x83, 0x62,
    0x81, 0xa8, 0x83, 0x41, 0x83, 0x43, 0x83, 0x45, 0x83, 0x47, 0x83, 0x49, 0x83, 0x4a, 0x83, 0x4c,
    0x83, 0x4e, 0x83, 0x50, 0x83, 0x52, 0x83, 0x54, 0x83, 0x56, 0x83, 0x58, 0x83, 0x5a, 0x83, 0x5c,
    0x83, 0x5e, 0x83, 0x60, 0x83, 0x63, 0x83, 0x65, 0x83, 0x67, 0x83, 0x69, 0x83, 0x6a, 0x83, 0x6b,
    0x83, 0x6c, 0x83, 0x6d, 0x83, 0x6e, 0x83, 0x71, 0x83, 0x74, 0x83, 0x77, 0x83, 0x7a, 0x83, 0x7d,
    0x83, 0x7e, 0x83, 0x80, 0x83, 0x81, 0x83, 0x82, 0x83, 0x84, 0x83, 0x86, 0x83, 0x88, 0x83, 0x89,
    0x83, 0x8a, 0x83, 0x8b, 0x83, 0x8c, 0x83, 0x8d, 0x83, 0x8f, 0x83, 0x93, 0x82, 0x81, 0x82, 0x82,
    0x82, 0x83, 0x82, 0x84, 0x82, 0x85, 0x82, 0x86, 0x82, 0x87, 0x82, 0x88, 0x82, 0x89, 0x82, 0x8a,
    0x82, 0x8b, 0x82, 0x8c, 0x82, 0x8d, 0x82, 0x8e, 0x82, 0x8f, 0x82, 0x90, 0x82, 0x91, 0x82, 0x92,
    0x82, 0x93, 0x82, 0x94, 0x82, 0x95, 0x82, 0x96, 0x82, 0x97, 0x82, 0x98, 0x82, 0x99, 0x82, 0x9a,
    0x82, 0xaa, 0x82, 0xac, 0x82, 0xae, 0x82, 0xb0, 0x82, 0xb2, 0x82, 0xb4, 0x82, 0xb6, 0x82, 0xb8,
    0x82, 0xba, 0x82, 0xbc, 0x82, 0xbe, 0x82, 0xc0, 0x82, 0xc3, 0x82, 0xc5, 0x82, 0xc7, 0x82, 0xce,
    0x82, 0xd1, 0x82, 0xd4, 0x82, 0xd7, 0x82, 0xda, 0x83, 0x4b, 0x83, 0x4d, 0x83, 0x4f, 0x83, 0x51,
    0x83, 0x53, 0x83, 0x55, 0x83, 0x57, 0x83, 0x59, 0x83, 0x5b, 0x83, 0x5d, 0x83, 0x5f, 0x83, 0x61,
    0x83, 0x64, 0x83, 0x66, 0x83, 0x68, 0x83, 0x6f, 0x83, 0x72, 0x83, 0x75, 0x83, 0x78, 0x83, 0x7b,
    0x83, 0x94, 0x82, 0xcf, 0x82, 0xd2, 0x82, 0xd5, 0x82, 0xd8, 0x82, 0xdb, 0x83, 0x70, 0x83, 0x73,
    0x83, 0x76, 0x83, 0x79, 0x83, 0x7c, 0x81, 0x77, 0x81, 0x78, 0x83, 0x96, 0x81, 0x58, 0x81, 0x9b,
    0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40,
    0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40, 0x81, 0x40,
};

/* ─── (2) dimension math ────────────────────────────────────────────────
 *
 * Engine math from FUN_0047c474 lines 184-185:
 *   uVar9 = (-(blackBoxX & 3) & 3) + 8 + blackBoxX;
 *   pbVar19 = (byte *)(blackBoxY + 8);
 *
 * `-(blackBoxX & 3) & 3` is the "round up to multiple of 4" pad: 0
 * for x%4==0, 3/2/1 for x%4==1/2/3. So tex_w = blackBoxX + 8 + pad
 * where the result is divisible by 4. tex_h is plain blackBoxY + 8. */
void font_atlas_padded_dim(int black_box_x, int black_box_y,
                           int *out_tex_w, int *out_tex_h)
{
    int pad = (-black_box_x) & 3;
    *out_tex_w = black_box_x + 8 + pad;
    *out_tex_h = black_box_y + 8;
}

/* ─── (3) blit glyph into padded buffer ─────────────────────────────────
 *
 * Engine code at lines 213-243.
 *
 * GGO_GRAY4 returns values 0..16 per pixel (17 levels). The engine
 * clamps at 15 — losing the topmost level but keeping the encoding
 * to a single nibble. We mirror that clamp.
 *
 * Per-pixel encoding into dst byte:
 *   src==0 → dst byte unchanged (zero by zero-fill precondition)
 *   src>0  → dst = (src << 4) | 0xf
 *            high nibble holds the alpha, low nibble holds the engine's
 *            quirky "0xf" marker that says "this pixel is part of the
 *            anti-aliased glyph body". The dilation step will overwrite
 *            the low nibble for transparent neighbors only.
 *
 * The blit is placed at (row=4, col=4) inside the dst buffer, leaving
 * a 4-pixel border on every side (which becomes the dilation halo's
 * scratch space).
 */
void font_atlas_blit_glyph(uint8_t *dst, int tex_w, int tex_h,
                           const uint8_t *src, int src_w, int src_h)
{
    /* Engine guards: skip blit if src dimensions are zero. */
    if (src_w <= 0 || src_h <= 0) return;
    if (tex_w < src_w + 8 || tex_h < src_h + 8) return;

    for (int y = 0; y < src_h; y++) {
        const uint8_t *src_row = src + (size_t)y * src_w;
        uint8_t *dst_row = dst + (size_t)(y + 4) * tex_w + 4;
        for (int x = 0; x < src_w; x++) {
            uint8_t a = src_row[x];
            if (a > 0xf) a = 0xf;
            /* Engine's two-step write: set 0xf marker, then add alpha<<4. */
            if (a != 0) {
                dst_row[x] = (uint8_t)(0xf + (a << 4)); /* = (a<<4) | 0xf */
            }
        }
    }
}

/* ─── (4) radial 5×5 edge dilation ──────────────────────────────────────
 *
 * Engine code at lines 244-294. For each glyph pixel (high nibble > 1),
 * splat a halo into the 5×5 neighborhood (deltas in [-4..4]² minus
 * (0,0)). Halo intensity is:
 *
 *    if (dist <= edgewi)       intensity = 15           // full
 *    else if (intensity > 0)   intensity = 15 - floor(dist*edgedel)
 *    else                      skip
 *
 * The halo only lands on transparent/barely-opaque neighbors (high
 * nibble <= 1), and only if it beats whatever halo is already there
 * (low nibble strictly less than the new value).
 *
 * Final neighbor byte = (intensity & 0xf) | 0x10. Bit 4 marks the
 * pixel as "dilated edge"; the texture upload path will decode this
 * as "draw with foreground=black, alpha=intensity*17".
 */
void font_atlas_dilate(uint8_t *buf, int tex_w, int tex_h,
                       float edgewi, float edgedel)
{
    for (int y = 0; y < tex_h; y++) {
        for (int x = 0; x < tex_w; x++) {
            uint8_t cur = buf[(size_t)y * tex_w + x];
            /* Engine gate: only propagate from pixels with alpha > 1
             * (high nibble > 1 → byte & 0xf0 > 0x1f). */
            if ((cur & 0xf0) <= 0x1f) continue;

            for (int dy = -4; dy <= 4; dy++) {
                int ny = y + dy;
                if (ny < 0 || ny >= tex_h) continue;
                for (int dx = -4; dx <= 4; dx++) {
                    int nx = x + dx;
                    if (nx < 0 || nx >= tex_w) continue;
                    if (dx == 0 && dy == 0) continue;

                    float d2 = (float)(dx * dx + dy * dy);
                    float dist = sqrtf(d2);
                    int intensity = 0xf;
                    if (edgewi <= dist) {
                        /* Engine: __ftol(dist * edgedel) — truncate
                         * toward zero. dist is non-negative so this
                         * is plain floor. */
                        intensity = 0xf - (int)(dist * edgedel);
                        if (intensity < 1) continue;
                    }

                    size_t nidx = (size_t)ny * tex_w + nx;
                    uint8_t neighbor = buf[nidx];
                    /* Neighbor must be transparent/barely-opaque AND
                     * not already painted brighter. */
                    if ((neighbor & 0xf0u) >= 0x20u) continue;
                    if ((neighbor & 0x0fu) >= (uint8_t)intensity) continue;
                    buf[nidx] = (uint8_t)((intensity & 0xf) | 0x10);
                }
            }
        }
    }
}

/* ─── (5) pack a fontidx record ─────────────────────────────────────────
 *
 * Engine code at lines 299-310. The +8 padding on every dimension is
 * the engine's bookkeeping for the 4-pixel border that font_atlas_blit
 * adds (4 on each side = +8 total). cell_inc_x also gets the same
 * x-pad rounding to keep advance widths aligned. */
void font_atlas_pack_record(struct font_atlas_record *out,
                            uint32_t running_offset,
                            uint32_t data_size,
                            int32_t cell_inc_x_raw,
                            int32_t tmHeight,
                            int32_t tmAscent,
                            int32_t black_box_x,
                            int32_t black_box_y,
                            int32_t origin_x,
                            int32_t origin_y)
{
    int32_t pad = (-black_box_x) & 3;
    out->data_offset = running_offset;
    out->data_size   = data_size;
    out->cell_inc_x  = cell_inc_x_raw + 8 + pad;
    out->line_height = tmHeight + 8;
    out->origin_x    = origin_x;
    out->ascent      = tmAscent;
    out->origin_y    = origin_y;
    out->tex_width   = black_box_x + 8 + pad;
    out->tex_height  = black_box_y + 8;
    out->reserved    = 0;
}

/* ─── (5b) atlas loader (disk-only) ─────────────────────────────────────
 *
 * Mirrors FUN_0047c3a5 minus the storage_* fallback. Reads two files
 * into the heap and exposes them via g_font_atlas.
 *
 * The engine's loader adds +10 bytes of slack to its malloc (line 9 of
 * 47c3a5.c — `_malloc(iVar1 + 10)`). We don't need that here since
 * we're not in the `storage_size` fallback branch; pure fopen/ftell
 * gives the exact byte count.
 */

static int slurp_file(const char *path, uint8_t **out_buf, size_t *out_size)
{
    *out_buf = NULL;
    *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return 0; }

    *out_buf  = buf;
    *out_size = (size_t)sz;
    return 1;
}

void font_atlas_free(void)
{
    free(g_font_atlas.fontdata);
    free(g_font_atlas.fontidx);
    memset(&g_font_atlas, 0, sizeof g_font_atlas);
}

/* Try storage_read fallback (lnkdatas). The EN retail build ships
 * fontdata.bin + fontidx.bin INSIDE lnkdatas — the engine's
 * FUN_0047c3a5 loader probes fopen first, then falls back to
 * storage_read by name. We mirror that path. The shipped atlas was
 * built on a JP-locale dev machine and works cleanly with the
 * engine's draw_text math regardless of the user's locale, so it's
 * the preferred source. */
#ifdef _WIN32
static int slurp_storage(const char *name, uint8_t **out_buf, size_t *out_size)
{
    *out_buf  = NULL;
    *out_size = 0;
    size_t sz = storage_get_size(name);
    if (sz == 0) return 0;
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) return 0;
    size_t got = storage_read(name, buf);
    if (got != sz) { free(buf); return 0; }
    *out_buf  = buf;
    *out_size = sz;
    return 1;
}
#endif

int font_atlas_load(const char *atlas_dir)
{
    font_atlas_free();

    const char *dir = atlas_dir ? atlas_dir : FONT_ATLAS_DEFAULT_DIR;

    char tmp_data[2][512], tmp_idx[2][512];
    snprintf(tmp_data[0], sizeof tmp_data[0], "%s/fontdata.bin", dir);
    snprintf(tmp_idx[0],  sizeof tmp_idx[0],  "%s/fontidx.bin",  dir);
    snprintf(tmp_data[1], sizeof tmp_data[1], "%s", "fontdata.bin");
    snprintf(tmp_idx[1],  sizeof tmp_idx[1],  "%s", "fontidx.bin");

    uint8_t *data_buf = NULL, *idx_buf = NULL;
    size_t   data_sz  = 0,    idx_sz  = 0;
    const char *picked_label = NULL;

    /* Pass 1+2: disk paths (atlas_dir then cwd). */
    for (int i = 0; i < 2; i++) {
        if (slurp_file(tmp_data[i], &data_buf, &data_sz) &&
            slurp_file(tmp_idx[i],  &idx_buf,  &idx_sz)) {
            picked_label = tmp_data[i];
            break;
        }
        free(data_buf); data_buf = NULL; data_sz = 0;
        free(idx_buf);  idx_buf  = NULL; idx_sz  = 0;
    }

#ifdef _WIN32
    /* Pass 3: lnkdatas / bmpdata via storage_read. This is where the
     * EN retail build's shipped atlas lives — and on EN-locale Windows
     * it's the only reliable source. */
    if (!picked_label) {
        if (slurp_storage("fontdata.bin", &data_buf, &data_sz) &&
            slurp_storage("fontidx.bin",  &idx_buf,  &idx_sz)) {
            picked_label = "storage:fontdata.bin";
        } else {
            free(data_buf); data_buf = NULL; data_sz = 0;
            free(idx_buf);  idx_buf  = NULL; idx_sz  = 0;
        }
    }
#endif

    if (!picked_label) {
        fprintf(stderr,
            "font_atlas: no atlas files found "
            "(searched '%s/', cwd, storage)\n", dir);
        return 0;
    }

    /* Sanity: fontidx must be a whole multiple of 40. */
    if (idx_sz == 0 || idx_sz % FONT_ATLAS_RECORD_SIZE != 0) {
        fprintf(stderr,
            "font_atlas: fontidx.bin from %s is %zu bytes (not /%d)\n",
            picked_label, idx_sz, FONT_ATLAS_RECORD_SIZE);
        free(data_buf); free(idx_buf);
        return 0;
    }

    g_font_atlas.fontdata      = data_buf;
    g_font_atlas.fontdata_size = data_sz;
    g_font_atlas.fontidx       = (struct font_atlas_record *)idx_buf;
    g_font_atlas.fontidx_count = idx_sz / FONT_ATLAS_RECORD_SIZE;
    fprintf(stderr,
        "font_atlas: loaded %s (%zu glyph bytes, %zu records)\n",
        picked_label,
        g_font_atlas.fontdata_size,
        g_font_atlas.fontidx_count);
    return 1;
}

/* ─── (6) Win32 GDI atlas builder — DEAD CODE in the shipped game ──────
 *
 * This is a fully working port of FUN_0047c474, but the shipped EN
 * retail build never actually runs it: vendor config.idx has the
 * `font:` key commented out, so DAT_073dfd00 stays at 0 and WinMain's
 * `if (DAT_073dfd00 != 0) FUN_0047c474()` branch is dead. The atlas
 * the game actually uses (fontdata.bin + fontidx.bin) ships INSIDE
 * lnkdatas — see docs/findings/engine-quirks.md §"Font atlas is
 * shipped, not regenerated".
 *
 * Why is this here anyway? On the original 2007 dev's Japanese-locale
 * Windows machine, GDI's font substitution resolved
 * `face="ＭＳ Ｐゴシック" SJIS + lfCharSet=SHIFTJIS_CHARSET` to a font
 * variant whose glyph metrics compose cleanly with the engine's
 * draw_text math (fixed dst_h=42, src rect [1,1,41,41]). We tried to
 * reproduce that setup on the user's English-locale Windows and
 * couldn't — every variant we got back from GDI (MS Gothic SHIFTJIS,
 * MS Gothic ANSI, MS PGothic ANSI, ...) produced visually-mangled
 * output. The shipped atlas works because the dev's machine had the
 * exact right font variant available, baked it into the atlas bytes,
 * and shipped the result.
 *
 * The code stays for fidelity + future use (a JP-version port might
 * end up needing to regen on a JP-locale machine, in which case this
 * still works). For the EN drop-in case, main.c skips the call
 * entirely; the loader pulls the atlas from storage. */
#ifdef _WIN32

#include <windows.h>

/* Decide if a phase-1 (SJIS double-byte) iVar11 lands in one of the
 * three gap ranges the engine writes zero records for. Returns 1 if
 * gap (skip render, write zero record), 0 if eligible. */
static int sjis_gap_skip(int v)
{
    if (v > 0x9ffc && v < 0xe040) return 1;
    if (v > 0xeeec && v < 0xfa57) return 1;
    if (v > 0xfbfc)               return 1;
    return 0;
}

static int write_glyph(FILE *fdata, FILE *fidx,
                       HDC hdc,
                       UINT codepoint,
                       uint32_t *running_offset,
                       float edgewi, float edgedel)
{
    /* GetGlyphOutline GGO_GRAY4 with identity transform. */
    MAT2 mat = {
        .eM11 = {0, 1}, .eM12 = {0, 0},
        .eM21 = {0, 0}, .eM22 = {0, 1},
    };
    GLYPHMETRICS gm;
    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);

    DWORD glyph_size = GetGlyphOutlineA(hdc, codepoint, GGO_GRAY4_BITMAP,
                                        &gm, 0, NULL, &mat);
    if (glyph_size == GDI_ERROR) {
        glyph_size = 0;
    }

    uint8_t *raw = NULL;
    if (glyph_size > 0) {
        raw = (uint8_t *)malloc(glyph_size);
        if (!raw) return 0;
        if (GetGlyphOutlineA(hdc, codepoint, GGO_GRAY4_BITMAP,
                             &gm, glyph_size, raw, &mat) == GDI_ERROR) {
            free(raw);
            return 0;
        }
    }

    /* Engine line 180: small glyphs (size < 3) write the raw GGO bytes
     * straight to fontdata.bin with no blit/dilate. Larger ones get the
     * full padded + halo encoding. */
    uint32_t bytes_written = 0;
    if (glyph_size < 3) {
        if (glyph_size > 0) {
            if (fwrite(raw, 1, glyph_size, fdata) != glyph_size) {
                free(raw);
                return 0;
            }
            bytes_written = glyph_size;
        }
    } else {
        int tex_w, tex_h;
        font_atlas_padded_dim((int)gm.gmBlackBoxX, (int)gm.gmBlackBoxY,
                              &tex_w, &tex_h);
        size_t buf_size = (size_t)tex_w * (size_t)tex_h;
        uint8_t *padded = (uint8_t *)calloc(buf_size, 1);
        if (!padded) { free(raw); return 0; }
        font_atlas_blit_glyph(padded, tex_w, tex_h,
                              raw, (int)gm.gmBlackBoxX, (int)gm.gmBlackBoxY);
        font_atlas_dilate(padded, tex_w, tex_h, edgewi, edgedel);
        if (fwrite(padded, 1, buf_size, fdata) != buf_size) {
            free(padded); free(raw); return 0;
        }
        bytes_written = (uint32_t)buf_size;
        free(padded);
    }

    /* Pack and write the fontidx record. */
    struct font_atlas_record rec;
    font_atlas_pack_record(&rec, *running_offset, bytes_written,
                           (int32_t)gm.gmCellIncX,
                           tm.tmHeight, tm.tmAscent,
                           (int32_t)gm.gmBlackBoxX, (int32_t)gm.gmBlackBoxY,
                           (int32_t)gm.gmptGlyphOrigin.x,
                           (int32_t)gm.gmptGlyphOrigin.y);
    if (fwrite(&rec, 1, FONT_ATLAS_RECORD_SIZE, fidx)
        != FONT_ATLAS_RECORD_SIZE) {
        free(raw);
        return 0;
    }

    *running_offset += bytes_written;
    free(raw);
    return 1;
}

static int write_zero_record(FILE *fidx)
{
    struct font_atlas_record zero = {0};
    return fwrite(&zero, 1, FONT_ATLAS_RECORD_SIZE, fidx)
           == FONT_ATLAS_RECORD_SIZE;
}

int font_atlas_build_win32(const char *out_dir,
                           const char *face_name,
                           float edgewi,
                           float edgedel,
                           int kanji_off)
{
    if (!out_dir || !face_name) return 0;

    /* Ensure out_dir exists. CreateDirectoryA returns 0 on failure;
     * we accept ERROR_ALREADY_EXISTS as success. */
    if (!CreateDirectoryA(out_dir, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            fprintf(stderr,
                "font_atlas: cannot create out dir '%s' (err=%lu)\n",
                out_dir, (unsigned long)err);
            return 0;
        }
    }

    /* CreateFontIndirectA — same LOGFONTA layout the engine sets up. */
    LOGFONTA lf = {0};
    lf.lfHeight        = 0x2a;          /* 42px — engine constant */
    (void)0;                            /* placeholder */
    /* Engine literal: SHIFTJIS_CHARSET (0x80). We override to
     * ANSI_CHARSET (0) for the drop-in case:
     *
     * The engine sends face="ＭＳ Ｐゴシック" SJIS bytes + SHIFTJIS.
     * On the user's EN Windows, MS PGothic is installed under its
     * ASCII name; the SJIS byte name doesn't match. GDI falls back
     * to MS Gothic's SHIFTJIS variant, whose ANSI-codepoint metrics
     * (gmCellIncX / gmBlackBoxX) don't compose cleanly with the
     * engine's draw_text math (fixed dst_h = 42, src rect [1,1,41,41])
     * — text renders as a mangled vertical-bar pattern.
     *
     * Caller (main.c) overrides the face name to ASCII "MS PGothic"
     * which DOES match the installed font. Combined with ANSI_CHARSET
     * here, GDI returns MS PGothic + tmCharSet=0 — proportional
     * Japanese font with proper Latin metrics. Text renders cleanly.
     *
     * Engine-intent (SHIFTJIS variant with kanji glyphs) deferred to
     * a future JP-version port; the EN game never renders kanji
     * regardless. See tools/diagnostics/font/ for the probe history. */
    lf.lfCharSet       = ANSI_CHARSET;
    /* Engine literal: OUT_TT_ONLY_PRECIS (7) — restricts to TrueType.
     * Retail's process gets a NON-TrueType MS Gothic (tmPF=0x02 has
     * no TMPF_TRUETYPE bit), implying GDI falls through OUT_TT_ONLY
     * in retail's context. Use OUT_DEFAULT_PRECIS (0) so GDI can
     * pick the same raster/vector MS Gothic retail picks. */
    lf.lfOutPrecision  = OUT_DEFAULT_PRECIS;
    lf.lfQuality       = ANTIALIASED_QUALITY; /* 2 */
    /* Engine: 0x31 = FIXED_PITCH (1) | FF_MODERN (0x30), NOT |FF_DONTCARE
     * (0). Wrong family hint here pushes GDI through different font
     * substitution and ends up picking a DIFFERENT face when MS PGothic
     * is missing the exact glyph — kanji land at 48×49 instead of 16×16,
     * blowing the atlas to 7× the expected size. */
    lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN; /* 0x31 */
    /* Copy face_name into lfFaceName with explicit truncation. */
    strncpy(lf.lfFaceName, face_name, LF_FACESIZE - 1);
    lf.lfFaceName[LF_FACESIZE - 1] = '\0';

    HFONT hfont = CreateFontIndirectA(&lf);
    if (!hfont) {
        fprintf(stderr, "font_atlas: CreateFontIndirectA failed (face='%s')\n",
                face_name);
        return 0;
    }

    HDC hdc = GetDC(NULL);
    if (!hdc) {
        fprintf(stderr, "font_atlas: GetDC(NULL) failed\n");
        DeleteObject(hfont);
        return 0;
    }
    HGDIOBJ old = SelectObject(hdc, hfont);

    /* Log the actual face name GDI selected. Useful for diagnosing
     * locale-dependent font substitution — the same lfCharSet request
     * resolves to different variants of MS Gothic depending on the
     * process's default code page. */
    {
        char actual_face[64] = {0};
        TEXTMETRICA dtm;
        GetTextFaceA(hdc, sizeof actual_face, actual_face);
        GetTextMetricsA(hdc, &dtm);
        fprintf(stderr,
            "font_atlas: GDI face='%s' tmCharSet=%d "
            "tmH=%ld tmAsc=%ld tmDes=%ld tmPF=0x%02x tmFC=%lu\n",
            actual_face, dtm.tmCharSet,
            dtm.tmHeight, dtm.tmAscent, dtm.tmDescent,
            dtm.tmPitchAndFamily, (unsigned long)dtm.tmFirstChar);
    }

    /* Build the two output paths. MAX_PATH-safe. */
    char path_data[MAX_PATH], path_idx[MAX_PATH];
    int n1 = snprintf(path_data, sizeof path_data, "%s\\fontdata.bin", out_dir);
    int n2 = snprintf(path_idx,  sizeof path_idx,  "%s\\fontidx.bin",  out_dir);
    if (n1 < 0 || n1 >= (int)sizeof path_data ||
        n2 < 0 || n2 >= (int)sizeof path_idx) {
        fprintf(stderr, "font_atlas: out_dir path too long\n");
        SelectObject(hdc, old); ReleaseDC(NULL, hdc); DeleteObject(hfont);
        return 0;
    }

    FILE *fdata = fopen(path_data, "wb");
    FILE *fidx  = fopen(path_idx,  "wb");
    int ok = (fdata != NULL && fidx != NULL);
    if (!ok) {
        fprintf(stderr,
            "font_atlas: failed to open output files (%s, %s)\n",
            path_data, path_idx);
        goto cleanup;
    }

    fprintf(stderr,
        "font_atlas: building atlas (face='%s' edgewi=%.2f edgedel=%.2f kanji_off=%d)\n",
        face_name, edgewi, edgedel, kanji_off);

    uint32_t running_offset = 0;
    uint32_t records_written = 0;

    /* Phase 0a: 256 single-byte codepoints 0x00..0xff. */
    for (int cp = 0; cp < 256; cp++) {
        if (!write_glyph(fdata, fidx, hdc, (UINT)cp, &running_offset,
                         edgewi, edgedel)) {
            ok = 0; goto cleanup;
        }
        records_written++;
    }

    /* Phase 0b: 288 special 2-byte codepoints from the embedded table. */
    for (int i = 0; i < FONT_ATLAS_SPECIAL_TABLE_COUNT; i++) {
        uint8_t hi = font_atlas_special_table[i * 2];
        uint8_t lo = font_atlas_special_table[i * 2 + 1];
        /* SJIS codepoint as a single UINT: lead-byte<<8 | trail-byte. */
        UINT cp = ((UINT)hi << 8) | lo;
        if (!write_glyph(fdata, fidx, hdc, cp, &running_offset,
                         edgewi, edgedel)) {
            ok = 0; goto cleanup;
        }
        records_written++;
    }

    /* Phase 1: SJIS double-byte walk from 0x883f, with gap zero-records.
     *
     * Engine quirk: the very first iteration renders codepoint 0x883f,
     * which is *not* a valid SJIS double-byte (low 0x3f outside the
     * valid second-byte range 0x40..0x7e,0x80..0xfc). GDI returns an
     * empty glyph; the fontidx slot at index 544 ends up referencing
     * zero bytes. Harmless — no reader ever asks for 0x883f.
     *
     * Loop terminates either when iVar11 - 0x883e == 0x8000 (i.e.
     * iVar11 = 0x1083e — full walk done) or kanji_off is set.
     *
     * Note on the polarity of `kanji_off`: the engine's check at
     * 0x47ca0e in disassembly is `if (DAT_005cbc70 != 0) break;` —
     * Ghidra renders this as `== 0` due to branch reversal. Vendor
     * config has kanjioff=0 (default) → kanji rendered. We pass the
     * caller's `kanji_off` directly; non-zero means "skip kanji". */
    if (!kanji_off) {
        int iVar11 = 0x883f;
        while (1) {
            if (iVar11 + -0x883e == 0x8000) break; /* full walk done */
            int gap = sjis_gap_skip(iVar11);
            int low = (iVar11 + -0x883e) & 0xff;
            if (low < 0xc0 && !gap) {
                if (!write_glyph(fdata, fidx, hdc, (UINT)iVar11,
                                 &running_offset, edgewi, edgedel)) {
                    ok = 0; goto cleanup;
                }
            } else {
                if (!write_zero_record(fidx)) { ok = 0; goto cleanup; }
            }
            records_written++;
            iVar11++;
        }
    }

    fprintf(stderr,
        "font_atlas: wrote %u records, %u glyph bytes\n",
        (unsigned)records_written, (unsigned)running_offset);

cleanup:
    if (fdata) fclose(fdata);
    if (fidx)  fclose(fidx);
    if (hdc) {
        SelectObject(hdc, old);
        ReleaseDC(NULL, hdc);
    }
    DeleteObject(hfont);
    return ok;
}

#endif /* _WIN32 */
