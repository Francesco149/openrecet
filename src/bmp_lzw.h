/*
 * bmp_lzw.h — LZW decompressor for bmpdata.bin slices.
 *
 * Mirrors the engine's three-function chain at:
 *   FUN_00434b32  — main decode loop
 *   FUN_00434c2c  — 12-bit MSB-first bit reader
 *   FUN_00434ca9  — dictionary-chain walker
 *
 * Format summary:
 *   - Variable-width codes, fixed 12 bits per code, MSB-first.
 *   - Codes 0..255   literal byte.
 *   - Code 256       reset marker (engine only honors it as the very first
 *                    code; mid-stream resets are not supported — same as
 *                    /opt/src/recettear-repacker/bmp_unpack.py).
 *   - Codes >= 257   reference dictionary entries built online.
 *   - Dictionary frozen after 3839 entries (codes 257..4095).
 *
 * Output size is determined by the caller (the bmpdata index records the
 * decompressed size per asset); we decode until the input is exhausted.
 */

#ifndef OPENRECET_BMP_LZW_H
#define OPENRECET_BMP_LZW_H

#include <stddef.h>

/*
 * bmp_lzw_decompress — decode one LZW-coded slice into dst.
 *
 *   src      compressed bytes (the per-asset payload slice from bmpdata.bin)
 *   csize    compressed byte count
 *   dst      caller-allocated destination buffer; must be >= dsize from the
 *            bmpdata index entry (caller knows the size)
 *
 * Returns the number of bytes written to dst.
 *
 * No allocation, no globals: all state lives on the caller's stack frame
 * (~16 KiB for dict + staging).
 */
size_t bmp_lzw_decompress(const void *src, size_t csize, void *dst);

#endif /* OPENRECET_BMP_LZW_H */
