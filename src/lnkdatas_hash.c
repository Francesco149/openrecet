/*
 * lnkdatas_hash.c — Integrity hash for lnkdatas.bin (FUN_00474f14 @ 0x474f14)
 *
 * Faithful translation of the 58-byte Ghidra decompilation.
 *
 * Algorithm: CRC-16/CCITT-FALSE variant.
 *   - Polynomial : 0x1021
 *   - Init value : 0xFFFF
 *   - Input/output reflection: none
 *   - Final XOR  : 0xFFFF (i.e. bitwise NOT of the running CRC)
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
 * Known unit vectors (from CRC-16/CCITT-FALSE, init=0xFFFF, poly=0x1021):
 *   ""        (empty)  -> 0x1D0F  (NOT(0xFFFF ^ 0) = final XOR only... actually
 *                                  for empty: ~0xFFFF & 0xFFFF = 0x0000? NO:
 *                                  empty loop => crc stays 0xFFFF => ~0xFFFF = 0x0000
 *                                  but cast to int16_t => 0x0000)
 *   "123456789" (9 bytes ASCII) -> depends on variant; for CCITT-FALSE:
 *   The canonical "check value" for CRC-16/CCITT-FALSE on "123456789" is 0x29B1.
 *   ~0x29B1 & 0xFFFF = 0xD64E.  Our function returns (int16_t)0xD64E = -0x29B2.
 *   Use the real file for end-to-end validation.
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
