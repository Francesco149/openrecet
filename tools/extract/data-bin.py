#!/usr/bin/env python3
"""
tools/extract/data-bin.py — extract Recettear's `bin/data*.bin` archive.

Reads `lnkdatas.bin` (the index) and the concatenated `bin/data###.bin`
files, then for each entry: seeks to its absolute offset (across the
logical stream that spans data###.bin files at 10 MiB boundaries),
reads `compressed_size` bytes, LZSS-decompresses, and writes the result.

Spec: docs/formats/data-bin.md. Independent reimplementation matching the
spec verified by UnrealPowerz/recettear-repacker; the two produce
byte-identical output on the current Steam build.

Also handles the Japanese release: `lnkdata.bin` (no trailing 's') is a
5-byte header + a payload obfuscated per-byte as `dst = 0x01 - src`
(docs/findings/engine-quirks.md §2); after decoding it is the same
index format. Auto-detected when `lnkdatas.bin` is absent.

Usage:
    extract/data-bin.py <game_root> <out_dir>
    extract/data-bin.py vendor/original/ extracted/

    # Cross-check against the reference implementation:
    extract/data-bin.py vendor/original/ extracted/ \
        --validate-against /opt/src/recettear-repacker

    # List only — print the index without extracting:
    extract/data-bin.py vendor/original/ --list
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


BIN_SIZE = 10 * 1024 * 1024            # 10 MiB per data###.bin chunk
NAME_LEN = 128                          # bytes per name in lnkdatas.bin
ENTRY_FMT = ">" + str(NAME_LEN) + "siii"
ENTRY_SIZE = struct.calcsize(ENTRY_FMT) # 128 + 4 + 4 + 4 = 140


# ─── header ────────────────────────────────────────────────────────────────


JP_HEADER_LEN = 5                       # raw prefix on the JP lnkdata.bin


def index_path(game_root: Path) -> Path:
    """EN `lnkdatas.bin` if present, else the JP `lnkdata.bin`."""
    en = game_root / "lnkdatas.bin"
    return en if en.exists() else game_root / "lnkdata.bin"


def read_index(lnkdatas_path: Path) -> list[tuple[str, int, int, int]]:
    """Return list of (name, decompressed_size, offset, compressed_size)."""
    data = lnkdatas_path.read_bytes()
    if lnkdatas_path.name == "lnkdata.bin":
        # JP release: 5-byte header, then payload bytes are 0x01 - src.
        data = bytes((0x01 - b) & 0xFF for b in data[JP_HEADER_LEN:])
    (n,) = struct.unpack(">i", data[:4])
    items: list[tuple[str, int, int, int]] = []
    pos = 4
    for _ in range(n):
        raw_name, dsize, offset, size = struct.unpack(
            ENTRY_FMT, data[pos:pos + ENTRY_SIZE]
        )
        try:
            raw_name = raw_name[: raw_name.index(0)]
        except ValueError:
            pass
        items.append((raw_name.decode("ascii"), dsize, offset, size))
        pos += ENTRY_SIZE
    return items


# ─── LZSS decompressor ─────────────────────────────────────────────────────


def lzss_decompress(src: bytes) -> bytearray:
    """Custom LZSS variant — see docs/formats/data-bin.md for the spec.

    Control byte's bits read MSB-first. Bit 0 = literal byte;
    bit 1 = back-reference of (b1, b2[, ext]):
        back   = ((b1 & 0xF0) << 4) | b2           (12-bit distance)
        length = (b1 & 0x0F) or (b2_next + 16) if 0
        copy `length + 1` bytes from out[-back .. ]   (self-overlap intentional)
        back == 0 marks end of stream.
    """
    out = bytearray()
    it = iter(src)
    nxt = it.__next__
    while True:
        ctrl = nxt()
        for shift in range(7, -1, -1):
            if not (ctrl & (1 << shift)):
                out.append(nxt())
            else:
                b1 = nxt()
                b2 = nxt()
                back = ((b1 & 0xF0) << 4) | b2
                if back == 0:
                    return out
                length = b1 & 0x0F
                if length == 0:
                    length = nxt() + 16
                start = len(out) - back
                for i in range(length + 1):
                    out.append(out[start + i])


# ─── stream reader spanning bin/data*.bin ─────────────────────────────────


class DataStream:
    """Reads bytes from the logical concatenation of bin/data###.bin files."""

    def __init__(self, game_root: Path):
        self.bin_dir = game_root / "bin"
        self._file = None
        self._idx = -1

    def _open(self, idx: int) -> None:
        if self._idx == idx:
            return
        if self._file is not None:
            self._file.close()
        path = self.bin_dir / f"data{idx:03d}.bin"
        if not path.exists():
            raise FileNotFoundError(f"missing chunk {path}")
        self._file = path.open("rb")
        self._idx = idx

    def read_at(self, offset: int, length: int) -> bytes:
        out = bytearray()
        remaining = length
        cur = offset
        while remaining > 0:
            file_idx = cur // BIN_SIZE
            file_off = cur % BIN_SIZE
            self._open(file_idx)
            self._file.seek(file_off)
            chunk_avail = BIN_SIZE - file_off
            take = min(remaining, chunk_avail)
            buf = self._file.read(take)
            if not buf:
                raise IOError(
                    f"short read at offset {cur} (file {file_idx}, off {file_off})"
                )
            out.extend(buf)
            remaining -= len(buf)
            cur += len(buf)
        return bytes(out)

    def close(self) -> None:
        if self._file is not None:
            self._file.close()
            self._file = None
            self._idx = -1

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


