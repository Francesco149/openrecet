#!/usr/bin/env python3
"""
tools/analyze/pe.py — PE32 helper for the unpacked Recettear binary.

Reusable utilities for reverse-engineering work that recurs across sessions:
mapping virtual-addresses to file offsets, dumping NUL-terminated strings
(cp932-decoded), pulling fixed-size byte/struct blobs at a VA, listing
sections.

By default points at `vendor/unpacked/recettear.unpacked.exe` (the
SteamStub-decrypted dump) — the only build we can usefully read bytes
out of. Pass `--exe PATH` to override.

Use as a CLI (subcommands below) or `from pe import PE` from sibling
scripts under `tools/analyze/`.

Subcommands:
    sections                 — list PE sections
    va2off  VA [VA ...]      — map virtual addresses to file offsets
    str     VA [VA ...]      — dump cp932-decoded NUL-terminated strings
    bytes   VA LEN           — dump LEN raw bytes at VA (hex)
    blob    VA LEN [--out F] — write LEN raw bytes at VA to file (or stdout)
    callers VA               — list `call VA` sites with the immediate pushed
                                 right before (PUSH imm8 / imm32 only — anything
                                 else is shown as ?). Useful for tracing what
                                 args a setter is called with at runtime.

Examples:
    tools/analyze/pe.py str 0x005cb38c 0x005cb3b0 0x005cb3b8
    tools/analyze/pe.py va2off 0x005c23f0
    tools/analyze/pe.py bytes 0x005c23f0 64
    tools/analyze/pe.py blob 0x005c23f0 1664 --out enemy-names.bin
    tools/analyze/pe.py callers 0x00461bf6
"""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


DEFAULT_EXE = Path("vendor/unpacked/recettear.unpacked.exe")


@dataclass
class Section:
    name: str
    vaddr: int      # RVA
    vsize: int
    raw_off: int
    raw_size: int


