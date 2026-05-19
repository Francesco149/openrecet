/*
 * storage.c — "init strage ok" subsystem (FUN_004341fe @ 0x4341fe).
 *
 * Opens lnkdata.bin (Japanese/original filename) or lnkdatas.bin
 * (English/Steam filename), loads the entire index into memory, and
 * validates it with the engine's CRC-16/CCITT variant (FUN_00474f14).
 *
 * The Japanese file has a 5-byte obfuscation header; the English file is
 * plain.  Both paths are implemented to match the original engine exactly.
 *
 * Globals exposed to other translation units via storage.h:
 *   (none — all globals are file-static; callers use the API surface)
 *
 * Standalone test build:
 *   i686-w64-mingw32-gcc -DSTORAGE_TEST_MAIN src/storage.c \
 *       -o /tmp/storage_test.exe -luser32
 * Run under Wine:
 *   wine /tmp/storage_test.exe
 * Expected output (Steam EN build):
 *   storage_init OK: 1188 items loaded (lnkdatas.bin)
 */

#define WIN32_LEAN_AND_MEAN
#define WINVER        0x0500
#define _WIN32_WINNT  0x0500
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "storage.h"

#include "lnkdatas_hash.h"   /* int16_t lnkdatas_hash(const void *buf, size_t size) */
#include "bmp_lzw.h"         /* size_t bmp_lzw_decompress(const void *src, size_t csize, void *dst) */

/* ─── module-level globals (mirror DAT_0438abcc / DAT_0438abd4 / etc.) ───── */

/* DAT_0438abcc — FILE* for the open lnkdata(s).bin index file */
static FILE  *g_lnkdatas_fp    = NULL;

/* DAT_0438abd4 — malloc'd buffer holding the decoded index */
static char  *g_lnkdatas_buf   = NULL;

/* DAT_0437bb50 — n_items: first 4 bytes of the index (big-endian) */
static int32_t g_lnkdatas_count = 0;

/* DAT_0438abdc — 1 if loaded from the Japanese lnkdata.bin (XOR-encoded),
 *                0 if loaded from the English lnkdatas.bin (plain).
 *                Set before hash validation so shutdown can know if the
 *                buffer needs special handling. */
static int     g_lnkdatas_is_jp = 0;

/* ─── bmpdata.bin overlay globals (mirror DAT_0438abd0 / abd8 / aac4) ────── */

/* DAT_0438abd0 — FILE* for bmpdata.bin (closed after slurp in our build) */
static FILE     *g_bmpdata_fp    = NULL;

/* DAT_0438abd8 — full bmpdata.bin slurped into memory (header + index +
 *                concatenated LZW slices).  Owned. */
static uint8_t  *g_bmpdata_buf   = NULL;
static size_t    g_bmpdata_size  = 0;

/* DAT_0438aac4 — n_items: first 4 bytes of bmpdata.bin (big-endian).
 *                Cached to avoid re-decoding every lookup. */
static int32_t   g_bmpdata_count = 0;

/* Pre-computed byte offset to the start of the LZW data section, equal to
 * 4 + g_bmpdata_count * 96 — caches the engine's expression
 *    DAT_0438abd8 + DAT_0438aac4 * 0x60 + 4. */
static size_t    g_bmpdata_data_start = 0;

/* ─── expected hash sentinels (from RE of FUN_004341fe) ─────────────────── */

/* Hash sentinel for the plain English lnkdatas.bin */
#define LNKDATAS_HASH_EN  ((int16_t)(-0x7456))   /* 0x8BAA */

/* Hash sentinel for the XOR-encoded Japanese lnkdata.bin */
#define LNKDATAS_HASH_JP  ((int16_t)(-0x3a1f))   /* 0xC5E1 */

/* Hash sentinel for bmpdata.bin (positive — no sign-extension trick) */
#define BMPDATA_HASH      ((int16_t) 0x21dc)

/* ─── bmpdata.bin index layout ────────────────────────────────────────────
 *
 * Header:      int32 n_items (big-endian)
 * Per entry:   uint8 name[84] (NUL-padded ASCII, max 83 visible chars + NUL)
 *              int32 dsize   (big-endian — decompressed bytes)
 *              int32 offset  (big-endian — byte offset into the data section)
 *              int32 csize   (big-endian — compressed bytes in the slice)
 *
 * Engine references: FUN_00434585 walks 0x60-stride entries starting at
 * offset 4, comparing up to 0x58 (88) bytes case-insensitively, and reads
 * the +0x54 (dsize), +0x58 (offset), +0x5c (csize) fields as big-endian
 * i32s.  We mirror those offsets exactly.                                 */
