/*
 * bmp_lzw.c — LZW decompressor for bmpdata.bin slices.
 *
 * Translation of FUN_00434b32 + FUN_00434c2c + FUN_00434ca9.  The engine
 * keeps the bit-reader state, dictionary, and staging buffer in globals
 * (DAT_0437bb54 / DAT_0437bb58 / DAT_0437bb64 / DAT_0437c774 / DAT_0437a758
 * / DAT_0437a75c / DAT_0438abe8 / DAT_0438abec / DAT_0438abf0); we keep
 * them on the stack of the decode function since there is no caller-visible
 * difference and the engine never recurses or runs two decodes in parallel.
 *
 * Validated against /opt/src/recettear-repacker/bmp_unpack.py — byte-for-byte
 * identical output on the current Steam build's bmpdata.bin.
 *
 * Standalone test build:
 *   i686-w64-mingw32-gcc -DBMP_LZW_TEST_MAIN src/bmp_lzw.c \
 *       -o /tmp/bmp_lzw_test.exe
 */

#include <stdint.h>
#include <string.h>

#include "bmp_lzw.h"

/* ─── format constants ────────────────────────────────────────────────────── */

/* Codes 257..(257+3838) are dictionary entries.  After 3839 successful adds
 * the engine stops adding (see FUN_00434b32: `if ((int)local_10 < 0x4387b5d)`
 * — the table extent is exactly 3839 * 12 bytes plus a 5-byte slack that
 * still passes the bound check for one final add, but the math works out to
 * 3839 entries; same as bmp_unpack.py's `if dict_size < 3839`). */
#define BMP_LZW_DICT_SIZE   3839
#define BMP_LZW_CODE_BITS   12
#define BMP_LZW_RESET_CODE  0x100   /* literal value 256 — exit on first read */
#define BMP_LZW_FIRST_DICT  0x101   /* first assignable dictionary code (257) */

/* Worst-case staging-buffer length:
 *   - longest chain = BMP_LZW_DICT_SIZE entries deep
 *   - plus the leaf literal at the chain tail
 *   - plus the pre-set byte used by the KwK (code-equals-next-to-add) case
 * Round up to a power of two for stack alignment.                            */
#define BMP_LZW_STAGING_MAX  4096

/* ─── dictionary entry ────────────────────────────────────────────────────── */

/* Engine layout: 12-byte entries with prev_code at offset 0 (uint32) and a
 * single char at offset 4 (with 7 trailing pad bytes).  We pack to 4 bytes
 * — the redundant bytes are never read.                                      */
typedef struct {
    uint16_t prev;
    uint8_t  ch;
    uint8_t  _pad;
} bmp_lzw_entry;

/* ─── bit reader: 12-bit MSB-first ────────────────────────────────────────── */

/* Mirrors FUN_00434c2c exactly.
 *
 * State invariants:
 *   - br->buf holds the most recently fetched source byte (or 0 initially).
 *   - br->buf_bits is the count of unconsumed bits in the low end of br->buf
 *     (0..8).  When buf_bits == 0 the buffer is logically empty.
 *   - br->eof becomes 1 once a refill ran past src_len; further reads keep
 *     returning bits from the stale buf (matches the engine, which never
 *     resets DAT_0438abec on EOF).                                           */
typedef struct {
    const uint8_t *src;
    size_t         src_len;
    size_t         src_pos;
    uint32_t       buf;
    int            buf_bits;
    int            eof;
} bmp_lzw_bits;

static uint32_t bmp_lzw_read_bits(bmp_lzw_bits *br, int n)
{
    uint32_t acc = 0;
    int      need = n;

    while (need > br->buf_bits) {
        /* Consume all remaining bits in buf as the high portion of acc. */
        need -= br->buf_bits;
        if (br->buf_bits > 0) {
            uint32_t low = br->buf & ((1u << br->buf_bits) - 1u);
            acc |= low << need;
        }

        /* Refill one byte (or note EOF). */
        if (br->src_pos < br->src_len) {
            br->buf = br->src[br->src_pos++];
        } else {
            br->eof = 1;
        }
        br->buf_bits = 8;
    }

    /* Slice the top `need` bits out of buf. */
    br->buf_bits -= need;
    acc |= (br->buf >> br->buf_bits) & ((1u << need) - 1u);
    return acc;
}

/* ─── dictionary-chain walker (KwK-aware expand) ─────────────────────────── */

/* Mirrors FUN_00434ca9.  Walks the parent chain of `code`, writing one char
 * per step at increasing offsets in `staging` (so the result is REVERSED).
 * Returns the new write position — i.e. (start + length-just-written).      */
static int bmp_lzw_expand(uint8_t *staging, int start, uint32_t code,
                          const bmp_lzw_entry *dict)
{
    int pos = start;
    while (code > 0xff) {
        const bmp_lzw_entry *e = &dict[code - BMP_LZW_FIRST_DICT];
        staging[pos++] = e->ch;
        code = e->prev;
    }
    staging[pos++] = (uint8_t)code;
    return pos;
}

/* ─── main decode loop ────────────────────────────────────────────────────── */

