#!/usr/bin/env python3
"""no_sse_math.py — gate: the port's FLOAT MATH must stay on x87.

Frame-exact parity with retail (VC6-era, statically-linked CRT, x87 FPU) depends
on BOTH sides computing float math through the same x87 instructions — same
fsin/fcos, same 80-bit intermediates, same __ftol truncation
(docs/audits/2026-06-09-methodology-audit.md §1.3). A toolchain bump or Makefile
edit that flips mingw to SSE math (-mfpmath=sse, -march=pentium4+, x86_64) would
silently change rounding across every validated function.

This scans the built exe's disassembly for SSE/SSE2 *result-changing* float
instructions — arithmetic, conversion, comparison. Pure data moves (movss/movaps)
and integer SIMD (p*) are NOT flagged: runtime-dispatched CRT memcpy/strlen may
legally use SIMD loads/stores without touching float results.

Usage: no_sse_math.py [exe]            (default build/openrecet.exe)
Exit 0 = clean · 1 = SSE float math found · 2 = cannot scan
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

# Result-changing float mnemonics (scalar + packed): arithmetic, sqrt/rcp,
# min/max, rounds, conversions, compares. NOT mov* (data moves are benign).
SSE_FLOAT = re.compile(
    r"^(?:v?(?:add|sub|mul|div|sqrt|min|max|rsqrt|rcp|round|dpp?|hadd|hsub)"
    r"(?:ss|sd|ps|pd)"
    r"|v?cvt\w+"
    r"|v?u?comis[sd]"
    r"|v?cmp(?:ss|sd|ps|pd)"
    r"|vf(?:n?m(?:add|sub))\w*)$")

MNEMONIC = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*([a-z][a-z0-9.]*)")


def find_objdump() -> str | None:
    for c in ("i686-w64-mingw32-objdump", "objdump"):
        if shutil.which(c):
            return c
    return None


def main(argv: list[str]) -> int:
    exe = Path(argv[1]) if len(argv) > 1 else Path("build/openrecet.exe")
    if not exe.exists():
        print(f"no_sse_math: {exe} not found", file=sys.stderr)
        return 2
    od = find_objdump()
    if not od:
        print("no_sse_math: no objdump on PATH", file=sys.stderr)
        return 2
    r = subprocess.run([od, "-d", "--no-show-raw-insn", str(exe)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"no_sse_math: objdump failed: {r.stderr[:400]}", file=sys.stderr)
        return 2
    hits: list[str] = []
    for ln in r.stdout.splitlines():
        m = re.match(r"^\s*[0-9a-f]+:\s+(\S+)", ln)
        if m and SSE_FLOAT.match(m.group(1)):
            hits.append(ln.strip())
    if hits:
        print(f"no_sse_math: FAIL — {len(hits)} SSE float-math instruction(s) in "
              f"{exe} (FP model must stay x87 for retail parity; check Makefile/"
              f"flake toolchain flags):", file=sys.stderr)
        for h in hits[:12]:
            print(f"  {h}", file=sys.stderr)
        if len(hits) > 12:
            print(f"  … +{len(hits) - 12} more", file=sys.stderr)
        return 1
    print(f"no_sse_math: OK — no SSE float math in {exe} (x87 invariant holds)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
