/*
 * lnkdatas_hash.c — Integrity hash for lnkdatas.bin (FUN_00474f14 @ 0x474f14)
 *
 * Faithful translation of the 58-byte Ghidra decompilation.
 *
 * Algorithm: CCITT-FALSE-shaped, with one wrinkle — the feedback step
 * is a SUBTRACTION, not the canonical XOR. The bit-level recurrence
 * is:
 *
 *     crc = (crc & 0x8000) ? ((crc << 1) - 0x1021) : (crc << 1)
 *
 * For input bytes whose CRC trajectory never produces a borrow, this
 * coincides with the standard CCITT-FALSE XOR. Real inputs do
 * produce borrows, so the engine's hash diverges from the standard:
 * "123456789" → 0xF5B7 here vs 0x29B1 for canonical CCITT-FALSE.
 *
 *   - Polynomial / subtracted constant: 0x1021
 *   - Init value : 0xFFFF
 *   - Input/output reflection: none
 *   - Final transform: bitwise NOT
 *
 * The engine operates on a 32-bit register (uint uVar1) and never explicitly
 * masks it to 16 bits inside the bit loop, but — because the initial value
 * and every XOR operand are ≤16-bit — the register never escapes 16 bits in
 * practice.  The two formulations (32-bit register vs. masked-to-16) produce
 * identical low-16-bit results on all real inputs.  We use the 16-bit form
 * here so the C behaviour is well-defined without relying on unsigned 32-bit
 * wraparound.
 *
 * Cross-reference: /opt/src/recettear-repacker/crc.py (UnrealPowerz) is an
 * independent port of the same hash and produces identical results.
 *
 * Compile (mingw32, C11):
 *   i686-w64-mingw32-gcc -std=c11 -O2 -c lnkdatas_hash.c -o lnkdatas_hash.o
 *
 * Test program (see bottom of file for a minimal self-test).
 */

#include "lnkdatas_hash.h"

#include <stddef.h>
#include <stdint.h>

int16_t lnkdatas_hash(const void *buf, size_t size)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint16_t crc = 0xFFFFu;
    size_t   i;

    for (i = 0; i < size; i++) {
        int bit;
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (bit = 0; bit < 8; bit++) {
            if ((crc & 0x8000u) == 0) {
                crc = (uint16_t)(crc << 1);
            } else {
                crc = (uint16_t)((crc << 1) - 0x1021u);
            }
        }
    }

    return (int16_t)(~crc);
}

/*
 * ============================================================
 * SELF-TEST (compile and run to verify — not part of the lib)
 * ============================================================
 *
 * Compile with:
 *   i686-w64-mingw32-gcc -std=c11 -O2 -DLNKDATAS_HASH_TEST \
 *       lnkdatas_hash.c -o lnkdatas_hash_test.exe
 * Run (WSLInterop or Wine):
 *   ./lnkdatas_hash_test.exe lnkdatas.bin
 * Expected output:
 *   hash = 0x8BAA  (VALID)
 *
 * Or for a known-short test vector (empty buffer):
 *   i686-w64-mingw32-gcc -std=c11 -O2 -DLNKDATAS_HASH_TEST \
 *       -DLNKDATAS_HASH_TEST_UNIT lnkdatas_hash.c -o unit.exe
 *
 * Known unit vectors (engine variant — XOR replaced by subtraction):
 *   ""           (empty)         -> 0x0000   (empty loop, init inverted)
 *   "123456789"  (9 bytes ASCII) -> 0xF5B7   (NOT 0x29B1; differs from
 *                                             canonical CCITT-FALSE
 *                                             because of the subtraction)
 * Both are covered by tests/test_lnkdatas_hash.c; the second is also
 * cross-checked against /opt/src/recettear-repacker/crc.py.
 */

#ifdef LNKDATAS_HASH_TEST
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    FILE    *f;
    long     sz;
    uint8_t *buf;
    int16_t  h;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <lnkdatas.bin>\n", argv[0]);
        return 1;
    }

    f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }

    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);

    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fputs("OOM\n", stderr); fclose(f); return 1; }
    fread(buf, 1, (size_t)sz, f);
    fclose(f);

    h = lnkdatas_hash(buf, (size_t)sz);
    free(buf);

    printf("hash = 0x%04X  (%s)\n",
           (unsigned)(uint16_t)h,
           h == LNKDATAS_HASH_VALID ? "VALID" : "INVALID/MISMATCH");
    return h == LNKDATAS_HASH_VALID ? 0 : 1;
}
#endif /* LNKDATAS_HASH_TEST */
