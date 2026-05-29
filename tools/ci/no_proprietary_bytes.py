#!/usr/bin/env python3
"""
no_proprietary_bytes.py — guard that a built openrecet.exe ships no
embedded proprietary game data.

OpenRecet must redistribute ZERO game bytes. The one asset class that was
ever embedded is the SE sound effects (WAVs), removed in the
public-release detour (Task 2) in favour of runtime extraction from the
user's own retail exe (docs/formats/se-pack.md). This check is the
automated backstop that keeps it that way: if the embedding ever
regresses, the WAV `RIFF` magic reappears and CI fails before publishing.

It scans for:
  - `RIFF` magic       — the WAV container header. Any occurrence means
                         audio (or other RIFF media) got embedded. HARD FAIL.

The `WAVE` token is deliberately NOT scanned: it legitimately appears as
Win32/DirectMusic type and GUID symbol names (WAVEFORMATEX,
GUID_DSFX_WAVES_REVERB, …) and as our own FindResource type literal.
WAV payload is identified by `RIFF`, which our source never contains.

A size sanity ceiling is emitted as a WARNING only (the port grows over
time, so a hard size gate would be brittle).

Usage:
    tools/ci/no_proprietary_bytes.py build/openrecet.exe [more.exe ...]

Exit code 0 = clean, 1 = proprietary bytes found, 2 = usage/IO error.
"""

from __future__ import annotations

import sys
from pathlib import Path

# WAV container magic. WAV files begin "RIFF<size>WAVE"; the embedded SE
# blobs were full WAVs, so each produced one RIFF.
RIFF_MAGIC = b"RIFF"

# Advisory only: a build with embedded SE was ~1.5 MB larger. Flag, don't fail.
SIZE_WARN_BYTES = 4_500_000


def scan(path: Path) -> int:
    try:
        data = path.read_bytes()
    except OSError as e:
        print(f"error: cannot read {path}: {e}", file=sys.stderr)
        return 2

    riff = data.count(RIFF_MAGIC)
    print(f"{path}: {len(data):,} bytes, RIFF occurrences = {riff}")

    if len(data) > SIZE_WARN_BYTES:
        print(f"  warning: {path} is larger than {SIZE_WARN_BYTES:,} bytes — "
              f"sanity-check that no asset crept in", file=sys.stderr)

    if riff:
        print(f"  FAIL: found {riff} RIFF (WAV) signature(s) — proprietary "
              f"audio appears embedded. SE must be extracted at runtime "
              f"(docs/formats/se-pack.md), never linked into the binary.",
              file=sys.stderr)
        return 1

    print(f"  ok: no embedded WAV payload")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[-3], file=sys.stderr)
        print("usage: no_proprietary_bytes.py <exe> [<exe> ...]",
              file=sys.stderr)
        return 2

    worst = 0
    for arg in argv[1:]:
        rc = scan(Path(arg))
        worst = max(worst, rc)
    return worst


if __name__ == "__main__":
    sys.exit(main(sys.argv))
