/*
 * font_atlas.h — GDI-driven font atlas builder (FUN_0047c474) + the
 * pure-C glyph encoding helpers it uses.
 *
 * On a stock retail install with `font: <face>` in config.idx, the
 * engine runs FUN_0047c474 once at boot to rasterize a SJIS glyph set
 * into two parallel files:
 *
 *   - fontdata.bin  — raw glyph bytes, variable-size per codepoint
 *   - fontidx.bin   — fixed-size (40 bytes) records, one per codepoint
 *
 * The runtime loader (FUN_0047c3a5 → font_atlas_load) then pulls both
 * back into memory. fontidx[cp] tells us where to find the cp's bytes
 * in fontdata + how big the glyph is + advance / ascent / origin.
 *
 * Layout of one fontidx record — derived from FUN_0047c474 lines
 * 299-310, cross-checked against the reader at line 79717:
 *
 *   off  name        meaning
 *   ---  ----        -------
 *   0x00 data_offset offset into fontdata.bin where this glyph's bytes
 *                    start (running cumulative)
 *   0x04 data_size   length in bytes (cjBuffer from GetGlyphOutlineA)
 *   0x08 cell_inc_x  total advance: gmCellIncX + 8 + (pad to 4)
 *   0x0c line_height tmHeight + 8
 *   0x10 origin_x    gmptGlyphOrigin.x  (signed)
 *   0x14 ascent      tmAscent
 *   0x18 origin_y    gmptGlyphOrigin.y  (signed)
 *   0x1c tex_width   gmBlackBoxX + 8 + (pad to 4)
 *   0x20 tex_height  gmBlackBoxY + 8
 *   0x24 reserved    always zero
 *
 * Glyph bytes (data_size each) encode an 8x8-padded bitmap:
 *   - High nibble of each byte: alpha 0..f from GetGlyphOutline GGO_GRAY4
 *   - Low nibble: edge/outline intensity 0..f from radial dilation
 *
 * See font_atlas_blit_glyph + font_atlas_dilate for the encoding rules.
 *
 * The codepoint iteration order (= fontidx row index) is:
 *   - 0..0xff:   single-byte codepoints 0x00..0xff (most are blank in
 *                practice — GDI rasterizes nothing for non-printables)
 *   - 0x100..0x21f: 288 special 2-byte SJIS codepoints from the table
 *                   at 0x005cbc7c (full-width punctuation, ASCII, kana
 *                   that the engine wants reachable via single-codepoint
 *                   lookup — see font_atlas_special_table[])
 *   - 0x220 onwards: SJIS double-byte starting at lead-byte 0x88, with
 *                    the gap ranges 0x9ffd..0xe03f, 0xeeed..0xfa56,
 *                    0xfbfd..eof written as zero-records.
 *                    Reader formula: index = sjis_code - 0x861f.
 *
 * Win32 entrypoint `font_atlas_build_win32` writes both files to
 * `out_dir`. Non-_WIN32 builds don't expose it (no GDI on Linux); the
 * pure-C helpers are still compiled so tests can exercise them.
 */

#ifndef OPENRECET_FONT_ATLAS_H
#define OPENRECET_FONT_ATLAS_H

#include <stdint.h>
#include <stddef.h>

#define FONT_ATLAS_RECORD_SIZE 40
#define FONT_ATLAS_SPECIAL_TABLE_BYTES 576        /* 288 entries × 2 bytes */
#define FONT_ATLAS_SPECIAL_TABLE_COUNT 288

struct font_atlas_record {
    uint32_t data_offset;     /* +0x00 — into fontdata.bin               */
    uint32_t data_size;       /* +0x04 — bytes of glyph data (cjBuffer)  */
    int32_t  cell_inc_x;      /* +0x08 — advance: cellIncX + 8 + pad     */
    int32_t  line_height;     /* +0x0c — tmHeight + 8                    */
    int32_t  origin_x;        /* +0x10 — gmptGlyphOrigin.x               */
    int32_t  ascent;          /* +0x14 — tmAscent                        */
    int32_t  origin_y;        /* +0x18 — gmptGlyphOrigin.y               */
    int32_t  tex_width;       /* +0x1c — blackBoxX + 8 + pad             */
    int32_t  tex_height;      /* +0x20 — blackBoxY + 8                   */
    int32_t  reserved;        /* +0x24 — always zero                     */
};

/* Compile-time guard so the on-disk layout matches the reader's stride. */
_Static_assert(sizeof(struct font_atlas_record) == FONT_ATLAS_RECORD_SIZE,
               "font_atlas_record must be exactly 40 bytes on disk");

/*
 * The 288-entry special-codepoint table extracted from the unpacked
 * binary at VA 0x005cbc7c. Two bytes per entry (SJIS high + low).
 *
 * The engine null-terminates the table; we drop the terminator from
 * the embedded copy since the count is fixed.
 */
extern const uint8_t font_atlas_special_table[FONT_ATLAS_SPECIAL_TABLE_BYTES];

