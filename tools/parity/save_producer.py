#!/usr/bin/env python3
"""tools/parity/save_producer.py — the `save` pillar PRODUCER.

The save pillar compares the SAVE FILE each side WRITES during a scenario against
its retail counterpart, byte-for-byte, and localizes the first divergence to a
named region of the canonical state map (ST-00). Unlike the per-frame pillars
(pixels/render_program), a save is a single scenario-scoped artifact: the ~18 MB
save arena the game dumps to save.dat on a commit.

The capture layer already exists (survey 2026-07-16, no new engine/agent work):
a `scenario-test <scen> --target both` drive on a save-committing scenario leaves
two byte-comparable save.dat files —

    <run>/openrecet/saveout/save.dat   (port, via --save-write-dir)
    <run>/retail/saveout/save.dat      (retail, via the CreateFileW/A Frida
                                        redirect hook → the same sandbox)

both seeded from the SAME {savefile} input, so the untouched storage banks match
and any diff is a real port↔retail gap in the committed state (or the shared
header). This producer is the missing COMPARATOR: read the two files → a
normalized `save-metrics.json` the EP-04 `save` adapter (save.py) adjudicates.

Split like pixel_producer so the truth-defining core is testable with NO Windows
dependency:

  * compare_saves(port, retail, state_map, source)  pure: two byte buffers → doc.
  * produce(port_dat, retail_dat, out, ...)          driver: read files → write doc.
  * produce_from_run_dir(run_dir, out, ...)           locate the two saveout/save.dat.

FAIL CLOSED: a missing file or a wrong-size buffer (not exactly the 18,838,832-byte
arena) RAISES — the producer never emits `identical: true` from absent/partial
evidence.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Optional

import numpy as np

from .fingerprint import sha256_file
from .observations import OBS_SCHEMA_VERSION
from .state_map import StateMap

# The whole save arena (src/save_bank.h SAVE_BANK_ARENA_BYTES). A save.dat that
# isn't exactly this is not the raw arena dump this pillar compares.
SAVE_ARENA_BYTES = 18838832

# Bound on how many differing offsets we localize into the per-region summary, so
# a pathological all-different pair can't spin for minutes. Real port↔retail
# gaps are a few thousand bytes; this only guards the degenerate case.
_SUMMARY_LOCALIZE_CAP = 1_000_000


class SaveProducerError(Exception):
    """A fatal producer condition: a missing save.dat, or a buffer that isn't the
    exact 18,838,832-byte arena. The CLI turns it into exit 2."""


def _dword_at(buf: bytes, off: int) -> int:
    """The u32 (LE) of the dword-aligned window containing absolute byte `off`,
    for a human-meaningful value in the report. `off` is aligned DOWN to 4."""
    base = off - (off % 4)
    if base + 4 > len(buf):
        return int(buf[base])  # tail; shouldn't happen for an in-arena offset
    return int.from_bytes(buf[base:base + 4], "little")


def compare_saves(port: bytes, retail: bytes, state_map: StateMap, *,
                  source: Optional[dict] = None) -> dict:
    """Compare two raw save arenas → the normalized `save-metrics.json` doc.

    FAIL CLOSED on a size mismatch. `differ` semantics mirror the project's other
    producers: retail is the ground truth (A), port is the subject (B). The doc
    carries the first divergence (localized to a named region via `state_map`) and
    a per-region summary so the M1 report names the correct region+field."""
    if len(port) != SAVE_ARENA_BYTES:
        raise SaveProducerError(
            f"port save is {len(port)} bytes, not the {SAVE_ARENA_BYTES}-byte arena")
    if len(retail) != SAVE_ARENA_BYTES:
        raise SaveProducerError(
            f"retail save is {len(retail)} bytes, not the {SAVE_ARENA_BYTES}-byte arena")

    pa = np.frombuffer(port, dtype=np.uint8)
    ra = np.frombuffer(retail, dtype=np.uint8)
    diff_mask = pa != ra
    ndiff = int(diff_mask.sum())

    doc = {
        "schema_version": OBS_SCHEMA_VERSION,
        "pillar": "save",
        "arena_bytes": SAVE_ARENA_BYTES,
        "identical": ndiff == 0,
        "ndiff": ndiff,
    }
    if source is not None:
        doc["source"] = source

    if ndiff == 0:
        doc["first_divergence"] = None
        doc["region_summary"] = []
        return doc

    diff_offsets = np.nonzero(diff_mask)[0]
    first_off = int(diff_offsets[0])
    loc = state_map.locate(first_off)
    doc["first_divergence"] = {
        "byte_off": first_off,
        "scope": loc.scope,
        "bank": loc.bank,
        "dword": loc.dword,
        "region": loc.region,
        "class": loc.field_class,
        "element_index": loc.element_index,
        "path": loc.path(),
        "note": loc.note,
        "port_byte": int(port[first_off]),
        "retail_byte": int(retail[first_off]),
        "port_dword_hex": f"0x{_dword_at(port, first_off):08x}",
        "retail_dword_hex": f"0x{_dword_at(retail, first_off):08x}",
    }

    # per-region summary: bucket by (scope, region) — COLLAPSING array elements and
    # banks — so "6836 bytes across 3 regions, ranking_records over banks 1-99" reads
    # clearly instead of one bucket per (element, bank). Each bucket carries how many
    # banks it spans (a systematic all-banks divergence vs a single-slot one).
    truncated = len(diff_offsets) > _SUMMARY_LOCALIZE_CAP
    buckets: dict = {}
    for off in diff_offsets[:_SUMMARY_LOCALIZE_CAP].tolist():
        l = state_map.locate(off)
        key = (l.scope, l.region)
        b = buckets.get(key)
        if b is None:
            buckets[key] = {"scope": l.scope, "region": l.region, "class": l.field_class,
                            "ndiff": 1, "first_byte_off": off, "_banks": set()}
        else:
            b["ndiff"] += 1
        if l.bank is not None:
            buckets[key]["_banks"].add(l.bank)
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


# ── drivers ──────────────────────────────────────────────────────────────────

def produce(port_dat, retail_dat, out_path, *, state_map: Optional[StateMap] = None,
            stamp_source: bool = True) -> tuple[dict, Path]:
    """Compare two save.dat files → write `out_path` (save-metrics.json). Stamps
    `source` = each file's SHA-256 so a proof binds the doc to the exact saves."""
    port_dat, retail_dat = Path(port_dat), Path(retail_dat)
    for p, role in ((port_dat, "port"), (retail_dat, "retail")):
        if not p.exists():
            raise SaveProducerError(f"no {role} save.dat at {p}")
    sm = state_map or StateMap.load()
    source = None
    if stamp_source:
        source = {"port_save_sha256": sha256_file(port_dat),
                  "retail_save_sha256": sha256_file(retail_dat)}
    doc = compare_saves(port_dat.read_bytes(), retail_dat.read_bytes(), sm, source=source)
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(doc))
    return doc, out_path


def saveout_pair(run_dir) -> tuple[Path, Path]:
    """The (port, retail) save.dat paths a `--target both` drive leaves under
    `run_dir`. Raises if either is absent (fail closed)."""
    run_dir = Path(run_dir)
    port = run_dir / "openrecet" / "saveout" / "save.dat"
    retail = run_dir / "retail" / "saveout" / "save.dat"
    for p, role in ((port, "port"), (retail, "retail")):
        if not p.exists():
            raise SaveProducerError(
                f"no {role} save.dat at {p} — did the drive reach the save commit? "
                f"(scenario-test <scen> --target both, a save-committing scenario)")
    return port, retail


def produce_from_run_dir(run_dir, out_path, *,
                         state_map: Optional[StateMap] = None) -> tuple[dict, Path]:
    """Locate the two saveout/save.dat under a `--target both` run dir and produce
    `out_path`."""
    port, retail = saveout_pair(run_dir)
    return produce(port, retail, out_path, state_map=state_map)
