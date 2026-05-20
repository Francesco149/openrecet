/*
 * storage.h — asset storage subsystem (FUN_004341fe + FUN_00434585 +
 * FUN_004346bf).
 *
 * Owns the lnkdata.bin / lnkdatas.bin index and the bmpdata.bin update
 * overlay.  bmpdata is checked first on every lookup, then we fall back
 * to lnkdatas — same priority order as the original engine.
 *
 * Build note: this module uses i686-w64-mingw32-gcc (32-bit Win32 PE).
 * See src/Makefile for the full build invocation.
 */

#ifndef OPENRECET_STORAGE_H
#define OPENRECET_STORAGE_H

#include <stddef.h>

/*
 * storage_init() — mirrors FUN_004341fe.
 *
 * Returns 1 on success, 0 on any fatal error (MessageBoxA already shown
 * to the user before returning 0, matching the original engine's
 * convention).
 *
 * Loads:
 *   - lnkdata.bin (JP, XOR-encoded) or lnkdatas.bin (EN, plain) — index only
 *   - bmpdata.bin (LZW-compressed update overlay) — full file into memory,
 *     hash-validated against sentinel 0x21dc
 *
 * Content reads via the lnkdatas index open `bin/data%03d.bin` on demand
 * (one cached FILE*, reopened across chunk boundaries — matches the
 * engine's behavior in FUN_004346bf).
 */
int  storage_init(void);

/*
 * storage_shutdown() — mirrors FUN_004349e4.
 *
 * Frees all buffers, closes all FILE*s.  Safe to call even if
 * storage_init() was never called or returned failure.
 */
void storage_shutdown(void);

/*
 * storage_get_size(name) — mirrors FUN_00434585.
 *
 * Looks up `name` in the bmpdata overlay first (case-insensitive over
 * up to 88 chars), then falls back to the lnkdatas index (case-sensitive
 * over up to 128 chars — matching the engine's exact byte-compare loop
 * for the lnkdatas branch).  Returns the decompressed size, or 0 if
 * the asset is in neither index.
 */
size_t storage_get_size(const char *name);

/*
 * storage_read(name, dst) — mirrors FUN_004346bf.
 *
 * Decompresses the asset for `name` into `dst`.  Caller must provide a
 * buffer of at least storage_get_size(name) bytes.
 *
 *   - bmpdata hit  → LZW decompress (bmp_lzw_decompress).
 *   - lnkdatas hit → read `compressed_size` bytes from the data*.bin
 *                    stream (may straddle a 10 MiB chunk boundary),
 *                    then LZSS-decompress (lnk_lzss_decompress).
 *
 * Returns the decompressed size on success, 0 on lookup miss / I/O error.
 */
size_t storage_read(const char *name, void *dst);

#endif /* OPENRECET_STORAGE_H */
