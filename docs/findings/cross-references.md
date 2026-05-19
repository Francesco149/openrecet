# Cross-reference projects

Three prior RE projects on Recettear, cloned to `/opt/src/` as siblings.
None duplicate our scope (a full drop-in `recettear.exe` reimplementation),
but each contributes pieces we'd otherwise have to derive.

## `/opt/src/recettear-repacker` — UnrealPowerz

**What it gives us:** complete spec for the asset archive format
(`bin/data*.bin` + `lnkdatas.bin` + `bmpdata.bin`). Python scripts that
unpack and repack. Verified to still work on current Steam build
(2026-05-19: 1188 files, 440 MB extracted — matches the README).

Files of interest:
- `header.py` — `lnkdatas.bin` entry layout (big-endian, 128-byte name + 3 × int32).
- `lnk_unpack.py` — custom LZSS variant (12-bit back-distance, MSB-first ctrl byte).
- `bmp_unpack.py` — LZW for `bmpdata.bin`.
- `crc.py` — CRC routine (need to read in detail; possibly the engine's
  string hash for path lookup).

Our format doc: [`../formats/data-bin.md`](../formats/data-bin.md).
Our extractor: `tools/extract/data-bin.py` (we own a clean reimplementation
so the format lives in our repo; validated by diff against the upstream).

## `/opt/src/RecettearXTools` — ribeena

**What it gives us:** `.x` ↔ USD converter via Blender 4.1. We already
know the `.x` format from our own probe (stock DX retained-mode templates),
but their scripts double-check our understanding and may reveal Recettear-
specific conventions (frame nicknames, collision flags, specular-color
handling) — see `_speculars.json` / `_frames.json` sidecars they produce.

Files of interest:
- `x_file_parser.py` — their text-`.x` tokenizer.
- `x_file_writer.py` — round-trip writer.
- `usd_exporter.py` — USD output.

Animation note: their README says animation isn't supported yet — so if
Recettear uses `AnimationSet`/`AnimationKey` templates anywhere, we're on
our own. Our scan of `xfile/city/dun_city00.x` shows no animation
templates, but we should scan the full tree.

## `/opt/src/FancyScreenPatchForRecettear` — just-harry

**What it gives us:** runtime patcher for widescreen and render fixes.
Crucially, this tells us **specific code addresses and patch sites** in
the original `recettear.exe`. Even when we don't use the patches, the
addresses are signposts:

- Wherever the patcher writes, that's an engine entry point worth
  understanding.
- Resolution-clamping code, presentation parameters, viewport math —
  all of which we need to reimplement.

To read next: the patcher script(s) — likely PowerShell + a manifest of
`(offset, original_bytes, new_bytes)` tuples. We can mine that for code
landmarks to focus our Ghidra work.

## Licensing / attribution

These are independent third-party projects. Our project doesn't redistribute
them or include their code. We:

- Cite them in `docs/formats/*.md` where they contributed the format spec.
- Write our own implementations matching the verified spec.
- Note them in `README.md` under "References" once those references stabilize.

If any author wants to be credited differently or doesn't want their work
referenced, we'll honor that — these are pull-references, not copies.