/*
 * Blit a GGO_GRAY4 alpha glyph into a padded buffer, mirroring the
 * engine's pre-dilation pass at FUN_0047c474 lines 188-243.
 *
 * Input:
 *   src       — black_x * black_y bytes, alpha 0..f per pixel
 *               (GetGlyphOutline GGO_GRAY4_BITMAP doesn't actually
 *               write values > 64 in practice, but the engine clamps
 *               at 0xf so we match).
 *   src_w     — raw glyph width (gmBlackBoxX, not row-padded)
 *   src_h     — raw glyph height (gmBlackBoxY)
 *
 * Output:
 *   dst       — must be tex_w * tex_h bytes, MUST be zero-filled by
 *               the caller (we don't memset).
 *   tex_w     — padded width: (src_w + 7) rounded up to multiple of 4
 *               with the engine's specific `+8 - blackBoxX%4` math
 *   tex_h     — src_h + 8
 *
 * Each pixel in `src` (with alpha clamped to 15) becomes a high-nibble
 * value in `dst` at offset (row+4, col+4). The low nibble is also set
 * to 0xf when the source alpha is > 0 — the engine's quirky
 * "is-opaque" marker that the texture upload step later overwrites
 * with the dilation edge value.
 *
 * Engine quirks faithfully reproduced:
 *   - The `if (alpha > 0) dst = 0xf; dst += alpha << 4;` sequence (line
 *     223-230). Net result: dst = 0 for transparent, dst = 0xf + alpha*16
 *     = (alpha<<4)|0xf otherwise.
 */
void font_atlas_blit_glyph(uint8_t *dst, int tex_w, int tex_h,
                           const uint8_t *src, int src_w, int src_h);

/*
 * In-place 5x5 radial edge dilation (FUN_0047c474 lines 244-294).
 *
 * For each pixel whose high nibble > 1 (anti-aliased glyph body), paint
 * a halo into neighbors in a 5x5 grid (deltas in [-4..4]², excluding
 * the center). The halo intensity is `15 - floor(dist * edgedel)`,
 * gated by `dist <= edgewi` — both constants pulled from config.idx.
 *
 * Halo lands in the LOW nibble of the neighbor, but only if:
 *   - the neighbor's high nibble is 0 or 1 (transparent / barely-opaque)
 *   - the new halo intensity is brighter than the existing low nibble
 *
 * The neighbor's final byte is `(halo_intensity | 0x10)` — bit 4 set
 * marks the pixel as "edge", bits 0..3 hold the halo value. See the
 * engine comparison at line 276: `(byte & 0xfffffff0) < 0x20`.
 *
 * edgewi / edgedel are floats sourced from `*(float*)&DAT_005cbc74` /
 * `*(float*)&DAT_005cbc78`, set by config.idx's edgewi: / edgedel:
 * keys. Defaults are 2.0 / 6.0 from the vendor file.
 */
void font_atlas_dilate(uint8_t *buf, int tex_w, int tex_h,
                       float edgewi, float edgedel);

/*
 * Pack a fontidx record from raw GDI metrics. Pure layout — no Win32
 * dependency, easily testable.
 *
 *   running_offset — current file offset into fontdata.bin (the
 *                    caller maintains this as a sum of prior data_size)
 *   data_size      — cjBuffer from GetGlyphOutlineA (raw or padded)
 *   cell_inc_x_raw — gmCellIncX  (the +8+pad math is applied here)
 *   tmHeight       — TEXTMETRIC.tmHeight (the +8 is applied here)
 *   tmAscent       — TEXTMETRIC.tmAscent
 *   black_box_x    — gmBlackBoxX  (the +8+pad math is applied here)
 *   black_box_y    — gmBlackBoxY  (the +8 is applied here)
 *   origin_x       — gmptGlyphOrigin.x
 *   origin_y       — gmptGlyphOrigin.y
 */
void font_atlas_pack_record(struct font_atlas_record *out,
                            uint32_t running_offset,
                            uint32_t data_size,
                            int32_t cell_inc_x_raw,
                            int32_t tmHeight,
                            int32_t tmAscent,
                            int32_t black_box_x,
                            int32_t black_box_y,
                            int32_t origin_x,
                            int32_t origin_y);

/*
 * Compute the dst tex_w / tex_h that font_atlas_blit_glyph expects,
 * given a GGO_GRAY4 raw glyph's (blackBoxX, blackBoxY).
 *
 *   tex_w = blackBoxX + 8 + ((-blackBoxX) & 3)   // pad to multiple of 4
 *   tex_h = blackBoxY + 8
 *
 * Provided as a helper so the Win32 driver, tests, and the texture
 * upload all derive the same numbers from the engine's quirky math.
 */
void font_atlas_padded_dim(int black_box_x, int black_box_y,
                           int *out_tex_w, int *out_tex_h);

#ifdef _WIN32

/*
 * Win32 GDI atlas builder. Mirrors FUN_0047c474 end-to-end.
 *
 *   out_dir — directory to write fontdata.bin / fontidx.bin into.
 *             Caller ensures it exists (we CreateDirectoryA-equivalent
 *             if missing; details in font_atlas.c).
 *   face_name — GDI face name (LOGFONTA.lfFaceName). Should come from
 *               g_config.font_name.
 *   edgewi    — engine config: outline radius
 *   edgedel   — engine config: outline falloff
 *   kanji_off — engine quirk: when set, stops before the SJIS double
 *               byte walk. Mirrors the `DAT_005cbc70 == 0` guard at
 *               line 329. From g_config.kanjioff.
 *
 * Returns 1 on full success, 0 on any error (CreateFontIndirect,
 * GetDC, fopen, etc.). Errors are logged to stderr; on partial failure
 * the output files may be incomplete and should be deleted by the
 * caller before retrying.
 */
int font_atlas_build_win32(const char *out_dir,
                           const char *face_name,
                           float edgewi,
                           float edgedel,
                           int kanji_off);

#endif /* _WIN32 */

#endif /* OPENRECET_FONT_ATLAS_H */
