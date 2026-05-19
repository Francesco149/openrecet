# `recettear.unpacked.exe` — imports and asset layout

**Date:** 2026-05-19
**Source:** strings dump + import scan of `vendor/unpacked/recettear.unpacked.exe`
(SHA256 in `vendor/unpacked/run.json` after setup.sh, or check
`sha256sum vendor/unpacked/recettear.unpacked.exe`).

## DLL imports (static)

Only six top-level imports — a tight footprint:

| dll          | purpose                                              |
|--------------|------------------------------------------------------|
| KERNEL32.dll | files, threads, memory, errors                       |
| USER32.dll   | window, input, messages                              |
| SHELL32.dll  | trivial — likely `ShellExecuteA` for the manual      |
| WINMM.dll    | `mciSendString` etc. — opening movie & maybe BGM     |
| ole32.dll    | COM init — DirectX retained-mode `DirectXFileCreate` |
| ADVAPI32.dll | registry (probably for save-path lookup)             |

No `d3d8.dll` in static imports.

## DirectX is loaded dynamically

Despite no static link, the unpacked binary contains many `D3DERR_*` literals
and a `d3d8.dll` / `d3d8d.dll` filename string. Conclusion: the engine calls
`LoadLibraryA("d3d8.dll")` + `GetProcAddress("Direct3DCreate8")` at startup,
falling back to `d3d8d.dll` (the debug runtime) if requested. This is a
common 2002–2008-era pattern for not failing hard on systems missing the
DX8 runtime.

For our reimplementation we can do the same dance or link statically against
`-ld3d8`. To preserve drop-in semantics we should match dynamic load + the
same fallback order.

`DirectXFileCreate` (from `d3dxof.dll`, the DirectX File library) is used
for `.x` model parsing — matches what we found in the `xfile/` data.

## DirectX version: **8**

Confirmed by:
- `d3d8.dll` / `d3d8d.dll` string literals
- `D3DERR_*` constants
- `DirectXFileCreate` (DX8-era API)

No `d3d9.dll`, no `d3dx9_*.dll` references. Fixed-function pipeline (no
HLSL, no shader compilers).

## Asset layout

The strings dump references many paths that **do not exist on disk**:

- `bmp/item/item%02d.bmp` (printf-formatted)
- `bmp/item_win.tga`, `bmp/shopmode.tga`, `bmp/ivent/01recette_04.tga`
- `data/item.txt`
- `bin/se/00re/shop/re_yata.bin`

The actual `Recettear/` directory only contains the top-level dirs `bin/`,
`bgm/`, `ef/`, `manual/`, `xfile/`, `xfile2/` (and the few top-level `.bin`
files). Therefore **`bin/data###.bin` archives contain the `bmp/` (textures
as TGA), `data/` (gameplay text files), and likely other resource trees.**

That's the priority extractor for phase 2 — cracking the archive format
unlocks ~all 2D art, gameplay tables (item stats, dialogue, scripts), and
audio.

### What we know about asset formats from strings

- **Textures: TGA + BMP** — both formats used. TGA likely for sprites with
  transparency; BMP for opaque backgrounds. UI is TGA.
- **Game data: plain text** — `data/item.txt` is a text file. Strongly
  suggests other tables (`data/customer.txt`, `data/dungeon.txt`, etc.)
  follow the same pattern. We won't know format details until we extract
  one, but text-based is excellent news for both RE and modding.
- **SFX**: `bin/se/<group>/<subgroup>/<name>.bin` — already on disk
  outside the archives.

## Strings sample — gameplay-relevant

The dump contains lots of English UI text ("At this level, you may fuse
items.", "Can't use items during Dark Soul!", "Pick the items you wish to
bring with you."). Most strings appear inline in `.rdata` — the engine does
not use a separate `.po`/string-table system. That makes the i18n story
simple for us, and means the Japanese version's strings (if we wanted
those) would just be a different binary build.

## Implications for OpenRecet

1. **Target DX8 directly** in `src/`. Use `wineWow64Packages.stagingFull`'s
   d3d8 implementation under wine for testing; on Windows the system
   d3d8.dll will be present.
2. **Mimic dynamic load + fallback to `d3d8d.dll`** when our exe boots, so
   `recet.ini`'s debug-runtime expectations stay valid.
3. **Asset archive format is the next critical extractor.** Its format
   will be visible in the Ghidra decompilation as the function that opens
   `bin/data%03d.bin`, reads a header, and demuxes the path map.
4. **No shader work needed.** Fixed-function rendering = no HLSL compiler
   in our toolchain.
