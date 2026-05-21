#!/usr/bin/env python3
"""
tools/extract/se-wavs.py — extract the SE WAV blobs from the unpacked
exe's .rsrc section.

The engine ships 110 sound-effect WAVs as a *custom* PE resource type
named "WAVE" (NOT the standard RT_WAVE / RT_RCDATA). At boot it walks
a 110-entry ID table at &DAT_005d1584 (8-byte stride: u32 resource_id
at +0 + u32 channel_flag at +4) and FindResourceA's each one out of
itself before handing the resulting blob to IDirectMusicLoader::
GetObject as a memory-backed segment.

The +4 column is the SE voice-group / AudioPath router that FUN_00499c63
reads at playback time — see engine-quirks.md #46. In shipped vendor
data every +4 cell is zero, so this extractor only consumes the +0
column. If a future build populates +4 we'll resurrect the second
column then.

This tool:
  1. Parses the .rsrc tree of vendor/unpacked/recettear.unpacked.exe
     looking under type "WAVE".
  2. Reads the SE ID table at 0x005d1584 (110 entries).
  3. For each ID present in the resource tree, dumps the blob to
     vendor/unpacked/se-extracted/NNN.wav (decimal ID — easier to
     skim than 0x13d-style hex).
  4. Prints a summary + JSON manifest mapping ID → file size + offset.

Output dir is gitignored (it lives under vendor/unpacked/). The
manifest goes to stdout — pipe it into src/audio_se_table.h
generation if needed.

Run:
    nix develop --command python3 tools/extract/se-wavs.py
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "analyze"))

from pe import PE  # noqa: E402


SE_ID_TABLE_VA    = 0x005d1584
SE_ID_TABLE_END   = 0x005d18f4
SE_ID_STRIDE      = 8        # u32 id + u32 padding
CUSTOM_TYPE_NAME  = "WAVE"   # custom (named) PE resource type


def read_se_ids(pe: PE) -> list[int]:
    raw = pe.read(SE_ID_TABLE_VA, SE_ID_TABLE_END - SE_ID_TABLE_VA)
    ids = []
    for i in range(0, len(raw), SE_ID_STRIDE):
        ids.append(struct.unpack_from("<I", raw, i)[0])
    return ids


# ─── PE .rsrc walker ────────────────────────────────────────────────────
# The PE rsrc tree is three levels: type → id → language. Each
# IMAGE_RESOURCE_DIRECTORY is followed by N entries; each entry has
# either a name pointer (high-bit-set offset to an IMAGE_RESOURCE_DIR_
# STRING_U) or a numeric ID. Leaves point at IMAGE_RESOURCE_DATA_ENTRY.


def _read_rsrc_dir_string(rsrc_bytes: bytes, off: int) -> str:
    """A name string in .rsrc is `u16 len` followed by `len` UTF-16-LE chars."""
    n = struct.unpack_from("<H", rsrc_bytes, off)[0]
    return rsrc_bytes[off + 2: off + 2 + n * 2].decode("utf-16-le")


def _walk_rsrc(rsrc_bytes: bytes, off: int, *, level: int) -> list:
    """Yield (type_key, id, lang_id, data_rva, data_size) tuples at the
    leaves. `type_key` is a string for named types, int for numeric."""
    num_named, num_id = struct.unpack_from("<HH", rsrc_bytes, off + 12)
    entries_off = off + 16
    out = []
    for i in range(num_named + num_id):
        name_or_id, child_off_or_data = struct.unpack_from(
            "<II", rsrc_bytes, entries_off + i * 8
        )
        is_named = bool(name_or_id & 0x80000000)
        if is_named:
            key = _read_rsrc_dir_string(rsrc_bytes, name_or_id & 0x7fffffff)
        else:
            key = name_or_id
        is_dir = bool(child_off_or_data & 0x80000000)
        child = child_off_or_data & 0x7fffffff
        if is_dir:
            for sub in _walk_rsrc(rsrc_bytes, child, level=level + 1):
                # Bubble keys up by level so leaves know (type, id, lang).
                if level == 0:
                    out.append((key,) + sub)
                else:
                    out.append((key,) + sub)
        else:
            # Leaf: child points at IMAGE_RESOURCE_DATA_ENTRY (16 bytes).
            data_rva, data_size, _cp, _rsvd = struct.unpack_from(
                "<IIII", rsrc_bytes, child
            )
            out.append((key, data_rva, data_size))
    return out


def collect_wave_resources(pe: PE) -> dict[int, tuple[int, int]]:
    """Walk .rsrc and return {id: (file_offset, size)} for every leaf
    under the custom "WAVE" type."""
    rsrc = next((s for s in pe.sections if s.name == ".rsrc"), None)
    if rsrc is None:
        raise SystemExit("no .rsrc section")
    rsrc_bytes = pe.read(pe.image_base + rsrc.vaddr, rsrc.raw_size)
    flat = _walk_rsrc(rsrc_bytes, 0, level=0)

    out: dict[int, tuple[int, int]] = {}
    for entry in flat:
        # Possible shapes:
        #   (type, id, data_rva, data_size)            — language-less
        #   (type, id, lang, data_rva, data_size)
        if len(entry) == 4:
            type_key, res_id, data_rva, data_size = entry
        elif len(entry) == 5:
            type_key, res_id, _lang, data_rva, data_size = entry
        else:
            continue
        if type_key != CUSTOM_TYPE_NAME:
            continue
        file_off = pe.va_to_off(pe.image_base + data_rva)
        out[res_id] = (file_off, data_size)
    return out


# ─── main ───────────────────────────────────────────────────────────────


def main() -> int:
    exe = ROOT / "vendor" / "unpacked" / "recettear.unpacked.exe"
    if not exe.exists():
        raise SystemExit(f"missing exe: {exe}")
    pe = PE(str(exe))

    ids = read_se_ids(pe)
    waves = collect_wave_resources(pe)

    out_dir = ROOT / "vendor" / "unpacked" / "se-extracted"
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = []
    written = 0
    missing = []
    for slot, res_id in enumerate(ids):
        if res_id not in waves:
            missing.append((slot, res_id))
            continue
        file_off, size = waves[res_id]
        blob = pe.data[file_off: file_off + size]
        out_name = f"se_{slot:03d}_id{res_id:04x}.wav"
        (out_dir / out_name).write_bytes(blob)
        manifest.append({
            "slot": slot,
            "id": res_id,
            "size": size,
            "file_offset": file_off,
            "out": str(out_name),
        })
        written += 1

    print(f"# extracted {written}/{len(ids)} SE WAVs to "
          f"{out_dir.relative_to(ROOT)}")
    if missing:
        print(f"# missing from .rsrc tree: {len(missing)} slots")
        for slot, res_id in missing[:5]:
            print(f"#   slot={slot:3d} id=0x{res_id:x}")
    print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
