"""
lnkdatas_hash.py — Integrity hash for lnkdatas.bin (engine: FUN_00474f14 @ 0x474f14)

Algorithm: CRC-16/CCITT-FALSE variant.
  Polynomial : 0x1021
  Init       : 0xFFFF
  Reflection : none (MSB-first)
  Final XOR  : 0xFFFF (i.e. bitwise NOT)

The engine function operates on a 32-bit unsigned register but never produces
values wider than 16 bits in practice, so the result is equivalent to a
masked-to-16-bit implementation (which is what crc.py in recettear-repacker
uses).

Sentinel: lnkdatas_hash(data) == 0x8BAA  means the file is valid.
In the engine this is compared as a signed 16-bit value: (int16_t)0x8BAA == -0x7456.

Cross-reference: /opt/src/recettear-repacker/crc.py (UnrealPowerz) — an
independent port that produces identical results.

Usage (CLI):
    python3 lnkdatas_hash.py path/to/lnkdatas.bin
"""

from __future__ import annotations

import sys

# The value the engine checks for a valid lnkdatas.bin (uint16).
LNKDATAS_HASH_VALID: int = 0x8BAA


def lnkdatas_hash(data: bytes) -> int:
    """
    Compute the engine integrity hash over *data*.

    Returns a uint16 value (0..0xFFFF).  A result of 0x8BAA (LNKDATAS_HASH_VALID)
    means the data is a valid, unmodified lnkdatas.bin.

    Equivalent signed interpretation: (int16_t)0x8BAA == -0x7456.
    """
    crc: int = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000 == 0:
                crc = (crc << 1) & 0xFFFF
            else:
                crc = ((crc << 1) - 0x1021) & 0xFFFF
    return (~crc) & 0xFFFF


if __name__ == "__main__":
    import hashlib

    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <lnkdatas.bin>", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    with open(path, "rb") as fh:
        data = fh.read()

    result = lnkdatas_hash(data)
    sha256 = hashlib.sha256(data).hexdigest()

    status = "VALID" if result == LNKDATAS_HASH_VALID else "INVALID/MISMATCH"
    print(f"lnkdatas_hash({path}): 0x{result:04X}  [{status}]")
    print(f"  sha256: {sha256}")
