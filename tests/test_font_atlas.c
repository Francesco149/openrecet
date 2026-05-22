/*
 * test_font_atlas.c — unit tests for the pure-C parts of font_atlas.c.
 *
 * The GDI driver (font_atlas_build_win32) is Win32-only and exercised
 * by the scenario runner — not these tests. Here we verify:
 *
 *   - on-disk record layout (offset asserts)
 *   - the 288-entry special table is the right size + null-free
 *   - font_atlas_padded_dim agrees with the engine's mod-4 math
 *   - font_atlas_blit_glyph encoding: alpha→(a<<4)|0xf, 4-pixel border
 *   - font_atlas_dilate: halo lands only on transparent neighbors with
 *     a bigger intensity, never overwrites glyph body
 *   - font_atlas_pack_record: byte-for-byte field placement
 */

#include "t.h"

#include "../src/font_atlas.h"

#include <stddef.h>
#include <string.h>

/* ─── record layout ─────────────────────────────────────────────────── */

int test_font_atlas_record_size_is_40(void)
{
    T_ASSERT_EQ_U(sizeof(struct font_atlas_record), 40);
    return 0;
}

int test_font_atlas_record_field_offsets(void)
{
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, data_offset), 0x00);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, data_size),   0x04);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, cell_inc_x),  0x08);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, line_height), 0x0c);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, origin_x),    0x10);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, ascent),      0x14);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, origin_y),    0x18);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, tex_width),   0x1c);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, tex_height),  0x20);
    T_ASSERT_EQ_U(offsetof(struct font_atlas_record, reserved),    0x24);
    return 0;
}

/* ─── embedded special-codepoint table ──────────────────────────────── */

int test_font_atlas_special_table_size_is_576(void)
{
    T_ASSERT_EQ_U(FONT_ATLAS_SPECIAL_TABLE_BYTES, 576);
    T_ASSERT_EQ_U(FONT_ATLAS_SPECIAL_TABLE_COUNT, 288);
    return 0;
}

int test_font_atlas_special_table_first_entry_is_fullwidth_space(void)
{
    /* SJIS 0x8140 = "　" (full-width space) — the engine's first entry
     * in the special-codepoint table. */
    T_ASSERT_EQ_U(font_atlas_special_table[0], 0x81);
    T_ASSERT_EQ_U(font_atlas_special_table[1], 0x40);
    return 0;
}

int test_font_atlas_special_table_known_punctuation(void)
{
    /* SJIS 0x8149 = "！" (full-width exclamation) — entry index 1. */
    T_ASSERT_EQ_U(font_atlas_special_table[2], 0x81);
    T_ASSERT_EQ_U(font_atlas_special_table[3], 0x49);
    return 0;
}

int test_font_atlas_special_table_no_internal_nul(void)
{
    /* The engine null-terminates after the 288 entries; the bytes
     * within the table should themselves contain no SJIS lead byte of
     * value 0x00 — every entry should be a valid SJIS double-byte. */
    for (int i = 0; i < FONT_ATLAS_SPECIAL_TABLE_BYTES; i += 2) {
        T_ASSERT(font_atlas_special_table[i] != 0x00);
    }
    return 0;
}

/* ─── dimension math ────────────────────────────────────────────────── */

int test_font_atlas_padded_dim_zero_mod_4(void)
{
    int w, h;
    font_atlas_padded_dim(8, 12, &w, &h);
    T_ASSERT_EQ_I(w, 8 + 8 + 0);    /* pad=0 */
    T_ASSERT_EQ_I(h, 12 + 8);
    return 0;
}

int test_font_atlas_padded_dim_one_mod_4(void)
{
    int w, h;
    font_atlas_padded_dim(9, 12, &w, &h);
    T_ASSERT_EQ_I(w, 9 + 8 + 3);    /* pad=3 (-9 & 3 = 3) */
    T_ASSERT_EQ_I(h, 12 + 8);
    return 0;
}

int test_font_atlas_padded_dim_two_mod_4(void)
{
    int w, h;
    font_atlas_padded_dim(10, 12, &w, &h);
    T_ASSERT_EQ_I(w, 10 + 8 + 2);
    T_ASSERT_EQ_I(h, 12 + 8);
    return 0;
}

int test_font_atlas_padded_dim_three_mod_4(void)
{
    int w, h;
    font_atlas_padded_dim(11, 12, &w, &h);
    T_ASSERT_EQ_I(w, 11 + 8 + 1);
    T_ASSERT_EQ_I(h, 12 + 8);
    return 0;
}