#define BMPDATA_ENTRY_STRIDE   0x60   /* 96 bytes */
#define BMPDATA_NAME_MAX       0x58   /* 88 — engine's strncmp bound        */
#define BMPDATA_OFF_DSIZE      0x54
#define BMPDATA_OFF_OFFSET     0x58
#define BMPDATA_OFF_CSIZE      0x5c

/* ─── internal helper: get file size (mirrors FUN_004341d4) ─────────────── */
/* FUN_004341d4 does: fseek(fp,0,SEEK_END); n=ftell(fp); fseek(fp,0,SEEK_SET)
 * (SEEK_END=2, SEEK_SET=0 — matches the fseek/ftell thunks FUN_00503f3c /
 * FUN_00503de4).                                                            */
static long storage_file_size(FILE *fp)
{
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    return n;
}

/* ─── storage_init — mirrors FUN_004341fe ─────────────────────────────────
 *
 * Step-by-step mapping to the original:
 *
 *  _DAT_0438abdc = 0;                    → g_lnkdatas_is_jp = 0
 *  DAT_0438abcc = fopen("lnkdata.bin")   → try JP filename first
 *  if (!DAT_0438abcc) {                  → fall back to EN filename
 *    DAT_0438abcc = fopen("lnkdatas.bin")
 *    if (!DAT_0438abcc) { MsgBox; return 0; }
 *    // EN path: plain read, hash must == -0x7456
 *    size = filesize(fp);
 *    DAT_0438abd4 = malloc(size);
 *    fread(DAT_0438abd4, 1, size, fp);
 *    DAT_0437bb50 = first 4 bytes (big-endian)
 *    if (hash != -0x7456) { MsgBox; return 0; }
 *  } else {
 *    // JP path: skip 5-byte header, XOR-decode, hash must == -0x3a1f
 *    size = filesize(fp);
 *    tmp  = malloc(size);
 *    DAT_0438abd4 = malloc(size);          // destination
 *    fseek(fp, 5, SEEK_SET);              // skip 5-byte header
 *    fread(tmp, 1, size-5, fp);
 *    for i in 0..(size-5): dst[i] = 0x01 - tmp[i];  // XOR/negate transform
 *    free(tmp);
 *    DAT_0437bb50 = first 4 bytes (big-endian)
 *    if (hash != -0x3a1f) { MsgBox; return 0; }
 *    _DAT_0438abdc = 1;
 *  }
 *  return 1;
 *
 * Note: the function continues after storage validation to also open
 * bin/data00[0-4].bin and bmpdata.bin.  Those are separate subsystems;
 * we only implement the lnkdata(s).bin portion here per the task scope.
 */
