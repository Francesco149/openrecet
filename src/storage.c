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

/* ─── module-level globals (mirror DAT_0438abcc / DAT_0438abd4 / etc.) ───── */

/* DAT_0438abcc — FILE* for the open index file */
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

/* ─── expected hash sentinels (from RE of FUN_004341fe) ─────────────────── */

/* Hash sentinel for the plain English lnkdatas.bin */
#define LNKDATAS_HASH_EN  ((int16_t)(-0x7456))   /* 0x8BAA */

/* Hash sentinel for the XOR-encoded Japanese lnkdata.bin */
#define LNKDATAS_HASH_JP  ((int16_t)(-0x3a1f))   /* 0xC5E1 */

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

    /* Success: g_lnkdatas_fp, g_lnkdatas_buf, g_lnkdatas_count are set. */
    return 1;
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
#ifdef STORAGE_TEST_MAIN

/* Provide a minimal WinMain entry point required by -mwindows linker. */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmdLine, int nCmdShow)
{
    (void)hInst; (void)hPrev; (void)lpCmdLine; (void)nCmdShow;

    int ok = storage_init();
    char msg[128];
    if (ok) {
        wsprintfA(msg, "storage_init OK: %d items loaded (%s)",
                  (int)g_lnkdatas_count,
                  g_lnkdatas_is_jp ? "lnkdata.bin" : "lnkdatas.bin");
        MessageBoxA(NULL, msg, "storage_test", 0);
        storage_shutdown();
        return 0;
    } else {
        /* storage_init already showed the error box */
        return 1;
    }
}

#endif /* STORAGE_TEST_MAIN */