size_t bmp_lzw_decompress(const void *src, size_t csize, void *dst)
{
    bmp_lzw_bits  br = {
        .src = (const uint8_t *)src,
        .src_len = csize,
        .src_pos = 0,
        .buf = 0,
        .buf_bits = 0,
        .eof = 0,
    };
    bmp_lzw_entry dict[BMP_LZW_DICT_SIZE];
    uint8_t       staging[BMP_LZW_STAGING_MAX];
    uint8_t      *out = (uint8_t *)dst;
    size_t        out_pos = 0;

    uint32_t next_code = BMP_LZW_FIRST_DICT;   /* engine: local_8 */
    int      n_entries = 0;

    /* First code: literal byte unless it's the reset marker. */
    uint32_t code = bmp_lzw_read_bits(&br, BMP_LZW_CODE_BITS);
    if (code == BMP_LZW_RESET_CODE) {
        return 0;
    }
    out[out_pos++] = (uint8_t)code;

    /* Track "first char of the just-emitted string" for the KwK rule. */
    uint8_t  prev_first_char = (uint8_t)code;
    uint32_t prev_code       = code;   /* engine: uVar5 */
    uint32_t cur_code        = bmp_lzw_read_bits(&br, BMP_LZW_CODE_BITS);

    while (!br.eof) {
        /* Code 256 is the reset / end-of-stream marker.  Encoders emit it
         * as a sentinel before the input bits run out.  Matches
         * bmp_unpack.py's `if code == 256: dict_size = 0; continue` — we
         * skip emission entirely and reset the dictionary counter.  The
         * original engine doesn't handle this branch and walks past the
         * dict array on 256; the game tolerates the resulting garbage
         * bytes because no file in the shipping bmpdata.bin uses 256
         * outside the trailing sentinel (which the engine just overshoots
         * dsize on).  We can't tolerate that overflow — we honor 256.     */
        if (cur_code == BMP_LZW_RESET_CODE) {
            n_entries = 0;
            next_code = BMP_LZW_FIRST_DICT;
            cur_code  = bmp_lzw_read_bits(&br, BMP_LZW_CODE_BITS);
            continue;
        }

        /* KwK (code-equals-next-to-add): the current code refers to an
         * entry the encoder is about to build with us, so its expansion is
         * (prev_string + prev_string[0]).  Pre-set staging[0] to
         * prev_first_char and expand prev_code from offset 1.               */
        int      kwk = (cur_code >= next_code);
        uint32_t to_expand = cur_code;
        int      start = 0;

        if (kwk) {
            staging[0] = prev_first_char;
            to_expand = prev_code;
            start = 1;
        }

        int len = bmp_lzw_expand(staging, start, to_expand, dict);

        /* staging[len-1] is the leaf literal of `to_expand` — i.e. the
         * first char of the just-decoded string (in either branch).         */
        uint8_t first_char = staging[len - 1];

        /* Emit staging in forward order (it was written in reverse). */
        for (int i = len - 1; i >= 0; --i) {
            out[out_pos++] = staging[i];
        }

        /* Add a new dictionary entry: (prev=prev_code, char=first_char).
         * Freezes at BMP_LZW_DICT_SIZE.                                     */
        if (n_entries < BMP_LZW_DICT_SIZE) {
            dict[n_entries].prev = (uint16_t)prev_code;
            dict[n_entries].ch   = first_char;
            n_entries++;
            next_code++;
        }

        prev_first_char = first_char;
        prev_code       = cur_code;
        cur_code        = bmp_lzw_read_bits(&br, BMP_LZW_CODE_BITS);
    }

    return out_pos;
}

/* ─── standalone test harness ────────────────────────────────────────────── */
/*
 * Reads a raw LZW-coded buffer from stdin and writes the decompressed bytes
 * to stdout.  Lets a fish/bash script feed any slice and diff against
 * recettear-repacker.  Build:
 *
 *   i686-w64-mingw32-gcc -DBMP_LZW_TEST_MAIN -O2 -Wall -Wextra \
 *       src/bmp_lzw.c -o /tmp/bmp_lzw_test.exe
 *
 * (Use under `wine /tmp/bmp_lzw_test.exe < compressed.bin > out.bin`.)
 */
#ifdef BMP_LZW_TEST_MAIN
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <io.h>

int main(void)
{
    /* Binary stdio under Windows runtimes. */
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    /* Slurp stdin. */
    size_t   cap = 1 << 20, len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) return 1;
    for (;;) {
        if (len == cap) {
            cap *= 2;
            uint8_t *nb = (uint8_t *)realloc(buf, cap);
            if (!nb) { free(buf); return 1; }
            buf = nb;
        }
        size_t n = fread(buf + len, 1, cap - len, stdin);
        if (n == 0) break;
        len += n;
    }

    /* Worst-case expansion: a 12-bit code expands to up to BMP_LZW_DICT_SIZE
     * chars.  Each 12-bit code occupies 1.5 bytes, so worst case is
     * (len * 2 / 3) codes * BMP_LZW_DICT_SIZE chars.  Cap allocation at
     * something reasonable for the typical bmpdata slices.                  */
    size_t out_cap = (size_t)len * 64 + 1024;
    uint8_t *out = (uint8_t *)malloc(out_cap);
    if (!out) { free(buf); return 1; }

    size_t dsize = bmp_lzw_decompress(buf, len, out);
    fwrite(out, 1, dsize, stdout);

    free(out);
    free(buf);
    return 0;
}
#endif /* BMP_LZW_TEST_MAIN */
