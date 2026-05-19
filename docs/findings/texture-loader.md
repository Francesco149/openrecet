# Texture loader (`FUN_0047193c`)

**Status:** ported (2026-05-20). OpenRecet now matches the engine flow:
`src/sprite.c:sprite_load` does the disk-first / storage-fallback
lookup, sniffs the buffer, and dispatches to our `src/bmp.c` (with
green color key) or `src/tga.c` (uncompressed + RLE). Pixel-perfect
against a reference Python decode on the shipping `bmp/ivent/*.tga`
storage entries (max diff 0/255) plus synthetic disk + RLE + BMP-key
fixtures. The one engine path still TODO is D3DX-style resampling to
`expected_w`/`expected_h` — every audited asset ships at native
resolution.

## What the original does

`FUN_0047193c(blend_mode, slot_ptr, path, expected_w, expected_h)` is the
texture-loading entry point. Callers include `FUN_004733d5` ("read
titletex ok") and the per-system initialisers.

Sequence:

1. Stash `expected_w`/`expected_h` and `blend_mode` into the texture slot.
2. Allocate a working buffer of `expected_w * expected_h * 8` bytes
   (suggests room for mip generation; D3DX uses this for its scratch).
3. **Scan the path string for a `.b` / `.B` extension** — if present, the
   blend-mode arg gets overwritten with `0xff00ff00` (D3DCOLOR magenta-
   like: A=0xff, R=0x00, G=0xff, B=0x00). This is the **green color key
   for BMP assets**. TGAs skip this and use their alpha channel directly.
4. **Try the path on disk first** via `FUN_005038b0(name, "rb")` — that's
   a thin wrapper over `fopen` (it forwards to `FUN_00503890(name, mode,
   0x40)`, the 3-arg helper with a 0x40 buffer-size hint). On success,
   `FUN_004341d4` (size via `fseek SEEK_END` + `ftell`), `FUN_00503734`
   (fread), `FUN_005036de` (fclose).
5. **If the disk open returned NULL,** fall back to `FUN_004346bf(name,
   buf)` — the storage subsystem read (bmpdata overlay first, lnkdatas
   archive second). The size returned is the decompressed payload
   length.
6. Hand the resulting buffer (from either path) to `FUN_004cd30e(...)`
   — **`D3DXCreateTextureFromFileInMemoryEx`** (identified by the
   15-arg signature and the error-code switch: `D3DXERR_INVALIDDATA =
   0x88760B59`, `D3DERR_OUTOFVIDEOMEMORY = 0x8876086C`, etc.).

**Correction from the earlier draft of this doc:** the disk and storage
calls were originally written here in the opposite order (storage first,
disk fallback). Re-reading `FUN_0047193c` against `FUN_005038b0`'s
3-arg-fopen identity flipped it — disk is tried first, storage second.
The user-facing implication: the engine accepts external/mod overrides
on disk for any asset name, which is consistent with how
`recettear-repacker` works (you can ship loose files alongside the exe
and they take precedence over packed assets).

The `D3DXCreateTextureFromFileInMemoryEx` call args:

```
pDevice          DAT_073dfcbc                  (global IDirect3DDevice8 *)
pSrcData         the lnkdatas/disk buffer
SrcDataSize      its size
Width            expected_w
Height           expected_h
MipLevels        1
Usage            0
Format           D3DFMT_UNKNOWN (0)
Pool             D3DPOOL_MANAGED (1)
Filter           D3DX_DEFAULT (0xFFFFFFFF)
MipFilter        D3DX_DEFAULT (0xFFFFFFFF)
ColorKey         blend_mode (0x00000000 for TGA, 0xFF00FF00 for BMP)
pSrcInfo         NULL
pPalette         NULL
ppTexture        slot_ptr (output)
```

## Implications for the port

- We accept BMP with a green color key (`0x00FF00`) and TGA with native
  alpha. `src/tga.c` covers Type 2 (uncompressed) and Type 10 (RLE) at
  24- and 32-bit, both top-down and bottom-up; `src/bmp.c` covers 24-
  and 32-bit BI_RGB DIBs with color-key application.
- `d3dx8.dll` is not in nixpkgs and is a deprecated Microsoft package.
  We do not link against it; we have our own decoders. Pixel-identical
  output under D3DX's filter chain when resampling to `expected_w`/
  `expected_h` is still TODO — every audited asset ships at native
  resolution, so this rarely matters in practice.
- Global `DAT_073dfcbc` is the `IDirect3DDevice8 *`, distinct from
  `DAT_073dfcb8` (the `IDirect3D8 *`). Both already noted in
  `winmain-and-bootstrap.md`; record here for cross-reference.

## Open questions

- The engine *does* exercise the disk-first path in practice (mod-style
  overrides). Whether the shipping Steam build's flow ever ships a path
  that resolves only via disk (no storage entry) is still an open
  question — worth grepping for callers that use literal paths.
- The 8× working-buffer size suggests mip levels are generated even
  though `MipLevels=1` is passed — possibly a sentinel meaning "auto" in
  some D3DX revisions. Confirm by hooking the real d3dx8 call.