int storage_init(void)
{
    g_lnkdatas_is_jp = 0;

    /* ── 1. Try Japanese filename first ── */
    g_lnkdatas_fp = fopen("lnkdata.bin", "rb");

    if (g_lnkdatas_fp == NULL) {
        /* ── 2. Fall back to English filename ── */
        g_lnkdatas_fp = fopen("lnkdatas.bin", "rb");
        if (g_lnkdatas_fp == NULL) {
            /* Both filenames failed — fatal. */
            MessageBoxA(NULL,
                "lnkdatas.bin open error",
                "Error", 0);
            return 0;
        }

        /* ── EN path: plain read ── */
        long size = storage_file_size(g_lnkdatas_fp);

        g_lnkdatas_buf = (char *)malloc((size_t)size);
        if (g_lnkdatas_buf == NULL) {
            MessageBoxA(NULL,
                "lnkdatas.bin malloc error",
                "Error", 0);
            return 0;
        }

        fread(g_lnkdatas_buf, 1, (size_t)size, g_lnkdatas_fp);

        /* Capture n_items: first 4 bytes, big-endian
         * (mirrors the CONCAT31/CONCAT21/CONCAT11 chain in the decompiler) */
        g_lnkdatas_count =
              ((int32_t)(uint8_t)g_lnkdatas_buf[0] << 24)
            | ((int32_t)(uint8_t)g_lnkdatas_buf[1] << 16)
            | ((int32_t)(uint8_t)g_lnkdatas_buf[2] <<  8)
            | ((int32_t)(uint8_t)g_lnkdatas_buf[3]      );

        /* Integrity hash — FUN_00474f14(buf, size) — signed int16_t result */
        int16_t h = lnkdatas_hash(g_lnkdatas_buf, (size_t)size);
        if (h != LNKDATAS_HASH_EN) {
            MessageBoxA(NULL,
                "lnkdatas.bin integrity error",
                "Error", 0);
            return 0;
        }

    } else {
        /* ── JP path: skip 5-byte header, then XOR-decode ── */
        long size = storage_file_size(g_lnkdatas_fp);
        long payload_size = size - 5;   /* 5-byte header is skipped */

        /* Temporary read buffer (freed before return) */
        char *tmp = (char *)malloc((size_t)(payload_size > 0 ? payload_size : 1));

        g_lnkdatas_buf = (char *)malloc((size_t)(payload_size > 0 ? payload_size : 1));
        if (g_lnkdatas_buf == NULL) {
            free(tmp);
            MessageBoxA(NULL,
                "lnkdata.bin malloc error",
                "Error", 0);
            return 0;
        }

        /* Skip the 5-byte obfuscation header (fseek(fp, 5, SEEK_SET)) */
        fseek(g_lnkdatas_fp, 5, SEEK_SET);
        fread(tmp, 1, (size_t)payload_size, g_lnkdatas_fp);

        /* XOR/negate transform: dst[i] = 0x01 - src[i]
         * (mirrors: *pcVar3 = '\x01' - pcVar3[iVar4]  in the decompiler) */
        if (payload_size > 0) {
            for (long i = 0; i < payload_size; i++) {
                g_lnkdatas_buf[i] = (char)(0x01 - (unsigned char)tmp[i]);
            }
        }

        free(tmp);

        /* Capture n_items: first 4 bytes, big-endian */
        g_lnkdatas_count =
              ((int32_t)(uint8_t)g_lnkdatas_buf[0] << 24)
            | ((int32_t)(uint8_t)g_lnkdatas_buf[1] << 16)
            | ((int32_t)(uint8_t)g_lnkdatas_buf[2] <<  8)
            | ((int32_t)(uint8_t)g_lnkdatas_buf[3]      );

        /* Integrity hash — FUN_00474f14(buf, payload_size) — signed int16_t result */
        int16_t h = lnkdatas_hash(g_lnkdatas_buf, (size_t)payload_size);
        if (h != LNKDATAS_HASH_JP) {
            MessageBoxA(NULL,
                "lnkdata.bin integrity error",
                "Error", 0);
            return 0;
        }

        /* Mark that we loaded the Japanese (XOR-encoded) file */
        g_lnkdatas_is_jp = 1;
    }

    /* ── 3. bmpdata.bin overlay ────────────────────────────────────────────
     *
     * Engine path (FUN_004341fe, after the lnkdatas branch):
     *   fp = fopen("bmpdata.bin");                 → DAT_0438abd0
     *   sz = filesize(fp);
     *   buf = malloc(sz);                          → DAT_0438abd8
     *   fread(buf, 1, sz, fp);
     *   n  = first 4 bytes (big-endian) of buf;    → DAT_0438aac4
     *   if (hash(buf, sz) != 0x21dc) error;
     *
     * The engine treats this file as required (errors otherwise).  We do
     * the same — bmpdata.bin is part of the Steam install and we'd rather
     * fail loudly than silently miss the overlay.
     */
    g_bmpdata_fp = fopen("bmpdata.bin", "rb");
    if (g_bmpdata_fp == NULL) {
        MessageBoxA(NULL, "bmpdata.bin open error", "Error", 0);
        return 0;
    }

    long bsize_l = storage_file_size(g_bmpdata_fp);
    if (bsize_l <= 0) {
        MessageBoxA(NULL, "bmpdata.bin empty or seek error", "Error", 0);
        return 0;
    }
    g_bmpdata_size = (size_t)bsize_l;

    g_bmpdata_buf = (uint8_t *)malloc(g_bmpdata_size);
    if (g_bmpdata_buf == NULL) {
        MessageBoxA(NULL, "bmpdata.bin malloc error", "Error", 0);
        return 0;
    }

    if (fread(g_bmpdata_buf, 1, g_bmpdata_size, g_bmpdata_fp) != g_bmpdata_size) {
        MessageBoxA(NULL, "bmpdata.bin read error", "Error", 0);
        return 0;
    }

    /* n_items: first 4 bytes, big-endian */
    g_bmpdata_count =
          ((int32_t)g_bmpdata_buf[0] << 24)
        | ((int32_t)g_bmpdata_buf[1] << 16)
        | ((int32_t)g_bmpdata_buf[2] <<  8)
        | ((int32_t)g_bmpdata_buf[3]      );

    if (g_bmpdata_count < 0) {
        MessageBoxA(NULL, "bmpdata.bin negative count", "Error", 0);
        return 0;
    }

    g_bmpdata_data_start =
        (size_t)4 + (size_t)g_bmpdata_count * BMPDATA_ENTRY_STRIDE;

    if (g_bmpdata_data_start > g_bmpdata_size) {
        MessageBoxA(NULL, "bmpdata.bin truncated index", "Error", 0);
        return 0;
    }

    /* Integrity hash — same algorithm as lnkdatas, sentinel 0x21dc */
    {
        int16_t h = lnkdatas_hash(g_bmpdata_buf, g_bmpdata_size);
        if (h != BMPDATA_HASH) {
            MessageBoxA(NULL, "bmpdata.bin integrity error", "Error", 0);
            return 0;
        }
    }

    /* Success: g_lnkdatas_* + g_bmpdata_* are all set. */
    return 1;
}

