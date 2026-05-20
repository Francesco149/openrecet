/*
 * test_lnk_lzss.c — unit tests for the lnkdatas LZSS decoder.
 *
 * Coverage:
 *   1. Single literal               (ctrl bit 0)
 *   2. All-literal control byte     (ctrl = 0x00 — engine fast path)
 *   3. Short back-reference         (length 2..16 via low nibble)
 *   4. Extended-length back-ref     (low nibble 0 + extra byte)
 *   5. Self-overlap (RLE pattern)   (back == 1, long length)
 *   6. End-of-stream sentinel       (back == 0 terminates)
 *   7. Mixed flags within one ctrl  (every other bit literal/back)
 *
 * Plus a vendor-dependent round-trip (skipped if vendor/original is
 * absent) that iterates every lnkdatas entry, reads its slice from the
 * data*.bin stream, and verifies the decompressed length matches the
 * declared `dsize` field — the same structural check used by
 * test_bmp_lzw.
 */
#include "t.h"
#include "lnk_lzss.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ─── 1. single literal ──────────────────────────────────────────────────
 * ctrl=0x00 means "all 8 flags are literal" — but we only consume the
 * first one before hitting the end-of-stream marker.  Verifies that
 * literals are emitted in stream order.
 *
 * Stream:
 *   0x00            ctrl: 8 literal flags
 *   0x41 ... 0x48   literal bytes 'A'..'H'
 *   0x80 0x00 0x00  back-ref with back=0 → end-of-stream
 */
int test_lnk_lzss_single_literal(void)
{
    uint8_t in[] = {
        0x00,
        'A','B','C','D','E','F','G','H',
        0x80, 0x00, 0x00,
    };
    uint8_t out[16] = {0};
    size_t n = lnk_lzss_decompress(in, out);

    T_ASSERT_EQ_U(n, 8);
    T_ASSERT_MEM_EQ(out, "ABCDEFGH", 8);
    return 0;
}

/* ─── 2. short back-reference ────────────────────────────────────────────
 * Encode "ABCABC": three literals, then back-ref (back=3, length nibble=2 →
 * copy length+1 = 3 bytes).
 *
 * Stream:
 *   0xE0            ctrl: lit, lit, lit, back, end, ...
 *                   bits 7..0 = 1110 0000 — but we only use first 5
 *   wait — we want lit,lit,lit,back. That's bits 0,0,0,1 (msb first).
 *   So ctrl = 0b0001_xxxx where x = end-of-stream is encoded as another
 *   back-ref flag with back=0.  Use ctrl = 0001_1000 = 0x18:
 *     bit7=0 lit, bit6=0 lit, bit5=0 lit, bit4=1 back, bit3=1 end,
 *     bit2..0 unused (the back-ref-with-back==0 returns immediately).
 *
 *   0x18            ctrl
 *   'A' 'B' 'C'     three literals
 *   0x02 0x03       b1=0x02 (high nibble 0 → back high bits=0;
 *                            low nibble 2 → length = 2)
 *                   b2=0x03 → back = (0<<8) | 3 = 3
 *                   so back=3, length+1 = 3 bytes copied from out-3
 *   0x00 0x00       end (back == 0)
 */
int test_lnk_lzss_back_reference_short(void)
{
    uint8_t in[] = {
        0x18,
        'A','B','C',
        0x02, 0x03,
        0x00, 0x00,
    };
    uint8_t out[16] = {0};
    size_t n = lnk_lzss_decompress(in, out);

    T_ASSERT_EQ_U(n, 6);
    T_ASSERT_MEM_EQ(out, "ABCABC", 6);
    return 0;
}

/* ─── 3. extended-length back-reference ──────────────────────────────────
 * Encode "AAA...A" × 100: 1 literal 'A', then back-ref with back=1,
 * length nibble = 0 → extended length encoding.  Extended length formula
 * is `length = next_byte + 16`, and the copy emits `length + 1` bytes.
 * So to emit 99 'A's after the initial 'A' (100 total) we need
 * length+1 = 99 → length = 98 → next_byte = 82.
 *
 * Stream:
 *   ctrl bits MSB-first: lit, back, end
 *     bit7=0, bit6=1, bit5=1, rest=don't-care
 *   ctrl = 0b0110_0000 = 0x60
 *
 *   0x60            ctrl
 *   'A'             literal
 *   0x00 0x01 0x52  back-ref: b1=0x00 → back high bits=0, length nibble=0
 *                   b2=0x01 → back = 1
 *                   b3=0x52 = 82 → length = 82 + 16 = 98, copy 99 bytes
 *   0x00 0x00       end
 */
