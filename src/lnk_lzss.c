/*
 * lnk_lzss.c — port of FUN_004349e5 @ 0x4349e5.
 *
 * The original is ~333 bytes of dense MSB-first bit-shifting plus an
 * unrolled fast path for ctrl == 0 (8 consecutive literals).  We keep
 * the structure straight (loop over the 8 flag bits) and trust the
 * compiler to vectorize where it wants.  Pixel-equivalent output is
 * the requirement, not instruction-equivalent code.
 *
 * Spec source: docs/formats/data-bin.md, validated against
 * tools/extract/data-bin.py:lzss_decompress and the engine code (see
 * docs/decompiled/by-address/4349e5.c).
 */

#include "lnk_lzss.h"

size_t lnk_lzss_decompress(const uint8_t *src, uint8_t *dst)
{
    uint8_t       *out   = dst;
    const uint8_t *p     = src;

    for (;;) {
        uint8_t ctrl = *p++;

        for (int i = 0; i < 8; ++i) {
            if ((ctrl & 0x80u) == 0u) {
                /* literal */
                *out++ = *p++;
            } else {
                uint8_t b1 = *p++;
                uint8_t b2 = *p++;
                unsigned back =
                    ((unsigned)(b1 & 0xF0u) << 4) | (unsigned)b2;

                /* End-of-stream sentinel: back-distance == 0. */
                if (back == 0u) return (size_t)(out - dst);

                unsigned length = (unsigned)(b1 & 0x0Fu);
                if (length == 0u) length = (unsigned)(*p++) + 16u;

                /* Copy length+1 bytes from out-back.  Self-overlap is
                 * intentional — this is how the format encodes runs
                 * (back=1 + long length collapses to memset(out, last)). */
                const uint8_t *from = out - back;
                for (unsigned j = 0; j <= length; ++j) {
                    *out++ = *from++;
                }
            }
            ctrl = (uint8_t)(ctrl << 1);
        }
    }
}