/* ─── case-insensitive name compare against an in-buffer index entry ──────
 *
 * Mirrors the inner comparison from FUN_00434585 / FUN_004346bf:
 *   - walk both `name` (NUL-terminated) and `entry_name` (fixed-stride
 *     buffer) byte by byte
 *   - exact match OR (caller upper-A..Z + 0x20 == buffer-byte) → continue
 *   - hitting NUL in name OR reaching name_max → match
 *
 * Returns 1 on match, 0 on mismatch.                                       */
static int bmpdata_name_eq(const char *name, const uint8_t *entry_name)
{
    for (int i = 0; i < BMPDATA_NAME_MAX; ++i) {
        char c = name[i];
        if (c == '\0') return 1;            /* short name — match suffix-pad */

        uint8_t b = entry_name[i];
        if ((uint8_t)c == b) continue;
        if (c >= 'A' && c <= 'Z' && (uint8_t)(c + 0x20) == b) continue;
        return 0;
    }
    return 1;                               /* hit the max-name bound */
}

/* ─── big-endian 4-byte read ──────────────────────────────────────────────
 *
 * The index is stored MSB-first.  Returns the 32-bit value; caller uses it
 * either as size_t (size/offset) or int32_t (semantics).                   */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24)
         | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8)
         | ((uint32_t)p[3]      );
}

/* ─── locate a bmpdata index entry by name ────────────────────────────────
 *
 * Returns a pointer to the 96-byte entry on hit, or NULL if not found.
 * Used by both storage_get_size and storage_read.                          */
static const uint8_t *bmpdata_find(const char *name)
{
    if (g_bmpdata_buf == NULL) return NULL;

    const uint8_t *entry = g_bmpdata_buf + 4;
    for (int32_t i = 0; i < g_bmpdata_count; ++i) {
        if (bmpdata_name_eq(name, entry)) {
            return entry;
        }
        entry += BMPDATA_ENTRY_STRIDE;
    }
    return NULL;
}

/* ─── storage_get_size — mirrors FUN_00434585 (bmpdata branch only) ─────── */
size_t storage_get_size(const char *name)
{
    const uint8_t *entry = bmpdata_find(name);
    if (entry == NULL) return 0;
    return (size_t)be32(entry + BMPDATA_OFF_DSIZE);
}

/* ─── storage_read — mirrors FUN_004346bf (bmpdata branch only) ───────────
 *
 * Engine logic at LAB_0043476e:
 *   src   = data_section + entry.offset
 *   csize = entry.csize
 *   dst   = param_2 (caller buffer)
 *   FUN_00434b32(src, dst, csize);   ← LZW decompress
 *   return entry.dsize;
 */
size_t storage_read(const char *name, void *dst)
{
    const uint8_t *entry = bmpdata_find(name);
    if (entry == NULL) return 0;

    size_t dsize  = (size_t)be32(entry + BMPDATA_OFF_DSIZE);
    size_t offset = (size_t)be32(entry + BMPDATA_OFF_OFFSET);
    size_t csize  = (size_t)be32(entry + BMPDATA_OFF_CSIZE);

    size_t slice_start = g_bmpdata_data_start + offset;
    if (slice_start + csize > g_bmpdata_size) {
        /* Truncated archive — refuse rather than read past the buffer. */
        return 0;
    }

    bmp_lzw_decompress(g_bmpdata_buf + slice_start, csize, dst);
    return dsize;
}

