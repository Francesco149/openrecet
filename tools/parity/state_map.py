#!/usr/bin/env python3
"""tools/parity/state_map.py — ST-00 canonical-state-map loader + byte localizer.

Loads docs/schemas/state-map-v1.json (the named-region layout of the engine's
~18 MB save/working arena) and resolves an absolute byte offset in a captured
save.dat to a meaningful locus: (scope, bank, region, element, field, class).

The save arena (disk save.dat) and the working arena share ONE layout
(docs/findings/save-working-arena.md): a 0x0b10 shared header followed by 100
banks of 0x2dfc8 bytes. So a single map localizes any offset in either. This is
the ST-01 save-pillar producer's attribution layer (roadmap M1 — "report a
mutation at the correct anchor, REGION, object, and FIELD") and is reusable by
the later ST-02 encoder / ST-04 first-divergence report.

FAIL SAFE, not fail closed: an offset with no named region resolves to
`(unmapped)` (class `unknown`) — still carrying scope/bank/dword so the report is
honest ("bank 0 dword 0x1234, unmapped") rather than a fabricated name. Pure
Python, no third-party deps — unit-testable in isolation.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import NamedTuple, Optional

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
DEFAULT_MAP = ROOT / "docs/schemas/state-map-v1.json"

MAP_SCHEMA_VERSION = 1

# element byte width per declared type (see state-map-v1.json regions[].type).
_ELEM_BYTES = {
    "u32": 4, "i32": 4, "f32": 4,
    "u16": 2, "i16": 2,
    "u16x2": 4, "i16x2": 4, "i16_in_dword": 4,
    "char": 1, "i8": 1, "u8": 1,
}


class StateMapError(Exception):
    """A malformed/incompatible state map, or an out-of-arena offset."""


class Locus(NamedTuple):
    """Where an absolute arena byte offset lives, named. `region` is
    `(unmapped)` when no named region covers the offset (class `unknown`)."""

    off: int              # absolute arena byte offset (0-indexed)
    scope: str            # "header" | "bank"
    bank: Optional[int]   # bank index (None for header)
    within: int           # byte offset within the scope (header/bank)
    dword: int            # dword index within the scope (within // 4)
    region: str           # named region, or "(unmapped)"
    field_class: str      # region class, or "unknown"
    element_index: Optional[int]  # array element index within the region, or None
    note: str

    def path(self) -> str:
        """A compact state-tree path for a proof `first_divergence.path`, e.g.
        `bank0/occupied_playtime`, `bank0/closeness[37]`, `header/last_slot_used`,
        `bank0/dword0x0abc(unmapped)`."""
        base = "header" if self.scope == "header" else f"bank{self.bank}"
        if self.region == "(unmapped)":
            return f"{base}/dword0x{self.dword:04x}(unmapped)"
        if self.element_index is not None:
            return f"{base}/{self.region}[{self.element_index}]"
        return f"{base}/{self.region}"


def _region_span(reg: dict) -> tuple[int, int]:
    """(start_byte, size_bytes) of a region WITHIN its scope. `byte_off` takes
    precedence (byte-addressed fields); else `dword`*4. Size = count * elem_bytes."""
    if "byte_off" in reg:
        start = int(reg["byte_off"])
        elem = int(reg.get("bytes", 1))
    else:
        start = int(reg["dword"]) * 4
        elem = _ELEM_BYTES.get(reg.get("type", "u32"), 4)
    size = elem * int(reg.get("count", 1))
    return start, size


class StateMap:
    """The parsed state map with an offset→Locus resolver."""

    def __init__(self, doc: dict):
        if doc.get("schema_version") != MAP_SCHEMA_VERSION:
            raise StateMapError(
                f"state map schema_version {doc.get('schema_version')!r} "
                f"!= {MAP_SCHEMA_VERSION}")
        arena = doc.get("arena") or {}
        self.arena_bytes = int(arena["bytes"])
        self.header_bytes = int(arena["header_bytes"])
        self.bank_stride = int(arena["bank_stride_bytes"])
        self.bank_count = int(arena["bank_count"])
        regions = doc.get("regions") or {}
        # Each scope keeps a list of (start, end, elem_bytes, region) sorted by
        # (size asc) so the MOST SPECIFIC region covering an offset wins — a
        # byte-field nested inside a dword region resolves to the byte field.
        self._header = self._prep(regions.get("header") or [])
        bank = list(regions.get("bank") or []) + list(regions.get("bank_byte_fields") or [])
        self._bank = self._prep(bank)

    @staticmethod
    def _prep(regs: list) -> list:
        out = []
        for r in regs:
            start, size = _region_span(r)
            if "byte_off" in r:
                elem = int(r.get("bytes", 1))
            else:
                elem = _ELEM_BYTES.get(r.get("type", "u32"), 4)
            out.append((start, start + size, elem, r))
        # smaller regions first ⇒ nested/specific wins on a tie
        out.sort(key=lambda t: (t[1] - t[0], t[0]))
        return out

    @classmethod
    def load(cls, path=DEFAULT_MAP) -> "StateMap":
        p = Path(path)
        try:
            doc = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise StateMapError(f"cannot load state map {p}: {exc}") from exc
        return cls(doc)

    def locate(self, off: int) -> Locus:
        """Resolve absolute arena byte offset `off` (0-indexed) to a named Locus.
        Raises StateMapError if off is outside the arena."""
        if not isinstance(off, int) or off < 0 or off >= self.arena_bytes:
            raise StateMapError(
                f"offset {off} outside arena [0,{self.arena_bytes})")
        if off < self.header_bytes:
            scope, bank, within, regs = "header", None, off, self._header
        else:
            rel = off - self.header_bytes
            bank = rel // self.bank_stride
            within = rel % self.bank_stride
            scope, regs = "bank", self._bank
        dword = within // 4
        for start, end, elem, r in regs:
            if start <= within < end:
                idx = (within - start) // elem
                elem_index = idx if int(r.get("count", 1)) > 1 else None
                return Locus(off, scope, bank, within, dword, r["name"],
                             r.get("class", "unknown"), elem_index, r.get("note", ""))
        return Locus(off, scope, bank, within, dword, "(unmapped)", "unknown", None, "")
