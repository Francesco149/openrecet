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
 * Out of scope for this build (storage_read returns 0 for these paths):
 *   - bin/data_NNN.bin payloads accessed via the lnkdatas index
 *   - bmp/chr_formdata.bin / formdata.bin
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
 * Returns the decompressed size of `name` if it's listed in the bmpdata
 * overlay index, otherwise 0.  Name comparison is case-insensitive (the
 * engine accepts e.g. "BMP/window.tga" for an entry stored as
 * "bmp/window.tga").
 *
 * lnkdatas fallback is intentionally not wired (see header comment).
 */
size_t storage_get_size(const char *name);

/*
 * storage_read(name, dst) — mirrors FUN_004346bf.
 *
 * Decompresses the bmpdata entry for `name` into `dst`.  Caller must
 * provide a buffer of at least storage_get_size(name) bytes.
 *
 * Returns the number of bytes written (== decompressed size), or 0 if
 * the name is not in the bmpdata overlay.
 */
size_t storage_read(const char *name, void *dst);

#endif /* OPENRECET_STORAGE_H */
