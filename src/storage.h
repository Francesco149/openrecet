/*
 * storage.h — "init strage ok" subsystem (FUN_004341fe).
 *
 * Opens lnkdata.bin / lnkdatas.bin, loads it into memory, and validates
 * the integrity hash.  Mirrors the original engine's DAT_0438abcc /
 * DAT_0438abd4 / DAT_0437bb50 globals.
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
 * Side-effects on success:
 *   g_lnkdatas_fp     — FILE* left open (for later random-access reads)
 *   g_lnkdatas_buf    — malloc'd buffer holding the entire index file
 *   g_lnkdatas_count  — first 4 bytes of the index, big-endian n_items
 *   g_lnkdatas_is_jp  — 1 if the Japanese lnkdata.bin was loaded
 *                        (it has a 5-byte encoded header; the EN file does not)
 */
int  storage_init(void);

/*
 * storage_shutdown() — mirrors FUN_004349e4.
 *
 * Frees g_lnkdatas_buf and closes g_lnkdatas_fp.  Safe to call even if
 * storage_init() was never called or returned failure.
 */
void storage_shutdown(void);

#endif /* OPENRECET_STORAGE_H */