/* ─── blit ───────────────────────────────────────────────────────────── */

int test_font_atlas_blit_zeroes_yield_zeroes(void)
{
    /* src all-zero → dst unchanged. */
    uint8_t dst[20 * 20] = {0};
    uint8_t src[8 * 8]  = {0};
    font_atlas_blit_glyph(dst, 20, 20, src, 8, 8);
    for (size_t i = 0; i < sizeof dst; i++) {
        T_ASSERT_EQ_U(dst[i], 0);
    }
    return 0;
}

int test_font_atlas_blit_alpha_to_high_nibble_marker_in_low(void)
{
    /* A single src pixel at (0,0) with alpha=8 should land at
     * dst[(4)*20 + 4] = 0x8f (alpha<<4 | 0xf). */
    uint8_t dst[20 * 20] = {0};
    uint8_t src[4 * 4]  = {0};
    src[0] = 8;
    font_atlas_blit_glyph(dst, 20, 20, src, 4, 4);
    T_ASSERT_EQ_U(dst[4 * 20 + 4], 0x8f);
    /* Border pixels stay 0. */
    T_ASSERT_EQ_U(dst[0], 0);
    T_ASSERT_EQ_U(dst[3 * 20 + 3], 0);
    return 0;
}

int test_font_atlas_blit_clamps_alpha_at_15(void)
{
    /* GGO_GRAY4 returns up to 16; engine clamps to 15. */
    uint8_t dst[20 * 20] = {0};
    uint8_t src[1] = {16};
    font_atlas_blit_glyph(dst, 20, 20, src, 1, 1);
    T_ASSERT_EQ_U(dst[4 * 20 + 4], 0xff);  /* (15<<4)|0xf = 0xff */
    return 0;
}

int test_font_atlas_blit_keeps_alpha_zero_pixels_at_zero(void)
{
    uint8_t dst[20 * 20] = {0};
    uint8_t src[3 * 3]  = {0, 8, 0, 0, 0, 0, 8, 0, 8};
    font_atlas_blit_glyph(dst, 20, 20, src, 3, 3);
    /* (0,0) alpha=0 → dst stays 0. */
    T_ASSERT_EQ_U(dst[4 * 20 + 4], 0);
    /* (0,1) alpha=8 → dst = 0x8f. */
    T_ASSERT_EQ_U(dst[4 * 20 + 5], 0x8f);
    return 0;
}

int test_font_atlas_blit_skips_when_dst_too_small(void)
{
    /* dst smaller than src + 8 → no-op (guard). */
    uint8_t dst[8 * 8] = {0};
    uint8_t src[4 * 4] = {0};
    src[0] = 8;
    font_atlas_blit_glyph(dst, 8, 8, src, 4, 4);   /* needs 12x12 min */
    /* Should be a no-op since 8 < 4+8. */
    for (size_t i = 0; i < sizeof dst; i++) {
        T_ASSERT_EQ_U(dst[i], 0);
    }
    return 0;
}

/* ─── dilate ─────────────────────────────────────────────────────────── */

int test_font_atlas_dilate_no_glyph_no_change(void)
{
    /* Empty buffer → no dilation. */
    uint8_t buf[20 * 20] = {0};
    font_atlas_dilate(buf, 20, 20, 2.0f, 6.0f);
    for (size_t i = 0; i < sizeof buf; i++) {
        T_ASSERT_EQ_U(buf[i], 0);
    }
    return 0;
}

int test_font_atlas_dilate_glyph_body_unchanged(void)
{
    /* A glyph pixel (high nibble 8) at (5, 5) should remain at 0x8f
     * after dilation (only neighbors change). */
    uint8_t buf[20 * 20] = {0};
    buf[5 * 20 + 5] = 0x8f;
    font_atlas_dilate(buf, 20, 20, 2.0f, 6.0f);
    T_ASSERT_EQ_U(buf[5 * 20 + 5], 0x8f);
    return 0;
}

int test_font_atlas_dilate_neighbor_gets_intensity_15_within_edgewi(void)
{
    /* Glyph at (10, 10). edgewi=2, edgedel=6. Distance 1 → within
     * edgewi, intensity 15. The byte at (10, 11) should be 0x1f
     * (high=1 (since |0x10), low=15). */
    uint8_t buf[24 * 24] = {0};
    buf[10 * 24 + 10] = 0xff;  /* full glyph */
    font_atlas_dilate(buf, 24, 24, 2.0f, 6.0f);
    T_ASSERT_EQ_U(buf[10 * 24 + 11], 0x1f);
    /* Distance √2 ≈ 1.41 → still within edgewi=2, intensity 15. */
    T_ASSERT_EQ_U(buf[11 * 24 + 11], 0x1f);
    return 0;
}