int test_lnk_lzss_back_reference_extended(void)
{
    uint8_t in[] = {
        0x60,
        'A',
        0x00, 0x01, 0x52,
        0x00, 0x00,
    };
    uint8_t out[128] = {0};
    size_t n = lnk_lzss_decompress(in, out);

    T_ASSERT_EQ_U(n, 100);
    for (size_t i = 0; i < 100; i++) {
        if (out[i] != 'A') T_FAIL("byte %zu = 0x%02x, expected 'A'", i, out[i]);
    }
    return 0;
}

/* ─── 4. self-overlap RLE pattern ───────────────────────────────────────
 * Verifies that copying with back < length+1 correctly propagates the
 * just-written bytes — that's how the format spells "repeat the last K
 * bytes N times".
 *
 * Encode "ABABAB": 2 literals 'A' 'B', then back-ref with back=2,
 * length nibble = 3 → length+1 = 4 bytes copied.  The copy reads out[-2],
 * out[-1], then out[-2] which is now the *just-written* byte from the
 * previous iteration.
 *
 * Stream:
 *   ctrl bits MSB-first: lit, lit, back, end → 0b0011_0000 = 0x30
 *   0x30
 *   'A' 'B'
 *   0x03 0x02       b1=0x03 (high nib 0; low nib 3 → length=3, copy 4)
 *                   b2=0x02 → back=2
 *   0x00 0x00       end
 */
int test_lnk_lzss_self_overlap(void)
{
    uint8_t in[] = {
        0x30,
        'A','B',
        0x03, 0x02,
        0x00, 0x00,
    };
    uint8_t out[16] = {0};
    size_t n = lnk_lzss_decompress(in, out);

    T_ASSERT_EQ_U(n, 6);
    T_ASSERT_MEM_EQ(out, "ABABAB", 6);
    return 0;
}

/* ─── 5. end-of-stream is the only terminator ────────────────────────────
 * Demonstrates that decoding stops at back==0 mid-control-byte, not at
 * the end of the 8 flag bits.  Output should be exactly the literals
 * before the end marker.
 */
int test_lnk_lzss_end_of_stream_mid_ctrl(void)
{
    /* ctrl=0b0100_0000 = 0x40: lit, end, (rest unread)
     * Output: just 'X'. */
    uint8_t in[] = {
        0x40,
        'X',
        0x00, 0x00,
    };
    uint8_t out[8] = {0};
    /* Pre-fill with sentinel so we catch accidental writes past EOS. */
    memset(out, 0xCC, sizeof out);

    size_t n = lnk_lzss_decompress(in, out);
    T_ASSERT_EQ_U(n, 1);
    T_ASSERT_EQ_U(out[0], (unsigned)'X');
    T_ASSERT_EQ_U(out[1], 0xCC);
    return 0;
}

/* ─── 6. high-bit back-distance ──────────────────────────────────────────
 * Verifies that the upper 4 bits of the back distance come from
 * b1 & 0xF0 (shifted to bits 11..8), not somewhere else.
 *
 * Pre-fill the dictionary by writing 256 distinct bytes (one full ctrl
 * group of 8 literals × 32), then back-reference at distance 256 with
 * length 1.
 *
 * To get back = 256 = 0x100: high nibble of b1 = 1 → b1 = 0x10 + low nib.
 * (1 << 8) | 0 = 256.  Length 1 (b1 low nib = 1) → 2-byte copy fast path.
 *
 * That copies out[-256], out[-255] — i.e. bytes 0 and 1 of the output.
 */
