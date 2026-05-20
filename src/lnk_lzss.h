/*
 * lnk_lzss.h — LZSS decompressor for `bin/data*.bin` slices.
 *
 * Mirrors FUN_004349e5 @ 0x4349e5 — the lnkdatas content-read decoder.
 *
 * Format summary (see docs/formats/data-bin.md):
 *   - 8-bit control byte, MSB-first across 8 flags
 *   - flag = 0 → literal: copy next 1 byte to output
 *   - flag = 1 → back-reference: 2 bytes b1, b2
 *       back   = ((b1 & 0xF0) << 4) | b2     (12-bit back-distance)
 *       length = b1 & 0x0F                    (or b3 + 16 if 0)
 *       copy length+1 bytes from out[-back..] (self-overlap is intentional —
 *       this is how the encoder spells RLE)
 *       back == 0 marks end of stream
 *
 * The stream is self-delimiting, so no input size is required.
 */

#ifndef OPENRECET_LNK_LZSS_H
#define OPENRECET_LNK_LZSS_H

#include <stddef.h>
#include <stdint.h>

/*
 * lnk_lzss_decompress — decode one LZSS-compressed slice into `dst`.
 *
 *   src   compressed bytes (one entry's slice from the data*.bin stream)
 *   dst   caller-allocated buffer; must hold at least the decompressed
 *         size (recorded in the lnkdatas index `dsize` field)
 *
 * Returns the number of bytes written to `dst`.
 */
size_t lnk_lzss_decompress(const uint8_t *src, uint8_t *dst);

#endif /* OPENRECET_LNK_LZSS_H */
