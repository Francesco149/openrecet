/*
 * lnkdatas_hash.h — Integrity hash for lnkdatas.bin (FUN_00474f14)
 *
 * CCITT-FALSE-shaped: polynomial 0x1021, init 0xFFFF, no reflect,
 * result bitwise-inverted. BUT the feedback step uses SUBTRACTION
 * (`crc = (crc << 1) - 0x1021`) rather than the standard XOR
 * (`crc = (crc << 1) ^ 0x1021`), so the engine's hash does *not*
 * match published CCITT-FALSE check values (e.g. "123456789" hashes
 * to 0xF5B7 here, not the standard 0x29B1). Cross-reference:
 * /opt/src/recettear-repacker/crc.py — independent port, same
 * behavior.
 *
 * A return value of -0x7456 (0x8BAA as uint16) means the English
 * Steam build's lnkdatas.bin is valid; the JP build hashes to a
 * different sentinel (-0x3a1f / 0xC5E1).
 *
 * Signature mirrors the engine call in FUN_004341fe:
 *   hash = FUN_00474f14(n, buffer);
 *   if (hash != -0x7456) fatal("lnkdatas.bin corrupt");
 */

#ifndef LNKDATAS_HASH_H
#define LNKDATAS_HASH_H

#include <stddef.h>
#include <stdint.h>

/*
 * lnkdatas_hash - compute the engine integrity hash over [buf, buf+size).
 *
 * Returns -0x7456 (== (int16_t)0x8BAA) when the buffer is a valid,
 * unmodified lnkdatas.bin.
 */
int16_t lnkdatas_hash(const void *buf, size_t size);

/* Sentinel: the value returned for a valid lnkdatas.bin. */
#define LNKDATAS_HASH_VALID  ((int16_t)(-0x7456))

#endif /* LNKDATAS_HASH_H */