class PE:
    """Lazy-loaded view of a PE32 image. Maps VAs to file offsets and
    serves bytes out of the raw image. Not a full parser."""

    def __init__(self, path: Path | str = DEFAULT_EXE):
        path = Path(path)
        if not path.is_absolute():
            # Try CWD-relative first, then repo-root-relative.
            if not path.exists():
                repo_root = Path(__file__).resolve().parents[2]
                cand = repo_root / path
                if cand.exists():
                    path = cand
        self.path = path
        self.data = path.read_bytes()
        e_lfanew = struct.unpack_from("<I", self.data, 0x3C)[0]
        pe = e_lfanew
        if self.data[pe:pe + 4] != b"PE\0\0":
            raise ValueError(f"{path}: not a PE image (PE signature missing)")
        num_sections = struct.unpack_from("<H", self.data, pe + 6)[0]
        opt_hdr_size = struct.unpack_from("<H", self.data, pe + 0x14)[0]
        self.image_base = struct.unpack_from("<I", self.data, pe + 0x18 + 0x1C)[0]

        sec_off = pe + 0x18 + opt_hdr_size
        sections: list[Section] = []
        for i in range(num_sections):
            o = sec_off + i * 0x28
            name = self.data[o:o + 8].rstrip(b"\x00").decode("ascii", "replace")
            vsize = struct.unpack_from("<I", self.data, o + 0x08)[0]
            vaddr = struct.unpack_from("<I", self.data, o + 0x0C)[0]
            rsize = struct.unpack_from("<I", self.data, o + 0x10)[0]
            roff = struct.unpack_from("<I", self.data, o + 0x14)[0]
            sections.append(Section(name, vaddr, vsize, roff, rsize))
        self.sections = sections

    def va_to_off(self, va: int) -> int | None:
        """Map a virtual address (e.g. 0x005cb38c) to a file offset, or
        None if the VA isn't backed by raw bytes."""
        rva = va - self.image_base
        for s in self.sections:
            limit = max(s.vsize, s.raw_size)
            if s.vaddr <= rva < s.vaddr + limit:
                file_off = s.raw_off + (rva - s.vaddr)
                if file_off >= len(self.data):
                    return None
                return file_off
        return None

    def read(self, va: int, length: int) -> bytes:
        off = self.va_to_off(va)
        if off is None:
            raise ValueError(f"VA {va:#x} not mapped")
        return self.data[off:off + length]

    def cstring(self, va: int, *, max_len: int = 4096) -> bytes:
        """Raw NUL-terminated bytes at VA (excludes the terminator).
        Returns at most `max_len` bytes if no NUL is found within range."""
        off = self.va_to_off(va)
        if off is None:
            raise ValueError(f"VA {va:#x} not mapped")
        end = self.data.find(b"\x00", off, off + max_len)
        if end < 0:
            end = off + max_len
        return self.data[off:end]

    def cstring_sjis(self, va: int, *, max_len: int = 4096) -> str:
        """Same as cstring(), decoded as cp932 (Shift-JIS) — Recettear's
        in-game encoding. Falls back to repr() on undecodable bytes."""
        raw = self.cstring(va, max_len=max_len)
        try:
            return raw.decode("cp932")
        except UnicodeDecodeError:
            return repr(raw)

    # ─── x86 call-site scan ──────────────────────────────────────────────

    def find_callers(self, target_va: int) -> list[dict]:
        """Return every `call <target_va>` site in the .text section,
        along with the 1- or 4-byte PUSH immediate that immediately
        precedes the CALL (if any). Useful for figuring out what an
        opaque setter is called with at runtime.

        Each result is::

            {"call_va": int, "call_off": int,
             "pre_push": int | None, "pre_bytes": bytes}

        `pre_push` is None when the bytes preceding the CALL aren't a
        clean PUSH imm8 (0x6A xx) or PUSH imm32 (0x68 xx xx xx xx); in
        that case `pre_bytes` holds the raw 5 bytes for hand-decoding.
        """
        text = next((s for s in self.sections if s.name == ".text"), None)
        if text is None:
            raise ValueError("no .text section in this PE")

        results: list[dict] = []
        start = text.raw_off
        end = text.raw_off + min(text.vsize, text.raw_size)
        text_va_base = self.image_base + text.vaddr
        i = start
        while i < end - 5:
            if self.data[i] == 0xE8:  # CALL rel32
                rel = struct.unpack_from("<i", self.data, i + 1)[0]
                call_va = text_va_base + (i - start)
                if call_va + 5 + rel == target_va:
                    pre_va = max(start, i - 5)
                    pre = self.data[pre_va:i]
                    pre_push: int | None = None
                    # Last 2 bytes: PUSH imm8 ?
                    if len(pre) >= 2 and pre[-2] == 0x6A:
                        pre_push = pre[-1]
                    # Last 5 bytes: PUSH imm32 ?
                    elif len(pre) >= 5 and pre[-5] == 0x68:
                        pre_push = struct.unpack_from("<I", pre, len(pre) - 4)[0]
                    results.append({
                        "call_va":  call_va,
                        "call_off": i,
                        "pre_push": pre_push,
                        "pre_bytes": bytes(pre),
                    })
            i += 1
        return results


# ─── CLI ──────────────────────────────────────────────────────────────────


def _parse_va(s: str) -> int:
    return int(s, 0)


def cmd_sections(pe: PE, args: argparse.Namespace) -> int:
    print(f"ImageBase 0x{pe.image_base:08x}  ({pe.path})")
    print(f"{'name':<10} {'VA':>10}  {'VSize':>10}  {'RawOff':>10}  {'RawSize':>10}")
    for s in pe.sections:
        print(
            f"{s.name:<10} 0x{pe.image_base + s.vaddr:08x}  "
            f"0x{s.vsize:08x}  0x{s.raw_off:08x}  0x{s.raw_size:08x}"
        )
    return 0