int test_lnk_lzss_back_high_bits(void)
{
    uint8_t in[400];
    size_t  ip = 0;

    /* Emit 256 literal bytes, value = position low byte.  Each ctrl byte
     * (0x00) covers 8 literals → 32 ctrl groups. */
    for (int g = 0; g < 32; g++) {
        in[ip++] = 0x00;
        for (int b = 0; b < 8; b++) {
            in[ip++] = (uint8_t)((g * 8) + b);
        }
    }

    /* Now emit a ctrl byte where bit 7 = back-ref, then end-of-stream. */
    in[ip++] = 0xC0;           /* 1100 0000: back, end */
    in[ip++] = 0x11;           /* b1: high nib 1 → back hi = 1; low nib 1 → length 1 */
    in[ip++] = 0x00;           /* b2: back lo = 0 → back = 0x100 = 256 */
    in[ip++] = 0x00;           /* b1 of end marker: high nib 0 */
    in[ip++] = 0x00;           /* b2 of end marker: back == 0 */

    uint8_t out[300] = {0};
    size_t n = lnk_lzss_decompress(in, out);

    /* 256 literals + 2 bytes from the back-ref = 258. */
    T_ASSERT_EQ_U(n, 258);
    /* The first 256 bytes should be 0..255. */
    for (int i = 0; i < 256; i++) {
        if (out[i] != (uint8_t)i) {
            T_FAIL("literal at %d = 0x%02x, expected 0x%02x",
                   i, out[i], (unsigned)(uint8_t)i);
        }
    }
    /* out[256] = out[256-256] = out[0] = 0; out[257] = out[1] = 1. */
    T_ASSERT_EQ_U(out[256], 0);
    T_ASSERT_EQ_U(out[257], 1);
    return 0;
}

/* ─── 7. mixed flags within a single control byte ───────────────────────
 * Stream: lit 'A', back-ref(back=1, 2-byte copy of 'A's), lit 'B', end.
 * Expected output: "AAAB".  Exercises bit-walking past more than one
 * flag transition inside the same ctrl byte.
 *
 * ctrl bits MSB-first: lit, back, lit, back(end)
 *   = 0b0101_0000 = 0x50
 */
int test_lnk_lzss_mixed_flags(void)
{
    uint8_t in[] = {
        0x50,
        'A',
        0x01, 0x01,    /* b1=0x01: hi=0, lo=1 (length 1 → 2-byte copy);
                          b2=0x01: back=1 → both copied bytes are 'A' */
        'B',
        0x00, 0x00,    /* end */
    };
    uint8_t out[16] = {0};
    size_t n = lnk_lzss_decompress(in, out);

    T_ASSERT_EQ_U(n, 4);
    T_ASSERT_MEM_EQ(out, "AAAB", 4);
    return 0;
}

/* ─── vendor round-trip ─────────────────────────────────────────────────
 * Iterates every entry in vendor/original/lnkdatas.bin, reads its
 * compressed slice from bin/data*.bin (spanning 10 MiB chunk boundaries
 * as needed), decompresses, and verifies the result's length matches
 * the declared `dsize`.
 *
 * Like the bmp_lzw vendor test, this catches any silent truncation or
 * over-read bug.  Pixel-exact verification is out of scope here — the
 * extractor already validates against recettear-repacker via
 * `tools/extract/data-bin.py --validate-against …`.
 */
#define VENDOR_ROOT       OPENRECET_ROOT "/vendor/original"
#define LNKDATAS_PATH     VENDOR_ROOT "/lnkdatas.bin"
#define BIN_CHUNK_SIZE    ((size_t)0xa00000u)   /* 10 MiB */

static uint8_t *vendor_slurp(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return NULL; }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return NULL;
    }
    fclose(fp);
    *out_size = (size_t)sz;
    return buf;
}

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

/* Mini DataStream — 1-deep FILE* cache, mirrors data_stream_read in
 * src/storage.c but for the host-side test driver (no Win32 dep). */
typedef struct {
    FILE *fp;
    int   idx;
} vendor_stream;