/* ─── storage_shutdown ────────────────────────────────────────────────────
 * Mirrors FUN_004349e4 (called in the shutdown sequence after the main loop).
 * The original calls FUN_005036af (free/fclose thunks) on the file handles.
 */
void storage_shutdown(void)
{
    if (g_lnkdatas_buf != NULL) {
        free(g_lnkdatas_buf);
        g_lnkdatas_buf = NULL;
    }
    if (g_lnkdatas_fp != NULL) {
        fclose(g_lnkdatas_fp);
        g_lnkdatas_fp = NULL;
    }
    g_lnkdatas_count = 0;
    g_lnkdatas_is_jp = 0;

    if (g_bmpdata_buf != NULL) {
        free(g_bmpdata_buf);
        g_bmpdata_buf = NULL;
    }
    if (g_bmpdata_fp != NULL) {
        fclose(g_bmpdata_fp);
        g_bmpdata_fp = NULL;
    }
    g_bmpdata_size       = 0;
    g_bmpdata_count      = 0;
    g_bmpdata_data_start = 0;
}

/* ─── standalone test harness ────────────────────────────────────────────
 *
 * Build:
 *   i686-w64-mingw32-gcc -DSTORAGE_TEST_MAIN src/storage.c \
 *       -o /tmp/storage_test.exe -luser32
 *
 * Run (from the game's working directory, where lnkdatas.bin lives):
 *   wine /tmp/storage_test.exe
 *
 * Expected output (Steam EN build — 1188 items):
 *   storage_init OK: 1188 items loaded (lnkdatas.bin)
 *
 * Expected output (Japanese original):
 *   storage_init OK: <n> items loaded (lnkdata.bin)
 */
/* ─── standalone "extract one asset" harness ─────────────────────────────
 *
 * Build:
 *   i686-w64-mingw32-gcc -DSTORAGE_TEST_EXTRACT -O2 -Wall -Wextra -std=c11 \
 *       src/storage.c src/lnkdatas_hash.c src/bmp_lzw.c \
 *       -o /tmp/storage_extract.exe -luser32
 *
 * Usage (from a directory containing lnkdatas.bin + bmpdata.bin):
 *   /tmp/storage_extract.exe NAME > out.bin
 *
 * Returns 0 on success, non-zero on error.  Writes the decompressed asset
 * bytes to stdout, suitable for `diff` against recettear-repacker output. */
#ifdef STORAGE_TEST_EXTRACT
#include <stdio.h>
#include <fcntl.h>
#include <io.h>

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s NAME\n", argv[0]);
        return 2;
    }
    _setmode(_fileno(stdout), _O_BINARY);

    if (!storage_init()) return 1;

    size_t dsize = storage_get_size(argv[1]);
    if (dsize == 0) {
        fprintf(stderr, "not found in bmpdata: %s\n", argv[1]);
        storage_shutdown();
        return 3;
    }

    void *buf = malloc(dsize);
    if (!buf) { storage_shutdown(); return 4; }

    size_t written = storage_read(argv[1], buf);
    if (written != dsize) {
        fprintf(stderr, "size mismatch: get=%lu read=%lu\n",
                (unsigned long)dsize, (unsigned long)written);
        free(buf); storage_shutdown();
        return 5;
    }

    fwrite(buf, 1, dsize, stdout);
    free(buf);
    storage_shutdown();
    return 0;
}
#endif /* STORAGE_TEST_EXTRACT */

#ifdef STORAGE_TEST_MAIN

/* Provide a minimal WinMain entry point required by -mwindows linker. */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nCmdShow;

    int ok = storage_init();
    char msg[256];
    if (ok) {
        wsprintfA(msg,
                  "storage_init OK\n"
                  "  lnkdatas: %d items (%s)\n"
                  "  bmpdata:  %d items (%lu bytes, data @ %lu)",
                  (int)g_lnkdatas_count,
                  g_lnkdatas_is_jp ? "lnkdata.bin" : "lnkdatas.bin",
                  (int)g_bmpdata_count,
                  (unsigned long)g_bmpdata_size,
                  (unsigned long)g_bmpdata_data_start);
        MessageBoxA(NULL, msg, "storage_test", 0);
        storage_shutdown();
        return 0;
    } else {
        /* storage_init already showed the error box */
        return 1;
    }
}

#endif /* STORAGE_TEST_MAIN */