def cmd_va2off(pe: PE, args: argparse.Namespace) -> int:
    rc = 0
    for va in args.vas:
        off = pe.va_to_off(va)
        if off is None:
            print(f"{va:#010x}: not mapped")
            rc = 1
        else:
            print(f"{va:#010x} → file offset 0x{off:x}")
    return rc


def cmd_str(pe: PE, args: argparse.Namespace) -> int:
    rc = 0
    for va in args.vas:
        try:
            raw = pe.cstring(va, max_len=args.max_len)
        except ValueError as e:
            print(f"{va:#010x}: {e}")
            rc = 1
            continue
        try:
            decoded = raw.decode("cp932")
            print(f"{va:#010x}: {raw!r}  ::  {decoded!r}")
        except UnicodeDecodeError:
            print(f"{va:#010x}: {raw!r}  ::  <undecodable cp932>")
    return rc


def cmd_bytes(pe: PE, args: argparse.Namespace) -> int:
    try:
        raw = pe.read(args.va, args.length)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1
    width = 16
    for i in range(0, len(raw), width):
        chunk = raw[i:i + width]
        hex_part = " ".join(f"{b:02x}" for b in chunk)
        ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
        print(f"{args.va + i:#010x}  {hex_part:<{width * 3}}  {ascii_part}")
    return 0


def cmd_callers(pe: PE, args: argparse.Namespace) -> int:
    hits = pe.find_callers(args.va)
    if not hits:
        print(f"no callers found for VA {args.va:#010x}")
        return 1
    print(f"{len(hits)} call site(s) for VA {args.va:#010x}:")
    for h in hits:
        if h["pre_push"] is None:
            arg = f"? (pre-bytes: {h['pre_bytes'].hex()})"
        else:
            arg = f"PUSH 0x{h['pre_push']:x}"
        print(f"  call @ 0x{h['call_va']:08x}  {arg}")
    return 0


def cmd_blob(pe: PE, args: argparse.Namespace) -> int:
    try:
        raw = pe.read(args.va, args.length)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1
    if args.out:
        Path(args.out).write_bytes(raw)
        print(f"wrote {len(raw)} bytes to {args.out}")
    else:
        sys.stdout.buffer.write(raw)
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=DEFAULT_EXE,
                    help=f"PE image to read (default: {DEFAULT_EXE})")
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("sections", help="list PE sections")

    p_va = sub.add_parser("va2off", help="map virtual address(es) to file offset(s)")
    p_va.add_argument("vas", nargs="+", type=_parse_va, metavar="VA")

    p_str = sub.add_parser("str", help="dump cp932 NUL-terminated string(s) at VA(s)")
    p_str.add_argument("vas", nargs="+", type=_parse_va, metavar="VA")
    p_str.add_argument("--max-len", type=int, default=4096,
                       help="max bytes to scan past VA for NUL (default 4096)")

    p_b = sub.add_parser("bytes", help="hex-dump LEN bytes at VA")
    p_b.add_argument("va", type=_parse_va)
    p_b.add_argument("length", type=_parse_va)

    p_blob = sub.add_parser("blob", help="raw-extract LEN bytes at VA to a file")
    p_blob.add_argument("va", type=_parse_va)
    p_blob.add_argument("length", type=_parse_va)
    p_blob.add_argument("--out", type=Path,
                        help="output file (default: write to stdout)")

    p_call = sub.add_parser("callers",
                            help="list `call VA` sites with their pushed immediate")
    p_call.add_argument("va", type=_parse_va)

    args = ap.parse_args(argv)
    pe = PE(args.exe)

    handlers = {
        "sections": cmd_sections,
        "va2off":   cmd_va2off,
        "str":      cmd_str,
        "bytes":    cmd_bytes,
        "blob":     cmd_blob,
        "callers":  cmd_callers,
    }
    return handlers[args.cmd](pe, args)


if __name__ == "__main__":
    sys.exit(main())