static int vendor_stream_read(vendor_stream *vs, size_t offset,
                              size_t length, void *dst_)
{
    uint8_t *dst = (uint8_t *)dst_;
    size_t   rem = length;
    size_t   cur = offset;

    while (rem > 0) {
        int    file_idx = (int)(cur / BIN_CHUNK_SIZE);
        size_t file_off = cur % BIN_CHUNK_SIZE;

        if (vs->fp == NULL || vs->idx != file_idx) {
            if (vs->fp) { fclose(vs->fp); vs->fp = NULL; }
            char path[256];
            snprintf(path, sizeof path,
                     VENDOR_ROOT "/bin/data%03d.bin", file_idx);
            vs->fp = fopen(path, "rb");
            if (!vs->fp) return 0;
            vs->idx = file_idx;
        }
        if (fseek(vs->fp, (long)file_off, SEEK_SET) != 0) return 0;

        size_t avail = BIN_CHUNK_SIZE - file_off;
        size_t take  = rem < avail ? rem : avail;
        if (fread(dst, 1, take, vs->fp) != take) return 0;

        dst += take; rem -= take; cur += take;
    }
    return 1;
}

int test_lnk_lzss_vendor_round_trip(void)
{
    size_t  idx_size;
    uint8_t *idx = vendor_slurp(LNKDATAS_PATH, &idx_size);
    if (!idx) T_SKIP("vendor lnkdatas.bin not present at %s", LNKDATAS_PATH);

    if (idx_size < 4) { free(idx); T_FAIL("lnkdatas.bin too small"); }

    /* lnkdatas.bin has either a 5-byte JP obfuscation header + XOR'd
     * payload, or a plain EN payload with the n_items i32 BE at offset
     * 0.  We only support the EN file here (most users on Steam) —
     * skip if the count is implausible.  Real validation lives in
     * src/storage.c. */
    uint32_t n = rd_be32(idx);
    if (n == 0 || n > 0x10000u) {
        free(idx);
        T_SKIP("lnkdatas.bin doesn't look like the EN format (n=%u)", n);
    }

    const size_t entry_stride = 140;       /* 128-byte name + 3×i32 */
    if ((size_t)4 + (size_t)n * entry_stride > idx_size) {
        free(idx);
        T_FAIL("lnkdatas index truncated (n=%u, file=%zu)", n, idx_size);
    }

    vendor_stream vs = { NULL, -1 };

    int     checked = 0;
    int     io_skipped = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = idx + 4 + (size_t)i * entry_stride;
        uint32_t dsize  = rd_be32(e + 0x80);
        uint32_t doff   = rd_be32(e + 0x84);
        uint32_t csize  = rd_be32(e + 0x88);

        if (csize == 0 || dsize == 0) continue;

        /* Allocate +1 sentinel byte so we can detect a one-byte overflow. */
        uint8_t *cbuf = (uint8_t *)malloc(csize);
        uint8_t *dbuf = (uint8_t *)malloc((size_t)dsize + 1);
        if (!cbuf || !dbuf) {
            free(cbuf); free(dbuf);
            free(idx); if (vs.fp) fclose(vs.fp);
            T_FAIL("OOM (csize=%u dsize=%u)", csize, dsize);
        }
        dbuf[dsize] = 0xCC;        /* canary */

        if (!vendor_stream_read(&vs, doff, csize, cbuf)) {
            /* First missing chunk → skip the whole test cleanly. */
            free(cbuf); free(dbuf);
            io_skipped = 1;
            break;
        }

        size_t got     = lnk_lzss_decompress(cbuf, dbuf);
        uint8_t canary = dbuf[dsize];
        if (got != dsize || canary != 0xCC) {
            char name[129];
            memcpy(name, e, 128); name[128] = '\0';
            free(cbuf); free(dbuf);
            free(idx); if (vs.fp) fclose(vs.fp);
            T_FAIL("entry %u (%s): got %zu, expected %u (canary=0x%02x)",
                   i, name, got, dsize, canary);
        }

        free(cbuf); free(dbuf);
        checked++;
    }

    if (vs.fp) fclose(vs.fp);
    free(idx);

    if (io_skipped && checked == 0) {
        T_SKIP("vendor bin/data*.bin not present");
    }
    if (checked < 1) T_FAIL("no entries checked");
    return 0;
}