# ─── extract ──────────────────────────────────────────────────────────────


def extract(game_root: Path, out_dir: Path, quiet: bool = False) -> int:
    lnk_path = index_path(game_root)
    if not lnk_path.exists():
        raise SystemExit(f"missing {lnk_path}")
    items = read_index(lnk_path)
    if not quiet:
        print(f"index: {len(items)} entries")

    out_dir.mkdir(parents=True, exist_ok=True)

    mismatched = 0
    with DataStream(game_root) as ds:
        for i, (name, dsize, offset, size) in enumerate(items, 1):
            compressed = ds.read_at(offset, size)
            decompressed = lzss_decompress(compressed)
            if len(decompressed) != dsize:
                mismatched += 1
                print(
                    f"!! size mismatch: {name}: declared {dsize}, "
                    f"got {len(decompressed)}",
                    file=sys.stderr,
                )
            target = out_dir / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(decompressed)
            if not quiet and (i % 100 == 0 or i == len(items)):
                print(f"  {i:>5}/{len(items)}  {name}")

    if mismatched:
        print(f"⚠️  {mismatched} entries had declared/actual size mismatch")
    return len(items)


# ─── list-only mode ───────────────────────────────────────────────────────


def list_index(game_root: Path) -> None:
    items = read_index(index_path(game_root))
    print(f"{'name':<50}  {'offset':>11}  {'comp':>8}  {'uncomp':>10}")
    print("─" * (50 + 11 + 8 + 10 + 6))
    for name, dsize, offset, size in items:
        print(f"{name:<50}  {offset:>11}  {size:>8}  {dsize:>10}")
    print(f"\n{len(items)} entries.")


# ─── validation ───────────────────────────────────────────────────────────


def sha256_dir(root: Path) -> dict[str, str]:
    """Map relative-path → sha256 for every regular file under root."""
    out: dict[str, str] = {}
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        h = hashlib.sha256(p.read_bytes()).hexdigest()
        out[str(p.relative_to(root))] = h
    return out


def validate_against(game_root: Path, ref_repo: Path) -> int:
    """Extract via ours and via recettear-repacker, diff the trees."""
    if not (ref_repo / "lnk_unpack.py").exists():
        raise SystemExit(
            f"not a recettear-repacker checkout: {ref_repo} "
            "(expected lnk_unpack.py)"
        )

    with tempfile.TemporaryDirectory(prefix="openrecet-validate-") as tmp:
        ours = Path(tmp) / "ours"
        theirs = Path(tmp) / "theirs"
        ours.mkdir()
        theirs.mkdir()

        print("[validate] extracting via openrecet…")
        extract(game_root, ours, quiet=True)
        print("[validate] extracting via recettear-repacker…")
        subprocess.run(
            ["python3", str(ref_repo / "lnk_unpack.py"),
             str(game_root), str(theirs)],
            check=True, stdout=subprocess.DEVNULL,
        )

        print("[validate] hashing both trees…")
        a = sha256_dir(ours)
        b = sha256_dir(theirs)

        keys = set(a) | set(b)
        diffs = []
        for k in sorted(keys):
            if a.get(k) != b.get(k):
                diffs.append((k, a.get(k), b.get(k)))

        if diffs:
            print(f"[validate] {len(diffs)} differences:")
            for k, ah, bh in diffs[:20]:
                print(f"  {k}\n    openrecet:    {ah}\n    repacker:     {bh}")
            if len(diffs) > 20:
                print(f"  … {len(diffs) - 20} more")
            return 1
        print(f"[validate] ✓ identical ({len(a)} files)")
        return 0


# ─── cli ──────────────────────────────────────────────────────────────────


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("game_root", type=Path,
                    help="dir containing lnkdatas.bin + bin/data*.bin")
    ap.add_argument("out_dir", type=Path, nargs="?",
                    help="output directory (omit with --list)")
    ap.add_argument("--list", action="store_true",
                    help="print the index without extracting")
    ap.add_argument("--validate-against", type=Path, default=None,
                    metavar="REF_REPO",
                    help="path to a recettear-repacker checkout to diff against")
    args = ap.parse_args()

    if args.list:
        list_index(args.game_root)
        return 0
    if args.validate_against is not None:
        return validate_against(args.game_root, args.validate_against)
    if args.out_dir is None:
        ap.error("out_dir is required unless --list or --validate-against")
    n = extract(args.game_root, args.out_dir)
    print(f"done: {n} files extracted to {args.out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
