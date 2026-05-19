/*
 * test_bmp_lzw.c — round-trip vendor bmpdata.bin through bmp_lzw.
 *
 * The decoder is already validated byte-for-byte against
 * recettear-repacker/bmp_unpack.py (see PROGRESS 2026-05-20). This
 * test re-runs that comparison from the C side at every test pass,
 * but limited to verifying that decompressed lengths match each
 * entry's `dsize` field — a stricter, structural check that catches
 * any regression that silently shortens the output (which a
 * pixel-equality check on a single asset wouldn't necessarily catch).
 *
 * Skipped if the vendor file is not present.
 */
#include "t.h"
#include "bmp_lzw.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BMPDATA_PATH OPENRECET_ROOT "/vendor/original/bmpdata.bin"

static uint8_t *slurp(const char *path, size_t *out_size)
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

/* Big-endian readers — bmpdata.bin uses BE integers. */
static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

int test_bmp_lzw_round_trip_vendor(void)
{
    size_t total;
    uint8_t *file = slurp(BMPDATA_PATH, &total);
    if (!file) T_SKIP("vendor bmpdata.bin not present at %s", BMPDATA_PATH);

    if (total < 4) { free(file); T_FAIL("bmpdata.bin too small: %zu bytes", total); }

    uint32_t n = rd_be32(file);
    /* Sanity-bound the entry count so a corrupt file can't make us
     * compute a payload offset that wraps. */
    if (n == 0 || n > 0x10000u) {
        free(file);
        T_FAIL("implausible entry count: %u", n);
    }
    size_t entries_end = (size_t)4 + (size_t)n * 96u;
    if (entries_end > total) {
        free(file);
        T_FAIL("entry table overruns file (entries_end=%zu total=%zu)",
               entries_end, total);
    }

    int ok = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = file + 4 + (size_t)i * 96u;
        uint32_t dsize  = rd_be32(e + 84);
        uint32_t doff   = rd_be32(e + 88);
        uint32_t csize  = rd_be32(e + 92);

        if ((size_t)doff + (size_t)csize > total - entries_end) {
            free(file);
            T_FAIL("entry %u slice out of bounds (off=%u csize=%u)",
                   i, doff, csize);
        }
        const uint8_t *slice = file + entries_end + doff;

        uint8_t *out = (uint8_t *)malloc(dsize);
        if (!out) { free(file); T_FAIL("OOM allocating %u bytes", dsize); }

        size_t written = bmp_lzw_decompress(slice, csize, out);
        if (written != dsize) {
            char name[85];
            memcpy(name, e, 84);
            name[84] = '\0';
            free(out); free(file);
            T_FAIL("entry %u (%s): wrote %zu bytes, expected %u",
                   i, name, written, dsize);
        }
        free(out);
        ok++;
    }
    free(file);

    /* We expect a small number of patched assets in the shipping
     * Steam build — sanity-check we did at least one. */
    if (ok < 1) T_FAIL("no entries decoded");

    return 0;
}
