#!/usr/bin/env python3
"""tools/save_compare.py — byte-exact port↔retail save.dat compare, region-localized.

Compares the two ~18 MB save arenas a save-committing scenario writes and names
where they differ. A `scenario-test <scen> --target both` drive on a scenario
that commits a save leaves:

    <run>/openrecet/saveout/save.dat   (port, via --save-write-dir)
    <run>/retail/saveout/save.dat      (retail, via the Frida CreateFile redirect)

both seeded from the same {savefile}, so untouched banks match and any diff is a
real port↔retail gap in committed state (or an environmental field such as the
playtime counter). Each differing byte is resolved through the region map
tools/save_arena_map.json (layout from src/save_bank.h) to
`header/<region>` / `bankN/<region>[elem]`, or `bankN/dword0x…(unmapped)` when no
named region covers it (honest, never a guessed name).

This found two real port bugs on its first runs (encyclopedia store re-init in
banks 1–99, the 0xb37d house-camera flag on continue) — see
docs/findings/parity-save-producer.md. Extracted 2026-10-13 from the retired
tools/parity package (ST-01 save producer); same comparison, no proof bundle.

Usage:
    save_compare.py PORT.dat RETAIL.dat [--json OUT]
    save_compare.py --run-dir runs/scenarios/<scen>-both-<ts> [--json OUT]
    save_compare.py --scenario <scen>        # newest runs/scenarios/<scen>-both-*

Exit: 0 identical, 1 differ, 2 fatal (missing file / not an arena-sized dump).
Pure Python (no third-party deps).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import NamedTuple, Optional

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MAP = Path(__file__).resolve().parent / "save_arena_map.json"
SCEN_RUNS = ROOT / "runs" / "scenarios"

# The whole save arena (src/save_bank.h SAVE_BANK_ARENA_BYTES): a 0x0b10 shared
# header + 100 banks of 0x2dfc8 bytes. A save.dat is this arena dumped verbatim.
SAVE_ARENA_BYTES = 18838832
MAP_SCHEMA_VERSION = 1

# Guard for the per-region summary on a pathological all-different pair; real
# port↔retail gaps are a few thousand bytes. `ndiff` stays exact regardless.
SUMMARY_LOCALIZE_CAP = 1_000_000

_BLOCK = 4096

# element byte width per declared region type (save_arena_map.json regions[].type)
_ELEM_BYTES = {
    "u32": 4, "i32": 4, "f32": 4,
    "u16": 2, "i16": 2,
    "u16x2": 4, "i16x2": 4, "i16_in_dword": 4,
    "char": 1, "i8": 1, "u8": 1,
}


class SaveCompareError(Exception):
    """Fatal: a missing save.dat, a wrong-size buffer, or a bad region map."""


# ── region map ───────────────────────────────────────────────────────────────

class Locus(NamedTuple):
    """Where an absolute arena byte offset lives. `region` is `(unmapped)` when
    no named region covers the offset (class `unknown`)."""

    off: int
    scope: str                    # "header" | "bank"
    bank: Optional[int]           # None for header
    within: int                   # byte offset within header/bank
    dword: int                    # within // 4
    region: str
    field_class: str
    element_index: Optional[int]  # array element index, or None
    note: str

    def path(self) -> str:
        """`bank0/occupied_playtime`, `bank5/closeness[3]`, `header/last_slot_used`,
        `bank0/dword0x0005(unmapped)`."""
        base = "header" if self.scope == "header" else f"bank{self.bank}"
        if self.region == "(unmapped)":
            return f"{base}/dword0x{self.dword:04x}(unmapped)"
        if self.element_index is not None:
            return f"{base}/{self.region}[{self.element_index}]"
        return f"{base}/{self.region}"


def _region_span(reg: dict) -> tuple[int, int, int]:
    """(start_byte, size_bytes, elem_bytes) within the region's scope. `byte_off`
    (byte-addressed fields) takes precedence over `dword`."""
    if "byte_off" in reg:
        start = int(reg["byte_off"])
        elem = int(reg.get("bytes", 1))
    else:
        start = int(reg["dword"]) * 4
        elem = _ELEM_BYTES.get(reg.get("type", "u32"), 4)
    return start, elem * int(reg.get("count", 1)), elem


class ArenaMap:
    """Parsed region map with an offset → Locus resolver. Smaller regions are
    tried first, so a byte field nested in a dword region resolves to the byte
    field."""

    def __init__(self, doc: dict):
        if doc.get("schema_version") != MAP_SCHEMA_VERSION:
            raise SaveCompareError(
                f"region map schema_version {doc.get('schema_version')!r} != {MAP_SCHEMA_VERSION}")
        arena = doc.get("arena") or {}
        self.arena_bytes = int(arena["bytes"])
        self.header_bytes = int(arena["header_bytes"])
        self.bank_stride = int(arena["bank_stride_bytes"])
        regions = doc.get("regions") or {}
        self._header = self._prep(regions.get("header") or [])
        self._bank = self._prep(list(regions.get("bank") or [])
                                + list(regions.get("bank_byte_fields") or []))

    @staticmethod
    def _prep(regs: list) -> list:
        out = []
        for r in regs:
            start, size, elem = _region_span(r)
            out.append((start, start + size, elem, r))
        out.sort(key=lambda t: (t[1] - t[0], t[0]))
        return out

    @classmethod
    def load(cls, path=DEFAULT_MAP) -> "ArenaMap":
        try:
            doc = json.loads(Path(path).read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise SaveCompareError(f"cannot load region map {path}: {exc}") from exc
        return cls(doc)

    def locate(self, off: int) -> Locus:
        if not isinstance(off, int) or off < 0 or off >= self.arena_bytes:
            raise SaveCompareError(f"offset {off} outside arena [0,{self.arena_bytes})")
        if off < self.header_bytes:
            scope, bank, within, regs = "header", None, off, self._header
        else:
            rel = off - self.header_bytes
            bank, within = divmod(rel, self.bank_stride)
            scope, regs = "bank", self._bank
        dword = within // 4
        for start, end, elem, r in regs:
            if start <= within < end:
                elem_index = (within - start) // elem if int(r.get("count", 1)) > 1 else None
                return Locus(off, scope, bank, within, dword, r["name"],
                             r.get("class", "unknown"), elem_index, r.get("note", ""))
        return Locus(off, scope, bank, within, dword, "(unmapped)", "unknown", None, "")


# ── compare ──────────────────────────────────────────────────────────────────

def _diff_offsets(a: bytes, b: bytes):
    """Yield every differing byte offset (equal-length inputs), skipping equal
    4 KiB blocks at C speed."""
    for base in range(0, len(a), _BLOCK):
        pa, pb = a[base:base + _BLOCK], b[base:base + _BLOCK]
        if pa == pb:
            continue
        for i, (x, y) in enumerate(zip(pa, pb)):
            if x != y:
                yield base + i


def _dword_at(buf: bytes, off: int) -> int:
    base = off - (off % 4)
    return int.from_bytes(buf[base:base + 4], "little")


def compare_saves(port: bytes, retail: bytes, amap: ArenaMap, *,
                  source: Optional[dict] = None) -> dict:
    """Two raw save arenas → a report dict. Raises on a size mismatch (never
    reports `identical` from partial evidence). Retail is the reference."""
    for buf, role in ((port, "port"), (retail, "retail")):
        if len(buf) != SAVE_ARENA_BYTES:
            raise SaveCompareError(
                f"{role} save is {len(buf)} bytes, not the {SAVE_ARENA_BYTES}-byte arena")
    doc: dict = {"arena_bytes": SAVE_ARENA_BYTES}
    if source is not None:
        doc["source"] = source

    ndiff = 0
    first = None
    buckets: dict = {}
    truncated = False
    for off in _diff_offsets(port, retail):
        ndiff += 1
        if first is None:
            first = off
        if ndiff > SUMMARY_LOCALIZE_CAP:
            truncated = True
            continue
        loc = amap.locate(off)
        key = (loc.scope, loc.region)
        b = buckets.get(key)
        if b is None:
            b = buckets[key] = {"scope": loc.scope, "region": loc.region,
                                "class": loc.field_class, "ndiff": 0,
                                "first_byte_off": off, "_banks": set()}
        b["ndiff"] += 1
        if loc.bank is not None:
            b["_banks"].add(loc.bank)

    doc["identical"] = ndiff == 0
    doc["ndiff"] = ndiff
    if first is None:
        doc["first_divergence"] = None
        doc["region_summary"] = []
        return doc

    loc = amap.locate(first)
    doc["first_divergence"] = {
        "byte_off": first, "scope": loc.scope, "bank": loc.bank, "dword": loc.dword,
        "region": loc.region, "class": loc.field_class,
        "element_index": loc.element_index, "path": loc.path(), "note": loc.note,
        "port_byte": port[first], "retail_byte": retail[first],
        "port_dword_hex": f"0x{_dword_at(port, first):08x}",
        "retail_dword_hex": f"0x{_dword_at(retail, first):08x}",
    }
    summary = []
    for b in buckets.values():
        banks = sorted(b.pop("_banks"))
        b["n_banks"] = len(banks)
        if banks:
            b["bank_min"], b["bank_max"] = banks[0], banks[-1]
        summary.append(b)
    doc["region_summary"] = sorted(summary, key=lambda r: r["first_byte_off"])
    if truncated:
        doc["region_summary_truncated"] = True
    return doc


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compare_files(port_dat, retail_dat, *, amap: Optional[ArenaMap] = None) -> dict:
    port_dat, retail_dat = Path(port_dat), Path(retail_dat)
    for p, role in ((port_dat, "port"), (retail_dat, "retail")):
        if not p.is_file():
            raise SaveCompareError(f"no {role} save.dat at {p}")
    source = {"port_save": str(port_dat), "retail_save": str(retail_dat),
              "port_save_sha256": _sha256(port_dat),
              "retail_save_sha256": _sha256(retail_dat)}
    return compare_saves(port_dat.read_bytes(), retail_dat.read_bytes(),
                         amap or ArenaMap.load(), source=source)


def saveout_pair(run_dir) -> tuple[Path, Path]:
    """(port, retail) save.dat a `--target both` drive leaves under run_dir."""
    run_dir = Path(run_dir)
    port = run_dir / "openrecet" / "saveout" / "save.dat"
    retail = run_dir / "retail" / "saveout" / "save.dat"
    for p, role in ((port, "port"), (retail, "retail")):
        if not p.is_file():
            raise SaveCompareError(
                f"no {role} save.dat at {p} — did the drive reach the save commit? "
                f"(scenario-test <scen> --target both on a save-committing scenario)")
    return port, retail


def latest_both_run(scenario: str, runs: Path = SCEN_RUNS) -> Path:
    cands = sorted(runs.glob(f"{scenario}-both-*"))
    if not cands:
        raise SaveCompareError(
            f"no --target both run under {runs}/{scenario}-both-* — drive it first "
            f"(scenario-test.py {scenario} --target both)")
    return cands[-1]


def format_report(doc: dict) -> str:
    if doc["identical"]:
        return f"save: IDENTICAL ({doc['arena_bytes']} bytes)"
    fd = doc["first_divergence"]
    lines = [
        f"save: DIFFER — {doc['ndiff']} / {doc['arena_bytes']} bytes",
        f"  first @ byte {fd['byte_off']} = {fd['path']} ({fd['class']}): "
        f"port {fd['port_byte']} ({fd['port_dword_hex']}) vs retail "
        f"{fd['retail_byte']} ({fd['retail_dword_hex']})",
        "  region summary (first byte, region, bytes, banks):",
    ]
    for r in doc["region_summary"]:
        banks = ""
        if r.get("n_banks"):
            banks = (f"bank {r['bank_min']}" if r["n_banks"] == 1
                     else f"banks {r['bank_min']}-{r['bank_max']} ({r['n_banks']})")
        lines.append(f"    {r['first_byte_off']:>9}  {r['scope']}/{r['region']:<28} "
                     f"{r['ndiff']:>7}  {banks}")
    if doc.get("region_summary_truncated"):
        lines.append(f"    (summary truncated at {SUMMARY_LOCALIZE_CAP} bytes)")
    return "\n".join(lines)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("port", nargs="?", type=Path, help="port save.dat")
    ap.add_argument("retail", nargs="?", type=Path, help="retail save.dat")
    ap.add_argument("--run-dir", type=Path, help="a scenario-test --target both run dir")
    ap.add_argument("--scenario", help="use the newest runs/scenarios/<scenario>-both-* run")
    ap.add_argument("--map", type=Path, default=DEFAULT_MAP, help="region map JSON")
    ap.add_argument("--json", type=Path, help="also write the full report as JSON")
    args = ap.parse_args(argv)
    try:
        if args.port and args.retail:
            port, retail = args.port, args.retail
        elif args.run_dir or args.scenario:
            run = args.run_dir or latest_both_run(args.scenario)
            port, retail = saveout_pair(run)
        else:
            ap.error("give PORT RETAIL, --run-dir or --scenario")
        doc = compare_files(port, retail, amap=ArenaMap.load(args.map))
    except SaveCompareError as exc:
        print(f"save_compare: {exc}", file=sys.stderr)
        return 2
    print(format_report(doc))
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(doc, indent=1))
    return 0 if doc["identical"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
