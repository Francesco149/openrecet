#!/usr/bin/env python3
"""tools/parity/state_codec.py — ST-02 canonical VOLATILE-state encoder.

Encodes a captured frame's volatile-state fields (the once-per-frame flow-trace
STATE_VA fields — rng, player/companion actors, phase, interaction, customer
service, dialogue, title menu) into a canonical, name-keyed tree so ST-02's
Merkle layer (state_merkle.py) hashes semantically-equal state equally and
localizes a divergence to an exact (subsystem, field) leaf.

Two authoritative inputs, no duplication:
  * the SUBSYSTEM TREE — docs/schemas/state-volatile-v1.json (the R3 grouping).
  * each field's va+TYPE — tools/flow/retail_fields.json (the single source of
    truth; a schema field absent there is a fail-closed error, so the grouping
    can never drift from the capture spec).

Canonical value encoding (the project's x87-bit-invariant rule — no epsilon):
  i32 / u32 / hex -> 4 bytes  pack('<I', v & 0xFFFFFFFF)   (the 32-bit value;
      a signed-vs-unsigned repr mismatch across the two capture paths can't read
      as a divergence when the bits match)
  f32             -> 4 bytes  pack('<f', v)                 (IEEE-754 bit pattern;
      v arrives already collapsed to its f32 by orv3_state._norm_f32)

FAIL CLOSED: an unknown type, a non-numeric value, or a schema field missing from
retail_fields.json RAISES (never silently skipped)."""
from __future__ import annotations

import json
import struct
from collections import OrderedDict
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parent.parent.parent
SCHEMA_PATH = ROOT / "docs/schemas/state-volatile-v1.json"
FIELDS_JSON = ROOT / "tools/flow/retail_fields.json"


class StateCodecError(Exception):
    """A fatal encoder condition: an unknown field/type or a non-numeric value.
    The producer/CLI turns it into a fail-closed exit (never a silent pass)."""


def _reg_entry(reg: dict, va_hex: str):
    """retail_fields.json keys VAs as hex strings ('0x48670f'); accept a few forms."""
    va = int(va_hex, 16) if va_hex.lower().startswith("0x") else int(va_hex)
    return reg.get(hex(va)) or reg.get(str(va)) or reg.get(f"0x{va:x}")


class StateSchema:
    """The loaded volatile-state map: the ordered subsystem tree + each field's
    type (resolved from retail_fields.json) + the benign exclusions."""

    def __init__(self, doc: dict, field_types: "OrderedDict[str, dict]"):
        self.schema_version = int(doc["schema_version"])
        self._subsystems: "OrderedDict[str, list[tuple[str, str]]]" = OrderedDict()
        benign = {(b["subsystem"], b["field"]) for b in doc.get("benign", [])}
        self.benign = benign
        for sub, spec in doc["subsystems"].items():
            va = spec["va"]
            rows: list[tuple[str, str]] = []
            for fname in spec["fields"]:
                if (sub, fname) in benign:
                    continue
                key = (va, fname)
                typ = field_types.get(key)
                if typ is None:
                    raise StateCodecError(
                        f"schema field {sub}/{fname} (va {va}) not in retail_fields.json "
                        f"— the grouping drifted from the capture spec")
                rows.append((fname, typ))
            self._subsystems[sub] = rows

    @classmethod
    def load(cls, schema_path: Optional[Path] = None,
             fields_json: Optional[Path] = None) -> "StateSchema":
        doc = json.loads(Path(schema_path or SCHEMA_PATH).read_text())
        reg = json.loads(Path(fields_json or FIELDS_JSON).read_text()).get("fields", {})
        # (va, field-name) -> type, over exactly the STATE_VAS the schema names.
        ftypes: "OrderedDict[str, dict]" = OrderedDict()
        for va in {spec["va"] for spec in doc["subsystems"].values()}:
            e = _reg_entry(reg, va)
            if not e or "fields" not in e:
                raise StateCodecError(f"retail_fields.json has no field list for va {va}")
            for f in e["fields"]:
                if "name" in f and "type" in f:
                    ftypes[(va, f["name"])] = f["type"]
        return cls(doc, ftypes)

    def subsystems(self) -> list[str]:
        """Subsystem names in schema-declared order (the canonical tree order)."""
        return list(self._subsystems)

    def fields(self, subsystem: str) -> list[tuple[str, str]]:
        """Ordered [(field-name, type)] for a subsystem (benign already dropped)."""
        return self._subsystems.get(subsystem, [])

    def field_type(self, subsystem: str, field: str) -> Optional[str]:
        for name, typ in self._subsystems.get(subsystem, []):
            if name == field:
                return typ
        return None


def encode_value(typ: str, value) -> bytes:
    """One field value -> its canonical 4 bytes. FAIL CLOSED on a bad type/value."""
    try:
        if typ == "f32":
            return struct.pack("<f", float(value))
        if typ in ("i32", "u32", "hex"):
            return struct.pack("<I", int(value) & 0xFFFFFFFF)
    except (TypeError, ValueError, struct.error) as exc:
        raise StateCodecError(f"cannot encode {value!r} as {typ}: {exc}")
    raise StateCodecError(f"unknown field type {typ!r}")


def build_tree(state_fields: dict, schema: StateSchema) -> "OrderedDict[str, OrderedDict]":
    """A captured frame's flat {field: value} dict -> the canonical tree
    OrderedDict{subsystem: OrderedDict{field: (type, value, canon_bytes)}}.

    Only in-schema, non-benign fields PRESENT in `state_fields` are included, in
    schema order — a symmetrically-absent subsystem simply doesn't appear (both
    sides drop it); an asymmetric presence surfaces as a Merkle divergence. A
    subsystem with no present fields is omitted entirely."""
    tree: "OrderedDict[str, OrderedDict]" = OrderedDict()
    for sub in schema.subsystems():
        node: "OrderedDict[str, tuple]" = OrderedDict()
        for fname, typ in schema.fields(sub):
            if fname in state_fields:
                v = state_fields[fname]
                node[fname] = (typ, v, encode_value(typ, v))
        if node:
            tree[sub] = node
    return tree
