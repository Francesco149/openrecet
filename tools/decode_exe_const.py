#!/usr/bin/env python3
"""decode_exe_const.py — print the float / int32 at a list of unpacked-exe VAs.

The Ghidra decompile drops x87 FP constants (renders them as NaN garbage or
omits dropped call args).  The objdump shows the real `.rdata` load address
(e.g. `flds 0x5194b8`); this tool resolves that VA to the actual float so a
port can transcribe the exact immediate instead of guessing.

PE section map (vendor/unpacked/recettear.unpacked.exe):
  .text  VMA 0x00401000  file 0x00000400
  .rdata VMA 0x00515000  file 0x00113a00
  .data  VMA 0x00528000  file 0x00126800

Usage:
  nix develop --command python3 tools/decode_exe_const.py 0x5194b8 0x519378 ...
"""
import struct
import sys

EXE = "vendor/unpacked/recettear.unpacked.exe"
SECTIONS = [  # (vma, size, file_off)
    (0x00401000, 0x0011358e, 0x00000400),
    (0x00515000, 0x00012d90, 0x00113a00),
    (0x00528000, 0x000dbe00, 0x00126800),
    (0x0964f000, 0x000008e0, 0x00202600),
]


def va_to_off(va):
    for vma, size, foff in SECTIONS:
        if vma <= va < vma + size:
            return foff + (va - vma)
    return None


def main():
    with open(EXE, "rb") as f:
        blob = f.read()
    for arg in sys.argv[1:]:
        va = int(arg, 0)
        off = va_to_off(va)
        if off is None:
            print(f"{va:#08x}: <not in any section>")
            continue
        raw = blob[off:off + 4]
        fv = struct.unpack("<f", raw)[0]
        iv = struct.unpack("<I", raw)[0]
        print(f"{va:#08x}: float={fv:<16g} int=0x{iv:08x} ({iv})")


if __name__ == "__main__":
    main()