int test_font_atlas_dilate_neighbor_falloff_outside_edgewi(void)
{
    /* Glyph at (10, 10). Distance 3 → outside edgewi=2.
     * intensity = 15 - floor(3*6) = 15 - 18 = -3 → skip. */
    uint8_t buf[24 * 24] = {0};
    buf[10 * 24 + 10] = 0xff;
    font_atlas_dilate(buf, 24, 24, 2.0f, 6.0f);
    T_ASSERT_EQ_U(buf[10 * 24 + 13], 0);
    return 0;
}

int test_font_atlas_dilate_skip_low_alpha_propagation(void)
{
    /* A pixel with alpha=1 (high nibble = 1, byte = 0x1f) is NOT a
     * glyph body for dilation purposes — it should not splat halo. */
    uint8_t buf[20 * 20] = {0};
    buf[10 * 20 + 10] = 0x1f;   /* fringe pixel */
    font_atlas_dilate(buf, 20, 20, 2.0f, 6.0f);
    /* Neighbors stay 0. */
    T_ASSERT_EQ_U(buf[10 * 20 + 11], 0);
    T_ASSERT_EQ_U(buf[9  * 20 + 10], 0);
    return 0;
}

int test_font_atlas_dilate_does_not_overwrite_glyph_body(void)
{
    /* Two adjacent glyph pixels (10, 10) and (10, 11) at alpha=8.
     * Neither should be overwritten by the other's halo. */
    uint8_t buf[24 * 24] = {0};
    buf[10 * 24 + 10] = 0x8f;
    buf[10 * 24 + 11] = 0x8f;
    font_atlas_dilate(buf, 24, 24, 2.0f, 6.0f);
    T_ASSERT_EQ_U(buf[10 * 24 + 10], 0x8f);
    T_ASSERT_EQ_U(buf[10 * 24 + 11], 0x8f);
    return 0;
}

int test_font_atlas_dilate_neighbor_keeps_brighter_existing_halo(void)
{
    /* Pre-stamp neighbor (10,11) with intensity 15 from a closer source.
     * A second glyph pixel (5, 11) at distance 5 — too far to reach (10,11).
     * Build state: neighbor stays 0x1f.
     *
     * Then put another glyph at distance 2 (8,11) and verify halo
     * doesn't DOWNgrade the existing 0x1f. */
    uint8_t buf[24 * 24] = {0};
    buf[10 * 24 + 11] = 0x1f;   /* pre-existing edge */
    buf[8  * 24 + 11] = 0xff;   /* new glyph 2 rows away */
    font_atlas_dilate(buf, 24, 24, 2.0f, 6.0f);
    /* (10,11) already at 0x1f. Glyph at (8,11) is dist=2 → edgewi
     * inclusive → intensity 15. Halo would attempt to set 0x1f.
     * Engine gate: `(neighbor & 0xf) < iVar8` (strictly less). Existing
     * low nibble = 15 = iVar8 → no overwrite. */
    T_ASSERT_EQ_U(buf[10 * 24 + 11], 0x1f);
    return 0;
}

/* ─── record packing ─────────────────────────────────────────────────── */

int test_font_atlas_pack_record_basic(void)
{
    struct font_atlas_record rec;
    font_atlas_pack_record(&rec,
        /*running_offset=*/100,
        /*data_size=*/200,
        /*cell_inc_x_raw=*/12,
        /*tmHeight=*/30,
        /*tmAscent=*/22,
        /*black_box_x=*/9,        /* mod 4 = 1 → pad=3 */
        /*black_box_y=*/14,
        /*origin_x=*/1,
        /*origin_y=*/24);
    T_ASSERT_EQ_U(rec.data_offset, 100);
    T_ASSERT_EQ_U(rec.data_size,   200);
    T_ASSERT_EQ_I(rec.cell_inc_x,  12 + 8 + 3);   /* +8+pad */
    T_ASSERT_EQ_I(rec.line_height, 30 + 8);
    T_ASSERT_EQ_I(rec.origin_x,    1);
    T_ASSERT_EQ_I(rec.ascent,      22);
    T_ASSERT_EQ_I(rec.origin_y,    24);
    T_ASSERT_EQ_I(rec.tex_width,   9 + 8 + 3);
    T_ASSERT_EQ_I(rec.tex_height,  14 + 8);
    T_ASSERT_EQ_I(rec.reserved,    0);
    return 0;
}

