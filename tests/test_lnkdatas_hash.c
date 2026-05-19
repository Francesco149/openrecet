/*
 * test_lnkdatas_hash.c — engine integrity-hash coverage.
 *
 * The engine's hash is CCITT-FALSE-shaped (poly 0x1021, init 0xFFFF,
 * no reflect, final invert) but uses SUBTRACTION instead of XOR in
 * the feedback step — so it does NOT match the standard CCITT-FALSE
 * check value. The cross-reference is recettear-repacker/crc.py.
 *
 * Synthetic vectors (no vendor data needed):
 *   ""           → 0x0000   (empty loop, init inverted)
 *   "123456789"  → 0xF5B7   (engine variant; CCITT-FALSE would be 0xD64E)
 *
 * Plus an opportunistic check against vendor/original/lnkdatas.bin:
 * a valid English-build file hashes to LNKDATAS_HASH_VALID (-0x7456 ==
 * 0x8BAA). Skipped if the file is not present.
 */
#include "t.h"
#include "lnkdatas_hash.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LNKDATAS_PATH OPENRECET_ROOT "/vendor/original/lnkdatas.bin"

int test_lnkdatas_hash_empty(void)
{
    /* Empty input — no iterations, CRC stays 0xFFFF, inverted to 0. */
    int16_t h = lnkdatas_hash("", 0);
    T_ASSERT_EQ_I(h, (int16_t)0);
    return 0;
}

int test_lnkdatas_hash_test_vector(void)
{
    const char *s = "123456789";
    int16_t h = lnkdatas_hash(s, strlen(s));
    /* 0xF5B7 unsigned = -0x0A49 signed. Cross-checked against
     * /opt/src/recettear-repacker/crc.py, which uses the same
     * subtraction-based feedback as the engine. */
    T_ASSERT_EQ_I(h, (int16_t)0xF5B7);
    return 0;
}

int test_lnkdatas_hash_vendor(void)
{
    FILE *fp = fopen(LNKDATAS_PATH, "rb");
    if (!fp) T_SKIP("vendor lnkdatas.bin not present at %s", LNKDATAS_PATH);

    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); T_FAIL("fseek failed"); }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); T_FAIL("ftell returned %ld", sz); }
    rewind(fp);

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    T_ASSERT(buf != NULL);
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); T_FAIL("short read");
    }
    fclose(fp);

    int16_t h = lnkdatas_hash(buf, (size_t)sz);
    free(buf);

    /* Steam build (English) should hit LNKDATAS_HASH_VALID; the JP build
     * uses a different sentinel. Accept either to keep the test useful
     * across both, since the *purpose* here is "the file hashes
     * stably". */
    if (h != LNKDATAS_HASH_VALID && h != (int16_t)-0x3a1f) {
        T_FAIL("hash 0x%04X is neither EN sentinel (0x%04X) nor JP "
               "sentinel (0x%04X)", (uint16_t)h, (uint16_t)LNKDATAS_HASH_VALID,
               (uint16_t)-0x3a1f);
    }
    return 0;
}