int test_font_atlas_pack_record_zero_box_zero_pad(void)
{
    struct font_atlas_record rec;
    font_atlas_pack_record(&rec, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    /* All-zero inputs (other than the +8 padding) lands at tex_w=tex_h=8
     * — empty glyph still gets its 4px border. */
    T_ASSERT_EQ_I(rec.tex_width,  8);
    T_ASSERT_EQ_I(rec.tex_height, 8);
    T_ASSERT_EQ_I(rec.cell_inc_x, 8);
    return 0;
}

/* ─── loader ────────────────────────────────────────────────────────── */

#include <unistd.h>
#include <sys/stat.h>

static void write_file(const char *path, const void *buf, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f) { fwrite(buf, 1, n, f); fclose(f); }
}

int test_font_atlas_load_missing_returns_zero(void)
{
    font_atlas_free();
    /* Point at a non-existent directory. */
    int rc = font_atlas_load("/tmp/openrecet_test_nope_does_not_exist_xyz");
    T_ASSERT_EQ_I(rc, 0);
    T_ASSERT(g_font_atlas.fontdata == NULL);
    T_ASSERT(g_font_atlas.fontidx == NULL);
    T_ASSERT_EQ_U(g_font_atlas.fontdata_size, 0);
    T_ASSERT_EQ_U(g_font_atlas.fontidx_count, 0);
    return 0;
}

int test_font_atlas_load_basic_roundtrip(void)
{
    font_atlas_free();
    /* Build a 2-record atlas in a tmp dir. */
    char dir[256];
    snprintf(dir, sizeof dir, "/tmp/openrecet_test_atlas_%d", (int)getpid());
    mkdir(dir, 0755);

    struct font_atlas_record recs[2] = {0};
    font_atlas_pack_record(&recs[0], 0,   0, 0,  0, 0, 0,  0, 0, 0);
    font_atlas_pack_record(&recs[1], 0,  16, 8, 30, 22, 8, 9, 1, 24);

    uint8_t data_blob[16] = {1,2,3,4,5,6,7,8, 9,10,11,12,13,14,15,16};
    char path_data[512], path_idx[512];
    snprintf(path_data, sizeof path_data, "%s/fontdata.bin", dir);
    snprintf(path_idx,  sizeof path_idx,  "%s/fontidx.bin",  dir);
    write_file(path_data, data_blob, sizeof data_blob);
    write_file(path_idx, recs, sizeof recs);

    int rc = font_atlas_load(dir);
    T_ASSERT_EQ_I(rc, 1);
    T_ASSERT_EQ_U(g_font_atlas.fontdata_size, 16);
    T_ASSERT_EQ_U(g_font_atlas.fontidx_count, 2);
    T_ASSERT_MEM_EQ(g_font_atlas.fontdata, data_blob, sizeof data_blob);
    T_ASSERT_EQ_I(g_font_atlas.fontidx[1].data_size, 16);
    T_ASSERT_EQ_I(g_font_atlas.fontidx[1].tex_width, 8 + 8 + ((-8) & 3));

    /* Free, cleanup. */
    font_atlas_free();
    T_ASSERT(g_font_atlas.fontdata == NULL);

    unlink(path_data); unlink(path_idx); rmdir(dir);
    return 0;
}

int test_font_atlas_load_rejects_bad_idx_size(void)
{
    font_atlas_free();
    char dir[256];
    snprintf(dir, sizeof dir, "/tmp/openrecet_test_atlas_bad_%d", (int)getpid());
    mkdir(dir, 0755);

    char path_data[512], path_idx[512];
    snprintf(path_data, sizeof path_data, "%s/fontdata.bin", dir);
    snprintf(path_idx,  sizeof path_idx,  "%s/fontidx.bin",  dir);
    uint8_t zero = 0;
    write_file(path_data, &zero, 1);
    /* 33 bytes — not a multiple of 40. */
    uint8_t garbage[33] = {0};
    write_file(path_idx, garbage, sizeof garbage);

    int rc = font_atlas_load(dir);
    T_ASSERT_EQ_I(rc, 0);
    T_ASSERT(g_font_atlas.fontdata == NULL);

    unlink(path_data); unlink(path_idx); rmdir(dir);
    return 0;
}

int test_font_atlas_free_is_idempotent(void)
{
    font_atlas_free();
    font_atlas_free();  /* should not crash */
    T_ASSERT(g_font_atlas.fontdata == NULL);
    return 0;
}
